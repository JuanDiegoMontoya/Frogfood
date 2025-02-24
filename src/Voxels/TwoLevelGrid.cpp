#include "TwoLevelGrid.h"

#include "tracy/Tracy.hpp"

#include "glm/vector_relational.hpp"
#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/vec4.hpp"

#include <type_traits>
#include <algorithm>
#include <mutex>

std::mutex STINKY_MUTEX;
//TracyLockable(std::mutex, STINKY_MUTEX); // Crashes Tracy client, possibly because I have too many threads.

// Assert that we can memset these types and produce them from a bag of bytes.
static_assert(std::is_trivially_constructible_v<TwoLevelGrid::TopLevelBrick>);
static_assert(std::is_trivially_constructible_v<TwoLevelGrid::TopLevelBrickPtr>);
static_assert(std::is_trivially_constructible_v<TwoLevelGrid::BottomLevelBrick>);
static_assert(std::is_trivially_constructible_v<TwoLevelGrid::BottomLevelBrickPtr>);

bool TwoLevelGrid::TraceRaySimple(glm::vec3 rayPosition, glm::vec3 rayDirection, float tMax, HitSurfaceParameters& hit) const
{
  using namespace glm;
  // https://www.shadertoy.com/view/X3BXDd
  vec3 mapPos = glm::floor(rayPosition); // integer cell coordinate of initial cell

  const vec3 deltaDist = 1.0f / abs(rayDirection); // ray length required to step from one cell border to the next in x, y and z directions

  const vec3 S       = vec3(step(0.0f, rayDirection)); // S is rayDir non-negative? 0 or 1
  const vec3 stepDir = 2.0f * S - 1.0f;                // Step sign

  // if 1./abs(rayDir[i]) is inf, then rayDir[i] is 0., but then S = step(0., rayDir[i]) is 1
  // so S cannot be 0. while deltaDist is inf, and stepDir * fract(pos) can never be 1.
  // Therefore we should not have to worry about getting NaN here :)

  // initial distance to cell sides, then relative difference between traveled sides
  vec3 sideDist = (S - stepDir * fract(rayPosition)) * deltaDist; // alternative: //sideDist = (S-stepDir * (pos - map)) * deltaDist;

  for (int i = 0; i < tMax; i++)
  {
    // Decide which way to go!
    vec4 conds = step(vec4(sideDist.x, sideDist.x, sideDist.y, sideDist.y), vec4(sideDist.y, sideDist.z, sideDist.z, sideDist.x)); // same as vec4(sideDist.xxyy <= sideDist.yzzx);

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

    // Putting the exit condition down here implicitly skips the first voxel
    if (all(greaterThanEqual(mapPos, vec3(0))) && all(lessThan(mapPos, vec3(dimensions_))))
    {
      const voxel_t voxel = GetVoxelAt(ivec3(mapPos));
      if (voxel != 0)
      {
        const vec3 p      = mapPos + 0.5f - stepDir * 0.5f; // Point on axis plane
        const vec3 normal = vec3(ivec3(vec3(cases))) * -vec3(stepDir);

        // Solve ray plane intersection equation: dot(n, ro + t * rd - p) = 0.
        // for t :
        const float t          = (dot(normal, p - rayPosition)) / dot(normal, rayDirection);
        const vec3 hitWorldPos = rayPosition + rayDirection * t;
        const vec3 uvw         = hitWorldPos - mapPos; // Don't use fract here

        hit.voxel           = voxel;
        hit.voxelPosition   = ivec3(mapPos);
        hit.positionWorld   = hitWorldPos;
        hit.texCoords       = {};//vx_GetTexCoords(normal, uvw);
        hit.flatNormalWorld = normal;
        return true;
      }
    }
  }

  return false;
}

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

TwoLevelGrid::GridHierarchyCoords TwoLevelGrid::GetCoordsOfVoxelAt(glm::ivec3 voxelCoord) const
{
  const auto topLevelCoord    = voxelCoord / TL_BRICK_VOXELS_PER_SIDE;
  const auto bottomLevelCoord = (voxelCoord / BL_BRICK_SIDE_LENGTH) % TL_BRICK_SIDE_LENGTH;
  const auto localVoxelCoord  = voxelCoord % BL_BRICK_SIDE_LENGTH;

  assert(glm::all(glm::lessThan(topLevelCoord, topLevelBricksDims_)));
  assert(glm::all(glm::lessThan(bottomLevelCoord, glm::ivec3(TL_BRICK_SIDE_LENGTH))));
  assert(glm::all(glm::lessThan(localVoxelCoord, glm::ivec3(BL_BRICK_SIDE_LENGTH))));

  return {topLevelCoord, bottomLevelCoord, localVoxelCoord};
}

TwoLevelGrid::voxel_t TwoLevelGrid::GetVoxelAt(glm::ivec3 voxelCoord) const
{
  //assert(glm::all(glm::greaterThanEqual(voxelCoord, glm::ivec3(0))));
  //assert(glm::all(glm::lessThan(voxelCoord, dimensions_)));
  if (glm::any(glm::lessThan(voxelCoord, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(voxelCoord, dimensions_)))
  {
    return 0;
  }

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
  assert(IsPositionInGrid(voxelCoord));

  auto [topLevelCoord, bottomLevelCoord, localVoxelCoord] = GetCoordsOfVoxelAt(voxelCoord);

  const auto topLevelIndex = FlattenTopLevelBrickCoord(topLevelCoord);
  assert(topLevelIndex < numTopLevelBricks_);
  auto& topLevelBrickPtr = buffer.GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];

  if (topLevelBrickPtr.voxelsDoBeAllSame)
  {
    // Make a top-level brick
    topLevelBrickPtr = TopLevelBrickPtr{.voxelsDoBeAllSame = false, .topLevelBrick = AllocateTopLevelBrick(topLevelBrickPtr.voxelIfAllSame)};
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
    bottomLevelBrickPtr = BottomLevelBrickPtr{.voxelsDoBeAllSame = false, .bottomLevelBrick = AllocateBottomLevelBrick(bottomLevelBrickPtr.voxelIfAllSame)};
#ifndef GAME_HEADLESS
    buffer.MarkDirtyPages(&bottomLevelBrickPtr);
#endif
  }

  const auto localVoxelIndex = FlattenVoxelCoord(localVoxelCoord);
  assert(localVoxelIndex < CELLS_PER_BL_BRICK);
  assert(bottomLevelBrickPtr.bottomLevelBrick < buffer.SizeBytes() / sizeof(BottomLevelBrick));
  auto& blBrick  = buffer.GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick];
  auto& dstVoxel = blBrick.voxels[localVoxelIndex] = voxel;
  blBrick.occupancy.Set(localVoxelIndex, materials_[voxel].isVisible);
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
  ZoneScoped;
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

uint32_t TwoLevelGrid::AllocateTopLevelBrick(voxel_t initialVoxel)
{
  ZoneScoped;

  uint32_t index;

  {
    auto lk = std::unique_lock(STINKY_MUTEX);
    // The alignment of the allocation should be the size of the object being allocated so it can be indexed from the base ptr
    auto allocation = buffer.Allocate(sizeof(TopLevelBrick), sizeof(TopLevelBrick));
    index           = uint32_t(allocation.offset / sizeof(TopLevelBrick));
    topLevelBrickIndexToAlloc.emplace(index, allocation);
  }

  // Initialize
  auto& top = buffer.GetBase<TopLevelBrick>()[index];
  std::construct_at(&top);
  std::ranges::fill(top.bricks, BottomLevelBrickPtr{.voxelsDoBeAllSame = true, .bottomLevelBrick = initialVoxel});
#ifndef GAME_HEADLESS
  auto lk = std::unique_lock(STINKY_MUTEX);
  buffer.MarkDirtyPages(&top);
#endif
  return index;
}

uint32_t TwoLevelGrid::AllocateBottomLevelBrick(voxel_t initialVoxel)
{
  ZoneScoped;
  
  uint32_t index;

  {
    auto lk         = std::unique_lock(STINKY_MUTEX);
    auto allocation = buffer.Allocate(sizeof(BottomLevelBrick), sizeof(BottomLevelBrick));
    index      = uint32_t(allocation.offset / sizeof(BottomLevelBrick));
    bottomLevelBrickIndexToAlloc.emplace(index, allocation);
  }

  // Initialize
  auto& bottom = buffer.GetBase<BottomLevelBrick>()[index];
  std::construct_at(&bottom);
  std::ranges::fill(bottom.voxels, initialVoxel);
  std::ranges::fill(bottom.occupancy.bitmask, materials_[initialVoxel].isVisible ? ~0u : 0u);
#ifndef GAME_HEADLESS
  auto lk = std::unique_lock(STINKY_MUTEX);
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

bool TwoLevelGrid::IsPositionInGrid(glm::ivec3 worldPos)
{
  return glm::all(glm::greaterThanEqual(worldPos, glm::ivec3(0))) && glm::all(glm::lessThan(worldPos, dimensions_));
}

void TwoLevelGrid::SetVoxelAtNoDirty(glm::ivec3 voxelCoord, voxel_t voxel)
{
  assert(glm::all(glm::greaterThanEqual(voxelCoord, glm::ivec3(0))));
  assert(glm::all(glm::lessThan(voxelCoord, dimensions_)));

  auto [topLevelCoord, bottomLevelCoord, localVoxelCoord] = GetCoordsOfVoxelAt(voxelCoord);

  const auto topLevelIndex = FlattenTopLevelBrickCoord(topLevelCoord);
  assert(topLevelIndex < numTopLevelBricks_);
  auto& topLevelBrickPtr = buffer.GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];

  if (topLevelBrickPtr.voxelsDoBeAllSame)
  {
    // Make a top-level brick
    topLevelBrickPtr = TopLevelBrickPtr{.voxelsDoBeAllSame = false, .topLevelBrick = AllocateTopLevelBrickNoDirty(topLevelBrickPtr.voxelIfAllSame)};
  }

  const auto bottomLevelIndex = FlattenBottomLevelBrickCoord(bottomLevelCoord);
  assert(bottomLevelIndex < CELLS_PER_TL_BRICK);
  assert(topLevelBrickPtr.topLevelBrick < buffer.SizeBytes() / sizeof(TopLevelBrick));
  auto& bottomLevelBrickPtr = buffer.GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick].bricks[bottomLevelIndex];

  if (bottomLevelBrickPtr.voxelsDoBeAllSame)
  {
    // Make a bottom-level brick
    bottomLevelBrickPtr = BottomLevelBrickPtr{.voxelsDoBeAllSame = false, .bottomLevelBrick = AllocateBottomLevelBrickNoDirty(bottomLevelBrickPtr.voxelIfAllSame)};
  }

  const auto localVoxelIndex = FlattenVoxelCoord(localVoxelCoord);
  assert(localVoxelIndex < CELLS_PER_BL_BRICK);
  assert(bottomLevelBrickPtr.bottomLevelBrick < buffer.SizeBytes() / sizeof(BottomLevelBrick));
  auto& blBrick  = buffer.GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick];
  blBrick.voxels[localVoxelIndex] = voxel;
  blBrick.occupancy.Set(localVoxelIndex, materials_[voxel].isVisible);
}

uint32_t TwoLevelGrid::AllocateTopLevelBrickNoDirty(voxel_t initialVoxel)
{
  ZoneScoped;

  uint32_t index;

  {
    auto lk = std::unique_lock(STINKY_MUTEX);
    // The alignment of the allocation should be the size of the object being allocated so it can be indexed from the base ptr
    auto allocation = buffer.Allocate(sizeof(TopLevelBrick), sizeof(TopLevelBrick));
    index           = uint32_t(allocation.offset / sizeof(TopLevelBrick));
    topLevelBrickIndexToAlloc.emplace(index, allocation);
  }

  // Initialize
  auto& top = buffer.GetBase<TopLevelBrick>()[index];
  std::construct_at(&top);
  std::ranges::fill(top.bricks, BottomLevelBrickPtr{.voxelsDoBeAllSame = true, .bottomLevelBrick = initialVoxel});
  return index;
}

uint32_t TwoLevelGrid::AllocateBottomLevelBrickNoDirty(voxel_t initialVoxel)
{
  ZoneScoped;

  uint32_t index;

  {
    auto lk         = std::unique_lock(STINKY_MUTEX);
    auto allocation = buffer.Allocate(sizeof(BottomLevelBrick), sizeof(BottomLevelBrick));
    index           = uint32_t(allocation.offset / sizeof(BottomLevelBrick));
    bottomLevelBrickIndexToAlloc.emplace(index, allocation);
  }

  // Initialize
  auto& bottom = buffer.GetBase<BottomLevelBrick>()[index];
  std::construct_at(&bottom);
  std::ranges::fill(bottom.voxels, initialVoxel);
  std::ranges::fill(bottom.occupancy.bitmask, materials_[initialVoxel].isVisible ? ~0u : 0u);
  return index;
}

void TwoLevelGrid::MarkTopLevelBrickAndChildrenDirty(glm::ivec3 topLevelBrickPos)
{
  ZoneScoped;
  const auto topLevelIndex = FlattenTopLevelBrickCoord(topLevelBrickPos);
  assert(topLevelIndex < numTopLevelBricks_);
  auto& topLevelBrickPtr = buffer.GetBase<TopLevelBrickPtr>()[topLevelBrickPtrsBaseIndex + topLevelIndex];

  auto lk = std::unique_lock(STINKY_MUTEX);
  buffer.MarkDirtyPages(&topLevelBrickPtr);
  dirtyTopLevelBricks.emplace(&topLevelBrickPtr);

  if (topLevelBrickPtr.voxelsDoBeAllSame)
  {
    return;
  }

  auto& topLevelBrick = buffer.GetBase<TopLevelBrick>()[topLevelBrickPtr.topLevelBrick];
  buffer.MarkDirtyPages(&topLevelBrick);
  for (auto& bottomLevelBrickPtr : topLevelBrick.bricks)
  {
    buffer.MarkDirtyPages(&bottomLevelBrickPtr);
    dirtyBottomLevelBricks.emplace(&bottomLevelBrickPtr);

    if (bottomLevelBrickPtr.voxelsDoBeAllSame)
    {
      continue;
    }

    const auto& bottomLevelBrick = buffer.GetBase<BottomLevelBrick>()[bottomLevelBrickPtr.bottomLevelBrick];
    buffer.MarkDirtyPages(&bottomLevelBrick);
  }
}
