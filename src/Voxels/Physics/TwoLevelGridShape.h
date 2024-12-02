#pragma once
#include "Voxels/TwoLevelGrid.h"

#include "Jolt/Jolt.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"
#include "Jolt/Physics/Collision/CollideShape.h"
#include "Jolt/Physics/Collision/CollisionDispatch.h"


namespace Physics
{
  class TwoLevelGridShape final : public JPH::Shape
  {
  public:
    TwoLevelGridShape(const TwoLevelGrid& twoLevelGrid) : Shape(JPH::EShapeType::User1, JPH::EShapeSubType::User1), twoLevelGrid_(&twoLevelGrid) {}

    const TwoLevelGrid* twoLevelGrid_;

    static void CollideTwoLevelGrid(const Shape* inShape1,
      const Shape* inShape2,
      JPH::Vec3Arg inScale1,
      JPH::Vec3Arg inScale2,
      JPH::Mat44Arg inCenterOfMassTransform1,
      JPH::Mat44Arg inCenterOfMassTransform2,
      const JPH::SubShapeIDCreator& inSubShapeIDCreator1,
      const JPH::SubShapeIDCreator& inSubShapeIDCreator2,
      const JPH::CollideShapeSettings& inCollideShapeSettings,
      JPH::CollideShapeCollector& ioCollector,
      const JPH::ShapeFilter& inShapeFilter);

    static void CastTwoLevelGrid(const JPH::ShapeCast& inShapeCast,
      const JPH::ShapeCastSettings& inShapeCastSettings,
      const JPH::Shape* inShape,
      JPH::Vec3Arg inScale,
      const JPH::ShapeFilter& inShapeFilter,
      JPH::Mat44Arg inCenterOfMassTransform2,
      const JPH::SubShapeIDCreator& inSubShapeIDCreator1,
      const JPH::SubShapeIDCreator& inSubShapeIDCreator2,
      JPH::CastShapeCollector& ioCollector);

    static void sRegister();

    void CastRay(const JPH::RayCast& inRay,
      const JPH::RayCastSettings& inRayCastSettings,
      const JPH::SubShapeIDCreator& inSubShapeIDCreator,
      JPH::CastRayCollector& ioCollector,
      const JPH::ShapeFilter& inShapeFilter) const override;

    bool CastRay(const JPH::RayCast& inRay,
      const JPH::SubShapeIDCreator& inSubShapeIDCreator,
      JPH::RayCastResult& ioHit) const override;

    void CollidePoint(JPH::Vec3Arg inPoint,
      const JPH::SubShapeIDCreator& inSubShapeIDCreator,
      JPH::CollidePointCollector& ioCollector,
      const JPH::ShapeFilter& inShapeFilter) const override;

    void CollideSoftBodyVertices(JPH::Mat44Arg inCenterOfMassTransform,
      JPH::Vec3Arg inScale,
      const JPH::CollideSoftBodyVertexIterator& inVertices,
      JPH::uint inNumVertices,
      int inCollidingShapeIndex) const override;

    float GetInnerRadius() const override;

    JPH::AABox GetLocalBounds() const override;

    JPH::MassProperties GetMassProperties() const override;

    const JPH::PhysicsMaterial* GetMaterial(const JPH::SubShapeID& inSubShapeID) const override;

    Stats GetStats() const override;

    JPH::uint GetSubShapeIDBitsRecursive() const override;

    void GetSubmergedVolume(JPH::Mat44Arg inCenterOfMassTransform,
      JPH::Vec3Arg inScale,
      const JPH::Plane& inSurface,
      float& outTotalVolume,
      float& outSubmergedVolume,
      JPH::Vec3& outCenterOfBuoyancy) const override;

    JPH::Vec3 GetSurfaceNormal(const JPH::SubShapeID& inSubShapeID, JPH::Vec3Arg inLocalSurfacePosition) const override;

    int GetTrianglesNext(GetTrianglesContext& ioContext, int inMaxTrianglesRequested, JPH::Float3* outTriangleVertices, const JPH::PhysicsMaterial** outMaterials) const override;

    void GetTrianglesStart(GetTrianglesContext& ioContext, const JPH::AABox& inBox, JPH::Vec3Arg inPositionCOM, JPH::QuatArg inRotation, JPH::Vec3Arg inScale) const override;

    float GetVolume() const override;

  private:
  };
} // namespace Physics
