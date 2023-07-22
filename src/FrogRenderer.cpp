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

  for (auto& plane : planes) {
      plane = glm::normalize(plane);
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

static Fwog::ComputePipeline CreateHzbCopyPipeline()
{
  auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/hzb/HZBCopy.comp.glsl");

  return Fwog::ComputePipeline({
    .shader = &comp,
  });
}

static Fwog::ComputePipeline CreateHzbReducePipeline()
{
  auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/hzb/HZBReduce.comp.glsl");

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
    .depthState = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = Fwog::CompareOp::ALWAYS},
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
    .depthState = {.depthTestEnable = true, .depthWriteEnable = false, .depthCompareOp = Fwog::CompareOp::EQUAL},
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

static Fwog::GraphicsPipeline CreateDebugLinesPipeline()
{
  auto vs = Fwog::Shader(Fwog::PipelineStage::VERTEX_SHADER, Application::LoadFile("shaders/Debug.vert.glsl"));
  auto fs = Fwog::Shader(Fwog::PipelineStage::FRAGMENT_SHADER, Application::LoadFile("shaders/Flat.frag.glsl"));

  auto inputBinding = Fwog::VertexInputBindingDescription{
    .location = 0,
    .binding = 0,
    .format = Fwog::Format::R32G32B32_FLOAT,
    .offset = 0,
  };

  return Fwog::GraphicsPipeline({
    .vertexShader = &vs,
    .fragmentShader = &fs,
    .inputAssemblyState = {.topology = Fwog::PrimitiveTopology::LINE_LIST},
    .vertexInputState = {{&inputBinding, 1}},
    .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
    .depthState =
      {
        .depthTestEnable = true,
        .depthWriteEnable = false,
      },
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
    hzbCopyPipeline(CreateHzbCopyPipeline()),
    hzbReducePipeline(CreateHzbReducePipeline()),
    visbufferPipeline(CreateVisbufferPipeline()),
    materialDepthPipeline(CreateMaterialDepthPipeline()),
    visbufferResolvePipeline(CreateVisbufferResolvePipeline()),
    scenePipeline(CreateScenePipeline()),
    rsmScenePipeline(CreateShadowPipeline()),
    shadingPipeline(CreateShadingPipeline()),
    postprocessingPipeline(CreatePostprocessingPipeline()),
    debugTexturePipeline(CreateDebugTexturePipeline()),
    debugLinesPipeline(CreateDebugLinesPipeline())
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

  cameraSpeed = 3.5f;
  mainCamera.position.y = 1;

  if (!filename)
  {
    Utility::LoadModelFromFileMeshlet(scene, "models/simple_scene.glb", glm::scale(glm::vec3{.125}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "models/light_test.glb", glm::scale(glm::vec3{.125}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "/run/media/master/Samsung S0/Dev/CLion/IrisVk/models/sponza/Sponza.gltf", glm::scale(glm::vec3{.125}), false);

    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/modular_ruins_c_2.glb", glm::scale(glm::vec3{.125}), true);

    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf", glm::scale(glm::vec3{.5}), false);

    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_compressed.glb", glm::scale(glm::vec3{.25}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_curtains_compressed.glb", glm::scale(glm::vec3{.25}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_ivy_compressed.glb", glm::scale(glm::vec3{.25}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_tree_compressed.glb", glm::scale(glm::vec3{.25}), true);

    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/deccer-cubes/SM_Deccer_Cubes_Textured.glb", glm::scale(glm::vec3{0.5f}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/subdiv_deccer_cubes.glb", glm::scale(glm::vec3{.5}), true);
    //Utility::LoadModelFromFileMeshlet(scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/subdiv_inverted_cube.glb", glm::scale(glm::vec3{.25}), true);
  }
  else
  {
    Utility::LoadModelFromFileMeshlet(scene, *filename, glm::scale(glm::vec3{scale}), binary);
  }
  
  lightBuffer = Fwog::TypedBuffer<Utility::GpuLight>(scene.lights);

  printf("Loaded %zu lights\n", scene.lights.size());

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

  meshUniformBuffer.emplace(meshUniforms, Fwog::BufferStorageFlag::DYNAMIC_STORAGE);

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
  frame.gRoughnessMetallicAoSwizzled = frame.gMetallicRoughnessAo->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gEmissionSwizzled = frame.gEmission->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gNormalSwizzled = frame.gNormal->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gDepthSwizzled = frame.gDepth->CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
  frame.gRsmIlluminanceSwizzled = frame.rsm->GetIndirectLighting().CreateSwizzleView({.a = Fwog::ComponentSwizzle::ONE});
}

void FrogRenderer::OnUpdate([[maybe_unused]] double dt)
{
  frameIndex++;

  debugLines.clear();

  if (debugDisplayMainFrustum)
  {
    auto invViewProj = glm::inverse(debugMainViewProj);

    // Get frustum corners in world space
    auto tln = UnprojectUV_ZO(0, {0, 1}, invViewProj);
    auto trn = UnprojectUV_ZO(0, {1, 1}, invViewProj);
    auto bln = UnprojectUV_ZO(0, {0, 0}, invViewProj);
    auto brn = UnprojectUV_ZO(0, {1, 0}, invViewProj);

    auto tlf = UnprojectUV_ZO(1, {0, 1}, invViewProj);
    auto trf = UnprojectUV_ZO(1, {1, 1}, invViewProj);
    auto blf = UnprojectUV_ZO(1, {0, 0}, invViewProj);
    auto brf = UnprojectUV_ZO(1, {1, 0}, invViewProj);

    // Connect-the-dots
    // Near and far "squares"
    debugLines.emplace_back(tln, trn);
    debugLines.emplace_back(bln, brn);
    debugLines.emplace_back(tln, bln);
    debugLines.emplace_back(trn, brn);
    debugLines.emplace_back(tlf, trf);
    debugLines.emplace_back(blf, brf);
    debugLines.emplace_back(tlf, blf);
    debugLines.emplace_back(trf, brf);

    // Lines connecting near and far planes
    debugLines.emplace_back(tln, tlf);
    debugLines.emplace_back(trn, trf);
    debugLines.emplace_back(bln, blf);
    debugLines.emplace_back(brn, brf);
  }

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

  ss = {};
  ss.minFilter = Fwog::Filter::NEAREST;
  ss.magFilter = Fwog::Filter::NEAREST;
  ss.mipmapFilter = Fwog::Filter::NEAREST;
  auto hzbSampler = Fwog::Sampler(ss);

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
  if (updateCullingFrustum) {
    mainCameraUniforms.oldViewProjUnjittered = frameIndex == 1 ? viewProjUnjittered : mainCameraUniforms.viewProjUnjittered;
  }
  mainCameraUniforms.viewProjUnjittered = viewProjUnjittered;
  mainCameraUniforms.viewProj = viewProj;
  mainCameraUniforms.invViewProj = glm::inverse(mainCameraUniforms.viewProj);
  mainCameraUniforms.proj = projJittered;
  mainCameraUniforms.cameraPos = glm::vec4(mainCamera.position, 0.0);
  mainCameraUniforms.meshletCount = meshletCount;
  mainCameraUniforms.bindlessSamplerLodBias = fsr2LodBias;
  if (updateCullingFrustum)
  {
    MakeFrustumPlanes(viewProjUnjittered, mainCameraUniforms.frustumPlanes);
    debugMainViewProj = viewProjUnjittered;
  }

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

  Fwog::Compute(
    "Meshlet Generate Pass",
    [&]
    {
      Fwog::Cmd::BindStorageBuffer(0, *meshletBuffer);
      Fwog::Cmd::BindStorageBuffer(3, *instancedMeshletBuffer);
      Fwog::Cmd::BindStorageBuffer(4, *transformBuffer);
      Fwog::Cmd::BindStorageBuffer(6, *mesheletIndirectCommand);
      Fwog::Cmd::BindUniformBuffer(5, globalUniformsBuffer);
      Fwog::Cmd::BindSampledImage(0, *frame.hzb, hzbSampler);
      Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::BUFFER_UPDATE_BIT);
      Fwog::Cmd::BindComputePipeline(meshletGeneratePipeline);
      Fwog::Cmd::Dispatch((meshletCount + 3) / 4, 1, 1);
      Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::INDEX_BUFFER_BIT | Fwog::MemoryBarrierBit::COMMAND_BUFFER_BIT);
    });

  auto visbufferAttachment =
    Fwog::RenderColorAttachment{.texture = frame.visbuffer.value(), .loadOp = Fwog::AttachmentLoadOp::CLEAR, .clearValue = {~0u, ~0u, ~0u, ~0u}};
  auto visbufferDepthAttachment = Fwog::RenderDepthStencilAttachment{
    .texture = frame.gDepth.value(),
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

  if (updateCullingFrustum)
  {
    Fwog::Compute("HZB Build Pass",
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
          Fwog::Cmd::BindImage(0, *frame.hzb, level - 1);
          Fwog::Cmd::BindImage(1, *frame.hzb, level);
          hzbCurrentWidth = std::max(1u, hzbCurrentWidth >> 1);
          hzbCurrentHeight = std::max(1u, hzbCurrentHeight >> 1);
          Fwog::Cmd::Dispatch((hzbCurrentWidth + 15) / 16, (hzbCurrentHeight + 15) / 16, 1);
        }
        Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT);
      });
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
      Fwog::Cmd::BindStorageBuffer(0, *meshletBuffer);
      Fwog::Cmd::BindImage(0, frame.visbuffer.value(), 0);
      Fwog::Cmd::BindGraphicsPipeline(materialDepthPipeline);
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
      Fwog::Cmd::BindImage(0, frame.visbuffer.value(), 0);
      Fwog::Cmd::BindStorageBuffer(0, *meshletBuffer);
      Fwog::Cmd::BindStorageBuffer(1, *primitiveBuffer);
      Fwog::Cmd::BindStorageBuffer(2, *vertexBuffer);
      Fwog::Cmd::BindStorageBuffer(3, *indexBuffer);
      Fwog::Cmd::BindStorageBuffer(4, *transformBuffer);
      Fwog::Cmd::BindUniformBuffer(5, globalUniformsBuffer);
      Fwog::Cmd::BindStorageBuffer(7, *materialStorageBuffer);

      // Render a full-screen tri for each material, but only fragments with matching material (stored in depth) are shaded
      for (uint32_t materialId = 0; materialId < scene.materials.size(); ++materialId)
      {
        auto& material = scene.materials[materialId];

        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_BASE_COLOR_TEXTURE)
        {
          auto& [texture, sampler] = material.albedoTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage(0, texture, Fwog::Sampler(sampler));
        }

        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_METALLIC_ROUGHNESS_TEXTURE)
        {
          auto& [texture, sampler] = material.metallicRoughnessTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage(1, texture, Fwog::Sampler(sampler));
        }

        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_NORMAL_TEXTURE)
        {
          auto& [texture, sampler] = material.normalTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage(2, texture, Fwog::Sampler(sampler));
        }

        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_OCCLUSION_TEXTURE)
        {
          auto& [texture, sampler] = material.occlusionTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage(3, texture, Fwog::Sampler(sampler));
        }

        if (material.gpuMaterial.flags & Utility::MaterialFlagBit::HAS_EMISSION_TEXTURE)
        {
          auto& [texture, sampler] = material.emissiveTextureSampler.value();
          sampler.lodBias = fsr2LodBias;
          Fwog::Cmd::BindSampledImage(4, texture, Fwog::Sampler(sampler));
        }

        Fwog::Cmd::Draw(3, 1, 0, materialId);
      }
    });


  /*// Shadow map (RSM) scene pass
  rsmUniforms.UpdateData(shadingUniforms.sunViewProj);
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

  // After shading, we render debug geometry
  auto debugColorAttachment = Fwog::RenderColorAttachment{
    .texture = frame.colorHdrRenderRes.value(),
    .loadOp = Fwog::AttachmentLoadOp::LOAD,
  };
  auto debugDepthAttachment = Fwog::RenderDepthStencilAttachment{
    .texture = frame.gDepth.value(),
    .loadOp = Fwog::AttachmentLoadOp::LOAD,
  };
  Fwog::Render(
    {
      .name = "Debug Geometry",
      .colorAttachments = {&debugColorAttachment, 1},
      .depthAttachment = debugDepthAttachment,
    },
    [&]
    {
      // Lines
      if (!debugLines.empty())
      {
        auto vertexBuffer = Fwog::TypedBuffer<Line>(debugLines);
        Fwog::Cmd::BindGraphicsPipeline(debugLinesPipeline);
        Fwog::Cmd::BindUniformBuffer(0, globalUniformsBuffer);
        Fwog::Cmd::BindVertexBuffer(0, vertexBuffer, 0, sizeof(glm::vec3));
        Fwog::Cmd::Draw(uint32_t(debugLines.size() * 2), 1, 0, 0);
      }
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
    Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT);
  }
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
    });
}