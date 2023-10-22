#include "FrogRenderer.h"

#include "Pipelines.h"

#include <Fwog/BasicTypes.h>
#include <Fwog/Buffer.h>
#include <Fwog/DebugMarker.h>
#include <Fwog/Pipeline.h>
#include <Fwog/Rendering.h>
#include <Fwog/Shader.h>
#include <Fwog/Texture.h>
#include <Fwog/Context.h>
#include <Fwog/Timer.h>
#include <Fwog/detail/ApiToEnum.h>

#include "shaders/Config.shared.h"

#include <tracy/Tracy.hpp>

#include <GLFW/glfw3.h>

#include <stb_image.h>

#include <glm/gtx/transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <thread>

static glm::mat4 InfReverseZPerspectiveRH(float fovY_radians, float aspectWbyH, float zNear)
{
  float f = 1.0f / tan(fovY_radians / 2.0f);
  return glm::mat4(f / aspectWbyH, 0.0f, 0.0f, 0.0f, 0.0f, f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, zNear, 0.0f);
}

static constexpr uint32_t previousPower2(uint32_t x)
{
  uint32_t v = 1;
  while ((v << 1) < x)
  {
    v <<= 1;
  }
  return v;
}

static void MakeFrustumPlanes(const glm::mat4& viewProj, glm::vec4(&planes)[6])
{
  for (auto i = 0; i < 4; ++i) { planes[0][i] = viewProj[i][3] + viewProj[i][0]; }
  for (auto i = 0; i < 4; ++i) { planes[1][i] = viewProj[i][3] - viewProj[i][0]; }
  for (auto i = 0; i < 4; ++i) { planes[2][i] = viewProj[i][3] + viewProj[i][1]; }
  for (auto i = 0; i < 4; ++i) { planes[3][i] = viewProj[i][3] - viewProj[i][1]; }
  for (auto i = 0; i < 4; ++i) { planes[4][i] = viewProj[i][3] + viewProj[i][2]; }
  for (auto i = 0; i < 4; ++i) { planes[5][i] = viewProj[i][3] - viewProj[i][2]; }

  for (auto& plane : planes)
  {
    plane /= glm::length(glm::vec3(plane));
    plane.w = -plane.w;
  }
}

// Zero-origin unprojection. E.g., pass sampled depth, screen UV, and invViewProj to get a world-space pos
static glm::vec3 UnprojectUV_ZO(float depth, glm::vec2 uv, const glm::mat4& invXProj)
{
  glm::vec4 ndc = glm::vec4(uv * 2.0f - 1.0f, depth, 1.0f);
  glm::vec4 world = invXProj * ndc;
  return glm::vec3(world) / world.w;
}

static std::vector<Debug::Line> GenerateFrustumWireframe(const glm::mat4& invViewProj, const glm::vec4& color, float near, float far)
{
  auto lines = std::vector<Debug::Line>{};

  // Get frustum corners in world space
  auto tln = UnprojectUV_ZO(near, {0, 1}, invViewProj);
  auto trn = UnprojectUV_ZO(near, {1, 1}, invViewProj);
  auto bln = UnprojectUV_ZO(near, {0, 0}, invViewProj);
  auto brn = UnprojectUV_ZO(near, {1, 0}, invViewProj);

  // Far corners are lerped slightly to near in case it is an infinite projection
  auto tlf = UnprojectUV_ZO(glm::mix(far, near, 1e-5), {0, 1}, invViewProj);
  auto trf = UnprojectUV_ZO(glm::mix(far, near, 1e-5), {1, 1}, invViewProj);
  auto blf = UnprojectUV_ZO(glm::mix(far, near, 1e-5), {0, 0}, invViewProj);
  auto brf = UnprojectUV_ZO(glm::mix(far, near, 1e-5), {1, 0}, invViewProj);

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

FrogRenderer::FrogRenderer(const Application::CreateInfo& createInfo, std::optional<std::string_view> filename, float scale, bool binary)
  : Application(createInfo),
    // Create constant-size buffers
    globalUniformsBuffer(Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    shadingUniformsBuffer(Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    shadowUniformsBuffer(shadowUniforms, Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    rsmUniforms(Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    // Create the pipelines used in the application
    meshletGeneratePipeline(Pipelines::MeshletGenerate()),
    hzbCopyPipeline(Pipelines::HzbCopy()),
    hzbReducePipeline(Pipelines::HzbReduce()),
    visbufferPipeline(Pipelines::Visbuffer()),
    shadowMainPipeline(Pipelines::ShadowMain()),
    materialDepthPipeline(Pipelines::MaterialDepth()),
    visbufferResolvePipeline(Pipelines::VisbufferResolve()),
    // rsmScenePipeline(CreateShadowPipeline()),
    shadingPipeline(Pipelines::Shading()),
    tonemapPipeline(Pipelines::Tonemap()),
    debugTexturePipeline(Pipelines::DebugTexture()),
    debugLinesPipeline(Pipelines::DebugLines()),
    debugAabbsPipeline(Pipelines::DebugAabbs()),
    debugRectsPipeline(Pipelines::DebugRects()),
    tonemapUniformBuffer(Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    exposureBuffer(1.0f),
    vsmContext({
      .maxVsms = 64,
      .pageSize = {Techniques::VirtualShadowMaps::pageSize, Techniques::VirtualShadowMaps::pageSize},
      .numPages = 1024,
    }),
    vsmSun({
      .context = vsmContext,
      .virtualExtent = Techniques::VirtualShadowMaps::maxExtent,
      .numClipmaps = 10,
    }),
    vsmShadowPipeline(Pipelines::ShadowVsm()),
    vsmShadowUniformBuffer(Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    viewerUniformsBuffer(Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    viewerVsmPageTablesPipeline(Pipelines::ViewerVsm()),
    viewerVsmPhysicalPagesPipeline(Pipelines::ViewerVsmPhysicalPages()),
    viewerVsmBitmaskHzbPipeline(Pipelines::ViewerVsmBitmaskHzb())
{
  ZoneScoped;
  int x = 0;
  int y = 0;
  const auto noise = stbi_load("textures/bluenoise32.png", &x, &y, nullptr, 4);
  assert(noise);
  noiseTexture = Fwog::CreateTexture2D({static_cast<uint32_t>(x), static_cast<uint32_t>(y)}, Fwog::Format::R8G8B8A8_UNORM);
  noiseTexture->UpdateImage({
    .extent = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
    .format = Fwog::UploadFormat::RGBA,
    .type = Fwog::UploadType::UBYTE,
    .pixels = noise,
  });
  stbi_image_free(noise);

  InitGui();

  cursorIsActive = true;

  cameraSpeed = 3.5f;
  mainCamera.position.y = 1;
  
  debugGpuAabbsBuffer = Fwog::Buffer(sizeof(Fwog::DrawIndirectCommand) + sizeof(Debug::Aabb) * 100'000);
  debugGpuAabbsBuffer->FillData();
  debugGpuAabbsBuffer->FillData({.size = sizeof(Fwog::DrawIndirectCommand), .data = 0});
  debugGpuAabbsBuffer->FillData({.offset = offsetof(Fwog::DrawIndirectCommand, vertexCount), .size = sizeof(uint32_t), .data = 14});

  debugGpuRectsBuffer = Fwog::Buffer(sizeof(Fwog::DrawIndirectCommand) + sizeof(Debug::Rect) * 100'000);
  debugGpuRectsBuffer->FillData();
  debugGpuRectsBuffer->FillData({.size = sizeof(Fwog::DrawIndirectCommand), .data = 0});
  debugGpuRectsBuffer->FillData({.offset = offsetof(Fwog::DrawIndirectCommand, vertexCount), .size = sizeof(uint32_t), .data = 4});

  if (!filename)
  {
    Utility::LoadModelFromFileMeshlet(scene, "models/simple_scene.glb", glm::scale(glm::vec3{.5}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/light_test.glb", glm::scale(glm::vec3{.5}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "/run/media/master/Samsung S0/Dev/CLion/IrisVk/models/sponza/Sponza.gltf", glm::scale(glm::vec3{.125}), false);

    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/bistro_compressed.glb", glm::scale(glm::vec3{.5}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/modular_ruins_c_2.glb", glm::scale(glm::vec3{.5}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/building0.glb", glm::scale(glm::vec3{.05f}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/terrain.glb", glm::scale(glm::vec3{0.125f}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/terrain2_compressed.glb", glm::scale(glm::translate(glm::vec3(0, 100, 0)), glm::vec3{1000.0f}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/powerplant.glb", glm::scale(glm::vec3{1.0f}), true);

    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf", glm::scale(glm::vec3{.5}), false);

    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/Main/NewSponza_Main_Blender_glTF.gltf", glm::scale(glm::vec3{1}), false);

    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_compressed.glb", glm::scale(glm::vec3{1}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_curtains_compressed.glb", glm::scale(glm::vec3{1}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_ivy_compressed.glb", glm::scale(glm::vec3{1}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_tree_compressed.glb", glm::scale(glm::vec3{1}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/deccer_balls.gltf", glm::scale(glm::vec3{1}), false);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/2.0/MetalRoughSpheres/glTF-Binary/MetalRoughSpheres.glb", glm::scale(glm::vec3{1}), true);
  }
  else
  {
    Utility::LoadModelFromFileMeshlet(scene, *filename, glm::scale(glm::vec3{scale}), binary);
  }
  
  lightBuffer = Fwog::TypedBuffer<Utility::GpuLight>(scene.lights, Fwog::BufferStorageFlag::DYNAMIC_STORAGE);

  meshletBuffer = Fwog::TypedBuffer<Utility::Meshlet>(scene.meshlets);
  vertexBuffer = Fwog::TypedBuffer<Utility::Vertex>(scene.vertices);
  indexBuffer = Fwog::TypedBuffer<uint32_t>(scene.indices);
  primitiveBuffer = Fwog::TypedBuffer<uint8_t>(scene.primitives);
  transformBuffer = Fwog::TypedBuffer<glm::mat4>(scene.transforms);
  meshletIndirectCommand = Fwog::TypedBuffer<Fwog::DrawIndexedIndirectCommand>(Fwog::BufferStorageFlag::DYNAMIC_STORAGE);

  const auto maxPrimitives = scene.primitives.size() * 3;
  instancedMeshletBuffer = Fwog::TypedBuffer<uint32_t>(maxPrimitives);

  std::vector<Utility::GpuMaterial> materials(scene.materials.size());
  std::transform(scene.materials.begin(), scene.materials.end(), materials.begin(), [](const auto& m)
  {
    return m.gpuMaterial;
  });
  materialStorageBuffer = Fwog::TypedBuffer<Utility::GpuMaterial>(materials);

  std::vector<ObjectUniforms> meshUniforms;
  for (size_t i = 0; i < scene.transforms.size(); i++)
  {
    meshUniforms.push_back({scene.transforms[i]});
  }

  meshUniformBuffer.emplace(meshUniforms, Fwog::BufferStorageFlag::DYNAMIC_STORAGE);

  viewBuffer = Fwog::TypedBuffer<View>(Fwog::BufferStorageFlag::DYNAMIC_STORAGE);

  // clusterTexture({.imageType = Fwog::ImageType::TEX_3D,
  //                                      .format = Fwog::Format::R16G16_UINT,
  //                                      .extent = {16, 9, 24},
  //                                      .pageTableMipLevels = 1,
  //                                      .arrayLayers = 1,
  //                                      .sampleCount = Fwog::SampleCount::SAMPLES_1},
  //                                     "Cluster Texture");

  //// atomic counter + uint array
  // clusterIndicesBuffer = Fwog::Buffer(sizeof(uint32_t) + sizeof(uint32_t) * 10000);
  // const uint32_t zero = 0; // what it says on the tin
  // clusterIndicesBuffer.ClearSubData(0,
  //                                   clusterIndicesBuffer.Size(),
  //                                   Fwog::Format::R32_UINT,
  //                                   Fwog::UploadFormat::R,
  //                                   Fwog::UploadType::UINT,
  //                                   &zero);

  OnWindowResize(windowWidth, windowHeight);
}

void FrogRenderer::OnWindowResize(uint32_t newWidth, uint32_t newHeight)
{
  ZoneScoped;
  frame = {};

#ifdef FROGRENDER_FSR2_ENABLE
  // create FSR 2 context
  if (fsr2Enable)
  {
    if (!fsr2FirstInit)
    {
      ffxFsr2ContextDestroy(&fsr2Context);
    }
    frameIndex = 0;
    fsr2FirstInit = false;
    renderWidth = static_cast<uint32_t>(newWidth / fsr2Ratio);
    renderHeight = static_cast<uint32_t>(newHeight / fsr2Ratio);
    FfxFsr2ContextDescription contextDesc{
      .flags = FFX_FSR2_ENABLE_DEBUG_CHECKING | FFX_FSR2_ENABLE_AUTO_EXPOSURE | FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE |
               FFX_FSR2_ALLOW_NULL_DEVICE_AND_COMMAND_LIST | FFX_FSR2_ENABLE_DEPTH_INFINITE | FFX_FSR2_ENABLE_DEPTH_INVERTED,
      .maxRenderSize = {renderWidth, renderHeight},
      .displaySize = {newWidth, newHeight},
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
    fsr2ScratchMemory = std::make_unique<char[]>(ffxFsr2GetScratchMemorySizeGL());
    ffxFsr2GetInterfaceGL(&contextDesc.callbacks, fsr2ScratchMemory.get(), ffxFsr2GetScratchMemorySizeGL(), glfwGetProcAddress);
    ffxFsr2ContextCreate(&fsr2Context, &contextDesc);

    frame.gReactiveMask = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R8_UNORM, "Reactive Mask");
  }
  else
#endif
  {
    renderWidth = newWidth;
    renderHeight = newHeight;
  }

  // Visibility buffer textures
  frame.visbuffer = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R32_UINT, "visbuffer");
  frame.materialDepth = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::D32_FLOAT, "materialDepth");
  {
    const uint32_t hzbWidth = previousPower2(renderWidth);
    const uint32_t hzbHeight = previousPower2(renderHeight);
    const uint32_t hzbMips = 1 + static_cast<uint32_t>(glm::floor(glm::log2(static_cast<float>(glm::max(hzbWidth, hzbHeight)))));
    frame.hzb = Fwog::CreateTexture2DMip({hzbWidth, hzbHeight}, Fwog::Format::R32_FLOAT, hzbMips, "HZB");
  }
  
  // Create gbuffer textures and render info
  frame.gAlbedo = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R8G8B8A8_SRGB, "gAlbedo");
  frame.gMetallicRoughnessAo = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R8G8B8_UNORM, "gMetallicRoughnessAo");
  frame.gNormal = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R16G16B16_SNORM, "gNormal");
  frame.gEmission = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R11G11B10_FLOAT, "gEmission");
  frame.gDepth = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::D32_FLOAT, "gDepth");
  frame.gMotion = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R16G16_FLOAT, "gMotion");
  frame.gNormalPrev = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R16G16B16_SNORM);
  frame.gDepthPrev = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::D32_FLOAT);
  frame.colorHdrRenderRes = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R11G11B10_FLOAT, "colorHdrRenderRes");
  frame.colorHdrWindowRes = Fwog::CreateTexture2D({newWidth, newHeight}, Fwog::Format::R11G11B10_FLOAT, "colorHdrWindowRes");
  frame.colorHdrBloomScratchBuffer = Fwog::CreateTexture2DMip({newWidth / 2, newHeight / 2}, Fwog::Format::R11G11B10_FLOAT, 8);
  frame.colorLdrWindowRes = Fwog::CreateTexture2D({newWidth, newHeight}, Fwog::Format::R8G8B8A8_UNORM, "colorLdrWindowRes");

  // create debug views
  frame.gAlbedoSwizzled = frame.gAlbedo->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gRoughnessMetallicAoSwizzled = frame.gMetallicRoughnessAo->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gEmissionSwizzled = frame.gEmission->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gNormalSwizzled = frame.gNormal->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gDepthSwizzled = frame.gDepth->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
}

void FrogRenderer::OnUpdate([[maybe_unused]] double dt)
{
  ZoneScoped;
  if (fakeLag > 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(fakeLag));
  }

  frameIndex++;

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
  
  if (clearDebugAabbsEachFrame)
  {
    debugGpuAabbsBuffer->FillData({.offset = offsetof(Fwog::DrawIndirectCommand, instanceCount), .size = sizeof(uint32_t), .data = 0});
  }

  if (clearDebugRectsEachFrame)
  {
    debugGpuRectsBuffer->FillData({.offset = offsetof(Fwog::DrawIndirectCommand, instanceCount), .size = sizeof(uint32_t), .data = 0});
  }

  shadingUniforms.numberOfLights = (uint32_t)scene.lights.size();

  if (scene.lights.size() * sizeof(Utility::GpuLight) > lightBuffer->Size())
  {
    lightBuffer = Fwog::TypedBuffer<Utility::GpuLight>(scene.lights.size() * 2, Fwog::BufferStorageFlag::DYNAMIC_STORAGE);
  }

  lightBuffer->UpdateData(scene.lights);

  if (fsr2Enable)
  {
    shadingUniforms.random = {PCG::RandFloat(seed), PCG::RandFloat(seed)};
  }
  else
  {
    shadingUniforms.random = {0, 0};
  }

  if (shouldResizeNextFrame)
  {
    OnWindowResize(windowWidth, windowHeight);
    shouldResizeNextFrame = false;
  }

  tonemapUniformBuffer.UpdateData(tonemapUniforms);
  vsmContext.UpdateUniforms(vsmUniforms);
}

static glm::vec2 GetJitterOffset(
  [[maybe_unused]] uint32_t frameIndex,
  [[maybe_unused]] uint32_t renderWidth,
  [[maybe_unused]] uint32_t renderHeight,
  [[maybe_unused]] uint32_t windowWidth)
{
#ifdef FROGRENDER_FSR2_ENABLE
  float jitterX{};
  float jitterY{};
  ffxFsr2GetJitterOffset(&jitterX, &jitterY, frameIndex, ffxFsr2GetJitterPhaseCount(renderWidth, windowWidth));
  return {2.0f * jitterX / static_cast<float>(renderWidth), 2.0f * jitterY / static_cast<float>(renderHeight)};
#else
  return {0, 0};
#endif
}

void FrogRenderer::CullMeshletsForView(const View& view, std::string_view name)
{
  Fwog::SamplerState ss = {};
  ss.minFilter = Fwog::Filter::NEAREST;
  ss.magFilter = Fwog::Filter::NEAREST;
  ss.mipmapFilter = Fwog::Filter::NEAREST;
  auto hzbSampler = Fwog::Sampler(ss);

  viewBuffer->UpdateData(view);

  // Clear all the fields to zero, then set the instance count to one (this way should be more efficient than a CPU-side buffer update)
  const auto drawCommand = Fwog::DrawIndexedIndirectCommand{
    .indexCount = 0,
    .instanceCount = 1,
    .firstIndex = 0,
    .vertexOffset = 0,
    .firstInstance = 0,
  };
  meshletIndirectCommand->UpdateData(drawCommand, 0);

  Fwog::Compute(
    name,
    [&]
    {
      Fwog::Cmd::BindComputePipeline(meshletGeneratePipeline);
      Fwog::Cmd::BindStorageBuffer("MeshletDataBuffer", *meshletBuffer);
      Fwog::Cmd::BindStorageBuffer("MeshletIndexBuffer", *instancedMeshletBuffer);
      Fwog::Cmd::BindStorageBuffer("TransformBuffer", *transformBuffer);
      Fwog::Cmd::BindStorageBuffer("IndirectDrawCommand", *meshletIndirectCommand);
      Fwog::Cmd::BindUniformBuffer("PerFrameUniformsBuffer", globalUniformsBuffer);
      Fwog::Cmd::BindStorageBuffer("ViewBuffer", viewBuffer.value());
      // Fwog::Cmd::BindStorageBuffer("DebugAabbBuffer", debugGpuAabbsBuffer.value());
      // Fwog::Cmd::BindStorageBuffer("DebugRectBuffer", debugGpuRectsBuffer.value());
      Fwog::Cmd::BindUniformBuffer(6, vsmContext.uniformBuffer_);
      Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::BUFFER_UPDATE_BIT);
      Fwog::Cmd::BindSampledImage("s_hzb", *frame.hzb, hzbSampler);
      vsmContext.BindResourcesForCulling();
      Fwog::Cmd::Dispatch(((uint32_t)scene.meshlets.size() + 3) / 4, 1, 1);
      Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::INDEX_BUFFER_BIT | Fwog::MemoryBarrierBit::COMMAND_BUFFER_BIT);
    });
}

void FrogRenderer::OnRender([[maybe_unused]] double dt)
{
  ZoneScoped;
  std::swap(frame.gDepth, frame.gDepthPrev);
  std::swap(frame.gNormal, frame.gNormalPrev);

  shadingUniforms.sunDir = glm::vec4(PolarToCartesian(sunElevation, sunAzimuth), 0);
  shadingUniforms.sunStrength = glm::vec4{sunStrength * sunColor, 0};

  const float fsr2LodBias = fsr2Enable ? log2(float(renderWidth) / float(windowWidth)) - 1.0f : 0.0f;

  Fwog::SamplerState ss;

  ss.minFilter = Fwog::Filter::NEAREST;
  ss.magFilter = Fwog::Filter::NEAREST;
  ss.addressModeU = Fwog::AddressMode::REPEAT;
  ss.addressModeV = Fwog::AddressMode::REPEAT;
  auto nearestSampler = Fwog::Sampler(ss);

  ss = {};
  ss.minFilter = Fwog::Filter::NEAREST;
  ss.magFilter = Fwog::Filter::NEAREST;
  ss.mipmapFilter = Fwog::Filter::NEAREST;
  auto hzbSampler = Fwog::Sampler(ss);
  
  const auto jitterOffset = fsr2Enable ? GetJitterOffset(frameIndex, renderWidth, renderHeight, windowWidth) : glm::vec2{};
  const auto jitterMatrix = glm::translate(glm::mat4(1), glm::vec3(jitterOffset, 0));
  //const auto projUnjittered = glm::perspectiveZO(cameraFovY, aspectRatio, cameraNear, cameraFar);
  const auto projUnjittered = InfReverseZPerspectiveRH(cameraFovyRadians, aspectRatio, cameraNearPlane);
  const auto projJittered = jitterMatrix * projUnjittered;
  
  // Set global uniforms
  const uint32_t meshletCount = (uint32_t)scene.meshlets.size();
  const auto viewProj = projJittered * mainCamera.GetViewMatrix();
  const auto viewProjUnjittered = projUnjittered * mainCamera.GetViewMatrix();
  
  auto mainView = View{
    .proj = projUnjittered,
    .view = mainCamera.GetViewMatrix(),
    .viewProj = viewProjUnjittered,
    .cameraPos = glm::vec4(mainCamera.position, 0.0),
    .viewport = {0.0f, 0.0f, static_cast<float>(renderWidth), static_cast<float>(renderHeight)},
  };

  if (updateCullingFrustum)
  {
    // TODO: the main view has an infinite projection, so we should omit the far plane. It seems to be causing the test to always pass.
    // We should probably emit the far plane regardless, but alas, one thing at a time.
    MakeFrustumPlanes(viewProjUnjittered, mainView.frustumPlanes);
    debugMainViewProj = viewProjUnjittered;
  }

  // TODO: this may wreak havoc on future (non-culling) systems that depend on this matrix, but we'll leave it for now
  if (executeMeshletGeneration)
  {
    mainCameraUniforms.oldViewProjUnjittered = frameIndex == 1 ? viewProjUnjittered : mainCameraUniforms.viewProjUnjittered;
  }
  mainCameraUniforms.viewProjUnjittered = viewProjUnjittered;
  mainCameraUniforms.viewProj = viewProj;
  mainCameraUniforms.invViewProj = glm::inverse(mainCameraUniforms.viewProj);
  mainCameraUniforms.proj = projJittered;
  mainCameraUniforms.cameraPos = glm::vec4(mainCamera.position, 0.0);
  mainCameraUniforms.meshletCount = meshletCount;
  mainCameraUniforms.maxIndices = static_cast<uint32_t>(scene.primitives.size() * 3);
  mainCameraUniforms.bindlessSamplerLodBias = fsr2LodBias;

  globalUniformsBuffer.UpdateData(mainCameraUniforms);

  shadowUniformsBuffer.UpdateData(shadowUniforms);
  
  shadingUniformsBuffer.UpdateData(shadingUniforms);

  if (frameIndex == 1)
  {
    vsmSun.UpdateExpensive(mainCamera.position, glm::vec3{-shadingUniforms.sunDir}, vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
  }
  else
  {
    vsmSun.UpdateOffset(mainCamera.position);
  }

  if (executeMeshletGeneration)
  {
    CullMeshletsForView(mainView, "Cull Meshlets Main");
  }

  auto visbufferAttachment = Fwog::RenderColorAttachment{
    .texture = frame.visbuffer.value(),
    .loadOp = Fwog::AttachmentLoadOp::CLEAR,
    .clearValue = {~0u, ~0u, ~0u, ~0u},
  };
  auto visbufferDepthAttachment = Fwog::RenderDepthStencilAttachment{
    .texture = frame.gDepth.value(),
    .loadOp = Fwog::AttachmentLoadOp::CLEAR,
    .clearValue = {.depth = FAR_DEPTH},
  };
  Fwog::Render(
    {
      .name = "Main Visbuffer Pass",
      .viewport = {Fwog::Viewport{.drawRect = {{0, 0}, {renderWidth, renderHeight}}}},
      .colorAttachments = {&visbufferAttachment, 1},
      .depthAttachment = visbufferDepthAttachment,
    },
    [&]
    {
      Fwog::Cmd::BindGraphicsPipeline(visbufferPipeline);
      Fwog::Cmd::BindStorageBuffer("MeshletDataBuffer", *meshletBuffer);
      Fwog::Cmd::BindStorageBuffer("MeshletPrimitiveBuffer", *primitiveBuffer);
      Fwog::Cmd::BindStorageBuffer("MeshletVertexBuffer", *vertexBuffer);
      Fwog::Cmd::BindStorageBuffer("MeshletIndexBuffer", *indexBuffer);
      Fwog::Cmd::BindStorageBuffer("TransformBuffer", *transformBuffer);
      Fwog::Cmd::BindUniformBuffer("PerFrameUniformsBuffer", globalUniformsBuffer);
      Fwog::Cmd::BindStorageBuffer("MaterialBuffer", *materialStorageBuffer);
      Fwog::Cmd::BindIndexBuffer(*instancedMeshletBuffer, Fwog::IndexType::UNSIGNED_INT);
      Fwog::Cmd::DrawIndexedIndirect(*meshletIndirectCommand, 0, 1, 0);
    });

  // VSMs
  {
    const auto debugMarker = Fwog::ScopedDebugMarker("Virtual Shadow Maps");
    vsmContext.ResetPageVisibility();
    vsmSun.MarkVisiblePages(frame.gDepth.value(), globalUniformsBuffer);
    vsmContext.FreeNonVisiblePages();
    vsmContext.AllocateRequestedPages();
    vsmSun.GenerateBitmaskHzb();
    vsmContext.ClearDirtyPages();

    // Sun VSMs
    for (uint32_t i = 0; i < vsmSun.NumClipmaps(); i++)
    {
      auto sunCurrentClipmapView = View{
        .oldViewProj =
          vsmSun.GetProjections()[i] * vsmSun.GetViewMatrices()[i], // TODO: this is slightly wrong, but hopefully an old viewProj won't be needed in the future
        .proj = vsmSun.GetProjections()[i],
        .view = vsmSun.GetViewMatrices()[i],
        .viewProj = vsmSun.GetProjections()[i] * vsmSun.GetViewMatrices()[i],
        .oldViewProjStableForVsmOnly = vsmSun.GetProjections()[i] * vsmSun.GetStableViewMatrix(),
        .cameraPos = {}, // unused
        .viewport = {0.f, 0.f, vsmSun.GetExtent().width, vsmSun.GetExtent().height},
        .isVirtual = true,
        .virtualTableIndex = vsmSun.GetClipmapTableIndices()[i],
      };
      MakeFrustumPlanes(sunCurrentClipmapView.viewProj, sunCurrentClipmapView.frustumPlanes);

      CullMeshletsForView(sunCurrentClipmapView, "Cull Sun VSM Meshlets, View " + std::to_string(i));

      const auto vsmExtent = Fwog::Extent2D{Techniques::VirtualShadowMaps::maxExtent, Techniques::VirtualShadowMaps::maxExtent};
      Fwog::RenderNoAttachments(
        {
          .name = "VSM Render Sun Clipmaps",
          .viewport = {{{0, 0}, vsmExtent}},
          .framebufferSize = {vsmExtent.width, vsmExtent.height, 1},
          .framebufferSamples = Fwog::SampleCount::SAMPLES_1,
        },
        [&]
        {
          Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT | Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT);
          Fwog::Cmd::BindGraphicsPipeline(vsmShadowPipeline);
          Fwog::Cmd::BindStorageBuffer("MeshletDataBuffer", *meshletBuffer);
          Fwog::Cmd::BindStorageBuffer("MeshletPrimitiveBuffer", *primitiveBuffer);
          Fwog::Cmd::BindStorageBuffer("MeshletVertexBuffer", *vertexBuffer);
          Fwog::Cmd::BindStorageBuffer("MeshletIndexBuffer", *indexBuffer);
          Fwog::Cmd::BindStorageBuffer("TransformBuffer", *transformBuffer);
          Fwog::Cmd::BindUniformBuffer("PerFrameUniformsBuffer", globalUniformsBuffer);
          Fwog::Cmd::BindStorageBuffer("ViewBuffer", viewBuffer.value());
          vsmSun.BindResourcesForDrawing();
          Fwog::Cmd::BindImage(1, vsmContext.physicalPagesUint_, 0);
          Fwog::Cmd::BindUniformBuffer("VsmShadowUniforms", vsmShadowUniformBuffer);
          Fwog::Cmd::BindIndexBuffer(*instancedMeshletBuffer, Fwog::IndexType::UNSIGNED_INT);

          vsmShadowUniformBuffer.UpdateData(vsmSun.GetClipmapTableIndices()[i]);
          Fwog::Cmd::DrawIndexedIndirect(*meshletIndirectCommand, 0, 1, 0);
        });
    }
  }

  //vsmSun.GenerateHZB();

  if (generateHizBuffer)
  {
    Fwog::Compute(
      "HZB Build Pass",
      [&]
      {
        Fwog::Cmd::BindImage(0, *frame.hzb, 0);
        Fwog::Cmd::BindSampledImage(1, *frame.gDepth, hzbSampler);
        Fwog::Cmd::BindComputePipeline(hzbCopyPipeline);
        uint32_t hzbCurrentWidth = frame.hzb->GetCreateInfo().extent.width;
        uint32_t hzbCurrentHeight = frame.hzb->GetCreateInfo().extent.height;
        const uint32_t hzbLevels = frame.hzb->GetCreateInfo().mipLevels;
        Fwog::Cmd::Dispatch((hzbCurrentWidth + 15) / 16, (hzbCurrentHeight + 15) / 16, 1);
        Fwog::Cmd::BindComputePipeline(hzbReducePipeline);
        for (uint32_t level = 1; level < hzbLevels; ++level)
        {
          Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT);
          Fwog::Cmd::BindImage(0, *frame.hzb, level - 1);
          Fwog::Cmd::BindImage(1, *frame.hzb, level);
          hzbCurrentWidth = std::max(1u, hzbCurrentWidth >> 1);
          hzbCurrentHeight = std::max(1u, hzbCurrentHeight >> 1);
          Fwog::Cmd::Dispatch((hzbCurrentWidth + 15) / 16, (hzbCurrentHeight + 15) / 16, 1);
        }
        Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT);
      });
  }
  else
  {
    const uint32_t hzbLevels = frame.hzb->GetCreateInfo().mipLevels;
    for (uint32_t level = 0; level < hzbLevels; level++)
    {
      constexpr float farDepth = FAR_DEPTH;
      frame.hzb->ClearImage({.level = level, .data = &farDepth});
    }
  }

  auto materialDepthAttachment = Fwog::RenderDepthStencilAttachment{
    .texture = frame.materialDepth.value(),
    .loadOp = Fwog::AttachmentLoadOp::CLEAR,
    .clearValue = {.depth = 1.0f},
  };

  Fwog::Render(
    {
      .name = "Material Visbuffer Pass",
      .viewport =
        {
          Fwog::Viewport{
            .drawRect = {{0, 0}, {renderWidth, renderHeight}},
          },
        },
      .depthAttachment = materialDepthAttachment,
    },
    [&]
    {
      Fwog::Cmd::BindGraphicsPipeline(materialDepthPipeline);
      Fwog::Cmd::BindStorageBuffer("MeshletDataBuffer", *meshletBuffer);
      Fwog::Cmd::BindImage("visbuffer", frame.visbuffer.value(), 0);
      Fwog::Cmd::Draw(3, 1, 0, 0);
    });

  Fwog::RenderColorAttachment gBufferAttachments[] = {
    {
      .texture = frame.gAlbedo.value(),
      .loadOp = Fwog::AttachmentLoadOp::DONT_CARE
    },
    {
      .texture = frame.gMetallicRoughnessAo.value(),
      .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
    },
    {
      .texture = frame.gNormal.value(),
      .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
    },
    {
      .texture = frame.gEmission.value(),
      .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
    },
    {
      .texture = frame.gMotion.value(),
      .loadOp = Fwog::AttachmentLoadOp::CLEAR,
      .clearValue = {0.f, 0.f, 0.f, 0.f},
    },
  };

  auto visbufferResolveDepthAttachment = Fwog::RenderDepthStencilAttachment{
    .texture = frame.materialDepth.value(),
    .loadOp = Fwog::AttachmentLoadOp::LOAD,
  };

  Fwog::Render(
    {
      .name = "Resolve Visbuffer Pass",
      .viewport =
        {
          Fwog::Viewport{
            .drawRect = {{0, 0}, {renderWidth, renderHeight}},
          },
        },
      .colorAttachments = gBufferAttachments,
      .depthAttachment = visbufferResolveDepthAttachment,
    },
    [&]
    {
      Fwog::Cmd::BindGraphicsPipeline(visbufferResolvePipeline);
      Fwog::Cmd::BindImage("visbuffer", frame.visbuffer.value(), 0);
      Fwog::Cmd::BindStorageBuffer("MeshletDataBuffer", *meshletBuffer);
      Fwog::Cmd::BindStorageBuffer("MeshletPrimitiveBuffer", *primitiveBuffer);
      Fwog::Cmd::BindStorageBuffer("MeshletVertexBuffer", *vertexBuffer);
      Fwog::Cmd::BindStorageBuffer("MeshletIndexBuffer", *indexBuffer);
      Fwog::Cmd::BindStorageBuffer("TransformBuffer", *transformBuffer);
      Fwog::Cmd::BindUniformBuffer("PerFrameUniformsBuffer", globalUniformsBuffer);
      Fwog::Cmd::BindStorageBuffer("MaterialBuffer", *materialStorageBuffer);

      // Render a full-screen tri for each material, but only fragments with matching material (stored in depth) are shaded
      for (uint32_t materialId = 0; materialId < scene.materials.size(); ++materialId)
      {
        auto& material = scene.materials[materialId];

        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_BASE_COLOR_TEXTURE)
        {
          auto& [texture, sampler] = material.albedoTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage("s_baseColor", texture, Fwog::Sampler(sampler));
        }

        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_METALLIC_ROUGHNESS_TEXTURE)
        {
          auto& [texture, sampler] = material.metallicRoughnessTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage("s_metallicRoughness", texture, Fwog::Sampler(sampler));
        }

        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_NORMAL_TEXTURE)
        {
          auto& [texture, sampler] = material.normalTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage("s_normal", texture, Fwog::Sampler(sampler));
        }

        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_OCCLUSION_TEXTURE)
        {
          auto& [texture, sampler] = material.occlusionTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage("s_occlusion", texture, Fwog::Sampler(sampler));
        }

        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_EMISSION_TEXTURE)
        {
          auto& [texture, sampler] = material.emissiveTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage("s_emission", texture, Fwog::Sampler(sampler));
        }

        // TODO: this is for debugging only
        Fwog::Cmd::BindSampledImage(5, frame.hzb.value(), hzbSampler);

        Fwog::Cmd::Draw(3, 1, 0, materialId);
      }
    });

  // shading pass (full screen tri)
  auto shadingColorAttachment = Fwog::RenderColorAttachment{
    .texture = frame.colorHdrRenderRes.value(),
    .loadOp = Fwog::AttachmentLoadOp::CLEAR,
    .clearValue = {.1f, .3f, .5f, 0.0f},
  };
  Fwog::Render(
    {
      .name = "Shading",
      .colorAttachments = {&shadingColorAttachment, 1},
    },
    [&]
    {
      Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT | Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT);
      Fwog::Cmd::BindGraphicsPipeline(shadingPipeline);
      Fwog::Cmd::BindSampledImage("s_gAlbedo", *frame.gAlbedo, nearestSampler);
      Fwog::Cmd::BindSampledImage("s_gNormal", *frame.gNormal, nearestSampler);
      Fwog::Cmd::BindSampledImage("s_gDepth", *frame.gDepth, nearestSampler);
      // Fwog::Cmd::BindSampledImage("s_rsmIndirect", frame.rsm->GetIndirectLighting(), nearestSampler);
      // Fwog::Cmd::BindSampledImage("s_rsmDepth", rsmDepth, nearestSampler);
      // Fwog::Cmd::BindSampledImage("s_rsmDepthShadow", rsmDepth, shadowSampler);
      Fwog::Cmd::BindSampledImage("s_emission", *frame.gEmission, nearestSampler);
      Fwog::Cmd::BindSampledImage("s_metallicRoughnessAo", *frame.gMetallicRoughnessAo, nearestSampler);
      Fwog::Cmd::BindUniformBuffer("PerFrameUniformsBuffer", globalUniformsBuffer);
      Fwog::Cmd::BindUniformBuffer("ShadingUniforms", shadingUniformsBuffer);
      Fwog::Cmd::BindUniformBuffer("ShadowUniforms", shadowUniformsBuffer);
      Fwog::Cmd::BindStorageBuffer("LightBuffer", *lightBuffer);
      vsmSun.BindResourcesForDrawing();
      Fwog::Cmd::Draw(3, 1, 0, 0);
    });

  // After shading, we render debug geometry
  auto debugDepthAttachment = Fwog::RenderDepthStencilAttachment{
    .texture = frame.gDepth.value(),
    .loadOp = Fwog::AttachmentLoadOp::LOAD,
  };

  auto colorAttachments = std::vector<Fwog::RenderColorAttachment>{};
  colorAttachments.emplace_back(frame.colorHdrRenderRes.value(), Fwog::AttachmentLoadOp::LOAD);
  if (fsr2Enable)
  {
    colorAttachments.emplace_back(frame.gReactiveMask.value(), Fwog::AttachmentLoadOp::CLEAR, Fwog::ClearColorValue{0.0f});
  }

  Fwog::Render(
    {
      .name = "Debug Geometry",
      .colorAttachments = colorAttachments,
      .depthAttachment = debugDepthAttachment,
    },
    [&]
    {
      Fwog::Cmd::BindUniformBuffer(0, globalUniformsBuffer);
      // Lines
      if (!debugLines.empty())
      {
        auto lineVertexBuffer = Fwog::TypedBuffer<Debug::Line>(debugLines);
        Fwog::Cmd::BindGraphicsPipeline(debugLinesPipeline);
        Fwog::Cmd::BindVertexBuffer(0, lineVertexBuffer, 0, sizeof(glm::vec3) + sizeof(glm::vec4));
        Fwog::Cmd::Draw(uint32_t(debugLines.size() * 2), 1, 0, 0);
      }

      // GPU-generated geometry past here
      Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::COMMAND_BUFFER_BIT | Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT);

      // AABBs
      if (drawDebugAabbs)
      {
        Fwog::Cmd::BindGraphicsPipeline(debugAabbsPipeline);
        Fwog::Cmd::BindStorageBuffer("DebugAabbBuffer", debugGpuAabbsBuffer.value());
        Fwog::Cmd::DrawIndirect(debugGpuAabbsBuffer.value(), 0, 1, 0);
      }

      // Rects
      if (drawDebugRects)
      {
        Fwog::Cmd::BindGraphicsPipeline(debugRectsPipeline);
        Fwog::Cmd::BindStorageBuffer("DebugRectBuffer", debugGpuRectsBuffer.value());
        Fwog::Cmd::DrawIndirect(debugGpuRectsBuffer.value(), 0, 1, 0);
      }
    });

  autoExposure.Apply({
    .image = frame.colorHdrRenderRes.value(),
    .exposureBuffer = exposureBuffer,
    .deltaTime = static_cast<float>(dt),
    .adjustmentSpeed = autoExposureAdjustmentSpeed,
    .targetLuminance = autoExposureTargetLuminance,
    .logMinLuminance = autoExposureLogMinLuminance,
    .logMaxLuminance = autoExposureLogMaxLuminance,
  });

#ifdef FROGRENDER_FSR2_ENABLE
  if (fsr2Enable)
  {
    Fwog::Compute(
      "FSR 2",
      [&]
      {
        static Fwog::TimerQueryAsync timer(5);
        if (auto t = timer.PopTimestamp())
        {
          fsr2Performance = *t / 10e5;
        }
        Fwog::TimerScoped scopedTimer(timer);

        if (frameIndex == 1)
        {
          dt = 17.0 / 1000.0;
        }

        float jitterX{};
        float jitterY{};
        ffxFsr2GetJitterOffset(&jitterX, &jitterY, frameIndex, ffxFsr2GetJitterPhaseCount(renderWidth, windowWidth));

        FfxFsr2DispatchDescription dispatchDesc{
          .color = ffxGetTextureResourceGL(frame.colorHdrRenderRes->Handle(), renderWidth, renderHeight, Fwog::detail::FormatToGL(frame.colorHdrRenderRes->GetCreateInfo().format)),
          .depth = ffxGetTextureResourceGL(frame.gDepth->Handle(), renderWidth, renderHeight, Fwog::detail::FormatToGL(frame.gDepth->GetCreateInfo().format)),
          .motionVectors = ffxGetTextureResourceGL(frame.gMotion->Handle(), renderWidth, renderHeight, Fwog::detail::FormatToGL(frame.gMotion->GetCreateInfo().format)),
          .exposure = {},
          .reactive = ffxGetTextureResourceGL(frame.gReactiveMask->Handle(), renderWidth, renderHeight, Fwog::detail::FormatToGL(frame.gReactiveMask->GetCreateInfo().format)),
          .transparencyAndComposition = {},
          .output = ffxGetTextureResourceGL(frame.colorHdrWindowRes->Handle(), windowWidth, windowHeight, Fwog::detail::FormatToGL(frame.colorHdrWindowRes->GetCreateInfo().format)),
          .jitterOffset = {jitterX, jitterY},
          .motionVectorScale = {float(renderWidth), float(renderHeight)},
          .renderSize = {renderWidth, renderHeight},
          .enableSharpening = fsr2Sharpness != 0,
          .sharpness = fsr2Sharpness,
          .frameTimeDelta = static_cast<float>(dt * 1000.0),
          .preExposure = 1,
          .reset = false,
          .cameraNear = std::numeric_limits<float>::max(),
          .cameraFar = cameraNearPlane,
          .cameraFovAngleVertical = cameraFovyRadians,
          .viewSpaceToMetersFactor = 1,
          .deviceDepthNegativeOneToOne = false,
        };

        if (auto err = ffxFsr2ContextDispatch(&fsr2Context, &dispatchDesc); err != FFX_OK)
        {
          printf("FSR 2 error: %d\n", err);
        }
      });
    Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT);
  }
#endif

  if (bloomEnable)
  {
    bloom.Apply({
      .target = fsr2Enable ? frame.colorHdrWindowRes.value() : frame.colorHdrRenderRes.value(),
      .scratchTexture = frame.colorHdrBloomScratchBuffer.value(),
      .passes = bloomPasses,
      .strength = bloomStrength,
      .width = bloomWidth,
      .useLowPassFilterOnFirstPass = bloomUseLowPassFilter,
    });
  }

  Fwog::Compute("Postprocessing",
    [&]
    {
      Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::UNIFORM_BUFFER_BIT);
      Fwog::Cmd::BindComputePipeline(tonemapPipeline);
      Fwog::Cmd::BindSampledImage(0, fsr2Enable ? frame.colorHdrWindowRes.value() : frame.colorHdrRenderRes.value(), nearestSampler);
      Fwog::Cmd::BindSampledImage(1, noiseTexture.value(), nearestSampler);
      Fwog::Cmd::BindUniformBuffer(0, exposureBuffer);
      Fwog::Cmd::BindUniformBuffer(1, tonemapUniformBuffer);
      Fwog::Cmd::BindImage(0, frame.colorLdrWindowRes.value(), 0);
      Fwog::Cmd::DispatchInvocations(frame.colorLdrWindowRes.value().Extent());
      Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT); // So future samples can see changes
    });

  if (debugRenderToSwapchain)
  {
    Fwog::RenderToSwapchain(
      {
        .name = "Copy to swapchain",
        .viewport =
          Fwog::Viewport{
            .drawRect{.offset = {0, 0}, .extent = {windowWidth, windowHeight}},
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
          },
        .colorLoadOp = Fwog::AttachmentLoadOp::DONT_CARE,
        .depthLoadOp = Fwog::AttachmentLoadOp::DONT_CARE,
        .stencilLoadOp = Fwog::AttachmentLoadOp::DONT_CARE,
      },
      [&]
      {
        const Fwog::Texture* tex = &frame.colorLdrWindowRes.value();
        if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS)
          tex = &frame.gAlbedo.value();
        if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS)
          tex = &frame.gNormal.value();
        if (glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS)
          tex = &frame.gDepth.value();
        if (tex)
        {
          Fwog::Cmd::BindGraphicsPipeline(debugTexturePipeline);
          Fwog::Cmd::BindSampledImage(0, *tex, nearestSampler);
          Fwog::Cmd::Draw(3, 1, 0, 0);
        }
      });
  }
}