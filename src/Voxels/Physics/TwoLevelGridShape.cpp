#include "TwoLevelGridShape.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/component_wise.hpp"

#include <cassert>

void Physics::TwoLevelGridShape::CollideTwoLevelGrid([[maybe_unused]] const Shape* inShape1,
  [[maybe_unused]] const Shape* inShape2,
  [[maybe_unused]] JPH::Vec3Arg inScale1,
  [[maybe_unused]] JPH::Vec3Arg inScale2,
  [[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform1,
  [[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform2,
  [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator1,
  [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator2,
  [[maybe_unused]] const JPH::CollideShapeSettings& inCollideShapeSettings,
  [[maybe_unused]] JPH::CollideShapeCollector& ioCollector,
  [[maybe_unused]] const JPH::ShapeFilter& inShapeFilter)
{
  assert(false);
}

void Physics::TwoLevelGridShape::CastTwoLevelGrid([[maybe_unused]] const JPH::ShapeCast& inShapeCast,
  [[maybe_unused]] const JPH::ShapeCastSettings& inShapeCastSettings,
  [[maybe_unused]] const JPH::Shape* inShape,
  [[maybe_unused]] JPH::Vec3Arg inScale,
  [[maybe_unused]] const JPH::ShapeFilter& inShapeFilter,
  [[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform2,
  [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator1,
  [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator2,
  [[maybe_unused]] JPH::CastShapeCollector& ioCollector)
{
  assert(false);
}

void Physics::TwoLevelGridShape::sRegister()
{
  JPH::CollisionDispatch::sRegisterCollideShape(JPH::EShapeSubType::User1, JPH::EShapeSubType::Sphere, CollideTwoLevelGrid);
  JPH::CollisionDispatch::sRegisterCastShape(JPH::EShapeSubType::User1, JPH::EShapeSubType::Sphere, CastTwoLevelGrid);
  JPH::CollisionDispatch::sRegisterCollideShape(JPH::EShapeSubType::Sphere, JPH::EShapeSubType::User1, JPH::CollisionDispatch::sReversedCollideShape);
  JPH::CollisionDispatch::sRegisterCastShape(JPH::EShapeSubType::Sphere, JPH::EShapeSubType::User1, JPH::CollisionDispatch::sReversedCastShape);
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
  assert(false);
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
  assert(false);
  return {};
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
