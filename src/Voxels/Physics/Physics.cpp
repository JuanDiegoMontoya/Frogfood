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
#include "Jolt/Physics/Collision/RayCast.h"
#include "Jolt/Physics/Collision/CollisionCollector.h"
#include "Jolt/Physics/Collision/CollidePointResult.h"

#include "entt/entity/handle.hpp"
#include "entt/signal/dispatcher.hpp"

#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

#include <shared_mutex>
#include <mutex>
#include <vector>
#include <unordered_map>

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
          using namespace Layers;
        case WORLD: return inObject2 != HITBOX && inObject2 != HURTBOX && inObject2 != HITBOX_AND_HURTBOX;
        case CHARACTER: return inObject2 == WORLD;
        case PROJECTILE: return inObject2 == WORLD || inObject2 == HITBOX || inObject2 == HITBOX_AND_HURTBOX;
        case DROPPED_ITEM: return inObject2 == WORLD || inObject2 == DROPPED_ITEM || inObject2 == HITBOX || inObject2 == HITBOX_AND_HURTBOX;
        case DEBRIS: return inObject2 == WORLD || inObject2 == DEBRIS;
        case HITBOX: return inObject2 == HURTBOX || inObject2 == HITBOX_AND_HURTBOX || inObject2 == PROJECTILE || inObject2 == DROPPED_ITEM;
        case HURTBOX: return inObject2 == HITBOX || inObject2 == HITBOX_AND_HURTBOX;
        case HITBOX_AND_HURTBOX: return inObject2 == HITBOX || inObject2 == HURTBOX || inObject2 == HITBOX_AND_HURTBOX || inObject2 == PROJECTILE || inObject2 == DROPPED_ITEM;
        case CAST_WORLD: return inObject2 == WORLD;
        case CAST_PROJECTILE: return inObject2 == WORLD || inObject2 == HITBOX || inObject2 == HITBOX_AND_HURTBOX;
        case CAST_CHARACTER: return inObject2 == WORLD || inObject2 == CHARACTER;
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
        mObjectToBroadPhase[Layers::WORLD]            = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::CHARACTER]        = BroadPhaseLayers::MOVING;
        mObjectToBroadPhase[Layers::PROJECTILE]       = BroadPhaseLayers::MOVING;
        mObjectToBroadPhase[Layers::DROPPED_ITEM]     = BroadPhaseLayers::MOVING;
        mObjectToBroadPhase[Layers::DEBRIS]           = BroadPhaseLayers::MOVING;
        mObjectToBroadPhase[Layers::HITBOX]             = BroadPhaseLayers::MOVING;
        mObjectToBroadPhase[Layers::HURTBOX]            = BroadPhaseLayers::MOVING;
        mObjectToBroadPhase[Layers::HITBOX_AND_HURTBOX] = BroadPhaseLayers::MOVING;
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
      JPH::BroadPhaseLayer mObjectToBroadPhase[static_cast<int>(Layers::NUM_LAYERS)];
    };

    class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
      bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
      {
        switch (inLayer1)
        {
        case Layers::WORLD: return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::CHARACTER:
        case Layers::PROJECTILE:
        case Layers::DROPPED_ITEM:
        case Layers::DEBRIS: return true;
        case Layers::HITBOX:
        case Layers::HURTBOX:
        case Layers::HITBOX_AND_HURTBOX: return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::CAST_WORLD: return inLayer2 == BroadPhaseLayers::NON_MOVING;
        case Layers::CAST_PROJECTILE:
        case Layers::CAST_CHARACTER: return true;
        default: JPH_ASSERT(false); return false;
        }
      }
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
      JPH::BodyInterface* bodyInterfaceNoLock{};
      std::shared_mutex contactListenerMutex;

      std::unordered_map<JPH::Ref<JPH::Constraint>, std::pair<JPH::BodyID, JPH::BodyID>> constraintToBodyPair;
      std::unordered_multimap<JPH::BodyID, JPH::Constraint*> bodyToConstraints;
      std::vector<JPH::CharacterVirtual*> allCharacters;
      std::vector<JPH::Character*> allCharactersShrimple;
      std::vector<ContactAddedPair> contactAddedPairs;
      std::vector<ContactPersistedPair> contactPersistedPairs;
      entt::dispatcher dispatcher;
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
        s->contactAddedPairs.emplace_back(static_cast<entt::entity>(inBody1.GetUserData()), static_cast<entt::entity>(inBody2.GetUserData()));
      }

      void OnContactPersisted(const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        [[maybe_unused]] const JPH::ContactManifold& inManifold,
        [[maybe_unused]] JPH::ContactSettings& ioSettings) override
      {
        auto lock = std::unique_lock(s->contactListenerMutex);
        s->contactPersistedPairs.emplace_back(static_cast<entt::entity>(inBody1.GetUserData()), static_cast<entt::entity>(inBody2.GetUserData()));
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
        s->contactAddedPairs.emplace_back(static_cast<entt::entity>(inCharacter->GetUserData()), e2);
      }
    };
  }

  RigidBody& AddRigidBody(entt::handle handle, const RigidBodySettings& settings)
  {
    auto position = glm::vec3(0);
    auto rotation = glm::quat(1, 0, 0, 0);
    if (auto* t = handle.try_get<GlobalTransform>())
    {
      position = t->position;
      rotation = t->rotation;
    }

    auto bodySettings = JPH::BodyCreationSettings(settings.shape, ToJolt(position), ToJolt(rotation), settings.motionType, settings.layer);
    bodySettings.mIsSensor = settings.isSensor;
    bodySettings.mAllowedDOFs = settings.degreesOfFreedom;
    auto bodyId = s->bodyInterface->CreateAndAddBody(bodySettings, settings.activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

    s->bodyInterface->SetUserData(bodyId, static_cast<JPH::uint64>(handle.entity()));

    handle.emplace_or_replace<LinearVelocity>();
    handle.emplace_or_replace<Shape>().shape = settings.shape;
    auto& rb                                 = handle.emplace_or_replace<RigidBody>(bodyId);
    handle.emplace_or_replace<RigidBodySettings>(settings);
    return rb;
  }

  CharacterController& AddCharacterController(entt::handle handle, const CharacterControllerSettings& settings)
  {
    auto position = glm::vec3(0);
    auto rotation = glm::quat(1, 0, 0, 0);
    if (auto* t = handle.try_get<GlobalTransform>())
    {
      position = t->position;
      //rotation = t->rotation;
    }

    auto characterSettings = JPH::CharacterVirtualSettings();
    characterSettings.SetEmbedded();
    characterSettings.mShape = settings.shape;
    characterSettings.mEnhancedInternalEdgeRemoval = true;
    //characterSettings.mPredictiveContactDistance   = 1e-3f;
    //characterSettings.mCharacterPadding = 0.00002f;
    //characterSettings.mPredictiveContactDistance = 0.22f;
    //characterSettings.mSupportingVolume = JPH::Plane(JPH::Vec3(0, 1, 0), -0.5f);
    // TODO: use mInnerBodyShape to give character a physical presence (to be detected by ray casts, etc.)
    auto* character = new JPH::CharacterVirtual(&characterSettings, ToJolt(position), ToJolt(rotation), static_cast<JPH::uint64>(handle.entity()), s->engine.get());
    character->SetListener(s->characterContactListener.get());

    s->allCharacters.emplace_back(character);
    s->characterCollisionInterface->Add(character);
    handle.emplace_or_replace<Shape>().shape = settings.shape;
    handle.emplace_or_replace<LinearVelocity>();
    return handle.emplace_or_replace<CharacterController>(character);
  }

  CharacterControllerShrimple& AddCharacterControllerShrimple(entt::handle handle, const CharacterControllerShrimpleSettings& settings)
  {
    auto position = glm::vec3(0);
    auto rotation = glm::quat(1, 0, 0, 0);
    if (auto* t = handle.try_get<GlobalTransform>())
    {
      position = t->position;
      //rotation = t->rotation;
    }

    auto characterSettings = JPH::CharacterSettings();
    characterSettings.SetEmbedded();
    characterSettings.mLayer = Layers::CHARACTER;
    characterSettings.mUp = {0, 1, 0};
    characterSettings.mShape = settings.shape;
    //characterSettings.mSupportingVolume = JPH::Plane(JPH::Vec3(0, 1, 0), -1);
    characterSettings.mEnhancedInternalEdgeRemoval = true;
    auto* character = new JPH::Character(&characterSettings, ToJolt(position), ToJolt(rotation), static_cast<JPH::uint64>(handle.entity()), s->engine.get());
    character->AddToPhysicsSystem();
    s->bodyInterface->SetRestitution(character->GetBodyID(), 0);

    s->allCharactersShrimple.emplace_back(character);

    handle.emplace_or_replace<LinearVelocity>();
    handle.emplace_or_replace<Shape>().shape = settings.shape;
    return handle.emplace_or_replace<CharacterControllerShrimple>(character);
  }

  void RegisterConstraint(JPH::Ref<JPH::Constraint> constraint, JPH::BodyID body1, JPH::BodyID body2)
  {
    s->engine->AddConstraint(constraint);
    s->constraintToBodyPair.emplace(constraint, std::make_pair(body1, body2));
    s->bodyToConstraints.emplace(body1, constraint.GetPtr());
    s->bodyToConstraints.emplace(body2, constraint.GetPtr());
  }

  static void GetIgnoreEntityAndChildrenFilterHelper(entt::handle handle, JPH::IgnoreMultipleBodiesFilter& bodyFilter)
  {
    if (auto* cc = handle.try_get<CharacterController>())
    {
      bodyFilter.IgnoreBody(cc->character->GetInnerBodyID());
    }
    if (auto* cc = handle.try_get<CharacterControllerShrimple>())
    {
      bodyFilter.IgnoreBody(cc->character->GetBodyID());
    }
    if (auto* rb = handle.try_get<RigidBody>())
    {
      bodyFilter.IgnoreBody(rb->body);
    }

    if (auto* h = handle.try_get<Hierarchy>())
    {
      for (auto child : h->children)
      {
        GetIgnoreEntityAndChildrenFilterHelper({*handle.registry(), child}, bodyFilter);
      }
    }
  }

  std::unique_ptr<JPH::IgnoreMultipleBodiesFilter> GetIgnoreEntityAndChildrenFilter(entt::handle handle)
  {
    auto bodyFilter = std::make_unique<JPH::IgnoreMultipleBodiesFilter>();
    GetIgnoreEntityAndChildrenFilterHelper(handle, *bodyFilter);
    return bodyFilter;
  }

  static void RemoveConstraintsFromBody(JPH::BodyID body)
  {
    auto constraintsToRemoveFromOtherBodies = std::vector<std::pair<JPH::BodyID, JPH::Constraint*>>();

    // Erase all the constraints attached to this body.
    auto [begin, end] = s->bodyToConstraints.equal_range(body);
    for (auto it = begin; it != end; ++it)
    {
      auto [_, constraint] = *it;
      s->engine->RemoveConstraint(constraint);
      auto bodyPairIt = s->constraintToBodyPair.find(constraint);
      auto bodyPair   = bodyPairIt->second;
      constraintsToRemoveFromOtherBodies.emplace_back(body == bodyPair.first ? bodyPair.second : bodyPair.first, constraint);
      s->constraintToBodyPair.erase(bodyPairIt);
    }

    s->bodyToConstraints.erase(body);

    // Unmap the constraints from other bodies.
    for (auto [body2, constraint] : constraintsToRemoveFromOtherBodies)
    {
      auto [begin2, end2] = s->bodyToConstraints.equal_range(body2);
      for (auto it = begin2; it != end2; ++it)
      {
        if (it->second == constraint)
        {
          s->bodyToConstraints.erase(it);
          break;
        }
      }
    }
  }

  static void OnRigidBodyDestroy(entt::registry& registry, entt::entity entity)
  {
    auto& p = registry.get<RigidBody>(entity);
    RemoveConstraintsFromBody(p.body);
    s->bodyInterface->RemoveBody(p.body);
    s->bodyInterface->DestroyBody(p.body);
  }

  static void OnCharacterControllerDestroy(entt::registry& registry, entt::entity entity)
  {
    auto& c = registry.get<CharacterController>(entity);
    if (!c.character->GetInnerBodyID().IsInvalid())
    {
      RemoveConstraintsFromBody(c.character->GetInnerBodyID());
    }
    s->characterCollisionInterface->Remove(c.character);
    std::erase(s->allCharacters, c.character);
    delete c.character;
  }

  static void OnCharacterControllerShrimpleDestroy(entt::registry& registry, entt::entity entity)
  {
    auto& c = registry.get<CharacterControllerShrimple>(entity);
    RemoveConstraintsFromBody(c.character->GetBodyID());
    c.character->RemoveFromPhysicsSystem();
    std::erase(s->allCharactersShrimple, c.character);
    delete c.character;
  }

  const JPH::NarrowPhaseQuery& GetNarrowPhaseQuery()
  {
    return s->engine->GetNarrowPhaseQuery();
  }

  JPH::BodyInterface& GetBodyInterface()
  {
    return *s->bodyInterface;
  }

  JPH::PhysicsSystem& GetPhysicsSystem()
  {
    return *s->engine;
  }

  entt::dispatcher& GetDispatcher()
  {
    return s->dispatcher;
  }

  void Initialize(World& world)
  {
    world.GetRegistry().on_destroy<RigidBody>().connect<&OnRigidBodyDestroy>();
    world.GetRegistry().on_destroy<RigidBody>().connect<&entt::registry::remove<Shape>>();
    world.GetRegistry().on_destroy<RigidBody>().connect<&entt::registry::remove<RigidBodySettings>>();
    world.GetRegistry().on_destroy<CharacterController>().connect<&OnCharacterControllerDestroy>();
    world.GetRegistry().on_destroy<CharacterController>().connect<&entt::registry::remove<Shape>>();
    world.GetRegistry().on_destroy<CharacterControllerShrimple>().connect<&OnCharacterControllerShrimpleDestroy>();
    world.GetRegistry().on_destroy<CharacterControllerShrimple>().connect<&entt::registry::remove<Shape>>();
    s = std::make_unique<StaticVars>();

    JPH::RegisterDefaultAllocator();
    // JPH::Trace =
    // JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = )
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    TwoLevelGridShape::sRegister();

    s->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(50 * 1024 * 1024);
    s->jobSystem     = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

#ifdef JPH_DEBUG_RENDERER
    s->debugRenderer = std::make_unique<DebugRenderer>();
#endif

    s->broadPhaseLayerInterface      = std::make_unique<BPLayerInterfaceImpl>();
    s->objectVsBroadPhaseLayerFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
    s->objectVsObjectLayerFilter     = std::make_unique<ObjectLayerPairFilterImpl>();
    s->engine                        = std::make_unique<JPH::PhysicsSystem>();

    constexpr JPH::uint cMaxBodies             = 50'000;
    constexpr JPH::uint cNumBodyMutexes        = 0;
    constexpr JPH::uint cMaxBodyPairs          = 50'000;
    constexpr JPH::uint cMaxContactConstraints = 50'000;
    
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
    s->bodyInterfaceNoLock = &s->engine->GetBodyInterfaceNoLock();
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

    for (auto&& [entity, linearVelocity, friction] : world.GetRegistry().view<LinearVelocity, Friction>().each())
    {
      linearVelocity.v -= friction.axes * linearVelocity.v * dt;
    }

    // Pre-update: synchronize physics and ECS representations
    for (auto&& [entity, transform, rigidBody, rigidBodySettings, linearVelocity] : world.GetRegistry().view<GlobalTransform, RigidBody, RigidBodySettings, LinearVelocity>().each())
    {
      s->bodyInterfaceNoLock->SetPositionAndRotationWhenChanged(rigidBody.body,
        ToJolt(transform.position),
        ToJolt(transform.rotation).Normalized(),
        JPH::EActivation::Activate);
      s->bodyInterfaceNoLock->SetLinearVelocity(rigidBody.body, ToJolt(linearVelocity.v));
    }
    
    for (auto&& [entity, cc, transform, linearVelocity] : world.GetRegistry().view<CharacterController, GlobalTransform, LinearVelocity>().each())
    {
      cc.previousGroundState = cc.character->GetGroundState();

      cc.character->SetPosition(ToJolt(transform.position));
      //cc.character->SetRotation(ToJolt(transform.rotation));
      cc.character->SetLinearVelocity(ToJolt(linearVelocity.v));
    }

    for (auto&& [entity, cc, transform, linearVelocity] : world.GetRegistry().view<CharacterControllerShrimple, GlobalTransform, LinearVelocity>().each())
    {
      cc.previousGroundState = cc.character->GetGroundState();
      
      s->bodyInterfaceNoLock->SetPosition(cc.character->GetBodyID(),
        ToJolt(transform.position),
        JPH::EActivation::Activate);
      s->bodyInterfaceNoLock->SetLinearVelocity(cc.character->GetBodyID(), ToJolt(linearVelocity.v));
    }

    // Update character controllers
    for (auto& character : s->allCharacters)
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
        s->engine->GetDefaultBroadPhaseLayerFilter(Layers::CHARACTER),
        s->engine->GetDefaultLayerFilter(Layers::CHARACTER),
        {},
        {},
        *s->tempAllocator);

      auto entity = static_cast<entt::entity>(character->GetUserData());
      if (auto* t = world.GetRegistry().try_get<LocalTransform>(entity))
      {
        t->position = ToGlm(character->GetPosition());
        UpdateLocalTransform({world.GetRegistry(), entity});
      }
      world.GetRegistry().get<LinearVelocity>(entity).v = ToGlm(character->GetLinearVelocity());
    }

    // Update world
    {
      ZoneScopedN("PhysicsSystem::Update");
      const auto substeps = static_cast<int>(std::ceil(dt / s->targetRate));
      ZoneTextF("%s%d", "Substeps: ", substeps);
      s->engine->Update(dt, substeps, s->tempAllocator.get(), s->jobSystem.get());
    }
    
    for (auto& character : s->allCharactersShrimple)
    {
      ZoneScopedN("Character->PostSimulation");
      character->PostSimulation(1e-4f);

      auto entity = static_cast<entt::entity>(s->bodyInterface->GetUserData(character->GetBodyID()));
      if (auto* t = world.GetRegistry().try_get<LocalTransform>(entity))
      {
        t->position = ToGlm(character->GetPosition());
        UpdateLocalTransform({world.GetRegistry(), entity});
      }

      auto& velocity = world.GetRegistry().get<LinearVelocity>(entity).v;
      velocity = ToGlm(s->bodyInterfaceNoLock->GetLinearVelocity(character->GetBodyID()));
    }

    // Update transform of each entity with a RigidBody component. If we don't exclude character controllers from being updated here, we get funny behavior.
    for (auto&& [entity, rigidBody, rigidBodySettings, transform, linearVelocity] :
      world.GetRegistry().view<RigidBody, RigidBodySettings, LocalTransform, LinearVelocity>(/*entt::exclude<CharacterControllerShrimple>*/).each())
    {
      if (rigidBodySettings.motionType == JPH::EMotionType::Dynamic && s->bodyInterfaceNoLock->IsActive(rigidBody.body))
      {
        transform.position = ToGlm(s->bodyInterfaceNoLock->GetPosition(rigidBody.body));
        transform.rotation = ToGlm(s->bodyInterfaceNoLock->GetRotation(rigidBody.body));
        linearVelocity.v   = ToGlm(s->bodyInterfaceNoLock->GetLinearVelocity(rigidBody.body));
        UpdateLocalTransform({world.GetRegistry(), entity});
      }
    }

    // Simulate projectiles
    for (auto&& [entity, gt, lt, projectile, linearVelocity] : world.GetRegistry().view<GlobalTransform, LocalTransform, Projectile, LinearVelocity>().each())
    {
      // Calculate updated velocity
      const auto acceleration = -9.81f * glm::vec3(0, 1, 0) + -linearVelocity.v * projectile.drag;
      const auto newVelocity  = linearVelocity.v + acceleration * dt;

      // Cast ray against characters and world
      const auto initialPosition = gt.position;
      const auto castVelocity    = newVelocity * dt;
      auto collector             = JPH::ClosestHitCollisionCollector<JPH::CastRayCollector>();
      GetNarrowPhaseQuery().CastRay(JPH::RRayCast(ToJolt(initialPosition), ToJolt(castVelocity)),
        JPH::RayCastSettings(),
        collector,
        s->engine->GetDefaultBroadPhaseLayerFilter(Layers::CAST_PROJECTILE),
        s->engine->GetDefaultLayerFilter(Layers::CAST_PROJECTILE));

      lt.position += castVelocity;
      linearVelocity.v = newVelocity;

      if (collector.HadHit())
      {
        // Generate collision events.
        const auto entity2 = static_cast<entt::entity>(s->bodyInterface->GetUserData(collector.mHit.mBodyID));
        auto pair          = ContactAddedPair(entity, entity2);
        auto ppair         = &pair;
        s->dispatcher.trigger(ppair);

        // Object may have been destroyed in event.
        if (world.GetRegistry().any_of<DeferredDelete>(pair.entity1))
        {
          continue;
        }

        // Collided-with entity is dead, don't reflect.
        if (auto* h = world.GetRegistry().try_get<Health>(pair.entity2); h && h->hp <= 0)
        {
          continue;
        }

        // Calculate new velocity if it hit a surface
        const auto hitPosition = initialPosition + castVelocity * collector.mHit.mFraction;
        const auto hitNormal   = ToGlm(
          s->engine->GetBodyLockInterfaceNoLock().TryGetBody(collector.mHit.mBodyID)->GetWorldSpaceSurfaceNormal(collector.mHit.mSubShapeID2, ToJolt(hitPosition)));

        // The bullet's remaining dt is "stolen". This could be solved by looping until there are no more collisions, but the artifact is difficult to notice.
        lt.position                = hitPosition + hitNormal * 1e-3f;
        constexpr auto restitution = 0.25f;
        // Reflect projectile with more restitution (bounciness) as the impact angle gets shallower.
        linearVelocity.v = glm::reflect(newVelocity, hitNormal) * (1 - (1 - restitution) * abs(glm::dot(glm::normalize(newVelocity), hitNormal)));
      }

      UpdateLocalTransform({world.GetRegistry(), entity});
    }

    // Dispatch events for newly-added contacts
    for (auto& contactPair : s->contactAddedPairs)
    {
      auto ppair = &contactPair;
      s->dispatcher.trigger(ppair);
    }
    s->contactAddedPairs.clear();

    // Dispatch events for persisted contacts
    for (auto& contactPair : s->contactPersistedPairs)
    {
      auto ppair = &contactPair;
      s->dispatcher.trigger(ppair);
    }
    s->contactPersistedPairs.clear();

#ifdef JPH_DEBUG_RENDERER
    const auto debug = world.GetRegistry().ctx().get<Debugging>();
    s->debugRenderer->ClearPrimitives();
    for (auto&& [entity, transform] : world.GetRegistry().view<LocalPlayer, GlobalTransform>().each())
    {
      s->debugRenderer->SetCameraPos(ToJolt(transform.position));
      
      if (debug.drawDebugProbe)
      {
        // Create shape cast
        using namespace JPH;
        const Vec3 start     = ToJolt(transform.position + GetForward(transform.rotation) * 3.0f - GetRight(transform.rotation));
        const Vec3 direction = ToJolt(2.0f * GetRight(transform.rotation) + glm::vec3(0, -.5f, 0));
        // const Vec3 base_offset      = start + 0.5f * direction;
        const Vec3 base_offset = start + 0.5f * direction;
        auto shape             = JPH::RefConst(new CapsuleShape(0.5f, 0.25f));
        // Mat44 rotation        = Mat44::sRotation(Vec3::sAxisX(), 0.1f * JPH_PI) * Mat44::sRotation(Vec3::sAxisY(), 0.2f * JPH_PI);
        Mat44 rotation        = Mat44::sIdentity();
        RShapeCast shape_cast = RShapeCast::sFromWorldTransform(shape, Vec3::sReplicate(1.0f), RMat44::sTranslation(start) * rotation, direction);

        // Settings
        ShapeCastSettings settings;
        // settings.mUseShrunkenShapeAndConvexRadius = mUseShrunkenShapeAndConvexRadius;
        // settings.mActiveEdgeMode                  = mActiveEdgeMode;
        // settings.mBackFaceModeTriangles           = mBackFaceModeTriangles;
        // settings.mBackFaceModeConvex              = mBackFaceModeConvex;
        // settings.mReturnDeepestPoint              = mReturnDeepestPoint;
        // settings.mCollectFacesMode                = mCollectFacesMode;

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
          // ShapeCastResult& first_hit = hits.front();
          // outPosition                = shape_cast.GetPointOnRay(first_hit.mFraction);
          // outFraction                = first_hit.mFraction;
          // outID                      = first_hit.mBodyID2;

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
              shape_cast.mShape->Draw(s->debugRenderer.get(),
                shape_cast.mCenterOfMassStart.PostTranslated(hit.mFraction * shape_cast.mDirection),
                Vec3::sReplicate(1.0f),
                color,
                false,
                false);

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
              // const PhysicsMaterial* material2 = hit_body.GetShape()->GetMaterial(hit.mSubShapeID2);
              // s->debugRenderer->DrawText3D(position, material2->GetDebugName());

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
          shape_cast.mShape
            ->Draw(s->debugRenderer.get(), shape_cast.mCenterOfMassStart.PostTranslated(shape_cast.mDirection), Vec3::sReplicate(1.0f), Color::sRed, false, false);
        }
      }
    }

    s->engine->DrawBodies(
      JPH::BodyManager::DrawSettings{
        //.mDrawGetSupportFunction        = true,
        //.mDrawSupportDirection          = true,
        //.mDrawGetSupportingFace         = true,
        .mDrawShape                     = debug.drawPhysicsShapes,
        //.mDrawShapeWireframe            =,
        //.mDrawShapeColor                =,
        //.mDrawBoundingBox               = true,
        //.mDrawCenterOfMassTransform     = true,
        //.mDrawWorldTransform            = true,
        .mDrawVelocity                  = debug.drawPhysicsVelocity,
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

    s->engine->DrawConstraints(s->debugRenderer.get());
#endif
  }

  void NearestHitCollector::AddHit(const ResultType& inResult)
  {
    if (!nearest || inResult.mFraction < nearest->mFraction)
    {
      nearest = inResult;
      this->UpdateEarlyOutFraction(nearest->mFraction);
    }
  }

  void NearestRayCollector::AddHit(const NearestRayCollector::ResultType& inResult)
  {
    if (!nearest || inResult.mFraction < nearest->mFraction)
    {
      nearest = inResult;
      this->UpdateEarlyOutFraction(inResult.mFraction);
    }
  }

  void AllRayCollector::AddHit(const ResultType& inResult)
  {
    unorderedHits.emplace_back(inResult);
  }

} // namespace Physics
