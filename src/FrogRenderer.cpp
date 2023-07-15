#include "FrogRenderer.h"
#include "Fwog/Context.h"

#include <Fwog/BasicTypes.h>
#include <Fwog/Buffer.h>
#include <Fwog/Pipeline.h>
#include <Fwog/Rendering.h>
#include <Fwog/Shader.h>
#include <Fwog/Texture.h>
#include <Fwog/Timer.h>

#include <GLFW/glfw3.h>

#include <stb_image.h>
#include <stb_include.h>

#include <glm/gtx/transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>

static Fwog::Shader LoadShaderWithIncludes(Fwog::PipelineStage stage, const std::filesystem::path& path)
{
  if (!std::filesystem::exists(path) || std::filesystem::is_directory(path))
  {
    throw std::runtime_error("Path does not refer to a file");
  }
  auto pathStr = path.string();
  auto parentPathStr = path.parent_path().string();
  auto processedSource = std::unique_ptr<char, decltype([](char* p) { free(p); })>(stb_include_file(pathStr.c_str(), nullptr, parentPathStr.c_str(), nullptr));
  return Fwog::Shader(stage, processedSource.get());
}

static constexpr std::array<Fwog::VertexInputBindingDescription, 3> sceneInputBindingDescs{
  Fwog::VertexInputBindingDescription{
    .location = 0,
    .binding = 0,
    .format = Fwog::Format::R32G32B32_FLOAT,
    .offset = offsetof(Utility::Vertex, position),
  },
  Fwog::VertexInputBindingDescription{
    .location = 1,
    .binding = 0,
    .format = Fwog::Format::R16G16_SNORM,
    .offset = offsetof(Utility::Vertex, normal),
  },
  Fwog::VertexInputBindingDescription{
    .location = 2,
    .binding = 0,
    .format = Fwog::Format::R32G32_FLOAT,
    .offset = offsetof(Utility::Vertex, texcoord),
  },
};

static Fwog::GraphicsPipeline CreateScenePipeline()
{
  auto vs = Fwog::Shader(Fwog::PipelineStage::VERTEX_SHADER, Application::LoadFile("shaders/SceneDeferredPbr.vert.glsl"));
  auto fs = Fwog::Shader(Fwog::PipelineStage::FRAGMENT_SHADER, Application::LoadFile("shaders/SceneDeferredPbr.frag.glsl"));

  return Fwog::GraphicsPipeline({
    .vertexShader = &vs,
    .fragmentShader = &fs,
    .vertexInputState = {sceneInputBindingDescs},
    .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
    .depthState = {.depthTestEnable = true, .depthWriteEnable = true},
  });
}

static Fwog::ComputePipeline CreateMeshletGeneratePipeline()
{
  auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/visbuffer/Visbuffer.comp.glsl");

  return Fwog::ComputePipeline({
    .shader = &comp,
  });
}

static Fwog::GraphicsPipeline CreateVisbufferPipeline()
{
  auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/visbuffer/Visbuffer.vert.glsl");
  auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/Visbuffer.frag.glsl");

  return Fwog::GraphicsPipeline({
    .vertexShader = &vs,
    .fragmentShader = &fs,
    .rasterizationState = {.cullMode = Fwog::CullMode::BACK},
    .depthState = {.depthTestEnable = true, .depthWriteEnable = true},
  });
}

static Fwog::GraphicsPipeline CreateMaterialDepthPipeline()
{
  auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
  auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/VisbufferMaterialDepth.frag.glsl");

  return Fwog::GraphicsPipeline({
    .vertexShader = &vs,
    .fragmentShader = &fs,
    .vertexInputState = {},
    .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
    .depthState = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = Fwog::CompareOp::ALWAYS}
  });
}

static Fwog::GraphicsPipeline CreateVisbufferResolvePipeline()
{
  auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/visbuffer/VisbufferResolve.vert.glsl");
  auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/VisbufferResolve.frag.glsl");

  return Fwog::GraphicsPipeline({
    .vertexShader = &vs,
    .fragmentShader = &fs,
    .vertexInputState = {},
    .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
    .depthState = {.depthTestEnable = true, .depthWriteEnable = false, .depthCompareOp = Fwog::CompareOp::EQUAL}
  });
}

static Fwog::GraphicsPipeline CreateShadowPipeline()
{
  auto vs = Fwog::Shader(Fwog::PipelineStage::VERTEX_SHADER, Application::LoadFile("shaders/SceneDeferredPbr.vert.glsl"));
  auto fs = Fwog::Shader(Fwog::PipelineStage::FRAGMENT_SHADER, Application::LoadFile("shaders/RSMScenePbr.frag.glsl"));

  return Fwog::GraphicsPipeline({
    .vertexShader = &vs,
    .fragmentShader = &fs,
    .vertexInputState = {sceneInputBindingDescs},
    .depthState = {.depthTestEnable = true, .depthWriteEnable = true},
  });
}

static Fwog::GraphicsPipeline CreateShadingPipeline()
{
  auto vs = Fwog::Shader(Fwog::PipelineStage::VERTEX_SHADER, Application::LoadFile("shaders/FullScreenTri.vert.glsl"));
  auto fs = Fwog::Shader(Fwog::PipelineStage::FRAGMENT_SHADER, Application::LoadFile("shaders/ShadeDeferredPbr.frag.glsl"));

  return Fwog::GraphicsPipeline({
    .vertexShader = &vs,
    .fragmentShader = &fs,
    .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
  });
}

static Fwog::GraphicsPipeline CreatePostprocessingPipeline()
{
  auto vs = Fwog::Shader(Fwog::PipelineStage::VERTEX_SHADER, Application::LoadFile("shaders/FullScreenTri.vert.glsl"));
  auto fs = Fwog::Shader(Fwog::PipelineStage::FRAGMENT_SHADER, Application::LoadFile("shaders/TonemapAndDither.frag.glsl"));
  return Fwog::GraphicsPipeline({
    .vertexShader = &vs,
    .fragmentShader = &fs,
    .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
  });
}

static Fwog::GraphicsPipeline CreateDebugTexturePipeline()
{
  auto vs = Fwog::Shader(Fwog::PipelineStage::VERTEX_SHADER, Application::LoadFile("shaders/FullScreenTri.vert.glsl"));
  auto fs = Fwog::Shader(Fwog::PipelineStage::FRAGMENT_SHADER, Application::LoadFile("shaders/Texture.frag.glsl"));

  return Fwog::GraphicsPipeline({
    .vertexShader = &vs,
    .fragmentShader = &fs,
    .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
  });
}

FrogRenderer::FrogRenderer(const Application::CreateInfo& createInfo, std::optional<std::string_view> filename, float scale, bool binary)
  : Application(createInfo),
    // Create RSM textures
    rsmFlux(Fwog::CreateTexture2D({gShadowmapWidth, gShadowmapHeight}, Fwog::Format::R11G11B10_FLOAT)),
    rsmNormal(Fwog::CreateTexture2D({gShadowmapWidth, gShadowmapHeight}, Fwog::Format::R16G16B16_SNORM)),
    rsmDepth(Fwog::CreateTexture2D({gShadowmapWidth, gShadowmapHeight}, Fwog::Format::D16_UNORM)),
    rsmFluxSwizzled(rsmFlux.CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE})),
    rsmNormalSwizzled(rsmNormal.CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE})),
    rsmDepthSwizzled(rsmDepth.CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE})),
    // Create constant-size buffers
    globalUniformsBuffer(Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    shadingUniformsBuffer(Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    shadowUniformsBuffer(shadowUniforms, Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    rsmUniforms(Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
    // Create the pipelines used in the application
    meshletGeneratePipeline(CreateMeshletGeneratePipeline()),
    visbufferPipeline(CreateVisbufferPipeline()),
    materialDepthPipeline(CreateMaterialDepthPipeline()),
    visbufferResolvePipeline(CreateVisbufferResolvePipeline()),
    scenePipeline(CreateScenePipeline()),
    rsmScenePipeline(CreateShadowPipeline()),
    shadingPipeline(CreateShadingPipeline()),
    postprocessingPipeline(CreatePostprocessingPipeline()),
    debugTexturePipeline(CreateDebugTexturePipeline())
{
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

  cameraSpeed = 2.5f;
  mainCamera.position.y = 1;

  if (!filename)
  {
    // Utility::LoadModelFromFile(scene, "models/simple_scene.glb", glm::mat4{.125}, true);
    Utility::LoadModelFromFileMeshlet(scene, "models/simple_scene.glb", glm::mat4{.125}, true);
    //Utility::LoadModelFromFileMeshlet(scene, "/run/media/master/Samsung S0/Dev/CLion/IrisVk/models/sponza/Sponza.gltf", glm::mat4{.125}, false);

    // Utility::LoadModelFromFile(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/modular_ruins_c_2.glb", glm::mat4{.125}, true);

    // Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf", glm::mat4{.5}, false);

    // Utility::LoadModelFromFile(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_compressed.glb",
    // glm::mat4{.25}, true); Utility::LoadModelFromFile(scene, "H:/Repositories/glTF-Sample-Models/downloaded
    // schtuff/sponza_curtains_compressed.glb", glm::mat4{.25}, true); Utility::LoadModelFromFile(scene,
    // "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_ivy_compressed.glb", glm::mat4{.25}, true); Utility::LoadModelFromFile(scene,
    // "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_tree_compressed.glb", glm::mat4{.25}, true);

    // Utility::LoadModelFromFile(scene, "H:/Repositories/deccer-cubes/SM_Deccer_Cubes_Textured.glb", glm::mat4{0.5f}, true);
  }
  else
  {
    Utility::LoadModelFromFileMeshlet(scene, *filename, glm::scale(glm::vec3{scale}), binary);
  }

  meshletBuffer = Fwog::TypedBuffer<Utility::Meshlet>(scene.meshlets);
  vertexBuffer = Fwog::TypedBuffer<Utility::Vertex>(scene.vertices);
  indexBuffer = Fwog::TypedBuffer<uint32_t>(scene.indices);
  primitiveBuffer = Fwog::TypedBuffer<uint8_t>(scene.primitives);
  transformBuffer = Fwog::TypedBuffer<glm::mat4>(scene.transforms);
  mesheletIndirectCommand = Fwog::TypedBuffer<Fwog::DrawIndexedIndirectCommand>(Fwog::BufferStorageFlag::DYNAMIC_STORAGE);
  instancedMeshletBuffer = Fwog::TypedBuffer<uint32_t>(scene.primitives.size() * 3);

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

  //////////////////////////////////////// Clustered rendering stuff
  std::vector<Light> lights;
  // lights.push_back(Light{ .position = { 3, 2, 0, 0 }, .intensity = { .2f, .8f, 1.0f }, .invRadius = 1.0f / 4.0f });
  // lights.push_back(Light{ .position = { 3, -2, 0, 0 }, .intensity = { .7f, .8f, 0.1f }, .invRadius = 1.0f / 2.0f });
  // lights.push_back(Light{ .position = { 3, 2, 0, 0 }, .intensity = { 1.2f, .8f, .1f }, .invRadius = 1.0f / 6.0f });

  meshUniformBuffer.emplace(meshUniforms, Fwog::BufferStorageFlag::DYNAMIC_STORAGE);

  lightBuffer.emplace(lights, Fwog::BufferStorageFlag::DYNAMIC_STORAGE);

  // clusterTexture({.imageType = Fwog::ImageType::TEX_3D,
  //                                      .format = Fwog::Format::R16G16_UINT,
  //                                      .extent = {16, 9, 24},
  //                                      .mipLevels = 1,
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
      .flags = FFX_FSR2_ENABLE_DEBUG_CHECKING | FFX_FSR2_ENABLE_AUTO_EXPOSURE | FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR2_ALLOW_NULL_DEVICE_AND_COMMAND_LIST,
      .maxRenderSize = {renderWidth, renderHeight},
      .displaySize = {newWidth, newHeight},
      .fpMessage =
        [](FfxFsr2MsgType type, const wchar_t* message)
      {
        char cstr[256] = {};
        wcstombs_s(nullptr, cstr, sizeof(cstr), message, sizeof(cstr));
        cstr[255] = '\0';
        printf("FSR 2 message (type=%d): %s\n", type, cstr);
      },
    };
    fsr2ScratchMemory = std::make_unique<char[]>(ffxFsr2GetScratchMemorySizeGL());
    ffxFsr2GetInterfaceGL(&contextDesc.callbacks, fsr2ScratchMemory.get(), ffxFsr2GetScratchMemorySizeGL(), glfwGetProcAddress);
    ffxFsr2ContextCreate(&fsr2Context, &contextDesc);
  }
  else
#endif
  {
    renderWidth = newWidth;
    renderHeight = newHeight;
  }

  frame.visbuffer = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R32_UINT, "visbuffer");
  frame.visDepth = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::D32_FLOAT, "visDepth");
  frame.materialDepth = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::D32_FLOAT, "materialDepth");
  frame.visResolve = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R8G8B8A8_SRGB, "visResolve");

  // create gbuffer textures and render info
  frame.gAlbedo = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R8G8B8A8_SRGB, "gAlbedo");
  frame.gNormal = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R16G16B16_SNORM, "gNormal");
  frame.gDepth = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::D32_FLOAT, "gDepth");
  frame.gNormalPrev = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R16G16B16_SNORM);
  frame.gDepthPrev = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::D32_FLOAT);
  frame.gMotion = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R16G16_FLOAT, "gMotion");
  frame.colorHdrRenderRes = Fwog::CreateTexture2D({renderWidth, renderHeight}, Fwog::Format::R11G11B10_FLOAT, "colorHdrRenderRes");
  frame.colorHdrWindowRes = Fwog::CreateTexture2D({newWidth, newHeight}, Fwog::Format::R11G11B10_FLOAT, "colorHdrWindowRes");
  frame.colorLdrWindowRes = Fwog::CreateTexture2D({newWidth, newHeight}, Fwog::Format::R8G8B8A8_UNORM, "colorLdrWindowRes");

  if (!frame.rsm)
  {
    frame.rsm = RSM::RsmTechnique(renderWidth, renderHeight);
  }
  else
  {
    frame.rsm->SetResolution(renderWidth, renderHeight);
  }

  // create debug views
  frame.gAlbedoSwizzled = frame.gAlbedo->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gNormalSwizzled = frame.gNormal->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gDepthSwizzled = frame.gDepth->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gRsmIlluminanceSwizzled = frame.rsm->GetIndirectLighting().CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
}

void FrogRenderer::OnUpdate([[maybe_unused]] double dt)
{
  frameIndex++;

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
}

static glm::vec2 GetJitterOffset(uint32_t frameIndex, uint32_t renderWidth, uint32_t renderHeight, uint32_t windowWidth)
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

void FrogRenderer::OnRender([[maybe_unused]] double dt)
{
  std::swap(frame.gDepth, frame.gDepthPrev);
  std::swap(frame.gNormal, frame.gNormalPrev);

  shadingUniforms.sunDir = glm::vec4(sin(sunElevation) * cos(sunAzimuth), cos(sunElevation), sin(sunElevation) * sin(sunAzimuth), 0);
  shadingUniforms.sunStrength = glm::vec4{sunStrength * sunColor, 0};
  
  const float fsr2LodBias = fsr2Enable ? log2(float(renderWidth) / float(windowWidth)) - 1.0f : 0.0f;

  Fwog::SamplerState ss;

  ss.minFilter = Fwog::Filter::NEAREST;
  ss.magFilter = Fwog::Filter::NEAREST;
  ss.addressModeU = Fwog::AddressMode::REPEAT;
  ss.addressModeV = Fwog::AddressMode::REPEAT;
  auto nearestSampler = Fwog::Sampler(ss);

  ss.lodBias = 0;
  ss.compareEnable = true;
  ss.compareOp = Fwog::CompareOp::LESS;
  auto shadowSampler = Fwog::Sampler(ss);

  constexpr float cameraNear = 0.1f;
  constexpr float cameraFar = 100.0f;
  constexpr float cameraFovY = glm::radians(70.f);
  const auto jitterOffset = fsr2Enable ? GetJitterOffset(frameIndex, renderWidth, renderHeight, windowWidth) : glm::vec2{};
  const auto jitterMatrix = glm::translate(glm::mat4(1), glm::vec3(jitterOffset, 0));
  const auto projUnjittered = glm::perspectiveZO(cameraFovY, aspectRatio, cameraNear, cameraFar);
  const auto projJittered = jitterMatrix * projUnjittered;

  // Set global uniforms
  const uint32_t meshletCount = (uint32_t)scene.meshlets.size();
  const auto viewProj = projJittered * mainCamera.GetViewMatrix();
  const auto viewProjUnjittered = projUnjittered * mainCamera.GetViewMatrix();
  mainCameraUniforms.oldViewProjUnjittered = frameIndex == 1 ? viewProjUnjittered : mainCameraUniforms.viewProjUnjittered;
  mainCameraUniforms.viewProjUnjittered = viewProjUnjittered;
  mainCameraUniforms.viewProj = viewProj;
  mainCameraUniforms.invViewProj = glm::inverse(mainCameraUniforms.viewProj);
  mainCameraUniforms.proj = projJittered;
  mainCameraUniforms.cameraPos = glm::vec4(mainCamera.position, 0.0);
  mainCameraUniforms.meshletCount = meshletCount;

  globalUniformsBuffer.UpdateData(mainCameraUniforms);

  shadowUniformsBuffer.UpdateData(shadowUniforms);

  glm::vec3 eye = glm::vec3{shadingUniforms.sunDir * -5.f};
  float eyeWidth = 7.0f;
  // shadingUniforms.viewPos = glm::vec4(camera.position, 0);
  shadingUniforms.sunProj = glm::orthoZO(-eyeWidth, eyeWidth, -eyeWidth, eyeWidth, -100.0f, 100.f);
  shadingUniforms.sunView = glm::lookAt(eye, glm::vec3(0), glm::vec3{0, 1, 0});
  shadingUniforms.sunViewProj = shadingUniforms.sunProj * shadingUniforms.sunView;
  shadingUniformsBuffer.UpdateData(shadingUniforms);

  mesheletIndirectCommand->UpdateData(Fwog::DrawIndexedIndirectCommand{.instanceCount = 1});

  Fwog::Compute("Meshlet Generate Pass",
                [&]
                {
                  Fwog::Cmd::BindStorageBuffer(0, *meshletBuffer);
                  Fwog::Cmd::BindStorageBuffer(3, *instancedMeshletBuffer);
                  Fwog::Cmd::BindStorageBuffer(6, *mesheletIndirectCommand);
                  Fwog::Cmd::BindUniformBuffer(5, globalUniformsBuffer);
                  Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::BUFFER_UPDATE_BIT);
                  Fwog::Cmd::BindComputePipeline(meshletGeneratePipeline);
                  Fwog::Cmd::Dispatch((meshletCount + 3) / 4, 1, 1);
                  Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::INDEX_BUFFER_BIT | Fwog::MemoryBarrierBit::COMMAND_BUFFER_BIT);
                });

  auto visbufferAttachment =
    Fwog::RenderColorAttachment{.texture = frame.visbuffer.value(), .loadOp = Fwog::AttachmentLoadOp::CLEAR, .clearValue = {~0u, ~0u, ~0u, ~0u}};
  auto visbufferDepthAttachment = Fwog::RenderDepthStencilAttachment{
    .texture = frame.visDepth.value(),
    .loadOp = Fwog::AttachmentLoadOp::CLEAR,
    .clearValue = {.depth = 1.0f},
  };
  Fwog::Render(
    {
      .name = "Main Visbuffer Pass",
      .viewport =
        {
          Fwog::Viewport{
            .drawRect = {{0, 0}, {renderWidth, renderHeight}},
          },
        },
      .colorAttachments = {&visbufferAttachment, 1},
      .depthAttachment = visbufferDepthAttachment,
    },
    [&]
    {
      Fwog::Cmd::BindStorageBuffer(0, *meshletBuffer);
      Fwog::Cmd::BindStorageBuffer(1, *primitiveBuffer);
      Fwog::Cmd::BindStorageBuffer(2, *vertexBuffer);
      Fwog::Cmd::BindStorageBuffer(3, *indexBuffer);
      Fwog::Cmd::BindStorageBuffer(4, *transformBuffer);
      Fwog::Cmd::BindUniformBuffer(5, globalUniformsBuffer);
      Fwog::Cmd::BindGraphicsPipeline(visbufferPipeline);
      Fwog::Cmd::BindIndexBuffer(*instancedMeshletBuffer, Fwog::IndexType::UNSIGNED_INT);
      Fwog::Cmd::DrawIndexedIndirect(*mesheletIndirectCommand, 0, 1, 0);
    });

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
      Fwog::Cmd::BindStorageBuffer(0, *meshletBuffer);
      Fwog::Cmd::BindImage(0, frame.visbuffer.value(), 0);
      Fwog::Cmd::BindGraphicsPipeline(materialDepthPipeline);
      Fwog::Cmd::Draw(3, 1, 0, 0);
    });

  //auto visbufferResolveAttachment =
  //  Fwog::RenderColorAttachment{.texture = frame.visResolve.value(), .loadOp = Fwog::AttachmentLoadOp::CLEAR, .clearValue = { 0.0f }};
  auto visbufferResolveDepthAttachment = Fwog::RenderDepthStencilAttachment{
    .texture = frame.materialDepth.value(),
    .loadOp = Fwog::AttachmentLoadOp::LOAD,
  };
  auto gAlbedoAttachment = Fwog::RenderColorAttachment{
    .texture = frame.gAlbedo.value(),
    .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
  };
  auto gNormalAttachment = Fwog::RenderColorAttachment{
    .texture = frame.gNormal.value(),
    .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
  };
  auto gMotionAttachment = Fwog::RenderColorAttachment{
    .texture = frame.gMotion.value(),
    .loadOp = Fwog::AttachmentLoadOp::CLEAR,
    .clearValue = {0.f, 0.f, 0.f, 0.f},
  };
  auto gBufferAttachments = {gAlbedoAttachment, gNormalAttachment, gMotionAttachment};

  Fwog::Render(
    {
      .name = "Resolve Visbuffer Pass",
      .viewport =
        {
          Fwog::Viewport{
            .drawRect = {{0, 0}, {renderWidth, renderHeight}},
          },
        },
      //.colorAttachments = {&visbufferResolveAttachment, 1},
      .colorAttachments = gBufferAttachments,
      .depthAttachment = visbufferResolveDepthAttachment,
    },
    [&]
    {
      Fwog::Cmd::BindGraphicsPipeline(visbufferResolvePipeline);
      Fwog::Cmd::BindImage(0, frame.visbuffer.value(), 0);
      Fwog::Cmd::BindStorageBuffer(0, *meshletBuffer);
      Fwog::Cmd::BindStorageBuffer(1, *primitiveBuffer);
      Fwog::Cmd::BindStorageBuffer(2, *vertexBuffer);
      Fwog::Cmd::BindStorageBuffer(3, *indexBuffer);
      Fwog::Cmd::BindStorageBuffer(4, *transformBuffer);
      Fwog::Cmd::BindUniformBuffer(5, globalUniformsBuffer);
      Fwog::Cmd::BindStorageBuffer(7, *materialStorageBuffer);
      for (uint32_t materialId = 0; materialId < scene.materials.size(); ++materialId)
      {
        auto& material = scene.materials[materialId];
        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_BASE_COLOR_TEXTURE)
        {
          auto& [texture, sampler] = material.albedoTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage(0, texture, Fwog::Sampler(sampler));
        }
        Fwog::Cmd::Draw(3, 1, 0, materialId);
      }
    });

  /*// Render scene geometry to the g-buffer
  auto gAlbedoAttachment = Fwog::RenderColorAttachment{
    .texture = frame.gAlbedo.value(),
    .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
  };
  auto gNormalAttachment = Fwog::RenderColorAttachment{
    .texture = frame.gNormal.value(),
    .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
  };
  auto gMotionAttachment = Fwog::RenderColorAttachment{
    .texture = frame.gMotion.value(),
    .loadOp = Fwog::AttachmentLoadOp::CLEAR,
    .clearValue = {0.f, 0.f, 0.f, 0.f},
  };
  auto gDepthAttachment = Fwog::RenderDepthStencilAttachment{
    .texture = frame.gDepth.value(),
    .loadOp = Fwog::AttachmentLoadOp::CLEAR,
    .clearValue = {.depth = 1.0f},
  };
  Fwog::RenderColorAttachment cgAttachments[] = {gAlbedoAttachment, gNormalAttachment, gMotionAttachment};
  Fwog::Render(
    {
      .name = "Base Pass",
      .viewport =
        Fwog::Viewport{
          .drawRect = {{0, 0}, {renderWidth, renderHeight}},
          //.depthRange = Fwog::ClipDepthRange::NEGATIVE_ONE_TO_ONE,
        },
      .colorAttachments = cgAttachments,
      .depthAttachment = gDepthAttachment,
    },
    [&]
    {
      Fwog::Cmd::BindGraphicsPipeline(scenePipeline);
      Fwog::Cmd::BindUniformBuffer(0, globalUniformsBuffer);
      Fwog::Cmd::BindUniformBuffer(2, materialUniformsBuffer);

      Fwog::Cmd::BindStorageBuffer(1, *meshUniformBuffer);
      for (uint32_t i = 0; i < static_cast<uint32_t>(scene.meshes.size()); i++)
      {
        const auto& mesh = scene.meshes[i];
        const auto& material = scene.materials[mesh.materialIdx];
        materialUniformsBuffer.UpdateData(material.gpuMaterial);
        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_BASE_COLOR_TEXTURE)
        {
          const auto& textureSampler = material.albedoTextureSampler.value();
          auto sampler = textureSampler.sampler;
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage(0, textureSampler.texture, Fwog::Sampler(sampler));
        }
        Fwog::Cmd::BindVertexBuffer(0, mesh.vertexBuffer, 0, sizeof(Utility::Vertex));
        Fwog::Cmd::BindIndexBuffer(mesh.indexBuffer, Fwog::IndexType::UNSIGNED_INT);
        Fwog::Cmd::DrawIndexed(static_cast<uint32_t>(mesh.indexBuffer.Size()) / sizeof(uint32_t), 1, 0, 0, i);
      }
    });*/

  rsmUniforms.UpdateData(shadingUniforms.sunViewProj);

  /*// Shadow map (RSM) scene pass
  auto rcolorAttachment = Fwog::RenderColorAttachment{
    .texture = rsmFlux,
    .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
  };
  auto rnormalAttachment = Fwog::RenderColorAttachment{
    .texture = rsmNormal,
    .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
  };
  auto rdepthAttachment = Fwog::RenderDepthStencilAttachment{
    .texture = rsmDepth,
    .loadOp = Fwog::AttachmentLoadOp::CLEAR,
    .clearValue = {.depth = 1.0f},
  };
  Fwog::RenderColorAttachment crAttachments[] = {rcolorAttachment, rnormalAttachment};
  Fwog::Render(
    {
      .name = "RSM Scene",
      .colorAttachments = crAttachments,
      .depthAttachment = rdepthAttachment,
    },
    [&]
    {
      Fwog::Cmd::BindGraphicsPipeline(rsmScenePipeline);
      Fwog::Cmd::BindUniformBuffer(0, rsmUniforms);
      Fwog::Cmd::BindUniformBuffer(1, shadingUniformsBuffer);
      Fwog::Cmd::BindUniformBuffer(2, materialUniformsBuffer);

      Fwog::Cmd::BindStorageBuffer(1, *meshUniformBuffer, 0);
      for (uint32_t i = 0; i < static_cast<uint32_t>(scene.meshes.size()); i++)
      {
        const auto& mesh = scene.meshes[i];
        const auto& material = scene.materials[mesh.materialIdx];
        materialUniformsBuffer.UpdateData(material.gpuMaterial);
        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_BASE_COLOR_TEXTURE)
        {
          const auto& textureSampler = material.albedoTextureSampler.value();
          Fwog::Cmd::BindSampledImage(0, textureSampler.texture, Fwog::Sampler(textureSampler.sampler));
        }
        Fwog::Cmd::BindVertexBuffer(0, mesh.vertexBuffer, 0, sizeof(Utility::Vertex));
        Fwog::Cmd::BindIndexBuffer(mesh.indexBuffer, Fwog::IndexType::UNSIGNED_INT);
        Fwog::Cmd::DrawIndexed(static_cast<uint32_t>(mesh.indexBuffer.Size()) / sizeof(uint32_t), 1, 0, 0, i);
      }
    });

  {
    auto rsmCameraUniforms = RSM::CameraUniforms{
      .viewProj = projUnjittered * mainCamera.GetViewMatrix(),
      .invViewProj = glm::inverse(viewProjUnjittered),
      .proj = projUnjittered,
      .cameraPos = glm::vec4(mainCamera.position, 0),
      .viewDir = mainCamera.GetForwardDir(),
      .jitterOffset = jitterOffset,
      .lastFrameJitterOffset = fsr2Enable ? GetJitterOffset(frameIndex - 1, renderWidth, renderHeight, windowWidth) : glm::vec2{},
    };
    static Fwog::TimerQueryAsync timer(5);
    if (auto t = timer.PopTimestamp())
    {
      rsmPerformance = *t / 10e5;
    }
    Fwog::TimerScoped scopedTimer(timer);
    frame.rsm->ComputeIndirectLighting(shadingUniforms.sunViewProj,
                                       rsmCameraUniforms,
                                       frame.gAlbedo.value(),
                                       frame.gNormal.value(),
                                       frame.gDepth.value(),
                                       rsmFlux,
                                       rsmNormal,
                                       rsmDepth,
                                       frame.gDepthPrev.value(),
                                       frame.gNormalPrev.value(),
                                       frame.gMotion.value());
  }*/

  // clear cluster indices atomic counter
  // clusterIndicesBuffer.ClearSubData(0, sizeof(uint32_t), Fwog::Format::R32_UINT, Fwog::UploadFormat::R, Fwog::UploadType::UINT, &zero);

  // record active clusters
  // TODO

  // light culling+cluster assignment

  //

  // shading pass (full screen tri)

  /*auto shadingColorAttachment = Fwog::RenderColorAttachment{
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
      Fwog::Cmd::BindGraphicsPipeline(shadingPipeline);
      Fwog::Cmd::BindSampledImage(0, *frame.gAlbedo, nearestSampler);
      Fwog::Cmd::BindSampledImage(1, *frame.gNormal, nearestSampler);
      Fwog::Cmd::BindSampledImage(2, *frame.gDepth, nearestSampler);
      Fwog::Cmd::BindSampledImage(3, frame.rsm->GetIndirectLighting(), nearestSampler);
      Fwog::Cmd::BindSampledImage(4, rsmDepth, nearestSampler);
      Fwog::Cmd::BindSampledImage(5, rsmDepth, shadowSampler);
      Fwog::Cmd::BindUniformBuffer(0, globalUniformsBuffer);
      Fwog::Cmd::BindUniformBuffer(1, shadingUniformsBuffer);
      Fwog::Cmd::BindUniformBuffer(2, shadowUniformsBuffer);
      Fwog::Cmd::BindStorageBuffer(0, *lightBuffer);
      Fwog::Cmd::Draw(3, 1, 0, 0);
    });

#ifdef FROGRENDER_FSR2_ENABLE
  if (fsr2Enable)
  {
    Fwog::Compute("FSR 2",
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
                      .color = ffxGetTextureResourceGL(frame.colorHdrRenderRes->Handle(), renderWidth, renderHeight, GL_R11F_G11F_B10F),
                      .depth = ffxGetTextureResourceGL(frame.gDepth->Handle(), renderWidth, renderHeight, GL_DEPTH_COMPONENT32F),
                      .motionVectors = ffxGetTextureResourceGL(frame.gMotion->Handle(), renderWidth, renderHeight, GL_RG16F),
                      .exposure = {},
                      .reactive = {},
                      .transparencyAndComposition = {},
                      .output = ffxGetTextureResourceGL(frame.colorHdrWindowRes->Handle(), windowWidth, windowHeight, GL_R11F_G11F_B10F),
                      .jitterOffset = {jitterX, jitterY},
                      .motionVectorScale = {float(renderWidth), float(renderHeight)},
                      .renderSize = {renderWidth, renderHeight},
                      .enableSharpening = fsr2Sharpness != 0,
                      .sharpness = fsr2Sharpness,
                      .frameTimeDelta = static_cast<float>(dt * 1000.0),
                      .preExposure = 1,
                      .reset = false,
                      .cameraNear = cameraNear,
                      .cameraFar = cameraFar,
                      .cameraFovAngleVertical = cameraFovY,
                      .viewSpaceToMetersFactor = 1,
                      .deviceDepthNegativeOneToOne = false,
                    };

                    if (auto err = ffxFsr2ContextDispatch(&fsr2Context, &dispatchDesc); err != FFX_OK)
                    {
                      printf("FSR 2 error: %d\n", err);
                    }
                  });
  }
  Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT);
#endif

  const auto ppAttachment = Fwog::RenderColorAttachment{
    .texture = frame.colorLdrWindowRes.value(),
    .loadOp = Fwog::AttachmentLoadOp::DONT_CARE,
  };

  Fwog::Render(
    {
      .name = "Postprocessing",
      .colorAttachments = {&ppAttachment, 1},
    },
    [&]
    {
      Fwog::Cmd::BindGraphicsPipeline(postprocessingPipeline);
      Fwog::Cmd::BindSampledImage(0, fsr2Enable ? frame.colorHdrWindowRes.value() : frame.colorHdrRenderRes.value(), nearestSampler);
      Fwog::Cmd::BindSampledImage(1, noiseTexture.value(), nearestSampler);
      Fwog::Cmd::Draw(3, 1, 0, 0);
    });

  // TODO: Conditionally render to the screen when an option to hide the UI is selected
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
      if (glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS)
        tex = &frame.rsm->GetIndirectLighting();
      if (tex)
      {
        Fwog::Cmd::BindGraphicsPipeline(debugTexturePipeline);
        Fwog::Cmd::BindSampledImage(0, *tex, nearestSampler);
        Fwog::Cmd::Draw(3, 1, 0, 0);
      }
    });*/
}