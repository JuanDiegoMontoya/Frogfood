#pragma once
#include "Application.h"
#include "Fvog/Buffer2.h"
#include "Fvog/Texture2.h"
#include "PipelineManager.h"

#include "vk_mem_alloc.h"

#include <glm/vec3.hpp>

#include <memory>
#include <unordered_map>
#include <optional>

namespace Temp
{
  struct Uniforms
  {
    glm::mat4 viewProj;
    glm::mat4 oldViewProjUnjittered;
    glm::mat4 viewProjUnjittered;
    glm::mat4 invViewProj;
    glm::mat4 proj;
    glm::mat4 invProj;
    glm::vec4 cameraPos;
    glm::uint meshletCount;
    glm::uint maxIndices;
    float bindlessSamplerLodBias;
    glm::uint flags;
    float alphaHashScale;
  };

  FVOG_DECLARE_ARGUMENTS(PushConstants)
  {
    FVOG_IVEC3 topLevelBricksDims;
    FVOG_UINT32 topLevelBrickPtrsBaseIndex;
    FVOG_IVEC3 dimensions;
    FVOG_UINT32 bufferIdx;
    FVOG_UINT32 uniformBufferIndex;
    shared::Image2D outputImage;
  };

  FVOG_DECLARE_ARGUMENTS(DebugTextureArguments)
  {
    FVOG_UINT32 textureIndex;
    FVOG_UINT32 samplerIndex;
  };
} // namespace Temp

struct GridHierarchy
{
  static constexpr int BL_BRICK_SIDE_LENGTH = 8;
  static constexpr int CELLS_PER_BL_BRICK   = BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH;

  static constexpr int TL_BRICK_SIDE_LENGTH = 8;
  static constexpr int CELLS_PER_TL_BRICK   = TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH;

  static constexpr int VOXELS_PER_TL_BRICK      = CELLS_PER_TL_BRICK * CELLS_PER_BL_BRICK * CELLS_PER_TL_BRICK;
  static constexpr int TL_BRICK_VOXELS_PER_SIDE = TL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH;

  using voxel_t = uint32_t;

  // The storage of a "chunk"
  struct BottomLevelBrick
  {
    voxel_t voxels[CELLS_PER_BL_BRICK];
  };

  struct BottomLevelBrickPtr
  {
    bool voxelsDoBeAllSame;
    union
    {
      //BottomLevelBrick* bottomLevelBrick;
      uint32_t bottomLevelBrick;
      voxel_t voxelIfAllSame;
    };
  };

  struct TopLevelBrick
  {
    BottomLevelBrickPtr bricks[CELLS_PER_TL_BRICK];
  };

  struct TopLevelBrickPtr
  {
    bool voxelsDoBeAllSame;
    union
    {
      //TopLevelBrick* topLevelBrick;
      uint32_t topLevelBrick;
      voxel_t voxelIfAllSame;
    };
  };

  explicit GridHierarchy(glm::ivec3 topLevelBrickDims);

  struct GridHierarchyCoords
  {
    glm::ivec3 topLevel;
    glm::ivec3 bottomLevel;
    glm::ivec3 localVoxel;
  };
  GridHierarchyCoords GetCoordsOfVoxelAt(glm::ivec3 voxelCoord);
  voxel_t GetVoxelAt(glm::ivec3 voxelCoord);
  void SetVoxelAt(glm::ivec3 voxelCoord, voxel_t voxel);
  void CollapseGrids();

  int FlattenTopLevelBrickCoord(glm::ivec3 coord) const;
  static int FlattenBottomLevelBrickCoord(glm::ivec3 coord);
  static int FlattenVoxelCoord(glm::ivec3 coord);
  uint32_t AllocateTopLevelBrick();
  uint32_t AllocateBottomLevelBrick();
  void FreeTopLevelBrick(uint32_t index);
  void FreeBottomLevelBrick(uint32_t index);

  Fvog::ReplicatedBuffer buffer;
  //std::unique_ptr<TopLevelBrickPtr[]> topLevelBricks;
  Fvog::ReplicatedBuffer::Alloc topLevelBrickPtrs{};
  uint32_t topLevelBrickPtrsBaseIndex{};
  size_t numTopLevelBricks_{};
  glm::ivec3 topLevelBricksDims_{};
  glm::ivec3 dimensions_{};
  std::unordered_map<uint32_t, Fvog::ReplicatedBuffer::Alloc> topLevelBrickIndexToAlloc;
  std::unordered_map<uint32_t, Fvog::ReplicatedBuffer::Alloc> bottomLevelBrickIndexToAlloc;
};

class VoxelRenderer final : public Application
{
public:

  explicit VoxelRenderer(const Application::CreateInfo& createInfo);
  ~VoxelRenderer() override;

private:

  void OnFramebufferResize(uint32_t newWidth, uint32_t newHeight) override;
  void OnUpdate(double dt) override;
  void OnRender(double dt, VkCommandBuffer commandBuffer, uint32_t swapchainImageIndex) override;
  void OnGui(double dt, VkCommandBuffer commandBuffer) override;
  void OnPathDrop(std::span<const char*> paths) override;

  GridHierarchy grid{{1, 1, 1}};

  std::optional<Fvog::Texture> mainImage;
  Fvog::NDeviceBuffer<Temp::Uniforms> perFrameUniforms;
  PipelineManager::ComputePipelineKey testPipeline;
  PipelineManager::GraphicsPipelineKey debugTexturePipeline;
};
