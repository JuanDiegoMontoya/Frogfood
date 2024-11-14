#include "VoxelRenderer.h"
#include "VoxelRenderer.h"
#include "VoxelRenderer.h"

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

GridHierarchy::GridHierarchy(glm::ivec3 topLevelBrickDims)
  : buffer(1'000'000'000, "World"),
    topLevelBricksDims_(topLevelBrickDims),
    dimensions_(topLevelBricksDims_.x * TL_BRICK_VOXELS_PER_SIDE, topLevelBricksDims_.y * TL_BRICK_VOXELS_PER_SIDE, topLevelBricksDims_.z * TL_BRICK_VOXELS_PER_SIDE)
{
  ZoneScoped;
  numTopLevelBricks_ = topLevelBricksDims_.x * topLevelBricksDims_.y * topLevelBricksDims_.z;
  assert(topLevelBricksDims_.x > 0 && topLevelBricksDims_.y > 0 && topLevelBricksDims_.z > 0);
  
  topLevelBrickPtrs = buffer.Allocate(sizeof(TopLevelBrickPtr) * numTopLevelBricks_, sizeof(TopLevelBrickPtr));
  for (size_t i = 0; i < numTopLevelBricks_; i++)
  {
    auto& topLevelBrickPtr = buffer.GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + i];
    std::construct_at(&topLevelBrickPtr);
    topLevelBrickPtr = {.voxelsDoBeAllSame = true, .voxelIfAllSame = 0 };
    buffer.MarkDirtyPages(&topLevelBrickPtr);
  }
  topLevelBrickPtrsBaseIndex = uint32_t(topLevelBrickPtrs.offset / sizeof(TopLevelBrickPtr));
}

static_assert(std::is_trivially_constructible_v<GridHierarchy::TopLevelBrick>);
static_assert(std::is_trivially_constructible_v<GridHierarchy::TopLevelBrickPtr>);
static_assert(std::is_trivially_constructible_v<GridHierarchy::BottomLevelBrick>);
static_assert(std::is_trivially_constructible_v<GridHierarchy::BottomLevelBrickPtr>);

GridHierarchy::GridHierarchyCoords GridHierarchy::GetCoordsOfVoxelAt(glm::ivec3 voxelCoord)
{
  const auto topLevelCoord    = voxelCoord / TL_BRICK_VOXELS_PER_SIDE;
  const auto bottomLevelCoord = (voxelCoord / BL_BRICK_SIDE_LENGTH) % TL_BRICK_SIDE_LENGTH;
  const auto localVoxelCoord  = voxelCoord % BL_BRICK_SIDE_LENGTH;

  assert(glm::all(glm::lessThan(topLevelCoord, topLevelBricksDims_)));
  assert(glm::all(glm::lessThan(bottomLevelCoord, glm::ivec3(TL_BRICK_SIDE_LENGTH))));
  assert(glm::all(glm::lessThan(localVoxelCoord, glm::ivec3(BL_BRICK_SIDE_LENGTH))));

  return {topLevelCoord, bottomLevelCoord, localVoxelCoord};
}

GridHierarchy::voxel_t GridHierarchy::GetVoxelAt(glm::ivec3 voxelCoord)
{
  assert(glm::all(glm::greaterThanEqual(voxelCoord, glm::ivec3(0))));
  assert(glm::all(glm::lessThan(voxelCoord, dimensions_)));

  auto [topLevelCoord, bottomLevelCoord, localVoxelCoord] = GetCoordsOfVoxelAt(voxelCoord);

  const auto topLevelIndex = FlattenTopLevelBrickCoord(topLevelCoord);
  assert(topLevelIndex < numTopLevelBricks_);
  const auto& topLevelBrickPtr = buffer.GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];

  if (topLevelBrickPtr.voxelsDoBeAllSame)
  {
    return topLevelBrickPtr.voxelIfAllSame;
  }

  const auto bottomLevelIndex = FlattenBottomLevelBrickCoord(bottomLevelCoord);
  assert(bottomLevelIndex < CELLS_PER_TL_BRICK);
  const auto& bottomLevelBrickPtr = buffer.GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks[bottomLevelIndex];

  if (bottomLevelBrickPtr.voxelsDoBeAllSame)
  {
    return bottomLevelBrickPtr.voxelIfAllSame;
  }

  const auto localVoxelIndex = FlattenVoxelCoord(localVoxelCoord);
  assert(localVoxelIndex < CELLS_PER_BL_BRICK);
  return buffer.GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick].voxels[localVoxelIndex];
}

void GridHierarchy::SetVoxelAt(glm::ivec3 voxelCoord, voxel_t voxel)
{
  ZoneScoped;
  assert(glm::all(glm::greaterThanEqual(voxelCoord, glm::ivec3(0))));
  assert(glm::all(glm::lessThan(voxelCoord, dimensions_)));

  auto [topLevelCoord, bottomLevelCoord, localVoxelCoord] = GetCoordsOfVoxelAt(voxelCoord);

  const auto topLevelIndex = FlattenTopLevelBrickCoord(topLevelCoord);
  assert(topLevelIndex < numTopLevelBricks_);
  auto& topLevelBrickPtr = buffer.GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];

  if (topLevelBrickPtr.voxelsDoBeAllSame)
  {
    // Make a top-level brick
    topLevelBrickPtr = TopLevelBrickPtr{.voxelsDoBeAllSame = false, .topLevelBrick = AllocateTopLevelBrick()};
    buffer.MarkDirtyPages(&topLevelBrickPtr);
  }

  const auto bottomLevelIndex = FlattenBottomLevelBrickCoord(bottomLevelCoord);
  assert(bottomLevelIndex < CELLS_PER_TL_BRICK);
  assert(topLevelBrickPtr.topLevelBrick < buffer.SizeBytes() / sizeof(TopLevelBrick));
  auto& bottomLevelBrickPtr = buffer.GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks[bottomLevelIndex];

  if (bottomLevelBrickPtr.voxelsDoBeAllSame)
  {
    // Make a bottom-level brick
    bottomLevelBrickPtr = BottomLevelBrickPtr{.voxelsDoBeAllSame = false, .bottomLevelBrick = AllocateBottomLevelBrick()};
    buffer.MarkDirtyPages(&bottomLevelBrickPtr);
  }

  const auto localVoxelIndex = FlattenVoxelCoord(localVoxelCoord);
  assert(localVoxelIndex < CELLS_PER_BL_BRICK);
  assert(bottomLevelBrickPtr.bottomLevelBrick < buffer.SizeBytes() / sizeof(BottomLevelBrick));
  auto& dstVoxel = buffer.GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick].voxels[localVoxelIndex];
  dstVoxel = voxel;
  buffer.MarkDirtyPages(&dstVoxel);
}

int GridHierarchy::FlattenTopLevelBrickCoord(glm::ivec3 coord) const
{
  return (coord.z * topLevelBricksDims_.x * topLevelBricksDims_.y) + (coord.y * topLevelBricksDims_.x) + coord.x;
}

int GridHierarchy::FlattenBottomLevelBrickCoord(glm::ivec3 coord)
{
  return (coord.z * TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH) + (coord.y * TL_BRICK_SIDE_LENGTH) + coord.x;
}

int GridHierarchy::FlattenVoxelCoord(glm::ivec3 coord)
{
  return (coord.z * BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH) + (coord.y * BL_BRICK_SIDE_LENGTH) + coord.x;
}

uint32_t GridHierarchy::AllocateTopLevelBrick()
{
  ZoneScoped;
  // The alignment of the allocation should be the size of the object being allocated so it can be indexed from the base ptr
  auto allocation = buffer.Allocate(sizeof(TopLevelBrick), sizeof(TopLevelBrick));
  auto index = uint32_t(allocation.offset / sizeof(TopLevelBrick));
  topLevelBrickIndexToAlloc.emplace(index, allocation);
  // Initialize
  auto& top = buffer.GetBase<TopLevelBrick>()[index];
  std::construct_at(&top);
  for (auto& bottomLevelBrickPtr : top.bricks)
  {
    bottomLevelBrickPtr.voxelsDoBeAllSame = true;
    bottomLevelBrickPtr.voxelIfAllSame    = 0;
  }
  buffer.MarkDirtyPages(&top);
  return index;
}

uint32_t GridHierarchy::AllocateBottomLevelBrick()
{
  ZoneScoped;
  auto allocation = buffer.Allocate(sizeof(BottomLevelBrick), sizeof(BottomLevelBrick));
  auto index      = uint32_t(allocation.offset / sizeof(BottomLevelBrick));
  bottomLevelBrickIndexToAlloc.emplace(index, allocation);
  // Initialize
  auto& bottom = buffer.GetBase<BottomLevelBrick>()[index];
  std::construct_at(&bottom);
  std::memset(&bottom, 0, sizeof(bottom));
  buffer.MarkDirtyPages(&bottom);
  return index;
}

VoxelRenderer::VoxelRenderer(const Application::CreateInfo& createInfo)
  : Application(createInfo)
{
  ZoneScoped;

  mainImage = Fvog::CreateTexture2D({windowFramebufferWidth, windowFramebufferHeight}, Fvog::Format::R8G8B8A8_UNORM, Fvog::TextureUsage::GENERAL, "Main Image");

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
            .colorAttachmentFormats = {Fvog::detail::VkToFormat(swapchainFormat_.format)},
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

void VoxelRenderer::OnUpdate([[maybe_unused]] double dt)
{
  ZoneScoped;
}

void VoxelRenderer::OnRender([[maybe_unused]] double dt, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex)
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

  auto t = (float)glfwGetTime();
  for (int i = 0; i < grid.dimensions_.x; i++)
    for (int j = 0; j < grid.dimensions_.y; j++)
      for (int k = 0; k < grid.dimensions_.z; k++)
      {
        glm::vec3 p      = {i, j, k};
        const auto left  = glm::vec3(50, 30, 20);
        const auto right = glm::vec3(90, 30, 20);
        const auto rot   = glm::mat3(glm::rotate(t, glm::vec3{1, 2, 3}));
        if ((glm::distance(p, rot * left) < 10) || (glm::distance(p, rot * right) < 10) || (p.y > 30 && glm::distance(glm::vec2(p.x, p.z), glm::vec2(70, 20)) < 10))
        {
          grid.SetVoxelAt({i, j, k}, 1);
        }
        else
        {
          grid.SetVoxelAt({i, j, k}, 0);
        }
      }

  grid.buffer.FlushWritesToGPU(commandBuffer);

  ctx.Barrier();

  const auto view_from_world = mainCamera.GetViewMatrix();
  //const auto clip_from_view = Math::InfReverseZPerspectiveRH(cameraFovyRadians, aspectRatio, cameraNearPlane);
  const auto clip_from_view = Math::InfReverseZPerspectiveRH(glm::radians(65.0f), (float)windowFramebufferWidth / windowFramebufferHeight, 0.1f);
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
      .cameraPos              = glm::vec4(mainCamera.position, 0),
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
  ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

  //const auto renderArea = VkRect2D{.offset = {}, .extent = {renderOutputWidth, renderOutputHeight}};
  const auto renderArea = VkRect2D{.offset = {}, .extent = {windowFramebufferWidth, windowFramebufferHeight}};
  vkCmdBeginRendering(commandBuffer,
    Fvog::detail::Address(VkRenderingInfo{
      .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea           = renderArea,
      .layerCount           = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments    = Fvog::detail::Address(VkRenderingAttachmentInfo{
           .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
           .imageView   = swapchainImageViews_[swapchainImageIndex],
           .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
           .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
           .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
           .clearValue  = {.color = VkClearColorValue{.float32 = {0, 0, 1, 1}}},
      }),
    }));

  //vkCmdSetViewport(commandBuffer, 0, 1, Fvog::detail::Address(VkViewport{0, 0, (float)renderOutputWidth, (float)renderOutputHeight, 0, 1}));
  vkCmdSetViewport(commandBuffer, 0, 1, Fvog::detail::Address(VkViewport{0, 0, (float)windowFramebufferWidth, (float)windowFramebufferHeight, 0, 1}));
  vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);
  ctx.BindGraphicsPipeline(debugTexturePipeline.GetPipeline());
  auto pushConstants = Temp::DebugTextureArguments{
    .textureIndex = mainImage->ImageView().GetSampledResourceHandle().index,
    .samplerIndex = nearestSampler.GetResourceHandle().index,
  };

  ctx.SetPushConstants(pushConstants);
  ctx.Draw(3, 1, 0, 0);

  vkCmdEndRendering(commandBuffer);

  ctx.ImageBarrier(swapchainImages_[swapchainImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void VoxelRenderer::OnGui([[maybe_unused]] double dt, [[maybe_unused]] VkCommandBuffer commandBuffer)
{
  ZoneScoped;
  if (ImGui::Begin("Test"))
  {
    GetPipelineManager().PollModifiedShaders();
    GetPipelineManager().EnqueueModifiedShaders();

    ImGui::Text("Framerate: %.0f (%.2fms)", 1 / dt, dt * 1000);
    ImGui::Text("Camera pos: (%.2f, %.2f, %.2f)", mainCamera.position.x, mainCamera.position.y, mainCamera.position.z);
    ImGui::Text("Camera dir: (%.2f, %.2f, %.2f)", mainCamera.GetForwardDir().x, mainCamera.GetForwardDir().y, mainCamera.GetForwardDir().z);
    VmaStatistics stats{};
    vmaGetVirtualBlockStatistics(grid.buffer.GetAllocator(), &stats);
    auto [usedSuffix, usedDivisor] = Math::BytesToSuffixAndDivisor(stats.allocationBytes);
    auto [blockSuffix, blockDivisor] = Math::BytesToSuffixAndDivisor(stats.blockBytes);
    ImGui::Text("Voxel memory: %.2f %s / %.2f %s", stats.allocationBytes / usedDivisor, usedSuffix, stats.blockBytes / blockDivisor, blockSuffix);
  }
  ImGui::End();
}

void VoxelRenderer::OnPathDrop([[maybe_unused]] std::span<const char*> paths)
{
  ZoneScoped;
}
