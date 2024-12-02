#include "Physics.h"
#include "Game.h"

#include "TwoLevelGrid.h"

#include "Jolt/Jolt.h"
#include "Jolt/RegisterTypes.h"
#include "Jolt/Core/Factory.h"
#include "Jolt/Core/TempAllocator.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/Physics/PhysicsSettings.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/MeshShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/BodyActivationListener.h"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#include "Jolt/Physics/Character/Character.h"
#include "Jolt/Physics/Collision/ContactListener.h"
#include "Jolt/Physics/Collision/CollisionDispatch.h"

#include "entt/entity/handle.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/component_wise.hpp"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

#include <shared_mutex>
#include <mutex>
#include <vector>

namespace Physics
{
  namespace
  {
    JPH::Vec3 ToJolt(glm::vec3 v)
    {
      return JPH::Vec3(v.x, v.y, v.z);
    }

    JPH::Quat ToJolt(glm::quat q)
    {
      return JPH::Quat(q.x, q.y, q.z, q.w);
    }

    glm::vec3 ToGlm(JPH::Vec3 v)
    {
      return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
    }

    glm::quat ToGlm(JPH::Quat q)
    {
      return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }

    namespace BroadPhaseLayers
    {
      constexpr JPH::BroadPhaseLayer NON_MOVING(0);
      constexpr JPH::BroadPhaseLayer MOVING(1);
      constexpr JPH::uint NUM_LAYERS(2);
    }; // namespace BroadPhaseLayers

    class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
    {
    public:
      bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
      {
        switch (inObject1)
        {
        case Layers::NON_MOVING: return inObject2 == Layers::MOVING; // Non moving only collides with moving
        case Layers::MOVING: return true;                            // Moving collides with everything
        default: JPH_ASSERT(false); return false;
        }
      }
    };

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
    public:
      BPLayerInterfaceImpl()
      {
        // Create a mapping table from object to broad phase layer
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING]     = BroadPhaseLayers::MOVING;
      }

      JPH::uint GetNumBroadPhaseLayers() const override
      {
        return BroadPhaseLayers::NUM_LAYERS;
      }

      JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
      {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return mObjectToBroadPhase[inLayer];
      }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
      const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
      {
        switch ((JPH::BroadPhaseLayer::Type)inLayer)
        {
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
        default: JPH_ASSERT(false); return "INVALID";
        }
      }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

    private:
      JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
      bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
      {
        switch (inLayer1)
        {
        case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING: return true;
        default: JPH_ASSERT(false); return false;
        }
      }
    };

    class TwoLevelGridShape final : public JPH::Shape
    {
    public:
      TwoLevelGridShape(const TwoLevelGrid& twoLevelGrid)
        : Shape(JPH::EShapeType::User1, JPH::EShapeSubType::User1),
          twoLevelGrid_(&twoLevelGrid)
      {
        
      }

      const TwoLevelGrid* twoLevelGrid_;

      static void CollideTwoLevelGrid([[maybe_unused]] const Shape* inShape1,
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

      static void CastTwoLevelGrid([[maybe_unused]] const JPH::ShapeCast& inShapeCast,
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

      static void sRegister()
      {
        JPH::CollisionDispatch::sRegisterCollideShape(JPH::EShapeSubType::User1, JPH::EShapeSubType::Box, CollideTwoLevelGrid);
        JPH::CollisionDispatch::sRegisterCastShape(JPH::EShapeSubType::User1, JPH::EShapeSubType::Box, CastTwoLevelGrid);
        JPH::CollisionDispatch::sRegisterCollideShape(JPH::EShapeSubType::Box, JPH::EShapeSubType::User1, JPH::CollisionDispatch::sReversedCollideShape);
        JPH::CollisionDispatch::sRegisterCastShape(JPH::EShapeSubType::Box, JPH::EShapeSubType::User1, JPH::CollisionDispatch::sReversedCastShape);
      }

      void CastRay([[maybe_unused]] const JPH::RayCast& inRay,
        [[maybe_unused]] const JPH::RayCastSettings& inRayCastSettings,
        [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator,
        [[maybe_unused]] JPH::CastRayCollector& ioCollector,
        [[maybe_unused]] const JPH::ShapeFilter& inShapeFilter) const override
      {
        assert(false);
      }

      bool CastRay([[maybe_unused]] const JPH::RayCast& inRay,
        [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator,
        [[maybe_unused]] JPH::RayCastResult& ioHit) const override
      {
        assert(false);
      }

      void CollidePoint([[maybe_unused]] JPH::Vec3Arg inPoint,
        [[maybe_unused]] const JPH::SubShapeIDCreator& inSubShapeIDCreator,
        [[maybe_unused]] JPH::CollidePointCollector& ioCollector,
        [[maybe_unused]] const JPH::ShapeFilter& inShapeFilter) const override
      {
        assert(false);
      }

      void CollideSoftBodyVertices([[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform,
        [[maybe_unused]] JPH::Vec3Arg inScale,
        [[maybe_unused]] const JPH::CollideSoftBodyVertexIterator& inVertices,
        [[maybe_unused]] JPH::uint inNumVertices,
        [[maybe_unused]] int inCollidingShapeIndex) const override
      {
        assert(false);
      }

      float GetInnerRadius() const override
      {
        return float(glm::compMin(twoLevelGrid_->dimensions_)) / 2.0f;
      }

      JPH::AABox GetLocalBounds() const override
      {
        return JPH::AABox(JPH::Vec3Arg(0, 0, 0), JPH::Vec3Arg((float)twoLevelGrid_->dimensions_.x, (float)twoLevelGrid_->dimensions_.y, (float)twoLevelGrid_->dimensions_.z));
      }

      JPH::MassProperties GetMassProperties() const override
      {
        assert(false);
        return {};
      }

      const JPH::PhysicsMaterial* GetMaterial([[maybe_unused]] const JPH::SubShapeID& inSubShapeID) const override
      {
        assert(false);
        return nullptr;
      }

      Stats GetStats() const override
      {
        return Stats(sizeof(*this), 0);
      }

      JPH::uint GetSubShapeIDBitsRecursive() const override
      {
        assert(0);
        return 0;
      }

      void GetSubmergedVolume([[maybe_unused]] JPH::Mat44Arg inCenterOfMassTransform,
        [[maybe_unused]] JPH::Vec3Arg inScale,
        [[maybe_unused]] const JPH::Plane& inSurface,
        [[maybe_unused]] float& outTotalVolume,
        [[maybe_unused]] float& outSubmergedVolume,
        [[maybe_unused]] JPH::Vec3& outCenterOfBuoyancy) const override
      {
        assert(false);
      }

      JPH::Vec3 GetSurfaceNormal([[maybe_unused]] const JPH::SubShapeID& inSubShapeID, [[maybe_unused]] JPH::Vec3Arg inLocalSurfacePosition) const override
      {
        assert(false);
        return {};
      }

      int GetTrianglesNext(GetTrianglesContext&, int, JPH::Float3*, const JPH::PhysicsMaterial**) const override
      {
        assert(false);
        return 0;
      }

      void GetTrianglesStart(GetTrianglesContext&, const JPH::AABox&, JPH::Vec3Arg, JPH::QuatArg, JPH::Vec3Arg) const override
      {
        assert(false);
      }

      float GetVolume() const override
      {
        assert(false);
        return 0;
      }
    private:

    };

    struct ContactPair
    {
      JPH::BodyID inBody1;
      JPH::BodyID inBody2;
    };

    struct BodyDeleter;

    struct StaticVars
    {
      std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
      std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
      std::unique_ptr<BPLayerInterfaceImpl> broadPhaseLayerInterface;
      std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> objectVsBroadPhaseLayerFilter;
      std::unique_ptr<ObjectLayerPairFilterImpl> objectVsObjectLayerFilter;
      std::unique_ptr<JPH::PhysicsSystem> engine;
      std::unique_ptr<struct ContactListenerImpl> contactListener;
      JPH::BodyInterface* bodyInterface{};
      std::shared_mutex contactListenerMutex;

      std::vector<std::unique_ptr<JPH::Body, BodyDeleter>> bodies;
      std::vector<ContactPair> contactPairs;
      constexpr static float targetRate = 1.0f / 60.0f;
    };
    std::unique_ptr<StaticVars> s;

    struct BodyDeleter
    {
      void operator()(JPH::Body* body)
      {
        s->bodyInterface->RemoveBody(body->GetID());
        s->bodyInterface->DestroyBody(body->GetID());
      }
    };

    struct ContactListenerImpl final : JPH::ContactListener
    {
      ContactListenerImpl() = default;

      void OnContactAdded(const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        [[maybe_unused]] const JPH::ContactManifold& inManifold,
        [[maybe_unused]] JPH::ContactSettings& ioSettings) override
      {
        auto lock = std::unique_lock(s->contactListenerMutex);
        s->contactPairs.emplace_back(inBody1.GetID(), inBody2.GetID());
      }
    };
  }

  RigidBody& AddRigidBody(entt::handle handle, const RigidBodySettings& settings)
  {
    auto position = glm::vec3(0);
    auto rotation = glm::quat(1, 0, 0, 0);
    if (auto* t = handle.try_get<Transform>())
    {
      position = t->position;
      rotation = t->rotation;
    }

    auto bodyId = s->bodyInterface->CreateAndAddBody(
      JPH::BodyCreationSettings(settings.shape, ToJolt(position), ToJolt(rotation), settings.motionType, settings.layer),
      settings.activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    
    s->bodyInterface->SetUserData(bodyId, static_cast<JPH::uint64>(handle.entity()));

    return handle.emplace_or_replace<RigidBody>(bodyId);
  }

  void OnRigidBodyDestroy(entt::registry& registry, entt::entity entity)
  {
    auto& p = registry.get<RigidBody>(entity);
    s->bodyInterface->RemoveBody(p.body);
    s->bodyInterface->DestroyBody(p.body);
  }

  void Initialize(World& world)
  {
    world.GetRegistry().on_destroy<RigidBody>().connect<&OnRigidBodyDestroy>();
    s = std::make_unique<StaticVars>();

    JPH::RegisterDefaultAllocator();
    // JPH::Trace =
    // JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = )
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    TwoLevelGridShape::sRegister();

    s->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    s->jobSystem     = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

    s->broadPhaseLayerInterface      = std::make_unique<BPLayerInterfaceImpl>();
    s->objectVsBroadPhaseLayerFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
    s->objectVsObjectLayerFilter     = std::make_unique<ObjectLayerPairFilterImpl>();
    s->engine                        = std::make_unique<JPH::PhysicsSystem>();

    constexpr JPH::uint cMaxBodies             = 5000;
    constexpr JPH::uint cNumBodyMutexes        = 0;
    constexpr JPH::uint cMaxBodyPairs          = 5000;
    constexpr JPH::uint cMaxContactConstraints = 5000;

    s->engine->Init(cMaxBodies,
      cNumBodyMutexes,
      cMaxBodyPairs,
      cMaxContactConstraints,
      *s->broadPhaseLayerInterface,
      *s->objectVsBroadPhaseLayerFilter,
      *s->objectVsObjectLayerFilter);

    s->contactListener = std::make_unique<ContactListenerImpl>();
    s->engine->SetContactListener(s->contactListener.get());
    
    s->bodyInterface = &s->engine->GetBodyInterface();
  }

  void Terminate()
  {
    s.reset();
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
  }
  
  void FixedUpdate(float dt, World& world)
  {
    const auto substeps = static_cast<int>(std::ceil(dt / s->targetRate));
    s->engine->Update(dt, substeps, s->tempAllocator.get(), s->jobSystem.get());

    // Update transform of each entity with a RigidBody component
    for (auto&& [entity, rigidBody, transform] : world.GetRegistry().view<RigidBody, Transform>().each())
    {
      transform.position = ToGlm(s->bodyInterface->GetPosition(rigidBody.body));
      transform.rotation = ToGlm(s->bodyInterface->GetRotation(rigidBody.body));
    }
  }
} // namespace Physics
