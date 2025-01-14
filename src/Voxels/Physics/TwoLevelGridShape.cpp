#include "TwoLevelGridShape.h"

#include "PhysicsUtils.h"

#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Geometry/AABox.h"
#include "Jolt/Geometry/Sphere.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/geometric.hpp"
#include "glm/gtx/component_wise.hpp"

#include <cassert>

// Amount to shrink block size by.
constexpr float VX_EPSILON = 0;

// Amount by which to expand the AABB of shapes tested against the grid. This is a hack to make the player not stick to surfaces.
constexpr float VX_AABB_EPSILON = 1e-1f;

void Physics::TwoLevelGridShape::CollideTwoLevelGrid(const Shape* inShape1,
  const Shape* inShape2,
  JPH::Vec3Arg inScale1,
  JPH::Vec3Arg inScale2,
  JPH::Mat44Arg inCenterOfMassTransform1,
  JPH::Mat44Arg inCenterOfMassTransform2,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator1,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator2,
  const JPH::CollideShapeSettings& inCollideShapeSettings,
  JPH::CollideShapeCollector& ioCollector,
  const JPH::ShapeFilter& inShapeFilter)
{
  assert(inShape1->GetType() == JPH::EShapeType::User1);
  auto* s1 = static_cast<const TwoLevelGridShape*>(inShape1);
  
  const auto transform2_to_1 = inCenterOfMassTransform1.InversedRotationTranslation() * inCenterOfMassTransform2;
  const auto boundsOf2InSpaceOf1 = inShape2->GetLocalBounds().Scaled(inScale2).Transformed(transform2_to_1);

  // Test shape against every voxel AABB in its bounds
  // TODO: Investigate using AABox4.h to accelerate collision tests
  const auto s2min    = boundsOf2InSpaceOf1.GetCenter() - boundsOf2InSpaceOf1.GetExtent();
  const auto s2max    = boundsOf2InSpaceOf1.GetCenter() + boundsOf2InSpaceOf1.GetExtent();
  const auto boxShape = JPH::BoxShape({0.5f - VX_EPSILON, 0.5f - VX_EPSILON, 0.5f - VX_EPSILON});
  for (int z = (int)std::floor(s2min.GetZ() - VX_AABB_EPSILON); z < (int)std::ceil(s2max.GetZ() + VX_AABB_EPSILON); z++)
  for (int y = (int)std::floor(s2min.GetY() - VX_AABB_EPSILON); y < (int)std::ceil(s2max.GetY() + VX_AABB_EPSILON); y++)
  for (int x = (int)std::floor(s2min.GetX() - VX_AABB_EPSILON); x < (int)std::ceil(s2max.GetX() + VX_AABB_EPSILON); x++)
  {
    // Skip voxel if non-solid
    if (s1->twoLevelGrid_->GetVoxelAt({x, y, z}) == 0)
    {
      continue;
    }

    const auto boxCenterOfMassTransform = inCenterOfMassTransform1.PreTranslated({x + 0.5f, y + 0.5f, z + 0.5f});
    JPH::CollisionDispatch::sCollideShapeVsShape(&boxShape,
      inShape2,
      inScale1,
      inScale2,
      boxCenterOfMassTransform,
      inCenterOfMassTransform2,
      // If there's ever an assert tripped in Jolt's hash map (e.g. for very big shapes), this hack is probably the culprit.
      // Each collision with a different voxel needs a unique shape ID.
      inSubShapeIDCreator1.PushID(TwoLevelGrid::FlattenBottomLevelBrickCoord({x, y, z}), 16),
      inSubShapeIDCreator2,
      inCollideShapeSettings,
      ioCollector,
      inShapeFilter);
  }
}

// inShapeCast == any other shape
// inShape == TwoLevelGridShape
// We want the swept shape to be NOT TwoLevelGridShape (its swept shape would be horrific)
void Physics::TwoLevelGridShape::CastTwoLevelGrid(const JPH::ShapeCast& inShapeCast,
  const JPH::ShapeCastSettings& inShapeCastSettings,
  const JPH::Shape* inShape,
  JPH::Vec3Arg inScale,
  const JPH::ShapeFilter& inShapeFilter,
  [[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform2,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator1,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator2,
  JPH::CastShapeCollector& ioCollector)
{
  assert(inShape->GetType() == JPH::EShapeType::User1);
  auto* s2 = static_cast<const TwoLevelGridShape*>(inShape);

  const auto castBoundsWorldSpaceStart = inShapeCast.mShapeWorldBounds;
  const auto extent                    = inShapeCast.mShapeWorldBounds.GetExtent();
  const auto castBoundsWorldSpaceEnd =
    JPH::AABox::sFromTwoPoints(
      inShapeCast.mShapeWorldBounds.GetCenter() - extent + inShapeCast.mDirection,
      inShapeCast.mShapeWorldBounds.GetCenter() + extent + inShapeCast.mDirection);

  const auto min = JPH::Vec3::sMin(castBoundsWorldSpaceStart.GetCenter() - extent, castBoundsWorldSpaceEnd.GetCenter() - extent);
  const auto max = JPH::Vec3::sMax(castBoundsWorldSpaceStart.GetCenter() + extent, castBoundsWorldSpaceEnd.GetCenter() + extent);

  const auto castBoundsWorldSpace = JPH::AABox::sFromTwoPoints(min, max);

  auto shapeCastSettings2 = inShapeCastSettings;
  //shapeCastSettings2.mUseShrunkenShapeAndConvexRadius = true;
  //shapeCastSettings2.mReturnDeepestPoint = true;
  
  // Test cast shape against every voxel AABB in its bounds
  const auto castMin  = castBoundsWorldSpace.GetCenter() - castBoundsWorldSpace.GetExtent();
  const auto castMax  = castBoundsWorldSpace.GetCenter() + castBoundsWorldSpace.GetExtent();
  const auto boxShape = JPH::BoxShape({0.5f - VX_EPSILON, 0.5f - VX_EPSILON, 0.5f - VX_EPSILON});
  boxShape.SetEmbedded();
  for (int z = (int)std::floor(castMin.GetZ() - VX_AABB_EPSILON); z < (int)std::ceil(castMax.GetZ() + VX_AABB_EPSILON); z++)
  for (int y = (int)std::floor(castMin.GetY() - VX_AABB_EPSILON); y < (int)std::ceil(castMax.GetY() + VX_AABB_EPSILON); y++)
  for (int x = (int)std::floor(castMin.GetX() - VX_AABB_EPSILON); x < (int)std::ceil(castMax.GetX() + VX_AABB_EPSILON); x++)
  {
    // Skip voxel if non-solid
    if (s2->twoLevelGrid_->GetVoxelAt({x, y, z}) == 0)
    {
      continue;
    }

    auto negVec     = JPH::Vec3{-x - 0.5f, -y - 0.5f, -z - 0.5f};
    auto posVec     = JPH::Vec3{x + 0.5f, y + 0.5f, z + 0.5f};
    auto shapeCast2 = inShapeCast.PostTranslated(negVec);

    const auto boxCenterOfMassTransform = inCenterOfMassTransform2.PreTranslated(posVec);
    JPH::CollisionDispatch::sCastShapeVsShapeLocalSpace(shapeCast2,
      shapeCastSettings2,
      &boxShape,
      inScale,
      inShapeFilter,
      boxCenterOfMassTransform,
      inSubShapeIDCreator1,
      // If there's ever an assert tripped in Jolt's hash map (e.g. for very big shapes), this hack is probably the culprit.
      // Each collision with a different voxel needs a unique shape ID.
      inSubShapeIDCreator2.PushID(TwoLevelGrid::FlattenBottomLevelBrickCoord({x, y, z}), 16),
      ioCollector);
  }
}

void Physics::TwoLevelGridShape::sRegister()
{
  for (auto shapeSubType : JPH::sConvexSubShapeTypes)
  {
    JPH::CollisionDispatch::sRegisterCollideShape(JPH::EShapeSubType::User1, shapeSubType, CollideTwoLevelGrid);
    JPH::CollisionDispatch::sRegisterCastShape(JPH::EShapeSubType::User1, shapeSubType, JPH::CollisionDispatch::sReversedCastShape);
    JPH::CollisionDispatch::sRegisterCollideShape(shapeSubType, JPH::EShapeSubType::User1, JPH::CollisionDispatch::sReversedCollideShape);
    JPH::CollisionDispatch::sRegisterCastShape(shapeSubType, JPH::EShapeSubType::User1, CastTwoLevelGrid);
  }
}

void Physics::TwoLevelGridShape::CastRay(const JPH::RayCast& inRay,
  [[maybe_unused]] const JPH::RayCastSettings& inRayCastSettings,
  const JPH::SubShapeIDCreator& inSubShapeIDCreator,
  JPH::CastRayCollector& ioCollector,
  [[maybe_unused]] const JPH::ShapeFilter& inShapeFilter) const
{
  auto hit = TwoLevelGrid::HitSurfaceParameters();
  const auto direction = Physics::ToGlm(inRay.mDirection.Normalized());
  auto tMax = inRay.mDirection.Length();
  const auto origin = Physics::ToGlm(inRay.mOrigin);
  if (twoLevelGrid_->TraceRaySimple(origin, direction, 100, hit)) // TODO: fix tMax
  {
    auto id     = inSubShapeIDCreator.PushID(TwoLevelGrid::FlattenBottomLevelBrickCoord(glm::ivec3(hit.voxelPosition)), 16).GetID();
    auto result = JPH::CastRayCollector::ResultType();
    result.mSubShapeID2 = id;
    result.mBodyID      = inShapeFilter.mBodyID2;
    result.mFraction    = glm::distance(origin, hit.positionWorld) / glm::distance(origin, origin + direction * tMax);
    if (result.mFraction <= 1)
    {
      ioCollector.AddHit(result);
    }
  }
}

bool Physics::TwoLevelGridShape::CastRay([[maybe_unused]] const JPH::RayCast& inRay,
  [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator,
  [[maybe_unused]] JPH::RayCastResult& ioHit) const
{
  const auto origin    = Physics::ToGlm(inRay.mOrigin);
  const auto direction = glm::normalize(Physics::ToGlm(inRay.mDirection));
  const auto tMax      = inRay.mDirection.Length();
  auto hit = TwoLevelGrid::HitSurfaceParameters();
  if (twoLevelGrid_->TraceRaySimple(origin, direction, 100, hit)) // TODO: fix tMax
  {
    auto id            = inSubShapeIDCreator.PushID(TwoLevelGrid::FlattenBottomLevelBrickCoord(glm::ivec3(hit.voxelPosition)), 16).GetID();
    ioHit.mSubShapeID2 = id;
    ioHit.mBodyID      = {}; // TODO?
    ioHit.mFraction    = glm::distance(origin, hit.positionWorld) / glm::distance(origin, origin + direction * tMax);
    if (ioHit.mFraction <= 1)
    {
      return true;
    }
  }
  return false;
}

void Physics::TwoLevelGridShape::CollidePoint([[maybe_unused]] JPH::Vec3Arg inPoint,
  [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator,
  [[maybe_unused]] JPH::CollidePointCollector& ioCollector,
  [[maybe_unused]] const JPH::ShapeFilter& inShapeFilter) const
{
  assert(false);
}

void Physics::TwoLevelGridShape::CollideSoftBodyVertices([[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform,
  [[maybe_unused]] JPH::Vec3Arg inScale,
  [[maybe_unused]] const JPH::CollideSoftBodyVertexIterator& inVertices,
  [[maybe_unused]] JPH::uint inNumVertices,
  [[maybe_unused]] int inCollidingShapeIndex) const
{
  assert(false);
}

float Physics::TwoLevelGridShape::GetInnerRadius() const
{
  return float(glm::compMin(twoLevelGrid_->dimensions_)) / 2.0f;
}

JPH::AABox Physics::TwoLevelGridShape::GetLocalBounds() const
{
  return JPH::AABox(JPH::Vec3Arg(0, 0, 0),
    JPH::Vec3Arg((float)twoLevelGrid_->dimensions_.x, (float)twoLevelGrid_->dimensions_.y, (float)twoLevelGrid_->dimensions_.z));
}

JPH::MassProperties Physics::TwoLevelGridShape::GetMassProperties() const
{
  assert(false);
  return {};
}

const JPH::PhysicsMaterial* Physics::TwoLevelGridShape::GetMaterial([[maybe_unused]] const JPH::SubShapeID& inSubShapeID) const
{
  return nullptr;
}

JPH::Shape::Stats Physics::TwoLevelGridShape::GetStats() const
{
  return Stats(sizeof(*this), 0);
}

JPH::uint Physics::TwoLevelGridShape::GetSubShapeIDBitsRecursive() const
{
  assert(0);
  return 0;
}

void Physics::TwoLevelGridShape::GetSubmergedVolume([[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform,
  [[maybe_unused]] JPH::Vec3Arg inScale,
  [[maybe_unused]] const JPH::Plane& inSurface,
  [[maybe_unused]] float& outTotalVolume,
  [[maybe_unused]] float& outSubmergedVolume,
  [[maybe_unused]] JPH::Vec3& outCenterOfBuoyancy
#ifdef JPH_DEBUG_RENDERER
  , [[maybe_unused]] JPH::RVec3Arg inBaseOffset
#endif
) const
{
  assert(false);
}

JPH::Vec3 Physics::TwoLevelGridShape::GetSurfaceNormal([[maybe_unused]] const JPH::SubShapeID& inSubShapeID, [[maybe_unused]] JPH::Vec3Arg inLocalSurfacePosition) const
{
  // Check both voxels on the edge (from the component that is nearest to an integer), then use the position of the solid one.
  const auto inLocalPos     = inLocalSurfacePosition;
  const auto absDiffFromInt = JPH::Vec3(
    abs(inLocalPos.GetX() - round(inLocalPos.GetX())),
    abs(inLocalPos.GetY() - round(inLocalPos.GetY())),
    abs(inLocalPos.GetZ() - round(inLocalPos.GetZ())));
  const auto nearestIntCompIdx = absDiffFromInt.GetLowestComponentIndex();

  auto pos0 = inLocalPos;
  pos0.SetComponent(nearestIntCompIdx, floor(inLocalPos[nearestIntCompIdx]));

  auto pos1 = inLocalPos;
  pos1.SetComponent(nearestIntCompIdx, ceil(inLocalPos[nearestIntCompIdx]));

  const auto v0pos = glm::ivec3(glm::floor(ToGlm(pos0)));
  const auto v1pos = glm::ivec3(glm::floor(ToGlm(pos1)));

  auto v0 = twoLevelGrid_->GetVoxelAt(v0pos);
  //auto v1 = twoLevelGrid_->GetVoxelAt(glm::ivec3(ToGlm(pos1)));

  // Choose position of solid voxel, which is the one we're colliding with. If both voxels are solid (which shouldn't happen), pick an arbitrary position.
  auto solidVoxel = JPH::Vec3();
  [[maybe_unused]] auto airVoxel = JPH::Vec3();
  if (v0 != 0)
  {
    solidVoxel = ToJolt(v0pos);
    airVoxel   = ToJolt(v1pos);
  }
  else
  {
    solidVoxel = ToJolt(v1pos);
    airVoxel   = ToJolt(v0pos);
  }

  // Find the greatest component of the difference between the voxel pos and surface pos.
  const auto dir   = inLocalSurfacePosition - (solidVoxel + JPH::Vec3::sReplicate(0.5f));
  const auto idx   = dir.Abs().GetHighestComponentIndex();
  auto normal      = JPH::Vec3::sReplicate(0);
  normal.SetComponent(idx, dir.GetSign()[idx]);
  //printf("%f, %f, %f\n", normal[0], normal[1], normal[2]);
  //printf("%f, %f, %f\n", dir[0], dir[1], dir[2]);
  return -normal; // FIXME: For some reason, this needs to be negated for bullets to rest properly.
}

int Physics::TwoLevelGridShape::GetTrianglesNext([[maybe_unused]] GetTrianglesContext& ioContext,
  [[maybe_unused]] int inMaxTrianglesRequested,
  [[maybe_unused]] JPH::Float3* outTriangleVertices,
  [[maybe_unused]] const JPH::PhysicsMaterial** outMaterials) const
{
  assert(false);
  return 0;
}

void Physics::TwoLevelGridShape::GetTrianglesStart([[maybe_unused]] GetTrianglesContext& ioContext,
  [[maybe_unused]] const JPH::AABox& inBox,
  [[maybe_unused]] JPH::Vec3Arg inPositionCOM,
  [[maybe_unused]] JPH::QuatArg inRotation,
  [[maybe_unused]] JPH::Vec3Arg inScale) const
{
  assert(false);
}

float Physics::TwoLevelGridShape::GetVolume() const
{
  assert(false);
  return 0;
}

#ifdef JPH_DEBUG_RENDERER
void Physics::TwoLevelGridShape::Draw([[maybe_unused]] JPH::DebugRenderer* inRenderer,
  [[maybe_unused]] JPH::RMat44Arg inCenterOfMassTransform,
  [[maybe_unused]] JPH::Vec3Arg inScale,
  [[maybe_unused]] JPH::ColorArg inColor,
  [[maybe_unused]] bool inUseMaterialColors,
  [[maybe_unused]] bool inDrawWireframe) const
{
  //assert(false);
}
#endif
