#include "TwoLevelGridShape.h"

#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Geometry/AABox.h"
#include "Jolt/Geometry/Sphere.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/component_wise.hpp"

#include <cassert>

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
  const auto boxShape = JPH::Ref(new JPH::BoxShape({0.5f - 1e-3f, 0.5f - 1e-3f, 0.5f - 1e-3f}));
  for (int z = (int)std::floor(s2min.GetZ()); z < (int)std::ceil(s2max.GetZ()); z++)
  for (int y = (int)std::floor(s2min.GetY()); y < (int)std::ceil(s2max.GetY()); y++)
  for (int x = (int)std::floor(s2min.GetX()); x < (int)std::ceil(s2max.GetX()); x++)
  {
    // Skip voxel if non-solid
    if (s1->twoLevelGrid_->GetVoxelAt({x, y, z}) == 0)
    {
      continue;
    }

    const auto boxCenterOfMassTransform = inCenterOfMassTransform1.PreTranslated({x + 0.5f, y + 0.5f, z + 0.5f});
    JPH::CollisionDispatch::sCollideShapeVsShape(boxShape,
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
  JPH::Mat44Arg inCenterOfMassTransform2,
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
  //shapeCastSettings2.mReturnDeepestPoint = true;

  // Test cast shape against every voxel AABB in its bounds
  const auto castMin  = castBoundsWorldSpace.GetCenter() - castBoundsWorldSpace.GetExtent();
  const auto castMax  = castBoundsWorldSpace.GetCenter() + castBoundsWorldSpace.GetExtent();
  printf("castMin = %d, %d, %d. castMax = %d, %d, %d\n",
    (int)std::floor(castMin.GetX()),
    (int)std::floor(castMin.GetY()),
    (int)std::floor(castMin.GetZ()),
    (int)std::ceil(castMax.GetX()),
    (int)std::ceil(castMax.GetY()),
    (int)std::ceil(castMax.GetZ()));
  const auto boxShape = JPH::Ref(new JPH::BoxShape({0.5f - 1e-3f, 0.5f - 1e-3f, 0.5f - 1e-3f}));
  for (int z = (int)std::floor(castMin.GetZ()); z < (int)std::ceil(castMax.GetZ()); z++)
  for (int y = (int)std::floor(castMin.GetY()); y < (int)std::ceil(castMax.GetY()); y++)
  for (int x = (int)std::floor(castMin.GetX()); x < (int)std::ceil(castMax.GetX()); x++)
  {
    // Skip voxel if non-solid
    if (s2->twoLevelGrid_->GetVoxelAt({x, y, z}) == 0)
    {
      continue;
    }

    const auto boxCenterOfMassTransform = inCenterOfMassTransform2.PreTranslated({x + 0.5f, y + 0.5f, z + 0.5f});
    //JPH::CollisionDispatch::sCastShapeVsShapeWorldSpace(inShapeCast,
    JPH::CollisionDispatch::sCastShapeVsShapeLocalSpace(inShapeCast,
      shapeCastSettings2,
      boxShape,
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

void Physics::TwoLevelGridShape::CastRay([[maybe_unused]] const JPH::RayCast& inRay,
  [[maybe_unused]] const JPH::RayCastSettings& inRayCastSettings,
  [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator,
  [[maybe_unused]] JPH::CastRayCollector& ioCollector,
  [[maybe_unused]] const JPH::ShapeFilter& inShapeFilter) const
{
  assert(false);
}

bool Physics::TwoLevelGridShape::CastRay([[maybe_unused]] const JPH::RayCast& inRay,
  [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator,
  [[maybe_unused]] JPH::RayCastResult& ioHit) const
{
  assert(false);
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
  [[maybe_unused]] JPH::Vec3& outCenterOfBuoyancy) const
{
  assert(false);
}

JPH::Vec3 Physics::TwoLevelGridShape::GetSurfaceNormal([[maybe_unused]] const JPH::SubShapeID& inSubShapeID, [[maybe_unused]] JPH::Vec3Arg inLocalSurfacePosition) const
{

  // This is brittle. It requires the surface position to be slightly *inside* the voxel, but since voxels share surface positions it's highly subject to floating-point error.
  // TODO: To be more robust, check both voxels on the edge (from the component that is nearest to an integer), then use the position of the solid one.
  const auto voxel = JPH::Vec3(std::floor(inLocalSurfacePosition[0]), std::floor(inLocalSurfacePosition[1]), std::floor(inLocalSurfacePosition[2]));
  const auto dir   = (inLocalSurfacePosition - (voxel + JPH::Vec3::sReplicate(0.5f)));
  const auto idx   = dir.Abs().GetHighestComponentIndex();
  auto normal      = JPH::Vec3::sReplicate(0);
  normal.SetComponent(idx, 1 * dir.GetSign()[idx]);
  //printf("%f, %f, %f\n", normal[0], normal[1], normal[2]);
  //printf("%f, %f, %f\n", dir[0], dir[1], dir[2]);
  return normal;
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
