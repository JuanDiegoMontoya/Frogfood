#pragma once
#include "SketchyBuffer.h"
#include "glm/vec3.hpp"

#include <cstdint>
#include <unordered_map>

struct TwoLevelGrid
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
  GridHierarchyCoords GetCoordsOfVoxelAt(glm::ivec3 voxelCoord);
  voxel_t GetVoxelAt(glm::ivec3 voxelCoord);
  void SetVoxelAt(glm::ivec3 voxelCoord, voxel_t voxel);
  void CoalesceBricksSLOW();
  void CoalesceDirtyBricks();
  void CoalesceTopLevelBrick(TopLevelBrickPtr& topLevelBrickPtr);
  void CoalesceBottomLevelBrick(BottomLevelBrickPtr& bottomLevelBrickPtr);

  int FlattenTopLevelBrickCoord(glm::ivec3 coord) const;
  static int FlattenBottomLevelBrickCoord(glm::ivec3 coord);
  static int FlattenVoxelCoord(glm::ivec3 coord);
  uint32_t AllocateTopLevelBrick();
  uint32_t AllocateBottomLevelBrick();
  void FreeTopLevelBrick(uint32_t index);
  void FreeBottomLevelBrick(uint32_t index);

  SketchyBuffer buffer;
  SketchyBuffer::Alloc topLevelBrickPtrs{};
  uint32_t topLevelBrickPtrsBaseIndex{};
  size_t numTopLevelBricks_{};
  glm::ivec3 topLevelBricksDims_{};
  glm::ivec3 dimensions_{};
  std::unordered_map<uint32_t, SketchyBuffer::Alloc> topLevelBrickIndexToAlloc;
  std::unordered_map<uint32_t, SketchyBuffer::Alloc> bottomLevelBrickIndexToAlloc;

  // Used to determine which bricks to look at when coalescing the grid
  std::unordered_set<TopLevelBrickPtr*> dirtyTopLevelBricks;
  std::unordered_set<BottomLevelBrickPtr*> dirtyBottomLevelBricks;
};