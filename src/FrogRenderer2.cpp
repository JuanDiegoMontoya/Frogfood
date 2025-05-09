#include "FrogRenderer2.h"

#include "PipelineManager.h"
#include "SceneLoader.h"

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

#include <memory_resource>

#define CONCAT_HELPER(x, y) x##y
#define CONCAT(x, y)        CONCAT_HELPER(x, y)

#define TIME_SCOPE_GPU(statGroup, statEnum, commandBuffer) \
  stats[(int)(statGroup)][statEnum].Measure();   \
  const auto CONCAT(gpu_timer_, __LINE__) = stats[(int)(statGroup)][statEnum].MakeScopedTimer(commandBuffer); \
  TracyVkZoneTransient(tracyVkContext_, CONCAT(asdf, __LINE__), commandBuffer, statGroups[(int)(statGroup)].statNames[statEnum], true) 

static Fvog::Texture LoadTonyMcMapfaceTexture()
{
  int x{};
  int y{};
  auto* pixels = stbi_load((GetTextureDirectory() / "tony_mcmapface/lut.png").string().c_str(), &x, &y, nullptr, 4);
  if (!pixels)
  {
    throw std::runtime_error("Texture not found");
  }

  constexpr uint32_t dim = 48;
  if (x != dim || y != dim * dim) // Image should be a column of 48x48 images
  {
    throw std::runtime_error("Texture had invalid dimensions");
  }
  auto texture = Fvog::Texture({
      .viewType = VK_IMAGE_VIEW_TYPE_3D,
      .format   = Fvog::Format::R8G8B8A8_UNORM,
      .extent   = {dim, dim, dim},
      .usage    = Fvog::TextureUsage::READ_ONLY,
    },
    "Tony McMapface LUT");

  for (uint32_t i = 0; i < dim; i++)
  {
    texture.UpdateImageSLOW({
      .level       = 0,
      .offset      = {0, 0, i},
      .extent      = {dim, dim, 1},
      .data        = pixels + (i * 4 * dim * dim),
      .rowLength   = 0,
      .imageHeight = 0,
    });
  }

  stbi_image_free(pixels);
  return texture;
}

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

static std::vector<Debug::Line> GenerateSubfrustumWireframe(const glm::mat4& invViewProj,
  const glm::vec4& color,
  float near,
  float far,
  float bottom,
  float top,
  float left,
  float right)
{
  auto lines = std::vector<Debug::Line>{};

  // Get frustum corners in world space
  auto tln = Math::UnprojectUV_ZO(near, {left, top}, invViewProj);
  auto trn = Math::UnprojectUV_ZO(near, {right, top}, invViewProj);
  auto bln = Math::UnprojectUV_ZO(near, {left, bottom}, invViewProj);
  auto brn = Math::UnprojectUV_ZO(near, {right, bottom}, invViewProj);

  // Far corners are lerped slightly to near in case it is an infinite projection
  auto tlf = Math::UnprojectUV_ZO(glm::mix(far, near, 1e-5), {left, top}, invViewProj);
  auto trf = Math::UnprojectUV_ZO(glm::mix(far, near, 1e-5), {right, top}, invViewProj);
  auto blf = Math::UnprojectUV_ZO(glm::mix(far, near, 1e-5), {left, bottom}, invViewProj);
  auto brf = Math::UnprojectUV_ZO(glm::mix(far, near, 1e-5), {right, bottom}, invViewProj);

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

static std::vector<Debug::Line> GenerateFrustumWireframe(const glm::mat4& invViewProj, const glm::vec4& color, float near, float far)
{
  return GenerateSubfrustumWireframe(invViewProj, color, near, far, 0, 1, 0, 1);
}

void FrogRenderer2::CreatePipelines()
{
  cullMeshletsPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name             = "Cull Meshlets",
    .shaderModuleInfo = {.path = GetShaderDirectory() / "visbuffer/CullMeshlets.comp.glsl"},
  });

  cullTrianglesPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name             = "Cull Triangles",
    .shaderModuleInfo = {.path = GetShaderDirectory() / "visbuffer/CullTriangles.comp.glsl"},
  });

  hzbCopyPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name             = "HZB Copy",
    .shaderModuleInfo = {.path = GetShaderDirectory() / "hzb/HZBCopy.comp.glsl"},
  });

  hzbReducePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name             = "HZB Reduce",
    .shaderModuleInfo = {.path = GetShaderDirectory() / "hzb/HZBReduce.comp.glsl"},
  });

  visbufferPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Visbuffer",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "visbuffer/Visbuffer.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "visbuffer/Visbuffer.frag.glsl",
      },
    .state =
      PipelineManager::GraphicsPipelineState{
        .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = true,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::visbufferFormat}},
            .depthAttachmentFormat  = Frame::gDepthFormat,
          },
      },
  });

  visbufferResolvePipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Visbuffer Resolve",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "visbuffer/VisbufferResolve.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "visbuffer/VisbufferResolve.frag.glsl",
      },
    .state =
      {
        .rasterizationState  = {.cullMode = VK_CULL_MODE_NONE},
        .depthState          = {.depthTestEnable = false, .depthWriteEnable = false},
        .renderTargetFormats = {{
          Frame::gAlbedoFormat,
          Frame::gMetallicRoughnessAoFormat,
          Frame::gNormalAndFaceNormalFormat,
          Frame::gSmoothVertexNormalFormat,
          Frame::gEmissionFormat,
          Frame::gMotionFormat,
        }},
      },
  });

  tonemapPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name             = "Tonemap",
    .shaderModuleInfo = {.path = GetShaderDirectory() / "post/TonemapAndDither.comp.glsl"},
  });

  shadingPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Shading",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "ShadeDeferredPbr.frag.glsl",
      },
    .state =
      {
        .rasterizationState  = {.cullMode = VK_CULL_MODE_NONE},
        .renderTargetFormats = {{Frame::colorHdrRenderResFormat}},
      },
  });

  debugTexturePipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug Texture",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "Texture.frag.glsl",
      },
    .state =
      {
        .rasterizationState  = {.cullMode = VK_CULL_MODE_NONE},
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Fvog::detail::VkToFormat(swapchainFormat_.format)}},
          },
      },
  });

  debugLinesPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug Lines",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "debug/Debug.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "debug/VertexColor.frag.glsl",
      },
    .state =
      {
        .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST},
        .rasterizationState = {.cullMode = VK_CULL_MODE_NONE, .lineWidth = 2},
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = false,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::colorHdrRenderResFormat, Frame::gReactiveMaskFormat}},
            .depthAttachmentFormat  = Frame::gDepthFormat,
          },
      },
  });

  auto blend0 = Fvog::ColorBlendAttachmentState{
    .blendEnable         = true,
    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
  };

  auto blend1 = Fvog::ColorBlendAttachmentState{
    .blendEnable = false,
  };

  auto blends = {blend0, blend1};

  debugAabbsPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug AABBs",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "debug/DebugAabb.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "debug/VertexColor.frag.glsl",
      },
    .state =
      {
        .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP},
        .rasterizationState =
          {
            .polygonMode             = VK_POLYGON_MODE_LINE,
            .cullMode                = VK_CULL_MODE_NONE,
            .depthBiasEnable         = true,
            .depthBiasConstantFactor = 50.0f,
          },
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = false,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .colorBlendState =
          {
            .attachments = blends,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::colorHdrRenderResFormat, Frame::gReactiveMaskFormat}},
            .depthAttachmentFormat  = Frame::gDepthFormat,
          },
      },
  });

  debugRectsPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug Rects",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "debug/DebugRect.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "debug/VertexColor.frag.glsl",
      },
    .state =
      {
        .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN},
        .rasterizationState =
          {
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode    = VK_CULL_MODE_NONE,
          },
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = false,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .colorBlendState =
          {
            .attachments = blends,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::colorHdrRenderResFormat, Frame::gReactiveMaskFormat}},
            .depthAttachmentFormat  = Frame::gDepthFormat,
          },
      },
  });

  viewerVsmPageTablesPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug VSM Page Tables",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "debug/viewer/VsmDebugPageTable.frag.glsl",
      },
    .state =
      {
        .rasterizationState  = {.cullMode = VK_CULL_MODE_NONE},
        .renderTargetFormats = {.colorAttachmentFormats = {{viewerOutputTextureFormat}}},
      },
  });

  viewerVsmPhysicalPagesPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug VSM Physical Pages",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "debug/viewer/VsmPhysicalPages.frag.glsl",
      },
    .state =
      {
        .rasterizationState  = {.cullMode = VK_CULL_MODE_NONE},
        .renderTargetFormats = {.colorAttachmentFormats = {{viewerOutputTextureFormat}}},
      },
  });

  viewerVsmBitmaskHzbPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug VSM Physical Pages",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "debug/viewer/VsmBitmaskHzb.frag.glsl",
      },
    .state =
      {
        .rasterizationState  = {.cullMode = VK_CULL_MODE_NONE},
        .renderTargetFormats = {.colorAttachmentFormats = {{viewerOutputTextureFormat}}},
      },
  });

  viewerVsmPhysicalPagesOverdrawPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug VSM Physical Pages",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "debug/viewer/VsmOverdrawHeatmap.frag.glsl",
      },
    .state =
      {
        .rasterizationState  = {.cullMode = VK_CULL_MODE_NONE},
        .renderTargetFormats = {.colorAttachmentFormats = {{viewerOutputTextureFormat}}},
      },
  });

  calibrateHdrPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name             = "Calibrate HDR",
    .shaderModuleInfo = {.path = GetShaderDirectory() / "CalibrateHdr.comp.glsl"},
  });

  vsmShadowPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Shadow VSM",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "shadows/ShadowMain.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "shadows/vsm/VsmShadow.frag.glsl",
      },
    .state =
      {
        //.rasterizationState = {.cullMode = VK_CULL_MODE_BACK_BIT},
        .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
#if VSM_USE_TEMP_ZBUFFER
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = true,
          },
        .depthAttachmentFormat = Fvog::Format::D32_SFLOAT,
#endif
      },
  });
}

FrogRenderer2::FrogRenderer2(const Application::CreateInfo& createInfo)
  : Application(createInfo),
    // Create constant-size buffers
    globalUniformsBuffer(1, "Global Uniforms"),
    shadingUniformsBuffer(1, "Shading Uniforms"),
    shadowUniformsBuffer(1, "Shadow Uniforms"),
    geometryBuffer(1'000'000'000, "Geometry Buffer"),
    meshletInstancesBuffer(100'000'000 * sizeof(Render::MeshletInstance), "Meshlet Instances Buffer"),
    lightsBuffer(1'000 * sizeof(GpuLight), "Light Buffer"),
    // TODO: remove
//    testRayTracingPipeline(Pipelines2::TestRayTracingPipeline()),
//    testRayTracingOutput(Fvog::Texture({
//      .viewType = VK_IMAGE_VIEW_TYPE_2D,
//      .format = Fvog::Format::R8G8B8A8_UNORM,
//      .extent = { 1920, 1080, 1 },
//    })),
    forwardRenderer_(),
    tonemapUniformBuffer(1, "Tonemap Uniforms"),
    tonyMcMapfaceLut(LoadTonyMcMapfaceTexture()),
    calibrateHdrTexture(Fvog::CreateTexture2D({2, 2}, Fvog::Format::A2R10G10B10_UNORM, Fvog::TextureUsage::GENERAL, "HDR Calibration Texture")),
    //calibrateHdrPipeline(Pipelines2::CalibrateHdr()),
    bloom(),
    autoExposure(),
    exposureBuffer({}, "Exposure"),
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
//    vsmShadowPipeline(Pipelines2::ShadowVsm({
//#if VSM_USE_TEMP_ZBUFFER
//        .depthAttachmentFormat = Fvog::Format::D32_SFLOAT,
//#endif
//      })),
    vsmShadowUniformBuffer(),
    whiteTexture_(Fvog::CreateTexture2D({1, 1}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::READ_ONLY, "1x1 White Texture")),
    nearestSampler({
        .magFilter    = VK_FILTER_NEAREST,
        .minFilter    = VK_FILTER_NEAREST,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      },
      "Nearest"),
    linearMipmapSampler({
        .magFilter     = VK_FILTER_LINEAR,
        .minFilter     = VK_FILTER_LINEAR,
        .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxAnisotropy = 16,
      },
      "Linear Mipmap"),
    linearClampSampler({
        .magFilter     = VK_FILTER_LINEAR,
        .minFilter     = VK_FILTER_LINEAR,
        .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      },
      "Linear Clamp"),
    hzbSampler({
        .magFilter    = VK_FILTER_NEAREST,
        .minFilter    = VK_FILTER_NEAREST,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      },
      "HZB")
{
  ZoneScoped;

  CreatePipelines();

  int x = 0;
  int y = 0;
  const auto noise = stbi_load((GetTextureDirectory() / "bluenoise256.png").string().c_str(), &x, &y, nullptr, 4);
  assert(noise);
  noiseTexture = Fvog::CreateTexture2D({static_cast<uint32_t>(x), static_cast<uint32_t>(y)}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::READ_ONLY, "Noise");
  noiseTexture->UpdateImageSLOW({
    .extent = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
    .data = noise,
  });
  stbi_image_free(noise);

  InitGui();

  if (Fvog::GetDevice().supportsRayTracing)
  {
    rayTracedAo_.emplace();
  }

  mainCamera.position.y = 1;

  {
    ZoneScopedN("Load scene");

    auto* oldResource = std::pmr::get_default_resource();
    auto arena = std::pmr::monotonic_buffer_resource();
    auto sync  = std::pmr::synchronized_pool_resource(&arena);
    std::pmr::set_default_resource(&sync);

    scene.Import(*this, Utility::LoadModelFromFile(GetAssetDirectory() / "models/simple_scene.glb", glm::scale(glm::vec3{.5})));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/cube.glb", glm::scale(glm::vec3{1})));
    //Utility::LoadModelFromFile(*device_, scene, "H:\\Repositories\\glTF-Sample-Models\\2.0\\BoomBox\\glTF/BoomBox.gltf", glm::scale(glm::vec3{10.0f}));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf", glm::scale(glm::vec3{1})));
    //Utility::LoadModelFromFile(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/Main/NewSponza_Main_Blender_glTF.gltf", glm::scale(glm::vec3{1}));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/hotel_01.glb", glm::scale(glm::vec3{.125f})));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/bistro_compressed.glb", glm::scale(glm::vec3{.5})));
    //Utility::LoadModelFromFile(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_compressed.glb", glm::scale(glm::vec3{1}));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_compressed.glb", glm::scale(glm::vec3{1})));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/sponza_curtains_compressed.glb", glm::scale(glm::vec3{1})));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/SM_Airfield_Ground.glb", glm::scale(glm::vec3{1})));
    //Utility::LoadModelFromFile(*device_, scene, "H:/Repositories/glTF-Sample-Models/downloaded schtuff/subdiv_deccer_cubes.glb", glm::scale(glm::vec3{1}));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/SM_Deccer_Cubes_Textured.glb", glm::scale(glm::vec3{1})));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/small_city.glb", glm::scale(glm::vec3{1})));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/2.0/Box/glTF/Box.gltf", glm::scale(glm::vec3{1})));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/curtain.glb", glm::scale(glm::vec3{1})));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/triangles.glb", glm::scale(glm::vec3{1})));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/cornell.glb", glm::scale(glm::vec3{1})));
    //scene.Import(*this, Utility::LoadModelFromFile("H:/Repositories/glTF-Sample-Models/downloaded schtuff/cornell_box(1).glb", glm::translate(glm::vec3{3, 0, 0})));
    
    std::pmr::set_default_resource(oldResource);
  }

  meshletIndirectCommand = Fvog::TypedBuffer<Fvog::DrawIndexedIndirectCommand>({}, "Meshlet Indirect Command");
  cullTrianglesDispatchParams = Fvog::TypedBuffer<Fvog::DispatchIndirectCommand>({}, "Cull Triangles Dispatch Params");
  viewBuffer = Fvog::TypedBuffer<ViewParams>({}, "View Data");

  debugGpuAabbsBuffer = Fvog::Buffer({sizeof(Fvog::DrawIndirectCommand) + sizeof(Debug::Aabb) * 100'000}, "Debug GPU AABBs");

  debugGpuRectsBuffer = Fvog::Buffer({sizeof(Fvog::DrawIndirectCommand) + sizeof(Debug::Rect) * 100'000}, "Debug GPU Rects");

  debugGpuLinesBuffer = Fvog::Buffer({sizeof(Fvog::DrawIndirectCommand) + sizeof(Debug::Line) * 100'000}, "Debug GPU Lines");

  Fvog::GetDevice().ImmediateSubmit(
    [this](VkCommandBuffer commandBuffer)
    {
      auto ctx = Fvog::Context(commandBuffer);

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
      
      auto lineCommand = Fvog::DrawIndirectCommand{
        .vertexCount   = 2,
        .instanceCount = 0,
        .firstVertex   = 0,
        .firstInstance = 0,
      };
      ctx.TeenyBufferUpdate(*debugGpuLinesBuffer, lineCommand);
      
      exposureBuffer.FillData(commandBuffer, {.data = std::bit_cast<uint32_t>(1.0f)});
      cullTrianglesDispatchParams->UpdateDataExpensive(commandBuffer, Fvog::DispatchIndirectCommand{0, 1, 1});
    });

  stats.resize(std::size(statGroups));
  for (size_t i = 0; i < std::size(statGroups); i++)
  {
    for (auto statName : statGroups[i].statNames)
    {
      stats[i].emplace_back(statName);
    }
  }

  constexpr auto whiteUnorm8 = glm::u8vec4(255, 255, 255, 255);
  whiteTexture_.UpdateImageSLOW({.extent = {1, 1, 1}, .data = &whiteUnorm8});

  constexpr auto vsmExtent = Fvog::Extent2D{Techniques::VirtualShadowMaps::maxExtent, Techniques::VirtualShadowMaps::maxExtent};
  vsmTempDepthStencil      = Fvog::CreateTexture2D(vsmExtent, Fvog::Format::D32_SFLOAT, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "VSM Temp Depth Stencil");

  OnFramebufferResize(windowFramebufferWidth, windowFramebufferHeight);
  // The main loop might invoke the resize callback (which in turn causes a redraw) on the first frame, and OnUpdate produces
  // some resources necessary for rendering (but can be resused). This is a minor hack to make sure those resources are
  // available before the windowing system could issue a redraw.
  OnUpdate(0);
}

FrogRenderer2::~FrogRenderer2()
{
  vkDeviceWaitIdle(Fvog::GetDevice().device_);

  meshGeometryAllocations.clear();
  meshAllocations.clear();
  lightAllocations.clear();
  materialAllocations.clear();

  Fvog::GetDevice().FreeUnusedResources();

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
      vkDeviceWaitIdle(Fvog::GetDevice().device_);
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
      .device        = ffxGetDeviceVK(Fvog::GetDevice().device_),
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

    auto scratchMemorySize = ffxFsr2GetScratchMemorySizeVK(Fvog::GetDevice().physicalDevice_, vkEnumerateDeviceExtensionProperties);
    fsr2ScratchMemory = std::make_unique<char[]>(scratchMemorySize);
    ffxFsr2GetInterfaceVK(&contextDesc.callbacks,
      fsr2ScratchMemory.get(),
      scratchMemorySize,
      Fvog::GetDevice().physicalDevice_,
      vkGetDeviceProcAddr,
      vkGetPhysicalDeviceMemoryProperties,
      vkGetPhysicalDeviceProperties2,
      vkGetPhysicalDeviceFeatures2,
      vkEnumerateDeviceExtensionProperties,
      vkGetPhysicalDeviceProperties);
    ffxFsr2ContextCreate(&fsr2Context, &contextDesc);
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
  frame.visbuffer = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::visbufferFormat, usage, "visbuffer");

  {
    const uint32_t hzbWidth = Math::PreviousPower2(renderInternalWidth);
    const uint32_t hzbHeight = Math::PreviousPower2(renderInternalHeight);
    const uint32_t hzbMips = 1 + static_cast<uint32_t>(glm::floor(glm::log2(static_cast<float>(glm::max(hzbWidth, hzbHeight)))));
    frame.hzb = Fvog::CreateTexture2DMip({hzbWidth, hzbHeight}, Frame::hzbFormat, hzbMips, Fvog::TextureUsage::GENERAL, "HZB");
    Fvog::GetDevice().ImmediateSubmit(
      [&, this](VkCommandBuffer cmd)
      {
        auto ctx = Fvog::Context(cmd);
        ctx.ImageBarrierDiscard(*frame.hzb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ctx.ClearTexture(*frame.hzb, {.color = {FAR_DEPTH}});
      });
  }

  // Create gbuffer textures and render info
  frame.gAlbedo = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::gAlbedoFormat, usage, "gAlbedo");
  frame.gMetallicRoughnessAo = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::gMetallicRoughnessAoFormat, usage, "gMetallicRoughnessAo");
  frame.gNormalAndFaceNormal = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::gNormalAndFaceNormalFormat, usage, "gNormalAndFaceNormal");
  frame.gSmoothVertexNormal = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::gSmoothVertexNormalFormat, usage, "gSmoothVertexNormal");
  frame.gEmission = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::gEmissionFormat, usage, "gEmission");
  frame.gDepth = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::gDepthFormat, usage, "gDepth");
  frame.gMotion = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::gMotionFormat, usage, "gMotion");
  frame.gNormaAndFaceNormallPrev = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::gNormalAndFaceNormalFormat, usage, "gNormaAndFaceNormallPrev");
  frame.gDepthPrev = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::gDepthPrevFormat, usage, "gDepthPrev");
  // The reamining are general so they can be written to in postprocessing passes via compute
  frame.colorHdrRenderRes = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::colorHdrRenderResFormat, Fvog::TextureUsage::GENERAL, "colorHdrRenderRes");
  frame.colorHdrWindowRes = Fvog::CreateTexture2D({newWidth, newHeight}, Frame::colorHdrWindowResFormat, Fvog::TextureUsage::GENERAL, "colorHdrWindowRes");
  frame.colorHdrBloomScratchBuffer = Fvog::CreateTexture2DMip({newWidth / 2, newHeight / 2}, Frame::colorHdrBloomScratchBufferFormat, 8, Fvog::TextureUsage::GENERAL, "colorHdrBloomScratchBuffer");
  frame.colorLdrWindowRes = Fvog::CreateTexture2D({newWidth, newHeight}, Frame::colorLdrWindowResFormat, Fvog::TextureUsage::GENERAL, "colorLdrWindowRes");
  frame.gReactiveMask = Fvog::CreateTexture2D({renderInternalWidth, renderInternalHeight}, Frame::gReactiveMaskFormat, Fvog::TextureUsage::GENERAL, "Reactive Mask");

  // Create debug views with alpha swizzle set to one so they can be seen in ImGui
  frame.gAlbedoSwizzled = frame.gAlbedo->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
  frame.gRoughnessMetallicAoSwizzled = frame.gMetallicRoughnessAo->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
  frame.gEmissionSwizzled = frame.gEmission->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
  frame.gNormalSwizzled = frame.gNormalAndFaceNormal->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});
  frame.gDepthSwizzled = frame.gDepth->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});

  frame.forwardRenderTarget = Fvog::CreateTexture2D({newWidth, newHeight}, Fvog::Format::R8G8B8A8_SRGB, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Forward Render Target");
  frame.forwardRenderTargetSwizzled = frame.forwardRenderTarget->CreateSwizzleView({.a = VK_COMPONENT_SWIZZLE_ONE});

  // TODO: only recreate if the swapchain format changed
  // TODO: add a function to change pipeline state (this code is verbose and leaks the old pipeline)
  debugTexturePipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Debug Texture",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "Texture.frag.glsl",
      },
    .state =
      {
        .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Fvog::detail::VkToFormat(swapchainFormat_.format)}},
          },
      },
  });
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

  scene.CalcUpdatedData(*this);

  shadingUniforms.numberOfLights = NumLights();

  // TODO: perhaps this logic belongs in Application
  if (shouldResizeNextFrame)
  {
    OnFramebufferResize(renderOutputWidth, renderOutputHeight);
    shouldResizeNextFrame = false;
  }
}

void FrogRenderer2::CullMeshletsForView(VkCommandBuffer commandBuffer, const ViewParams& view, Fvog::Buffer& visibleMeshletIds, std::string_view name)
{
  ZoneScoped;
  TracyVkZoneTransient(tracyVkContext_, tracyProfileVar, commandBuffer, name.data(), true);
  auto ctx = Fvog::Context(commandBuffer);
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
  
  ctx.BindComputePipeline(cullMeshletsPipeline.GetPipeline());

  auto vsmPushConstants = vsmContext.GetPushConstants();

  auto visbufferPushConstants = CullMeshletsPushConstants{
    .globalUniformsIndex   = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
    .meshletInstancesIndex = meshletInstancesBuffer.GetResourceHandle().index,
    .meshletDataIndex      = geometryBuffer.GetResourceHandle().index,
    .transformsIndex       = geometryBuffer.GetResourceHandle().index,
    .indirectDrawIndex     = meshletIndirectCommand->GetResourceHandle().index,
    .viewIndex             = viewBuffer->GetResourceHandle().index,

    .pageTablesIndex            = vsmPushConstants.pageTablesIndex,
    .physicalPagesIndex         = vsmPushConstants.physicalPagesIndex,
    .vsmBitmaskHzbIndex         = vsmPushConstants.vsmBitmaskHzbIndex,
    .vsmUniformsBufferIndex     = vsmPushConstants.vsmUniformsBufferIndex,
    .clipmapUniformsBufferIndex = vsmSun.clipmapUniformsBuffer_.GetResourceHandle().index,
    .nearestSamplerIndex        = nearestSampler.GetResourceHandle().index,

    .hzbIndex                   = frame.hzb->ImageView().GetSampledResourceHandle().index,
    .hzbSamplerIndex            = hzbSampler.GetResourceHandle().index,
    .cullTrianglesDispatchIndex = cullTrianglesDispatchParams->GetResourceHandle().index,
    .visibleMeshletsIndex       = visibleMeshletIds.GetResourceHandle().index,
    .debugAabbBufferIndex       = debugGpuAabbsBuffer->GetResourceHandle().index,
    .debugRectBufferIndex       = debugGpuRectsBuffer->GetResourceHandle().index,
  };
  ctx.SetPushConstants(visbufferPushConstants);
  
  ctx.DispatchInvocations(NumMeshletInstances(), 1, 1);
  
  ctx.Barrier();
  
  ctx.BindComputePipeline(cullTrianglesPipeline.GetPipeline());
  visbufferPushConstants.meshletPrimitivesIndex = geometryBuffer.GetResourceHandle().index;
  visbufferPushConstants.meshletVerticesIndex   = geometryBuffer.GetResourceHandle().index;
  visbufferPushConstants.meshletIndicesIndex    = geometryBuffer.GetResourceHandle().index;
  visbufferPushConstants.indexBufferIndex       = instancedMeshletBuffer->GetResourceHandle().index;
  ctx.SetPushConstants(visbufferPushConstants);

  ctx.DispatchIndirect(cullTrianglesDispatchParams.value());

  //Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::INDEX_BUFFER_BIT | Fwog::MemoryBarrierBit::COMMAND_BUFFER_BIT);
  ctx.Barrier();
}

void FrogRenderer2::OnRender([[maybe_unused]] double dt, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
{
  ZoneScoped;

  if (Fvog::GetDevice().frameNumber == 1)
  {
    dt = 0.016;
  }
  accumTimes.Push(accumTime += dt);
  stats[(int)StatGroup::eMainGpu][eFrame].timings.Push(dt * 1000);

  std::swap(frame.gDepth, frame.gDepthPrev);
  std::swap(frame.gNormalAndFaceNormal, frame.gNormaAndFaceNormallPrev);

  shadingUniforms.sunDir         = glm::vec4(SphericalToCartesian(sunElevation, sunAzimuth), 0);
  shadingUniforms.sunIlluminance = glm::vec4{sunIlluminance * sunColor, 0};
  {
    uint32_t state         = static_cast<uint32_t>(Fvog::GetDevice().frameNumber);
    shadingUniforms.random = {PCG::RandFloat(state), PCG::RandFloat(state)};
  }

  auto ctx = Fvog::Context(commandBuffer);

  const auto baseBias     = fsr2Enable ? log2(float(renderInternalWidth) / float(renderOutputWidth)) - 1.0f : 0.0f;
  const float fsr2LodBias = std::round(baseBias * 100) / 100;

  {
    ZoneScopedN("Update GPU Buffers");
    auto marker = ctx.MakeScopedDebugMarker("Update Buffers");

    auto actualTonemapUniforms = tonemapUniforms;
    // Special case for rendering to _SRGB images.
    if (!showGui && Fvog::detail::FormatIsSrgb(Fvog::detail::VkToFormat(swapchainFormat_.format)))
    {
      actualTonemapUniforms.tonemapOutputColorSpace = COLOR_SPACE_sRGB_LINEAR;
    }

    // Tonemappers use maxDisplayNits to determine the output brightness. Anything above 1 will result in incorrect output for SDR.
    const bool isSurfaceHDR = swapchainFormat_.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT || swapchainFormat_.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
    if (!isSurfaceHDR)
    {
      actualTonemapUniforms.maxDisplayNits = 1;
    }

    actualTonemapUniforms.shadingInternalColorSpace = shadingUniforms.shadingInternalColorSpace;
    tonemapUniformBuffer.UpdateData(commandBuffer, actualTonemapUniforms);

    // VSM lod bias corresponds to upscaling lod bias, otherwise shadows become blocky as the upscaling ratio increases.
    auto actualVsmUniforms = vsmUniforms;
    if (fsr2Enable)
    {
      actualVsmUniforms.lodBias += fsr2LodBias + 1.0f; // +1 to cancel "AA" factor and avoid too much negative bias (this should bring shadow detail to approx. pixel-scale)
    }
    vsmContext.UpdateUniforms(commandBuffer, actualVsmUniforms);
    ctx.Barrier();
  }

  FlushUpdatedSceneData(commandBuffer);

  if (Fvog::GetDevice().supportsRayTracing)
  {
    tlas = [&]
    {
      TracyVkZone(tracyVkContext_, commandBuffer, "Build TLAS");
      ZoneScopedN("Build TLAS");
      auto instances = std::vector<Fvog::TlasInstance>();
      instances.reserve(meshAllocations.size());
      for (const auto& [meshId, instance] : meshAllocations)
      {
        instances.push_back(instance.tlasInstance.value());
      }

      auto tlasInstances =
        Fvog::TypedBuffer<Fvog::TlasInstance>({(uint32_t)instances.size(), Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE}, "TLAS Instances Buffer");

      std::memcpy(tlasInstances.GetMappedMemory(), instances.data(), sizeof(Fvog::TlasInstance) * instances.size());

      return Fvog::Tlas(
        Fvog::TlasCreateInfo{
          .commandBuffer  = commandBuffer,
          .geoemtryFlags  = Fvog::AccelerationStructureGeometryFlag::OPAQUE,
          .buildFlags     = Fvog::AccelerationStructureBuildFlag::FAST_TRACE | Fvog::AccelerationStructureBuildFlag::ALLOW_DATA_ACCESS,
          .instanceBuffer = &tlasInstances,
        },
        "TLAS");
    }();
  }

  // A few of these buffers are really slow to create (2-3ms) and destroy every frame (large ones hit vkAllocateMemory), so
  // this scheme is still not ideal as e.g. adding geometry every frame will cause reallocs.
  // The current scheme works fine when the scene is mostly static.

  // Soft cap of 1 billion indices should prevent oversubscribing memory (on my system) when loading huge scenes.
  // This limit should be OK as it only limits post-culling geometry.
  const auto maxIndices = glm::min(1'000'000'000u, NumMeshletInstances() * Utility::maxMeshletPrimitives * 3);
  if (!instancedMeshletBuffer || instancedMeshletBuffer->Size() < maxIndices)
  {
    instancedMeshletBuffer = Fvog::TypedBuffer<uint32_t>({.count = maxIndices}, "Instanced Meshlets");
  }

  if (!persistentVisibleMeshletIds || persistentVisibleMeshletIds->Size() < NumMeshletInstances())
  {
    persistentVisibleMeshletIds = Fvog::TypedBuffer<uint32_t>({.count = std::max(1u, NumMeshletInstances())}, "Persistent Visible Meshlet IDs");
  }

  if (!transientVisibleMeshletIds || transientVisibleMeshletIds->Size() < NumMeshletInstances())
  {
    transientVisibleMeshletIds = Fvog::TypedBuffer<uint32_t>({.count = std::max(1u, NumMeshletInstances())}, "Transient Visible Meshlet IDs");
  }

  // Clear debug buffers
  debugGpuAabbsBuffer->FillData(commandBuffer, {.offset = offsetof(Fvog::DrawIndirectCommand, instanceCount), .size = sizeof(uint32_t), .data = 0});
  debugGpuRectsBuffer->FillData(commandBuffer, {.offset = offsetof(Fvog::DrawIndirectCommand, instanceCount), .size = sizeof(uint32_t), .data = 0});

  if (clearDebugGpuLinesOnce)
  {
    debugGpuLinesBuffer->FillData(commandBuffer, {.offset = offsetof(Fvog::DrawIndirectCommand, instanceCount), .size = sizeof(uint32_t), .data = 0});
    clearDebugGpuLinesOnce = false;
  }

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);

  if (Fvog::GetDevice().supportsRayTracing)
  {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);
  }

  const auto materialSampler = Fvog::Sampler({
    .magFilter     = VK_FILTER_LINEAR,
    .minFilter     = VK_FILTER_LINEAR,
    .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    .addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .mipLodBias    = fsr2LodBias,
    .maxAnisotropy = 16.0f,
  });

  const auto jitterOffset = fsr2Enable ? GetJitterOffset((uint32_t)Fvog::GetDevice().frameNumber, renderInternalWidth, renderInternalHeight, renderOutputWidth) : glm::vec2{};
  const auto jitterMatrix = glm::translate(glm::mat4(1), glm::vec3(jitterOffset, 0));
  // const auto projUnjittered = glm::perspectiveZO(cameraFovY, aspectRatio, cameraNear, cameraFar);
  const auto projUnjittered = Math::InfReverseZPerspectiveRH(cameraFovyRadians, aspectRatio, cameraNearPlane);
  const auto projJittered   = jitterMatrix * projUnjittered;

  // Set global uniforms
  const auto viewProj           = projJittered * mainCamera.GetViewMatrix();
  const auto viewProjUnjittered = projUnjittered * mainCamera.GetViewMatrix();

  if (Fvog::GetDevice().frameNumber == 1)
  {
    vsmSun.UpdateExpensive(commandBuffer, mainCamera.position, glm::vec3{-shadingUniforms.sunDir}, vsmFirstClipmapWidth, vsmDirectionalProjectionZLength);
  }
  else
  {
    vsmSun.UpdateOffset(commandBuffer, mainCamera.position);
  }

  globalUniforms.oldViewProjUnjittered = Fvog::GetDevice().frameNumber == 1 ? viewProjUnjittered : globalUniforms.viewProjUnjittered;

  auto mainView = ViewParams{
    .oldViewProj = globalUniforms.oldViewProjUnjittered,
    .proj        = projUnjittered,
    .view        = mainCamera.GetViewMatrix(),
    //.viewProj = viewProjUnjittered,
    .viewProj  = viewProj,
    .cameraPos = glm::vec4(mainCamera.position, 0.0),
    .viewport  = {0.0f, 0.0f, static_cast<float>(renderInternalWidth), static_cast<float>(renderInternalHeight)},
  };

  // TODO: the main view has an infinite projection, so we should omit the far plane. It seems to be causing the test to always pass.
  // We should probably emit the far plane regardless, but alas, one thing at a time.
  Math::MakeFrustumPlanes(viewProjUnjittered, mainView.frustumPlanes);
  debugMainViewProj = viewProjUnjittered;

  globalUniforms.viewProjUnjittered = viewProjUnjittered;
  globalUniforms.viewProj           = viewProj;
  globalUniforms.invViewProj        = glm::inverse(globalUniforms.viewProj);
  globalUniforms.proj               = projJittered;
  globalUniforms.invProj            = glm::inverse(globalUniforms.proj);
  globalUniforms.cameraPos          = glm::vec4(mainCamera.position, 0.0);
  globalUniforms.meshletCount       = NumMeshletInstances();
  // globalUniforms.maxIndices = static_cast<uint32_t>(scene.primitives.size() * 3);
  globalUniforms.maxIndices             = 0; // TODO: This doesn't seem to be used for anything.
  globalUniforms.bindlessSamplerLodBias = fsr2LodBias;

  globalUniformsBuffer.UpdateData(commandBuffer, globalUniforms);

  shadowUniformsBuffer.UpdateData(commandBuffer, shadowUniforms);

  shadingUniforms.materialBufferIndex = geometryBuffer.GetResourceHandle().index;
  shadingUniforms.instanceBufferIndex = geometryBuffer.GetResourceHandle().index;
  if (Fvog::GetDevice().supportsRayTracing)
  {
    shadingUniforms.tlasIndex   = tlas.value().GetResourceHandle().index;
    shadingUniforms.tlasAddress = tlas.value().GetAddress();
  }
  shadingUniforms.noiseTexture = noiseTexture->ImageView().GetTexture2D();
  shadingUniforms.frameNumber  = static_cast<uint32_t>(Fvog::GetDevice().frameNumber);
  shadingUniformsBuffer.UpdateData(commandBuffer, shadingUniforms);

  ctx.Barrier();
  
  {
    TIME_SCOPE_GPU(StatGroup::eMainGpu, eCullMeshletsMain, commandBuffer);
    CullMeshletsForView(commandBuffer, mainView, persistentVisibleMeshletIds.value(), "Cull Meshlets Main");
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
    TIME_SCOPE_GPU(StatGroup::eMainGpu, eRenderVisbufferMain, commandBuffer);
    ctx.BindGraphicsPipeline(visbufferPipeline.GetPipeline());
    auto visbufferArguments = VisbufferPushConstants{
      .globalUniformsIndex    = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .meshletInstancesIndex  = meshletInstancesBuffer.GetResourceHandle().index,
      .meshletDataIndex       = geometryBuffer.GetResourceHandle().index,
      .meshletPrimitivesIndex = geometryBuffer.GetResourceHandle().index,
      .meshletVerticesIndex   = geometryBuffer.GetResourceHandle().index,
      .meshletIndicesIndex    = geometryBuffer.GetResourceHandle().index,
      .transformsIndex        = geometryBuffer.GetResourceHandle().index,
      .indirectDrawIndex      = meshletIndirectCommand->GetResourceHandle().index,
      .materialsIndex         = geometryBuffer.GetResourceHandle().index,
      .viewIndex              = viewBuffer->GetResourceHandle().index,
      .visibleMeshletsIndex   = persistentVisibleMeshletIds->GetResourceHandle().index,
    };
    ctx.SetPushConstants(visbufferArguments);
    ctx.BindIndexBuffer(*instancedMeshletBuffer, 0, VK_INDEX_TYPE_UINT32);
    ctx.DrawIndexedIndirect(*meshletIndirectCommand, 0, 1, 0);
  }
  ctx.EndRendering();

  ctx.Barrier();

  // VSMs
  if (shadowUniforms.shadowMode == SHADOW_MODE_VIRTUAL_SHADOW_MAP)
  {
    const auto debugMarker = ctx.MakeScopedDebugMarker("Virtual Shadow Maps");
    TIME_SCOPE_GPU(StatGroup::eMainGpu, eVsm, commandBuffer);

    {
      TIME_SCOPE_GPU(StatGroup::eVsm, eVsmResetPageVisibility, commandBuffer);
      vsmContext.ResetPageVisibility(commandBuffer);
    }
    {
      TIME_SCOPE_GPU(StatGroup::eVsm, eVsmMarkVisiblePages, commandBuffer);
      vsmSun.MarkVisiblePages(commandBuffer, frame.gDepth.value(), globalUniformsBuffer.GetDeviceBuffer());
    }
    {
      TIME_SCOPE_GPU(StatGroup::eVsm, eVsmFreeNonVisiblePages, commandBuffer);
      vsmContext.FreeNonVisiblePages(commandBuffer);
    }
    {
      TIME_SCOPE_GPU(StatGroup::eVsm, eVsmAllocatePages, commandBuffer);
      vsmContext.AllocateRequestedPages(commandBuffer);
    }
    {
      TIME_SCOPE_GPU(StatGroup::eVsm, eVsmGenerateHpb, commandBuffer);
      vsmSun.GenerateBitmaskHzb(commandBuffer);
    }
    {
      TIME_SCOPE_GPU(StatGroup::eVsm, eVsmClearDirtyPages, commandBuffer);
      vsmContext.ClearDirtyPages(commandBuffer);
    }

    TIME_SCOPE_GPU(StatGroup::eVsm, eVsmRenderDirtyPages, commandBuffer);

    // Sun VSMs
    {
      auto clipmapsMarker = ctx.MakeScopedDebugMarker("Render Clipmaps");
      for (uint32_t i = 0; i < vsmSun.NumClipmaps(); i++)
      {
        TracyVkZone(tracyVkContext_, commandBuffer, "Render Clipmap")
        auto sunCurrentClipmapView = ViewParams{
          .oldViewProj              = vsmSun.GetProjections()[i] * vsmSun.GetViewMatrices()[i],
          .proj                     = vsmSun.GetProjections()[i],
          .view                     = vsmSun.GetViewMatrices()[i],
          .viewProj                 = vsmSun.GetProjections()[i] * vsmSun.GetViewMatrices()[i],
          .viewProjStableForVsmOnly = vsmSun.GetProjections()[i] * vsmSun.GetStableViewMatrix(),
          .cameraPos                = {}, // unused
          .viewport                 = {0.f, 0.f, vsmSun.GetExtent().width, vsmSun.GetExtent().height},
          .type                     = ViewType::VIRTUAL,
          .virtualTableIndex        = vsmSun.GetClipmapTableIndices()[i],
        };
        Math::MakeFrustumPlanes(sunCurrentClipmapView.viewProj, sunCurrentClipmapView.frustumPlanes);

        CullMeshletsForView(commandBuffer, sunCurrentClipmapView, transientVisibleMeshletIds.value(), "Cull Sun VSM Meshlets, View " + std::to_string(i));

        const auto vsmExtent = Fvog::Extent2D{Techniques::VirtualShadowMaps::maxExtent, Techniques::VirtualShadowMaps::maxExtent};
        ctx.Barrier();

#if VSM_USE_TEMP_ZBUFFER
        ctx.ImageBarrier(vsmTempDepthStencil.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
        auto vsmDepthAttachment = Fvog::RenderDepthStencilAttachment{
          .texture    = vsmTempDepthStencil.value().ImageView(),
          .loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .clearValue = {.depth = 1},
        };
#endif
        ctx.BeginRendering({
          .name = "Render Clipmap", .viewport = VkViewport{0, 0, (float)vsmExtent.width, (float)vsmExtent.height, 0, 1},
#if VSM_USE_TEMP_ZBUFFER
          .depthAttachment = vsmDepthAttachment,
#endif
        });

        ctx.BindGraphicsPipeline(vsmShadowPipeline.GetPipeline());

        auto pushConstants                       = vsmContext.GetPushConstants();
        pushConstants.meshletInstancesIndex      = meshletInstancesBuffer.GetResourceHandle().index;
        pushConstants.meshletDataIndex           = geometryBuffer.GetResourceHandle().index;
        pushConstants.meshletPrimitivesIndex     = geometryBuffer.GetResourceHandle().index;
        pushConstants.meshletVerticesIndex       = geometryBuffer.GetResourceHandle().index;
        pushConstants.meshletIndicesIndex        = geometryBuffer.GetResourceHandle().index;
        pushConstants.transformsIndex            = geometryBuffer.GetResourceHandle().index;
        pushConstants.globalUniformsIndex        = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index;
        pushConstants.viewIndex                  = viewBuffer->GetResourceHandle().index;
        pushConstants.materialsIndex             = geometryBuffer.GetResourceHandle().index;
        pushConstants.materialSamplerIndex       = materialSampler.GetResourceHandle().index;
        pushConstants.clipmapLod                 = vsmSun.GetClipmapTableIndices()[i];
        pushConstants.clipmapUniformsBufferIndex = vsmSun.clipmapUniformsBuffer_.GetResourceHandle().index;
        pushConstants.visibleMeshletsIndex       = transientVisibleMeshletIds->GetResourceHandle().index;

        ctx.BindIndexBuffer(*instancedMeshletBuffer, 0, VK_INDEX_TYPE_UINT32);

        ctx.SetPushConstants(pushConstants);
        ctx.DrawIndexedIndirect(*meshletIndirectCommand, 0, 1, 0);
        ctx.EndRendering();
      }
    }
  }

  // TODO: remove when descriptor indexing sync validation does not give false positives
  ctx.Barrier();
  
  ctx.ImageBarrier(*frame.gDepth, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

  if (generateHizBuffer)
  {
    TIME_SCOPE_GPU(StatGroup::eMainGpu, eHzb, commandBuffer);
    {
      auto marker = ctx.MakeScopedDebugMarker("HZB Build Pass", {.5f, .5f, 1.0f, 1.0f});
      ctx.SetPushConstants(HzbCopyPushConstants{
        .hzbIndex = frame.hzb->ImageView().GetStorageResourceHandle().index,
        .depthIndex = frame.gDepth->ImageView().GetSampledResourceHandle().index,
        .depthSamplerIndex = hzbSampler.GetResourceHandle().index,
      });

      ctx.BindComputePipeline(hzbCopyPipeline.GetPipeline());
      uint32_t hzbCurrentWidth = frame.hzb->GetCreateInfo().extent.width;
      uint32_t hzbCurrentHeight = frame.hzb->GetCreateInfo().extent.height;
      const uint32_t hzbLevels = frame.hzb->GetCreateInfo().mipLevels;
      ctx.Dispatch((hzbCurrentWidth + 15) / 16, (hzbCurrentHeight + 15) / 16, 1);

      // Sync val complains about WAR for colorLdrWindowRes in the next dispatch, even though it's definitely not accessed
      ctx.Barrier();

      ctx.BindComputePipeline(hzbReducePipeline.GetPipeline());
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
    TIME_SCOPE_GPU(StatGroup::eMainGpu, eResolveVisbuffer, commandBuffer);
    ctx.BindGraphicsPipeline(visbufferResolvePipeline.GetPipeline());
    
    auto pushConstants = VisbufferPushConstants{
      .globalUniformsIndex    = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .meshletInstancesIndex  = meshletInstancesBuffer.GetResourceHandle().index,
      .meshletDataIndex       = geometryBuffer.GetResourceHandle().index,
      .meshletPrimitivesIndex = geometryBuffer.GetResourceHandle().index,
      .meshletVerticesIndex   = geometryBuffer.GetResourceHandle().index,
      .meshletIndicesIndex    = geometryBuffer.GetResourceHandle().index,
      .transformsIndex        = geometryBuffer.GetResourceHandle().index,
      .materialsIndex         = geometryBuffer.GetResourceHandle().index,
      .visibleMeshletsIndex   = persistentVisibleMeshletIds->GetResourceHandle().index,
      .materialSamplerIndex   = materialSampler.GetResourceHandle().index,

      .visbufferIndex       = frame.visbuffer->ImageView().GetSampledResourceHandle().index,
    };

    ctx.SetPushConstants(pushConstants);
    ctx.Draw(3, 1, 0, 0);
  }

  ctx.EndRendering();

  // AO pass
  Fvog::Texture* aoTexture = &whiteTexture_;
  
  if (aoMethod_ == AoMethod::RAY_TRACED)
  {
    rayTracedAoParams_.frameNumber = 0;
    if (aoUsePerFrameRng)
    {
      rayTracedAoParams_.frameNumber = static_cast<uint32_t>(Fvog::GetDevice().frameNumber);
    }
    rayTracedAoParams_.tlas                     = &tlas.value();
    rayTracedAoParams_.inputDepth               = &frame.gDepth.value();
    rayTracedAoParams_.inputNormalAndFaceNormal = &frame.gNormalAndFaceNormal.value();
    rayTracedAoParams_.world_from_clip          = glm::inverse(globalUniforms.viewProj);
    rayTracedAoParams_.outputSize               = Fvog::Extent2D{renderInternalWidth, renderInternalHeight};
    aoTexture                                   = &rayTracedAo_->ComputeAO(commandBuffer, rayTracedAoParams_);
  }

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
    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
  };
  ctx.BeginRendering({
    .name = "Shading",
    .colorAttachments = {&shadingColorAttachment, 1},
  });
  {
    TIME_SCOPE_GPU(StatGroup::eMainGpu, eShadeOpaque, commandBuffer);

    // Certain VSM push constants are used by the shading pass
    auto vsmPushConstants = vsmContext.GetPushConstants();
    ctx.BindGraphicsPipeline(shadingPipeline.GetPipeline());
    ctx.SetPushConstants(ShadingPushConstants{
      .globalUniformsIndex  = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .shadingUniformsIndex = shadingUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .shadowUniformsIndex  = shadowUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
      .lightBufferIndex     = lightsBuffer.GetResourceHandle().index,

      .gAlbedoIndex              = frame.gAlbedo->ImageView().GetSampledResourceHandle().index,
      .gNormalAndFaceNormalIndex = frame.gNormalAndFaceNormal->ImageView().GetSampledResourceHandle().index,
      .gDepthIndex               = frame.gDepth->ImageView().GetSampledResourceHandle().index,
      .gSmoothVertexNormalIndex  = frame.gSmoothVertexNormal->ImageView().GetSampledResourceHandle().index,
      .gEmissionIndex            = frame.gEmission->ImageView().GetSampledResourceHandle().index,
      .gMetallicRoughnessAoIndex = frame.gMetallicRoughnessAo->ImageView().GetSampledResourceHandle().index,
      .ambientOcclusion          = aoTexture->ImageView().GetTexture2D(),

      .pageTablesIndex            = vsmPushConstants.pageTablesIndex,
      .physicalPagesIndex         = vsmPushConstants.physicalPagesIndex,
      .vsmBitmaskHzbIndex         = vsmPushConstants.vsmBitmaskHzbIndex,
      .vsmUniformsBufferIndex     = vsmPushConstants.vsmUniformsBufferIndex,
      .dirtyPageListBufferIndex   = vsmPushConstants.dirtyPageListBufferIndex,
      .clipmapUniformsBufferIndex = vsmSun.clipmapUniformsBuffer_.GetResourceHandle().index,
      .nearestSamplerIndex        = vsmPushConstants.nearestSamplerIndex,

      .physicalPagesOverdrawIndex = vsmPushConstants.physicalPagesOverdrawIndex,
      .debugLinesBuffer           = debugGpuLinesBuffer.value().GetBuffer(),
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
  colorAttachments.emplace_back(frame.gReactiveMask->ImageView(), VK_ATTACHMENT_LOAD_OP_CLEAR, Fvog::ClearColorValue{0.0f});

  ctx.ImageBarrierDiscard(*frame.gReactiveMask, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrier(*frame.gDepth, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrier(*frame.colorHdrRenderRes, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  if (!debugLines.empty())
  {
    if (!lineVertexBuffer || lineVertexBuffer->Size() < debugLines.size() * sizeof(Debug::Line))
    {
      lineVertexBuffer.emplace((uint32_t)debugLines.size(), "Debug Lines");
    }
    lineVertexBuffer->UpdateData(commandBuffer, debugLines);
  }

  ctx.BeginRendering({
    .name = "Debug Geometry",
    .colorAttachments = colorAttachments,
    .depthAttachment = debugDepthAttachment,
  });
  {
    TIME_SCOPE_GPU(StatGroup::eMainGpu, eDebugGeometry, commandBuffer);
    //  Lines
    if (!debugLines.empty())
    {
      ctx.BindGraphicsPipeline(debugLinesPipeline.GetPipeline());
      ctx.SetPushConstants(DebugLinesPushConstants{
        .vertexBufferIndex   = lineVertexBuffer->GetDeviceBuffer().GetResourceHandle().index,
        .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
        .useGpuVertexBuffer  = 0,
      });
      ctx.Draw(uint32_t(debugLines.size() * 2), 1, 0, 0);
    }

    // GPU Lines
    if (drawDebugLines)
    {
      ctx.BindGraphicsPipeline(debugLinesPipeline.GetPipeline());
      ctx.SetPushConstants(DebugLinesPushConstants{
        .vertexBufferIndex   = debugGpuLinesBuffer->GetResourceHandle().index,
        .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
        .useGpuVertexBuffer  = 1,
      });
      ctx.DrawIndirect(debugGpuLinesBuffer.value(), 0, 1, 0);
    }

    // GPU AABBs
    if (drawDebugAabbs)
    {
      ctx.BindGraphicsPipeline(debugAabbsPipeline.GetPipeline());
      ctx.SetPushConstants(DebugAabbArguments{
        .globalUniformsIndex = globalUniformsBuffer.GetDeviceBuffer().GetResourceHandle().index,
        .debugAabbBufferIndex = debugGpuAabbsBuffer->GetResourceHandle().index,
      });
      ctx.DrawIndirect(debugGpuAabbsBuffer.value(), 0, 1, 0);
    }

    // GPU Rects
    if (drawDebugRects)
    {
      ctx.BindGraphicsPipeline(debugRectsPipeline.GetPipeline());
      ctx.SetPushConstants(DebugRectArguments{
        .debugRectBufferIndex = debugGpuRectsBuffer->GetResourceHandle().index,
      });
      ctx.DrawIndirect(debugGpuRectsBuffer.value(), 0, 1, 0);
    }
  }
  ctx.EndRendering();

  {
    ctx.ImageBarrier(frame.colorHdrRenderRes.value(), VK_IMAGE_LAYOUT_GENERAL);
    TIME_SCOPE_GPU(StatGroup::eMainGpu, eAutoExposure, commandBuffer);
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
    TIME_SCOPE_GPU(StatGroup::eMainGpu, eFsr2, commandBuffer);
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
    ctx.ImageBarrierDiscard(*frame.colorHdrWindowRes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // This nonsense transition is needed for FSR2

    float jitterX{};
    float jitterY{};
    ffxFsr2GetJitterOffset(&jitterX, &jitterY, (int32_t)Fvog::GetDevice().frameNumber, ffxFsr2GetJitterPhaseCount(renderInternalWidth, renderOutputWidth));

    FfxFsr2DispatchDescription dispatchDesc{
      .commandList                = ffxGetCommandListVK(commandBuffer),
      .color                      = ffxGetTextureResourceVK(&fsr2Context,
        frame.colorHdrRenderRes->Image(),
        frame.colorHdrRenderRes->ImageView(),
        renderInternalWidth,
        renderInternalHeight,
        Fvog::detail::FormatToVk(frame.colorHdrRenderRes->GetCreateInfo().format)),
      .depth                      = ffxGetTextureResourceVK(&fsr2Context,
        frame.gDepth->Image(),
        frame.gDepth->ImageView(),
        renderInternalWidth,
        renderInternalHeight,
        Fvog::detail::FormatToVk(frame.gDepth->GetCreateInfo().format)),
      .motionVectors              = ffxGetTextureResourceVK(&fsr2Context,
        frame.gMotion->Image(),
        frame.gMotion->ImageView(),
        renderInternalWidth,
        renderInternalHeight,
        Fvog::detail::FormatToVk(frame.gMotion->GetCreateInfo().format)),
      .exposure                   = {},
      .reactive                   = ffxGetTextureResourceVK(&fsr2Context,
        frame.gReactiveMask->Image(),
        frame.gReactiveMask->ImageView(),
        renderInternalWidth,
        renderInternalHeight,
        Fvog::detail::FormatToVk(frame.gReactiveMask->GetCreateInfo().format)),
      .transparencyAndComposition = {},
      .output                     = ffxGetTextureResourceVK(&fsr2Context,
        frame.colorHdrWindowRes->Image(),
        frame.colorHdrWindowRes->ImageView(),
        renderOutputWidth,
        renderOutputHeight,
        Fvog::detail::FormatToVk(frame.colorHdrWindowRes->GetCreateInfo().format)),
      .jitterOffset               = {jitterX, jitterY},
      .motionVectorScale          = {float(renderInternalWidth), float(renderInternalHeight)},
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
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);
    *frame.colorHdrWindowRes->currentLayout = VK_IMAGE_LAYOUT_GENERAL;
  }
#endif

  if (bloomEnable)
  {
    TIME_SCOPE_GPU(StatGroup::eMainGpu, eBloom, commandBuffer);
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

    ctx.Barrier();
    ctx.ImageBarrierDiscard(*frame.colorLdrWindowRes, VK_IMAGE_LAYOUT_GENERAL);

    TIME_SCOPE_GPU(StatGroup::eMainGpu, eResolveImage, commandBuffer);

    ctx.BindComputePipeline(tonemapPipeline.GetPipeline());
    ctx.SetPushConstants(shared::TonemapArguments{
      .sceneColor = (fsr2Enable ? frame.colorHdrWindowRes.value() : frame.colorHdrRenderRes.value()).ImageView().GetTexture2D(),
      .noise = noiseTexture->ImageView().GetTexture2D(),
      .nearestSampler = nearestSampler,
      .linearClampSampler = linearClampSampler,
      .exposure = exposureBuffer,
      .tonemapUniforms = tonemapUniformBuffer.GetDeviceBuffer(),
      .outputImage = frame.colorLdrWindowRes->ImageView().GetImage2D(),
      .tonyMcMapface = tonyMcMapfaceLut.ImageView().GetTexture3D(),
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
        .imageView = swapchainImageViews_[swapchainImageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      }),
    }));

    vkCmdSetViewport(commandBuffer, 0, 1, Address(VkViewport{0, 0, (float)renderOutputWidth, (float)renderOutputHeight, 0, 1}));
    vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
    ctx.BindGraphicsPipeline(debugTexturePipeline.GetPipeline());
    auto pushConstants = DebugTextureArguments{
      .textureIndex = frame.colorLdrWindowRes->ImageView().GetSampledResourceHandle().index,
      .samplerIndex = nearestSampler.GetResourceHandle().index,
    };

    ctx.SetPushConstants(pushConstants);
    ctx.Draw(3, 1, 0, 0);

    vkCmdEndRendering(commandBuffer);
  }
  
//  ctx.ImageBarrierDiscard(*testRayTracingOutput, VK_IMAGE_LAYOUT_GENERAL);
//  ctx.BindRayTracingPipeline(testRayTracingPipeline);
//  ctx.SetPushConstants(testRayTracingOutput->ImageView().GetStorageResourceHandle().index);
//  ctx.TraceRays(testRayTracingOutput->GetCreateInfo().extent.width, testRayTracingOutput->GetCreateInfo().extent.height, 1);

  // swapchainImages_[swapchainImageIndex] needs to be COLOR_ATTACHMENT_OPTIMAL after this function returns
  ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void FrogRenderer2::OnPathDrop([[maybe_unused]] std::span<const char*> paths)
{
  ZoneScoped;
  for (const auto& path : paths)
  {
    scene.Import(*this, Utility::LoadModelFromFile(path, glm::identity<glm::mat4>()));
  }
}

Render::MeshGeometryID FrogRenderer2::RegisterMeshGeometry(MeshGeometryInfo meshGeometry)
{
  ZoneScoped;
  totalMeshlets += meshGeometry.meshlets.size();
  totalVertices += meshGeometry.vertices.size();
  totalRemappedIndices += meshGeometry.remappedIndices.size();
  totalOriginalIndices += meshGeometry.originalIndices.size();
  totalPrimitives += meshGeometry.primitives.size();

  auto verticesAlloc   = geometryBuffer.Allocate(std::span(meshGeometry.vertices).size_bytes(), sizeof(Render::Vertex));
  auto indicesAlloc    = geometryBuffer.Allocate(std::span(meshGeometry.remappedIndices).size_bytes(), sizeof(Render::index_t));
  auto primitivesAlloc = geometryBuffer.Allocate(std::span(meshGeometry.primitives).size_bytes(), sizeof(Render::primitive_t));
  auto meshletAlloc    = geometryBuffer.Allocate(std::span(meshGeometry.meshlets).size_bytes(), sizeof(Render::Meshlet));
  auto originalIndicesAlloc = geometryBuffer.Allocate(std::span(meshGeometry.originalIndices).size_bytes(), sizeof(Render::index_t));

  // Massage meshlets before uploading
  const auto baseVertex    = verticesAlloc.GetOffset() / sizeof(Render::Vertex);
  const auto baseIndex     = indicesAlloc.GetOffset() / sizeof(Render::index_t);
  const auto basePrimitive = primitivesAlloc.GetOffset() / sizeof(Render::primitive_t);
  for (auto& meshlet : meshGeometry.meshlets)
  {
    meshlet.vertexOffset += (uint32_t)baseVertex;
    meshlet.indexOffset += (uint32_t)baseIndex;
    meshlet.primitiveOffset += (uint32_t)basePrimitive;
  }

  std::memcpy(geometryBuffer.GetMappedMemory() + meshletAlloc.GetOffset(), meshGeometry.meshlets.data(), meshletAlloc.GetDataSize());
  std::memcpy(geometryBuffer.GetMappedMemory() + verticesAlloc.GetOffset(), meshGeometry.vertices.data(), verticesAlloc.GetDataSize());
  std::memcpy(geometryBuffer.GetMappedMemory() + indicesAlloc.GetOffset(), meshGeometry.remappedIndices.data(), indicesAlloc.GetDataSize());
  std::memcpy(geometryBuffer.GetMappedMemory() + primitivesAlloc.GetOffset(), meshGeometry.primitives.data(), primitivesAlloc.GetDataSize());
  std::memcpy(geometryBuffer.GetMappedMemory() + originalIndicesAlloc.GetOffset(), meshGeometry.originalIndices.data(), originalIndicesAlloc.GetDataSize());

  [[maybe_unused]] auto verticesOffset        = verticesAlloc.GetOffset();
  [[maybe_unused]] auto originalIndicesOffset = originalIndicesAlloc.GetOffset();

  auto myId = nextId++;
  [[maybe_unused]] const auto& meshGeometryAlloc = meshGeometryAllocations.emplace(myId,
    MeshGeometryAllocs{
      .meshletsAlloc   = std::move(meshletAlloc),
      .verticesAlloc   = std::move(verticesAlloc),
      .indicesAlloc    = std::move(indicesAlloc),
      .primitivesAlloc = std::move(primitivesAlloc),
      .originalIndicesAlloc = std::move(originalIndicesAlloc),
  });

  if (Fvog::GetDevice().supportsRayTracing)
  {
    meshGeometryAlloc.first->second.blas = Fvog::Blas(Fvog::BlasCreateInfo{
      .geoemtryFlags = Fvog::AccelerationStructureGeometryFlag::OPAQUE,
      .buildFlags    = Fvog::AccelerationStructureBuildFlag::FAST_TRACE | Fvog::AccelerationStructureBuildFlag::ALLOW_DATA_ACCESS | Fvog::AccelerationStructureBuildFlag::ALLOW_COMPACTION,
      .vertexFormat  = VK_FORMAT_R32G32B32_SFLOAT,
      .vertexBuffer  = geometryBuffer.GetBuffer().GetDeviceAddress() + verticesOffset,
      .indexBuffer   = geometryBuffer.GetBuffer().GetDeviceAddress() + originalIndicesOffset,
      .vertexStride  = sizeof(Render::Vertex),
      .numVertices   = (uint32_t)meshGeometry.vertices.size(),
      .indexType     = VK_INDEX_TYPE_UINT32,
      .numIndices    = (uint32_t)meshGeometry.originalIndices.size(),
    }),
    totalBlasMemory += meshGeometryAlloc.first->second.blas->GetBuffer().SizeBytes();
  }

  return {myId};
}

void FrogRenderer2::UnregisterMeshGeometry(Render::MeshGeometryID meshGeometry)
{
  ZoneScoped;
  auto it = meshGeometryAllocations.find(meshGeometry.id);
  assert(it != meshGeometryAllocations.end());
  totalMeshlets -= it->second.meshletsAlloc.GetDataSize() / sizeof(Render::Meshlet);
  totalVertices -= it->second.verticesAlloc.GetDataSize() / sizeof(Render::Vertex);
  totalRemappedIndices -= it->second.indicesAlloc.GetDataSize() / sizeof(Render::index_t);
  totalOriginalIndices -= it->second.originalIndicesAlloc.GetDataSize() / sizeof(Render::index_t);
  totalPrimitives -= it->second.primitivesAlloc.GetDataSize() / sizeof(Render::primitive_t);
  if (Fvog::GetDevice().supportsRayTracing)
  {
    totalBlasMemory -= it->second.blas.value().GetBuffer().SizeBytes();
  }
  meshGeometryAllocations.erase(it);
}

Render::MeshID FrogRenderer2::SpawnMesh(Render::MeshGeometryID meshGeometry)
{
  ZoneScoped;
  auto myId = nextId++;
  auto& meshAllocation = meshAllocations[myId];
  meshAllocation.instanceAlloc = geometryBuffer.Allocate(sizeof(Render::ObjectUniforms), sizeof(Render::ObjectUniforms));
  meshAllocation.geometryId    = meshGeometry; 
  spawnedMeshes.emplace_back(Render::MeshID{myId}, meshGeometry);
  return {myId};
}

void FrogRenderer2::DeleteMesh(Render::MeshID mesh)
{
  ZoneScoped;
  deletedMeshes.emplace_back(mesh.id);
}

// Having the data payload in the alloc function is kinda quirky and inconsistent, but I'll keep it for now.
Render::LightID FrogRenderer2::SpawnLight(const GpuLight& lightData)
{
  ZoneScoped;
  auto myId = nextId++;
  spawnedLights.emplace_back(myId, lightData);
  return {myId};
}

void FrogRenderer2::DeleteLight(Render::LightID light)
{
  ZoneScoped;
  deletedLights.emplace_back(light.id);
}

Render::MaterialID FrogRenderer2::RegisterMaterial(Render::Material&& material)
{
  ZoneScoped;
  auto materialAlloc = geometryBuffer.Allocate(sizeof(Render::GpuMaterial), sizeof(Render::GpuMaterial));
  std::memcpy(geometryBuffer.GetMappedMemory() + materialAlloc.GetOffset(), &material.gpuMaterial, sizeof(Render::GpuMaterial));

  auto myId = nextId++;
  materialAllocations.emplace(myId, MaterialAlloc{.materialAlloc = std::move(materialAlloc), .material = std::move(material)});
  return {myId};
}

void FrogRenderer2::UnregisterMaterial(Render::MaterialID material)
{
  ZoneScoped;
  materialAllocations.erase(material.id);
}

void FrogRenderer2::UpdateMesh(Render::MeshID mesh, const Render::ObjectUniforms& uniforms)
{
  ZoneScoped;
  modifiedMeshUniforms[mesh.id] = uniforms;
}

void FrogRenderer2::UpdateLight(Render::LightID light, const GpuLight& lightData)
{
  ZoneScoped;
  modifiedLights[light.id] = lightData;
}

void FrogRenderer2::UpdateMaterial(Render::MaterialID material, const Render::GpuMaterial& materialData)
{
  ZoneScoped;
  modifiedMaterials[material.id] = materialData;
}

uint32_t FrogRenderer2::GetMaterialGpuIndex(Render::MaterialID material)
{
  ZoneScoped;
  const auto& alloc = materialAllocations.at(material.id);
  return static_cast<uint32_t>(alloc.materialAlloc.GetOffset() / sizeof(Render::GpuMaterial));
}

Render::Material& FrogRenderer2::GetMaterial(Render::MaterialID material)
{
  return materialAllocations.at(material.id).material;
}

VkDeviceAddress FrogRenderer2::GetVertexBufferPointerFromMesh(Render::MeshID meshId)
{
  const auto& meshGeometryAllocs = meshGeometryAllocations.at(meshAllocations.at(meshId.id).geometryId->id);
  return geometryBuffer.GetBuffer().GetDeviceAddress() + meshGeometryAllocs.verticesAlloc.GetOffset();
}

VkDeviceAddress FrogRenderer2::GetOriginalIndexBufferPointerFromMesh(Render::MeshID meshId)
{
  const auto& meshGeometryAllocs = meshGeometryAllocations.at(meshAllocations.at(meshId.id).geometryId->id);
  return geometryBuffer.GetBuffer().GetDeviceAddress() + meshGeometryAllocs.originalIndicesAlloc.GetOffset();
}

void FrogRenderer2::FlushUpdatedSceneData(VkCommandBuffer commandBuffer)
{
  ZoneScoped;
  auto ctx = Fvog::Context(commandBuffer);

  ctx.Barrier();

  auto marker = ctx.MakeScopedDebugMarker("Flush updated scene data");

  // Deleted meshes
  for (auto id : deletedMeshes)
  {
    auto it = meshAllocations.find(id);
    meshletInstancesBuffer.Free(it->second.meshletInstancesAlloc.value(), commandBuffer);
    meshAllocations.erase(it);
  }

  struct MeshletInstancesUpload
  {
    size_t srcOffset;
    size_t dstOffset;
    size_t sizeBytes;
  };

  auto meshletInstancesUploads = std::vector<MeshletInstancesUpload>();
  auto meshletInstances        = std::vector<Render::MeshletInstance>();
  meshletInstancesUploads.reserve(spawnedMeshes.size());

  // Spawned meshes
  for (auto& [meshId, meshGeometry] : spawnedMeshes)
  {
    const auto& meshletsAlloc = meshGeometryAllocations.at(meshGeometry.id).meshletsAlloc;

    const auto meshletInstanceCount = meshletsAlloc.GetDataSize() / sizeof(Render::Meshlet);

    auto& partialMeshAlloc = meshAllocations.at(meshId.id);

    // Spawn a set of meshlet instances referring to the mesh's meshlets, with the correct offsets.
    auto baseMeshletIndex = meshletsAlloc.GetOffset() / sizeof(Render::Meshlet);
    auto instanceIndex    = partialMeshAlloc.instanceAlloc.value().GetOffset() / sizeof(Render::ObjectUniforms);
    auto srcOffset        = meshletInstances.size() * sizeof(Render::MeshletInstance);
    for (size_t i = 0; i < meshletInstanceCount; i++)
    {
      meshletInstances.emplace_back(uint32_t(baseMeshletIndex + i), (uint32_t)instanceIndex);
    }

    const auto meshletInstancesAlloc = meshletInstancesBuffer.Allocate(meshletInstanceCount * sizeof(Render::MeshletInstance));
    meshletInstancesUploads.emplace_back(srcOffset, meshletInstancesAlloc.offset, meshletInstancesAlloc.size);

    partialMeshAlloc.meshletInstancesAlloc = meshletInstancesAlloc;
    if (Fvog::GetDevice().supportsRayTracing)
    {
      auto tlasInstance = Fvog::TlasInstance{
        .transform                = {},
        .instanceCustomIndex      = uint32_t(instanceIndex),
        .mask                     = 0xFF,
        .shaderBindingTableOffset = 0,
        .flags                    = {},
        .blasAddress              = meshGeometryAllocations.at(meshGeometry.id).blas.value().GetAddress(),
      };
      partialMeshAlloc.tlasInstance = tlasInstance;
    }
  }
  
  // Upload meshlet instances of spawned meshes.
  // TODO: This should be a scatter-write compute shader
  if (!meshletInstancesUploads.empty())
  {
    auto uploadBuffer = Fvog::TypedBuffer<Render::MeshletInstance>({
        .count = (uint32_t)meshletInstances.size(),
        .flag  = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE | Fvog::BufferFlagThingy::NO_DESCRIPTOR,
      },
      "Meshlet Upload Staging Buffer");

    std::memcpy(uploadBuffer.GetMappedMemory(), meshletInstances.data(), meshletInstances.size() * sizeof(Render::MeshletInstance));

    for (auto [srcOffset, dstOffset, size] : meshletInstancesUploads)
    {
      ctx.CopyBuffer(uploadBuffer, meshletInstancesBuffer.GetBuffer(), {
        .srcOffset = srcOffset,
        .dstOffset = dstOffset,
        .size = size,
      });
    }
  }

  // Spawn lights
  for (const auto& [id, gpuLight] : spawnedLights)
  {
    const auto lightAlloc = lightsBuffer.Allocate(sizeof(GpuLight));
    lightAllocations.emplace(id, LightAlloc{.lightAlloc = lightAlloc});
    ctx.TeenyBufferUpdate(lightsBuffer.GetBuffer(), gpuLight, lightAlloc.offset);
  }

  // Delete lights
  for (auto id : deletedLights)
  {
    auto it = lightAllocations.find(id);
    lightsBuffer.Free(it->second.lightAlloc, commandBuffer);
    lightAllocations.erase(it);
  }

  ctx.Barrier();

  // Update mesh uniforms
  for (const auto& [id, uniforms] : modifiedMeshUniforms)
  {
    const auto offset = meshAllocations.at(id).instanceAlloc.value().GetOffset();
    assert(offset % sizeof(uniforms) == 0);
    ctx.TeenyBufferUpdate(geometryBuffer.GetBuffer(), uniforms, offset);

    if (Fvog::GetDevice().supportsRayTracing)
    {
      // Make memory layout match VkTransformMatrixKHR::matrix[3][4] (3 rows, 4 columns, row-major)
      // Must be here to ensure meshAllocations[mesh.id] exists
      auto transformAffine = glm::transpose(glm::mat4x3(uniforms.modelCurrent));
      std::memcpy(&meshAllocations.at(id).tlasInstance.value().transform, &transformAffine, sizeof(VkTransformMatrixKHR));
    }
  }

  // Update lights
  for (const auto& [id, light] : modifiedLights)
  {
    const auto offset = lightAllocations.at(id).lightAlloc.offset;
    assert(offset % sizeof(light) == 0);
    ctx.TeenyBufferUpdate(lightsBuffer.GetBuffer(), light, offset);
  }

  // Update materials
  for (const auto& [id, material] : modifiedMaterials)
  {
    const auto offset = materialAllocations.at(id).materialAlloc.GetOffset();
    assert(offset % sizeof(material) == 0);
    ctx.TeenyBufferUpdate(geometryBuffer.GetBuffer(), material, offset);
  }

  ctx.Barrier();
  modifiedMeshUniforms.clear();
  modifiedLights.clear();
  modifiedMaterials.clear();
  deletedMeshes.clear();
  spawnedMeshes.clear();
  deletedLights.clear();
  spawnedLights.clear();
}
