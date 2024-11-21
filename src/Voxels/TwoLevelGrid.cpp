#include "TwoLevelGrid.h"

#include "tracy/Tracy.hpp"

#include "glm/vector_relational.hpp"

#include <type_traits>

// Assert that we can memset these types and produce them from a bag of bytes.
static_assert(std::is_trivially_constructible_v<TwoLevelGrid::TopLevelBrick>);
static_assert(std::is_trivially_constructible_v<TwoLevelGrid::TopLevelBrickPtr>);
static_assert(std::is_trivially_constructible_v<TwoLevelGrid::BottomLevelBrick>);
static_assert(std::is_trivially_constructible_v<TwoLevelGrid::BottomLevelBrickPtr>);

TwoLevelGrid::TwoLevelGrid(glm::ivec3 topLevelBrickDims)
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
    topLevelBrickPtr = {.voxelsDoBeAllSame = true, .voxelIfAllSame = 0};
#ifndef GAME_HEADLESS
    buffer.MarkDirtyPages(&topLevelBrickPtr);
#endif
  }
  topLevelBrickPtrsBaseIndex = uint32_t(topLevelBrickPtrs.offset / sizeof(TopLevelBrickPtr));
}

TwoLevelGrid::GridHierarchyCoords TwoLevelGrid::GetCoordsOfVoxelAt(glm::ivec3 voxelCoord)
{
  const auto topLevelCoord    = voxelCoord / TL_BRICK_VOXELS_PER_SIDE;
  const auto bottomLevelCoord = (voxelCoord / BL_BRICK_SIDE_LENGTH) % TL_BRICK_SIDE_LENGTH;
  const auto localVoxelCoord  = voxelCoord % BL_BRICK_SIDE_LENGTH;

  assert(glm::all(glm::lessThan(topLevelCoord, topLevelBricksDims_)));
  assert(glm::all(glm::lessThan(bottomLevelCoord, glm::ivec3(TL_BRICK_SIDE_LENGTH))));
  assert(glm::all(glm::lessThan(localVoxelCoord, glm::ivec3(BL_BRICK_SIDE_LENGTH))));

  return {topLevelCoord, bottomLevelCoord, localVoxelCoord};
}

TwoLevelGrid::voxel_t TwoLevelGrid::GetVoxelAt(glm::ivec3 voxelCoord)
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

void TwoLevelGrid::SetVoxelAt(glm::ivec3 voxelCoord, voxel_t voxel)
{
  // ZoneScoped;
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
#ifndef GAME_HEADLESS
    buffer.MarkDirtyPages(&topLevelBrickPtr);
#endif
  }

  const auto bottomLevelIndex = FlattenBottomLevelBrickCoord(bottomLevelCoord);
  assert(bottomLevelIndex < CELLS_PER_TL_BRICK);
  assert(topLevelBrickPtr.topLevelBrick < buffer.SizeBytes() / sizeof(TopLevelBrick));
  auto& bottomLevelBrickPtr = buffer.GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks[bottomLevelIndex];

  if (bottomLevelBrickPtr.voxelsDoBeAllSame)
  {
    // Make a bottom-level brick
    bottomLevelBrickPtr = BottomLevelBrickPtr{.voxelsDoBeAllSame = false, .bottomLevelBrick = AllocateBottomLevelBrick()};
#ifndef GAME_HEADLESS
    buffer.MarkDirtyPages(&bottomLevelBrickPtr);
#endif
  }

  const auto localVoxelIndex = FlattenVoxelCoord(localVoxelCoord);
  assert(localVoxelIndex < CELLS_PER_BL_BRICK);
  assert(bottomLevelBrickPtr.bottomLevelBrick < buffer.SizeBytes() / sizeof(BottomLevelBrick));
  auto& dstVoxel = buffer.GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick].voxels[localVoxelIndex];
  dstVoxel       = voxel;
#ifndef GAME_HEADLESS
  buffer.MarkDirtyPages(&dstVoxel);
#endif
  dirtyTopLevelBricks.insert(&topLevelBrickPtr);
  dirtyBottomLevelBricks.insert(&bottomLevelBrickPtr);
}

void TwoLevelGrid::CoalesceBricksSLOW()
{
  ZoneScoped;

  // Check all voxels of each bottom-level brick
  for (size_t i = 0; i < numTopLevelBricks_; i++)
  {
    auto& topLevelBrickPtr = buffer.GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + i];
    if (topLevelBrickPtr.voxelsDoBeAllSame)
    {
      continue;
    }

    for (auto& bottomLevelBrickPtr : buffer.GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks)
    {
      CoalesceBottomLevelBrick(bottomLevelBrickPtr);
    }
  }

  // Check just the top-level grids after collapsing bottom-levels
  for (size_t i = 0; i < numTopLevelBricks_; i++)
  {
    auto& topLevelBrickPtr = buffer.GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + i];
    if (topLevelBrickPtr.voxelsDoBeAllSame)
    {
      continue;
    }

    CoalesceTopLevelBrick(topLevelBrickPtr);
  }

  dirtyTopLevelBricks.clear();
  dirtyBottomLevelBricks.clear();
}

void TwoLevelGrid::CoalesceDirtyBricks()
{
  for (auto* bottomLevelBrickPtr : dirtyBottomLevelBricks)
  {
    CoalesceBottomLevelBrick(*bottomLevelBrickPtr);
  }

  for (auto* topLevelBrickPtr : dirtyTopLevelBricks)
  {
    CoalesceTopLevelBrick(*topLevelBrickPtr);
  }

  dirtyTopLevelBricks.clear();
  dirtyBottomLevelBricks.clear();
}

void TwoLevelGrid::CoalesceTopLevelBrick(TopLevelBrickPtr& topLevelBrickPtr)
{
  ZoneScoped;

  // Brick is already coalesced
  if (topLevelBrickPtr.voxelsDoBeAllSame)
  {
    return;
  }

  const voxel_t firstVoxel = buffer.GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks[0].voxelIfAllSame;
  for (auto& bottomLevelBrickPtr : buffer.GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks)
  {
    if (!bottomLevelBrickPtr.voxelsDoBeAllSame || bottomLevelBrickPtr.voxelIfAllSame != firstVoxel)
    {
      return;
    }
  }

  FreeTopLevelBrick(topLevelBrickPtr.topLevelBrick);
  topLevelBrickPtr.voxelsDoBeAllSame = true;
  topLevelBrickPtr.voxelIfAllSame    = firstVoxel;
#ifndef GAME_HEADLESS
  buffer.MarkDirtyPages(&topLevelBrickPtr);
#endif
}

void TwoLevelGrid::CoalesceBottomLevelBrick(BottomLevelBrickPtr& bottomLevelBrickPtr)
{
  ZoneScoped;

  // Brick is already coalesced
  if (bottomLevelBrickPtr.voxelsDoBeAllSame)
  {
    return;
  }

  const voxel_t firstVoxel = buffer.GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick].voxels[0];
  for (const auto& voxel : buffer.GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick].voxels)
  {
    if (firstVoxel != voxel)
    {
      return;
    }
  }

  FreeBottomLevelBrick(bottomLevelBrickPtr.bottomLevelBrick);
  bottomLevelBrickPtr.voxelsDoBeAllSame = true;
  bottomLevelBrickPtr.voxelIfAllSame    = firstVoxel;
#ifndef GAME_HEADLESS
  buffer.MarkDirtyPages(&bottomLevelBrickPtr);
#endif
}

int TwoLevelGrid::FlattenTopLevelBrickCoord(glm::ivec3 coord) const
{
  return (coord.z * topLevelBricksDims_.x * topLevelBricksDims_.y) + (coord.y * topLevelBricksDims_.x) + coord.x;
}

int TwoLevelGrid::FlattenBottomLevelBrickCoord(glm::ivec3 coord)
{
  return (coord.z * TL_BRICK_SIDE_LENGTH * TL_BRICK_SIDE_LENGTH) + (coord.y * TL_BRICK_SIDE_LENGTH) + coord.x;
}

int TwoLevelGrid::FlattenVoxelCoord(glm::ivec3 coord)
{
  return (coord.z * BL_BRICK_SIDE_LENGTH * BL_BRICK_SIDE_LENGTH) + (coord.y * BL_BRICK_SIDE_LENGTH) + coord.x;
}

uint32_t TwoLevelGrid::AllocateTopLevelBrick()
{
  ZoneScoped;
  // The alignment of the allocation should be the size of the object being allocated so it can be indexed from the base ptr
  auto allocation = buffer.Allocate(sizeof(TopLevelBrick), sizeof(TopLevelBrick));
  auto index      = uint32_t(allocation.offset / sizeof(TopLevelBrick));
  topLevelBrickIndexToAlloc.emplace(index, allocation);
  // Initialize
  auto& top = buffer.GetBase<TopLevelBrick>()[index];
  std::construct_at(&top);
  for (auto& bottomLevelBrickPtr : top.bricks)
  {
    bottomLevelBrickPtr.voxelsDoBeAllSame = true;
    bottomLevelBrickPtr.voxelIfAllSame    = 0;
  }
#ifndef GAME_HEADLESS
  buffer.MarkDirtyPages(&top);
#endif
  return index;
}

uint32_t TwoLevelGrid::AllocateBottomLevelBrick()
{
  ZoneScoped;
  auto allocation = buffer.Allocate(sizeof(BottomLevelBrick), sizeof(BottomLevelBrick));
  auto index      = uint32_t(allocation.offset / sizeof(BottomLevelBrick));
  bottomLevelBrickIndexToAlloc.emplace(index, allocation);
  // Initialize
  auto& bottom = buffer.GetBase<BottomLevelBrick>()[index];
  std::construct_at(&bottom);
  std::memset(&bottom, 0, sizeof(bottom));
#ifndef GAME_HEADLESS
  buffer.MarkDirtyPages(&bottom);
#endif
  return index;
}

void TwoLevelGrid::FreeTopLevelBrick(uint32_t index)
{
  auto it = topLevelBrickIndexToAlloc.find(index);
  assert(it != topLevelBrickIndexToAlloc.end());
  buffer.Free(it->second);
  topLevelBrickIndexToAlloc.erase(it);
}

void TwoLevelGrid::FreeBottomLevelBrick(uint32_t index)
{
  auto it = bottomLevelBrickIndexToAlloc.find(index);
  assert(it != bottomLevelBrickIndexToAlloc.end());
  buffer.Free(it->second);
  bottomLevelBrickIndexToAlloc.erase(it);
}
