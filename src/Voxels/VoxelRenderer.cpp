#include "VoxelRenderer.h"

#include "Assets.h"
#include "MathUtilities.h"
#include "PipelineManager.h"
#include "imgui.h"
#include "Fvog/Rendering2.h"
#include "Fvog/detail/Common.h"

#include "volk.h"
#include "Fvog/detail/ApiToEnum2.h"

#include "tracy/Tracy.hpp"
#include "GLFW/glfw3.h" // TODO: remove

#include <memory>
#include <type_traits>

float de1(glm::vec3 p0)
{
  using namespace glm;
  vec4 p = vec4(p0, 1.);
  for (int i = 0; i < 8; i++)
  {
    p.x   = mod(p.x - 1.0f, 2.0f) - 1.0f;
    p.y   = mod(p.y - 1.0f, 2.0f) - 1.0f;
    p.z   = mod(p.z - 1.0f, 2.0f) - 1.0f;
    p *= 1.4f / dot(vec3(p), vec3(p));
  }
  return length(vec2(p.x, p.z) / p.w) * 0.25f;
}

float de2(glm::vec3 p)
{
  using namespace glm;
  p           = {p.x, p.z, p.y};
  vec3 cSize  = vec3(1., 1., 1.3);
  float scale = 1.;
  for (int i = 0; i < 12; i++)
  {
    p        = 2.0f * clamp(p, -cSize, cSize) - p;
    float r2 = dot(p, p);
    float k  = max((2.f) / (r2), .027f);
    p *= k;
    scale *= k;
  }
  float l   = length(vec2(p.x, p.y));
  float rxy = l - 4.0f;
  float n   = l * p.z;
  rxy       = max(rxy, -(n) / 4.f);
  return (rxy) / abs(scale);
}

float de3(glm::vec3 p)
{
  float h = (sin(p.x * 0.11f) * 10 + 10) + (sin(p.z * 0.11f) * 10 + 10);
  return p.y > h;
}

VoxelRenderer::VoxelRenderer(PlayerHead* head, World&) : head_(head)
{
  ZoneScoped;

  head_->renderCallback_ = [this](float dt, World& world, VkCommandBuffer cmd, uint32_t swapchainImageIndex) { OnRender(dt, world, cmd, swapchainImageIndex); };
  head_->framebufferResizeCallback_ = [this](uint32_t newWidth, uint32_t newHeight) { OnFramebufferResize(newWidth, newHeight); };
  head_->guiCallback_ = [this](float dt, World& world, VkCommandBuffer cmd) { OnGui(dt, world, cmd); };

  // Top level bricks
  for (int k = 0; k < grid.topLevelBricksDims_.z; k++)
    for (int j = 0; j < grid.topLevelBricksDims_.y; j++)
      for (int i = 0; i < grid.topLevelBricksDims_.x; i++)
      {
        const auto tl = glm::ivec3{i, j, k};

        // Bottom level bricks
        for (int c = 0; c < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; c++)
          for (int b = 0; b < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; b++)
            for (int a = 0; a < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; a++)
            {
              const auto bl = glm::ivec3{a, b, c};

              // Voxels
              for (int z = 0; z < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; z++)
                for (int y = 0; y < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; y++)
                  for (int x = 0; x < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; x++)
                  {
                    const auto local = glm::ivec3{x, y, z};
                    const auto p     = tl * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE + bl * TwoLevelGrid::BL_BRICK_SIDE_LENGTH + local;
                    // const auto left  = glm::vec3(50, 30, 20);
                    // const auto right = glm::vec3(90, 30, 20);
                    // if ((glm::distance(p, left) < 10) || (glm::distance(p, right) < 10) || (p.y > 30 && glm::distance(glm::vec2(p.x, p.z), glm::vec2(70, 20)) < 10))
                    if (de2(glm::vec3(p) / 10.f + 2.0f) < 0.011f)
                    //if (de3(p) < 0.011f)
                    {
                      grid.SetVoxelAt(p, 1);
                    }
                    else
                    {
                      grid.SetVoxelAt(p, 0);
                    }
                  }
            }

        grid.CoalesceDirtyBricks();
      }

  mainImage =
    Fvog::CreateTexture2D({head_->windowFramebufferWidth, head_->windowFramebufferHeight}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::GENERAL, "Main Image");

  testPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
    .name = "Test pipeline",
    .shaderModuleInfo =
      PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
        .path  = GetShaderDirectory() / "voxels/SimpleRayTracer.comp.glsl",
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
}

VoxelRenderer::~VoxelRenderer()
{
  ZoneScoped;
  vkDeviceWaitIdle(Fvog::GetDevice().device_);

  Fvog::GetDevice().FreeUnusedResources();

//#if FROGRENDER_FSR2_ENABLE
//  if (!fsr2FirstInit)
//  {
//    ffxFsr2ContextDestroy(&fsr2Context);
//  }
//#endif
}

void VoxelRenderer::OnFramebufferResize([[maybe_unused]] uint32_t newWidth, [[maybe_unused]] uint32_t newHeight)
{
  ZoneScoped;
}

void VoxelRenderer::OnRender([[maybe_unused]] double dt, World& world, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
{
  ZoneScoped;

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

  grid.buffer.FlushWritesToGPU(commandBuffer);

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

  ctx.BindComputePipeline(testPipeline.GetPipeline());
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

  ctx.SetPushConstants(Temp::PushConstants{
    .topLevelBricksDims         = grid.topLevelBricksDims_,
    .topLevelBrickPtrsBaseIndex = grid.topLevelBrickPtrsBaseIndex,
    .dimensions                 = grid.dimensions_,
    .bufferIdx                  = grid.buffer.GetGpuBuffer().GetResourceHandle().index,
    .uniformBufferIndex         = perFrameUniforms.GetDeviceBuffer().GetResourceHandle().index,
    .outputImage                = mainImage->ImageView().GetImage2D(),
  });

  ctx.ImageBarrierDiscard(mainImage.value(), VK_IMAGE_LAYOUT_GENERAL);
  ctx.DispatchInvocations(mainImage->GetCreateInfo().extent);

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
    .textureIndex = mainImage->ImageView().GetSampledResourceHandle().index,
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
  auto& gameState = world.GetSingletonComponent<GameState>();
  switch (gameState)
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
        world.CreateSingletonComponent<CloseApplication>();
      }
    }
    ImGui::End();
    break;
  default:;
  }
}
