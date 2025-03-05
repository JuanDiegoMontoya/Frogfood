#include "VoxelRenderer.h"

#include "Assets.h"
#include "MathUtilities.h"
#include "PipelineManager.h"
#include "imgui.h"
#include "Fvog/Device.h"
#include "Fvog/Rendering2.h"
#include "Fvog/detail/Common.h"
#include "shaders/Config.shared.h"
#include "Reflection.h"

#include "Physics/Physics.h" // TODO: remove
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Physics/PhysicsUtils.h"
#ifdef JPH_DEBUG_RENDERER
#include "Physics/DebugRenderer.h"
#endif

#include "volk.h"
#include "Fvog/detail/ApiToEnum2.h"

#include "tiny_obj_loader.h"
#include "tracy/Tracy.hpp"
#include "GLFW/glfw3.h" // TODO: remove
#include "stb_image.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "entt/meta/meta.hpp"
#include "entt/meta/factory.hpp"
#include "entt/meta/container.hpp"

#include "tracy/TracyVulkan.hpp"

#include <memory>
#include <numeric>
#include <type_traits>
#include <future>
#include <atomic>

namespace
{
  using index_t = uint32_t;

  struct Vertex
  {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec3 color{};
  };

  enum class MaterialFlagBit
  {
    HAS_BASE_COLOR_TEXTURE       = 1 << 0,
    HAS_EMISSION_TEXTURE         = 1 << 1,
    RANDOMIZE_TEXCOORDS_ROTATION = 1 << 2,
    IS_INVISIBLE                 = 1 << 3,
  };
  FVOG_DECLARE_FLAG_TYPE(VoxelMaterialFlags, MaterialFlagBit, uint32_t);

  struct GpuVoxelMaterial
  {
    VoxelMaterialFlags materialFlags;
    shared::Texture2D baseColorTexture;
    glm::vec3 baseColorFactor;
    shared::Texture2D emissionTexture;
    glm::vec3 emissionFactor;
  };

  struct GpuMesh
  {
    std::optional<Fvog::TypedBuffer<Vertex>> vertexBuffer;
    std::optional<Fvog::TypedBuffer<index_t>> indexBuffer;
    std::vector<Vertex> vertices;
    std::vector<index_t> indices;
  };

  GpuMesh LoadObjFile(const std::filesystem::path& path)
  {
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path.string()))
    {
      //std::cout << "TinyObjReader error: " << reader.Error() << '\n';
      throw std::runtime_error("Failed to parse obj");
    }

    if (!reader.Warning().empty())
    {
      //std::cout << "TinyObjReader warning: " << reader.Warning() << '\n';
    }

    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();
    // auto& materials = reader.GetMaterials();

    auto mesh = GpuMesh{};

    // Loop over shapes
    for (const auto& shape : shapes)
    {
      // Loop over faces(polygon)
      size_t index_offset = 0;
      for (const auto& fv : shape.mesh.num_face_vertices)
      {
        // Loop over vertices in the face.
        for (size_t v = 0; v < fv; v++)
        {
          auto vertex = Vertex{};

          // access to vertex
          tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
          tinyobj::real_t vx   = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
          tinyobj::real_t vy   = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
          tinyobj::real_t vz   = attrib.vertices[3 * size_t(idx.vertex_index) + 2];

          vertex.position = {vx, vy, vz};

          // Check if `normal_index` is zero or positive. negative = no normal data
          if (idx.normal_index >= 0)
          {
            tinyobj::real_t nx = attrib.normals[3 * size_t(idx.normal_index) + 0];
            tinyobj::real_t ny = attrib.normals[3 * size_t(idx.normal_index) + 1];
            tinyobj::real_t nz = attrib.normals[3 * size_t(idx.normal_index) + 2];
            vertex.normal      = {nx, ny, nz};
          }

          //// Check if `texcoord_index` is zero or positive. negative = no texcoord data
          // if (idx.texcoord_index >= 0)
          //{
          //   tinyobj::real_t tx = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
          //   tinyobj::real_t ty = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
          //   vertex.texcoord = { tx, ty };
          // }

          // Optional: vertex colors
          tinyobj::real_t red   = attrib.colors[3 * size_t(idx.vertex_index) + 0];
          tinyobj::real_t green = attrib.colors[3 * size_t(idx.vertex_index) + 1];
          tinyobj::real_t blue  = attrib.colors[3 * size_t(idx.vertex_index) + 2];
          vertex.color          = {red, green, blue};

          mesh.vertices.push_back(vertex);
        }
        index_offset += fv;
      }
    }

    mesh.indices = std::vector<index_t>(mesh.vertices.size());
    std::iota(mesh.indices.begin(), mesh.indices.end(), 0);
    
    mesh.indexBuffer.emplace(Fvog::TypedBufferCreateInfo{.count = (uint32_t)mesh.indices.size(), .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE});
    mesh.vertexBuffer.emplace(Fvog::TypedBufferCreateInfo{.count = (uint32_t)mesh.vertices.size(), .flag = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE});
    memcpy(mesh.indexBuffer->GetMappedMemory(), mesh.indices.data(), mesh.indices.size() * sizeof(index_t));
    memcpy(mesh.vertexBuffer->GetMappedMemory(), mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));

    return mesh;
  }

  [[nodiscard]] Fvog::Texture LoadImageFile(const std::filesystem::path& path)
  {
    stbi_set_flip_vertically_on_load(1);
    int x            = 0;
    int y            = 0;
    const auto pixels = stbi_load((GetTextureDirectory() / path).string().c_str(), &x, &y, nullptr, 4);
    assert(pixels);
    auto texture = Fvog::CreateTexture2D({static_cast<uint32_t>(x), static_cast<uint32_t>(y)}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::READ_ONLY, path.string());
    texture.UpdateImageSLOW({
      .extent = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
      .data   = pixels,
    });
    stbi_image_free(pixels);

    return texture;
  }

  FVOG_DECLARE_ARGUMENTS(DebugLinesPushConstants)
  {
    FVOG_UINT32 vertexBufferIndex;
    FVOG_UINT32 globalUniformsIndex;
    FVOG_UINT32 useGpuVertexBuffer;
  };

  FVOG_DECLARE_ARGUMENTS(BillboardPushConstants)
  {
    FVOG_UINT32 billboardsIndex;
    FVOG_UINT32 globalUniformsIndex;
    FVOG_VEC3 cameraRight;
    FVOG_VEC3 cameraUp;
    shared::Sampler texSampler;
  };

  std::unordered_map<std::string, GpuMesh> g_meshes;
} // namespace

VoxelRenderer::VoxelRenderer(PlayerHead* head, World&) : head_(head)
{
  ZoneScoped;

  g_meshes.emplace("frog", LoadObjFile(GetAssetDirectory() / "models/frog.obj"));
  g_meshes.emplace("ar15", LoadObjFile(GetAssetDirectory() / "models/ar15.obj"));
  g_meshes.emplace("tracer", LoadObjFile(GetAssetDirectory() / "models/tracer.obj"));
  g_meshes.emplace("cube", LoadObjFile(GetAssetDirectory() / "models/cube.obj"));
  g_meshes.emplace("spear", LoadObjFile(GetAssetDirectory() / "models/spear.obj"));
  g_meshes.emplace("pickaxe", LoadObjFile(GetAssetDirectory() / "models/pickaxe.obj"));
  g_meshes.emplace("axe", LoadObjFile(GetAssetDirectory() / "models/axe.obj"));
  g_meshes.emplace("torch", LoadObjFile(GetAssetDirectory() / "models/torch.obj"));

  head_->renderCallback_ = [this](float dt, World& world, VkCommandBuffer cmd, uint32_t swapchainImageIndex) { OnRender(dt, world, cmd, swapchainImageIndex); };
  head_->framebufferResizeCallback_ = [this](uint32_t newWidth, uint32_t newHeight) { OnFramebufferResize(newWidth, newHeight); };
  head_->guiCallback_ = [this](DeltaTime dt, World& world, VkCommandBuffer cmd) { OnGui(dt, world, cmd); };

  testPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Render voxels",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "FullScreenTri.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "voxels/SimpleRayTracer.frag.glsl",
      },
    .state =
      {
        .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
        .depthState         = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = FVOG_COMPARE_OP_NEARER_OR_EQUAL},
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneIlluminanceFormat}},
            .depthAttachmentFormat = Frame::sceneDepthFormat,
          },
      },
  });
  
  meshPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Render meshes",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "voxels/Forward.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "voxels/Forward.frag.glsl",
      },
    .state =
      {
        .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
        .depthState         = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = FVOG_COMPARE_OP_NEARER},
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneIlluminanceFormat}},
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
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
        .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {Fvog::detail::VkToFormat(head_->swapchainFormat_.format)},
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
        .rasterizationState =
          {
            .cullMode                = VK_CULL_MODE_NONE,
            .depthBiasEnable         = true,
            .depthBiasConstantFactor = 1,
            .lineWidth               = 2,
          },
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = true,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneIlluminanceFormat}},
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
      },
  });

  billboardsPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Billboards",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "Billboard.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "Billboard.frag.glsl",
      },
    .state =
      {
        .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
        .rasterizationState =
          {
            .cullMode = VK_CULL_MODE_NONE,
          },
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = false,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneIlluminanceFormat}},
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
      },
  });

  billboardSpritesPipeline = GetPipelineManager().EnqueueCompileGraphicsPipeline({
    .name = "Billboard Sprites",
    .vertexModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::VERTEX_SHADER,
        .path  = GetShaderDirectory() / "BillboardSprite.vert.glsl",
      },
    .fragmentModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::FRAGMENT_SHADER,
        .path  = GetShaderDirectory() / "BillboardSprite.frag.glsl",
      },
    .state =
      {
        .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
        .rasterizationState =
          {
            .cullMode = VK_CULL_MODE_NONE,
          },
        .depthState =
          {
            .depthTestEnable  = true,
            .depthWriteEnable = false,
            .depthCompareOp   = FVOG_COMPARE_OP_NEARER,
          },
        .renderTargetFormats =
          {
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat, Frame::sceneIlluminanceFormat}},
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
      },
  });

  noiseTexture = LoadImageFile("bluenoise256.png");

  OnFramebufferResize(head_->windowFramebufferWidth, head_->windowFramebufferHeight);
}

VoxelRenderer::~VoxelRenderer()
{
  ZoneScoped;
  g_meshes.clear();
  vkDeviceWaitIdle(Fvog::GetDevice().device_);

  Fvog::GetDevice().FreeUnusedResources();

//#if FROGRENDER_FSR2_ENABLE
//  if (!fsr2FirstInit)
//  {
//    ffxFsr2ContextDestroy(&fsr2Context);
//  }
//#endif
}

void VoxelRenderer::CreateRenderingMaterials(std::span<const std::unique_ptr<BlockDefinition>> blockDefinitions)
{
  auto voxelMaterials = std::vector<GpuVoxelMaterial>();

  // Translate block definitions to GPU materials, then upload.
  for (const auto& def : blockDefinitions)
  {
    const auto& desc = def->GetMaterialDesc();

    auto gpuMat = GpuVoxelMaterial{};
    gpuMat.baseColorFactor = desc.baseColorFactor;
    gpuMat.emissionFactor  = desc.emissionFactor;
    if (desc.randomizeTexcoordRotation)
    {
      gpuMat.materialFlags |= MaterialFlagBit::RANDOMIZE_TEXCOORDS_ROTATION;
    }
    if (desc.baseColorTexture)
    {
      gpuMat.materialFlags |= MaterialFlagBit::HAS_BASE_COLOR_TEXTURE;
      gpuMat.baseColorTexture = GetOrEmplaceCachedTexture(*desc.baseColorTexture).ImageView().GetTexture2D();
    }
    if (desc.emissionTexture)
    {
      gpuMat.materialFlags |= MaterialFlagBit::HAS_EMISSION_TEXTURE;
      gpuMat.emissionTexture = GetOrEmplaceCachedTexture(*desc.emissionTexture).ImageView().GetTexture2D();
    }
    if (desc.isInvisible)
    {
      gpuMat.materialFlags |= MaterialFlagBit::IS_INVISIBLE;
    }

    voxelMaterials.emplace_back(gpuMat);
  }

  voxelMaterialBuffer = Fvog::Buffer({.size = voxelMaterials.size() * sizeof(GpuVoxelMaterial), .flag = Fvog::BufferFlagThingy::NONE}, "Voxel Material Buffer");
  Fvog::GetDevice().ImmediateSubmit([&](VkCommandBuffer cmd) { voxelMaterialBuffer->UpdateDataExpensive(cmd, std::span(voxelMaterials)); });
}

void VoxelRenderer::OnFramebufferResize(uint32_t newWidth, uint32_t newHeight)
{
  ZoneScoped;

  const auto extent      = VkExtent2D{newWidth, newHeight};
  frame.sceneAlbedo      = Fvog::CreateTexture2D(extent, Frame::sceneAlbedoFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene albedo");
  frame.sceneNormal      = Fvog::CreateTexture2D(extent, Frame::sceneNormalFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene normal");
  frame.sceneRadiance    = Fvog::CreateTexture2D(extent, Frame::sceneIlluminanceFormat, Fvog::TextureUsage::GENERAL, "Scene radiance");
  frame.sceneIlluminance = Fvog::CreateTexture2D(extent, Frame::sceneIlluminanceFormat, Fvog::TextureUsage::GENERAL, "Scene illuminance");
  frame.sceneIlluminancePingPong = Fvog::CreateTexture2D(extent, Frame::sceneIlluminanceFormat, Fvog::TextureUsage::GENERAL, "Scene illuminance 2");
  frame.sceneDepth       = Fvog::CreateTexture2D(extent, Frame::sceneDepthFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene depth");
  frame.sceneColor       = Fvog::CreateTexture2D(extent, Frame::sceneColorFormat, Fvog::TextureUsage::GENERAL, "Scene color");
}

void VoxelRenderer::OnRender([[maybe_unused]] double dt, World& world, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
{
  ZoneScoped;
  TracyVkZone(head_->tracyVkContext_, commandBuffer, "OnRender");

  if (head_->shouldResizeNextFrame)
  {
    OnFramebufferResize(head_->windowFramebufferWidth, head_->windowFramebufferHeight);
    head_->shouldResizeNextFrame = false;
  }

  auto ctx = Fvog::Context(commandBuffer);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);

  if (Fvog::GetDevice().supportsRayTracing)
  {
    vkCmdBindDescriptorSets(commandBuffer,
      VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
      Fvog::GetDevice().defaultPipelineLayout,
      0,
      1,
      &Fvog::GetDevice().descriptorSet_,
      0,
      nullptr);
  }

  if (world.GetRegistry().ctx().get<GameState>() == GameState::GAME)
  {
    ctx.Barrier();
    RenderGame(dt, world, commandBuffer);
    ctx.Barrier();
  }

  ctx.ImageBarrier(head_->swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  const auto nearestSampler = Fvog::Sampler({
    .magFilter     = VK_FILTER_NEAREST,
    .minFilter     = VK_FILTER_NEAREST,
    .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .maxAnisotropy = 0,
  });

  //const auto renderArea = VkRect2D{.offset = {}, .extent = {renderOutputWidth, renderOutputHeight}};
  const auto renderArea = VkRect2D{.offset = {}, .extent = {head_->windowFramebufferWidth, head_->windowFramebufferHeight}};
  vkCmdBeginRendering(commandBuffer,
    Fvog::detail::Address(VkRenderingInfo{
      .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea           = renderArea,
      .layerCount           = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments    = Fvog::detail::Address(VkRenderingAttachmentInfo{
           .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
           .imageView   = head_->swapchainImageViews_[swapchainImageIndex],
           .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
           .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
           .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
           .clearValue  = {.color = VkClearColorValue{.float32 = {0, 0, 1, 1}}},
      }),
    }));

  //vkCmdSetViewport(commandBuffer, 0, 1, Fvog::detail::Address(VkViewport{0, 0, (float)renderOutputWidth, (float)renderOutputHeight, 0, 1}));
  vkCmdSetViewport(commandBuffer, 0, 1, Fvog::detail::Address(VkViewport{0, 0, (float)head_->windowFramebufferWidth, (float)head_->windowFramebufferHeight, 0, 1}));
  vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
  ctx.BindGraphicsPipeline(debugTexturePipeline.GetPipeline());
  auto pushConstants = Temp::DebugTextureArguments{
    .textureIndex = frame.sceneColor->ImageView().GetSampledResourceHandle().index,
    .samplerIndex = nearestSampler.GetResourceHandle().index,
  };

  ctx.SetPushConstants(pushConstants);
  ctx.Draw(3, 1, 0, 0);

  vkCmdEndRendering(commandBuffer);

  ctx.ImageBarrier(head_->swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void VoxelRenderer::RenderGame([[maybe_unused]] double dt, World& world, VkCommandBuffer commandBuffer)
{
  auto ctx = Fvog::Context(commandBuffer);

  if (world.GetRegistry().ctx().contains<TwoLevelGrid>())
  {
    auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
    grid.buffer.FlushWritesToGPU(commandBuffer);
  }

  ctx.Barrier();

  auto viewMat  = glm::mat4(1);
  auto position = glm::vec3();
  for (auto&& [entity, inputLook, transform] : world.GetRegistry().view<const InputLookState, const GlobalTransform, LocalPlayer>().each())
  {
    position = transform.position;
    // Flip z axis to correspond with Vulkan's NDC space.
    auto rotationQuat = transform.rotation;
    if (const auto* renderTransform = world.GetRegistry().try_get<RenderTransform>(entity))
    {
      // Because the player has its own variable delta pitch and yaw updates, we only care about smoothing positions here
      position = renderTransform->transform.position;
    }
    auto rotation = glm::mat4_cast(glm::inverse(rotationQuat));
    viewMat       = glm::translate(rotation, -position);
  }

  const auto view_from_world = viewMat;
  // const auto clip_from_view = Math::InfReverseZPerspectiveRH(cameraFovyRadians, aspectRatio, cameraNearPlane);
  const auto clip_from_view  = Math::InfReverseZPerspectiveRH(glm::radians(65.0f), (float)head_->windowFramebufferWidth / head_->windowFramebufferHeight, 0.1f);
  const auto clip_from_world = clip_from_view * view_from_world;

  ctx.ImageBarrierDiscard(frame.sceneAlbedo.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneNormal.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneIlluminance.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneRadiance.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneDepth.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  perFrameUniforms.UpdateData(commandBuffer,
    Temp::Uniforms{
      .viewProj               = clip_from_world,
      .oldViewProjUnjittered  = glm::mat4{},
      .viewProjUnjittered     = glm::mat4{},
      .invViewProj            = glm::inverse(clip_from_world),
      .proj                   = glm::mat4{},
      .invProj                = glm::mat4{},
      .cameraPos              = glm::vec4(position, 0),
      .meshletCount           = 0,
      .maxIndices             = 0,
      .bindlessSamplerLodBias = 0,
      .flags                  = 0,
      .alphaHashScale         = 0,
    });

  auto drawCalls       = std::vector<GpuMesh*>();
  auto meshUniformzVec = std::vector<Temp::ObjectUniforms>();
  for (auto&& [entity, transform, mesh] : world.GetRegistry().view<GlobalTransform, Mesh>().each())
  {
    GlobalTransform actualTransform = transform;
    if (auto* renderTransform = world.GetRegistry().try_get<RenderTransform>(entity))
    {
      actualTransform = renderTransform->transform;
    }
    auto worldFromObject = glm::translate(glm::mat4(1), actualTransform.position) * glm::mat4_cast(actualTransform.rotation) *
                           glm::scale(glm::mat4(1), glm::vec3(actualTransform.scale));
    auto& gpuMesh = g_meshes[mesh.name];
    auto tint     = glm::vec3(1);
    if (auto* tp = world.GetRegistry().try_get<Tint>(entity))
    {
      tint = tp->color;
    }
    meshUniformzVec.emplace_back(worldFromObject, gpuMesh.vertexBuffer->GetDeviceAddress(), tint);
    drawCalls.emplace_back(&gpuMesh);
  }

  auto meshUniformz = Fvog::TypedBuffer<Temp::ObjectUniforms>({
    .count = (uint32_t)meshUniformzVec.size(),
    .flag  = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE,
  });

  memcpy(meshUniformz.GetMappedMemory(), meshUniformzVec.data(), meshUniformzVec.size() * sizeof(Temp::ObjectUniforms));

  auto billboards = std::vector<Temp::BillboardInstance>();
  for (auto&& [entity, transform, health] : world.GetRegistry().view<RenderTransform, Health>(entt::exclude<LocalPlayer>).each())
  {
    billboards.emplace_back(Temp::BillboardInstance{
      .position   = transform.transform.position + glm::vec3(0, GetHeight({world.GetRegistry(), entity}) / 2.0f + 0.25f, 0),
      .scale      = {0.5f, 0.1f},
      .leftColor  = {0, 1, 0, 1},
      .rightColor = {1, 0, 0, 1},
      .middle     = health.hp / health.maxHp,
    });
  }

  auto billboardSprites = std::vector<Temp::BillboardSpriteInstance>();
  for (auto&& [entity, transform, billboardSprite] : world.GetRegistry().view<RenderTransform, Billboard>().each())
  {
    auto tint = glm::vec3(1);
    if (auto* tp = world.GetRegistry().try_get<Tint>(entity))
    {
      tint = tp->color;
    }
    billboardSprites.emplace_back(transform.transform.position,
      glm::vec2(transform.transform.scale),
      tint,
      GetOrEmplaceCachedTexture(billboardSprite.name).ImageView().GetTexture2D());
  }

  auto lights = std::vector<GpuLight>();
  for (auto&& [entity, light, transform] : world.GetRegistry().view<GpuLight, GlobalTransform>().each())
  {
    light.position  = transform.position;
    light.direction = GetForward(transform.rotation);
    if (const auto* rt = world.GetRegistry().try_get<RenderTransform>(entity))
    {
      light.position  = rt->transform.position;
      light.direction = GetForward(rt->transform.rotation);
    }
    light.colorSpace = COLOR_SPACE_sRGB_LINEAR;
    lights.emplace_back(light);
  }

  if (world.GetRegistry().ctx().contains<TwoLevelGrid>())
  {
    auto lines           = std::vector<Debug::Line>();
    const auto& ecsLines = world.GetRegistry().ctx().get<std::vector<Debug::Line>>();
    lines.insert(lines.end(), ecsLines.begin(), ecsLines.end());
#ifdef JPH_DEBUG_RENDERER
    const auto* debugRenderer = dynamic_cast<const Physics::DebugRenderer*>(JPH::DebugRenderer::sInstance);
    assert(debugRenderer);
    auto physicsLines = debugRenderer->GetLines();
    lines.insert(lines.end(), physicsLines.begin(), physicsLines.end());
#endif
    if (!lines.empty())
    {
      if (!lineVertexBuffer || lineVertexBuffer->Size() < lines.size() * sizeof(Debug::Line))
      {
        lineVertexBuffer.emplace((uint32_t)lines.size(), "Debug Lines");
      }
      lineVertexBuffer->UpdateData(commandBuffer, lines);
    }

    if (!billboards.empty())
    {
      if (!billboardInstanceBuffer || billboardInstanceBuffer->Size() < billboards.size() * sizeof(Temp::BillboardInstance))
      {
        billboardInstanceBuffer.emplace((uint32_t)billboards.size(), "Billboards");
      }
      billboardInstanceBuffer->UpdateData(commandBuffer, billboards);
    }

    if (!lights.empty())
    {
      if (!lightBuffer || lightBuffer->Size() < lights.size() * sizeof(GpuLight))
      {
        lightBuffer.emplace((uint32_t)lights.size(), "Lights");
      }
      lightBuffer->UpdateData(commandBuffer, lights);
    }

    if (!billboardSprites.empty())
    {
      if (!billboardSpriteInstanceBuffer || billboardSpriteInstanceBuffer->Size() < billboardSprites.size() * sizeof(Temp::BillboardSpriteInstance))
      {
        billboardSpriteInstanceBuffer.emplace((uint32_t)billboardSprites.size(), "Billboard Sprites");
      }
      billboardSpriteInstanceBuffer->UpdateData(commandBuffer, billboardSprites);
    }

    auto voxelSampler = Fvog::Sampler(
      {
        .magFilter     = VK_FILTER_NEAREST,
        .minFilter     = VK_FILTER_NEAREST,
        .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxAnisotropy = 16,
      },
      "Voxel Sampler");

    auto& grid            = world.GetRegistry().ctx().get<TwoLevelGrid>();
    auto albedoAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneAlbedo.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto normalAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneNormal.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto illuminanceAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneIlluminance.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    auto radianceAttachment = Fvog::RenderColorAttachment{
      .texture = frame.sceneRadiance.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    };
    Fvog::RenderColorAttachment colorAttachments[] = {albedoAttachment, normalAttachment, illuminanceAttachment, radianceAttachment};
    auto depthAttachment = Fvog::RenderDepthStencilAttachment{
      .texture = frame.sceneDepth.value().ImageView(),
      .loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .clearValue = {.depth = FAR_DEPTH},
    };
    ctx.BeginRendering({
      .name             = "Render voxels",
      .colorAttachments = colorAttachments,
      .depthAttachment  = depthAttachment,
    });
    const auto voxels = Temp::Voxels{
      .topLevelBricksDims         = grid.topLevelBricksDims_,
      .topLevelBrickPtrsBaseIndex = grid.topLevelBrickPtrsBaseIndex,
      .dimensions                 = grid.dimensions_,
      .bufferIdx                  = grid.buffer.GetGpuBuffer().GetResourceHandle().index,
      .materialBufferIdx          = voxelMaterialBuffer->GetResourceHandle().index,
      .voxelSampler               = voxelSampler,
      .numLights                  = (uint32_t)lights.size(),
      .lightBufferIdx             = lights.empty() ? 0 : lightBuffer->GetDeviceBuffer().GetResourceHandle().index,
    };
    {
      // Voxels
      TracyVkZone(head_->tracyVkContext_, commandBuffer, "Voxels");
      ctx.BindGraphicsPipeline(testPipeline.GetPipeline());
      ctx.SetPushConstants(Temp::PushConstants{
        .voxels = voxels,
        .uniformBufferIndex         = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .noiseTexture = noiseTexture->ImageView().GetTexture2D(),
      });
      ctx.Draw(3, 1, 0, 0);
    }
    {
      // Meshes
      TracyVkZone(head_->tracyVkContext_, commandBuffer, "Meshes");
      ctx.BindGraphicsPipeline(meshPipeline.GetPipeline());
      ctx.SetPushConstants(Temp::MeshArgs{
        .objects      = meshUniformz.GetDeviceAddress(),
        .frame        = perFrameUniforms.GetDeviceBuffer().GetDeviceAddress(),
        .voxels       = voxels,
        .noiseTexture = noiseTexture->ImageView().GetTexture2D(),
      });

      for (size_t i = 0; i < drawCalls.size(); i++)
      {
        const auto& mesh = drawCalls[i];
        ctx.BindIndexBuffer(mesh->indexBuffer.value(), 0, VK_INDEX_TYPE_UINT32);
        ctx.DrawIndexed((uint32_t)mesh->indices.size(), 1, 0, 0, (uint32_t)i);
      }

      if (!lines.empty())
      {
        ctx.BindGraphicsPipeline(debugLinesPipeline.GetPipeline());
        ctx.SetPushConstants(DebugLinesPushConstants{
          .vertexBufferIndex   = lineVertexBuffer->GetDeviceBuffer().GetResourceHandle().index,
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .useGpuVertexBuffer  = 0,
        });
        ctx.Draw(uint32_t(lines.size() * 2), 1, 0, 0);
      }

      if (!billboards.empty())
      {
        ctx.BindGraphicsPipeline(billboardsPipeline.GetPipeline());
        ctx.SetPushConstants(BillboardPushConstants{
          .billboardsIndex     = billboardInstanceBuffer->GetDeviceBuffer().GetResourceHandle().index,
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .cameraRight         = {view_from_world[0][0], view_from_world[1][0], view_from_world[2][0]},
          .cameraUp            = {view_from_world[0][1], view_from_world[1][1], view_from_world[2][1]},
          .texSampler          = voxelSampler,
        });
        ctx.Draw(uint32_t(billboards.size() * 6), 1, 0, 0);
      }

      if (!billboardSprites.empty())
      {
        ctx.BindGraphicsPipeline(billboardSpritesPipeline.GetPipeline());
        ctx.SetPushConstants(BillboardPushConstants{
          .billboardsIndex     = billboardSpriteInstanceBuffer->GetDeviceBuffer().GetResourceHandle().index,
          .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
          .cameraRight         = {view_from_world[0][0], view_from_world[1][0], view_from_world[2][0]},
          .cameraUp            = {view_from_world[0][1], view_from_world[1][1], view_from_world[2][1]},
          .texSampler          = voxelSampler,
        });
        ctx.Draw(uint32_t(billboardSprites.size() * 6), 1, 0, 0);
      }
    }
    ctx.EndRendering();
  }

  bilateral_.DenoiseIlluminance(
    {
      .sceneAlbedo              = &frame.sceneAlbedo.value(),
      .sceneNormal              = &frame.sceneNormal.value(),
      .sceneDepth               = &frame.sceneDepth.value(),
      .sceneRadiance            = &frame.sceneRadiance.value(),
      .sceneIlluminance         = &frame.sceneIlluminance.value(),
      .sceneIlluminancePingPong = &frame.sceneIlluminancePingPong.value(),
      .sceneColor               = &frame.sceneColor.value(),
      .clip_from_view           = clip_from_view,
      .world_from_clip          = glm::inverse(clip_from_world),
      .cameraPos                = position,
    },
    commandBuffer);
}

static std::string FixupTypeString(std::string_view str)
{
  if (auto pos = str.find("::"); pos != std::string_view::npos)
  {
    return std::string(str.substr(pos + 2));
  }

  if (auto pos = str.find_first_of(' '); pos != std::string_view::npos)
  {
    return std::string(str.substr(pos + 1));
  }

  return std::string(str);
}

static void DrawComponentHelper(entt::meta_any instance, entt::meta_custom custom, bool readonly, int& guiId)
{
  using namespace Core::Reflection;
  auto meta = instance.type();

  // If the type has a bespoke EditorWrite or EditorRead function, use that. Otherwise, recurse over data members.
  PropertiesMap properties = {};
  if (auto* mp = static_cast<const PropertiesMap*>(custom))
  {
    properties = *mp;
  }

  if (auto writeFunc = meta.func("EditorWrite"_hs); writeFunc && !readonly)
  {
    writeFunc.invoke(instance, properties);
  }
  else if (auto readFunc = meta.func("EditorRead"_hs))
  {
    readFunc.invoke(instance, properties);
  }
  else if (meta.is_sequence_container())
  {
    bool isOpen = false;
    bool didIndent = false;
    if (auto it = properties.find("name"_hs); it != properties.end())
    {
      isOpen = ImGui::TreeNodeEx(instance.try_cast<void>(), 0, "%s: %d", *it->second.try_cast<const char*>(), (int)instance.as_sequence_container().size());
    }
    else
    {
      auto name = FixupTypeString(meta.info().name());
      isOpen    = ImGui::TreeNodeEx(instance.try_cast<void>(), 0, "%s: %d", name.c_str(), (int)instance.as_sequence_container().size());
      ImGui::Indent();
      didIndent = true;
    }
    if (isOpen)
    {
      for (auto element : instance.as_sequence_container())
      {
        auto eType = element.type();
        ImGui::PushID(guiId++);
        DrawComponentHelper(element, eType.custom(), readonly, guiId);
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    if (didIndent)
    {
      ImGui::Unindent();
    }
  }
  else if (meta.is_associative_container())
  {
    ImGui::Text("TODO: associative containers");
    // TODO: Make two-column table.
    for (auto element : instance.as_associative_container())
    {
      //auto eType = element.second.type();
      //if (auto traits = eType.traits<Traits>(); traits & Traits::EDITOR || traits & Traits::EDITOR_READ)
      //{
      //  ImGui::PushID(guiId++);
      //  ImGui::Indent();
      //  DrawComponentHelper(element.second.get(eType.id()), eType.custom(), readonly || traits & Traits::EDITOR_READ, guiId);
      //  ImGui::Unindent();
      //  ImGui::PopID();
      //}
    }
  }
  else
  {
    for (auto [id, data] : meta.data())
    {
      if (auto traits = data.traits<Traits>(); traits & Traits::EDITOR || traits & Traits::EDITOR_READ)
      {
        ImGui::PushID(guiId++);
        ImGui::Indent();
        DrawComponentHelper(data.get(instance), data.custom(), readonly || traits & Traits::EDITOR_READ, guiId);
        ImGui::Unindent();
        ImGui::PopID();
      }
    }
  }
}

// `minified`: Display just the first row of the inventory. Used to display the player's hotbar.
// `userTransform`: Transform of the entity interacting with the container. Used to calculate throw position and direction.
static void DrawInventory(World& world, entt::entity parent, const GlobalTransform& userTransform, Inventory& inventory, bool minified = false)
{
  auto& registry = world.GetRegistry();
  struct InventoryDragDropPayload
  {
    glm::ivec2 sourceRowCol;
    entt::entity sourceEntity;
  };

  auto title = "Inventory" + std::to_string(std::underlying_type_t<entt::entity>(parent));
  if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration))
  {
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0.5f, 0.5f});
    ImGui::BeginTable(title.c_str(), (int)inventory.width, ImGuiTableFlags_Borders);

    for (size_t row = 0; row < inventory.slots.size(); row++)
    {
      ImGui::PushID(int(row));
      for (size_t col = 0; col < inventory.slots[row].size(); col++)
      {
        const auto currentSlotCoord = glm::ivec2(row, col);
        ImGui::TableNextColumn();
        ImGui::PushID(int(col));
        auto& slot          = inventory.slots[row][col];
        std::string nameStr = "";
        if (slot.id != nullItem)
        {
          const auto& def = world.GetRegistry().ctx().get<ItemRegistry>().Get(slot.id);
          nameStr         = def.GetName();
          if (def.GetMaxStackSize() > 1)
          {
            nameStr += "\n" + std::to_string(slot.count) + "/" + std::to_string(def.GetMaxStackSize());
          }
        }
        const auto name      = nameStr.c_str();
        const auto cursorPos = ImGui::GetCursorPos();
        if (ImGui::Selectable(("##" + nameStr).c_str(), inventory.activeSlotCoord == currentSlotCoord, 0, {50, 50}))
        {
          inventory.SetActiveSlot(currentSlotCoord, parent);
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
          const auto dragDropPayload = InventoryDragDropPayload{
            .sourceRowCol = currentSlotCoord,
            .sourceEntity = parent,
          };
          ImGui::SetDragDropPayload("INVENTORY_SLOT", &dragDropPayload, sizeof(dragDropPayload));
          ImGui::Text("%s", name);
          ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
          if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT"))
          {
            assert(payload->DataSize == sizeof(InventoryDragDropPayload));
            const auto inventoryPayload = *static_cast<const InventoryDragDropPayload*>(payload->Data);
            SwapInventorySlots(world, inventoryPayload.sourceEntity, inventoryPayload.sourceRowCol, parent, currentSlotCoord);
          }
          ImGui::EndDragDropTarget();
        }
        ImGui::SetCursorPos(cursorPos);
        ImGui::TextWrapped("%s", name);
        ImGui::PopID();
      }
      ImGui::PopID();
      // Only show first row if inventory is not open
      if (minified)
      {
        break;
      }
    }
    ImGui::EndTable();

    if (!minified)
    {
      // Moving entity from inventory to world
      ImGui::Selectable("Ground", false, 0, {ImGui::GetContentRegionAvail().x, 50});
      if (ImGui::BeginDragDropTarget())
      {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT"))
        {
          assert(payload->DataSize == sizeof(InventoryDragDropPayload));
          const auto inventoryPayload = *static_cast<const InventoryDragDropPayload*>(payload->Data);

          if (registry.valid(inventoryPayload.sourceEntity))
          {
            if (auto* plInventory = registry.try_get<Inventory>(inventoryPayload.sourceEntity))
            {
              if (auto dropped = plInventory->DropItem(inventoryPayload.sourceRowCol); dropped != entt::null)
              {
                const auto throwdir                                       = GetForward(userTransform.rotation);
                const auto pos                                            = userTransform.position + throwdir * 1.0f;
                world.GetRegistry().get<LocalTransform>(dropped).position = pos;
                world.GetRegistry().get<LinearVelocity>(dropped).v        = throwdir * 3.0f;
                UpdateLocalTransform({world.GetRegistry(), dropped});
              }
            }
          }
        }
        ImGui::EndDragDropTarget();
      }
    }
    ImGui::PopStyleVar();
  }
  ImGui::End();
}

void VoxelRenderer::OnGui([[maybe_unused]] DeltaTime dt, World& world, [[maybe_unused]] VkCommandBuffer commandBuffer)
{
  ZoneScoped;
  switch (auto& gameState = world.GetRegistry().ctx().get<GameState>())
  {
  case GameState::MENU:
    if (ImGui::Begin("Menu"))
    {
      if (ImGui::Button("Play"))
      {
        gameState = GameState::LOADING;
        world.InitializeGameState();
        world.GetRegistry().ctx().emplace_as<std::atomic_int32_t>("progress"_hs, 0);
        world.GetRegistry().ctx().emplace_as<std::atomic_int32_t>("total"_hs, 1);
        world.GetRegistry().ctx().emplace_as<std::future<void>>("loading"_hs, std::async(std::launch::async, [&world] { world.GenerateMap(); }));
      }
    }
    ImGui::End();
    break;
  case GameState::GAME:
  {
    if (ImGui::Begin("Test"))
    {
      GetPipelineManager().PollModifiedShaders();
      GetPipelineManager().EnqueueModifiedShaders();

      // auto& mainCamera = world.GetSingletonComponent<Temp::View>();
      ImGui::Text("Framerate: %.0f (%.2fms)", 1 / dt.real, dt.real * 1000);
      // ImGui::Text("Camera pos: (%.2f, %.2f, %.2f)", mainCamera.position.x, mainCamera.position.y, mainCamera.position.z);
      // ImGui::Text("Camera dir: (%.2f, %.2f, %.2f)", mainCamera.GetForwardDir().x, mainCamera.GetForwardDir().y, mainCamera.GetForwardDir().z);
      auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
      VmaStatistics stats{};
      vmaGetVirtualBlockStatistics(grid.buffer.GetAllocator(), &stats);
      auto [usedSuffix, usedDivisor]   = Math::BytesToSuffixAndDivisor(stats.allocationBytes);
      auto [blockSuffix, blockDivisor] = Math::BytesToSuffixAndDivisor(stats.blockBytes);
      ImGui::Text("Voxel memory: %.2f %s / %.2f %s", stats.allocationBytes / usedDivisor, usedSuffix, stats.blockBytes / blockDivisor, blockSuffix);

      if (ImGui::Button("Collapse da grid"))
      {
        grid.CoalesceDirtyBricks();
      }
    }
    ImGui::End();

    // Get information about the local player
    auto range = world.GetRegistry().view<Player, LocalPlayer, Inventory, GlobalTransform>().each();

    if (range.begin() == range.end())
    {
      return;
    }

    auto&& [playerEntity, p, inventory, gt] = *range.begin();
    
    // TODO: replace with bitmap font rendered above each creature
    auto collector = Physics::NearestRayCollector();
    auto dir       = GetForward(gt.rotation);
    auto start     = gt.position;
    Physics::GetNarrowPhaseQuery().CastRay(JPH::RRayCast(Physics::ToJolt(start), Physics::ToJolt(dir * 20.0f)),
      JPH::RayCastSettings(),
      collector,
      Physics::GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_CHARACTER),
      Physics::GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_CHARACTER));
    if (ImGui::Begin("Target"))
    {
      if (auto* h = world.GetRegistry().try_get<Health>(playerEntity))
      {
        ImGui::Text("Player HP: %.2f", h->hp);
      }
      if (collector.nearest)
      {
        auto entity = static_cast<entt::entity>(Physics::GetBodyInterface().GetUserData(collector.nearest->mBodyID));
        if (auto* n = world.GetRegistry().try_get<Name>(entity))
        {
          ImGui::Text("%s", n->name.c_str());
        }

        if (auto* h = world.GetRegistry().try_get<Health>(entity))
        {
          ImGui::Text("Health: %.2f", h->hp);
        }
      }
    }
    ImGui::End();

    DrawInventory(world, playerEntity, gt, inventory, !p.inventoryIsOpen);

    if (world.GetRegistry().valid(p.openContainerId))
    {
      if (auto* ip = world.GetRegistry().try_get<Inventory>(p.openContainerId))
      {
        p.inventoryIsOpen = true;
        DrawInventory(world, p.openContainerId, gt, *ip);
      }
    }

    if (p.showInteractPrompt)
    {
      ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      constexpr auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground;
      if (ImGui::Begin("Interact", nullptr, flags))
      {
        ImGui::Text("Press F to pay respects");
      }
      ImGui::End();
    }

    if (p.inventoryIsOpen)
    {
      if (ImGui::Begin("Crafting"))
      {
        // Get set of blocks around player. This is used to find the "crafting stations" that are near the player, which some recipes call for.
        auto nearVoxels = std::unordered_set<BlockId>();
        const auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
        for (int z = -5; z <= 5; z++)
        for (int y = -5; y <= 5; y++)
        for (int x = -5; x <= 5; x++)
        {
          const auto vp = glm::vec3(x, y, z);
          const auto fp = glm::ivec3(glm::floor(gt.position));
          if (Math::Distance2(glm::vec3(fp) + vp + 0.5f, gt.position) <= 5 * 5)
          {
            nearVoxels.emplace(grid.GetVoxelAt(glm::ivec3(vp) + fp));
          }
        }

        const auto& crafting = world.GetRegistry().ctx().get<Crafting>();
        const auto& itemRegistry = world.GetRegistry().ctx().get<ItemRegistry>();
        for (int index = 0; const auto& recipe : crafting.recipes)
        {
          if (index != 0)
          {
            ImGui::Separator();
          }
          ImGui::PushID(index);
          ImGui::BeginDisabled(!inventory.CanCraftRecipe(recipe) || !nearVoxels.contains(recipe.craftingStation));
          if (ImGui::Button("Craft"))
          {
            inventory.CraftRecipe(recipe, playerEntity);
          }
          ImGui::Text("Output");
          ImGui::Indent();
          for (const auto& output : recipe.output)
          {
            const auto& def = itemRegistry.Get(output.item);
            ImGui::TextWrapped("%s: %d", def.GetName().c_str(), output.count);
          }
          ImGui::Unindent();

          ImGui::Text("Ingredients");
          ImGui::Indent();
          for (const auto& ingredient : recipe.ingredients)
          {
            const auto& def = itemRegistry.Get(ingredient.item);
            ImGui::TextWrapped("%s: %d", def.GetName().c_str(), ingredient.count);
          }
          ImGui::Unindent();
          ImGui::EndDisabled();
          ImGui::PopID();
          index++;
        }
      }
      ImGui::End();
    }
    break;
  }
  case GameState::PAUSED:
    if (ImGui::Begin("Paused"))
    {
      if (ImGui::Button("Unpause"))
      {
        gameState = GameState::GAME;
      }

      if (ImGui::Button("Exit to main menu"))
      {
        gameState = GameState::MENU;
      }

      if (ImGui::Button("Exit to desktop"))
      {
        world.GetRegistry().ctx().emplace<CloseApplication>();
      }
    }
    ImGui::End();
    break;
  case GameState::LOADING:
  {
    // There is an ongoing connection attempt or the world is loading.
    auto& future = world.GetRegistry().ctx().get<std::future<void>>("loading"_hs);
    using namespace std::chrono_literals;
    if (future.wait_for(0s) == std::future_status::ready)
    {
      gameState = GameState::GAME;
    }
    else
    {
      // Show loading bar.
      const auto& progress = world.GetRegistry().ctx().get<std::atomic_int32_t>("progress"_hs);
      const auto& total = world.GetRegistry().ctx().get<std::atomic_int32_t>("total"_hs);
      if (ImGui::Begin("Loading"))
      {
        ImGui::Text("frogress: %d / %d", progress.load(), total.load());
      }
      ImGui::End();
    }
    break;
  }
  default: assert(0);
  }

  if (world.GetRegistry().ctx().get<Debugging>().showDebugGui)
  {
    auto& registry = world.GetRegistry();
    if (ImGui::Begin("Entities"))
    {
      if (!ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered() && ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left])
      {
        selectedEntity = entt::null;
      }

      // Show entity hierarchy.
      for (auto e : registry.view<entt::entity>())
      {
        ImGui::PushID((int)e);
        bool opened = false;

        int flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selectedEntity == e)
        {
          flags |= ImGuiTreeNodeFlags_Selected;
        }

        if (auto* s = registry.try_get<Name>(e))
        {
          opened = ImGui::TreeNodeEx("entity", flags, "%u (%s) (v%u)", entt::to_entity(e), s->name.c_str(), entt::to_version(e));
        }
        else
        {
          opened = ImGui::TreeNodeEx("entity", flags, "%u (v%u)", entt::to_entity(e), entt::to_version(e));
        }

        // Single-clicking anywhere should select the node
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
          selectedEntity = e;
        }

        if (opened)
        {
          ImGui::TextUnformatted("TODO lol");
          ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::PopID();
      }
    }
    ImGui::End();

    if (ImGui::Begin("Components"))
    {
      if (registry.valid(selectedEntity))
      {
        auto e = selectedEntity;
        
        if (ImGui::Button("Delete Entity"))
        {
          registry.emplace_or_replace<DeferredDelete>(e);
        }
        ImGui::SameLine();
        if (ImGui::BeginCombo("##add", "Add Component"))
        {
          using MetaPair = decltype(*entt::resolve().begin());
          auto metas = std::vector<MetaPair>();
          for (auto pair : entt::resolve())
          {
            if ((registry.storage(pair.first) && !registry.storage(pair.first)->contains(e)))
            {
              metas.emplace_back(pair);
            }
          }
          std::sort(metas.begin(),
            metas.end(),
            [](const MetaPair& p1, const MetaPair& p2) { return FixupTypeString(p1.second.info().name()) < FixupTypeString(p2.second.info().name()); });

          for (auto [id, meta] : metas)
          {
            if (meta.traits<Core::Reflection::Traits>() & Core::Reflection::Traits::COMPONENT)
            {
              const auto label        = FixupTypeString(meta.info().name());
              auto addFunc            = meta.func("add"_hs);
              auto emplaceDefaultFunc = meta.func("EmplaceDefault"_hs);
              int flags               = 0;
              if (!addFunc && !emplaceDefaultFunc)
              {
                flags |= ImGuiSelectableFlags_Disabled;
              }
              if (ImGui::Selectable(label.c_str(), false, flags))
              {
                if (addFunc)
                {
                  addFunc.invoke({}, &world, e); // Can't figure out how to invoke with a reference (std::ref doesn't work), so pointers it is.
                }
                else if (emplaceDefaultFunc)
                {
                  emplaceDefaultFunc.invoke({}, &registry, e);
                }
                else
                {
                  // Sad face :(
                  assert(false);
                }
              }
            }
          }
          ImGui::EndCombo();
        }

        // Sort component types by name.
        using SetPair = std::pair<entt::id_type, entt::sparse_set*>;
        auto storages = std::vector<SetPair>();
        for (auto pair : registry.storage())
        {
          storages.emplace_back(pair.first, &pair.second);
        }
        std::sort(storages.begin(),
          storages.end(),
          [](const SetPair& p1, const SetPair& p2)
          {
            auto meta1 = entt::resolve(p1.first);
            auto meta2 = entt::resolve(p2.first);
            if (meta1 && meta2)
            {
              return FixupTypeString(meta1.info().name()) < FixupTypeString(meta2.info().name());
            }
            return p1.first < p2.first;
          });
        
        for (int i = 0; auto&& [id, storage] : storages)
        {
          if (!storage->contains(e))
          {
            continue;
          }

          ImGui::PushID(i++);
          if (ImGui::Button("X"))
          {
            storage->remove(e);
          }
          ImGui::SameLine();
          ImGui::SeparatorText(FixupTypeString(storage->type().name()).c_str());

          if (auto meta = entt::resolve(id); storage->contains(e) && meta)
          {
            DrawComponentHelper(meta.from_void(storage->value(e)), meta.custom(), meta.traits<Core::Reflection::Traits>() & Core::Reflection::Traits::EDITOR_READ, i);
          }
          else
          {
            ImGui::Text("Reflection is unavailable for this type.");
          }
          ImGui::PopID();
        }
      }
    }
    ImGui::End();

    if (ImGui::Begin("Context"))
    {
      auto& ctx   = world.GetRegistry().ctx();
      auto& debug = ctx.get<Debugging>();
      ImGui::Checkbox("Show Debug GUI", &debug.showDebugGui);
      ImGui::Checkbox("Force Show Cursor", &debug.forceShowCursor);
      ImGui::Checkbox("Draw Debug Probe", &debug.drawDebugProbe);
      ImGui::Checkbox("Draw Physics Shapes", &debug.drawPhysicsShapes);
      ImGui::Checkbox("Draw Physics Velocity", &debug.drawPhysicsVelocity);

      ImGui::Text("Game state: %s", GameStateToStr(ctx.get<GameState>()));
      ImGui::Text("Time: %f", ctx.get<float>("time"_hs));

      ImGui::SliderFloat("Time Scale", &ctx.get<TimeScale>().scale, 0, 4, "%.2f", ImGuiSliderFlags_NoRoundToFormat);
      auto min = uint32_t(5);
      auto max = uint32_t(120);
      ImGui::SliderScalar("Tick Rate", ImGuiDataType_U32, &world.GetRegistry().ctx().get<TickRate>().hz, &min, &max, "%u", ImGuiSliderFlags_AlwaysClamp);
    }
    ImGui::End();

    if (ImGui::Begin("TEST PROBULUS"))
    {
      static glm::vec3 probePos = {0, 60, 0};
      static glm::vec3 probePos2 = {0, 61, 0};
      static float probeRadius  = 2;

#ifdef JPH_DEBUG_RENDERER
      //JPH::DebugRenderer::sInstance->DrawWireSphere(Physics::ToJolt(probePos), probeRadius, JPH::Color::sGreen);
      const auto jup    = Physics::ToJolt(glm::normalize(probePos2 - probePos));
      const auto jfor = jup.GetNormalizedPerpendicular();
      const auto jright = jfor.Cross(jup);
      
      auto mat = JPH::Mat44::sIdentity();
      mat.SetAxisX(jright);
      mat.SetAxisY(jup);
      mat.SetAxisZ(jfor);
      mat.SetTranslation(Physics::ToJolt((probePos + probePos2) / 2.0f));
      JPH::DebugRenderer::sInstance->DrawCapsule(mat, glm::distance(probePos, probePos2) / 2.0f, probeRadius, JPH::Color::sGreen);
#endif

      ImGui::DragFloat3("Probe pos1", &probePos[0], 0.25f);
      ImGui::DragFloat3("Probe pos2", &probePos2[0], 0.25f, 0, 0, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
      ImGui::DragFloat("Probe radius", &probeRadius, 0.125f, 0, 1000);
      ImGui::Separator();
      auto entities = world.GetEntitiesInCapsule(probePos, probePos2, probeRadius);
      ImGui::Text("Covered: %llu", entities.size());
      for (auto entity : entities)
      {
        auto name = std::string();
        if (const auto* n = world.GetRegistry().try_get<Name>(entity))
        {
          name = n->name;
        }
        ImGui::Text("%u (%s)", entt::to_entity(entity), name.c_str());
      }
    }
    ImGui::End();

    auto range = world.GetRegistry().view<Player, LocalPlayer, Inventory, GlobalTransform>().each();

    if (range.begin() == range.end())
    {
      return;
    }

    auto&& [playerEntity, p, inventory, gt] = *range.begin();

    if (ImGui::Begin("It's free real estate"))
    {
      const auto& itemRegistry = world.GetRegistry().ctx().get<ItemRegistry>();
      for (int i = 0; const auto& itemDefinition : itemRegistry.GetAllItemDefinitions())
      {
        ImGui::PushID(i);
        if (ImGui::Button(itemDefinition->GetName().c_str(), {-1, 0}))
        {
          auto item = ItemState{static_cast<ItemId>(i), 1};
          inventory.TryStackItem(item);
          if (item.count > 0)
          {
            if (auto slot = inventory.GetFirstEmptySlot())
            {
              inventory.OverwriteSlot(*slot, item, playerEntity);
            }
            else
            {
              world.CreateDroppedItem(item, gt.position, gt.rotation, gt.scale);
            }
          }
        }
        ImGui::PopID();
        i++;
      }
    }
    ImGui::End();
  }
}

Fvog::Texture& VoxelRenderer::GetOrEmplaceCachedTexture(const std::string& name)
{
  if (auto it = stringToTexture.find(name); it != stringToTexture.end())
  {
    return it->second;
  }

  // Make a very sketchy and unscalable assumption about the path.
  auto texture = LoadImageFile(std::filesystem::path("voxels") / (name + ".png"));

  return stringToTexture.emplace(name, std::move(texture)).first->second;
}
