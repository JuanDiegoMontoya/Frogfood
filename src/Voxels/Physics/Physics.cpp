#include "Physics.h"
#include "Voxels/Game.h"
#include "Voxels/TwoLevelGrid.h"

#include "PhysicsUtils.h"
#include "TwoLevelGridShape.h"

#ifdef JPH_DEBUG_RENDERER
#include "DebugRenderer.h"
#endif

#include "tracy/Tracy.hpp"

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
#include "Jolt/Physics/Collision/CollisionCollectorImpl.h"
#include "Jolt/Physics/Collision/ContactListener.h"
#include "Jolt/Physics/Collision/CollisionDispatch.h"

#include "entt/entity/handle.hpp"

#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

#include <shared_mutex>
#include <mutex>
#include <vector>

namespace Physics
{
  namespace
  {
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

    struct ContactPair
    {
      entt::entity entity1;
      entt::entity entity2;
    };

    struct StaticVars
    {
      std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
      std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
#ifdef JPH_DEBUG_RENDERER
      std::unique_ptr<DebugRenderer> debugRenderer;
#endif
      std::unique_ptr<BPLayerInterfaceImpl> broadPhaseLayerInterface;
      std::unique_ptr<ObjectVsBroadPhaseLayerFilterImpl> objectVsBroadPhaseLayerFilter;
      std::unique_ptr<ObjectLayerPairFilterImpl> objectVsObjectLayerFilter;
      std::unique_ptr<JPH::PhysicsSystem> engine;
      std::unique_ptr<struct ContactListenerImpl> contactListener;
      std::unique_ptr<struct CharacterContactListenerImpl> characterContactListener;
      std::unique_ptr<JPH::CharacterVsCharacterCollisionSimple> characterCollisionInterface;
      JPH::BodyInterface* bodyInterface{};
      std::shared_mutex contactListenerMutex;

      std::vector<JPH::CharacterVirtual*> allCharacters;
      std::vector<JPH::Character*> allCharactersShrimple;
      std::vector<ContactPair> contactPairs;
      // The deltaTime that is recommended by Jolt's docs to achieve a stable simulation.
      // The number of subticks is dynamic to keep the substep dt around this target.
      constexpr static float targetRate = 1.0f / 60.0f;

      glm::vec3 gravity = {0, -10, 0};
    };
    std::unique_ptr<StaticVars> s;

    struct ContactListenerImpl final : JPH::ContactListener
    {
      ContactListenerImpl() = default;

      void OnContactAdded(const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        [[maybe_unused]] const JPH::ContactManifold& inManifold,
        [[maybe_unused]] JPH::ContactSettings& ioSettings) override
      {
        auto lock = std::unique_lock(s->contactListenerMutex);
        s->contactPairs.emplace_back(static_cast<entt::entity>(inBody1.GetUserData()), static_cast<entt::entity>(inBody2.GetUserData()));
      }
    };

    struct CharacterContactListenerImpl final : JPH::CharacterContactListener
    {
      void OnContactAdded(const JPH::CharacterVirtual* inCharacter,
        const JPH::BodyID& inBodyID2,
        [[maybe_unused]] const JPH::SubShapeID& inSubShapeID2,
        [[maybe_unused]] JPH::RVec3Arg inContactPosition,
        [[maybe_unused]] JPH::Vec3Arg inContactNormal,
        [[maybe_unused]] JPH::CharacterContactSettings& ioSettings) override
      {
        auto lock = std::unique_lock(s->contactListenerMutex);
        auto e2 = static_cast<entt::entity>(s->bodyInterface->GetUserData(inBodyID2));
        s->contactPairs.emplace_back(static_cast<entt::entity>(inCharacter->GetUserData()), e2);
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

  CharacterController& AddCharacterController(entt::handle handle, const CharacterControllerSettings& settings)
  {
    auto position = glm::vec3(0);
    auto rotation = glm::quat(1, 0, 0, 0);
    if (auto* t = handle.try_get<Transform>())
    {
      position = t->position;
      rotation = t->rotation;
    }

    auto characterSettings = JPH::CharacterVirtualSettings();
    characterSettings.SetEmbedded();
    characterSettings.mShape = settings.shape;
    characterSettings.mEnhancedInternalEdgeRemoval = true;
    characterSettings.mPredictiveContactDistance   = 1e-3f;
    //characterSettings.mCharacterPadding = 0.00002f;
    //characterSettings.mPredictiveContactDistance = 0.22f;
    //characterSettings.mSupportingVolume = JPH::Plane(JPH::Vec3(0, 1, 0), -0.5f);
    // TODO: use mInnerBodyShape to give character a physical presence (to be detected by ray casts, etc.)
    auto* character = new JPH::CharacterVirtual(&characterSettings, ToJolt(position), ToJolt(rotation), static_cast<JPH::uint64>(handle.entity()), s->engine.get());
    character->SetListener(s->characterContactListener.get());

    s->allCharacters.emplace_back(character);
    s->characterCollisionInterface->Add(character);
    return handle.emplace_or_replace<CharacterController>(character);
  }

  CharacterControllerShrimple& AddCharacterControllerShrimple(entt::handle handle, const CharacterControllerShrimpleSettings& settings)
  {
    auto position = glm::vec3(0);
    auto rotation = glm::quat(1, 0, 0, 0);
    if (auto* t = handle.try_get<Transform>())
    {
      position = t->position;
      rotation = t->rotation;
    }

    auto characterSettings = JPH::CharacterSettings();
    characterSettings.SetEmbedded();
    characterSettings.mLayer = Layers::MOVING;
    characterSettings.mUp = {0, 1, 0};
    characterSettings.mShape = settings.shape;
    //characterSettings.mSupportingVolume = JPH::Plane(JPH::Vec3(0, 1, 0), -1);
    //characterSettings.mEnhancedInternalEdgeRemoval = true;
    auto* character = new JPH::Character(&characterSettings, ToJolt(position), ToJolt(rotation), static_cast<JPH::uint64>(handle.entity()), s->engine.get());
    character->AddToPhysicsSystem();
    s->bodyInterface->SetRestitution(character->GetBodyID(), 0);

    s->allCharactersShrimple.emplace_back(character);

    return handle.emplace_or_replace<CharacterControllerShrimple>(character);
  }

  static void OnRigidBodyDestroy(entt::registry& registry, entt::entity entity)
  {
    auto& p = registry.get<RigidBody>(entity);
    s->bodyInterface->RemoveBody(p.body);
    s->bodyInterface->DestroyBody(p.body);
  }

  static void OnCharacterControllerDestroy(entt::registry& registry, entt::entity entity)
  {
    auto& c = registry.get<CharacterController>(entity);
    std::erase(s->allCharacters, c.character);
    s->characterCollisionInterface->Remove(c.character);
    delete c.character;
  }

  static void OnCharacterControllerShrimpleDestroy(entt::registry& registry, entt::entity entity)
  {
    auto& c = registry.get<CharacterControllerShrimple>(entity);
    std::erase(s->allCharactersShrimple, c.character);
    c.character->RemoveFromPhysicsSystem();
    delete c.character;
  }

  void Initialize(World& world)
  {
    world.GetRegistry().on_destroy<RigidBody>().connect<&OnRigidBodyDestroy>();
    world.GetRegistry().on_destroy<CharacterController>().connect<&OnCharacterControllerDestroy>();
    world.GetRegistry().on_destroy<CharacterControllerShrimple>().connect<&OnCharacterControllerShrimpleDestroy>();
    s = std::make_unique<StaticVars>();

    JPH::RegisterDefaultAllocator();
    // JPH::Trace =
    // JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = )
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    TwoLevelGridShape::sRegister();

    s->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    s->jobSystem     = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

#ifdef JPH_DEBUG_RENDERER
    s->debugRenderer = std::make_unique<DebugRenderer>();
#endif

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
    s->engine->SetGravity(ToJolt(s->gravity));

    s->contactListener = std::make_unique<ContactListenerImpl>();
    s->characterContactListener = std::make_unique<CharacterContactListenerImpl>();
    s->characterCollisionInterface = std::make_unique<JPH::CharacterVsCharacterCollisionSimple>();
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
    ZoneScoped;
    // Update characters first
    for (auto* character : s->allCharacters)
    {
      ZoneScopedN("CharacterVirtual->ExtendedUpdate");
      character->ExtendedUpdate(dt,
        ToJolt(s->gravity),
        JPH::CharacterVirtual::ExtendedUpdateSettings{
          //.mStickToFloorStepDown             =,
          //.mWalkStairsStepUp                 =,
          //.mWalkStairsMinStepForward         =,
          //.mWalkStairsStepForwardTest        =,
          //.mWalkStairsCosAngleForwardContact =,
          //.mWalkStairsStepDownExtra = {0, -0.22f, 0},
        },
        s->engine->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        s->engine->GetDefaultLayerFilter(Layers::MOVING),
        {},
        {},
        *s->tempAllocator);

      auto entity = static_cast<entt::entity>(character->GetUserData());
      if (auto* t = world.GetRegistry().try_get<Transform>(entity))
      {
        t->position = ToGlm(character->GetPosition());
      }
    }

    const auto substeps = static_cast<int>(std::ceil(dt / s->targetRate));
    s->engine->Update(dt, substeps, s->tempAllocator.get(), s->jobSystem.get());

    for (auto* character : s->allCharactersShrimple)
    {
      ZoneScopedN("Character->PostSimulation");
      character->PostSimulation(1e-4f);

      auto entity = static_cast<entt::entity>(s->bodyInterface->GetUserData(character->GetBodyID()));
      if (auto* t = world.GetRegistry().try_get<Transform>(entity))
      {
        t->position = ToGlm(character->GetPosition());
      }
    }

    // Update transform of each entity with a RigidBody component
    for (auto&& [entity, rigidBody, transform] : world.GetRegistry().view<RigidBody, Transform>().each())
    {
      transform.position = ToGlm(s->bodyInterface->GetPosition(rigidBody.body));
      transform.rotation = ToGlm(s->bodyInterface->GetRotation(rigidBody.body));
    }

    // TODO: Invoke contact callbacks
    s->contactPairs.clear();

#ifdef JPH_DEBUG_RENDERER
    s->debugRenderer->ClearPrimitives();
    for (auto&& [entity, player, transform] : world.GetRegistry().view<Player, Transform>().each())
    {
      if (player.id == 0)
      {
        s->debugRenderer->SetCameraPos(ToJolt(transform.position));

#if 0
        // Create shape cast
        using namespace JPH;
        const Vec3 start            = ToJolt(transform.position + transform.GetForward() * 3.0f - transform.GetRight());
        const Vec3 direction        = ToJolt(2.0f * transform.GetRight() + glm::vec3(0, -.5f, 0));
        //const Vec3 base_offset      = start + 0.5f * direction;
        const Vec3 base_offset = start + 0.5f * direction;
        RefConst<Shape> shape       = new CapsuleShape(0.5f, 0.25f);
        //Mat44 rotation        = Mat44::sRotation(Vec3::sAxisX(), 0.1f * JPH_PI) * Mat44::sRotation(Vec3::sAxisY(), 0.2f * JPH_PI);
        Mat44 rotation        = Mat44::sIdentity();
        RShapeCast shape_cast = RShapeCast::sFromWorldTransform(shape, Vec3::sReplicate(1.0f), RMat44::sTranslation(start) * rotation, direction);

        // Settings
        ShapeCastSettings settings;
        //settings.mUseShrunkenShapeAndConvexRadius = mUseShrunkenShapeAndConvexRadius;
        //settings.mActiveEdgeMode                  = mActiveEdgeMode;
        //settings.mBackFaceModeTriangles           = mBackFaceModeTriangles;
        //settings.mBackFaceModeConvex              = mBackFaceModeConvex;
        //settings.mReturnDeepestPoint              = mReturnDeepestPoint;
        //settings.mCollectFacesMode                = mCollectFacesMode;

        int mMaxHits = 256;

        // Cast shape
        Array<ShapeCastResult> hits;
        if (mMaxHits == 0)
        {
          AnyHitCollisionCollector<CastShapeCollector> collector;
          s->engine->GetNarrowPhaseQuery().CastShape(shape_cast, settings, base_offset, collector);
          if (collector.HadHit())
            hits.push_back(collector.mHit);
        }
        else if (mMaxHits == 1)
        {
          ClosestHitCollisionCollector<CastShapeCollector> collector;
          s->engine->GetNarrowPhaseQuery().CastShape(shape_cast, settings, base_offset, collector);
          if (collector.HadHit())
            hits.push_back(collector.mHit);
        }
        else
        {
          AllHitCollisionCollector<CastShapeCollector> collector;
          s->engine->GetNarrowPhaseQuery().CastShape(shape_cast, settings, base_offset, collector);
          collector.Sort();
          hits.insert(hits.end(), collector.mHits.begin(), collector.mHits.end());
          if ((int)hits.size() > mMaxHits)
            hits.resize(mMaxHits);
        }

        const bool had_hit = !hits.empty();
        if (had_hit)
        {
          // Fill in results
          //ShapeCastResult& first_hit = hits.front();
          //outPosition                = shape_cast.GetPointOnRay(first_hit.mFraction);
          //outFraction                = first_hit.mFraction;
          //outID                      = first_hit.mBodyID2;

          // Draw results
          RVec3 prev_position = start;
          bool c              = false;
          for (const ShapeCastResult& hit : hits)
          {
            // Draw line
            RVec3 position = shape_cast.GetPointOnRay(hit.mFraction);
            s->debugRenderer->DrawLine(prev_position, position, c ? Color::sGrey : Color::sWhite);
            c             = !c;
            prev_position = position;

            BodyLockRead lock(s->engine->GetBodyLockInterface(), hit.mBodyID2);
            if (lock.Succeeded())
            {
              const Body& hit_body = lock.GetBody();

              // Draw shape
              Color color = hit_body.IsDynamic() ? Color::sYellow : Color::sOrange;
  #ifdef JPH_DEBUG_RENDERER
              shape_cast.mShape->Draw(s->debugRenderer.get(),
                shape_cast.mCenterOfMassStart.PostTranslated(hit.mFraction * shape_cast.mDirection),
                Vec3::sReplicate(1.0f),
                color,
                false,
                false);
  #endif // JPH_DEBUG_RENDERER

              // Draw normal
              JPH::RVec3 contact_position1 = base_offset + hit.mContactPointOn1;
              JPH::RVec3 contact_position2 = base_offset + hit.mContactPointOn2;
              JPH::Vec3 normal             = hit.mPenetrationAxis.Normalized();
              s->debugRenderer->DrawArrow(contact_position2, contact_position2 - normal, Color::sGreen, 0.01f); // Flip to make it point towards the cast body

              // Contact position 1
              s->debugRenderer->DrawMarker(contact_position1, Color::sGreen, 0.1f);

              // Draw perpendicular axis to indicate contact position 2
              Vec3 perp1 = normal.GetNormalizedPerpendicular();
              Vec3 perp2 = normal.Cross(perp1);
              s->debugRenderer->DrawLine(contact_position2 - 0.1f * perp1, contact_position2 + 0.1f * perp1, color);
              s->debugRenderer->DrawLine(contact_position2 - 0.1f * perp2, contact_position2 + 0.1f * perp2, color);

              // Draw material
              //const PhysicsMaterial* material2 = hit_body.GetShape()->GetMaterial(hit.mSubShapeID2);
              //s->debugRenderer->DrawText3D(position, material2->GetDebugName());

              // Draw faces
              s->debugRenderer->DrawWirePolygon(RMat44::sTranslation(base_offset), hit.mShape1Face, Color::sYellow, 0.01f);
              s->debugRenderer->DrawWirePolygon(RMat44::sTranslation(base_offset), hit.mShape2Face, Color::sRed, 0.01f);
            }
          }

          // Draw remainder of line
          s->debugRenderer->DrawLine(shape_cast.GetPointOnRay(hits.back().mFraction), start + direction, Color::sRed);
        }
        else
        {
          // Draw 'miss'
          s->debugRenderer->DrawLine(start, start + direction, Color::sRed);
  #ifdef JPH_DEBUG_RENDERER
          shape_cast.mShape
            ->Draw(s->debugRenderer.get(), shape_cast.mCenterOfMassStart.PostTranslated(shape_cast.mDirection), Vec3::sReplicate(1.0f), Color::sRed, false, false);
  #endif // JPH_DEBUG_RENDERER
        }
#endif
      }
    }
    s->engine->DrawBodies(
      JPH::BodyManager::DrawSettings{
        //.mDrawGetSupportFunction        = true,
        //.mDrawSupportDirection          = true,
        //.mDrawGetSupportingFace         = true,
        .mDrawShape                     = true,
        //.mDrawShapeWireframe            =,
        //.mDrawShapeColor                =,
        .mDrawBoundingBox               = true,
        //.mDrawCenterOfMassTransform     = true,
        //.mDrawWorldTransform            = true,
        .mDrawVelocity                  = true,
        //.mDrawMassAndInertia            =,
        //.mDrawSleepStats                =,
        //.mDrawSoftBodyVertices          =,
        //.mDrawSoftBodyVertexVelocities  =,
        //.mDrawSoftBodyEdgeConstraints   =,
        //.mDrawSoftBodyBendConstraints   =,
        //.mDrawSoftBodyVolumeConstraints =,
        //.mDrawSoftBodySkinConstraints   =,
        //.mDrawSoftBodyLRAConstraints    =,
        //.mDrawSoftBodyPredictedBounds   =,
        //.mDrawSoftBodyConstraintColor   =,
      },
      s->debugRenderer.get());
#endif
  }
} // namespace Physics
