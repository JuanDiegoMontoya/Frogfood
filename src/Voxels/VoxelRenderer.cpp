#include "VoxelRenderer.h"

#include "Assets.h"
#include "MathUtilities.h"
#include "PipelineManager.h"
#include "imgui.h"
#include "Fvog/Rendering2.h"
#include "Fvog/detail/Common.h"
#include "shaders/Config.shared.h"

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

#include "tracy/TracyVulkan.hpp"

#include <memory>
#include <numeric>
#include <type_traits>

namespace
{
  using index_t = uint32_t;

  struct Vertex
  {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec3 color{};
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat}},
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat}},
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat}},
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
            .colorAttachmentFormats = {{Frame::sceneAlbedoFormat, Frame::sceneNormalFormat, Frame::sceneIlluminanceFormat}},
            .depthAttachmentFormat  = Frame::sceneDepthFormat,
          },
      },
  });

  int x            = 0;
  int y            = 0;
  const auto noise = stbi_load((GetTextureDirectory() / "bluenoise256.png").string().c_str(), &x, &y, nullptr, 4);
  assert(noise);
  noiseTexture = Fvog::CreateTexture2D({static_cast<uint32_t>(x), static_cast<uint32_t>(y)}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::READ_ONLY, "Noise");
  noiseTexture->UpdateImageSLOW({
    .extent = {static_cast<uint32_t>(x), static_cast<uint32_t>(y)},
    .data   = noise,
  });
  stbi_image_free(noise);

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

void VoxelRenderer::OnFramebufferResize(uint32_t newWidth, uint32_t newHeight)
{
  ZoneScoped;

  const auto extent      = VkExtent2D{newWidth, newHeight};
  frame.sceneAlbedo      = Fvog::CreateTexture2D(extent, Frame::sceneAlbedoFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene albedo");
  frame.sceneNormal      = Fvog::CreateTexture2D(extent, Frame::sceneNormalFormat, Fvog::TextureUsage::ATTACHMENT_READ_ONLY, "Scene normal");
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

  const auto nearestSampler = Fvog::Sampler({
    .magFilter     = VK_FILTER_NEAREST,
    .minFilter     = VK_FILTER_NEAREST,
    .mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .maxAnisotropy = 0,
  });

  auto ctx = Fvog::Context(commandBuffer);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);

  if (Fvog::GetDevice().supportsRayTracing)
  {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Fvog::GetDevice().defaultPipelineLayout, 0, 1, &Fvog::GetDevice().descriptorSet_, 0, nullptr);
  }

  ctx.Barrier();

  if (world.GetRegistry().ctx().contains<TwoLevelGrid>())
  {
    auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
    grid.buffer.FlushWritesToGPU(commandBuffer);
  }

  ctx.Barrier();

  auto viewMat = glm::mat4(1);
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
    viewMat = glm::translate(rotation, -position);
  }

  const auto view_from_world = viewMat;
  //const auto clip_from_view = Math::InfReverseZPerspectiveRH(cameraFovyRadians, aspectRatio, cameraNearPlane);
  const auto clip_from_view  = Math::InfReverseZPerspectiveRH(glm::radians(65.0f), (float)head_->windowFramebufferWidth / head_->windowFramebufferHeight, 0.1f);
  const auto clip_from_world = clip_from_view * view_from_world;

  ctx.ImageBarrierDiscard(frame.sceneAlbedo.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneNormal.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
  ctx.ImageBarrierDiscard(frame.sceneIlluminance.value(), VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
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
    auto worldFromObject = glm::translate(glm::mat4(1), actualTransform.position) * glm::mat4_cast(actualTransform.rotation) * glm::scale(glm::mat4(1), glm::vec3(actualTransform.scale));
    auto& gpuMesh = g_meshes[mesh.name];
    meshUniformzVec.emplace_back(worldFromObject, gpuMesh.vertexBuffer->GetDeviceAddress());
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

  if (world.GetRegistry().ctx().contains<TwoLevelGrid>())
  {
    auto lines = std::vector<Debug::Line>();
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

    auto& grid           = world.GetRegistry().ctx().get<TwoLevelGrid>();
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
    Fvog::RenderColorAttachment colorAttachments[] = {albedoAttachment, normalAttachment, illuminanceAttachment};
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
        .objects = meshUniformz.GetDeviceAddress(),
        .frame = perFrameUniforms.GetDeviceBuffer().GetDeviceAddress(),
        .voxels = voxels,
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
        });
        ctx.Draw(uint32_t(billboards.size() * 6), 1, 0, 0);
      }
    }
    ctx.EndRendering();
  }

  bilateral_.DenoiseIlluminance(
    {
      .sceneAlbedo              = &frame.sceneAlbedo.value(),
      .sceneNormal              = &frame.sceneNormal.value(),
      .sceneDepth               = &frame.sceneDepth.value(),
      .sceneIlluminance         = &frame.sceneIlluminance.value(),
      .sceneIlluminancePingPong = &frame.sceneIlluminancePingPong.value(),
      .sceneColor               = &frame.sceneColor.value(),
      .clip_from_view           = clip_from_view,
      .world_from_clip          = glm::inverse(clip_from_world),
      .cameraPos                = position,
    },
    commandBuffer);

  ctx.Barrier();
  ctx.ImageBarrier(head_->swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

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
        gameState = GameState::GAME;
        world.InitializeGameState();
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

    auto&& [playerEntity, p, i, gt] = *range.begin();
    
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

    if (ImGui::Begin("Inventory", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration))
    {
      ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0.5f, 0.5f});
      ImGui::BeginTable("Inventory", (int)i.width, ImGuiTableFlags_Borders);

      for (size_t row = 0; row < i.slots.size(); row++)
      {
        ImGui::PushID(int(row));
        for (size_t col = 0; col < i.slots[row].size(); col++)
        {
          const auto currentSlotCoord = glm::ivec2(row, col);
          ImGui::TableNextColumn();
          ImGui::PushID(int(col));
          auto& slot = i.slots[row][col];
          std::string nameStr  = "";
          if (slot.id != nullItem)
          {
            const auto& def = world.GetRegistry().ctx().get<ItemRegistry>().Get(slot.id);
            nameStr         = def.GetName();
            if (def.GetMaxStackSize() > 1)
            {
              nameStr += "\n" + std::to_string(slot.count) + "/" + std::to_string(def.GetMaxStackSize());
            }
          }
          auto name = nameStr.c_str();
          if (ImGui::Selectable(name, i.activeSlotCoord == currentSlotCoord, 0, {50, 50}))
          {
            i.SetActiveSlot(currentSlotCoord, playerEntity);
          }
          if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
          {
            const auto rowCol = glm::ivec2(row, col);
            ImGui::SetDragDropPayload("INVENTORY_SLOT", &rowCol, sizeof(rowCol));
            ImGui::Text("%s", name);
            ImGui::EndDragDropSource();
          }
          if (ImGui::BeginDragDropTarget())
          {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT"))
            {
              assert(payload->DataSize == sizeof(glm::ivec2));
              const auto sourceRowCol = *static_cast<const glm::ivec2*>(payload->Data);
              i.SwapSlots(sourceRowCol, {row, col}, playerEntity);
            }
            ImGui::EndDragDropTarget();
          }
          ImGui::PopID();
        }
        ImGui::PopID();
        // Only show first row if inventory is not open
        if (!p.inventoryIsOpen)
        {
          break;
        }
      }
      ImGui::EndTable();

      if (p.inventoryIsOpen)
      {
        // Moving entity from inventory to world
        ImGui::Selectable("Ground", false, 0, {ImGui::GetContentRegionAvail().x, 50});
        if (ImGui::BeginDragDropTarget())
        {
          if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT"))
          {
            assert(payload->DataSize == sizeof(glm::ivec2));
            const auto rowCol = *static_cast<const glm::ivec2*>(payload->Data);

            if (auto dropped = i.DropItem(rowCol); dropped != entt::null)
            {
              auto& rb = world.GetRegistry().get<Physics::RigidBody>(dropped);

              const auto throwdir = GetForward(gt.rotation);
              const auto pos = gt.position + throwdir * 1.0f;
              Physics::GetBodyInterface().SetPosition(rb.body, Physics::ToJolt(pos), JPH::EActivation::Activate);
              Physics::GetBodyInterface().SetLinearVelocity(rb.body, Physics::ToJolt(throwdir * 2.0f));
            }
          }
          ImGui::EndDragDropTarget();
        }
      }
      ImGui::PopStyleVar();
    }
    ImGui::End();

    if (p.inventoryIsOpen)
    {
      if (ImGui::Begin("Crafting"))
      {
        const auto& crafting = world.GetRegistry().ctx().get<Crafting>();
        const auto& itemRegistry = world.GetRegistry().ctx().get<ItemRegistry>();
        for (int index = 0; const auto& recipe : crafting.recipes)
        {
          if (index != 0)
          {
            ImGui::Separator();
          }
          ImGui::PushID(index);
          ImGui::BeginDisabled(!i.CanCraftRecipe(recipe));
          if (ImGui::Button("Craft"))
          {
            i.CraftRecipe(recipe, playerEntity);
          }
          ImGui::Text("Output");
          ImGui::Indent();
          for (const auto& output : recipe.output)
          {
            const auto& def = itemRegistry.Get(output.item);
            ImGui::Text("%s: %d", def.GetName().c_str(), output.count);
          }
          ImGui::Unindent();

          ImGui::Text("Ingredients");
          ImGui::Indent();
          for (const auto& ingredient : recipe.ingredients)
          {
            const auto& def = itemRegistry.Get(ingredient.item);
            ImGui::Text("%s: %d", def.GetName().c_str(), ingredient.count);
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
  default:;
  }

  if (world.GetRegistry().ctx().get<Debugging>().showDebugGui)
  {
    if (ImGui::Begin("Entities"))
    {
      auto& registry = world.GetRegistry();
      for (auto e : registry.view<entt::entity>())
      {
        ImGui::PushID((int)e);
        bool opened = false;

        if (auto* s = registry.try_get<Name>(e))
        {
          opened = ImGui::TreeNode("entity", "%u (%s) (v%u)", entt::to_entity(e), s->name.c_str(), entt::to_version(e));
        }
        else
        {
          opened = ImGui::TreeNode("entity", "%u (v%u)", entt::to_entity(e), entt::to_version(e));
        }
        if (opened)
        {
          if (auto* t = registry.try_get<LocalTransform>(e))
          {
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat3("Position", &t->position[0], 0.25f);
            ImGui::DragFloat4("Rotation", &t->rotation[0], 0.125f);
            ImGui::DragFloat("Scale", &t->scale, 0.125f);
          }
          if (auto* it = registry.try_get<PreviousGlobalTransform>(e))
          {
            ImGui::SeparatorText("InterpolatedTransform");
            ImGui::Text("Accumulator: %f", dt.fraction);
            ImGui::Text("Position: %f, %f, %f", it->position[0], it->position[1], it->position[2]);
            ImGui::Text("Rotation: %f, %f, %f, %f", it->rotation.w, it->rotation.x, it->rotation.y, it->rotation.z);
            ImGui::Text("Scale: %f", it->scale);
          }
          if (auto* rt = registry.try_get<RenderTransform>(e))
          {
            ImGui::SeparatorText("RenderTransform");
            const auto& tr = rt->transform;
            ImGui::Text("Position: %f, %f, %f", tr.position[0], tr.position[1], tr.position[2]);
            ImGui::Text("Rotation: %f, %f, %f, %f", tr.rotation.w, tr.rotation.x, tr.rotation.y, tr.rotation.z);
            ImGui::Text("Scale: %f", tr.scale);
          }
          if (auto* p = registry.try_get<Player>(e))
          {
            ImGui::SeparatorText("Player");
          }
          bool hasNoclipCharacterController = registry.all_of<NoclipCharacterController>(e);
          if (ImGui::Checkbox("NoclipCharacterController", &hasNoclipCharacterController))
          {
            if (!hasNoclipCharacterController)
            {
              registry.remove<NoclipCharacterController>(e);
            }
            else
            {
              registry.emplace<NoclipCharacterController>(e);
            }
          }
          auto* characterController = registry.try_get<Physics::CharacterController>(e);
          bool hasCharacterController = characterController;
          if (ImGui::Checkbox("CharacterController", &hasCharacterController))
          {
            if (!hasCharacterController)
            {
              registry.remove<Physics::CharacterController>(e);
            }
            else
            {
              world.GivePlayerCharacterController(e);
            }
          }
          if (auto* is = registry.try_get<InputState>(e))
          {
            ImGui::SeparatorText("InputState");
            ImGui::Text("strafe: %f", is->strafe);
            ImGui::Text("forward: %f", is->forward);
            ImGui::Text("elevate: %f", is->elevate);
            ImGui::Text("jump         : %d", is->jump);
            ImGui::Text("sprint       : %d", is->sprint);
            ImGui::Text("walk         : %d", is->walk);
            ImGui::Text("usePrimary   : %d", is->usePrimary);
            ImGui::Text("useSecondary : %d", is->useSecondary);
            ImGui::Text("interact     : %d", is->interact);
          }
          if (auto* ils = registry.try_get<InputLookState>(e))
          {
            ImGui::SeparatorText("InputLookState");
          }
          if (auto* rb = registry.try_get<Physics::RigidBody>(e))
          {
            ImGui::SeparatorText("RigidBody");
          }
          if (auto* m = registry.try_get<Mesh>(e))
          {
            ImGui::SeparatorText("Mesh");
            ImGui::Text("%s", m->name.c_str());
          }
          if (auto* d = registry.try_get<DroppedItem>(e))
          {
            ImGui::SeparatorText("DroppedItem");
            const auto& def = world.GetRegistry().ctx().get<ItemRegistry>().Get(d->item.id);
            ImGui::Text("%u (%s)", d->item.id, d->item.id != nullItem ? def.GetName().c_str() : "NULL");
            ImGui::Text("%llu / %llu", d->item.count, def.GetMaxStackSize());
          }
          if (const auto* lp = registry.try_get<LinearPath>(e))
          {
            ImGui::SeparatorText("LinearPath");
            ImGui::Text("%.3fs", lp->secondsElapsed);
          }
          if (registry.all_of<DeferredDelete>(e))
          {
            ImGui::SeparatorText("DeferredDelete");
          }
          if (auto* h = registry.try_get<Hierarchy>(e))
          {
            ImGui::SeparatorText("Hierarchy");
            ImGui::Text(h->parent == entt::null ? "Parent: null" : "Parent: %u", entt::to_entity(h->parent));
            for (auto child : h->children)
            {
              ImGui::Text("Child: %u", entt::to_entity(child));
            }
          }
          ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::PopID();
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
  }
}
