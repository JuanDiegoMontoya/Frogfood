#include "FrogRenderer2.h"

#include "Pipelines2.h"

#include "Fvog/Rendering2.h"
#include "Fvog/Shader2.h"
#include "Fvog/detail/Common.h"
using namespace Fvog::detail;

#include "shaders/Config.shared.h"

#include "MathUtilities.h"

#include <stb_image.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <tracy/Tracy.hpp>


static glm::vec2 GetJitterOffset([[maybe_unused]] uint32_t frameIndex,
                                 [[maybe_unused]] uint32_t renderWidth,
                                 [[maybe_unused]] uint32_t renderHeight,
                                 [[maybe_unused]] uint32_t windowWidth)
{
//#ifdef FROGRENDER_FSR2_ENABLE
//  float jitterX{};
//  float jitterY{};
//  ffxFsr2GetJitterOffset(&jitterX, &jitterY, frameIndex, ffxFsr2GetJitterPhaseCount(renderWidth, windowWidth));
//  return {2.0f * jitterX / static_cast<float>(renderWidth), 2.0f * jitterY / static_cast<float>(renderHeight)};
//#else
  return {0, 0};
//#endif
}


FrogRenderer2::FrogRenderer2(const Application::CreateInfo& createInfo)
  : Application(createInfo),
    // Create constant-size buffers
    // TODO: Don't abuse the comma operator here. This is awful
    globalUniformsBuffer((Pipelines2::InitPipelineLayout(device_->device_, device_->descriptorSetLayout_), *device_), 1, "Global Uniforms"),
    shadingUniformsBuffer(*device_, 1, "Shading Uniforms"),
    shadowUniformsBuffer(*device_, 1, "Shadow Uniforms"),
    // Create the pipelines used in the application
    cullMeshletsPipeline(Pipelines2::CullMeshlets(device_->device_)),
    cullTrianglesPipeline(Pipelines2::CullTriangles(device_->device_)),
    hzbCopyPipeline(Pipelines2::HzbCopy(device_->device_)),
    hzbReducePipeline(Pipelines2::HzbReduce(device_->device_)),
    visbufferPipeline(Pipelines2::Visbuffer(device_->device_,
                                            {
                                              .colorAttachmentFormats = {{Frame::visbufferFormat}},
                                              .depthAttachmentFormat = Frame::gDepthFormat,
                                            })),
    materialDepthPipeline(Pipelines2::MaterialDepth(device_->device_,
                                                    {
                                                      .depthAttachmentFormat = Frame::materialDepthFormat,
                                                    })),
    visbufferResolvePipeline(Pipelines2::VisbufferResolve(device_->device_,
                                                          {
                                                            .colorAttachmentFormats = {
                                                              {
                                                                Frame::gAlbedoFormat,
                                                                Frame::gMetallicRoughnessAoFormat,
                                                                Frame::gNormalAndFaceNormalFormat,
                                                                Frame::gSmoothVertexNormalFormat,
                                                                Frame::gEmissionFormat,
                                                                Frame::gMotionFormat,
                                                              }},
                                                            .depthAttachmentFormat = Frame::materialDepthFormat,
                                                          })),
    shadingPipeline(Pipelines2::Shading(device_->device_,
                                        {
                                          .colorAttachmentFormats = {{Frame::colorHdrRenderResFormat}},
                                        })),
    tonemapPipeline(Pipelines2::Tonemap(device_->device_)),
    tonemapUniformBuffer(*device_, 1, "Tonemap Uniforms"),
    exposureBuffer(*device_, {}, "Exposure"),
    nearestSampler(*device_, {
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    }, "Nearest"),
    hzbSampler(*device_, {
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    }, "HZB")
    //debugTexturePipeline(Pipelines2::DebugTexture(device_->device_)),
    //debugLinesPipeline(Pipelines2::DebugLines(device_->device_)),
    //debugAabbsPipeline(Pipelines2::DebugAabbs(device_->device_)),
    //debugRectsPipeline(Pipelines2::DebugRects(device_->device_))
{
  ZoneScoped;

  int x = 0;
  int y = 0;
  const auto noise = stbi_load("textures/bluenoise32.png", &x, &y, nullptr, 4);
  assert(noise);
  noiseTexture = Fvog::CreateTexture2D(*device_, {static_cast<uint32_t>(x), static_cast<uint32_t>(y)}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::READ_ONLY, "Noise");
  noiseTexture->UpdateImageSLOW({
    .extent = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
    .data = noise,
  });
  stbi_image_free(noise);

  //Utility::LoadModelFromFileMeshlet(*device_, scene, "models/simple_scene.glb", glm::scale(glm::vec3{.5}));
  Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf", glm::scale(glm::vec3{.5}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/Main/NewSponza_Main_Blender_glTF.gltf", glm::scale(glm::vec3{1}));
  
  meshletIndirectCommand = Fvog::TypedBuffer<Fvog::DrawIndexedIndirectCommand>(*device_, {}, "Meshlet Indirect Command");
  cullTrianglesDispatchParams = Fvog::TypedBuffer<Fvog::DispatchIndirectCommand>(*device_, {}, "Cull Triangles Dispatch Params");
  viewBuffer = Fvog::TypedBuffer<ViewParams>(*device_, {}, "View Data");

  debugGpuAabbsBuffer = Fvog::Buffer(*device_, {sizeof(Fvog::DrawIndirectCommand) + sizeof(Debug::Aabb) * 100'000}, "Debug GPU AABBs");

  debugGpuRectsBuffer = Fvog::Buffer(*device_, {sizeof(Fvog::DrawIndirectCommand) + sizeof(Debug::Rect) * 100'000}, "Deug GPU Rects");

  device_->ImmediateSubmit(
    [this](VkCommandBuffer commandBuffer)
    {
      // Reset the instance count of the debug draw buffers
      debugGpuAabbsBuffer->FillData(commandBuffer, {.offset = offsetof(Fvog::DrawIndirectCommand, instanceCount), .size = sizeof(uint32_t), .data = 0});
      debugGpuRectsBuffer->FillData(commandBuffer, {.offset = offsetof(Fvog::DrawIndirectCommand, instanceCount), .size = sizeof(uint32_t), .data = 0});
      exposureBuffer.FillData(commandBuffer, {.data = std::bit_cast<uint32_t>(1.0f)});
      cullTrianglesDispatchParams->UpdateDataExpensive(commandBuffer, Fvog::DispatchIndirectCommand{0, 1, 1});
      MakeStaticSceneBuffers(commandBuffer);
    });

  OnWindowResize(windowWidth, windowHeight);
}

FrogRenderer2::~FrogRenderer2()
{
  vkDeviceWaitIdle(device_->device_);

  Pipelines2::DestroyPipelineLayout(device_->device_);
  vkDestroyDescriptorPool(device_->device_, imguiDescriptorPool_, nullptr);
}

void FrogRenderer2::OnWindowResize([[maybe_unused]] uint32_t newWidth, [[maybe_unused]] uint32_t newHeight)
{
  ZoneScoped;

//#ifdef FROGRENDER_FSR2_ENABLE
//  // create FSR 2 context
//  if (fsr2Enable)
//  {
//    if (!fsr2FirstInit)
//    {
//      ffxFsr2ContextDestroy(&fsr2Context);
//    }
//    frameIndex = 0;
//    fsr2FirstInit = false;
//    renderWidth = static_cast<uint32_t>(newWidth / fsr2Ratio);
//    renderHeight = static_cast<uint32_t>(newHeight / fsr2Ratio);
//    FfxFsr2ContextDescription contextDesc{
//      .flags = FFX_FSR2_ENABLE_DEBUG_CHECKING | FFX_FSR2_ENABLE_AUTO_EXPOSURE | FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE |
//               FFX_FSR2_ALLOW_NULL_DEVICE_AND_COMMAND_LIST | FFX_FSR2_ENABLE_DEPTH_INFINITE | FFX_FSR2_ENABLE_DEPTH_INVERTED,
//      .maxRenderSize = {renderWidth, renderHeight},
//      .displaySize = {newWidth, newHeight},
//      .fpMessage =
//        [](FfxFsr2MsgType type, const wchar_t* message)
//      {
//        char cstr[256] = {};
//        if (wcstombs_s(nullptr, cstr, sizeof(cstr), message, sizeof(cstr)) == 0)
//        {
//          cstr[255] = '\0';
//          printf("FSR 2 message (type=%d): %s\n", type, cstr);
//        }
//      },
//    };
//    fsr2ScratchMemory = std::make_unique<char[]>(ffxFsr2GetScratchMemorySizeGL());
//    ffxFsr2GetInterfaceGL(&contextDesc.callbacks, fsr2ScratchMemory.get(), ffxFsr2GetScratchMemorySizeGL(), glfwGetProcAddress);
//    ffxFsr2ContextCreate(&fsr2Context, &contextDesc);
//
//    frame.gReactiveMask = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R8_UNORM, "Reactive Mask");
//  }
//  else
//#endif
  {
    renderWidth = newWidth;
    renderHeight = newHeight;
  }

  //constexpr auto usageColorFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  //constexpr auto usageDepthFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  constexpr auto usage = Fvog::TextureUsage::ATTACHMENT_READ_ONLY;

  // Visibility buffer textures
  frame.visbuffer = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::visbufferFormat, usage, "visbuffer");
  frame.materialDepth = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::materialDepthFormat, usage, "materialDepth");
  {
    const uint32_t hzbWidth = Math::PreviousPower2(renderWidth);
    const uint32_t hzbHeight = Math::PreviousPower2(renderHeight);
    const uint32_t hzbMips = 1 + static_cast<uint32_t>(glm::floor(glm::log2(static_cast<float>(glm::max(hzbWidth, hzbHeight)))));
    frame.hzb = Fvog::CreateTexture2DMip(*device_, {hzbWidth, hzbHeight}, Frame::hzbFormat, hzbMips, Fvog::TextureUsage::GENERAL, "HZB");
  }

  // Create gbuffer textures and render info
  frame.gAlbedo = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gAlbedoFormat, usage, "gAlbedo");
  frame.gMetallicRoughnessAo = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gMetallicRoughnessAoFormat, usage, "gMetallicRoughnessAo");
  frame.gNormalAndFaceNormal = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gNormalAndFaceNormalFormat, usage, "gNormalAndFaceNormal");
  frame.gSmoothVertexNormal = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gSmoothVertexNormalFormat, usage, "gSmoothVertexNormal");
  frame.gEmission = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gEmissionFormat, usage, "gEmission");
  frame.gDepth = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gDepthFormat, usage, "gDepth");
  frame.gMotion = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gMotionFormat, usage, "gMotion");
  frame.gNormaAndFaceNormallPrev = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gNormalAndFaceNormalFormat, usage, "gNormaAndFaceNormallPrev");
  frame.gDepthPrev = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::gDepthPrevFormat, usage, "gDepthPrev");
  frame.colorHdrRenderRes = Fvog::CreateTexture2D(*device_, {renderWidth, renderHeight}, Frame::colorHdrRenderResFormat, usage, "colorHdrRenderRes");
  frame.colorHdrWindowRes = Fvog::CreateTexture2D(*device_, {newWidth, newHeight}, Frame::colorHdrWindowResFormat, usage, "colorHdrWindowRes");
  frame.colorHdrBloomScratchBuffer = Fvog::CreateTexture2DMip(*device_, {newWidth / 2, newHeight / 2}, Frame::colorHdrBloomScratchBufferFormat, 8, usage, "colorHdrBloomScratchBuffer");
  frame.colorLdrWindowRes = Fvog::CreateTexture2D(*device_, {newWidth, newHeight}, Frame::colorLdrWindowResFormat, Fvog::TextureUsage::GENERAL, "colorLdrWindowRes");

  //// Create debug views with alpha swizzle set to one so they can be seen in ImGui
  //frame.gAlbedoSwizzled = frame.gAlbedo->CreateSwizzleView({.a = Fvog::ComponentSwizzle::ONE});
  //frame.gRoughnessMetallicAoSwizzled = frame.gMetallicRoughnessAo->CreateSwizzleView({.a = Fvog::ComponentSwizzle::ONE});
  //frame.gEmissionSwizzled = frame.gEmission->CreateSwizzleView({.a = Fvog::ComponentSwizzle::ONE});
  //frame.gNormalSwizzled = frame.gNormalAndFaceNormal->CreateSwizzleView({.a = Fvog::ComponentSwizzle::ONE});
  //frame.gDepthSwizzled = frame.gDepth->CreateSwizzleView({.a = Fvog::ComponentSwizzle::ONE});
}

void FrogRenderer2::OnUpdate([[maybe_unused]] double dt)
{
  ZoneScoped;

  sceneFlattened = scene.Flatten();
  shadingUniforms.numberOfLights = (uint32_t)sceneFlattened.lights.size();

  // TODO: don't create a bunch of buffers here (instead, just upload and conditionally resize the buffers)
  const auto maxIndices = sceneFlattened.meshlets.size() * Utility::maxMeshletPrimitives * 3;
  instancedMeshletBuffer = Fvog::TypedBuffer<uint32_t>(*device_, {.count = (uint32_t)maxIndices}, "Instanced Meshlets");
  visibleMeshletIds = Fvog::TypedBuffer<uint32_t>(*device_, {.count = (uint32_t)sceneFlattened.meshlets.size()}, "Visible Meshlet IDs");

  lightBuffer = Fvog::NDeviceBuffer<Utility::GpuLight>(*device_, (uint32_t)sceneFlattened.lights.size(), "Lights");
  transformBuffer = Fvog::NDeviceBuffer<Utility::ObjectUniforms>(*device_, (uint32_t)sceneFlattened.transforms.size(), "Transforms");
  meshletBuffer = Fvog::NDeviceBuffer<Utility::Meshlet>(*device_, (uint32_t)sceneFlattened.meshlets.size(), "Meshlets");
}

void FrogRenderer2::CullMeshletsForView(VkCommandBuffer commandBuffer, const ViewParams& view, [[maybe_unused]] std::string_view name)
{
  // TODO: use multiple view buffers
  //viewBuffer->UpdateData(view);
  auto ctx = Fvog::Context(commandBuffer);
  ctx.Barrier();
  viewBuffer->UpdateDataExpensive(commandBuffer, view);

  // Clear all the fields to zero, then set the instance count to one (this way should be more efficient than a CPU-side buffer update)
  ctx.Barrier();
  constexpr auto drawCommand = Fvog::DrawIndexedIndirectCommand{
    .indexCount = 0,
    .instanceCount = 1,
    .firstIndex = 0,
    .vertexOffset = 0,
    .firstInstance = 0,
  };
  meshletIndirectCommand->UpdateDataExpensive(commandBuffer, drawCommand);

  // Clear groupCountX
  ctx.Barrier();
  cullTrianglesDispatchParams->FillData(commandBuffer, {.size = sizeof(uint32_t)});

  ctx.Barrier();
  
  ctx.BindComputePipeline(cullMeshletsPipeline);
  ctx.SetPushConstants(VisbufferPushConstants{
    .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
    .meshletDataIndex = meshletBuffer->GetDeviceBuffer().GetResourceHandle().index,
    .transformsIndex = transformBuffer->GetDeviceBuffer().GetResourceHandle().index,
    .indirectDrawIndex = meshletIndirectCommand->GetResourceHandle().index,
    .viewIndex = viewBuffer->GetResourceHandle().index,
    .hzbIndex = frame.hzb->ImageView().GetSampledResourceHandle().index,
    .hzbSamplerIndex = hzbSampler.GetResourceHandle().index,
    .cullTrianglesDispatchIndex = cullTrianglesDispatchParams->GetResourceHandle().index,
    .visibleMeshletsIndex = visibleMeshletIds->GetResourceHandle().index,
    .debugAabbBufferIndex = debugGpuAabbsBuffer->GetResourceHandle().index,
    .debugRectBufferIndex = debugGpuRectsBuffer->GetResourceHandle().index,
  });
  //Fwog::Cmd::BindStorageBuffer("MeshletDataBuffer", *meshletBuffer);
  //Fwog::Cmd::BindStorageBuffer("TransformBuffer", *transformBuffer);
  //Fwog::Cmd::BindStorageBuffer("IndirectDrawCommand", *meshletIndirectCommand);
  //Fwog::Cmd::BindUniformBuffer("PerFrameUniformsBuffer", globalUniformsBuffer);
  //Fwog::Cmd::BindStorageBuffer("ViewBuffer", viewBuffer.value());
  //Fwog::Cmd::BindStorageBuffer("MeshletVisibilityBuffer", visibleMeshletIds.value());
  //Fwog::Cmd::BindStorageBuffer("CullTrianglesDispatchParams", cullTrianglesDispatchParams.value());
  //Fwog::Cmd::BindStorageBuffer(11, debugGpuAabbsBuffer.value());
  //Fwog::Cmd::BindStorageBuffer(12, debugGpuRectsBuffer.value());
  ////Fwog::Cmd::BindUniformBuffer(6, vsmContext.uniformBuffer_);
  //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::BUFFER_UPDATE_BIT);
  //Fwog::Cmd::BindSampledImage("s_hzb", *frame.hzb, hzbSampler);
  //vsmContext.BindResourcesForCulling();
  ctx.DispatchInvocations((uint32_t)sceneFlattened.meshlets.size(), 1, 1);

  //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::COMMAND_BUFFER_BIT);
  ctx.Barrier();
  
  ctx.BindComputePipeline(cullTrianglesPipeline);
  ctx.SetPushConstants(VisbufferPushConstants{
    .meshletDataIndex = instancedMeshletBuffer->GetResourceHandle().index,
    .meshletPrimitivesIndex = primitiveBuffer->GetResourceHandle().index,
    .meshletVerticesIndex = vertexBuffer->GetResourceHandle().index,
    .meshletIndicesIndex = indexBuffer->GetResourceHandle().index,
  });
  //Fwog::Cmd::BindStorageBuffer("MeshletPrimitiveBuffer", *primitiveBuffer);
  //Fwog::Cmd::BindStorageBuffer("MeshletVertexBuffer", *vertexBuffer);
  //Fwog::Cmd::BindStorageBuffer("MeshletIndexBuffer", *indexBuffer);
  //Fwog::Cmd::BindStorageBuffer("MeshletPackedBuffer", *instancedMeshletBuffer);
  ctx.DispatchIndirect(cullTrianglesDispatchParams.value());

  //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::INDEX_BUFFER_BIT | Fwog::MemoryBarrierBit::COMMAND_BUFFER_BIT);
  ctx.Barrier();
}

void FrogRenderer2::OnRender([[maybe_unused]] double dt, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
{
  ZoneScoped;

  std::swap(frame.gDepth, frame.gDepthPrev);
  std::swap(frame.gNormalAndFaceNormal, frame.gNormaAndFaceNormallPrev);

  shadingUniforms.sunDir = glm::vec4(PolarToCartesian(sunElevation, sunAzimuth), 0);
  shadingUniforms.sunStrength = glm::vec4{sunStrength * sunColor, 0};

  auto ctx = Fvog::Context(commandBuffer);

  {
    ZoneScopedN("Update GPU Buffers");

    tonemapUniformBuffer.UpdateData(commandBuffer, tonemapUniforms);

    auto gpuMaterials = std::vector<Utility::GpuMaterial>(scene.materials.size());
    std::ranges::transform(scene.materials, gpuMaterials.begin(), [](const auto& mat) { return mat.gpuMaterial; });

    materialStorageBuffer->UpdateData(commandBuffer, gpuMaterials);
    lightBuffer->UpdateData(commandBuffer, sceneFlattened.lights);
    transformBuffer->UpdateData(commandBuffer, sceneFlattened.transforms);
    meshletBuffer->UpdateData(commandBuffer, sceneFlattened.meshlets);
    ctx.Barrier();
  }

  vkCmdBindDescriptorSets(
    commandBuffer,
    VK_PIPELINE_BIND_POINT_COMPUTE,
    Pipelines2::pipelineLayout,
    0,
    1,
    &device_->descriptorSet_,
    0,
    nullptr);

  
  vkCmdBindDescriptorSets(
    commandBuffer,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    Pipelines2::pipelineLayout,
    0,
    1,
    &device_->descriptorSet_,
    0,
    nullptr);











  
  const float fsr2LodBias = fsr2Enable ? log2(float(renderWidth) / float(windowWidth)) - 1.0f : 0.0f;
  
  const auto jitterOffset = fsr2Enable ? GetJitterOffset((uint32_t)device_->frameNumber, renderWidth, renderHeight, windowWidth) : glm::vec2{};
  const auto jitterMatrix = glm::translate(glm::mat4(1), glm::vec3(jitterOffset, 0));
  //const auto projUnjittered = glm::perspectiveZO(cameraFovY, aspectRatio, cameraNear, cameraFar);
  const auto projUnjittered = Math::InfReverseZPerspectiveRH(cameraFovyRadians, aspectRatio, cameraNearPlane);
  const auto projJittered = jitterMatrix * projUnjittered;
  
  // Set global uniforms
  const uint32_t meshletCount = (uint32_t)sceneFlattened.meshlets.size();
  const auto viewProj = projJittered * mainCamera.GetViewMatrix();
  const auto viewProjUnjittered = projUnjittered * mainCamera.GetViewMatrix();

  // TODO: this may wreak havoc on future (non-culling) systems that depend on this matrix, but we'll leave it for now
  if (executeMeshletGeneration)
  {
    globalUniforms.oldViewProjUnjittered = device_->frameNumber == 1 ? viewProjUnjittered : globalUniforms.viewProjUnjittered;
  }

  auto mainView = ViewParams{
    .oldViewProj = globalUniforms.oldViewProjUnjittered,
    .proj = projUnjittered,
    .view = mainCamera.GetViewMatrix(),
    //.viewProj = viewProjUnjittered,
    .viewProj = viewProj,
    .cameraPos = glm::vec4(mainCamera.position, 0.0),
    .viewport = {0.0f, 0.0f, static_cast<float>(renderWidth), static_cast<float>(renderHeight)},
  };

  if (updateCullingFrustum)
  {
    // TODO: the main view has an infinite projection, so we should omit the far plane. It seems to be causing the test to always pass.
    // We should probably emit the far plane regardless, but alas, one thing at a time.
    Math::MakeFrustumPlanes(viewProjUnjittered, mainView.frustumPlanes);
    debugMainViewProj = viewProjUnjittered;
  }
  globalUniforms.viewProjUnjittered = viewProjUnjittered;
  globalUniforms.viewProj = viewProj;
  globalUniforms.invViewProj = glm::inverse(globalUniforms.viewProj);
  globalUniforms.proj = projJittered;
  globalUniforms.invProj = glm::inverse(globalUniforms.proj);
  globalUniforms.cameraPos = glm::vec4(mainCamera.position, 0.0);
  globalUniforms.meshletCount = meshletCount;
  globalUniforms.maxIndices = static_cast<uint32_t>(scene.primitives.size() * 3);
  globalUniforms.bindlessSamplerLodBias = fsr2LodBias;

  globalUniformsBuffer.UpdateData(commandBuffer, globalUniforms);

  shadowUniformsBuffer.UpdateData(commandBuffer, shadowUniforms);
  
  shadingUniformsBuffer.UpdateData(commandBuffer, shadingUniforms);

  //if (device_->frameNumber == 1)
  //{
  //  vsmSun.UpdateExpensive(mainCamera.position, glm::vec3{-shadingUniforms.sunDir}, vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
  //}
  //else
  //{
  //  vsmSun.UpdateOffset(mainCamera.position);
  //}

  ctx.Barrier();

  if (executeMeshletGeneration)
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eCullMeshletsMain);
    CullMeshletsForView(commandBuffer, mainView, "Cull Meshlets Main");
  }

  ctx.ImageBarrier(*frame.visbuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrier(*frame.gDepth,    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  auto visbufferAttachment = Fvog::RenderColorAttachment{
    .texture = frame.visbuffer->ImageView(),
    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .clearValue = {~0u, ~0u, ~0u, ~0u},
  };
  auto visbufferDepthAttachment = Fvog::RenderDepthStencilAttachment{
    .texture = frame.gDepth->ImageView(),
    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .clearValue = {.depth = FAR_DEPTH},
  };

  ctx.BeginRendering({
    .name = "Main Visbuffer Pass",
    //.viewport = {VkViewport{.drawRect = {{0, 0}, {renderWidth, renderHeight}}}},
    .colorAttachments = {&visbufferAttachment, 1},
    .depthAttachment = visbufferDepthAttachment,
  });
  {
    // TIME_SCOPE_GPU(StatGroup::eMainGpu, eRenderVisbufferMain);
    ctx.BindGraphicsPipeline(visbufferPipeline);
    ctx.SetPushConstants(VisbufferPushConstants{
      .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .meshletDataIndex = meshletBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .meshletPrimitivesIndex = primitiveBuffer->GetResourceHandle().index,
      .meshletVerticesIndex = vertexBuffer->GetResourceHandle().index,
      .meshletIndicesIndex = indexBuffer->GetResourceHandle().index,
      .transformsIndex = transformBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .indirectDrawIndex = meshletIndirectCommand->GetResourceHandle().index,
      .materialsIndex = materialStorageBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .viewIndex = viewBuffer->GetResourceHandle().index,
    });
    ctx.BindIndexBuffer(*instancedMeshletBuffer, 0, VK_INDEX_TYPE_UINT32);
    ctx.DrawIndexedIndirect(*meshletIndirectCommand, 0, 1, 0);
  }
  ctx.EndRendering();

  //// VSMs
  //{
  //  const auto debugMarker = Fwog::ScopedDebugMarker("Virtual Shadow Maps");
  //  //TIME_SCOPE_GPU(StatGroup::eMainGpu, eVsm);

  //  {
  //    //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmResetPageVisibility);
  //    vsmContext.ResetPageVisibility();
  //  }
  //  {
  //    //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmMarkVisiblePages);
  //    vsmSun.MarkVisiblePages(frame.gDepth.value(), globalUniformsBuffer);
  //  }
  //  {
  //    //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmFreeNonVisiblePages);
  //    vsmContext.FreeNonVisiblePages();
  //  }
  //  {
  //    //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmAllocatePages);
  //    vsmContext.AllocateRequestedPages();
  //  }
  //  {
  //    //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmGenerateHpb);
  //    vsmSun.GenerateBitmaskHzb();
  //  }
  //  {
  //    //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmClearDirtyPages);
  //    vsmContext.ClearDirtyPages();
  //  }

  //  //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmRenderDirtyPages);

  //  // Sun VSMs
  //  for (uint32_t i = 0; i < vsmSun.NumClipmaps(); i++)
  //  {
  //    auto sunCurrentClipmapView = View{
  //      .oldViewProj = vsmSun.GetProjections()[i] * vsmSun.GetViewMatrices()[i],
  //      .proj = vsmSun.GetProjections()[i],
  //      .view = vsmSun.GetViewMatrices()[i],
  //      .viewProj = vsmSun.GetProjections()[i] * vsmSun.GetViewMatrices()[i],
  //      .viewProjStableForVsmOnly = vsmSun.GetProjections()[i] * vsmSun.GetStableViewMatrix(),
  //      .cameraPos = {}, // unused
  //      .viewport = {0.f, 0.f, vsmSun.GetExtent().width, vsmSun.GetExtent().height},
  //      .type = ViewType::VIRTUAL,
  //      .virtualTableIndex = vsmSun.GetClipmapTableIndices()[i],
  //    };
  //    Math::MakeFrustumPlanes(sunCurrentClipmapView.viewProj, sunCurrentClipmapView.frustumPlanes);

  //    CullMeshletsForView(sunCurrentClipmapView, "Cull Sun VSM Meshlets, View " + std::to_string(i));

  //    const auto vsmExtent = Fwog::Extent2D{Techniques::VirtualShadowMaps::maxExtent, Techniques::VirtualShadowMaps::maxExtent};
  //    Fwog::RenderNoAttachments(
  //      {
  //        .name = "Render Clipmap",
  //        .viewport = {{{0, 0}, vsmExtent}},
  //        .framebufferSize = {vsmExtent.width, vsmExtent.height, 1},
  //        .framebufferSamples = Fwog::SampleCount::SAMPLES_1,
  //      },
  //      [&]
  //      {
  //        Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT | Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT);
  //        Fwog::Cmd::BindGraphicsPipeline(vsmShadowPipeline);
  //        Fwog::Cmd::BindStorageBuffer("MeshletDataBuffer", *meshletBuffer);
  //        Fwog::Cmd::BindStorageBuffer("MeshletPrimitiveBuffer", *primitiveBuffer);
  //        Fwog::Cmd::BindStorageBuffer("MeshletVertexBuffer", *vertexBuffer);
  //        Fwog::Cmd::BindStorageBuffer("MeshletIndexBuffer", *indexBuffer);
  //        Fwog::Cmd::BindStorageBuffer("TransformBuffer", *transformBuffer);
  //        Fwog::Cmd::BindUniformBuffer("PerFrameUniformsBuffer", globalUniformsBuffer);
  //        Fwog::Cmd::BindStorageBuffer("ViewBuffer", viewBuffer.value());
  //        Fwog::Cmd::BindStorageBuffer("MaterialBuffer", materialStorageBuffer.value());
  //        vsmSun.BindResourcesForDrawing();
  //        Fwog::Cmd::BindImage(1, vsmContext.physicalPagesUint_, 0);
  //        Fwog::Cmd::BindUniformBuffer("VsmShadowUniforms", vsmShadowUniformBuffer);
  //        Fwog::Cmd::BindIndexBuffer(*instancedMeshletBuffer, Fwog::IndexType::UNSIGNED_INT);

  //        vsmShadowUniformBuffer.UpdateData(vsmSun.GetClipmapTableIndices()[i]);
  //        Fwog::Cmd::DrawIndexedIndirect(*meshletIndirectCommand, 0, 1, 0);
  //      });
  //  }
  //}

  // TODO: remove when descriptor indexing sync validation does not give false positives
  ctx.Barrier();
  ctx.ImageBarrier(*frame.gDepth, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

  if (generateHizBuffer)
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eHzb);
    //Fwog::Compute(
    //  "HZB Build Pass",
    //  [&]
    {
      ctx.SetPushConstants(HzbCopyPushConstants{
        .hzbIndex = frame.hzb->ImageView().GetStorageResourceHandle().index,
        .depthIndex = frame.gDepth->ImageView().GetSampledResourceHandle().index,
        .depthSamplerIndex = hzbSampler.GetResourceHandle().index,
      });
      //Fwog::Cmd::BindImage(0, *frame.hzb, 0);
      //Fwog::Cmd::BindSampledImage(1, *frame.gDepth, hzbSampler);
      //Fwog::Cmd::BindComputePipeline(hzbCopyPipeline);
      ctx.BindComputePipeline(hzbCopyPipeline);
      uint32_t hzbCurrentWidth = frame.hzb->GetCreateInfo().extent.width;
      uint32_t hzbCurrentHeight = frame.hzb->GetCreateInfo().extent.height;
      const uint32_t hzbLevels = frame.hzb->GetCreateInfo().mipLevels;
      //ctx.ImageBarrier(*frame.hzb, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
      ctx.Dispatch((hzbCurrentWidth + 15) / 16, (hzbCurrentHeight + 15) / 16, 1);

      ctx.BindComputePipeline(hzbReducePipeline);
      for (uint32_t level = 1; level < hzbLevels; ++level)
      {
        //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT);
        ctx.ImageBarrier(*frame.hzb, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        auto prevHzbView = frame.hzb->CreateSingleMipView(level - 1, "prevHzbMip");
        auto curHzbView = frame.hzb->CreateSingleMipView(level, "curHzbMip");

        ctx.SetPushConstants(HzbReducePushConstants{
          .prevHzbIndex = prevHzbView.GetStorageResourceHandle().index,
          .curHzbIndex = curHzbView.GetStorageResourceHandle().index,
        });
        //Fwog::Cmd::BindImage(0, *frame.hzb, level - 1);
        //Fwog::Cmd::BindImage(1, *frame.hzb, level);
        hzbCurrentWidth = std::max(1u, hzbCurrentWidth >> 1);
        hzbCurrentHeight = std::max(1u, hzbCurrentHeight >> 1);
        ctx.Dispatch((hzbCurrentWidth + 15) / 16, (hzbCurrentHeight + 15) / 16, 1);
      }
      //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT);
      ctx.ImageBarrier(*frame.hzb, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    }
  }
  else
  {
    const uint32_t hzbLevels = frame.hzb->GetCreateInfo().mipLevels;
    for (uint32_t level = 0; level < hzbLevels; level++)
    {
      constexpr float farDepth = FAR_DEPTH;
      
      //frame.hzb->ClearImage({.level = level, .data = &farDepth});
      ctx.ClearTexture(*frame.hzb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, {.color = {farDepth}, .baseMipLevel = level});
    }
  }

  // TODO: remove when descriptor indexing sync validation does not give false positives
  ctx.Barrier();
  ctx.ImageBarrier(*frame.materialDepth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  auto materialDepthAttachment = Fvog::RenderDepthStencilAttachment{
    .texture = frame.materialDepth->ImageView(),
    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .clearValue = {.depth = 1.0f},
  };

  ctx.BeginRendering({
    .name = "Material Visbuffer Pass",
    //.viewport =
    //  {
    //    Fwog::Viewport{
    //      .drawRect = {{0, 0}, {renderWidth, renderHeight}},
    //    },
    //  },
    .depthAttachment = materialDepthAttachment,
  });
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eMakeMaterialDepthBuffer);
    ctx.BindGraphicsPipeline(materialDepthPipeline);
    ctx.SetPushConstants(VisbufferPushConstants{
      .meshletDataIndex = meshletBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .visbufferIndex = frame.visbuffer->ImageView().GetSampledResourceHandle().index,
    });
    //Fwog::Cmd::BindStorageBuffer("MeshletDataBuffer", *meshletBuffer);
    //Fwog::Cmd::BindImage("visbuffer", frame.visbuffer.value(), 0);
    ctx.Draw(3, 1, 0, 0);
  }
  ctx.EndRendering();

  ctx.Barrier(); // MaterialDepth: attachment->attachment
  ctx.ImageBarrier(*frame.gAlbedo,              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrier(*frame.gNormalAndFaceNormal, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrier(*frame.gDepth,               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrier(*frame.gSmoothVertexNormal,  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrier(*frame.gEmission,            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrier(*frame.gMetallicRoughnessAo, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  Fvog::RenderColorAttachment gBufferAttachments[] = {
    {
      .texture = frame.gAlbedo->ImageView(),
      .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE
    },
    {
      .texture = frame.gMetallicRoughnessAo->ImageView(),
      .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    },
    {
      .texture = frame.gNormalAndFaceNormal->ImageView(),
      .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    },
    {
      .texture = frame.gSmoothVertexNormal->ImageView(),
      .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    },
    {
      .texture = frame.gEmission->ImageView(),
      .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    },
    {
      .texture = frame.gMotion->ImageView(),
      .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .clearValue = {0.f, 0.f, 0.f, 0.f},
    },
  };

  ctx.BeginRendering({
    .name = "Resolve Visbuffer Pass",
    //.viewport =
    //  {
    //    Fwog::Viewport{
    //      .drawRect = {{0, 0}, {renderWidth, renderHeight}},
    //    },
    //  },
    .colorAttachments = gBufferAttachments,
    .depthAttachment =
      Fvog::RenderDepthStencilAttachment{
        .texture = frame.materialDepth->ImageView(),
        .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      },
  });
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eResolveVisbuffer);
    ctx.BindGraphicsPipeline(visbufferResolvePipeline);
    //Fwog::Cmd::BindGraphicsPipeline(visbufferResolvePipeline);
    //Fwog::Cmd::BindImage("visbuffer", frame.visbuffer.value(), 0);
    //Fwog::Cmd::BindStorageBuffer("MeshletDataBuffer", *meshletBuffer);
    //Fwog::Cmd::BindStorageBuffer("MeshletPrimitiveBuffer", *primitiveBuffer);
    //Fwog::Cmd::BindStorageBuffer("MeshletVertexBuffer", *vertexBuffer);
    //Fwog::Cmd::BindStorageBuffer("MeshletIndexBuffer", *indexBuffer);
    //Fwog::Cmd::BindStorageBuffer("TransformBuffer", *transformBuffer);
    //Fwog::Cmd::BindUniformBuffer("PerFrameUniformsBuffer", globalUniformsBuffer);
    //Fwog::Cmd::BindStorageBuffer("MaterialBuffer", *materialStorageBuffer);

    // Render a full-screen tri for each material, but only fragments with matching material (stored in depth) are shaded
    for (uint32_t materialId = 0; materialId < scene.materials.size(); ++materialId)
    {
      auto& material = scene.materials[materialId];

      auto pushConstants = VisbufferPushConstants{
        .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
        .meshletDataIndex = meshletBuffer->GetDeviceBuffer().GetResourceHandle().index,
        .meshletPrimitivesIndex = primitiveBuffer->GetResourceHandle().index,
        .meshletVerticesIndex = vertexBuffer->GetResourceHandle().index,
        .meshletIndicesIndex = indexBuffer->GetResourceHandle().index,
        .transformsIndex = transformBuffer->GetDeviceBuffer().GetResourceHandle().index,
        .materialsIndex = materialStorageBuffer->GetDeviceBuffer().GetResourceHandle().index,

        .visbufferIndex = frame.visbuffer->ImageView().GetSampledResourceHandle().index,
      };


      if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_BASE_COLOR_TEXTURE)
      {
        pushConstants.baseColorIndex = material.albedoTextureSampler->texture.GetSampledResourceHandle().index;
        //auto& [texture, sampler] = material.albedoTextureSampler.value();
        //sampler.lodBias = fsr2LodBias;
        //Fwog::Cmd::BindSampledImage("s_baseColor", texture, Fvog::Sampler(sampler));
      }

      if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_METALLIC_ROUGHNESS_TEXTURE)
      {
        pushConstants.metallicRoughnessIndex = material.metallicRoughnessTextureSampler->texture.GetSampledResourceHandle().index;
        //auto& [texture, sampler] = material.metallicRoughnessTextureSampler.value();
        //sampler.lodBias = fsr2LodBias;
        //Fwog::Cmd::BindSampledImage("s_metallicRoughness", texture, Fwog::Sampler(sampler));
      }

      if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_NORMAL_TEXTURE)
      {
        pushConstants.normalIndex = material.normalTextureSampler->texture.GetSampledResourceHandle().index;
        //auto& [texture, sampler] = material.normalTextureSampler.value();
        //sampler.lodBias = fsr2LodBias;
        //Fwog::Cmd::BindSampledImage("s_normal", texture, Fwog::Sampler(sampler));
      }

      if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_OCCLUSION_TEXTURE)
      {
        pushConstants.occlusionIndex = material.occlusionTextureSampler->texture.GetSampledResourceHandle().index;
        //auto& [texture, sampler] = material.occlusionTextureSampler.value();
        //sampler.lodBias = fsr2LodBias;
        //Fwog::Cmd::BindSampledImage("s_occlusion", texture, Fwog::Sampler(sampler));
      }

      if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_EMISSION_TEXTURE)
      {
        pushConstants.emissionIndex = material.emissiveTextureSampler->texture.GetSampledResourceHandle().index;
        //auto& [texture, sampler] = material.emissiveTextureSampler.value();
        //sampler.lodBias = fsr2LodBias;
        //Fwog::Cmd::BindSampledImage("s_emission", texture, Fwog::Sampler(sampler));
      }

      ctx.SetPushConstants(pushConstants);
      ctx.Draw(3, 1, 0, materialId);
    }
  }
  ctx.EndRendering();

  ctx.ImageBarrier(*frame.gAlbedo, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrier(*frame.gNormalAndFaceNormal, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrier(*frame.gDepth, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrier(*frame.gSmoothVertexNormal, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrier(*frame.gEmission, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrier(*frame.gMetallicRoughnessAo, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

  // shading pass (full screen tri)
  auto shadingColorAttachment = Fvog::RenderColorAttachment{
    .texture = frame.colorHdrRenderRes->ImageView(),
    .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .clearValue = {.1f, .3f, .5f, 0.0f},
  };
  ctx.BeginRendering({
    .name = "Shading",
    .colorAttachments = {&shadingColorAttachment, 1},
  });
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eShadeOpaque);
    //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT | Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT);
    ctx.BindGraphicsPipeline(shadingPipeline);
    ctx.SetPushConstants(ShadingPushConstants{
      .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .shadingUniformsIndex = shadingUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .shadowUniformsIndex = shadowUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .lightBufferIndex = lightBuffer->GetDeviceBuffer().GetResourceHandle().index,

      .gAlbedoIndex = frame.gAlbedo->ImageView().GetSampledResourceHandle().index,
      .gNormalAndFaceNormalIndex = frame.gNormalAndFaceNormal->ImageView().GetSampledResourceHandle().index,
      .gDepthIndex = frame.gDepth->ImageView().GetSampledResourceHandle().index,
      .gSmoothVertexNormalIndex = frame.gSmoothVertexNormal->ImageView().GetSampledResourceHandle().index,
      .gEmissionIndex = frame.gEmission->ImageView().GetSampledResourceHandle().index,
      .gMetallicRoughnessAoIndex = frame.gMetallicRoughnessAo->ImageView().GetSampledResourceHandle().index,
    });
    //Fwog::Cmd::BindSampledImage("s_gAlbedo", *frame.gAlbedo, nearestSampler);
    //Fwog::Cmd::BindSampledImage(1, *frame.gNormalAndFaceNormal, nearestSampler);
    //Fwog::Cmd::BindSampledImage("s_gDepth", *frame.gDepth, nearestSampler);
    //Fwog::Cmd::BindSampledImage(3, *frame.gSmoothVertexNormal, nearestSampler);
    //Fwog::Cmd::BindSampledImage("s_emission", *frame.gEmission, nearestSampler);
    //Fwog::Cmd::BindSampledImage("s_metallicRoughnessAo", *frame.gMetallicRoughnessAo, nearestSampler);
    //Fwog::Cmd::BindUniformBuffer("PerFrameUniformsBuffer", globalUniformsBuffer);
    //Fwog::Cmd::BindUniformBuffer("ShadingUniforms", shadingUniformsBuffer);
    //Fwog::Cmd::BindUniformBuffer("ShadowUniforms", shadowUniformsBuffer);
    //Fwog::Cmd::BindStorageBuffer("LightBuffer", *lightBuffer);
    // TODO
    //vsmSun.BindResourcesForDrawing();
    ctx.Draw(3, 1, 0, 0);
  }
  ctx.EndRendering();

  // After shading, we render debug geometry
  auto debugDepthAttachment = Fvog::RenderDepthStencilAttachment{
    .texture = frame.gDepth->ImageView(),
    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
  };

  auto colorAttachments = std::vector<Fvog::RenderColorAttachment>{};
  colorAttachments.emplace_back(frame.colorHdrRenderRes->ImageView(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_LOAD_OP_LOAD);
  if (fsr2Enable)
  {
    colorAttachments.emplace_back(frame.gReactiveMask->ImageView(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_LOAD_OP_CLEAR, Fvog::ClearColorValue{0.0f});
  }

  //Fwog::Render(
  //  {
  //    .name = "Debug Geometry",
  //    .colorAttachments = colorAttachments,
  //    .depthAttachment = debugDepthAttachment,
  //  },
  //  [&]
  //  {
  //    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eDebugGeometry);
  //    Fwog::Cmd::BindUniformBuffer(0, globalUniformsBuffer);
  //    // Lines
  //    if (!debugLines.empty())
  //    {
  //      auto lineVertexBuffer = Fwog::TypedBuffer<Debug::Line>(debugLines);
  //      Fwog::Cmd::BindGraphicsPipeline(debugLinesPipeline);
  //      Fwog::Cmd::BindVertexBuffer(0, lineVertexBuffer, 0, sizeof(glm::vec3) + sizeof(glm::vec4));
  //      Fwog::Cmd::Draw(uint32_t(debugLines.size() * 2), 1, 0, 0);
  //    }

  //    // GPU-generated geometry past here
  //    Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::COMMAND_BUFFER_BIT | Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT);

  //    // AABBs
  //    if (drawDebugAabbs)
  //    {
  //      Fwog::Cmd::BindGraphicsPipeline(debugAabbsPipeline);
  //      Fwog::Cmd::BindStorageBuffer("DebugAabbBuffer", debugGpuAabbsBuffer.value());
  //      Fwog::Cmd::DrawIndirect(debugGpuAabbsBuffer.value(), 0, 1, 0);
  //    }

  //    // Rects
  //    if (drawDebugRects)
  //    {
  //      Fwog::Cmd::BindGraphicsPipeline(debugRectsPipeline);
  //      Fwog::Cmd::BindStorageBuffer("DebugRectBuffer", debugGpuRectsBuffer.value());
  //      Fwog::Cmd::DrawIndirect(debugGpuRectsBuffer.value(), 0, 1, 0);
  //    }
  //  });

  //{
  //  //TIME_SCOPE_GPU(StatGroup::eMainGpu, eAutoExposure);
  //  autoExposure.Apply({
  //    .image = frame.colorHdrRenderRes.value(),
  //    .exposureBuffer = exposureBuffer,
  //    .deltaTime = static_cast<float>(dt),
  //    .adjustmentSpeed = autoExposureAdjustmentSpeed,
  //    .targetLuminance = autoExposureTargetLuminance,
  //    .logMinLuminance = autoExposureLogMinLuminance,
  //    .logMaxLuminance = autoExposureLogMaxLuminance,
  //  });
  //}

//#ifdef FROGRENDER_FSR2_ENABLE
//  if (fsr2Enable)
//  {
//    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eFsr2);
//    Fwog::Compute(
//      "FSR 2",
//      [&]
//      {
//        static Fwog::TimerQueryAsync timer(5);
//        if (auto t = timer.PopTimestamp())
//        {
//          fsr2Performance = *t / 10e5;
//        }
//        Fwog::TimerScoped scopedTimer(timer);
//
//        if (frameIndex == 1)
//        {
//          dt = 17.0 / 1000.0;
//        }
//
//        float jitterX{};
//        float jitterY{};
//        ffxFsr2GetJitterOffset(&jitterX, &jitterY, frameIndex, ffxFsr2GetJitterPhaseCount(renderWidth, windowWidth));
//
//        FfxFsr2DispatchDescription dispatchDesc{
//          .color = ffxGetTextureResourceGL(frame.colorHdrRenderRes->Handle(), renderWidth, renderHeight, Fwog::detail::FormatToGL(frame.colorHdrRenderRes->GetCreateInfo().format)),
//          .depth = ffxGetTextureResourceGL(frame.gDepth->Handle(), renderWidth, renderHeight, Fwog::detail::FormatToGL(frame.gDepth->GetCreateInfo().format)),
//          .motionVectors = ffxGetTextureResourceGL(frame.gMotion->Handle(), renderWidth, renderHeight, Fwog::detail::FormatToGL(frame.gMotion->GetCreateInfo().format)),
//          .exposure = {},
//          .reactive = ffxGetTextureResourceGL(frame.gReactiveMask->Handle(), renderWidth, renderHeight, Fwog::detail::FormatToGL(frame.gReactiveMask->GetCreateInfo().format)),
//          .transparencyAndComposition = {},
//          .output = ffxGetTextureResourceGL(frame.colorHdrWindowRes->Handle(), windowWidth, windowHeight, Fwog::detail::FormatToGL(frame.colorHdrWindowRes->GetCreateInfo().format)),
//          .jitterOffset = {jitterX, jitterY},
//          .motionVectorScale = {float(renderWidth), float(renderHeight)},
//          .renderSize = {renderWidth, renderHeight},
//          .enableSharpening = fsr2Sharpness != 0,
//          .sharpness = fsr2Sharpness,
//          .frameTimeDelta = static_cast<float>(dt * 1000.0),
//          .preExposure = 1,
//          .reset = false,
//          .cameraNear = std::numeric_limits<float>::max(),
//          .cameraFar = cameraNearPlane,
//          .cameraFovAngleVertical = cameraFovyRadians,
//          .viewSpaceToMetersFactor = 1,
//          .deviceDepthNegativeOneToOne = false,
//        };
//
//        if (auto err = ffxFsr2ContextDispatch(&fsr2Context, &dispatchDesc); err != FFX_OK)
//        {
//          printf("FSR 2 error: %d\n", err);
//        }
//      });
//    Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT);
//  }
//#endif

  //if (bloomEnable)
  //{
  //  //TIME_SCOPE_GPU(StatGroup::eMainGpu, eBloom);
  //  bloom.Apply({
  //    .target = fsr2Enable ? frame.colorHdrWindowRes.value() : frame.colorHdrRenderRes.value(),
  //    .scratchTexture = frame.colorHdrBloomScratchBuffer.value(),
  //    .passes = bloomPasses,
  //    .strength = bloomStrength,
  //    .width = bloomWidth,
  //    .useLowPassFilterOnFirstPass = bloomUseLowPassFilter,
  //  });
  //}

  //Fwog::Compute("Postprocessing",
  //  [&]
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eResolveImage);
    //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::UNIFORM_BUFFER_BIT);
    ctx.Barrier();
    ctx.ImageBarrier(*frame.colorLdrWindowRes, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    ctx.BindComputePipeline(tonemapPipeline);
    ctx.SetPushConstants(TonemapArguments{
      .sceneColorIndex = (fsr2Enable ? frame.colorHdrWindowRes.value() : frame.colorHdrRenderRes.value()).ImageView().GetSampledResourceHandle().index,
      .noiseIndex = noiseTexture->ImageView().GetSampledResourceHandle().index,
      .nearestSamplerIndex = nearestSampler.GetResourceHandle().index,
      .exposureIndex = exposureBuffer.GetResourceHandle().index,
      .tonemapUniformsIndex = tonemapUniformBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .outputImageIndex = frame.colorLdrWindowRes->ImageView().GetStorageResourceHandle().index,
    });
    //Fwog::Cmd::BindSampledImage(0, fsr2Enable ? frame.colorHdrWindowRes.value() : frame.colorHdrRenderRes.value(), nearestSampler);
    //Fwog::Cmd::BindSampledImage(1, noiseTexture.value(), nearestSampler);
    //Fwog::Cmd::BindUniformBuffer(0, exposureBuffer);
    //Fwog::Cmd::BindUniformBuffer(1, tonemapUniformBuffer);
    //Fwog::Cmd::BindImage(0, frame.colorLdrWindowRes.value(), 0);
    ctx.DispatchInvocations(frame.colorLdrWindowRes.value().GetCreateInfo().extent);
    //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT); // So future samples can see changes
    ctx.ImageBarrier(*frame.colorLdrWindowRes, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  }

  ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  vkCmdBlitImage2(commandBuffer, Address(VkBlitImageInfo2{
    .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
    .srcImage = frame.colorLdrWindowRes->Image(),
    .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    .dstImage = swapchainImages_[swapchainImageIndex],
    .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    .regionCount = 1,
    .pRegions = Address(VkImageBlit2{
      .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
      .srcSubresource = VkImageSubresourceLayers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .layerCount = 1,
      },
      .srcOffsets = {{}, {(int)renderWidth, (int)renderHeight, 1}},
      .dstSubresource = VkImageSubresourceLayers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .layerCount = 1,
      },
      .dstOffsets = {{}, {(int)windowWidth, (int)windowHeight, 1}},
    }),
    .filter = VK_FILTER_NEAREST,
  }));

  // GUI is not rendered, draw directly to screen instead
  //if (!showGui)
  {
    //Fwog::RenderToSwapchain(
    //  {
    //    .name = "Copy to swapchain",
    //    .viewport =
    //      Fwog::Viewport{
    //        .drawRect{.offset = {0, 0}, .extent = {windowWidth, windowHeight}},
    //        .minDepth = 0.0f,
    //        .maxDepth = 1.0f,
    //      },
    //    .colorLoadOp = Fwog::AttachmentLoadOp::DONT_CARE,
    //    .depthLoadOp = Fwog::AttachmentLoadOp::DONT_CARE,
    //    .stencilLoadOp = Fwog::AttachmentLoadOp::DONT_CARE,
    //    .enableSrgb = false,
    //  },
    //  [&]
    //  {
    //    Fwog::Cmd::BindGraphicsPipeline(debugTexturePipeline);
    //    ctx.BindGraphicsPipeline(debugTexturePipeline);
    //    Fwog::Cmd::BindSampledImage(0, frame.colorLdrWindowRes.value(), nearestSampler);
    //    ctx.Draw(3, 1, 0, 0);
    //  });
  }


















  // TODO: swapchainImages_[swapchainImageIndex] needs to be COLOR_ATTACHMENT_OPTIMAL after this function returns
  ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void FrogRenderer2::OnGui([[maybe_unused]] double dt)
{
  ImGui::Begin("test");
  ImGui::Text("FPS:  %.1f Hz", 1.0 / dt);
  ImGui::Text("AFPS: %.1f Rad/s", glm::two_pi<double>() / dt); // Angular frames per second for the mechanically-minded
  ImGui::End();
}

void FrogRenderer2::MakeStaticSceneBuffers(VkCommandBuffer commandBuffer)
{
  auto ctx = Fvog::Context(commandBuffer);
  
  ctx.Barrier();

  vertexBuffer = Fvog::TypedBuffer<Utility::Vertex>(*device_, {(uint32_t)scene.vertices.size()}, "Vertex Buffer");
  vertexBuffer->UpdateDataExpensive(commandBuffer, scene.vertices);

  indexBuffer = Fvog::TypedBuffer<uint32_t>(*device_, {(uint32_t)scene.indices.size()}, "Index Buffer");
  indexBuffer->UpdateDataExpensive(commandBuffer, scene.indices);

  primitiveBuffer = Fvog::TypedBuffer<uint8_t>(*device_, {(uint32_t)scene.primitives.size()}, "Primitive Buffer");
  primitiveBuffer->UpdateDataExpensive(commandBuffer, scene.primitives);

  std::vector<Utility::GpuMaterial> materials(scene.materials.size());
  std::transform(scene.materials.begin(), scene.materials.end(), materials.begin(), [](const auto& m) { return m.gpuMaterial; });
  materialStorageBuffer = Fvog::NDeviceBuffer<Utility::GpuMaterial>(*device_, (uint32_t)materials.size(), "Material Buffer");
  materialStorageBuffer->GetDeviceBuffer().UpdateDataExpensive(commandBuffer, std::span(materials));

  ctx.Barrier();
}

void FrogRenderer2::OnPathDrop([[maybe_unused]] std::span<const char*> paths)
{
  
}
