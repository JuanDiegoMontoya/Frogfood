#include "VoxelRenderer.h"

#include "Assets.h"
#include "MathUtilities.h"
#include "PipelineManager.h"
#include "imgui.h"
#include "Fvog/Rendering2.h"
#include "Fvog/detail/Common.h"
#include "shaders/Config.shared.h"

#include "Physics/Physics.h" // TODO: remove
#ifdef JPH_DEBUG_RENDERER
#include "Physics/DebugRenderer.h"
#endif

#include "volk.h"
#include "Fvog/detail/ApiToEnum2.h"

#include "tiny_obj_loader.h"
#include "tracy/Tracy.hpp"
#include "GLFW/glfw3.h" // TODO: remove

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

  struct Mesh
  {
    std::optional<Fvog::TypedBuffer<Vertex>> vertexBuffer;
    std::optional<Fvog::TypedBuffer<index_t>> indexBuffer;
    std::vector<Vertex> vertices;
    std::vector<index_t> indices;
  };

  Mesh LoadObjFile(const std::filesystem::path& path)
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

    auto mesh = Mesh{};

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

  Mesh g_testMesh; // TODO: TEMP
} // namespace

VoxelRenderer::VoxelRenderer(PlayerHead* head, World&) : head_(head)
{
  ZoneScoped;

  g_testMesh = LoadObjFile(GetAssetDirectory() / "models/frog.obj");

  head_->renderCallback_ = [this](float dt, World& world, VkCommandBuffer cmd, uint32_t swapchainImageIndex) { OnRender(dt, world, cmd, swapchainImageIndex); };
  head_->framebufferResizeCallback_ = [this](uint32_t newWidth, uint32_t newHeight) { OnFramebufferResize(newWidth, newHeight); };
  head_->guiCallback_ = [this](float dt, World& world, VkCommandBuffer cmd) { OnGui(dt, world, cmd); };

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
        .depthState         = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL},
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
        .depthState         = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = VK_COMPARE_OP_GREATER},
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

  using namespace glm;
  auto TraceRay =
    [](vec3 rayPosition, vec3 rayDirection, float tMax) -> bool
  {
      // https://www.shadertoy.com/view/X3BXDd
      vec3 mapPos = floor(rayPosition); // integer cell coordinate of initial cell

      const vec3 deltaDist = 1.0f / abs(rayDirection); // ray length required to step from one cell border to the next in x, y and z directions

      const vec3 S       = vec3(step(0.0f, rayDirection)); // S is rayDir non-negative? 0 or 1
      const vec3 stepDir = 2.f * S - 1.f;                     // Step sign

      // if 1./abs(rayDir[i]) is inf, then rayDir[i] is 0., but then S = step(0., rayDir[i]) is 1
      // so S cannot be 0. while deltaDist is inf, and stepDir * fract(pos) can never be 1.
      // Therefore we should not have to worry about getting NaN here :)

      // initial distance to cell sides, then relative difference between traveled sides
      vec3 sideDist = (S - stepDir * fract(rayPosition)) * deltaDist; // alternative: //sideDist = (S-stepDir * (pos - map)) * deltaDist;

      for (int i = 0; i < tMax; i++)
      {
        // Decide which way to go!
        vec4 conds = step(vec4(sideDist.x, sideDist.x, sideDist.y, sideDist.y),
          vec4(sideDist.y, sideDist.z, sideDist.z, sideDist.x)); // same as vec4(sideDist.xxyy <= sideDist.yzzx);

        // This mimics the if, elseif and else clauses
        // * is 'and', 1.-x is negation
        vec3 cases = vec3(0);
        cases.x    = conds.x * conds.y;                   // if       x dir
        cases.y    = (1.0f - cases.x) * conds.z * conds.w; // else if  y dir
        cases.z    = (1.0f - cases.x) * (1.0f - cases.y);   // else     z dir

        // usually would have been:     sideDist += cases * deltaDist;
        // but this gives NaN when  cases[i] * deltaDist[i]  becomes  0. * inf
        // This gives NaN result in a component that should not have been affected,
        // so we instead give negative results for inf by mapping 'cases' to +/- 1
        // and then clamp negative values to zero afterwards, giving the correct result! :)
        sideDist += max((2.0f * cases - 1.0f) * deltaDist, 0.0f);

        mapPos += cases * stepDir;

#if 0
        // Putting the exit condition down here implicitly skips the first voxel
        if (all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, ivec3(pc.dimensions))))
        {
          const voxel_t voxel = GetVoxelAt(ivec3(mapPos));
          if (voxel != 0)
          {
            const vec3 p      = mapPos + 0.5 - stepDir * 0.5; // Point on axis plane
            const vec3 normal = vec3(ivec3(vec3(cases))) * -vec3(stepDir);

            // Solve ray plane intersection equation: dot(n, ro + t * rd - p) = 0.
            // for t :
            const float t          = (dot(normal, p - rayPosition)) / dot(normal, rayDirection);
            const vec3 hitWorldPos = rayPosition + rayDirection * t;
            const vec3 uvw         = hitWorldPos - mapPos; // Don't use fract here

            // Ugly, hacky way to get texCoord
            vec2 texCoord = {0, 0};
            if (normal.x > 0)
              texCoord = vec2(1 - uvw.z, uvw.y);
            if (normal.x < 0)
              texCoord = vec2(uvw.z, uvw.y);
            if (normal.y > 0)
              texCoord = vec2(uvw.x, 1 - uvw.z); // Arbitrary
            if (normal.y < 0)
              texCoord = vec2(uvw.x, uvw.z);
            if (normal.z > 0)
              texCoord = vec2(uvw.x, uvw.y);
            if (normal.z < 0)
              texCoord = vec2(1 - uvw.x, uvw.y);

            hit.voxel           = voxel;
            hit.voxelPosition   = ivec3(mapPos);
            hit.positionWorld   = hitWorldPos;
            hit.texCoord        = texCoord;
            hit.flatNormalWorld = normal;
            return true;
          }
        }
#endif
      }

      return false;
  };



  //TraceRay({0.5f, 1.5f, 0.f}, {.864f, -0.503f, 0.f}, 100);

  OnFramebufferResize(head_->windowFramebufferWidth, head_->windowFramebufferHeight);
}

VoxelRenderer::~VoxelRenderer()
{
  ZoneScoped;
  g_testMesh = {};
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
  for (auto&& [entity, inputLook, transform, player] : world.GetRegistry().view<InputLookState, Transform, Player>().each())
  {
    // TODO: use better way of seeing which player we own
    if (player.id == 0)
    {
      position = transform.position;
      // Flip z axis to correspond with Vulkan's NDC space.
      auto rotationQuat = transform.rotation;
      if (auto* renderTransform = world.GetRegistry().try_get<RenderTransform>(entity))
      {
        // Because the player has its own variable delta pitch and yaw updates, we only care about smoothing positions here
        position = renderTransform->transform.position;
      }
      auto rotation = glm::mat4(glm::vec4(1, 0, 0, 0), glm::vec4(0, 1, 0, 0), glm::vec4(0, 0, -1, 0), glm::vec4(0, 0, 0, 1)) * glm::mat4_cast(glm::inverse(rotationQuat));
      viewMat  = glm::translate(rotation, -position);
      break;
    }
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

  auto meshUniformzVec = std::vector<Temp::ObjectUniforms>();
  for (auto&& [entity, transform] : world.GetRegistry().view<Transform, TempMesh>().each())
  {
    Transform actualTransform = transform;
    if (auto* renderTransform = world.GetRegistry().try_get<RenderTransform>(entity))
    {
      actualTransform = renderTransform->transform;
    }
    auto worldFromObject = glm::translate(glm::mat4(1), actualTransform.position) * glm::mat4_cast(actualTransform.rotation) * glm::scale(glm::mat4(1), glm::vec3(actualTransform.scale));
    meshUniformzVec.emplace_back(worldFromObject, g_testMesh.vertexBuffer->GetDeviceAddress());
  }

  auto meshUniformz = Fvog::TypedBuffer<Temp::ObjectUniforms>({
    .count = (uint32_t)meshUniformzVec.size(),
    .flag  = Fvog::BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE,
  });

  memcpy(meshUniformz.GetMappedMemory(), meshUniformzVec.data(), meshUniformzVec.size() * sizeof(Temp::ObjectUniforms));

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
    if (!lines.empty())
    {
      if (!lineVertexBuffer || lineVertexBuffer->Size() < lines.size() * sizeof(Debug::Line))
      {
        lineVertexBuffer.emplace((uint32_t)lines.size(), "Debug Lines");
      }
      lineVertexBuffer->UpdateData(commandBuffer, lines);
    }
#endif

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
      .clearValue = {.depth = 0},
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
        //.outputImage                = mainImage->ImageView().GetImage2D(),
      });
      ctx.BindIndexBuffer(g_testMesh.indexBuffer.value(), 0, VK_INDEX_TYPE_UINT32);
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
      });
      ctx.DrawIndexed((uint32_t)g_testMesh.indices.size(), (uint32_t)meshUniformz.Size(), 0, 0, 0);
      
      ctx.BindGraphicsPipeline(debugLinesPipeline.GetPipeline());
      ctx.SetPushConstants(DebugLinesPushConstants{
        .vertexBufferIndex   = lineVertexBuffer->GetDeviceBuffer().GetResourceHandle().index,
        .globalUniformsIndex = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
        .useGpuVertexBuffer  = 0,
      });
      ctx.Draw(uint32_t(lines.size() * 2), 1, 0, 0);
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

void VoxelRenderer::OnGui([[maybe_unused]] double dt, World& world, [[maybe_unused]] VkCommandBuffer commandBuffer)
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
    if (ImGui::Begin("Test"))
    {
      GetPipelineManager().PollModifiedShaders();
      GetPipelineManager().EnqueueModifiedShaders();

      //auto& mainCamera = world.GetSingletonComponent<Temp::View>();
      ImGui::Text("Framerate: %.0f (%.2fms)", 1 / dt, dt * 1000);
      //ImGui::Text("Camera pos: (%.2f, %.2f, %.2f)", mainCamera.position.x, mainCamera.position.y, mainCamera.position.z);
      //ImGui::Text("Camera dir: (%.2f, %.2f, %.2f)", mainCamera.GetForwardDir().x, mainCamera.GetForwardDir().y, mainCamera.GetForwardDir().z);
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
    break;
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
          opened = ImGui::TreeNode("entity", "%u (%s)", e, s->name.c_str());
        }
        else
        {
          opened = ImGui::TreeNode("entity", "%u", e);
        }
        if (opened)
        {
          if (auto* t = registry.try_get<Transform>(e))
          {
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat3("Position", &t->position[0], 0.25f);
            ImGui::DragFloat4("Rotation", &t->rotation[0], 0.125f);
            ImGui::DragFloat("Scale", &t->scale, 0.125f);
          }
          if (auto* it = registry.try_get<InterpolatedTransform>(e))
          {
            ImGui::SeparatorText("InterpolatedTransform");
            ImGui::Text("Accumulator: %f", it->accumulator * world.GetRegistry().ctx().get<TickRate>().hz);
            const auto& tr = it->previousTransform;
            ImGui::Text("Position: %f, %f, %f", tr.position[0], tr.position[1], tr.position[2]);
            ImGui::Text("Rotation: %f, %f, %f, %f", tr.rotation.w, tr.rotation.x, tr.rotation.y, tr.rotation.z);
            ImGui::Text("Scale: %f", tr.scale);
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
          if (auto* is = registry.try_get<InputState>(e))
          {
            ImGui::SeparatorText("InputState");
          }
          if (auto* ils = registry.try_get<InputLookState>(e))
          {
            ImGui::SeparatorText("InputLookState");
          }
          if (auto* rb = registry.try_get<Physics::RigidBody>(e))
          {
            ImGui::SeparatorText("RigidBody");
          }
          if (registry.all_of<TempMesh>(e))
          {
            ImGui::SeparatorText("TempMesh");
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

      ImGui::Text("Game state: %s", GameStateToStr(ctx.get<GameState>()));
      ImGui::Text("Time: %f", ctx.get<float>("time"_hs));

      ImGui::SliderFloat("Time Scale", &ctx.get<TimeScale>().scale, 0, 4, "%.2f", ImGuiSliderFlags_NoRoundToFormat);

      auto min = uint32_t(5);
      auto max = uint32_t(120);
      ImGui::SliderScalar("Tick Rate", ImGuiDataType_U32, &world.GetRegistry().ctx().get<TickRate>().hz, &min, &max, "%u");
    }
    ImGui::End();
  }
}
