#pragma once
#include "ClassImplMacros.h"
#include "SketchyBuffer.h"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "ankerl/unordered_dense.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// Modifying operations are NOT thread-safe.
struct TwoLevelGrid
{
  static constexpr int BL_BRICK_SIDE_LENGTH = 8;
  static constexpr int CELLS_PER_BL_BRICK   = BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH;

  static constexpr int TL_BRICK_SIDE_LENGTH = 8;
  static constexpr int CELLS_PER_TL_BRICK   = TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH;

  static constexpr int VOXELS_PER_TL_BRICK      = CELLS_PER_TL_BRICK * CELLS_PER_BL_BRICK * CELLS_PER_TL_BRICK;
  static constexpr int TL_BRICK_VOXELS_PER_SIDE = TL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH;

  using voxel_t = uint32_t;

  struct OccupancyBitmask
  {
    bool Get(uint32_t index) const
    {
      assert(index < 512);
      return bitmask[index / 32u] & (1u << index % 32u);
    }

    void Set(uint32_t index, bool value)
    {
      assert(index < 512);
      auto& val = bitmask[index / 32u];
      val       = val & ~(1u << index % 32u);
      val |= static_cast<uint32_t>(value) << index % 32u;
    }

    uint32_t bitmask[16];
  };

  struct HitSurfaceParameters
  {
    voxel_t voxel;
    glm::vec3 voxelPosition;
    glm::vec3 positionWorld;
    glm::vec3 flatNormalWorld;
    glm::vec2 texCoords;
  };
  bool TraceRaySimple(glm::vec3 rayPosition, glm::vec3 rayDirection, float tMax, HitSurfaceParameters& hit) const;

  // The storage of a "chunk"
  struct BottomLevelBrick
  {
    OccupancyBitmask occupancy;
    voxel_t voxels[CELLS_PER_BL_BRICK];
  };

  struct BottomLevelBrickPtr
  {
    bool voxelsDoBeAllSame;
    union
    {
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
      uint32_t topLevelBrick;
      voxel_t voxelIfAllSame;
    };
  };

  explicit TwoLevelGrid(glm::ivec3 topLevelBrickDims);

  struct GridHierarchyCoords
  {
    glm::ivec3 topLevel;
    glm::ivec3 bottomLevel;
    glm::ivec3 localVoxel;
  };
  GridHierarchyCoords GetCoordsOfVoxelAt(glm::ivec3 voxelCoord) const;
  voxel_t GetVoxelAt(glm::ivec3 voxelCoord) const;
  void SetVoxelAt(glm::ivec3 voxelCoord, voxel_t voxel);
  void CoalesceBricksSLOW();
  void CoalesceDirtyBricks();
  void CoalesceTopLevelBrick(TopLevelBrickPtr& topLevelBrickPtr);
  void CoalesceBottomLevelBrick(BottomLevelBrickPtr& bottomLevelBrickPtr);

  int FlattenTopLevelBrickCoord(glm::ivec3 coord) const;
  static int FlattenBottomLevelBrickCoord(glm::ivec3 coord);
  static int FlattenVoxelCoord(glm::ivec3 coord);
  [[nodiscard]] uint32_t AllocateTopLevelBrick(voxel_t initialVoxel);
  [[nodiscard]] uint32_t AllocateBottomLevelBrick(voxel_t initialVoxel);
  void FreeTopLevelBrick(uint32_t index);
  void FreeBottomLevelBrick(uint32_t index);

  bool IsPositionInGrid(glm::ivec3 worldPos);

  struct Material
  {
    bool isVisible;
  };

  void SetMaterialArray(std::vector<Material> materials)
  {
    materials_ = std::move(materials);
  }

  // LOW LEVEL

  // Entrusts the caller with the responsibility of marking pages dirty.
  // Intended for world loading and other cases where mass modification is needed.
  void SetVoxelAtNoDirty(glm::ivec3 voxelCoord, voxel_t voxel);
  [[nodiscard]] uint32_t AllocateTopLevelBrickNoDirty(voxel_t initialVoxel);
  [[nodiscard]] uint32_t AllocateBottomLevelBrickNoDirty(voxel_t initialVoxel);
  void MarkTopLevelBrickAndChildrenDirty(glm::ivec3 topLevelBrickPos);

  SketchyBuffer buffer;
  SketchyBuffer::Alloc topLevelBrickPtrs{};
  uint32_t topLevelBrickPtrsBaseIndex{};
  size_t numTopLevelBricks_{};
  glm::ivec3 topLevelBricksDims_{};
  glm::ivec3 dimensions_{};

  #if 0
  std::unordered_map<uint32_t, SketchyBuffer::Alloc> topLevelBrickIndexToAlloc;
  std::unordered_map<uint32_t, SketchyBuffer::Alloc> bottomLevelBrickIndexToAlloc;

  // Used to determine which bricks to look at when coalescing the grid
  std::unordered_set<TopLevelBrickPtr*> dirtyTopLevelBricks;
  std::unordered_set<BottomLevelBrickPtr*> dirtyBottomLevelBricks;
  #else
  ankerl::unordered_dense::map<uint32_t, SketchyBuffer::Alloc> topLevelBrickIndexToAlloc;
  ankerl::unordered_dense::map<uint32_t, SketchyBuffer::Alloc> bottomLevelBrickIndexToAlloc;

  // Used to determine which bricks to look at when coalescing the grid
  ankerl::unordered_dense::set<TopLevelBrickPtr*> dirtyTopLevelBricks;
  ankerl::unordered_dense::set<BottomLevelBrickPtr*> dirtyBottomLevelBricks;
  #endif

  std::vector<Material> materials_;
};