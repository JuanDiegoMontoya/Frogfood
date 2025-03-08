#include "Game.h"
#ifndef GAME_HEADLESS
#include "PlayerHead.h"
#include "Input.h"
#include "debug/Shapes.h"
#endif
#include "Physics/Physics.h"
#include "Physics/TwoLevelGridShape.h"
#include "Physics/PhysicsUtils.h"
#include "TwoLevelGrid.h"
#include "Pathfinding.h"
#include "MathUtilities.h"
#include "HashGrid.h"
#include "Prefab.h"
#include "Reflection.h"

#include "entt/entity/handle.hpp"

#include "tracy/Tracy.hpp"

#include "Jolt/Physics/Collision/Shape/PlaneShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/CylinderShape.h"
#include "Jolt/Physics/Constraints/DistanceConstraint.h"
#include "Jolt/Physics/Constraints/SwingTwistConstraint.h"
#include "Jolt/Physics/Constraints/FixedConstraint.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Collision/CollisionCollectorImpl.h"
#include "Jolt/Physics/Collision/CollisionCollector.h"
#include "Jolt/Physics/Collision/RayCast.h"
#include "entt/signal/dispatcher.hpp"

#include "FastNoise/FastNoise.h"

#include <chrono>
#include <stack>
#include <execution>
#include <algorithm>
#include <mutex>

#define GAME_CATCH_EXCEPTIONS 0

// We don't want this to happen when the component/entity is actually deleted, as we care about having a valid parent.
static void OnDeferredDeleteConstruct(entt::registry& registry, entt::entity entity)
{
  ZoneScoped;
  assert(registry.valid(entity));
  auto* h = registry.try_get<Hierarchy>(entity);
  if (h && h->parent != entt::null)
  {
    auto& ph = registry.get<Hierarchy>(h->parent);
    ph.RemoveChild(entity);
  }
}

// Helper to simplify logic for OnContact. Calls the input function twice with swapped arguments.
// Callee should return true if conditions were met so it isn't unnecessarily invoked twice.
template<typename P, typename F>
void TryTwice(const P& pair, F&& function)
{
  if (function(pair.entity1, pair.entity2))
  {
    return;
  }

  function(pair.entity2, pair.entity1);
}

// *ppair is modified to contain the actual entities that collided
static void OnContactAdded(World& world, Physics::ContactAddedPair* ppair)
{
  ZoneScoped;
  assert(ppair);
  auto& pair = *ppair;

  if (world.GetRegistry().all_of<ForwardCollisionsToParent>(pair.entity1))
  {
    auto& h = world.GetRegistry().get<Hierarchy>(pair.entity1);
    if (h.parent != entt::null)
    {
      pair.entity1 = h.parent;
      OnContactAdded(world, ppair);
      return;
    }
  }

  if (world.GetRegistry().all_of<ForwardCollisionsToParent>(pair.entity2))
  {
    auto& h = world.GetRegistry().get<Hierarchy>(pair.entity2);
    if (h.parent != entt::null)
    {
      pair.entity2 = h.parent;
      OnContactAdded(world, ppair);
      return;
    }
  }

  // Projectiles hurt creatures
  TryTwice(pair,
    [&](entt::entity entity1, entt::entity entity2)
    {
      if (world.GetRegistry().any_of<Health>(entity1) && world.GetRegistry().all_of<Projectile, ContactDamage>(entity2))
      {
        if (world.AreEntitiesEnemies(entity1, entity2))
        {
          if (auto* h = world.GetRegistry().try_get<Health>(entity1); h && h->hp > 0)
          {
            const auto& projectile = world.GetRegistry().get<Projectile>(entity2);
            const auto& damage     = world.GetRegistry().get<ContactDamage>(entity2);
            auto& projVelocity     = world.GetRegistry().get<LinearVelocity>(entity2);

            //const auto currentSpeed2  = glm::dot(projectile.velocity, projectile.velocity);
            //const auto energyFraction = (currentSpeed2) / (projectile.initialSpeed * projectile.initialSpeed);
            const auto energyFraction = glm::length(projVelocity.v) / projectile.initialSpeed;

            const auto effectiveKnockback = damage.knockback * energyFraction;
            world.DamageEntity(entity1, damage.damage * energyFraction);
            auto pushDir = projVelocity.v;
            pushDir.y    = 0;
            if (glm::length(pushDir) > 1e-3f)
            {
              pushDir = glm::normalize(pushDir);
            }
            pushDir *= effectiveKnockback * 3;
            pushDir.y = effectiveKnockback;
            //world.SetLinearVelocity(entity1, pushDir);
            auto& velocity = world.GetRegistry().get<LinearVelocity>(entity1);
            pushDir.y /= exp2(glm::max(0.0f, velocity.v.y * 1.0f)); // Reduce velocity gain (prevent stuff from flying super high- subject to change).
            if (auto* m = world.GetRegistry().try_get<KnockbackMultiplier>(entity1))
            {
              pushDir *= m->factor;
            }
            velocity.v += pushDir;
            if (auto* cc = world.GetRegistry().try_get<Physics::CharacterControllerShrimple>(entity1))
            {
              cc->previousGroundState = JPH::CharacterBase::EGroundState::InAir;
            }
            world.GetRegistry().emplace_or_replace<DeferredDelete>(entity2);
            world.GetRegistry().remove<Projectile>(entity2);
          }
          return true;
        }
      }
      return false;
    });

  // Players pick up dropped items
  TryTwice(pair,
    [&](entt::entity entity1, entt::entity entity2)
    {
      if (world.GetRegistry().all_of<Player, Inventory>(entity1) && world.GetRegistry().all_of<DroppedItem>(entity2) &&
          !world.GetRegistry().any_of<CannotBePickedUp>(entity2))
      {
        auto& i = world.GetRegistry().get<Inventory>(entity1);
        auto& d = world.GetRegistry().get<DroppedItem>(entity2);

        if (d.item.id != nullItem)
        {
          i.TryStackItem(d.item);
          if (d.item.count > 0)
          {
            if (auto slotCoords = i.GetFirstEmptySlot())
            {
              i.OverwriteSlot(*slotCoords, d.item, entity1);
              d.item = {.count = 0};
            }
          }

          if (d.item.count == 0)
          {
            world.GetRegistry().remove<DroppedItem>(entity2);
            world.GetRegistry().get_or_emplace<DeferredDelete>(entity2);
          }
        }
        return true;
      }
      return false;
    });
}

static void OnContactPersisted(World& world, Physics::ContactPersistedPair* ppair)
{
  ZoneScoped;
  assert(ppair);
  auto& pair = *ppair;

  if (world.GetRegistry().all_of<ForwardCollisionsToParent>(pair.entity1))
  {
    auto& h = world.GetRegistry().get<Hierarchy>(pair.entity1);
    if (h.parent != entt::null)
    {
      pair.entity1 = h.parent;
      OnContactPersisted(world, ppair);
      return;
    }
  }

  if (world.GetRegistry().all_of<ForwardCollisionsToParent>(pair.entity2))
  {
    auto& h = world.GetRegistry().get<Hierarchy>(pair.entity2);
    if (h.parent != entt::null)
    {
      pair.entity2 = h.parent;
      OnContactPersisted(world, ppair);
      return;
    }
  }

  // Players take damage from enemy team
  TryTwice(pair,
    [&](entt::entity entity1, entt::entity entity2)
    {
      if (world.GetRegistry().all_of<Player, Health>(entity1) && world.GetRegistry().all_of<ContactDamage>(entity2))
      {
        if (world.AreEntitiesEnemies(entity1, entity2))
        {
          const auto& contactDamage = world.GetRegistry().get<ContactDamage>(entity2);

          if (world.DamageEntity(entity1, contactDamage.damage) > 0)
          {
            world.GetRegistry().emplace<Invulnerability>(entity1).remainingSeconds = 0.5f;
          }
        }
        return true;
      }
      return false;
    });

  // Other sources of contact damage hurt creatures
  TryTwice(pair,
    [&](entt::entity entity1, entt::entity entity2)
    {
      if (world.GetRegistry().any_of<Health>(entity1) && world.GetRegistry().all_of<ContactDamage>(entity2) &&
          !world.GetRegistry().any_of<Projectile>(entity2) && !world.GetRegistry().any_of<Player>(entity1))
      {
        if (world.AreEntitiesEnemies(entity1, entity2) && world.CanEntityDamageEntity(entity2, entity1))
        {
          auto& pos1          = world.GetRegistry().get<GlobalTransform>(entity1).position;
          auto pos2          = world.GetRegistry().get<GlobalTransform>(entity2).position;
          const auto& damage = world.GetRegistry().get<ContactDamage>(entity2);
          world.DamageEntity(entity1, damage.damage);
          auto pushDir = pos1 - pos2;
          pushDir.y    = 0;
          if (glm::length(pushDir) > 1e-3f)
          {
            pushDir = glm::normalize(pushDir);
          }
          pushDir *= damage.knockback * 3;
          pushDir.y               = damage.knockback;
          auto& velocity = world.GetRegistry().get<LinearVelocity>(entity1);
          pushDir.y /= exp2(glm::max(0.0f, velocity.v.y * 1.0f)); // Reduce velocity gain (prevent stuff from flying super high- subject to change).
          if (auto* m = world.GetRegistry().try_get<KnockbackMultiplier>(entity1))
          {
            pushDir *= m->factor;
          }
          velocity.v += pushDir;
          if (auto* cc = world.GetRegistry().try_get<Physics::CharacterControllerShrimple>(entity1))
          {
            cc->previousGroundState = JPH::CharacterBase::EGroundState::InAir;
          }
          world.GetRegistry().get_or_emplace<CannotDamageEntities>(entity2).entities[entity1] = 0.2f;
        }
        return true;
      }
      return false;
    });
}

void OnNoclipCharacterControllerConstruct(entt::registry& registry, entt::entity entity)
{
  registry.remove<FlyingCharacterController>(entity);
  registry.remove<Physics::CharacterController>(entity);
  registry.remove<Physics::CharacterControllerShrimple>(entity);
}

void OnFlyingCharacterControllerConstruct(entt::registry& registry, entt::entity entity)
{
  registry.remove<NoclipCharacterController>(entity);
  registry.remove<Physics::CharacterController>(entity);
  registry.remove<Physics::CharacterControllerShrimple>(entity);
}

void OnCharacterControllerConstruct(entt::registry& registry, entt::entity entity)
{
  registry.remove<NoclipCharacterController>(entity);
  registry.remove<FlyingCharacterController>(entity);
  registry.remove<Physics::CharacterControllerShrimple>(entity);
  registry.remove<Friction>(entity); // TODO: temporary until CC has inertia
}

void OnCharacterControllerShrimpleConstruct(entt::registry& registry, entt::entity entity)
{
  registry.remove<NoclipCharacterController>(entity);
  registry.remove<FlyingCharacterController>(entity);
  registry.remove<Physics::CharacterController>(entity);
}

void OnGlobalTransformRemove(entt::registry& registry, entt::entity entity)
{
  registry.ctx().get<HashGrid>().erase(entity);
}

void OnLinearPathRemove(entt::registry& registry, entt::entity entity)
{
  if (!registry.all_of<DeferredDelete>(entity))
  {
    auto& path = registry.get<LinearPath>(entity);
    registry.emplace_or_replace<LocalTransform>(entity, path.originalLocalTransform);
    UpdateLocalTransform({registry, entity});
  }
}

Game::Game(uint32_t tickHz)
{
  world_ = std::make_unique<World>();
  Physics::Initialize(*world_);
  Physics::GetDispatcher().sink<Physics::ContactAddedPair*>().connect<&OnContactAdded>(*world_);
  Physics::GetDispatcher().sink<Physics::ContactPersistedPair*>().connect<&OnContactPersisted>(*world_);
#ifdef GAME_HEADLESS
  head_ = std::make_unique<NullHead>();
  world_->GetRegistry().ctx().emplace<GameState>() = GameState::GAME;
  world_->InitializeGameState();
#else
  head_ = std::make_unique<PlayerHead>(PlayerHead::CreateInfo{
    .name        = "Gabagool",
    .maximize    = false,
    .decorate    = true,
    .presentMode = VK_PRESENT_MODE_FIFO_KHR,
    .world       = world_.get(),
  });
  world_->GetRegistry().ctx().emplace<GameState>() = GameState::MENU;
  world_->GetRegistry().ctx().emplace<std::vector<Debug::Line>>();
#endif

  world_->GetRegistry().ctx().emplace<Debugging>();
  world_->GetRegistry().ctx().emplace<TimeScale>();
  world_->GetRegistry().ctx().emplace<TickRate>().hz = tickHz;
  world_->GetRegistry().ctx().emplace_as<float>("time"_hs) = 0; // TODO: TEMP
  world_->GetRegistry().ctx().emplace<Pathfinding::PathCache>(); // Note: should be invalidated when voxel grid changes
  world_->GetRegistry().ctx().emplace<HashGrid>(16);
  world_->GetRegistry().ctx().emplace<Head*>() = head_.get(); // Hack

  world_->GetRegistry().on_construct<DeferredDelete>().connect<&OnDeferredDeleteConstruct>();
  world_->GetRegistry().on_construct<NoclipCharacterController>().connect<&OnNoclipCharacterControllerConstruct>();
  world_->GetRegistry().on_construct<FlyingCharacterController>().connect<&OnFlyingCharacterControllerConstruct>();
  world_->GetRegistry().on_construct<Physics::CharacterController>().connect<&OnCharacterControllerConstruct>();
  world_->GetRegistry().on_construct<Physics::CharacterControllerShrimple>().connect<&OnCharacterControllerShrimpleConstruct>();
  world_->GetRegistry().on_destroy<GlobalTransform>().connect<&OnGlobalTransformRemove>();
  world_->GetRegistry().on_destroy<LinearPath>().connect<&OnLinearPathRemove>();

  Core::Reflection::InitializeReflection();
}

Game::~Game()
{
  Physics::Terminate();
}

static std::optional<glm::vec3> SampleWalkablePosition(const TwoLevelGrid& grid, PCG::Rng& rng, glm::vec3 origin, float minDistance, float maxDistance, bool isAirWalkable)
{
  // Pick a random position in the sphere with a minimum distance from the player.
  const float r     = rng.RandFloat(minDistance, maxDistance);
  const float theta = rng.RandFloat(0, glm::two_pi<float>());
  const float phi   = rng.RandFloat(0, glm::two_pi<float>());
  const auto pos    = origin + Math::SphericalToCartesian(theta, phi, r); // Totally not uniform, but fine for testing.

  // Validate the position
  if (!grid.IsPositionInGrid(pos) || grid.GetVoxelAt(pos) != 0)
  {
    return std::nullopt;
  }

  auto lastValidPos      = pos;
  bool foundSolidSurface = false;
  if (!isAirWalkable)
  {
    // Slide down until on solid surface.
    for (int i = 0; i < 20; i++)
    {
      const auto nextPos = lastValidPos - glm::vec3(0, i, 0);
      if (!grid.IsPositionInGrid(nextPos) || grid.GetVoxelAt(nextPos) != 0)
      {
        foundSolidSurface = true;
        break;
      }
      lastValidPos = nextPos;
    }
  }

  const auto realPos = floor(lastValidPos) + glm::vec3(0.5f);
  // Re-validate distance as it may have changed.
  const auto dist2 = Math::Distance2(realPos, origin);
  if ((isAirWalkable || foundSolidSurface) && dist2 > minDistance * minDistance && dist2 < maxDistance * maxDistance)
  {
    return realPos;
  }

  return std::nullopt;
}

class MeleeFrogDefinition : public EntityPrefabDefinition
{
public:
  using EntityPrefabDefinition::EntityPrefabDefinition;

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat) const override
  {
    auto& registry = world.GetRegistry();
    auto sphere    = JPH::Ref(new JPH::SphereShape(0.4f));
    sphere->SetDensity(0.5f);

    auto e                         = world.CreateRenderableEntity(position, {1, 0, 0, 0}, 0.4f);
    registry.emplace<Mesh>(e).name = "frog";
    registry.emplace<Name>(e, "Frog");
    registry.emplace<Health>(e) = {100, 100};
    // registry_.emplace<SimpleEnemyBehavior>(e);
    registry.emplace<SimplePathfindingEnemyBehavior>(e);
    registry.emplace<Pathfinding::CachedPath>(e).timeBetweenUpdates = 1;
    registry.emplace<InputState>(e);
    registry.emplace<Loot>(e).name = "standard";
    registry.emplace<TeamFlags>(e, TeamFlagBits::ENEMY);
    registry.emplace<DespawnWhenFarFromPlayer>(e);
    registry.emplace<Enemy>(e);
    registry.emplace<AiVision>(e);
    registry.emplace<AiHearing>(e);
    registry.emplace<AiTarget>(e);
    registry.emplace<AiWanderBehavior>(e);
    registry.emplace<WalkingMovementAttributes>(e) = {.runBaseSpeed = 3.5f, .runMaxSpeed = 3.5f};

    auto& contactDamage  = registry.emplace<ContactDamage>(e);
    contactDamage.damage = 10;
    // Physics::AddCharacterController({registry_, e}, {sphere});
    Physics::AddCharacterControllerShrimple({registry, e}, {.shape = sphere});
    // registry_.emplace<FlyingCharacterController>(e) = {.maxSpeed = 6, .acceleration = 25};
    registry.emplace_or_replace<LinearVelocity>(e);
    auto rb = Physics::AddRigidBody({registry, e}, {.shape = sphere, .layer = Physics::Layers::CHARACTER});
    Physics::GetBodyInterface().SetGravityFactor(rb.body, 0);

    auto e2                         = world.CreateRenderableEntity({1.0f, 0.3f, -0.8f}, {1, 0, 0, 0}, 1.5f);
    registry.emplace<Name>(e2).name = "Child";
    registry.emplace<Mesh>(e2).name = "ar15";
    SetParent({registry, e2}, e);

    auto hitboxShape = JPH::Ref(new JPH::SphereShape(0.75f));

    // Make hitbox/hurtbox collider.
    auto eHitbox                         = registry.create();
    registry.emplace<Name>(eHitbox).name = "Frog hitbox";
    registry.emplace<ForwardCollisionsToParent>(eHitbox);
    registry.emplace<RenderTransform>(eHitbox);
    registry.emplace<PreviousGlobalTransform>(eHitbox);
    auto& tpHitbox                             = registry.emplace<LocalTransform>(eHitbox);
    tpHitbox.position                          = {};
    tpHitbox.rotation                          = glm::identity<glm::quat>();
    tpHitbox.scale                             = 1;
    registry.emplace<GlobalTransform>(eHitbox) = {{}, glm::identity<glm::quat>(), 1};
    registry.emplace<Hierarchy>(eHitbox);
    Physics::AddRigidBody({registry, eHitbox},
      {
        .shape      = hitboxShape,
        .isSensor   = true,
        .motionType = JPH::EMotionType::Kinematic,
        .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
      });
    SetParent({registry, eHitbox}, e);

    return e;
  }
};

class FlyingFrogDefinition : public EntityPrefabDefinition
{
public:
  using EntityPrefabDefinition::EntityPrefabDefinition;

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat) const override
  {
    auto& registry = world.GetRegistry();
    auto sphere    = JPH::Ref(new JPH::SphereShape(0.4f));

    auto e                         = world.CreateRenderableEntity(position, {1, 0, 0, 0}, 0.4f);
    registry.emplace<Mesh>(e).name = "frog";
    registry.emplace<Name>(e, "Frog");
    registry.emplace<Health>(e) = {70, 70};
    registry.emplace<PredatoryBirdBehavior>(e);
    // registry_.emplace<Pathfinding::CachedPath>(e).timeBetweenUpdates = 1;
    registry.emplace<InputState>(e);
    registry.emplace<Loot>(e).name = "standard";
    registry.emplace<TeamFlags>(e, TeamFlagBits::ENEMY);
    registry.emplace<DespawnWhenFarFromPlayer>(e);
    registry.emplace<Enemy>(e);

    auto& contactDamage  = registry.emplace<ContactDamage>(e);
    contactDamage.damage = 15;

    registry.emplace<FlyingCharacterController>(e) = {.maxSpeed = 3.5, .acceleration = 15};
    registry.emplace_or_replace<LinearVelocity>(e);
    auto rb = Physics::AddRigidBody({registry, e}, {.shape = sphere, .layer = Physics::Layers::CHARACTER});
    Physics::GetBodyInterface().SetGravityFactor(rb.body, 0);

    auto hitboxShape = JPH::Ref(new JPH::SphereShape(0.95f));

    // Make hitbox/hurtbox collider.
    auto eHitbox                         = registry.create();
    registry.emplace<Name>(eHitbox).name = "Frog hitbox";
    registry.emplace<ForwardCollisionsToParent>(eHitbox);
    registry.emplace<RenderTransform>(eHitbox);
    registry.emplace<PreviousGlobalTransform>(eHitbox);
    auto& tpHitbox                             = registry.emplace<LocalTransform>(eHitbox);
    tpHitbox.position                          = {};
    tpHitbox.rotation                          = glm::identity<glm::quat>();
    tpHitbox.scale                             = 1;
    registry.emplace<GlobalTransform>(eHitbox) = {{}, glm::identity<glm::quat>(), 1};
    registry.emplace<Hierarchy>(eHitbox);
    Physics::AddRigidBody({registry, eHitbox},
      {
        .shape      = hitboxShape,
        .isSensor   = true,
        .motionType = JPH::EMotionType::Kinematic,
        .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
      });
    SetParent({registry, eHitbox}, e);

    return e;
  }
};

entt::entity CreateSnake(World& world, glm::vec3, glm::quat)
{
  auto& registry          = world.GetRegistry();
  entt::entity head       = entt::null;
  auto prevBody2          = std::optional<JPH::BodyID>();
  entt::entity prevEntity = entt::null;
  for (int i = 0; i < 15; i++)
  {
    auto sphere2Settings = JPH::SphereShapeSettings(0.5f);
    sphere2Settings.SetEmbedded();
    sphere2Settings.mDensity = 35.0f - i * 2;
    auto sphere2             = sphere2Settings.Create().Get();

    // const auto position             = glm::vec3{cos(glm::two_pi<float>() * i / 15.0f) * 4.0f, 50 + i / 5.0f, sin(glm::two_pi<float>() * i / 15.0f) * 4.0f};
    const auto position            = glm::vec3{20, 75, i / 0.8f};
    auto a                         = world.CreateRenderableEntity(position, glm::identity<glm::quat>(), i == 0 ? 0.5f : 1.0f);
    registry.emplace<Name>(a).name = i == 0 ? "Worm head" : "Worm body";
    registry.emplace<Mesh>(a).name = "frog";
    auto body                      = JPH::BodyID();
    if (i != 0)
    {
      auto rb = Physics::AddRigidBody({registry, a},
        {
          .shape      = sphere2,
          .activate   = true,
          .isSensor   = false,
          .motionType = JPH::EMotionType::Dynamic,
          .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
        });
      body    = rb.body;

      Physics::GetBodyInterface().SetGravityFactor(rb.body, 1);
      Physics::GetBodyInterface().SetMotionQuality(rb.body, JPH::EMotionQuality::LinearCast);
    }

    if (i == 0)
    {
      head     = a;
      auto& cc = Physics::AddCharacterControllerShrimple({registry, a}, {.shape = sphere2});
      body     = cc.character->GetBodyID();
      // registry.emplace<NoclipCharacterController>(a);
      registry.emplace<SimplePathfindingEnemyBehavior>(a);
      registry.emplace<Pathfinding::CachedPath>(a).timeBetweenUpdates = 1;
      // registry.emplace<SimpleEnemyBehavior>(a);
      registry.emplace<InputState>(a);
      registry.emplace<TeamFlags>(a, TeamFlagBits::ENEMY);
      registry.emplace<Health>(a)    = {100, 100};
      registry.emplace<Loot>(a).name = "standard";
      registry.emplace<DespawnWhenFarFromPlayer>(a);
      registry.emplace<Enemy>(a);
    }

    if (prevBody2)
    {
      registry.emplace<ForwardCollisionsToParent>(a);
      auto prevPos = registry.get<GlobalTransform>(a).position;
      // if (i == 1)
      //{
      //   //SetLocalPosition(a, prevPos);
      //   //auto settings = JPH::Ref(new JPH::FixedConstraintSettings());
      //   //settings->mAutoDetectPoint = true;
      //   //auto constraint            = Physics::GetBodyInterface().CreateConstraint(settings, *prevBody2, rb.body);
      //   //Physics::GetPhysicsSystem().AddConstraint(constraint);
      //    auto settings   = JPH::Ref(new JPH::DistanceConstraintSettings());
      //    settings->mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
      //    settings->mMinDistance = 0.95f;
      //    settings->mMaxDistance = 1;
      //    settings->mConstraintPriority = 1'000'000'000 - i * 1000;
      //    auto constraint               = Physics::GetBodyInterface().CreateConstraint(settings, *prevBody2, body);
      //    Physics::GetPhysicsSystem().AddConstraint(constraint);
      // }
      // else
      {
        auto settings = JPH::Ref(new JPH::SwingTwistConstraintSettings);
        // settings->mPosition1           = settings->mPosition2  = Physics::ToJolt(position) + JPH::Vec3(-0.5f, 0, 0);
        settings->mPosition1 = settings->mPosition2 = Physics::ToJolt((prevPos + position) / 2.0f);
        settings->mTwistAxis1 = settings->mTwistAxis2 = JPH::Vec3::sAxisX();
        settings->mPlaneAxis1 = settings->mPlaneAxis2 = JPH::Vec3::sAxisY();
        settings->mNormalHalfConeAngle                = JPH::DegreesToRadians(60);
        settings->mPlaneHalfConeAngle                 = JPH::DegreesToRadians(60);
        settings->mTwistMinAngle                      = JPH::DegreesToRadians(-20);
        settings->mTwistMaxAngle                      = JPH::DegreesToRadians(20);
        // auto settings   = JPH::Ref(new JPH::DistanceConstraintSettings());
        // settings->mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
        // settings->mMinDistance = 0.95f;
        // settings->mMaxDistance = 1;
        auto constraint = Physics::GetBodyInterface().CreateConstraint(settings, *prevBody2, body);
        // constraint->SetNumPositionStepsOverride(100);
        Physics::RegisterConstraint(constraint, *prevBody2, body);
      }
      auto& h                    = registry.get<Hierarchy>(a);
      h.useLocalPositionAsGlobal = true;
      h.useLocalRotationAsGlobal = true;
      SetParent({registry, a}, prevEntity);

      auto hitboxShape                     = JPH::Ref(new JPH::SphereShape(0.5f));
      auto eHitbox                         = registry.create();
      registry.emplace<Name>(eHitbox).name = "Worm hitbox";
      registry.emplace<ForwardCollisionsToParent>(eHitbox);
      registry.emplace<PreviousGlobalTransform>(eHitbox);
      auto& tpHitbox                             = registry.emplace<LocalTransform>(eHitbox);
      tpHitbox.position                          = {};
      tpHitbox.rotation                          = glm::identity<glm::quat>();
      tpHitbox.scale                             = 1;
      registry.emplace<GlobalTransform>(eHitbox) = {{}, glm::identity<glm::quat>(), 1};
      registry.emplace<Hierarchy>(eHitbox);
      Physics::AddRigidBody({registry, eHitbox},
        {
          .shape      = hitboxShape,
          .isSensor   = true,
          .motionType = JPH::EMotionType::Kinematic,
          .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
        });
      SetParent({registry, eHitbox}, a);
    }
    prevBody2  = body;
    prevEntity = a;
  }
  return head;
}

entt::entity CreateTunnelingWorm(World& world, glm::vec3 position, glm::quat)
{
  auto& registry          = world.GetRegistry();
  entt::entity head       = entt::null;
  auto prevBody2          = std::optional<JPH::BodyID>();
  entt::entity prevEntity = entt::null;
  for (int i = 0; i < 10; i++)
  {
    auto sphere2Settings = JPH::SphereShapeSettings(0.5f);
    sphere2Settings.SetEmbedded();
    sphere2Settings.mDensity = 100000.0f / (10 * i + 1.0f);
    auto sphere2             = sphere2Settings.Create().Get();

    auto a                         = world.CreateRenderableEntity(position + glm::vec3{0, 0, i / 1.0f}, glm::identity<glm::quat>(), i == 0 ? 0.5f : 1.0f);
    registry.emplace<Name>(a).name = i == 0 ? "Worm head" : "Worm body";
    registry.emplace<Mesh>(a).name = "frog";
    auto body                      = JPH::BodyID();

    auto rb = Physics::AddRigidBody({registry, a},
      {
        .shape      = sphere2,
        .activate   = true,
        .isSensor   = true,
        .motionType = i == 0 ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
        .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
      });

    body = rb.body;

    Physics::GetBodyInterface().SetGravityFactor(rb.body, 0.0f);

    registry.emplace<Friction>(a, glm::vec3(i == 0 ? 5.0f : 0.2f));

    if (i == 0)
    {
      head = a;

      registry.emplace<FlyingCharacterController>(a)               = {.maxSpeed = 9, .acceleration = 35.0f};
      registry.emplace<WormEnemyBehavior>(a).maxTurnSpeedDegPerSec = 65;
      registry.emplace<InputState>(a);
      registry.emplace<ContactDamage>(a) = {.damage = 20, .knockback = 5};
      registry.emplace<TeamFlags>(a, TeamFlagBits::ENEMY);
      registry.emplace<Health>(a)                     = {500, 500};
      registry.emplace<Loot>(a).name                  = "standard";
      registry.emplace<KnockbackMultiplier>(a).factor = 2.0f;
      registry.emplace<DespawnWhenFarFromPlayer>(a);
      registry.emplace<Enemy>(a);
    }

    if (prevBody2)
    {
      registry.emplace<ForwardCollisionsToParent>(a);
      auto prevPos = registry.get<GlobalTransform>(a).position;
      // auto settings = JPH::Ref(new JPH::SwingTwistConstraintSettings);
      //// settings->mPosition1           = settings->mPosition2  = Physics::ToJolt(position) + JPH::Vec3(-0.5f, 0, 0);
      // settings->mPosition1 = settings->mPosition2 = Physics::ToJolt((prevPos + position) / 2.0f);
      // settings->mTwistAxis1 = settings->mTwistAxis2 = JPH::Vec3::sAxisX();
      // settings->mPlaneAxis1 = settings->mPlaneAxis2 = JPH::Vec3::sAxisY();
      // settings->mNormalHalfConeAngle                = JPH::DegreesToRadians(30);
      // settings->mPlaneHalfConeAngle                 = JPH::DegreesToRadians(30);
      // settings->mTwistMinAngle                      = JPH::DegreesToRadians(-20);
      // settings->mTwistMaxAngle                      = JPH::DegreesToRadians(20);

      auto settings          = JPH::Ref(new JPH::DistanceConstraintSettings());
      settings->mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
      settings->mMinDistance = 1;
      settings->mMaxDistance = 1;

      // auto settings   = JPH::Ref(new JPH::FixedConstraintSettings());
      // settings->mAutoDetectPoint = true;

      auto constraint = Physics::GetBodyInterface().CreateConstraint(settings, *prevBody2, body);
      // constraint->SetNumPositionStepsOverride(10);
      Physics::RegisterConstraint(constraint, *prevBody2, body);

      auto& h                    = registry.get<Hierarchy>(a);
      h.useLocalPositionAsGlobal = true;
      h.useLocalRotationAsGlobal = true;
      SetParent({registry, a}, prevEntity);

      auto hitboxShape                     = JPH::Ref(new JPH::SphereShape(0.5f));
      auto eHitbox                         = registry.create();
      registry.emplace<Name>(eHitbox).name = "Worm hitbox";
      registry.emplace<ForwardCollisionsToParent>(eHitbox);
      registry.emplace<PreviousGlobalTransform>(eHitbox);
      auto& tpHitbox                             = registry.emplace<LocalTransform>(eHitbox);
      tpHitbox.position                          = {};
      tpHitbox.rotation                          = glm::identity<glm::quat>();
      tpHitbox.scale                             = 1;
      registry.emplace<GlobalTransform>(eHitbox) = {{}, glm::identity<glm::quat>(), 1};
      registry.emplace<Hierarchy>(eHitbox);
      Physics::AddRigidBody({registry, eHitbox},
        {
          .shape      = hitboxShape,
          .isSensor   = true,
          .motionType = JPH::EMotionType::Kinematic,
          .layer      = Physics::Layers::HITBOX_AND_HURTBOX,
        });
      SetParent({registry, eHitbox}, a);
    }
    prevBody2  = body;
    prevEntity = a;
  }
  return head;
}

class TorchDefinition : public EntityPrefabDefinition
{
public:
  using EntityPrefabDefinition::EntityPrefabDefinition;

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat rotation) const override
  {
    auto& registry    = world.GetRegistry();
    const auto entity = world.CreateRenderableEntity(position, rotation);
    registry.emplace<Mesh>(entity, "torch");
    registry.emplace<Name>(entity, "Torch");
    auto& light     = registry.emplace<GpuLight>(entity);
    light.type      = LIGHT_TYPE_POINT;
    light.color     = {1, 0.58f, 0.3f};
    light.intensity = 15;
    light.range     = 200;
    return entity;
  }
};

class ChestDefinition : public EntityPrefabDefinition
{
public:
  using EntityPrefabDefinition::EntityPrefabDefinition;

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat rotation) const override
  {
    auto& registry    = world.GetRegistry();
    const auto entity = world.CreateRenderableEntity(position, rotation);
    registry.emplace<Name>(entity, "Chest");
    registry.emplace<Inventory>(entity, world).canHaveActiveItem = false;
    return entity;
  }
};

class ShrimpleMeshPrefabDefinition : public EntityPrefabDefinition
{
public:
  explicit ShrimpleMeshPrefabDefinition(std::string_view model, const CreateInfo& createInfo = {}) : EntityPrefabDefinition(createInfo), modelName(model) {}

  entt::entity Spawn(World& world, glm::vec3 position, glm::quat rotation) const override
  {
    auto& registry    = world.GetRegistry();
    const auto entity = world.CreateRenderableEntity(position, rotation);
    registry.emplace<Mesh>(entity, modelName);
    registry.emplace<Name>(entity, modelName);
    return entity;
  }

private:
  std::string modelName;
};



void Game::Run()
{
  ZoneScoped;
  isRunning_ = true;

  auto previousTimestamp  = std::chrono::steady_clock::now();
  double fixedUpdateAccum = 0;

  while (isRunning_)
  {
    ZoneScopedN("Main Loop");
#if GAME_CATCH_EXCEPTIONS
    try
#endif
    {
      const auto timeScale      = world_->GetRegistry().ctx().get<TimeScale>().scale;
      const auto tickHz         = world_->GetRegistry().ctx().get<TickRate>().hz;
      const double tickDuration = 1.0 / tickHz;

      const auto currentTimestamp = std::chrono::steady_clock::now();
      const auto realDeltaTime    = std::chrono::duration_cast<std::chrono::microseconds>(currentTimestamp - previousTimestamp).count() / 1'000'000.0;
      previousTimestamp           = currentTimestamp;

      auto dt = DeltaTime{
        .game     = static_cast<float>(realDeltaTime * timeScale),
        .real     = static_cast<float>(realDeltaTime),
        .fraction = float(fixedUpdateAccum / tickDuration),
      };

      if (head_)
      {
        head_->VariableUpdatePre(dt, *world_);
      }

      constexpr int MAX_TICKS = 10;
      int accumTicks          = 0;
      fixedUpdateAccum += realDeltaTime * timeScale;
      while (fixedUpdateAccum > tickDuration && accumTicks++ < MAX_TICKS)
      {
        fixedUpdateAccum -= tickDuration;
        // TODO: Networking update before FixedUpdate
        world_->FixedUpdate(static_cast<float>(tickDuration));
      }

      dt.fraction = std::clamp(float(fixedUpdateAccum / tickDuration), 0.0f, 1.0f);

      if (head_)
      {
        head_->VariableUpdatePost(dt, *world_);
      }

      if (world_->GetRegistry().ctx().contains<CloseApplication>())
      {
        isRunning_ = false;
      }
    }
#if GAME_CATCH_EXCEPTIONS
    catch(std::exception& e)
    {
      fprintf(stderr, "Exception caught: %s\n", e.what());
      throw;
    }
#endif
  }
}

void World::FixedUpdate(float dt)
{
  ZoneScoped;
  if (registry_.ctx().get<GameState>() == GameState::GAME)
  {
    registry_.ctx().get<float>("time"_hs) += dt;
#ifndef GAME_HEADLESS
    registry_.ctx().get<std::vector<Debug::Line>>().clear();
#endif

    assert(registry_.view<LocalPlayer>().size() <= 1);

    // Update previous transforms before updating it (this should be done after updating the game state from networking)
    for (auto&& [entity, transform, interpolatedTransform] : registry_.view<GlobalTransform, PreviousGlobalTransform>().each())
    {
      interpolatedTransform.position = transform.position;
      interpolatedTransform.rotation = transform.rotation;
      interpolatedTransform.scale    = transform.scale;
    }

    for (auto&& [entity] : registry_.view<LocalPlayer>().each())
    {
      UpdateLocalTransform({registry_, entity});
    }

    registry_.ctx().get<NpcSpawnDirector>().Update(dt);
    
    Physics::FixedUpdate(dt, *this);

    // Clamp movement input
    for (auto&& [entity, input] : registry_.view<InputState>().each())
    {
      input.strafe  = glm::clamp(input.strafe, -1.0f, 1.0f);
      input.forward = glm::clamp(input.forward, -1.0f, 1.0f);
      input.elevate = glm::clamp(input.elevate, -1.0f, 1.0f);
    }

    // Process linear transform paths
    for (auto&& [entity, linearPath, transform] : registry_.view<LinearPath, LocalTransform>().each())
    {
      assert(!linearPath.frames.empty());
      if (linearPath.secondsElapsed <= 0)
      {
        linearPath.originalLocalTransform = transform;
      }

      linearPath.secondsElapsed += dt;

      // See if the path is finished, reset original transform
      {
        float sum = 0;
        for (const auto& frame : linearPath.frames)
        {
          sum += frame.offsetSeconds;
        }
        if (linearPath.secondsElapsed > sum)
        {
          transform = linearPath.originalLocalTransform;
          registry_.remove<LinearPath>(entity);
          continue;
        }
      }

      // Locate frames to interpolate between with simple linear search
      LinearPath::KeyFrame firstFrame;
      LinearPath::KeyFrame secondFrame = {};
      float sum                        = 0;
      for (const auto& frame : linearPath.frames)
      {
        firstFrame = secondFrame;
        secondFrame = frame;
        sum += frame.offsetSeconds;
        if (sum >= linearPath.secondsElapsed)
        {
          break;
        }
      }

      // Do da interpolate
      const float alpha                   = (linearPath.secondsElapsed - firstFrame.offsetSeconds) / (sum - firstFrame.offsetSeconds);
      const glm::vec3 newRelativePosition = glm::mix(firstFrame.position, secondFrame.position, alpha);
      const glm::quat newRelativeRotation = glm::slerp(firstFrame.rotation, secondFrame.rotation, alpha);
      const float newRelativeScale        = glm::mix(firstFrame.scale, secondFrame.scale, alpha);

      // Apply new relative transform stuff relatively to the original transform
      transform.position = linearPath.originalLocalTransform.position + newRelativePosition;
      transform.rotation = linearPath.originalLocalTransform.rotation * newRelativeRotation;
      transform.scale    = linearPath.originalLocalTransform.scale * newRelativeScale;

      UpdateLocalTransform({registry_, entity});
    }

    // Avians
    for (auto&& [entity, input, transform, behavior] : registry_.view<InputState, LocalTransform, PredatoryBirdBehavior>().each())
    {
      behavior.accum += dt;

      const auto nearestPlayer = GetNearestPlayer(transform.position);

      if (nearestPlayer != entt::null)
      {
        const auto& pt = registry_.get<GlobalTransform>(nearestPlayer);
        if (glm::distance(pt.position, transform.position) < 20)
        {
          behavior.target = nearestPlayer;

          if (behavior.state == PredatoryBirdBehavior::State::IDLE)
          {
            behavior.state = PredatoryBirdBehavior::State::CIRCLING;
            behavior.accum = 0;
          }
        }
        else if (behavior.state != PredatoryBirdBehavior::State::IDLE)
        {
          behavior.state = PredatoryBirdBehavior::State::IDLE;
          behavior.idlePosition = transform.position + glm::vec3(0, 5, 0);
        }
      }
      else if (behavior.state != PredatoryBirdBehavior::State::IDLE)
      {
        behavior.state = PredatoryBirdBehavior::State::IDLE;
        behavior.idlePosition = transform.position + glm::vec3(0, 5, 0);
      }

      // Birds always be moving forward.
      input.forward = 1;

      switch (behavior.state)
      {
      case PredatoryBirdBehavior::State::IDLE:
      {
        const auto target = behavior.idlePosition + glm::vec3(sin(behavior.accum * 1.2f) * 8, 2 + sin(behavior.accum * 4) * 2, cos(behavior.accum * 1.2f) * 8);
        transform.rotation = glm::quatLookAtRH(glm::normalize(target - transform.position), {0, 1, 0});
        input.forward      = 0.5f;
        break;
      }
      case PredatoryBirdBehavior::State::CIRCLING:
      {
        const auto& pt = registry_.get<GlobalTransform>(behavior.target);
        const auto target = pt.position + glm::vec3(sin(behavior.accum * 1.2f) * 8, 4 + sin(behavior.accum * 4) * 2, cos(behavior.accum * 1.2f) * 8);

        transform.rotation = glm::quatLookAtRH(glm::normalize(target - transform.position), {0, 1, 0});
        
        auto rayCast = JPH::RRayCast(Physics::ToJolt(transform.position), Physics::ToJolt(target - transform.position));
        auto result  = JPH::RayCastResult();
        const bool hit = Physics::GetNarrowPhaseQuery().CastRay(rayCast,
          result,
          Physics::GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_WORLD),
          Physics::GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_WORLD));
        if (!hit)
        {
          behavior.lineOfSightDuration += dt;
          if (behavior.accum >= 5.0f && behavior.lineOfSightDuration >= 1)
          {
            behavior.accum = 0;
            behavior.state = PredatoryBirdBehavior::State::SWOOPING;
          }
        }
        else
        {
          behavior.lineOfSightDuration = 0;
        }
        break;
      }
      case PredatoryBirdBehavior::State::SWOOPING:
      {
        const auto& pt    = registry_.get<GlobalTransform>(behavior.target);
        transform.rotation = glm::quatLookAtRH(glm::normalize(pt.position - transform.position), {0, 1, 0});

        if (glm::distance(pt.position, transform.position) < 1.5f || behavior.accum > 5.0f)
        {
          behavior.accum = 0;
          behavior.state = PredatoryBirdBehavior::State::CIRCLING;
        }

        break;
      }
      default:;
      }

      UpdateLocalTransform({registry_, entity});
    }

    // Reset AI targets if target is invalid or dead.
    for (auto&& [entity, target] : registry_.view<AiTarget>().each())
    {
      if (!registry_.valid(target.currentTarget) || registry_.all_of<GhostPlayer>(target.currentTarget))
      {
        target.currentTarget = entt::null;
      }
    }

    // Generate input for enemies
    {
      ZoneScopedN("Pathfinding");
      // Won't work if entity is a child.
      for (auto&& [entity, input, aiTransform] : registry_.view<InputState, LocalTransform>(entt::exclude<Player>).each())
      {
        entt::entity pe = entt::null;
        GlobalTransform* pt = nullptr;
        float nearestDist2  = INFINITY;

        // Try to find the nearest player that satisfies target conditions.
        bool sawSomething = false;
        for (auto&& [pEntity, player, playerTransform] : registry_.view<Player, GlobalTransform>(entt::exclude<GhostPlayer>).each())
        {
          bool isCandidate = false;
          if (!registry_.any_of<AiVision, AiHearing>(entity))
          {
            isCandidate = true;
          }

          const auto dist2 = Math::Distance2(playerTransform.position, aiTransform.position);
          if (auto* aiv = registry_.try_get<AiVision>(entity))
          {
            // TODO: shoot visibility ray(s).
            if (dist2 < aiv->distance * aiv->distance && glm::acos(glm::dot(glm::normalize(playerTransform.position - aiTransform.position), GetForward(aiTransform.rotation))) < aiv->coneAngleRad)
            {
              aiv->accumulator += dt;
              sawSomething = true;
              if (aiv->accumulator > aiv->invAcuity)
              {
                isCandidate = true;
              }
            }
          }

          if (auto* aih = registry_.try_get<AiHearing>(entity))
          {
            if (dist2 < aih->distance * aih->distance)
            {
              isCandidate = true;
            }
          }

          if (isCandidate && dist2 < nearestDist2)
          {
            nearestDist2 = dist2;
            pe           = pEntity;
            pt           = &playerTransform;
          }
        }

        if (auto* aiv = registry_.try_get<AiVision>(entity); aiv && !sawSomething)
        {
          aiv->accumulator = glm::max(aiv->accumulator - dt, 0.0f);
        }

        auto* aiTarget = registry_.try_get<AiTarget>(entity);
        
        const bool hasValidTarget = (aiTarget && registry_.valid(aiTarget->currentTarget)) || pe != entt::null;

        // Update aiTarget if it didn't have a valid target.
        if (aiTarget && !registry_.valid(aiTarget->currentTarget) && pe != entt::null)
        {
          aiTarget->currentTarget = pe;
        }

        // If there was already a target, maintain it.
        if (aiTarget && registry_.valid(aiTarget->currentTarget))
        {
          pe = aiTarget->currentTarget;
          pt = registry_.try_get<GlobalTransform>(pe);
        }

        if (hasValidTarget && registry_.all_of<SimpleEnemyBehavior>(entity))
        {
          if (pt->position.y > aiTransform.position.y)
          {
            input.jump = true;
          }

          aiTransform.rotation = glm::quatLookAtRH(glm::normalize(pt->position - aiTransform.position), {0, 1, 0});

          input.forward = 1;
        }

        if (auto* w = registry_.try_get<WormEnemyBehavior>(entity); w && hasValidTarget)
        {
          const auto desiredRotation = glm::quatLookAtRH(glm::normalize(pt->position - aiTransform.position), {0, 1, 0});
          const auto angle           = glm::acos(glm::dot(GetForward(desiredRotation), GetForward(aiTransform.rotation)));

          aiTransform.rotation = glm::slerp(aiTransform.rotation, desiredRotation, glm::min(1.0f, glm::radians(w->maxTurnSpeedDegPerSec * dt) / angle));

          input.forward = 1;
        }

        auto* cp = registry_.try_get<Pathfinding::CachedPath>(entity);
        bool shouldFindPath = true;
        if (cp)
        {
          cp->updateAccum += dt;
          if (cp->updateAccum >= cp->timeBetweenUpdates)
          {
            cp->updateAccum = 0;
            cp->progress    = 0;
          }
          else
          {
            shouldFindPath = false;
          }
        }

        Pathfinding::Path path;
        const auto myFootPos = glm::ivec3(glm::floor(GetFootPosition({registry_, entity})));
        const auto myHeight  = (int)std::ceil(GetHeight({registry_, entity}));

        // Get cached path.
        if (cp)
        {
          path = cp->path;
        }

        if (hasValidTarget && registry_.any_of<SimplePathfindingEnemyBehavior>(entity))
        {
          if (!(cp && !shouldFindPath))
          {
            // For ground characters, cast the player down. That way, if the player is in the air, the character will at least try to get under them instead of giving up.
            auto targetFootPos = glm::ivec3(pt->position);
            if (const auto* pc = registry_.try_get<Physics::CharacterController>(pe))
            {
              const auto& playerCharacter = pc->character;
              const auto* playerShape     = playerCharacter->GetShape();
              auto shapeCast              = JPH::RShapeCast::sFromWorldTransform(playerShape, {1, 1, 1}, playerCharacter->GetWorldTransform(), {0, -10, 0});
              auto shapeCastCollector     = Physics::NearestHitCollector();
              Physics::GetNarrowPhaseQuery().CastShape(shapeCast,
                {},
                {},
                shapeCastCollector,
                Physics::GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_WORLD));
              if (shapeCastCollector.nearest)
              {
                targetFootPos = Physics::ToGlm(shapeCast.GetPointOnRay(shapeCastCollector.nearest->mFraction - 1e-2f));
              }
            }

            // path                 = Pathfinding::FindPath(*this, {.start = myFootPos, .goal = targetFootPos, .height = myHeight, .w = 1.5f});
            path = registry_.ctx().get<Pathfinding::PathCache>().FindOrGetCachedPath(*this,
              {
                .start  = glm::ivec3(myFootPos),
                .goal   = glm::ivec3(targetFootPos),
                .height = myHeight,
                .w      = 1.5f,
                .canFly = registry_.any_of<FlyingCharacterController>(entity),
              });

            if (cp)
            {
              cp->path = path;
            }
          }
        }

        // Only try to wander if there's no valid target.
        if (auto* wb = registry_.try_get<AiWanderBehavior>(entity); wb && !hasValidTarget)
        {
          wb->accumulator += dt;
          if (wb->accumulator > wb->timeBetweenMoves)
          {
            wb->accumulator  = 0;
            const auto& grid = registry_.ctx().get<TwoLevelGrid>();
            auto& rng        = registry_.ctx().get<PCG::Rng>();
            for (int i = 0; i < 5; i++)
            {
              if (auto pos = SampleWalkablePosition(grid, rng, aiTransform.position, wb->minWanderDistance, wb->maxWanderDistance, wb->targetCanBeFloating))
              {
                path = registry_.ctx().get<Pathfinding::PathCache>().FindOrGetCachedPath(*this,
                  {
                    .start  = glm::ivec3(myFootPos),
                    .goal   = glm::ivec3(*pos),
                    .height = myHeight,
                    .w      = 1.5f,
                    .canFly = registry_.any_of<FlyingCharacterController>(entity),
                    .maxNodesToSearch = 100,
                  });

                if (cp)
                {
                  cp->path = path;
                }
                break;
              }
            }
          }
        }

        if (!path.empty())
        {
#ifndef GAME_HEADLESS
          // Render path
          auto& lines = registry_.ctx().get<std::vector<Debug::Line>>();
          for (size_t i = 1; i < path.size(); i++)
          {
            lines.emplace_back(Debug::Line{
              .aPosition = path[i - 1],
              .aColor    = glm::vec4(0, 0, 1, 1),
              .bPosition = path[i + 0],
              .bColor    = glm::vec4(0, 0, 1, 1),
            });
          }
#endif

          auto nextNode = path.front();
          if (cp && cp->progress < path.size())
          {
            nextNode = path[cp->progress];
            if (cp->progress < path.size() - 1 && glm::distance(nextNode, glm::vec3(myFootPos)) <= 1.25f)
            {
              cp->progress++;
            }
          }

          if (cp && glm::distance(glm::vec3(myFootPos), cp->path.back()) < 1.0f)
          {
            cp->path.clear();
            cp->progress = 0;
          }

          if (nextNode.y > myFootPos.y + 0.5f)
          {
            input.jump = true;
          }
          aiTransform.rotation = glm::quatLookAtRH(glm::normalize(nextNode - aiTransform.position), {0, 1, 0});
          input.forward        = 1;
        }

        UpdateLocalTransform({registry_, entity});
      }
    }

    // Apply input (could be generated by players or NPCs!)
    for (auto&& [entity, input, transform] : registry_.view<InputState, LocalTransform>(entt::exclude<GhostPlayer>).each())
    {
      // Movement
      if (registry_.all_of<NoclipCharacterController>(entity))
      {
        const auto right     = GetRight(transform.rotation);
        const auto forward   = GetForward(transform.rotation);
        auto tempCameraSpeed = 14.5f;
        tempCameraSpeed *= input.sprint ? 4.0f : 1.0f;
        tempCameraSpeed *= input.walk ? 0.25f : 1.0f;
        auto velocity = glm::vec3(0);
        velocity += input.forward * forward * tempCameraSpeed;
        velocity += input.strafe * right * tempCameraSpeed;
        velocity.y += input.elevate * tempCameraSpeed;
        transform.position += velocity * dt;
        UpdateLocalTransform({registry_, entity});
        registry_.get_or_emplace<LinearVelocity>(entity).v = velocity;
      }

      if (auto* fc = registry_.try_get<FlyingCharacterController>(entity))
      {
        auto& velocity       = registry_.get<LinearVelocity>(entity).v;
        const auto right     = GetRight(transform.rotation);
        const auto forward   = GetForward(transform.rotation);
        const auto dv = fc->acceleration * dt;

        velocity += input.forward * forward * dv;
        velocity += input.strafe * right * dv;
        velocity += input.elevate * glm::vec3(0, 1, 0) * dv;

        if (glm::length(velocity) > fc->maxSpeed)
        {
          velocity = glm::normalize(velocity) * fc->maxSpeed;
        }

        transform.position += velocity * dt;

        UpdateLocalTransform({registry_, entity});
      }

      if (auto* attribs = registry_.try_get<WalkingMovementAttributes>(entity);
        attribs && registry_.any_of<Physics::CharacterController, Physics::CharacterControllerShrimple>(entity))
      {
        const auto rot   = glm::mat3_cast(transform.rotation);
        const auto right = rot[0];
        const auto gUp   = glm::vec3(0, 1, 0);
        // right and up will never be collinear if roll doesn't change
        const auto forward = glm::normalize(glm::cross(gUp, right));

        // Physics engine factors in deltaTime already
        float tempSpeed = attribs->runBaseSpeed;
        //tempSpeed *= input.sprint ? 2.0f : 1.0f;
        tempSpeed *= input.walk ? attribs->walkModifier : 1.0f;

        auto deltaVelocity = glm::vec3(0);
        deltaVelocity += input.forward * forward * tempSpeed;
        deltaVelocity += input.strafe * right * tempSpeed;

        if (auto* cc = registry_.try_get<Physics::CharacterController>(entity))
        {
          auto& velocity    = registry_.get<LinearVelocity>(entity).v;
          auto prevVelocity = velocity;
          if (cc->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround)
          {
            deltaVelocity += input.jump ? gUp * 8.0f : glm::vec3(0);
            prevVelocity.y = 0;
          }
          else // if (cc->character->GetGroundState() == JPH::CharacterBase::EGroundState::InAir)
          {
            const auto prevY = velocity.y;
            //const auto prevY = cc->character->GetPosition().GetY() - cc->previousPosition.y;
            deltaVelocity += glm::vec3{0, prevY - 15 * dt, 0};
            // velocity += glm::vec3{0, -15 * dt, 0};
          }
          
          // cc->character->CheckCollision(cc->character->GetPosition(), cc->character->GetRotation(), Physics::ToJolt(velocity), 1e-4f, )
          velocity = deltaVelocity;
          // printf("ground state: %d. height = %f. velocity.y = %f\n", (int)cc->character->GetGroundState(), cc->character->GetPosition().GetY(), velocity.y);
          // cc->character->AddLinearVelocity(Physics::ToJolt(velocity ));
          // cc->character->AddImpulse(Physics::ToJolt(velocity));
        }

        if (auto* cs = registry_.try_get<Physics::CharacterControllerShrimple>(entity))
        {
          auto velocity                  = registry_.get<LinearVelocity>(entity).v;
          auto& friction                 = registry_.get_or_emplace<Friction>(entity).axes;
          constexpr auto groundFriction = glm::vec3(6.0f);
          friction                 = groundFriction;
          if (cs->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround)
          {
            deltaVelocity += input.jump ? gUp * 8.0f : glm::vec3(0);
            // Make it possible to launch entities up with an impulse
            if (cs->previousGroundState == JPH::CharacterBase::EGroundState::OnGround)
            {
              velocity.y = 0;
            }
          }
          else
          {
            constexpr float airControl = 0.4f;
            constexpr auto airFriction = glm::vec3(0.05f);
            friction                    = airFriction;
            deltaVelocity.x *= airControl;
            deltaVelocity.z *= airControl;
            //const auto prevY = cs->character->GetLinearVelocity().GetY();
            //deltaVelocity += glm::vec3{0, prevY - 15 * dt, 0};
            deltaVelocity += glm::vec3{0, -15 * dt, 0};
          }
          friction.y               = 0;

          // Apply friction
          auto newVelocity = glm::vec3(velocity.x, 0, velocity.z);
          //newVelocity -= friction * newVelocity * dt;

          // Apply dv
          newVelocity.x += deltaVelocity.x;
          newVelocity.z += deltaVelocity.z;

          // Clamp xz speed
          const float speed = glm::length(newVelocity);
          if (speed > attribs->runMaxSpeed)
          {
            newVelocity = glm::normalize(newVelocity) * attribs->runMaxSpeed;
          }

          // Y is not affected by speed clamp or friction
          newVelocity.y = velocity.y + deltaVelocity.y;
          
          registry_.get<LinearVelocity>(entity).v = newVelocity;
        }
      }
    }

    // Close open containers if too far away.
    for (auto&& [entity, player, transform] : registry_.view<Player, GlobalTransform>().each())
    {
      if (registry_.valid(player.openContainerId))
      {
        player.showInteractPrompt = false;
        if (!registry_.all_of<Inventory>(player.openContainerId))
        {
          player.openContainerId = entt::null;
          continue;
        }

        if (auto* ct = registry_.try_get<GlobalTransform>(player.openContainerId); ct && glm::distance(transform.position, ct->position) > 6)
        {
          player.openContainerId = entt::null;
          continue;
        }
      }
      else
      {
        player.openContainerId = entt::null;
      }
    }

    // Player interaction
    for (auto&& [entity, player, transform, input, inventory] : registry_.view<Player, GlobalTransform, InputState, Inventory>(entt::exclude<GhostPlayer>).each())
    {
      const auto forward = GetForward(transform.rotation);
      constexpr float RAY_LENGTH = 4.0f;
      auto rayCast       = JPH::RRayCast(Physics::ToJolt(transform.position), Physics::ToJolt(forward * RAY_LENGTH));
      entt::entity hitEntity = entt::null;
      
      auto result = JPH::RayCastResult();
      if (Physics::GetNarrowPhaseQuery().CastRay(rayCast,
            result,
            Physics::GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::CAST_PROJECTILE),
            Physics::GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::CAST_PROJECTILE),
            *Physics::GetIgnoreEntityAndChildrenFilter({registry_, entity})))
      {
        hitEntity = static_cast<entt::entity>(Physics::GetBodyInterface().GetUserData(result.mBodyID));
        if (registry_.valid(hitEntity))
        {
          // Ray actually hit a voxel... see if it hit a voxel with a corresponding block entity.
          if (registry_.all_of<Voxels>(hitEntity))
          {
            const auto hitPos = transform.position + forward * (result.mFraction * RAY_LENGTH + 1e-3f);
            const auto voxelHitPos = glm::ivec3(hitPos);
            for (auto e2 : GetEntitiesInSphere(glm::vec3(voxelHitPos) + glm::vec3(0.5f), 0.25f))
            {
              if (registry_.all_of<BlockEntity>(e2))
              {
                hitEntity = e2;
                break;
              }
            }
          }

          if (auto [e3, _] = GetComponentFromAncestorOrDescendant<Inventory>(hitEntity); e3 != entt::null)
          {
            hitEntity                 = e3;
            player.showInteractPrompt = true;
          }
          else
          {
            player.showInteractPrompt = false;
          }
        }
      }
      else
      {
        player.showInteractPrompt = false;
      }

      if (input.interact)
      {
        if (registry_.valid(hitEntity))
        {
          if (auto [ent, inv] = GetComponentFromAncestor<Inventory>(hitEntity); inv)
          {
            player.openContainerId = ent;
          }
        }
      }

      if (input.usePrimary)
      {
        if (inventory.ActiveSlot().id != nullItem)
        {
          const auto& def = registry_.ctx().get<ItemRegistry>().Get(inventory.ActiveSlot().id);
          def.UsePrimary(dt, *this, inventory.activeSlotEntity, inventory.ActiveSlot());
          if (inventory.ActiveSlot().count <= 0)
          {
            inventory.OverwriteSlot(inventory.activeSlotCoord, {}, entt::null);
          }
        }
      }
    }

    // Update items in inventories (important to ensure cooldowns, etc. reset even when items are put away).
    for (auto&& [entity, player, inventory] : registry_.view<Player, Inventory>(entt::exclude<GhostPlayer>).each())
    {
      for (size_t row = 0; row < inventory.height; row++)
      {
        for (size_t col = 0; col < inventory.width; col++)
        {
          auto& slot = inventory.slots[row][col];
          if (slot.id != nullItem)
          {
            entt::entity self = entt::null;
            if (inventory.activeSlotCoord == glm::ivec2{row, col})
            {
              self = inventory.activeSlotEntity;
            }
            registry_.ctx().get<ItemRegistry>().Get(slot.id).Update(dt, *this, self, slot);
          }
        }
      }
    }

    for (auto&& [entity, cannotPickUp] : registry_.view<CannotBePickedUp>().each())
    {
      cannotPickUp.remainingSeconds -= dt;
      if (cannotPickUp.remainingSeconds <= 0)
      {
        registry_.remove<CannotBePickedUp>(entity);
      }
    }

    // Dropped items get sucked towards the player as if by magnetic attraction.
    for (auto&& [entity, player, transform] : registry_.view<Player, GlobalTransform>(entt::exclude<GhostPlayer>).each())
    {
      //for (auto nearEntity : this->GetEntitiesInSphere(transform.position, 2))
      for (auto nearEntity : this->GetEntitiesInCapsule(transform.position - glm::vec3(0, 1.5f, 0), transform.position + glm::vec3(0, 1.5f, 0), 2))
      {
        if (registry_.all_of<DroppedItem>(nearEntity) && !registry_.any_of<CannotBePickedUp>(nearEntity))
        {
          auto itemPos = registry_.get<GlobalTransform>(nearEntity).position;
          auto dist         = glm::distance(transform.position, itemPos);
          auto itemToPlayer = glm::normalize(transform.position - itemPos);

          auto& velocity = registry_.get<LinearVelocity>(nearEntity).v;
          velocity += 300 * dt * itemToPlayer / (dist * dist);
          const auto speed = glm::length(velocity);
          constexpr float maxSpeed = 10;
          if (speed > maxSpeed)
          {
            velocity = velocity / speed * maxSpeed;
          }
        }
      }
    }

    // Tick down ghost players
    for (auto&& [entity, ghost, player] : registry_.view<GhostPlayer, Player>().each())
    {
      ghost.remainingSeconds -= dt;

      if (ghost.remainingSeconds <= 0)
      {
        RespawnPlayer(entity);
      }
    }

    // Tick down invulnerability
    for (auto&& [entity, invulnerability] : registry_.view<Invulnerability>().each())
    {
      invulnerability.remainingSeconds -= dt;
      if (invulnerability.remainingSeconds <= 0)
      {
        registry_.remove<Invulnerability>(entity);
      }
    }

    for (auto&& [entity, cannotDamage] : registry_.view<CannotDamageEntities>().each())
    {
      for (auto it = cannotDamage.entities.begin(); it != cannotDamage.entities.end();)
      {
        auto& [e, time] = *it;
        time -= dt;
        if (time <= 0)
        {
          cannotDamage.entities.erase(it++);
        }
        else
        {
          ++it;
        }
      }
    }

    // Reset despawn timer for entities 
    for (auto&& [entity, transform, despawnInfo] : registry_.view<GlobalTransform, DespawnWhenFarFromPlayer>().each())
    {
      const auto maxDist2 = despawnInfo.maxDistance * despawnInfo.maxDistance;
      const auto nearestPlayer = GetNearestPlayer(transform.position);
      [[maybe_unused]] auto _ = registry_.get_or_emplace<Lifetime>(entity, despawnInfo.gracePeriod);
      if (nearestPlayer == entt::null || Math::Distance2(registry_.get<GlobalTransform>(nearestPlayer).position, transform.position) <= maxDist2)
      {
        registry_.emplace_or_replace<Lifetime>(entity, despawnInfo.gracePeriod);
      }
    }

    // Reset input
    for (auto&& [entity, input] : registry_.view<InputState>().each())
    {
      input = {};
    }

    // Tick down lifetimes
    for (auto&& [entity, lifetime] : registry_.view<Lifetime>().each())
    {
      lifetime.remainingSeconds -= dt;
      if (lifetime.remainingSeconds <= 0)
      {
        registry_.emplace<DeferredDelete>(entity);
      }
    }

    // Process entities with Health
    for (auto&& [entity, health, transform] : registry_.view<Health, GlobalTransform>(entt::exclude<GhostPlayer>).each())
    {
      if (health.hp <= 0)
      {
        if (auto* loot = registry_.try_get<Loot>(entity))
        {
          auto* table = registry_.ctx().get<LootRegistry>().Get(loot->name);
          assert(table);
          const auto& itemRegistry = registry_.ctx().get<ItemRegistry>();
          for (auto drop : table->Collect(Rng()))
          {
            const auto& def = itemRegistry.Get(drop.item);
            auto droppedEntity = def.Materialize(*this);
            def.GiveCollider(*this, droppedEntity);
            registry_.get<LocalTransform>(droppedEntity).position = transform.position;
            UpdateLocalTransform({registry_, droppedEntity});
            registry_.emplace<DroppedItem>(droppedEntity, DroppedItem{{.id = drop.item, .count = drop.count}});
            auto velocity = glm::vec3(0);
            if (auto* v = registry_.try_get<LinearVelocity>(entity))
            {
              velocity = v->v;
            }
            const auto newEntityVelocity =
              velocity + Rng().RandFloat(1, 3) * Math::RandVecInCone({Rng().RandFloat(), Rng().RandFloat()}, glm::vec3(0, 1, 0), glm::half_pi<float>());
            registry_.emplace_or_replace<LinearVelocity>(droppedEntity, newEntityVelocity);
          }
        }

        // Heroes never die!
        if (registry_.all_of<Player>(entity))
        {
          KillPlayer(entity);
        }
        else
        {
          registry_.emplace<DeferredDelete>(entity);
        }
      }
    }

    // Recursively mark entities in hierarchies for deletion.
    auto entitiesToDestroy = std::stack<entt::entity>();
    for (auto entity : registry_.view<DeferredDelete>())
    {
      entitiesToDestroy.push(entity);
    }
    while (!entitiesToDestroy.empty())
    {
      auto entity = entitiesToDestroy.top();
      entitiesToDestroy.pop();

      if (auto* h = registry_.try_get<Hierarchy>(entity))
      {
        for (auto child : h->children)
        {
          registry_.emplace_or_replace<DeferredDelete>(child);
          entitiesToDestroy.push(child);
        }
      }
    }

    // Here, we get a chance to perform game logic on any entity that is about to be deleted.

    // Non-player entities dump their inventory when they are destroyed.
    for (auto&& [entity, transform, inventory] : registry_.view<GlobalTransform, Inventory, DeferredDelete>(entt::exclude<Player>).each())
    {
      for (size_t row = 0; row < inventory.height; row++)
      {
        for (size_t col = 0; col < inventory.width; col++)
        {
          if (const auto droppedEntity = inventory.DropItem({row, col}); droppedEntity != entt::null)
          {
            registry_.get<LocalTransform>(droppedEntity).position = transform.position;
            UpdateLocalTransform({registry_, droppedEntity});
            auto velocity = glm::vec3(0);
            if (auto* v = registry_.try_get<LinearVelocity>(entity))
            {
              velocity = v->v;
            }
            const auto newEntityVelocity =
              velocity + Rng().RandFloat(1, 3) * Math::RandVecInCone({Rng().RandFloat(), Rng().RandFloat()}, glm::vec3(0, 1, 0), glm::half_pi<float>());
            registry_.emplace_or_replace<LinearVelocity>(droppedEntity, newEntityVelocity);
          }
        }
      }
    }

    // Actually destroy entities that were marked for deletion.
    for (auto entity : registry_.view<DeferredDelete>())
    {
      registry_.destroy(entity);
    }

    ticks_++;
  }
}

namespace
{
  float de1(glm::vec3 p0)
  {
    using namespace glm;
    vec4 p = vec4(p0, 1.);
    for (int i = 0; i < 8; i++)
    {
      p.x = mod(p.x - 1.0f, 2.0f) - 1.0f;
      p.y = mod(p.y - 1.0f, 2.0f) - 1.0f;
      p.z = mod(p.z - 1.0f, 2.0f) - 1.0f;
      p *= 1.4f / dot(vec3(p), vec3(p));
    }
    return length(vec2(p.x, p.z) / p.w) * 0.25f;
  }

  float de2(glm::vec3 p)
  {
    using namespace glm;
    p           = {p.x, p.z, p.y};
    vec3 cSize  = vec3(1., 1., 1.3);
    float scale = 1.;
    for (int i = 0; i < 12; i++)
    {
      p        = 2.0f * clamp(p, -cSize, cSize) - p;
      float r2 = dot(p, p);
      float k  = max((2.f) / (r2), .027f);
      p *= k;
      scale *= k;
    }
    float l   = length(vec2(p.x, p.y));
    float rxy = l - 4.0f;
    float n   = l * p.z;
    rxy       = max(rxy, -(n) / 4.f);
    return (rxy) / abs(scale);
  }

  float de3(glm::vec3 p)
  {
    float h = (sin(p.x * 0.11f) * 10 + 10) + (sin(p.z * 0.11f) * 10 + 10);
    return p.y > h;
  }
}

void World::InitializeGameState()
{
  ticks_ = 0;

  for (auto e : registry_.view<entt::entity>())
  {
    registry_.destroy(e);
  }

  registry_.ctx().insert_or_assign<NpcSpawnDirector>(NpcSpawnDirector{*this});

  // Reset entity prefab registry
  auto& entityPrefabs = registry_.ctx().insert_or_assign<EntityPrefabRegistry>({});
  [[maybe_unused]] auto meleeFrogId = entityPrefabs.Add("Melee Frog", new MeleeFrogDefinition({.spawnChance = 0.075f}));
  [[maybe_unused]] auto flyingFrogId = entityPrefabs.Add("Flying Frog", new FlyingFrogDefinition({.spawnChance = 0.05f, .canSpawnFloating = true}));
  [[maybe_unused]] auto torchId     = entityPrefabs.Add("Torch", new TorchDefinition());
  [[maybe_unused]] auto chestId      = entityPrefabs.Add("Chest", new ChestDefinition());
  [[maybe_unused]] auto mushroomId   = entityPrefabs.Add("Mushroom", new ShrimpleMeshPrefabDefinition("mushroom"));

  // Reset item registry
  auto& items = registry_.ctx().insert_or_assign<ItemRegistry>({});
  [[maybe_unused]] const auto gunId = items.Add(new Gun("M4", {}));

  [[maybe_unused]] const auto gun2Id = items.Add(new Gun("Frogun", {
    .model       = "frog",
    .scale       = 0.125f,
    .damage      = 10,
    .knockback   = 2,
    .fireRateRpm = 80,
    .bullets     = 9,
    .velocity    = 50,
    .accuracyMoa = 300,
    .vrecoil     = 10,
    .vrecoilDev  = 3,
    .hrecoil     = 1,
    .hrecoilDev  = 1,
  }));

  auto light      = GpuLight();
  light.color     = {1.0f, 0.4f, 0.2f};
  light.intensity = 500;
  light.type      = LIGHT_TYPE_POINT;
  light.range     = 200;

  [[maybe_unused]] const auto flareGunId = items.Add(new Gun("Flare Gun", {
    .model       = "ar15",
    .tint        = {1, 0.4f, 0.22f},
    .scale       = 1,
    .damage      = 10,
    .knockback   = 1,
    .fireRateRpm = 90,
    .bullets     = 1,
    .velocity    = 60,
    .accuracyMoa = 40,
    //.vrecoil = ,
    //.vrecoilDev = ,
    //.hrecoil = ,
    //.hrecoilDev = ,
    .light = light,
  }));

  [[maybe_unused]] const auto coinItemId      = items.Add(new SpriteItem("Coin", "coin"));
  [[maybe_unused]] const auto charcoalItemId  = items.Add(new SpriteItem("Charcoal", "charcoal"));
  [[maybe_unused]] const auto stickItemId     = items.Add(new SpriteItem("Stick", "stick"));
  [[maybe_unused]] const auto coolStickItemId = items.Add(new SpriteItem("Cool Stick", "stick", {1, 0, 0}));
  [[maybe_unused]] const auto healingPotionItemId = items.Add(new SpriteItem("Healing Potion", "potion_healing"));
  [[maybe_unused]] const auto stoneAxeId      = items.Add(new ToolDefinition("Stone Axe", {"axe", {.5f, .5f, .5f}, 20, 2, BlockDamageFlagBit::AXE}));
  [[maybe_unused]] const auto stonePickaxeId  = items.Add(new ToolDefinition("Stone Pickaxe", {"pickaxe", {.5f, .5f, .5f}, 20, 2, BlockDamageFlagBit::PICKAXE}));
  [[maybe_unused]] const auto opPickaxeId     = items.Add(new RainbowTool("OP Pickaxe", {"pickaxe", {1, 1, 1}, 1000, 100, BlockDamageFlagBit::ALL_TOOLS, 0.1f}));
  [[maybe_unused]] const auto spearId         = items.Add(new Spear("Stone Spear"));

  auto& blocks = registry_.ctx().insert_or_assign<BlockRegistry>(*this);

  const auto& stoneBlock = blocks.Get(blocks.Add(new BlockDefinition({
    .name = "Stone",
    .damageTier = 2,
    .damageFlags = BlockDamageFlagBit::PICKAXE,
    .voxelMaterialDesc =
      {
        .randomizeTexcoordRotation = true,
        .baseColorTexture          = "stone_albedo",
      },
  })));
  [[maybe_unused]] const auto stoneBlockId = stoneBlock.GetItemId();

  [[maybe_unused]] const auto frogLightId = blocks.Get(blocks.Add(new BlockDefinition({
    .name              = "Frog light",
    .initialHealth = 50,
    .voxelMaterialDesc = {
      .baseColorFactor = {0, 0, 0},
      .emissionFactor = {1, 5, 1}},
  }))).GetItemId();

  [[maybe_unused]] const auto grassBlockId = blocks.Get(blocks.Add(new BlockDefinition({
    .name = "Grass",
    .initialHealth = 50,
    .damageTier = 1,
    .damageFlags = BlockDamageFlagBit::PICKAXE,
    .voxelMaterialDesc =
      {
        .randomizeTexcoordRotation = true,
        .baseColorTexture          = "grass_albedo",
      },
  }))).GetItemId();
  
  [[maybe_unused]] const auto malachiteBlockId = blocks.Get(blocks.Add(new BlockDefinition({
    .name = "Malachite",
    .initialHealth = 100,
    .damageTier = 2,
    .damageFlags = BlockDamageFlagBit::PICKAXE,
    .voxelMaterialDesc =
      {
        .randomizeTexcoordRotation = true,
        .baseColorTexture          = "malachite_albedo",
      },
  }))).GetItemId();

  
  [[maybe_unused]] const auto forgeBlockItemId = blocks.Get(blocks.Add(new BlockDefinition({
    .name = "Forge",
    .initialHealth = 100,
    .damageTier = 1,
    .damageFlags = BlockDamageFlagBit::PICKAXE,
    .voxelMaterialDesc =
      {
        .baseColorTexture          = "forge_side_albedo",
        .emissionTexture           = "forge_side_emission",
        .emissionFactor            = {3, 3, 3},
      },
  }))).GetItemId();
  
  [[maybe_unused]] const auto bombId = blocks.Get(blocks.Add(new ExplodeyBlockDefinition({
    .name              = "Bomb",
    .initialHealth = 40,
    .voxelMaterialDesc = {
      .baseColorFactor = {0.8f, 0.2f, 0.2f},
      .emissionFactor = {0.1f, 0.01f, 0.01f}},
  },
  {
    .radius = 3,
    .damage = 100,
    .damageTier = 0,
    .pushForce = 8,
    .damageFlags = BlockDamageFlagBit::PICKAXE | BlockDamageFlagBit::AXE,
  }))).GetItemId();

  [[maybe_unused]] const auto stupidBombId = blocks.Get(blocks.Add(new ExplodeyBlockDefinition({
    .name              = "Stupid Bomb",
    .initialHealth = 40,
    .voxelMaterialDesc = {
      .baseColorFactor = {0.8f, 0.2f, 0.2f},
      .emissionFactor = {0.5f, 0.1f, 0.1f}},
  },
  {
    .radius = 8,
    .damage = 100,
    .damageTier = 2,
    .pushForce = 10,
    .damageFlags = BlockDamageFlagBit::PICKAXE | BlockDamageFlagBit::AXE | BlockDamageFlagBit::NO_LOOT,
  }
  ))).GetItemId();

  [[maybe_unused]] const auto woodBlockId = blocks.Add(new BlockDefinition({
    .name          = "Wood",
    .initialHealth = 100,
    .damageTier    = 1,
    .damageFlags   = BlockDamageFlagBit::AXE,
    .lootDrop      = "tree",
    .voxelMaterialDesc =
      {
        .baseColorFactor = {0.39f, 0.24f, 0.08f},
      },
  }));

  [[maybe_unused]] const auto torchBlockItemId = blocks.Get(blocks.Add(new BlockDefinition({.name = "Torch",
      .voxelMaterialDesc =
        VoxelMaterialDesc{
          //.baseColorTexture          =,
          //.baseColorFactor           =,
          //.emissionTexture           =,
          .emissionFactor = {5, 3, 2},
        },
  }))).GetItemId();

  blocks.Add(new BlockEntityDefinition({.name = "TEST", .voxelMaterialDesc = VoxelMaterialDesc{.isInvisible = true}}, {.id = torchId}));
  blocks.Add(new BlockEntityDefinition({.name = "Cheste", .voxelMaterialDesc = VoxelMaterialDesc{.baseColorTexture = "chest"}}, {.id = chestId}));
  const auto mushroomBlockItemId = blocks.Get(blocks.Add(new BlockEntityDefinition({.name = "Shroom", .voxelMaterialDesc = VoxelMaterialDesc{.isInvisible = true}}, {.id = mushroomId}))).GetItemId();

  auto* head = registry_.ctx().get<Head*>();
  auto blockDefs = blocks.GetAllDefinitions();
  head->CreateRenderingMaterials(blockDefs);

  auto& prefabs = registry_.ctx().insert_or_assign<PrefabRegistry>({});
  //const auto grassId = blocks.Get("Grass").GetBlockId();
  //const auto frogLightBlockId = blocks.Get("Frog Light").GetBlockId();

  auto* testTree = new SimplePrefab({.name = "Tree"});
  auto& binky    = testTree->voxels;
  binky.emplace_back(glm::ivec3(0, 0, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 1, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 2, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 3, 0), woodBlockId);
  binky.emplace_back(glm::ivec3(0, 4, 0), woodBlockId);
  prefabs.Add(testTree);

  auto* testDungeon = new SimplePrefab({.name = "Dungeon"});
  auto& horse       = testDungeon->voxels;

  constexpr int ds = 3;
  for (int z = -ds; z <= ds; z++)
  for (int y = -ds; y <= ds; y++)
  for (int x = -ds; x <= ds; x++)
  {
    if (x == -ds || x == ds || y == -ds || y == ds || z == -ds || z == ds)
    {
      horse.emplace_back(glm::ivec3(x, y, z), woodBlockId);
    }
    else
    {
      horse.emplace_back(glm::ivec3(x, y, z), 0);
    }
  }
  prefabs.Add(testDungeon);

  auto& crafting = registry_.ctx().insert_or_assign<Crafting>({});
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 5}},
    {{forgeBlockItemId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 5}},
    {{spearId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 5}},
    {{stonePickaxeId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stickItemId, 3}},
    {{charcoalItemId, 1}},
    blocks.Get("Forge").GetBlockId(),
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stickItemId, 1}, {charcoalItemId, 1}},
    {{torchBlockItemId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{stoneBlockId, 1}, {charcoalItemId, 1}, {mushroomBlockItemId, 1}},
    {{healingPotionItemId, 1}},
  });

  auto& loot = registry_.ctx().insert_or_assign<LootRegistry>({});
  auto standardLoot = std::make_unique<LootDrops>();
  standardLoot->drops.emplace_back(RandomLootDrop{
    .item = coinItemId,
    .count = 6,
    .chanceForOne = 0.5f,
  });
  loot.Add("standard", std::move(standardLoot));

  auto treeLoot = std::make_unique<LootDrops>();
  treeLoot->drops.emplace_back(RandomLootDrop{
    .item         = stickItemId,
    .count        = 6,
    .chanceForOne = 0.5f,
  });
  treeLoot->drops.emplace_back(RandomLootDrop{
    .item         = coolStickItemId,
    .count        = 1,
    .chanceForOne = 0.01f,
  });
  loot.Add("tree", std::move(treeLoot));

  // Reset RNG
  registry_.ctx().insert_or_assign<PCG::Rng>(1234);

  // Make player entity
  auto p = registry_.create();
  registry_.emplace<Name>(p).name = "Player";
  registry_.emplace<Player>(p);
  registry_.emplace<LocalPlayer>(p);
  registry_.emplace<InputState>(p);
  registry_.emplace<InputLookState>(p);
  registry_.emplace<Hierarchy>(p);
  registry_.emplace<Health>(p) = {100, 100};
  registry_.emplace<TeamFlags>(p, TeamFlagBits::FRIENDLY);

  auto& tp = registry_.emplace<LocalTransform>(p);
  tp.position = {2, 78, 2};
  tp.rotation = glm::identity<glm::quat>();
  tp.scale    = 1;
  registry_.emplace<GlobalTransform>(p) = {tp.position, tp.rotation, tp.scale};
  registry_.emplace<PreviousGlobalTransform>(p);
  registry_.emplace<RenderTransform>(p);
  registry_.emplace<WalkingMovementAttributes>(p);
  //GivePlayerCharacterController(p);
  //GivePlayerFlyingCharacterController(p);
  registry_.emplace<NoclipCharacterController>(p);
  registry_.emplace_or_replace<LinearVelocity>(p);
  //cc.character->SetMaxStrength(10000000);
  auto& inventory = registry_.emplace<Inventory>(p, *this);
  inventory.OverwriteSlot({0, 0}, {spearId}, p);
  inventory.OverwriteSlot({0, 1}, {stonePickaxeId}, p);
  inventory.OverwriteSlot({0, 2}, {stoneAxeId}, p);

  GivePlayerColliders(p);

  auto e = CreateRenderableEntity({0, 0, 0});
  registry_.emplace<Name>(e).name = "Test";
  registry_.emplace<Mesh>(e).name = "frog";

  auto planeSettings = JPH::PlaneShapeSettings(JPH::Plane(JPH::Vec3(0, 1, 0), 0));
  planeSettings.SetEmbedded();
  auto plane = planeSettings.Create().Get();
  plane->SetEmbedded();
  
  auto pe = registry_.create();
  registry_.emplace<Name>(pe).name = "Floor";
  Physics::AddRigidBody({registry_, pe}, {
    .shape = plane.GetPtr(),
    .activate = false,
    .motionType = JPH::EMotionType::Static,
    .layer = Physics::Layers::WORLD,
  });

  auto& grid = registry_.ctx().insert_or_assign(TwoLevelGrid(glm::vec3{4, 5, 4}));

  auto voxelMats = std::vector<TwoLevelGrid::Material>();
  for (const auto& def : blockDefs)
  {
    voxelMats.emplace_back(TwoLevelGrid::Material{
      .isVisible = !def->GetMaterialDesc().isInvisible,
    });
  }
  grid.SetMaterialArray(std::move(voxelMats));

  auto twoLevelGridShape = JPH::Ref(new Physics::TwoLevelGridShape(grid));

  auto ve                          = registry_.create();
  registry_.emplace<Name>(ve).name = "Voxels";
  registry_.emplace<Voxels>(ve);
  Physics::AddRigidBody({registry_, ve},
    {
      .shape      = twoLevelGridShape,
      .activate   = false,
      .motionType = JPH::EMotionType::Static,
      .layer      = Physics::Layers::WORLD,
    });
}

void World::GenerateMap()
{
  ZoneScoped;
#ifndef GAME_HEADLESS
  auto& progress  = registry_.ctx().get<std::atomic_int32_t>("progress"_hs);
  auto& total     = registry_.ctx().get<std::atomic_int32_t>("total"_hs);
#endif
  auto& blocks          = registry_.ctx().get<BlockRegistry>();
  const auto& grass     = blocks.Get("Grass");
  const auto& malachite = blocks.Get("Malachite");

  auto noiseGraph = FastNoise::NewFromEncodedNodeTree(
    "HgAZABsAJwABAAAAFgARAAAADQADAAAAAADAPxMAAADAPwgAAM3MzD4AAAAAAADNzMw9AQQAAAAAAAAAgD8AAAAAAAAAAAAAAADNzMy+AAAAAAAAAAABGQAbAB0AHgAEAAAAAACPwvU+AAAAAAAAAAAAAAAAMzMzPwAAAAAAAAAAAAAAAAAAAACAPwETAJqZmT4aAAERAAIAAAAAAOBAEAAAAIhBHwAWAAEAAAALAAMAAAACAAAAAwAAAAQAAAAAAAAAPwEUAP//CwAAAAAAPwAAAAA/AAAAAD8AAAAAPwEXAAAAgL8AAIA/PQoXQFK4HkATAAAAoEAGAACPwnU8AJqZmT4AAAAAAADhehQ/ARsADQAEAAAAAAAAQAgAAAAAAD8AAAAAAAEaAAAAAIA/AR4AHQAEAAAAAAAAAIA/AAAAAAAAAAAAAAAAzcxMPwAAAAAAAAAAAAAAgD8AAAAAAA==");
  auto copperGraph     = FastNoise::New<FastNoise::Simplex>();
  auto terrainHeight2D = FastNoise::NewFromEncodedNodeTree("FgARAAAADQADAAAAAAAAQBMAAADAPwgAAAAAAD8AAAAAAA==");
  auto surfaceCaves    = FastNoise::NewFromEncodedNodeTree(
    "GQAbAB0AHgAEAAAAAAAAAMA/AAAAAAAAAAAAAAAAmpmZPgAAAAAAAAAAAAAAAAAAAACAPwETAJqZmT4aAAERAAIAAAAAAOBAEAAAAIhBHwAWAAEAAAALAAMAAAACAAAAAwAAAAQAAAAAAAAAPwEUAP//AwAAAAAAPwAAAAA/AAAAAD8AAAAAPwEXAAAAgL8AAIA/PQoXQFK4HkATAAAAoEAGAACPwnU8AJqZmT4AAAAAAADhehQ/ARsADQAEAAAAAAAAQBMAAADAPwgAAAAAAD8AAAAAAAEaAAAAAIA/AR4AHQAEAAAAAAAAAIA/AAAAAAAAAAAAAAAAzcxMPwAAAAAAAAAAAAAAgD8AAAAAAA==");
  auto trees = FastNoise::New<FastNoise::Value>();

  constexpr auto samplesPerAxis = 32;
  constexpr auto sampleScale    = (float)samplesPerAxis / TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;
  auto sampleNoise3D            = [samplesPerAxis](const auto& tlCellNoise, glm::vec3 uv)
  {
    const auto unnormalized = uv * (float)samplesPerAxis - 0.5f;
    const auto intCoord     = glm::ivec3(unnormalized);
    const auto weight       = unnormalized - glm::vec3(intCoord);

    auto texelFetch3D = [&](glm::ivec3 p)
    {
      p = glm::clamp(p, glm::ivec3(0), glm::ivec3(samplesPerAxis - 1));
      return tlCellNoise[p.x + p.y * samplesPerAxis + p.z * samplesPerAxis * samplesPerAxis];
    };

    const auto bln = texelFetch3D(intCoord + glm::ivec3(0, 0, 0));
    const auto brn = texelFetch3D(intCoord + glm::ivec3(1, 0, 0));
    const auto tln = texelFetch3D(intCoord + glm::ivec3(0, 1, 0));
    const auto trn = texelFetch3D(intCoord + glm::ivec3(1, 1, 0));
    const auto blf = texelFetch3D(intCoord + glm::ivec3(0, 0, 1));
    const auto brf = texelFetch3D(intCoord + glm::ivec3(1, 0, 1));
    const auto tlf = texelFetch3D(intCoord + glm::ivec3(0, 1, 1));
    const auto trf = texelFetch3D(intCoord + glm::ivec3(1, 1, 1));

    const auto n = glm::mix(glm::mix(bln, brn, weight.x), glm::mix(tln, trn, weight.x), weight.y);
    const auto f = glm::mix(glm::mix(blf, brf, weight.x), glm::mix(tlf, trf, weight.x), weight.y);
    return glm::mix(n, f, weight.z);
  };

  auto sampleNoise2D = [samplesPerAxis](const auto& tlCellNoise, glm::vec2 uv)
  {
    const auto unnormalized = uv * (float)samplesPerAxis - 0.5f;
    const auto intCoord     = glm::ivec2(unnormalized);
    const auto weight       = unnormalized - glm::vec2(intCoord);

    auto texelFetch2D = [&](glm::ivec2 p)
    {
      p = glm::clamp(p, glm::ivec2(0), glm::ivec2(samplesPerAxis - 1));
      return tlCellNoise[p.x + p.y * samplesPerAxis];
    };

    const auto bl = texelFetch2D(intCoord + glm::ivec2(0, 0));
    const auto br = texelFetch2D(intCoord + glm::ivec2(1, 0));
    const auto tl = texelFetch2D(intCoord + glm::ivec2(0, 1));
    const auto tr = texelFetch2D(intCoord + glm::ivec2(1, 1));

    return glm::mix(glm::mix(bl, br, weight.x), glm::mix(tl, tr, weight.x), weight.y);
  };

  auto& grid = registry_.ctx().get<TwoLevelGrid>();

#ifndef GAME_HEADLESS
  total.store((int32_t)grid.numTopLevelBricks_);
#endif

  auto tlBrickColCoords = std::vector<glm::ivec2>();
  for (int k = 0; k < grid.topLevelBricksDims_.z; k++)
  {
    for (int i = 0; i < grid.topLevelBricksDims_.x; i++)
    {
      tlBrickColCoords.emplace_back(k, i);
    }
  }

  std::mutex coalesceMutex;
  auto treePositions = std::vector<glm::ivec3>();
  auto dungeonPositions = std::vector<glm::ivec3>();

  // Top level bricks
  std::for_each(std::execution::par, tlBrickColCoords.begin(), tlBrickColCoords.end(), [&](glm::ivec2 tlBrickColCoord)
  //for (int k = 0; k < grid.topLevelBricksDims_.z; k++)
  //{
  //  for (int i = 0; i < grid.topLevelBricksDims_.x; i++)
    {
      const int k = tlBrickColCoord[0];
      const int i = tlBrickColCoord[1];

      auto tlTerrainHeight = std::vector<float>(samplesPerAxis * samplesPerAxis);
      {
        ZoneScopedN("terrainHeight");

        terrainHeight2D->GenUniformGrid2D(tlTerrainHeight.data(),
          int(sampleScale * (i * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
          int(sampleScale * (k * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
          samplesPerAxis,
          samplesPerAxis,
          0.008f / sampleScale,
          1234);
      }
      for (int j = 0; j < grid.topLevelBricksDims_.y; j++) // Y last so we can compute heightmap once
      {
        const auto tl    = glm::ivec3{i, j, k};
        auto tlCellNoise = std::vector<float>(samplesPerAxis * samplesPerAxis * samplesPerAxis);
        auto tlCopperNoise = std::vector<float>(samplesPerAxis * samplesPerAxis * samplesPerAxis);
        {
          ZoneScopedN("noiseGraph->GenUniformGrid3D");
          surfaceCaves->GenUniformGrid3D(tlCellNoise.data(),
            int(sampleScale * (tl.x * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            int(sampleScale * (tl.y * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE - 180)),
            int(sampleScale * (tl.z * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            samplesPerAxis,
            samplesPerAxis,
            samplesPerAxis,
            0.008f / sampleScale,
            1234);

          copperGraph->GenUniformGrid3D(tlCopperNoise.data(),
            int(sampleScale * (tl.x * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            int(sampleScale * (tl.y * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE - 180)),
            int(sampleScale * (tl.z * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE)),
            samplesPerAxis,
            samplesPerAxis,
            samplesPerAxis,
            0.018f * 5 / sampleScale,
            1234);
        }

        // Bottom level bricks
        for (int c = 0; c < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; c++)
        {
          for (int b = 0; b < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; b++)
          {
            for (int a = 0; a < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; a++)
            {
              const auto bl = glm::ivec3{a, b, c};

              // Voxels
              for (int z = 0; z < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; z++)
              {
                for (int y = 0; y < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; y++)
                {
                  for (int x = 0; x < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; x++)
                  {
                    const auto local  = glm::ivec3{x, y, z};
                    const auto p      = tl * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE + bl * TwoLevelGrid::BL_BRICK_SIDE_LENGTH + local;
                    const auto pModTl = p % TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;
                    // if (de2(glm::vec3(p) / 10.f + 2.0f) < 0.011f)
                    // if (de2(glm::vec3(p) / 10.f + 2.0f) < 0.011f)
                    //  if (de3(p) < 0.011f)
                    // if (tlCellNoise[pModTl.x + pModTl.y * 64 + pModTl.z * 64 * 64] < 0)
                    const auto noiseUv3 = (glm::vec3(pModTl) + 0.5f) / (float)TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE;
                    const auto noiseUv2 = glm::vec2(noiseUv3.x, noiseUv3.z);
                    const auto height = (int)(sampleNoise2D(tlTerrainHeight, noiseUv2) * 10 + 260);
                    if (p.y < height && sampleNoise3D(tlCellNoise, noiseUv3) < 0)
                    //if (p.y < height)
                    {
                      if (p.y == height - 1 && trees->GenSingle2D((float)p.x, (float)p.z, 123456) > 0.98f)
                      {
                        auto lock = std::unique_lock(coalesceMutex);
                        treePositions.emplace_back(p);
                      }
                      else if (p.y > height - 2)
                      {
                        grid.SetVoxelAtNoDirty(p, grass.GetBlockId());
                      }
                      else
                      {
                        const auto pf = glm::vec3(p) * 0.018f;
                        if (sampleNoise3D(tlCopperNoise, noiseUv3) + 0.81f < 0)
                        {
                          grid.SetVoxelAtNoDirty(p, malachite.GetBlockId());
                        }
                        else
                        {
                          grid.SetVoxelAtNoDirty(p, 1);
                        }
                        if (trees->GenSingle3D((float)p.x, (float)p.y, (float)p.z, 123321) > 0.9999f)
                        {
                          auto lock = std::unique_lock(coalesceMutex);
                          dungeonPositions.emplace_back(p);
                        }
                      }
                    }
                    else
                    {
                      grid.SetVoxelAtNoDirty(p, 0);
                    }
                  }
                }
              }
            }
          }
        }

        grid.MarkTopLevelBrickAndChildrenDirty(tl);
        // TODO: coalesce top-level brick
#ifndef GAME_HEADLESS
        progress.fetch_add(1);
#endif
        //auto lock = std::unique_lock(coalesceMutex);
      }
    });
  //}

  for (auto treePos : treePositions)
  {
    auto treeBlocks = registry_.ctx().get<PrefabRegistry>().Get("Tree").GetVoxels(treePos);
    for (auto [pos, voxel] : treeBlocks)
    {
      grid.SetVoxelAt(treePos + pos, voxel);
    }
  }

  for (auto dungeonPos : dungeonPositions)
  {
    auto blockss = registry_.ctx().get<PrefabRegistry>().Get("Dungeon").GetVoxels(dungeonPos);
    for (auto [pos, voxel] : blockss)
    {
      if (grid.IsPositionInGrid(dungeonPos + pos))
      {
        grid.SetVoxelAt(dungeonPos + pos, voxel);
      }
    }
  }

  grid.CoalesceDirtyBricks();
}

entt::entity World::CreateRenderableEntityNoHashGrid(glm::vec3 position, glm::quat rotation, float scale)
{
  auto e     = registry_.create();
  auto& t    = registry_.emplace<LocalTransform>(e);
  t.position = position;
  t.rotation = rotation;
  t.scale    = scale;

  registry_.emplace<GlobalTransform>(e) = {t.position, t.rotation, t.scale};

  auto& it    = registry_.emplace<PreviousGlobalTransform>(e);
  it.position = t.position;
  it.rotation = t.rotation;
  it.scale    = t.scale;
  registry_.emplace<RenderTransform>(e);
  registry_.emplace<Hierarchy>(e);

  registry_.emplace<NoHashGrid>(e);
  return e;
}

entt::entity World::CreateRenderableEntity(glm::vec3 position, glm::quat rotation, float scale)
{
  auto e = CreateRenderableEntityNoHashGrid(position, rotation, scale);
  registry_.remove<NoHashGrid>(e);
  registry_.ctx().get<HashGrid>().set(position, e);
  return e;
}

entt::entity World::CreateDroppedItem(ItemState item, glm::vec3 position, glm::quat rotation, float scale)
{
  const auto& itemDef = registry_.ctx().get<ItemRegistry>().Get(item.id);
  auto entity         = itemDef.Materialize(*this);

  auto& t    = registry_.get<LocalTransform>(entity);
  t.position = position;
  t.rotation = rotation;
  t.scale    = scale;
  UpdateLocalTransform({registry_, entity});
  itemDef.GiveCollider(*this, entity);
  registry_.emplace<DroppedItem>(entity).item = ItemState{item.id};
  return entity;
}

GlobalTransform* World::TryGetLocalPlayerTransform()
{
  auto view = registry_.view<GlobalTransform, LocalPlayer>();
  auto e = view.front();
  if (e != entt::null)
  {
    return &view.get<GlobalTransform>(e);
  }
  return nullptr;
}

void World::SetLocalScale(entt::entity entity, float scale)
{
  auto& lt = registry_.get<LocalTransform>(entity);
  lt.scale = scale;
  UpdateLocalTransform({registry_, entity});
}

entt::entity World::GetChildNamed(entt::entity entity, std::string_view name) const
{
  for (auto child : registry_.get<Hierarchy>(entity).children)
  {
    if (auto* n = registry_.try_get<Name>(child); n && n->name == name)
    {
      return child;
    }
  }
  return entt::null;
}

glm::vec3 World::GetInheritedLinearVelocity(entt::entity entity)
{
  assert(registry_.valid(entity));
  if (auto* vel = registry_.try_get<LinearVelocity>(entity))
  {
    return vel->v;
  }
  if (auto* h = registry_.try_get<Hierarchy>(entity); h && h->parent != entt::null)
  {
    return GetInheritedLinearVelocity(h->parent);
  }
  return {0, 0, 0};
}

const TeamFlags* World::GetTeamFlags(entt::entity entity) const
{
  assert(registry_.valid(entity));
  if (auto* teamFlags = registry_.try_get<TeamFlags>(entity))
  {
    return teamFlags;
  }
  if (auto* h = registry_.try_get<Hierarchy>(entity); h && h->parent != entt::null)
  {
    return GetTeamFlags(h->parent);
  }
  return nullptr;
}

Physics::CharacterController& World::GivePlayerCharacterController(entt::entity playerEntity)
{
  constexpr float playerHalfHeight = 0.8f;
  constexpr float playerHalfWidth  = 0.3f;
  // auto playerCapsule = JPH::Ref(new JPH::CapsuleShape(playerHalfHeight - playerHalfWidth, playerHalfWidth));
  auto playerCapsule = JPH::Ref(new JPH::BoxShape(JPH::Vec3(playerHalfWidth, playerHalfHeight, playerHalfWidth)));

  auto playerShape = JPH::Ref(new JPH::RotatedTranslatedShape(JPH::Vec3(0, -playerHalfHeight * 0.875f, 0), JPH::Quat::sIdentity(), playerCapsule));

  return Physics::AddCharacterController({registry_, playerEntity}, {.shape = playerShape});
}

Physics::CharacterControllerShrimple& World::GivePlayerCharacterControllerShrimple(entt::entity playerEntity)
{
  constexpr float playerHalfHeight = 0.8f;
  constexpr float playerHalfWidth  = 0.3f;
  // auto playerCapsule = JPH::Ref(new JPH::CapsuleShape(playerHalfHeight - playerHalfWidth, playerHalfWidth));
  auto playerCapsule = JPH::Ref(new JPH::BoxShape(JPH::Vec3(playerHalfWidth, playerHalfHeight, playerHalfWidth)));

  auto playerShape = JPH::Ref(new JPH::RotatedTranslatedShape(JPH::Vec3(0, -playerHalfHeight * 0.875f, 0), JPH::Quat::sIdentity(), playerCapsule));

  return Physics::AddCharacterControllerShrimple({registry_, playerEntity}, {.shape = playerShape});
}

FlyingCharacterController& World::GivePlayerFlyingCharacterController(entt::entity playerEntity)
{
  registry_.emplace<Friction>(playerEntity, glm::vec3(5.0f));
  return registry_.emplace<FlyingCharacterController>(playerEntity) = {.maxSpeed = 9, .acceleration = 35.0f};
}

void World::GivePlayerColliders(entt::entity playerEntity)
{
  constexpr float playerHalfHeight = 0.8f * 1.0f;
  constexpr float playerHalfWidth  = 0.3f * 1.0f;
  assert(playerHalfHeight - playerHalfWidth >= 0);
  auto playerHitbox      = JPH::Ref(new JPH::CapsuleShape(playerHalfHeight - playerHalfWidth, playerHalfWidth));
  auto playerHitboxShape = JPH::Ref(new JPH::RotatedTranslatedShape(JPH::Vec3(0, -0.8f * 0.875f, 0), JPH::Quat::sIdentity(), playerHitbox));

  auto pHitbox                          = registry_.create();
  registry_.emplace<Name>(pHitbox).name = "Player hitbox";
  registry_.emplace<ForwardCollisionsToParent>(pHitbox);
  auto& tpHitbox    = registry_.emplace<LocalTransform>(pHitbox);
  tpHitbox.position = {};
  tpHitbox.rotation = glm::identity<glm::quat>();
  tpHitbox.scale    = 1;

  registry_.emplace<GlobalTransform>(pHitbox) = {{}, glm::identity<glm::quat>(), 1};

  registry_.emplace<Hierarchy>(pHitbox).useLocalRotationAsGlobal = true; // Stay upright
  Physics::AddRigidBody({registry_, pHitbox},
    {
      .shape      = playerHitboxShape,
      .isSensor   = true,
      .motionType = JPH::EMotionType::Kinematic,
      .layer      = Physics::Layers::HITBOX,
    });
  SetParent({registry_, pHitbox}, playerEntity);
}

void World::KillPlayer(entt::entity playerEntity)
{
  registry_.remove<NoclipCharacterController, FlyingCharacterController, Physics::CharacterController, Physics::CharacterControllerShrimple>(playerEntity);
  registry_.emplace<GhostPlayer>(playerEntity).remainingSeconds = 3;

  if (auto e = GetChildNamed(playerEntity, "Player hitbox"); e != entt::null)
  {
    registry_.destroy(e);
    registry_.get<Hierarchy>(playerEntity).RemoveChild(e);
  }

  auto& inventory = registry_.get<Inventory>(playerEntity);
  if (inventory.ActiveSlot().id != nullItem)
  {
    const auto& def = registry_.ctx().get<ItemRegistry>().Get(inventory.ActiveSlot().id);
    def.Dematerialize(*this, inventory.activeSlotEntity);
  }
}

void World::RespawnPlayer(entt::entity playerEntity)
{
  registry_.remove<GhostPlayer>(playerEntity);
  auto& tp    = registry_.emplace_or_replace<LocalTransform>(playerEntity);
  tp.position = {2, 78, 2};
  tp.rotation = glm::identity<glm::quat>();
  tp.scale    = 1;
  UpdateLocalTransform({registry_, playerEntity});

  registry_.get_or_emplace<Health>(playerEntity) = {100, 100};
  registry_.get_or_emplace<Invulnerability>(playerEntity).remainingSeconds = 5;

  GivePlayerCharacterController(playerEntity);
  GivePlayerColliders(playerEntity);

  auto& inventory = registry_.get<Inventory>(playerEntity);
  if (inventory.ActiveSlot().id != nullItem)
  {
    const auto& def = registry_.ctx().get<ItemRegistry>().Get(inventory.ActiveSlot().id);
    inventory.activeSlotEntity = def.Materialize(*this);
    SetParent({registry_, inventory.activeSlotEntity}, playerEntity);
  }
}

float World::DamageEntity(entt::entity entity, float damage)
{
  if (registry_.any_of<Invulnerability>(entity))
  {
    return 0;
  }

  auto& h = registry_.get<Health>(entity);
  h.hp -= damage;
  return damage;
}

bool World::CanEntityDamageEntity(entt::entity entitySource, entt::entity entityTarget) const
{
  if (const auto* cd = registry_.try_get<CannotDamageEntities>(entitySource))
  {
    if (cd->entities.contains(entityTarget))
    {
      return false;
    }
  }

  return true;
}

bool World::AreEntitiesEnemies(entt::entity entity1, entt::entity entity2) const
{
  auto* team1 = GetTeamFlags(entity1);
  auto* team2 = GetTeamFlags(entity2);
  if (!team1 || !team2 || !(*team1 & *team2))
  {
    return true;
  }
  return false;
}

std::vector<entt::entity> World::GetEntitiesInSphere(glm::vec3 center, float radius) const
{
  ZoneScoped;
  const float radius2 = radius * radius;
  const auto& grid = registry_.ctx().get<HashGrid>();

  auto entities = std::vector<entt::entity>();

  const auto lower = grid.QuantizeKey(center - radius);
  const auto upper = grid.QuantizeKey(center + radius);

  // Broadphase: iterate over all chunks touched by sphere.
  for (int z = lower.z; z <= upper.z; z++)
  {
    for (int y = lower.y; y <= upper.y; y++)
    {
      for (int x = lower.x; x <= upper.x; x++)
      {
        const auto [begin, end] = grid.equal_range_chunk({x, y, z});
        for (auto it = begin; it != end; ++it)
        {
          // Narrowphase: distance check.
          const auto entity = it->second;
          const auto& position = registry_.get<GlobalTransform>(entity).position;

          const auto vec = position - center;
          const auto distance2 = glm::dot(vec, vec);
          if (distance2 <= radius2)
          {
            entities.emplace_back(entity);
          }
        }
      }
    }
  }

  return entities;
}

std::vector<entt::entity> World::GetEntitiesInCapsule(glm::vec3 start, glm::vec3 end, float radius)
{
  ZoneScoped;
  const auto& grid = registry_.ctx().get<HashGrid>();

  auto entities = std::vector<entt::entity>();

  const auto lower = grid.QuantizeKey(glm::min(start - radius, end - radius));
  const auto upper = grid.QuantizeKey(glm::max(start + radius, end + radius));

  for (int z = lower.z; z <= upper.z; z++)
  {
    for (int y = lower.y; y <= upper.y; y++)
    {
      for (int x = lower.x; x <= upper.x; x++)
      {
        const auto [beginIt, endIt] = grid.equal_range_chunk({x, y, z});
        for (auto it = beginIt; it != endIt; ++it)
        {
          // Narrowphase: distance check.
          const auto entity    = it->second;
          const auto& position = registry_.get<GlobalTransform>(entity).position;
          
          if (Math::PointLineSegmentDistance(position, start, end) <= radius)
          {
            entities.emplace_back(entity);
          }
        }
      }
    }
  }

  return entities;
}

entt::entity World::GetNearestPlayer(glm::vec3 position)
{
  entt::entity nearestPlayer = entt::null;
  float nearestDistance2     = HUGE_VALF;

  for (auto [entity, transform, player] : registry_.view<GlobalTransform, Player>(entt::exclude<GhostPlayer>).each())
  {
    if (const auto dist2 = Math::Distance2(position, transform.position); dist2 < nearestDistance2)
    {
      nearestPlayer    = entity;
      nearestDistance2 = dist2;
    }
  }

  return nearestPlayer;
}

float World::DamageBlock(glm::ivec3 voxelPos, float damage, int damageTier, BlockDamageFlags damageType)
{
  auto& grid = registry_.ctx().get<TwoLevelGrid>();
  auto prevVoxel = grid.GetVoxelAt(voxelPos);
  if (prevVoxel == 0)
  {
    return 0;
  }

  entt::entity foundEntity = entt::null;
  BlockHealth* hp = nullptr;

  const auto worldPos = glm::vec3(voxelPos) + 0.5f;
  for (auto entity : GetEntitiesInSphere(worldPos, 0.125f))
  {
    hp = registry_.try_get<BlockHealth>(entity);
    if (hp)
    {
      foundEntity = entity;
      break;
    }
  }

  const auto& blockDef = registry_.ctx().get<BlockRegistry>().Get(prevVoxel);

  if (foundEntity == entt::null)
  {
    foundEntity = this->CreateRenderableEntity(worldPos);
    hp = &registry_.emplace<BlockHealth>(foundEntity, blockDef.GetInitialHealth());
  }

  registry_.emplace_or_replace<Lifetime>(foundEntity).remainingSeconds = 5;

  
  if ((damageType & blockDef.GetDamageFlags()).flags == 0 || damageTier < blockDef.GetDamageTier())
  {
    return 0;
  }

  const auto initialHealth = hp->health;
  hp->health -= damage;
  if (hp->health <= 0)
  {
    blockDef.OnDestroyBlock(*this, voxelPos);

    const auto hasNoLoot95 = damageType & BlockDamageFlagBit::NO_LOOT_95_PERCENT;
    if ((!hasNoLoot95 || Rng().RandFloat() >= 0.95) && !(damageType & BlockDamageFlagBit::NO_LOOT))
    {
      const auto dropType = blockDef.GetLootDropType();
      if (auto* ip = std::get_if<ItemState>(&dropType))
      {
        const auto& itemDef = registry_.ctx().get<ItemRegistry>().Get(ip->id);
        auto itemSelf       = itemDef.Materialize(*this);

        registry_.get<LocalTransform>(itemSelf).position = worldPos;
        UpdateLocalTransform({registry_, itemSelf});
        itemDef.GiveCollider(*this, itemSelf);
        registry_.emplace<DroppedItem>(itemSelf).item = *ip;

        const auto throwdir                                  = glm::vec3(Rng().RandFloat(-0.25f, 0.25f), 1, Rng().RandFloat(-0.25f, 0.25f));
        registry_.get_or_emplace<LinearVelocity>(itemSelf).v = throwdir * 2.0f;
      }
      else if (auto* lp = std::get_if<std::string>(&dropType))
      {
        auto* table           = registry_.ctx().get<LootRegistry>().Get(*lp);
        assert(table);
        const auto& itemRegistry = registry_.ctx().get<ItemRegistry>();
        for (auto drop : table->Collect(Rng()))
        {
          const auto& def    = itemRegistry.Get(drop.item);
          auto droppedEntity = def.Materialize(*this);
          def.GiveCollider(*this, droppedEntity);
          registry_.get<LocalTransform>(droppedEntity).position = worldPos;
          UpdateLocalTransform({registry_, droppedEntity});
          registry_.emplace<DroppedItem>(droppedEntity, DroppedItem{{.id = drop.item, .count = drop.count}});
          auto velocity = glm::vec3(0);
          const auto newEntityVelocity =
            velocity + Rng().RandFloat(1, 3) * Math::RandVecInCone({Rng().RandFloat(), Rng().RandFloat()}, glm::vec3(0, 1, 0), glm::half_pi<float>());
          registry_.emplace_or_replace<LinearVelocity>(droppedEntity, newEntityVelocity);
        }
      }
    }

    // Awaken bodies that are adjacent to destroyed voxel in case they were resting on it.
    // TODO: This doesn't seem to be robust. Setting mTimeBeforeSleep to 0 in PhysicsSettings seems to disable sleeping, which fixes this issue.
    Physics::GetBodyInterface().ActivateBodiesInAABox({Physics::ToJolt(worldPos), 2.0f}, {}, {});

    registry_.destroy(foundEntity);
  }

  return initialHealth - hp->health;
}

glm::vec3 GetFootPosition(entt::handle handle)
{
  const auto* t = handle.try_get<GlobalTransform>();
  assert(t);

  if (const auto* s = handle.try_get<Physics::Shape>())
  {
    const auto floorOffsetY = -s->shape->GetLocalBounds().GetExtent().GetY();
    return t->position + glm::vec3(0, floorOffsetY + 1e-1f, 0); // Needs fairly large epsilon because feet can penetrate ground in physics sim.
  }

  return t->position - glm::vec3(0, t->scale, 0);
}

float GetHeight(entt::handle handle)
{
  if (const auto* s = handle.try_get<Physics::Shape>())
  {
    return s->shape->GetLocalBounds().GetExtent().GetY() * 2.0f;
  }

  const auto& t = handle.get<GlobalTransform>();
  return t.scale * 2.0f;
}

static void RefreshGlobalTransform(entt::handle handle)
{
  auto& lt = handle.get<LocalTransform>(); // parent_local_from_local
  auto& gt = handle.get<GlobalTransform>(); // world_from_local
  auto& h = handle.get<Hierarchy>();

  if (h.parent == entt::null)
  {
    gt = {lt.position, lt.rotation, lt.scale};
  }
  else
  {
    const auto& pt = handle.registry()->get<GlobalTransform>(h.parent);
    gt.position    = pt.position + lt.position * pt.scale;

    gt.scale = lt.scale * pt.scale;

    gt.position -= pt.position;
    gt.position = glm::mat3_cast(pt.rotation) * gt.position;
    gt.position += pt.position;

    if (h.useLocalRotationAsGlobal)
    {
      gt.rotation = lt.rotation;
    }
    else
    {
      gt.rotation = pt.rotation * lt.rotation;
    }

    if (h.useLocalPositionAsGlobal)
    {
      gt.position = lt.position;
    }
  }

  if (!handle.any_of<NoHashGrid>())
  {
    handle.registry()->ctx().get<HashGrid>().set(gt.position, handle.entity());
  }

  for (auto child : h.children)
  {
    RefreshGlobalTransform({*handle.registry(), child});
  }
}

bool SwapInventorySlots(World& world, entt::entity parent1, glm::ivec2 parent1Slot, entt::entity parent2, glm::ivec2 parent2Slot)
{
  auto* inventory1 = world.GetRegistry().try_get<Inventory>(parent1);
  auto* inventory2 = world.GetRegistry().try_get<Inventory>(parent2);
  if (!inventory1 || !inventory2)
  {
    return false;
  }

  auto item1 = inventory1->slots[parent1Slot.x][parent1Slot.y];
  auto item2 = inventory2->slots[parent2Slot.x][parent2Slot.y];

  inventory1->OverwriteSlot(parent1Slot, item2, parent1);
  inventory2->OverwriteSlot(parent2Slot, item1, parent2);

  return true;
}

void UpdateLocalTransform(entt::handle handle)
{
  ZoneScoped;
  assert(handle.valid());
  RefreshGlobalTransform(handle);
}

glm::vec3 GetForward(glm::quat rotation)
{
  return -glm::mat3_cast(rotation)[2];
}

glm::vec3 GetUp(glm::quat rotation)
{
  return glm::mat3_cast(rotation)[1];
}

glm::vec3 GetRight(glm::quat rotation)
{
  return glm::mat3_cast(rotation)[0];
}

void SetParent(entt::handle handle, entt::entity parent)
{
  assert(handle.valid());
  assert(handle.entity() != parent);

  auto& registry = *handle.registry();
  auto& h        = handle.get<Hierarchy>();
  auto oldParent = h.parent;

  // Remove self from old parent
  if (h.parent != entt::null)
  {
    auto& ph = registry.get<Hierarchy>(h.parent);
    ph.RemoveChild(handle.entity());
  }

  // Handle case of removing parent
  if (parent == entt::null)
  {
    h.parent = entt::null;
    if (parent != oldParent)
    {
      auto&& [gt, lt] = handle.get<GlobalTransform, LocalTransform>();
      lt.position     = gt.position;
      lt.rotation     = gt.rotation;
      lt.scale        = gt.scale;
      UpdateLocalTransform(handle);
    }
    return;
  }

  // Add self to new parent
  h.parent = parent;
  auto& ph = registry.get<Hierarchy>(parent);
  ph.AddChild(handle.entity());

  // Detect cycles in debug mode
  for ([[maybe_unused]] entt::entity cParent = parent; cParent != entt::null; cParent = registry.get<Hierarchy>(cParent).parent)
  {
    assert(cParent != handle.entity());
  }

  UpdateLocalTransform(handle);
}

void Hierarchy::AddChild(entt::entity child)
{
  assert(std::count(children.begin(), children.end(), child) == 0);
  children.emplace_back(child);
}

void Hierarchy::RemoveChild(entt::entity child)
{
  assert(std::count(children.begin(), children.end(), child) == 1);
  std::erase(children, child);
}

void Inventory::SetActiveSlot(glm::ivec2 rowCol, entt::entity parent)
{
  if (!canHaveActiveItem)
  {
    return;
  }

  if (rowCol != activeSlotCoord)
  {
    if (ActiveSlot().id != nullItem)
    {
      world->GetRegistry().ctx().get<ItemRegistry>().Get(ActiveSlot().id).Dematerialize(*world, activeSlotEntity);
    }
    activeSlotCoord = rowCol;
    if (ActiveSlot().id != nullItem)
    {
      activeSlotEntity = world->GetRegistry().ctx().get<ItemRegistry>().Get(ActiveSlot().id).Materialize(*world);
      SetParent({world->GetRegistry(), activeSlotEntity}, parent);
    }
  }
}

void Inventory::SwapSlots(glm::ivec2 first, glm::ivec2 second, entt::entity parent)
{
  // Handle moving active item onto another slot or vice versa
  if (canHaveActiveItem)
  {
    const bool firstIsActive  = first == activeSlotCoord;
    const bool secondIsActive = second == activeSlotCoord;
    if (firstIsActive)
    {
      SetActiveSlot(second, parent);
      activeSlotCoord = first;
    }
    else if (secondIsActive)
    {
      SetActiveSlot(first, parent);
      activeSlotCoord = second;
    }
  }
  std::swap(slots[second[0]][second[1]], slots[first[0]][first[1]]);
}

entt::entity Inventory::DropItem(glm::ivec2 slot)
{
  auto& item = slots[slot[0]][slot[1]];
  if (item.id == nullItem)
  {
    return entt::null;
  }

  const auto& def = world->GetRegistry().ctx().get<ItemRegistry>().Get(item.id);

  if (canHaveActiveItem && activeSlotCoord == slot)
  {
    def.Dematerialize(*world, activeSlotEntity);
    activeSlotEntity = entt::null;
  }

  auto entity = def.Materialize(*world);
  def.GiveCollider(*world, entity);
  world->GetRegistry().emplace<DroppedItem>(entity).item = std::exchange(item, {});
  world->GetRegistry().emplace<CannotBePickedUp>(entity).remainingSeconds = 1.0f;
  return entity;
}

void Inventory::OverwriteSlot(glm::ivec2 rowCol, ItemState itemState, entt::entity parent)
{
  const bool dstIsActive = rowCol == activeSlotCoord;
  if (canHaveActiveItem && dstIsActive && ActiveSlot().id != nullItem)
  {
    world->GetRegistry().ctx().get<ItemRegistry>().Get(ActiveSlot().id).Dematerialize(*world, activeSlotEntity);
  }
  slots[rowCol[0]][rowCol[1]] = itemState;
  if (canHaveActiveItem && dstIsActive && itemState.id != nullItem)
  {
    activeSlotEntity = world->GetRegistry().ctx().get<ItemRegistry>().Get(ActiveSlot().id).Materialize(*world);
    SetParent({world->GetRegistry(), activeSlotEntity}, parent);
  }
}

void Inventory::TryStackItem(ItemState& item)
{
  const auto& def = world->GetRegistry().ctx().get<ItemRegistry>().Get(item.id);
  for (auto& row : slots)
  {
    for (auto& slot : row)
    {
      if (item.count <= 0)
      {
        return;
      }

      if (slot.id == item.id)
      {
        // Moves stack from item to slot, up to max stack size.
        const auto avail = glm::min(item.count, def.GetMaxStackSize() - slot.count);
        slot.count += avail;
        item.count -= avail;
      }
    }
  }
}

std::optional<glm::ivec2> Inventory::GetFirstEmptySlot() const
{
  for (size_t row = 0; row < height; row++)
  {
    for (size_t col = 0; col < width; col++)
    {
      if (slots[row][col].id == nullItem)
      {
        return glm::ivec2{row, col};
      }
    }
  }

  return std::nullopt;
}

bool Inventory::CanCraftRecipe(Crafting::Recipe recipe) const
{
  for (auto& ingredient : recipe.ingredients)
  {
    for (const auto& row : slots)
    {
      for (const auto& slot : row)
      {
        if (slot.id == ingredient.item)
        {
          ingredient.count -= (int)slot.count;
        }
      }
    }
  }
  
  for (const auto& ingredient : recipe.ingredients)
  {
    if (ingredient.count > 0)
    {
      return false;
    }
  }

  return true;
}

void Inventory::CraftRecipe(Crafting::Recipe recipe, entt::entity parent) 
{
  // For every ingredient, look at entire inventory and eat the required items. It's assumed that the required items are available.
  for (auto& ingredient : recipe.ingredients)
  {
    for (size_t rowIdx = 0; rowIdx < slots.size(); rowIdx++)
    {
      auto& row = slots[rowIdx];
      for (size_t colIdx = 0; colIdx < row.size(); colIdx++)
      {
        auto& slot = row[colIdx];
        if (slot.id == ingredient.item)
        {
          const auto consumed = glm::min(ingredient.count, (int)slot.count);
          ingredient.count -= consumed;
          slot.count -= consumed;
          if (slot.count <= 0)
          {
            OverwriteSlot({rowIdx, colIdx}, {});
          }
        }
      }
    }
  }

  // For each output, try to stack it with an existing slot. Otherwise, put it in a free spot. If there is no free spot, drop it.
  for (auto& output : recipe.output)
  {
    auto item = ItemState{output.item, output.count};
    TryStackItem(item);
    if (item.count > 0)
    {
      if (auto slot = GetFirstEmptySlot())
      {
        OverwriteSlot(*slot, item, parent);
      }
      else
      {
        const auto& t = world->GetRegistry().get<GlobalTransform>(parent);
        world->CreateDroppedItem(item, t.position, t.rotation, t.scale);
      }
    }
  }
}

entt::entity Gun::Materialize(World& world) const
{
  auto self = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f});
  world.SetLocalScale(self, createInfo_.scale);
  world.GetRegistry().emplace<Mesh>(self, createInfo_.model);
  world.GetRegistry().emplace<Tint>(self, createInfo_.tint);

  world.GetRegistry().emplace<Name>(self).name = name_;
  return self;
}

void Gun::UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const
{
  auto& registry = world.GetRegistry();
  // Only shoot if materialized
  if (!registry.valid(self))
  {
    return;
  }

  const auto& transform = registry.get<GlobalTransform>(self);
  const auto shootDt    = GetUseDt();
  if (state.useAccum >= shootDt)
  {
    state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);

    for (int i = 0; i < createInfo_.bullets; i++)
    {
      const float bulletScale = 0.05f;
      auto bulletShape        = JPH::Ref(new JPH::SphereShape(.04f));
      bulletShape->SetDensity(11000);
      const auto dir =
        Math::RandVecInCone({world.Rng().RandFloat(), world.Rng().RandFloat()}, GetForward(transform.rotation), glm::radians(createInfo_.accuracyMoa / 60.0f));
      auto up = glm::vec3(0, 1, 0);
      if (glm::epsilonEqual(abs(dot(dir, glm::vec3(0, 1, 0))), 1.0f, 0.001f))
      {
        up = {0, 0, 1};
      }
      auto rot = glm::quatLookAtRH(dir, up);
      auto b   = world.CreateRenderableEntity(transform.position + glm::vec3(0, 0.1f, 0) + GetForward(transform.rotation) * 1.0f, rot, bulletScale);

      registry.emplace<Name>(b).name                 = "Bullet";
      registry.emplace<Mesh>(b).name                 = "frog";
      registry.emplace<Lifetime>(b).remainingSeconds = 8;

      if (createInfo_.light)
      {
        registry.emplace<GpuLight>(b, *createInfo_.light);
      }

      const auto inheritedVelocity = world.GetInheritedLinearVelocity(self);
      auto& projectile             = registry.emplace<Projectile>(b);
      projectile.initialSpeed      = createInfo_.velocity + glm::length(inheritedVelocity);
      projectile.drag              = 0.25f;
      projectile.restitution       = 0.25f;

      registry.emplace<LinearVelocity>(b, dir * createInfo_.velocity + inheritedVelocity);

      auto& contactDamage     = registry.emplace<ContactDamage>(b);
      contactDamage.damage    = createInfo_.damage;
      contactDamage.knockback = createInfo_.knockback;

      if (auto* team = world.GetTeamFlags(self))
      {
        registry.emplace<TeamFlags>(b, *team);
      }
    }

    // If parent is player, apply recoil
    if (auto* h = registry.try_get<Hierarchy>(self); h && h->parent != entt::null)
    {
      const auto vr = glm::radians(createInfo_.vrecoil + world.Rng().RandFloat(-createInfo_.vrecoilDev, createInfo_.vrecoilDev));
      const auto hr = glm::radians(createInfo_.hrecoil + world.Rng().RandFloat(-createInfo_.hrecoilDev, createInfo_.hrecoilDev));
      if (auto* is = registry.try_get<InputLookState>(h->parent))
      {
        is->pitch += vr;
        is->yaw += hr;
        UpdateLocalTransform({registry, h->parent});
      }
    }
  }
}

entt::entity ToolDefinition::Materialize(World& world) const
{
  if (!createInfo_.meshName)
  {
    return entt::null;
  }

  auto self = world.CreateRenderableEntity({0.3f, -0.7f, -0.7f});
  world.GetRegistry().emplace<Mesh>(self, *createInfo_.meshName);
  world.GetRegistry().emplace<Tint>(self, createInfo_.meshTint);
  world.GetRegistry().emplace<Name>(self).name = GetName();
  return self;
}

void ToolDefinition::UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const
{
  if (state.useAccum < GetUseDt())
  {
    return;
  }
  auto& registry = world.GetRegistry();
  if (registry.all_of<LinearPath>(self))
  {
    registry.remove<LinearPath>(self);
  }
  auto& path = world.GetRegistry().emplace_or_replace<LinearPath>(self);
  path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, -0.25f, -0.25f}, .rotation = glm::angleAxis(glm::radians(-30.0f), glm::vec3(1, 0, 0)), .offsetSeconds = GetUseDt() * 0.4f});
  path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, 0, 0}, .offsetSeconds = GetUseDt() * 0.4f});

  state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);
  auto& reg      = world.GetRegistry();
  const auto& h  = reg.get<Hierarchy>(self);
  const auto p   = h.parent;
  const auto& pt = reg.get<GlobalTransform>(p);
  const auto pos = pt.position;
  const auto dir = GetForward(pt.rotation);

  auto& grid = reg.ctx().get<TwoLevelGrid>();
  auto hit   = TwoLevelGrid::HitSurfaceParameters();
  if (grid.TraceRaySimple(pos, dir, 10, hit))
  {
    const auto damage = world.DamageBlock(glm::ivec3(hit.voxelPosition), createInfo_.blockDamage, createInfo_.blockDamageTier, createInfo_.blockDamageFlags);

    constexpr float debrisSize = 0.0525f;
    auto cube = JPH::Ref(new JPH::BoxShape(JPH::Vec3::sReplicate(debrisSize)));

    // Make debris "particles"
    const auto numParticles = glm::clamp(glm::ceil(glm::mix(1.0f, 6.0f, damage / 20.0f)), 0.0f, 10.0f);
    for (int i = 0; i < (int)numParticles; i++)
    {
      auto offset = glm::vec3(world.Rng().RandFloat(-0.125f, 0.125f), world.Rng().RandFloat(-0.125f, 0.125f), world.Rng().RandFloat(-0.125f, 0.125f));
      offset *= glm::equal(hit.flatNormalWorld, glm::vec3(0)); // Zero out the component of the normal.
      auto e = world.CreateRenderableEntityNoHashGrid(hit.positionWorld + offset + hit.flatNormalWorld * debrisSize / 2.0f, glm::identity<glm::quat>(), debrisSize);
      reg.emplace<Mesh>(e).name = "cube";
      reg.emplace<Name>(e).name = "Debris";
      reg.emplace<Lifetime>(e).remainingSeconds = 2;
      Physics::AddRigidBody({reg, e}, {.shape = cube, .layer = Physics::Layers::DEBRIS});
      const auto velocity = Math::RandVecInCone({world.Rng().RandFloat(), world.Rng().RandFloat()}, hit.flatNormalWorld, glm::quarter_pi<float>()) * 3.0f;
      reg.emplace_or_replace<LinearVelocity>(e).v = velocity;
    }
  }
}

entt::entity Block::Materialize(World& world) const
{
  auto self = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f}, glm::identity<glm::quat>(), 0.25f);
  auto& mesh = world.GetRegistry().emplace<Mesh>(self);
  mesh.name = "cube";
  const auto& material = world.GetRegistry().ctx().get<BlockRegistry>().Get(voxel).GetMaterialDesc();
  world.GetRegistry().emplace<Tint>(self, material.baseColorFactor);
  if (!material.emissionTexture && glm::length(material.emissionFactor) > 0.01f)
  {
    // TODO: Convert from luminance (cd/m^2) to luminous intensity (cd)
    auto light      = GpuLight();
    light.color     = material.emissionFactor;
    light.intensity = 1;
    light.type      = LIGHT_TYPE_POINT;
    light.range     = 100;
    world.GetRegistry().emplace<GpuLight>(self, light);
  }

  world.GetRegistry().emplace<Name>(self).name = GetName();
  return self;
}

void Block::UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const
{
  if (state.useAccum < GetUseDt())
  {
    return;
  }

  state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);
  auto& reg      = world.GetRegistry();
  const auto& h  = reg.get<Hierarchy>(self);
  const auto p   = h.parent;
  const auto& pt = reg.get<GlobalTransform>(p);
  const auto pos = pt.position;
  const auto dir = GetForward(pt.rotation);

  auto& grid = reg.ctx().get<TwoLevelGrid>();
  auto hit   = TwoLevelGrid::HitSurfaceParameters();
  if (GetMaxStackSize() > 0)
  {
    if (grid.TraceRaySimple(pos, dir, 10, hit))
    {
      const auto newPos = glm::ivec3(hit.voxelPosition + hit.flatNormalWorld);
      if (grid.GetVoxelAt(newPos) == 0)
      {
        if (world.GetRegistry().ctx().get<BlockRegistry>().Get(voxel).OnTryPlaceBlock(world, newPos))
        {
          state.count--;
        }
      }
    }
  }
}

void ItemDefinition::Dematerialize(World& world, entt::entity self) const
{
  if (self != entt::null)
  {
    world.GetRegistry().emplace_or_replace<DeferredDelete>(self);
  }
}

Physics::RigidBody& ItemDefinition::GiveCollider(World& world, entt::entity self) const
{
  assert(self != entt::null);
  world.GetRegistry().emplace<Friction>(self).axes = {.2, .1, .2};
  return Physics::AddRigidBody({world.GetRegistry(), self},
    {
      .shape = JPH::Ref(new JPH::BoxShape(Physics::ToJolt(GetDroppedColliderSize()))),
      .layer = Physics::Layers::DROPPED_ITEM,
    });
}

const ItemDefinition& ItemRegistry::Get(const std::string& name) const
{
  const auto id = GetId(name);
  return Get(id);
}

const ItemDefinition& ItemRegistry::Get(ItemId id) const
{
  return *idToDefinition_.at(id);
}

ItemId ItemRegistry::GetId(const std::string& name) const
{
  return nameToId_.at(name);
}

ItemId ItemRegistry::Add(ItemDefinition* itemDefinition)
{
  const auto id = (ItemId)idToDefinition_.size();
  nameToId_.try_emplace(itemDefinition->GetName(), id);
  idToDefinition_.emplace_back(itemDefinition);
  return id;
}

entt::entity SpriteItem::Materialize(World& world) const
{
  auto self = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f}, glm::identity<glm::quat>(), 0.25f);
  world.GetRegistry().emplace<Billboard>(self, sprite_);
  world.GetRegistry().emplace<Tint>(self, tint_);
  world.GetRegistry().emplace<Name>(self, GetName());
  return self;
}

std::vector<ItemIdAndCount> RandomLootDrop::Sample(PCG::Rng& rng) const
{
  auto items = std::vector<ItemIdAndCount>();
  for (int i = 0; i < count; i++)
  {
    if (rng.RandFloat() < chanceForOne)
    {
      items.emplace_back(item, 1);
    }
  }
  return items;
}

std::vector<ItemIdAndCount> PoolLootDrop::Sample(PCG::Rng& rng) const
{
  assert(!pool.empty());
  if (rng.RandFloat() <= chance)
  {
    const auto sampled = (int)rng.RandFloat(0.5f, GetTotalWeight() + 0.5f);

    int sum = 0;
    for (const auto& element : pool)
    {
      sum += element.weight;
      if (sampled >= sum)
      {
        return element.items;
      }
    }
  }
  
  return {};
}

int PoolLootDrop::GetTotalWeight() const
{
  int sum = 0;
  for (const auto& element : pool)
  {
    sum += element.weight;
  }
  return sum;
}

std::vector<ItemIdAndCount> LootDrops::Collect(PCG::Rng& rng) const
{
  auto items = std::vector<ItemIdAndCount>();

  for (const auto& drop : drops)
  {
    auto sampled = std::vector<ItemIdAndCount>();

    if (auto* r = std::get_if<RandomLootDrop>(&drop))
    {
      sampled = r->Sample(rng);
    }
    if (auto* p = std::get_if<PoolLootDrop>(&drop))
    {
      sampled = p->Sample(rng);
    }

    items.insert(items.end(), sampled.begin(), sampled.end());
  }

  return items;
}

void LootRegistry::Add(std::string name, std::unique_ptr<LootDrops>&& lootDrops)
{
  nameToLoot_.emplace(std::move(name), std::move(lootDrops));
}

const LootDrops* LootRegistry::Get(const std::string& name)
{
  if (auto it = nameToLoot_.find(name); it != nameToLoot_.end())
  {
    return it->second.get();
  }
  return nullptr;
}

void Spear::UsePrimary([[maybe_unused]] float dt, World& world, entt::entity self, ItemState& state) const
{
  if (state.useAccum < GetUseDt())
  {
    return;
  }

  state.useAccum = glm::clamp(state.useAccum - dt, 0.0f, dt);
  auto& path = world.GetRegistry().emplace<LinearPath>(self);
  path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, 0, -1}, .offsetSeconds = 0.15f});
  path.frames.emplace_back(LinearPath::KeyFrame{.position = {0, 0, 0}, .offsetSeconds = 0.15f});

  auto& reg    = world.GetRegistry();
  auto child   = reg.create();
  reg.emplace<Name>(child).name = "Hurtbox";
  reg.emplace<LocalTransform>(child) = {{0, 0, -1}, glm::identity<glm::quat>(), 1};
  reg.emplace<GlobalTransform>(child) = {{0, 0, -1}, glm::identity<glm::quat>(), 1};
  reg.emplace<Lifetime>(child).remainingSeconds = GetUseDt();
  reg.emplace<Hierarchy>(child);
  reg.emplace<ContactDamage>(child) = {20, 5};
  SetParent({reg, child}, self);

  auto sphere = JPH::Ref(new JPH::SphereShape(0.125));

  Physics::AddRigidBody({world.GetRegistry(), child}, {.shape = sphere, .isSensor = true, .motionType = JPH::EMotionType::Kinematic, .layer = Physics::Layers::PROJECTILE});
}

entt::entity Spear::Materialize(World& world) const
{
  auto self                                    = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f});
  world.GetRegistry().emplace<Mesh>(self).name = "spear";
  world.GetRegistry().emplace<Name>(self).name = "Spear";
  return self;
}

BlockDefinition::BlockDefinition(const CreateInfo& info)
  : createInfo_(info)
{}

bool BlockDefinition::OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition) const
{
  auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
  grid.SetVoxelAt(voxelPosition, GetBlockId());
  return true;
}

void BlockDefinition::OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const
{
  auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
  grid.SetVoxelAt(voxelPosition, 0);
}

const BlockDefinition& BlockRegistry::Get(const std::string& name) const
{
  return *idToDefinition_.at(nameToId_.at(name));
}

const BlockDefinition& BlockRegistry::Get(BlockId id) const
{
  return *idToDefinition_.at(id);
}

BlockId BlockRegistry::Add(BlockDefinition* blockDefinition)
{
  assert(!nameToId_.contains(blockDefinition->GetName()));
  assert(world_->GetRegistry().ctx().contains<ItemRegistry>());

  const auto myBlockId = (BlockId)idToDefinition_.size();
  blockDefinition->blockId_ = myBlockId;

  nameToId_.emplace(blockDefinition->GetName(), myBlockId);
  idToDefinition_.emplace_back(blockDefinition);

  auto& itemRegistry = world_->GetRegistry().ctx().get<ItemRegistry>();
  blockDefinition->itemId_ = itemRegistry.Add(new Block(myBlockId, blockDefinition->GetName()));

  return myBlockId;
}

void ExplodeyBlockDefinition::OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const
{
  BlockDefinition::OnDestroyBlock(world, voxelPosition);

  const auto radius2 = explodeyInfo_.radius * explodeyInfo_.radius;
  const auto cr = (int)ceil(explodeyInfo_.radius);
  // Additionally damage all blocks in a radius.
  for (int z = -cr; z <= cr; z++)
  for (int y = -cr; y <= cr; y++)
  for (int x = -cr; x <= cr; x++)
  {
    const auto newPos = voxelPosition + glm::ivec3(x, y, z);
    if (Math::Distance2(voxelPosition, newPos) <= radius2 && newPos != voxelPosition)
    {
      world.DamageBlock(newPos, explodeyInfo_.damage, explodeyInfo_.damageTier, explodeyInfo_.damageFlags);
    }
  }

  // Push entities away from center of blast.
  const auto center = glm::vec3(voxelPosition) + 0.5f;
  for (auto entity : world.GetEntitiesInSphere(center, explodeyInfo_.radius))
  {
    if (auto* v = world.GetRegistry().try_get<LinearVelocity>(entity))
    {
      const auto& t = world.GetRegistry().get<GlobalTransform>(entity);
      
      const auto force = explodeyInfo_.pushForce;
      v->v += force * glm::normalize(t.position - center);
    }
  }
}

void RainbowTool::Update(float dt, World& world, entt::entity self, ItemState& state) const
{
  ToolDefinition::Update(dt, world, self, state);
  if (self == entt::null)
  {
    return;
  }

  using namespace glm;
  auto hsv_to_rgb = [](vec3 hsv)
  {
    vec3 rgb = clamp(abs(mod(hsv.x * 6.0f + vec3(0.0, 4.0, 2.0), 6.0f) - 3.0f) - 1.0f, 0.0f, 1.0f);
    return hsv.z * mix(vec3(1.0), rgb, hsv.y);
  };

  auto& tint = world.GetRegistry().get<Tint>(self);
  tint.color = hsv_to_rgb({0.33f * world.GetRegistry().ctx().get<float>("time"_hs), 0.875f, 0.85f});
}

bool BlockEntityDefinition::OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition) const
{
  if (BlockDefinition::OnTryPlaceBlock(world, voxelPosition))
  {
    const auto worldPosition = glm::vec3(voxelPosition) + glm::vec3(0.5f);

    auto& entityPrefabs = world.GetRegistry().ctx().get<EntityPrefabRegistry>();
    auto spawnedEntity  = entityPrefabs.Get(blockEntityInfo_.id).Spawn(world, glm::vec3(0), glm::identity<glm::quat>());
    auto& registry      = world.GetRegistry();
    
    auto parent = registry.create();
    registry.emplace<BlockEntity>(parent);
    registry.emplace<Hierarchy>(parent);
    registry.emplace<LocalTransform>(parent, LocalTransform{worldPosition, glm::identity<glm::quat>(), 1});
    registry.emplace<GlobalTransform>(parent);
    registry.emplace<Name>(parent, "Block Entity");
    SetParent({registry, spawnedEntity}, parent);
    UpdateLocalTransform({registry, parent});
    return true;
  }
  return false;
}

void BlockEntityDefinition::OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const
{
  BlockDefinition::OnDestroyBlock(world, voxelPosition);
  for (auto entity : world.GetEntitiesInSphere(glm::vec3(voxelPosition) + glm::vec3(0.5f), 0.25f))
  {
    // Remove block entity
    if (world.GetRegistry().all_of<BlockEntity>(entity))
    {
      world.GetRegistry().emplace<DeferredDelete>(entity);
      return;
    }
  }
  assert(false && "Block entity didn't exist!");
}

void NpcSpawnDirector::Update(float dt)
{
  accumulator += dt;

  auto& registry = world_->GetRegistry();
  auto& rng      = registry.ctx().get<PCG::Rng>();
  auto& grid     = registry.ctx().get<TwoLevelGrid>();

  while (accumulator >= timeBetweenSpawns)
  {
    accumulator -= timeBetweenSpawns;

    constexpr size_t MAX_ENEMIES = 10;

    for (auto&& [entity, player, transform] : registry.view<Player, GlobalTransform>().each())
    {
      for (const auto& pDefinition : registry.ctx().get<EntityPrefabRegistry>().GetAllPrefabs())
      {
        if (registry.view<Enemy>().size() >= MAX_ENEMIES)
        {
          continue;
        }

        const auto& info = pDefinition->GetCreateInfo();

        if (rng.RandFloat() > info.spawnChance)
        {
          continue;
        }

        // Multiple attempts to spawn the entity in case spawn positions are invalid.
        for (int attempt = 0; attempt < 10; attempt++)
        {
          if (auto realPos = SampleWalkablePosition(grid, rng, transform.position, info.minSpawnDistance, info.maxSpawnDistance, info.canSpawnFloating))
          {
            pDefinition->Spawn(*world_, *realPos);
            break;
          }
        }
      }
    }
  }
}
