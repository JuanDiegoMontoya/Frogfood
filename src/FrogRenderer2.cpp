#include "FrogRenderer2.h"

#include "Pipelines2.h"

#include "Fvog/Rendering2.h"
#include "Fvog/Shader2.h"
#include "Fvog/detail/Common.h"
#include "Fvog/detail/ApiToEnum2.h"
using namespace Fvog::detail;

#include "shaders/Config.shared.h"
#include "shaders/visbuffer/CullMeshlets.h.glsl"

#include "MathUtilities.h"

#include <stb_image.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include "ImGui/imgui_impl_fvog.h"

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>


static glm::vec2 GetJitterOffset([[maybe_unused]] uint32_t frameIndex,
                                 [[maybe_unused]] uint32_t renderInternalWidth,
                                 [[maybe_unused]] uint32_t renderInternalHeight,
                                 [[maybe_unused]] uint32_t renderOutputWidth)
{
#ifdef FROGRENDER_FSR2_ENABLE
  float jitterX{};
  float jitterY{};
  ffxFsr2GetJitterOffset(&jitterX, &jitterY, frameIndex, ffxFsr2GetJitterPhaseCount(renderInternalWidth, renderOutputWidth));
  return {2.0f * jitterX / static_cast<float>(renderInternalWidth), 2.0f * jitterY / static_cast<float>(renderInternalHeight)};
#else
  return {0, 0};
#endif
}

static std::vector<Debug::Line> GenerateFrustumWireframe(const glm::mat4& invViewProj, const glm::vec4& color, float near_, float far_)
{
  auto lines = std::vector<Debug::Line>{};

  // Get frustum corners in world space
  auto tln = Math::UnprojectUV_ZO(near_, {0, 1}, invViewProj);
  auto trn = Math::UnprojectUV_ZO(near_, {1, 1}, invViewProj);
  auto bln = Math::UnprojectUV_ZO(near_, {0, 0}, invViewProj);
  auto brn = Math::UnprojectUV_ZO(near_, {1, 0}, invViewProj);

  // Far corners are lerped slightly to near in case it is an infinite projection
  auto tlf = Math::UnprojectUV_ZO(glm::mix(far_, near_, 1e-5), {0, 1}, invViewProj);
  auto trf = Math::UnprojectUV_ZO(glm::mix(far_, near_, 1e-5), {1, 1}, invViewProj);
  auto blf = Math::UnprojectUV_ZO(glm::mix(far_, near_, 1e-5), {0, 0}, invViewProj);
  auto brf = Math::UnprojectUV_ZO(glm::mix(far_, near_, 1e-5), {1, 0}, invViewProj);

  // Connect-the-dots
  // Near and far "squares"
  lines.emplace_back(tln, color, trn, color);
  lines.emplace_back(bln, color, brn, color);
  lines.emplace_back(tln, color, bln, color);
  lines.emplace_back(trn, color, brn, color);
  lines.emplace_back(tlf, color, trf, color);
  lines.emplace_back(blf, color, brf, color);
  lines.emplace_back(tlf, color, blf, color);
  lines.emplace_back(trf, color, brf, color);

  // Lines connecting near and far planes
  lines.emplace_back(tln, color, tlf, color);
  lines.emplace_back(trn, color, trf, color);
  lines.emplace_back(bln, color, blf, color);
  lines.emplace_back(brn, color, brf, color);

  return lines;
}

FrogRenderer2::FrogRenderer2(const Application::CreateInfo& createInfo)
  : Application(createInfo),
    // Create constant-size buffers
    globalUniformsBuffer(*device_, 1, "Global Uniforms"),
    shadingUniformsBuffer(*device_, 1, "Shading Uniforms"),
    shadowUniformsBuffer(*device_, 1, "Shadow Uniforms"),
    // Create the pipelines used in the application
    cullMeshletsPipeline(Pipelines2::CullMeshlets(*device_)),
    cullTrianglesPipeline(Pipelines2::CullTriangles(*device_)),
    hzbCopyPipeline(Pipelines2::HzbCopy(*device_)),
    hzbReducePipeline(Pipelines2::HzbReduce(*device_)),
    visbufferPipeline(Pipelines2::Visbuffer(*device_,
      {
        .colorAttachmentFormats = {{Frame::visbufferFormat}},
        .depthAttachmentFormat  = Frame::gDepthFormat,
      })),
    visbufferResolvePipeline(Pipelines2::VisbufferResolve(*device_,
      {
        .colorAttachmentFormats = {{
          Frame::gAlbedoFormat,
          Frame::gMetallicRoughnessAoFormat,
          Frame::gNormalAndFaceNormalFormat,
          Frame::gSmoothVertexNormalFormat,
          Frame::gEmissionFormat,
          Frame::gMotionFormat,
        }},
      })),
    shadingPipeline(Pipelines2::Shading(*device_, {.colorAttachmentFormats = {{Frame::colorHdrRenderResFormat}},})),
    tonemapPipeline(Pipelines2::Tonemap(*device_)),
    debugTexturePipeline(Pipelines2::DebugTexture(*device_, {.colorAttachmentFormats = {{Fvog::detail::VkToFormat(swapchainUnormFormat)}},})),
    debugLinesPipeline(Pipelines2::DebugLines(*device_,
      {
        .colorAttachmentFormats = {{Fvog::detail::VkToFormat(swapchainUnormFormat), Frame::gReactiveMaskFormat}},
      })),
    debugAabbsPipeline(Pipelines2::DebugAabbs(*device_,
      {
        .colorAttachmentFormats = {{Fvog::detail::VkToFormat(swapchainUnormFormat), Frame::gReactiveMaskFormat}},
      })),
    debugRectsPipeline(Pipelines2::DebugRects(*device_,
      {
        .colorAttachmentFormats = {{Fvog::detail::VkToFormat(swapchainUnormFormat), Frame::gReactiveMaskFormat}},
      })),
    tonemapUniformBuffer(*device_, 1, "Tonemap Uniforms"),
    bloom(*device_),
    autoExposure(*device_),
    exposureBuffer(*device_, {}, "Exposure"),
    vsmContext(*device_, {
      .maxVsms = 64,
      .pageSize = {Techniques::VirtualShadowMaps::pageSize, Techniques::VirtualShadowMaps::pageSize},
      .numPages = 1024,
    }),
    vsmSun({
      .context = vsmContext,
      .virtualExtent = Techniques::VirtualShadowMaps::maxExtent,
      .numClipmaps = 10,
    }),
    vsmShadowPipeline(Pipelines2::ShadowVsm(*device_, {})),
    vsmShadowUniformBuffer(*device_),
    viewerVsmPageTablesPipeline(Pipelines2::ViewerVsm(*device_, {.colorAttachmentFormats = {{viewerOutputTextureFormat}}})),
    viewerVsmPhysicalPagesPipeline(Pipelines2::ViewerVsmPhysicalPages(*device_, {.colorAttachmentFormats = {{viewerOutputTextureFormat}}})),
    viewerVsmBitmaskHzbPipeline(Pipelines2::ViewerVsmBitmaskHzb(*device_, {.colorAttachmentFormats = {{viewerOutputTextureFormat}}})),
    nearestSampler(*device_, {
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    }, "Nearest"),
    linearMipmapSampler(*device_, {
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .maxAnisotropy = 16,
    }, "Linear Mipmap"),
    hzbSampler(*device_, {
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    }, "HZB")
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

  InitGui();

  mainCamera.position.y = 1;

  Utility::LoadModelFromFileMeshlet(*device_, scene, "models/simple_scene.glb", glm::scale(glm::vec3{.5}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:\\Repositories\\glTF-Sample-Models\\2.0\\BoomBox\\glTF/BoomBox.gltf", glm::scale(glm::vec3{10.0f}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf", glm::scale(glm::vec3{.5}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/Main/NewSponza_Main_Blender_glTF.gltf", glm::scale(glm::vec3{1}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/bistro_compressed.glb", glm::scale(glm::vec3{.5}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_compressed.glb", glm::scale(glm::vec3{1}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_compressed_tu.glb", glm::scale(glm::vec3{1}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/subdiv_deccer_cubes.glb", glm::scale(glm::vec3{1}));
  //Utility::LoadModelFromFileMeshlet(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/SM_Deccer_Cubes_Textured.glb", glm::scale(glm::vec3{1}));

  meshletIndirectCommand = Fvog::TypedBuffer<Fvog::DrawIndexedIndirectCommand>(*device_, {}, "Meshlet Indirect Command");
  cullTrianglesDispatchParams = Fvog::TypedBuffer<Fvog::DispatchIndirectCommand>(*device_, {}, "Cull Triangles Dispatch Params");
  viewBuffer = Fvog::TypedBuffer<ViewParams>(*device_, {}, "View Data");

  debugGpuAabbsBuffer = Fvog::Buffer(*device_, {sizeof(Fvog::DrawIndirectCommand) + sizeof(Debug::Aabb) * 100'000}, "Debug GPU AABBs");

  debugGpuRectsBuffer = Fvog::Buffer(*device_, {sizeof(Fvog::DrawIndirectCommand) + sizeof(Debug::Rect) * 100'000}, "Deug GPU Rects");

  device_->ImmediateSubmit(
    [this](VkCommandBuffer commandBuffer)
    {
      auto ctx = Fvog::Context(*device_, commandBuffer);

      // Reset the instance count of the debug draw buffers
      auto aabbCommand = Fvog::DrawIndirectCommand{
        .vertexCount = 14,
        .instanceCount = 0,
        .firstVertex = 0,
        .firstInstance = 0,
      };
      ctx.TeenyBufferUpdate(*debugGpuAabbsBuffer, aabbCommand);


      auto rectCommand = Fvog::DrawIndirectCommand{
        .vertexCount = 4,
        .instanceCount = 0,
        .firstVertex = 0,
        .firstInstance = 0,
      };
      ctx.TeenyBufferUpdate(*debugGpuRectsBuffer, rectCommand);
      
      exposureBuffer.FillData(commandBuffer, {.data = std::bit_cast<uint32_t>(1.0f)});
      cullTrianglesDispatchParams->UpdateDataExpensive(commandBuffer, Fvog::DispatchIndirectCommand{0, 1, 1});
      MakeStaticSceneBuffers(commandBuffer);
    });

  OnFramebufferResize(windowFramebufferWidth, windowFramebufferHeight);
  // The main loop might invoke the resize callback (which in turn causes a redraw) on the first frame, and OnUpdate produces
  // some resources necessary for rendering (but can be resused). This is a minor hack to make sure those resources are
  // available before the windowing system could issue a redraw.
  OnUpdate(0);
}

FrogRenderer2::~FrogRenderer2()
{
  vkDeviceWaitIdle(device_->device_);
#if FROGRENDER_FSR2_ENABLE
  if (!fsr2FirstInit)
  {
    ffxFsr2ContextDestroy(&fsr2Context);
  }
#endif
}

void FrogRenderer2::OnFramebufferResize([[maybe_unused]] uint32_t newWidth, [[maybe_unused]] uint32_t newHeight)
{
  ZoneScoped;

#ifdef FROGRENDER_FSR2_ENABLE
  // create FSR 2 context
  if (fsr2Enable)
  {
    if (!fsr2FirstInit)
    {
      // TODO: get rid of this stinky
      vkDeviceWaitIdle(device_->device_);
      ffxFsr2ContextDestroy(&fsr2Context);
    }

    fsr2FirstInit = false;
    renderInternalWidth = static_cast<uint32_t>(newWidth / fsr2Ratio);
    renderInternalHeight = static_cast<uint32_t>(newHeight / fsr2Ratio);
    FfxFsr2ContextDescription contextDesc{
      .flags = FFX_FSR2_ENABLE_DEBUG_CHECKING | FFX_FSR2_ENABLE_AUTO_EXPOSURE | FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR2_ENABLE_DEPTH_INFINITE |
               FFX_FSR2_ENABLE_DEPTH_INVERTED,
      .maxRenderSize = {renderInternalWidth, renderInternalHeight},
      .displaySize = {newWidth, newHeight},
      .device = ffxGetDeviceVK(device_->device_),
      .fpMessage =
        [](FfxFsr2MsgType type, const wchar_t* message)
      {
        char cstr[256] = {};
        if (wcstombs_s(nullptr, cstr, sizeof(cstr), message, sizeof(cstr)) == 0)
        {
          cstr[255] = '\0';
          printf("FSR 2 message (type=%d): %s\n", type, cstr);
        }
      },
    };

    auto scratchMemorySize = ffxFsr2GetScratchMemorySizeVK(device_->physicalDevice_, vkEnumerateDeviceExtensionProperties);
    fsr2ScratchMemory = std::make_unique<char[]>(scratchMemorySize);
    ffxFsr2GetInterfaceVK(&contextDesc.callbacks,
                          fsr2ScratchMemory.get(),
                          scratchMemorySize,
                          device_->physicalDevice_,
                          vkGetDeviceProcAddr,
                          vkGetPhysicalDeviceMemoryProperties,
                          vkGetPhysicalDeviceProperties2,
                          vkGetPhysicalDeviceFeatures2,
                          vkEnumerateDeviceExtensionProperties,
                          vkGetPhysicalDeviceProperties);
    ffxFsr2ContextCreate(&fsr2Context, &contextDesc);

    frame.gReactiveMask = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Fvog::Format::R8_UNORM, Fvog::TextureUsage::GENERAL, "Reactive Mask");
  }
  else
#endif
  {
    renderInternalWidth = newWidth;
    renderInternalHeight = newHeight;
  }

  renderOutputWidth = newWidth;
  renderOutputHeight = newHeight;

  aspectRatio = (float)renderInternalWidth / renderInternalHeight;

  //constexpr auto usageColorFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  //constexpr auto usageDepthFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  constexpr auto usage = Fvog::TextureUsage::ATTACHMENT_READ_ONLY;

  // Visibility buffer textures
  frame.visbuffer = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::visbufferFormat, usage, "visbuffer");

  {
    const uint32_t hzbWidth = Math::PreviousPower2(renderInternalWidth);
    const uint32_t hzbHeight = Math::PreviousPower2(renderInternalHeight);
    const uint32_t hzbMips = 1 + static_cast<uint32_t>(glm::floor(glm::log2(static_cast<float>(glm::max(hzbWidth, hzbHeight)))));
    frame.hzb = Fvog::CreateTexture2DMip(*device_, {hzbWidth, hzbHeight}, Frame::hzbFormat, hzbMips, Fvog::TextureUsage::GENERAL, "HZB");
  }

  // Create gbuffer textures and render info
  frame.gAlbedo = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::gAlbedoFormat, usage, "gAlbedo");
  frame.gMetallicRoughnessAo = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::gMetallicRoughnessAoFormat, usage, "gMetallicRoughnessAo");
  frame.gNormalAndFaceNormal = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::gNormalAndFaceNormalFormat, usage, "gNormalAndFaceNormal");
  frame.gSmoothVertexNormal = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::gSmoothVertexNormalFormat, usage, "gSmoothVertexNormal");
  frame.gEmission = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::gEmissionFormat, usage, "gEmission");
  frame.gDepth = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::gDepthFormat, usage, "gDepth");
  frame.gMotion = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::gMotionFormat, usage, "gMotion");
  frame.gNormaAndFaceNormallPrev = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::gNormalAndFaceNormalFormat, usage, "gNormaAndFaceNormallPrev");
  frame.gDepthPrev = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::gDepthPrevFormat, usage, "gDepthPrev");
  // The next four are general so they can be written to in postprocessing passes via compute
  frame.colorHdrRenderRes = Fvog::CreateTexture2D(*device_, {renderInternalWidth, renderInternalHeight}, Frame::colorHdrRenderResFormat, Fvog::TextureUsage::GENERAL, "colorHdrRenderRes");
  frame.colorHdrWindowRes = Fvog::CreateTexture2D(*device_, {newWidth, newHeight}, Frame::colorHdrWindowResFormat, Fvog::TextureUsage::GENERAL, "colorHdrWindowRes");
  frame.colorHdrBloomScratchBuffer = Fvog::CreateTexture2DMip(*device_, {newWidth / 2, newHeight / 2}, Frame::colorHdrBloomScratchBufferFormat, 8, Fvog::TextureUsage::GENERAL, "colorHdrBloomScratchBuffer");
  frame.colorLdrWindowRes = Fvog::CreateTexture2D(*device_, {newWidth, newHeight}, Frame::colorLdrWindowResFormat, Fvog::TextureUsage::GENERAL, "colorLdrWindowRes");

  // Create debug views with alpha swizzle set to one so they can be seen in ImGui
  frame.gAlbedoSwizzled = frame.gAlbedo->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
  frame.gRoughnessMetallicAoSwizzled = frame.gMetallicRoughnessAo->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
  frame.gEmissionSwizzled = frame.gEmission->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
  frame.gNormalSwizzled = frame.gNormalAndFaceNormal->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
  frame.gDepthSwizzled = frame.gDepth->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
}

void FrogRenderer2::OnUpdate([[maybe_unused]] double dt)
{
  ZoneScoped;
  if (fakeLag > 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(fakeLag));
  }

  debugLines.clear();

  if (debugDisplayMainFrustum)
  {
    auto mainFrustumLines = GenerateFrustumWireframe(glm::inverse(debugMainViewProj), glm::vec4(10, 1, 10, 1), NEAR_DEPTH, FAR_DEPTH);
    debugLines.insert(debugLines.end(), mainFrustumLines.begin(), mainFrustumLines.end());

    for (uint32_t i = 0; i < vsmSun.NumClipmaps(); i++)
    {
      auto lines = GenerateFrustumWireframe(glm::inverse(vsmSun.GetProjections()[i] * vsmSun.GetViewMatrices()[i]), glm::vec4(1, 1, 10, 1), 0, 1);
      debugLines.insert(debugLines.end(), lines.begin(), lines.end());
    }
  }

  sceneFlattened = scene.Flatten();
  shadingUniforms.numberOfLights = (uint32_t)sceneFlattened.lights.size();

  // A few of these buffers are really slow (2-3ms) to create and destroy every frame (large ones hit vkAllocateMemory), so
  // this scheme is still not ideal as e.g. adding geometry every frame will cause reallocs.
  // The current scheme works fine when the scene is mostly static.
  const auto numMeshlets = static_cast<uint32_t>(sceneFlattened.meshletInstances.size());
  const auto maxIndices = numMeshlets * Utility::maxMeshletPrimitives * 3;
  if (!instancedMeshletBuffer || instancedMeshletBuffer->Size() < maxIndices)
  {
    instancedMeshletBuffer = Fvog::TypedBuffer<uint32_t>(*device_, {.count = maxIndices}, "Instanced Meshlets");
  }

  if (!lightBuffer || lightBuffer->Size() < sceneFlattened.lights.size())
  {
    lightBuffer = Fvog::NDeviceBuffer<Utility::GpuLight>(*device_, (uint32_t)sceneFlattened.lights.size(), "Lights");
  }

  if (!transformBuffer || transformBuffer->Size() < sceneFlattened.transforms.size())
  {
    transformBuffer = Fvog::NDeviceBuffer<Utility::ObjectUniforms>(*device_, (uint32_t)sceneFlattened.transforms.size(), "Transforms");
  }

  if (!visibleMeshletIds || visibleMeshletIds->Size() < numMeshlets)
  {
    visibleMeshletIds = Fvog::TypedBuffer<uint32_t>(*device_, {.count = numMeshlets}, "Visible Meshlet IDs");
  }

  if (!meshletInstancesBuffer || meshletInstancesBuffer->Size() < numMeshlets)
  {
    meshletInstancesBuffer = Fvog::NDeviceBuffer<Utility::MeshletInstance>(*device_, numMeshlets, "Meshlet Instances");
  }

  // TODO: perhaps this logic belongs in Application
  if (shouldResizeNextFrame)
  {
    OnFramebufferResize(renderOutputWidth, renderOutputHeight);
    shouldResizeNextFrame = false;
  }
}

void FrogRenderer2::CullMeshletsForView(VkCommandBuffer commandBuffer, const ViewParams& view, std::string_view name)
{
  ZoneScoped;
  TracyVkZoneTransient(tracyVkContext_, tracyProfileVar, commandBuffer, name.data(), true);
  auto ctx = Fvog::Context(*device_, commandBuffer);
  auto marker = ctx.MakeScopedDebugMarker(name.data(), {.5f, .5f, 1.0f, 1.0f});

  ctx.Barrier();
  ctx.TeenyBufferUpdate(*viewBuffer, view);
  
  ctx.TeenyBufferUpdate(*meshletIndirectCommand,
    Fvog::DrawIndexedIndirectCommand{
      .indexCount    = 0,
      .instanceCount = 1,
      .firstIndex    = 0,
      .vertexOffset  = 0,
      .firstInstance = 0,
    });

  // Clear groupCountX
  cullTrianglesDispatchParams->FillData(commandBuffer, {.size = sizeof(uint32_t)});

  ctx.Barrier();
  
  ctx.BindComputePipeline(cullMeshletsPipeline);

  auto vsmPushConstants = vsmContext.GetPushConstants();

  auto visbufferPushConstants = CullMeshletsPushConstants{
    .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
    .meshletInstancesIndex = meshletInstancesBuffer->GetDeviceBuffer().GetResourceHandle().index,
    .meshletDataIndex = meshletBuffer->GetResourceHandle().index,
    .transformsIndex = transformBuffer->GetDeviceBuffer().GetResourceHandle().index,
    .indirectDrawIndex = meshletIndirectCommand->GetResourceHandle().index,
    .viewIndex = viewBuffer->GetResourceHandle().index,

    .pageTablesIndex = vsmPushConstants.pageTablesIndex,
    .physicalPagesIndex = vsmPushConstants.physicalPagesIndex,
    .vsmBitmaskHzbIndex = vsmPushConstants.vsmBitmaskHzbIndex,
    .vsmUniformsBufferIndex = vsmPushConstants.vsmUniformsBufferIndex,
    .clipmapUniformsBufferIndex = vsmSun.clipmapUniformsBuffer_.GetResourceHandle().index,
    .nearestSamplerIndex = nearestSampler.GetResourceHandle().index,

    .hzbIndex = frame.hzb->ImageView().GetSampledResourceHandle().index,
    .hzbSamplerIndex = hzbSampler.GetResourceHandle().index,
    .cullTrianglesDispatchIndex = cullTrianglesDispatchParams->GetResourceHandle().index,
    .visibleMeshletsIndex = visibleMeshletIds->GetResourceHandle().index,
    .debugAabbBufferIndex = debugGpuAabbsBuffer->GetResourceHandle().index,
    .debugRectBufferIndex = debugGpuRectsBuffer->GetResourceHandle().index,
  };
  ctx.SetPushConstants(visbufferPushConstants);
  
  ctx.DispatchInvocations((uint32_t)sceneFlattened.meshletInstances.size(), 1, 1);
  
  ctx.Barrier();
  
  ctx.BindComputePipeline(cullTrianglesPipeline);
  visbufferPushConstants.meshletPrimitivesIndex = primitiveBuffer->GetResourceHandle().index;
  visbufferPushConstants.meshletVerticesIndex = vertexBuffer->GetResourceHandle().index;
  visbufferPushConstants.meshletIndicesIndex = indexBuffer->GetResourceHandle().index;
  visbufferPushConstants.indexBufferIndex = instancedMeshletBuffer->GetResourceHandle().index;
  ctx.SetPushConstants(visbufferPushConstants);

  ctx.DispatchIndirect(cullTrianglesDispatchParams.value());

  //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::INDEX_BUFFER_BIT | Fwog::MemoryBarrierBit::COMMAND_BUFFER_BIT);
  ctx.Barrier();
}

void FrogRenderer2::OnRender([[maybe_unused]] double dt, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
{
  ZoneScoped;

  if (clearDebugAabbsEachFrame)
  {
    debugGpuAabbsBuffer->FillData(commandBuffer, {.offset = offsetof(Fvog::DrawIndirectCommand, instanceCount), .size = sizeof(uint32_t), .data = 0});
  }

  if (clearDebugRectsEachFrame)
  {
    debugGpuRectsBuffer->FillData(commandBuffer, {.offset = offsetof(Fvog::DrawIndirectCommand, instanceCount), .size = sizeof(uint32_t), .data = 0});
  }

  std::swap(frame.gDepth, frame.gDepthPrev);
  std::swap(frame.gNormalAndFaceNormal, frame.gNormaAndFaceNormallPrev);

  shadingUniforms.sunDir = glm::vec4(PolarToCartesian(sunElevation, sunAzimuth), 0);
  shadingUniforms.sunStrength = glm::vec4{sunStrength * sunColor, 0};

  auto ctx = Fvog::Context(*device_, commandBuffer);

  const float fsr2LodBias = fsr2Enable ? log2(float(renderInternalWidth) / float(renderOutputWidth)) - 1.0f : 0.0f;

  {
    ZoneScopedN("Update GPU Buffers");
    auto marker = ctx.MakeScopedDebugMarker("Update Buffers");

    tonemapUniformBuffer.UpdateData(commandBuffer, tonemapUniforms);

    auto gpuMaterials = std::vector<Utility::GpuMaterial>(scene.materials.size());
    std::ranges::transform(scene.materials, gpuMaterials.begin(), [](const auto& mat) { return mat.gpuMaterial; });

    materialStorageBuffer->UpdateData(commandBuffer, gpuMaterials);
    lightBuffer->UpdateData(commandBuffer, sceneFlattened.lights);
    transformBuffer->UpdateData(commandBuffer, sceneFlattened.transforms);
    meshletInstancesBuffer->UpdateData(commandBuffer, sceneFlattened.meshletInstances);
    // VSM lod bias corresponds to upscaling lod bias, otherwise shadows become blocky as the upscaling ratio increases.
    auto actualVsmUniforms = vsmUniforms;
    actualVsmUniforms.lodBias += fsr2LodBias;
    vsmContext.UpdateUniforms(commandBuffer, actualVsmUniforms);
    ctx.Barrier();
  }

  vkCmdBindDescriptorSets(
    commandBuffer,
    VK_PIPELINE_BIND_POINT_COMPUTE,
    device_->defaultPipelineLayout,
    0,
    1,
    &device_->descriptorSet_,
    0,
    nullptr);

  
  vkCmdBindDescriptorSets(
    commandBuffer,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    device_->defaultPipelineLayout,
    0,
    1,
    &device_->descriptorSet_,
    0,
    nullptr);











  
  const auto materialSampler = Fvog::Sampler(*device_, {
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .mipLodBias = fsr2LodBias,
    .maxAnisotropy = 16.0f,
  });
  
  const auto jitterOffset = fsr2Enable ? GetJitterOffset((uint32_t)device_->frameNumber, renderInternalWidth, renderInternalHeight, renderOutputWidth) : glm::vec2{};
  const auto jitterMatrix = glm::translate(glm::mat4(1), glm::vec3(jitterOffset, 0));
  //const auto projUnjittered = glm::perspectiveZO(cameraFovY, aspectRatio, cameraNear, cameraFar);
  const auto projUnjittered = Math::InfReverseZPerspectiveRH(cameraFovyRadians, aspectRatio, cameraNearPlane);
  const auto projJittered = jitterMatrix * projUnjittered;
  
  // Set global uniforms
  const uint32_t meshletCount = (uint32_t)sceneFlattened.meshletInstances.size();
  const auto viewProj = projJittered * mainCamera.GetViewMatrix();
  const auto viewProjUnjittered = projUnjittered * mainCamera.GetViewMatrix();

  if (device_->frameNumber == 1)
  {
    vsmSun.UpdateExpensive(commandBuffer, mainCamera.position, glm::vec3{-shadingUniforms.sunDir}, vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
  }
  else
  {
    vsmSun.UpdateOffset(commandBuffer, mainCamera.position);
  }

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
    .viewport = {0.0f, 0.0f, static_cast<float>(renderInternalWidth), static_cast<float>(renderInternalHeight)},
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

  ctx.Barrier();

  if (executeMeshletGeneration)
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eCullMeshletsMain);
    CullMeshletsForView(commandBuffer, mainView, "Cull Meshlets Main");
  }

  ctx.ImageBarrierDiscard(*frame.visbuffer, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(*frame.gDepth,    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  auto visbufferAttachment = Fvog::RenderColorAttachment{
    .texture = frame.visbuffer->ImageView(),
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .clearValue = {~0u, ~0u, ~0u, ~0u},
  };
  auto visbufferDepthAttachment = Fvog::RenderDepthStencilAttachment{
    .texture = frame.gDepth->ImageView(),
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .clearValue = {.depth = FAR_DEPTH},
  };

  ctx.BeginRendering({
    .name = "Main Visbuffer Pass",
    .colorAttachments = {&visbufferAttachment, 1},
    .depthAttachment = visbufferDepthAttachment,
  });
  {
    // TIME_SCOPE_GPU(StatGroup::eMainGpu, eRenderVisbufferMain);
    ctx.BindGraphicsPipeline(visbufferPipeline);
    auto visbufferArguments = VisbufferPushConstants{
      .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .meshletInstancesIndex = meshletInstancesBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .meshletDataIndex = meshletBuffer->GetResourceHandle().index,
      .meshletPrimitivesIndex = primitiveBuffer->GetResourceHandle().index,
      .meshletVerticesIndex = vertexBuffer->GetResourceHandle().index,
      .meshletIndicesIndex = indexBuffer->GetResourceHandle().index,
      .transformsIndex = transformBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .indirectDrawIndex = meshletIndirectCommand->GetResourceHandle().index,
      .materialsIndex = materialStorageBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .viewIndex = viewBuffer->GetResourceHandle().index,
    };
    ctx.SetPushConstants(visbufferArguments);
    ctx.BindIndexBuffer(*instancedMeshletBuffer, 0, VK_INDEX_TYPE_UINT32);
    ctx.DrawIndexedIndirect(*meshletIndirectCommand, 0, 1, 0);
  }
  ctx.EndRendering();

  // VSMs
  {
    const auto debugMarker = ctx.MakeScopedDebugMarker("Virtual Shadow Maps");
    TracyVkZone(tracyVkContext_, commandBuffer, "Virtual Shadow Maps")
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eVsm);

    {
      //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmResetPageVisibility);
      TracyVkZone(tracyVkContext_, commandBuffer, "Reset Page Visibility")
      vsmContext.ResetPageVisibility(commandBuffer);
    }
    {
      //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmMarkVisiblePages);
      TracyVkZone(tracyVkContext_, commandBuffer, "Mark Visible Pages")
      vsmSun.MarkVisiblePages(commandBuffer, frame.gDepth.value(), globalUniformsBuffer.GetDeviceBuffer());
    }
    {
      //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmFreeNonVisiblePages);
      TracyVkZone(tracyVkContext_, commandBuffer, "Free Non-Visible Pages")
      vsmContext.FreeNonVisiblePages(commandBuffer);
    }
    {
      //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmAllocatePages);
      TracyVkZone(tracyVkContext_, commandBuffer, "Allocate Requested Pages")
      vsmContext.AllocateRequestedPages(commandBuffer);
    }
    {
      //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmGenerateHpb);
      TracyVkZone(tracyVkContext_, commandBuffer, "Generate HPB")
      vsmSun.GenerateBitmaskHzb(commandBuffer);
    }
    {
      //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmClearDirtyPages);
      TracyVkZone(tracyVkContext_, commandBuffer, "Clear Dirty Pages")
      vsmContext.ClearDirtyPages(commandBuffer);
    }

    //TIME_SCOPE_GPU(StatGroup::eVsm, eVsmRenderDirtyPages);

    // Sun VSMs
    for (uint32_t i = 0; i < vsmSun.NumClipmaps(); i++)
    {
      TracyVkZone(tracyVkContext_, commandBuffer, "Render Clipmap")
      auto sunCurrentClipmapView = ViewParams{
        .oldViewProj = vsmSun.GetProjections()[i] * vsmSun.GetViewMatrices()[i],
        .proj = vsmSun.GetProjections()[i],
        .view = vsmSun.GetViewMatrices()[i],
        .viewProj = vsmSun.GetProjections()[i] * vsmSun.GetViewMatrices()[i],
        .viewProjStableForVsmOnly = vsmSun.GetProjections()[i] * vsmSun.GetStableViewMatrix(),
        .cameraPos = {}, // unused
        .viewport = {0.f, 0.f, vsmSun.GetExtent().width, vsmSun.GetExtent().height},
        .type = ViewType::VIRTUAL,
        .virtualTableIndex = vsmSun.GetClipmapTableIndices()[i],
      };
      Math::MakeFrustumPlanes(sunCurrentClipmapView.viewProj, sunCurrentClipmapView.frustumPlanes);

      CullMeshletsForView(commandBuffer, sunCurrentClipmapView, "Cull Sun VSM Meshlets, View " + std::to_string(i));

      const auto vsmExtent = Fwog::Extent2D{Techniques::VirtualShadowMaps::maxExtent, Techniques::VirtualShadowMaps::maxExtent};
      ctx.Barrier();
      ctx.BeginRendering({
        .name = "Render Clipmap",
        .viewport = VkViewport{0, 0, (float)vsmExtent.width, (float)vsmExtent.height, 0, 1},
      });
      
      ctx.BindGraphicsPipeline(vsmShadowPipeline);

      auto pushConstants = vsmContext.GetPushConstants();
      pushConstants.meshletInstancesIndex = meshletInstancesBuffer->GetDeviceBuffer().GetResourceHandle().index;
      pushConstants.meshletDataIndex = meshletBuffer->GetResourceHandle().index;
      pushConstants.meshletPrimitivesIndex = primitiveBuffer->GetResourceHandle().index;
      pushConstants.meshletVerticesIndex = vertexBuffer->GetResourceHandle().index;
      pushConstants.meshletIndicesIndex = indexBuffer->GetResourceHandle().index;
      pushConstants.transformsIndex = transformBuffer->GetDeviceBuffer().GetResourceHandle().index;
      pushConstants.globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index;
      pushConstants.viewIndex = viewBuffer->GetResourceHandle().index;
      pushConstants.materialsIndex = materialStorageBuffer->GetDeviceBuffer().GetResourceHandle().index;
      pushConstants.materialSamplerIndex = materialSampler.GetResourceHandle().index;
      pushConstants.clipmapLod = vsmSun.GetClipmapTableIndices()[i];
      pushConstants.clipmapUniformsBufferIndex = vsmSun.clipmapUniformsBuffer_.GetResourceHandle().index;

      ctx.BindIndexBuffer(*instancedMeshletBuffer, 0, VK_INDEX_TYPE_UINT32);
      
      ctx.SetPushConstants(pushConstants);
      ctx.DrawIndexedIndirect(*meshletIndirectCommand, 0, 1, 0);
      ctx.EndRendering();
    }
  }

  // TODO: remove when descriptor indexing sync validation does not give false positives
  ctx.Barrier();
  
  ctx.ImageBarrier(*frame.gDepth, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

  if (generateHizBuffer)
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eHzb);
    {
      auto marker = ctx.MakeScopedDebugMarker("HZB Build Pass", {.5f, .5f, 1.0f, 1.0f});
      ctx.SetPushConstants(HzbCopyPushConstants{
        .hzbIndex = frame.hzb->ImageView().GetStorageResourceHandle().index,
        .depthIndex = frame.gDepth->ImageView().GetSampledResourceHandle().index,
        .depthSamplerIndex = hzbSampler.GetResourceHandle().index,
      });

      ctx.BindComputePipeline(hzbCopyPipeline);
      uint32_t hzbCurrentWidth = frame.hzb->GetCreateInfo().extent.width;
      uint32_t hzbCurrentHeight = frame.hzb->GetCreateInfo().extent.height;
      const uint32_t hzbLevels = frame.hzb->GetCreateInfo().mipLevels;
      ctx.Dispatch((hzbCurrentWidth + 15) / 16, (hzbCurrentHeight + 15) / 16, 1);

      // Sync val complains about WAR for colorLdrWindowRes in the next dispatch, even though it's definitely not accessed
      ctx.Barrier();

      ctx.BindComputePipeline(hzbReducePipeline);
      for (uint32_t level = 1; level < hzbLevels; ++level)
      {
        ctx.ImageBarrier(*frame.hzb, VK_IMAGE_LAYOUT_GENERAL);
        auto& prevHzbView = frame.hzb->CreateSingleMipView(level - 1, "prevHzbMip");
        auto& curHzbView = frame.hzb->CreateSingleMipView(level, "curHzbMip");

        ctx.SetPushConstants(HzbReducePushConstants{
          .prevHzbIndex = prevHzbView.GetStorageResourceHandle().index,
          .curHzbIndex = curHzbView.GetStorageResourceHandle().index,
        });

        hzbCurrentWidth = std::max(1u, hzbCurrentWidth >> 1);
        hzbCurrentHeight = std::max(1u, hzbCurrentHeight >> 1);
        ctx.Dispatch((hzbCurrentWidth + 15) / 16, (hzbCurrentHeight + 15) / 16, 1);
      }
      ctx.ImageBarrier(*frame.hzb, VK_IMAGE_LAYOUT_GENERAL);
    }
  }
  else
  {
    const uint32_t hzbLevels = frame.hzb->GetCreateInfo().mipLevels;
    for (uint32_t level = 0; level < hzbLevels; level++)
    {
      constexpr float farDepth = FAR_DEPTH;
      
      ctx.ClearTexture(*frame.hzb, {.color = {farDepth}, .baseMipLevel = level});
    }
  }

  ctx.Barrier();
  ctx.ImageBarrierDiscard(*frame.gAlbedo,              VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(*frame.gNormalAndFaceNormal, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrier       (*frame.gDepth,               VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(*frame.gSmoothVertexNormal,  VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(*frame.gEmission,            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(*frame.gMetallicRoughnessAo, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(*frame.gMotion,              VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  Fvog::RenderColorAttachment gBufferAttachments[] = {
    {
      .texture = frame.gAlbedo->ImageView(),
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE
    },
    {
      .texture = frame.gMetallicRoughnessAo->ImageView(),
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    },
    {
      .texture = frame.gNormalAndFaceNormal->ImageView(),
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    },
    {
      .texture = frame.gSmoothVertexNormal->ImageView(),
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    },
    {
      .texture = frame.gEmission->ImageView(),
      .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    },
    {
      .texture = frame.gMotion->ImageView(),
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .clearValue = {0.f, 0.f, 0.f, 0.f},
    },
  };

  ctx.BeginRendering({
    .name = "Resolve Visbuffer Pass",
    .colorAttachments = gBufferAttachments,
  });

  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eResolveVisbuffer);
    ctx.BindGraphicsPipeline(visbufferResolvePipeline);
    
    auto pushConstants = VisbufferPushConstants{
      .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .meshletInstancesIndex = meshletInstancesBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .meshletDataIndex = meshletBuffer->GetResourceHandle().index,
      .meshletPrimitivesIndex = primitiveBuffer->GetResourceHandle().index,
      .meshletVerticesIndex = vertexBuffer->GetResourceHandle().index,
      .meshletIndicesIndex = indexBuffer->GetResourceHandle().index,
      .transformsIndex = transformBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .materialsIndex = materialStorageBuffer->GetDeviceBuffer().GetResourceHandle().index,
      .materialSamplerIndex = materialSampler.GetResourceHandle().index,

      .visbufferIndex = frame.visbuffer->ImageView().GetSampledResourceHandle().index,
    };

    ctx.SetPushConstants(pushConstants);
    ctx.Draw(3, 1, 0, 0);
  }

  ctx.EndRendering();

  ctx.ImageBarrier(*frame.gAlbedo,                  VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrier(*frame.gNormalAndFaceNormal,     VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrier(*frame.gDepth,                   VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrier(*frame.gSmoothVertexNormal,      VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrier(*frame.gEmission,                VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrier(*frame.gMetallicRoughnessAo,     VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  ctx.ImageBarrierDiscard(*frame.colorHdrRenderRes, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  // shading pass (full screen tri)
  auto shadingColorAttachment = Fvog::RenderColorAttachment{
    .texture = frame.colorHdrRenderRes->ImageView(),
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .clearValue = {.1f, .3f, .5f, 0.0f},
  };
  ctx.BeginRendering({
    .name = "Shading",
    .colorAttachments = {&shadingColorAttachment, 1},
  });
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eShadeOpaque);

    // Certain VSM push constants are used by the shading pass
    auto vsmPushConstants = vsmContext.GetPushConstants();
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

      .pageTablesIndex = vsmPushConstants.pageTablesIndex,
      .physicalPagesIndex = vsmPushConstants.physicalPagesIndex,
      .vsmBitmaskHzbIndex = vsmPushConstants.vsmBitmaskHzbIndex,
      .vsmUniformsBufferIndex = vsmPushConstants.vsmUniformsBufferIndex,
      .dirtyPageListBufferIndex = vsmPushConstants.dirtyPageListBufferIndex,
      .clipmapUniformsBufferIndex = vsmSun.clipmapUniformsBuffer_.GetResourceHandle().index,
      .nearestSamplerIndex = vsmPushConstants.nearestSamplerIndex,
    });

    ctx.Draw(3, 1, 0, 0);
  }
  ctx.EndRendering();

  // After shading, we render debug geometry
  auto debugDepthAttachment = Fvog::RenderDepthStencilAttachment{
    .texture = frame.gDepth->ImageView(),
    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
  };

  auto colorAttachments = std::vector<Fvog::RenderColorAttachment>{};
  colorAttachments.emplace_back(frame.colorHdrRenderRes->ImageView(), VK_ATTACHMENT_LOAD_OP_LOAD);
  if (fsr2Enable)
  {
    ctx.ImageBarrierDiscard(*frame.gReactiveMask, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
    colorAttachments.emplace_back(frame.gReactiveMask->ImageView(), VK_ATTACHMENT_LOAD_OP_CLEAR, Fvog::ClearColorValue{0.0f});
  }

  ctx.ImageBarrier(*frame.gDepth, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrier(*frame.colorHdrRenderRes, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  ctx.BeginRendering({
    .name = "Debug Geometry",
    .colorAttachments = colorAttachments,
    .depthAttachment = debugDepthAttachment,
  });
  {
    // TIME_SCOPE_GPU(StatGroup::eMainGpu, eDebugGeometry);
    //  Lines
    if (!debugLines.empty())
    {
      if (!lineVertexBuffer || lineVertexBuffer->Size() < debugLines.size() * sizeof(Debug::Line))
      {
        lineVertexBuffer.emplace(*device_, (uint32_t)debugLines.size(), "Debug Lines");
      }
      lineVertexBuffer->UpdateData(commandBuffer, debugLines);

      ctx.BindGraphicsPipeline(debugLinesPipeline);
      ctx.SetPushConstants(DebugLinesPushConstants{
        .vertexBufferIndex   = lineVertexBuffer->GetDeviceBuffer().GetResourceHandle().index,
        .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      });
      ctx.Draw(uint32_t(debugLines.size() * 2), 1, 0, 0);
    }

    //// GPU-generated geometry past here
    //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::COMMAND_BUFFER_BIT | Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT);

    // AABBs
    if (drawDebugAabbs)
    {
      ctx.BindGraphicsPipeline(debugAabbsPipeline);
      ctx.SetPushConstants(DebugAabbArguments{
        .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
        .debugAabbBufferIndex = debugGpuAabbsBuffer->GetResourceHandle().index,
      });
      ctx.DrawIndirect(debugGpuAabbsBuffer.value(), 0, 1, 0);
    }

    // Rects
    if (drawDebugRects)
    {
      ctx.BindGraphicsPipeline(debugRectsPipeline);
      ctx.SetPushConstants(DebugRectArguments{
        .debugRectBufferIndex = debugGpuRectsBuffer->GetResourceHandle().index,
      });
      ctx.DrawIndirect(debugGpuRectsBuffer.value(), 0, 1, 0);
    }
  }
  ctx.EndRendering();

  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eAutoExposure);
    ctx.ImageBarrier(frame.colorHdrRenderRes.value(), VK_IMAGE_LAYOUT_GENERAL);
    autoExposure.Apply(commandBuffer, {
      .image = frame.colorHdrRenderRes.value(),
      .exposureBuffer = exposureBuffer,
      .deltaTime = static_cast<float>(dt),
      .adjustmentSpeed = autoExposureAdjustmentSpeed,
      .targetLuminance = autoExposureTargetLuminance,
      .logMinLuminance = autoExposureLogMinLuminance,
      .logMaxLuminance = autoExposureLogMaxLuminance,
    });
  }

#ifdef FROGRENDER_FSR2_ENABLE
  if (fsr2Enable)
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eFsr2);
    //static Fwog::TimerQueryAsync timer(5);
    //if (auto t = timer.PopTimestamp())
    //{
    //  fsr2Performance = *t / 10e5;
    //}
    //Fwog::TimerScoped scopedTimer(timer);
    
    auto marker = ctx.MakeScopedDebugMarker("FSR 2");

    ctx.ImageBarrier(*frame.colorHdrRenderRes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.gDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.gMotion, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx.ImageBarrier(*frame.gReactiveMask, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx.ImageBarrierDiscard(*frame.colorHdrWindowRes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    float jitterX{};
    float jitterY{};
    ffxFsr2GetJitterOffset(&jitterX, &jitterY, (int32_t)device_->frameNumber, ffxFsr2GetJitterPhaseCount(renderInternalWidth, renderOutputWidth));

    FfxFsr2DispatchDescription dispatchDesc{
      .commandList = ffxGetCommandListVK(commandBuffer),
      .color = ffxGetTextureResourceVK(&fsr2Context,
                                       frame.colorHdrRenderRes->Image(),
                                       frame.colorHdrRenderRes->ImageView(),
                                       renderInternalWidth,
                                       renderInternalHeight,
                                       Fvog::detail::FormatToVk(frame.colorHdrRenderRes->GetCreateInfo().format)),
      .depth = ffxGetTextureResourceVK(&fsr2Context,
                                       frame.gDepth->Image(),
                                       frame.gDepth->ImageView(),
                                       renderInternalWidth,
                                       renderInternalHeight,
                                       Fvog::detail::FormatToVk(frame.gDepth->GetCreateInfo().format)),
      .motionVectors = ffxGetTextureResourceVK(&fsr2Context,
                                               frame.gMotion->Image(),
                                               frame.gMotion->ImageView(),
                                               renderInternalWidth,
                                               renderInternalHeight,
                                               Fvog::detail::FormatToVk(frame.gMotion->GetCreateInfo().format)),
      .exposure = {},
      .reactive = ffxGetTextureResourceVK(&fsr2Context,
                                          frame.gReactiveMask->Image(),
                                          frame.gReactiveMask->ImageView(),
                                          renderInternalWidth,
                                          renderInternalHeight,
                                          Fvog::detail::FormatToVk(frame.gReactiveMask->GetCreateInfo().format)),
      .transparencyAndComposition = {},
      .output = ffxGetTextureResourceVK(&fsr2Context,
                                        frame.colorHdrWindowRes->Image(),
                                        frame.colorHdrWindowRes->ImageView(),
                                        renderOutputWidth,
                                        renderOutputHeight,
                                        Fvog::detail::FormatToVk(frame.colorHdrWindowRes->GetCreateInfo().format)),
      .jitterOffset = {jitterX, jitterY},
      .motionVectorScale = {float(renderInternalWidth), float(renderInternalHeight)},
      .renderSize = {renderInternalWidth, renderInternalHeight},
      .enableSharpening = fsr2Sharpness != 0,
      .sharpness = fsr2Sharpness,
      .frameTimeDelta = static_cast<float>(dt * 1000.0),
      .preExposure = 1,
      .reset = false,
      .cameraNear = std::numeric_limits<float>::max(),
      .cameraFar = cameraNearPlane,
      .cameraFovAngleVertical = cameraFovyRadians,
      .viewSpaceToMetersFactor = 1,
    };

    if (auto err = ffxFsr2ContextDispatch(&fsr2Context, &dispatchDesc); err != FFX_OK)
    {
      printf("FSR 2 error: %d\n", err);
    }

    // Re-apply states that application assumes
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, device_->defaultPipelineLayout, 0, 1, &device_->descriptorSet_, 0, nullptr);
    *frame.colorHdrWindowRes->currentLayout = VK_IMAGE_LAYOUT_GENERAL;
  }
#endif

  if (bloomEnable)
  {
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eBloom);
    bloom.Apply(commandBuffer, {
      .target = fsr2Enable ? frame.colorHdrWindowRes.value() : frame.colorHdrRenderRes.value(),
      .scratchTexture = frame.colorHdrBloomScratchBuffer.value(),
      .passes = bloomPasses,
      .strength = bloomStrength,
      .width = bloomWidth,
      .useLowPassFilterOnFirstPass = bloomUseLowPassFilter,
    });
  }
  
  {
    auto marker = ctx.MakeScopedDebugMarker("Postprocessing", {.5f, .5f, 1.0f, 1.0f});
    //TIME_SCOPE_GPU(StatGroup::eMainGpu, eResolveImage);
    ctx.Barrier();
    ctx.ImageBarrierDiscard(*frame.colorLdrWindowRes, VK_IMAGE_LAYOUT_GENERAL);

    ctx.BindComputePipeline(tonemapPipeline);
    ctx.SetPushConstants(TonemapArguments{
      .sceneColorIndex = (fsr2Enable ? frame.colorHdrWindowRes.value() : frame.colorHdrRenderRes.value()).ImageView().GetSampledResourceHandle().index,
      .noiseIndex = noiseTexture->ImageView().GetSampledResourceHandle().index,
      .nearestSamplerIndex = nearestSampler.GetResourceHandle().index,
      .exposureIndex = exposureBuffer.GetResourceHandle().index,
      .tonemapUniformsIndex = tonemapUniformBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .outputImageIndex = frame.colorLdrWindowRes->ImageView().GetStorageResourceHandle().index,
    });
    ctx.DispatchInvocations(frame.colorLdrWindowRes.value().GetCreateInfo().extent);
    ctx.ImageBarrier(*frame.colorLdrWindowRes, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  }

  ctx.Barrier(); // Appease sync val
  ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  // GUI is not rendered, draw directly to screen instead
  if (!showGui)
  {
    auto marker = ctx.MakeScopedDebugMarker("Copy to swapchain");

    const auto renderArea = VkRect2D{.offset = {}, .extent = {renderOutputWidth, renderOutputHeight}};
    vkCmdBeginRendering(commandBuffer, Address(VkRenderingInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = renderArea,
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = Address(VkRenderingAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainImageViewsUnorm_[swapchainImageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      }),
    }));

    vkCmdSetViewport(commandBuffer, 0, 1, Address(VkViewport{0, 0, (float)renderOutputWidth, (float)renderOutputHeight, 0, 1}));
    vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
    ctx.BindGraphicsPipeline(debugTexturePipeline);
    auto pushConstants = DebugTextureArguments{
      .textureIndex = frame.colorLdrWindowRes->ImageView().GetSampledResourceHandle().index,
      .samplerIndex = nearestSampler.GetResourceHandle().index,
    };

    ctx.SetPushConstants(pushConstants);
    ctx.Draw(3, 1, 0, 0);

    vkCmdEndRendering(commandBuffer);
  }














  

  
  // swapchainImages_[swapchainImageIndex] needs to be COLOR_ATTACHMENT_OPTIMAL after this function returns
  ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void FrogRenderer2::MakeStaticSceneBuffers(VkCommandBuffer commandBuffer)
{
  auto ctx = Fvog::Context(*device_, commandBuffer);
  
  ctx.Barrier();

  vertexBuffer = Fvog::TypedBuffer<Utility::Vertex>(*device_, {(uint32_t)scene.vertices.size()}, "Vertex Buffer");
  vertexBuffer->UpdateDataExpensive(commandBuffer, scene.vertices);

  indexBuffer = Fvog::TypedBuffer<uint32_t>(*device_, {(uint32_t)scene.indices.size()}, "Index Buffer");
  indexBuffer->UpdateDataExpensive(commandBuffer, scene.indices);

  primitiveBuffer = Fvog::TypedBuffer<uint8_t>(*device_, {(uint32_t)scene.primitives.size()}, "Primitive Buffer");
  primitiveBuffer->UpdateDataExpensive(commandBuffer, scene.primitives);

  meshletBuffer = Fvog::TypedBuffer<Utility::Meshlet>(*device_, {(uint32_t)scene.meshlets.size()}, "Meshlet Buffer");
  meshletBuffer->UpdateDataExpensive(commandBuffer, scene.meshlets);

  std::vector<Utility::GpuMaterial> materials(scene.materials.size());
  std::transform(scene.materials.begin(), scene.materials.end(), materials.begin(), [](const auto& m) { return m.gpuMaterial; });
  materialStorageBuffer = Fvog::NDeviceBuffer<Utility::GpuMaterial>(*device_, (uint32_t)materials.size(), "Material Buffer");
  materialStorageBuffer->GetDeviceBuffer().UpdateDataExpensive(commandBuffer, std::span(materials));

  ctx.Barrier();
}

void FrogRenderer2::OnPathDrop([[maybe_unused]] std::span<const char*> paths)
{
  for (const auto& path : paths)
  {
    Utility::LoadModelFromFileMeshlet(*device_, scene, path, glm::identity<glm::mat4>());
  }

  device_->ImmediateSubmit(
    [&](VkCommandBuffer cmd) {
      MakeStaticSceneBuffers(cmd);
    });
}
