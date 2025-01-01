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

#include "entt/entity/handle.hpp"

#include "tracy/Tracy.hpp"

#include "Jolt/Physics/Collision/Shape/PlaneShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/CylinderShape.h"
#include "Jolt/Physics/Constraints/DistanceConstraint.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "entt/signal/dispatcher.hpp"

#include <chrono>
#include <stack>

// We don't want this to happen when the component/entity is actually deleted, as we care about having a valid parent.
static void OnDeferredDeleteConstruct(entt::registry& registry, entt::entity entity)
{
  assert(registry.valid(entity));
  auto& h = registry.get<Hierarchy>(entity);
  if (h.parent != entt::null)
  {
    auto& ph = registry.get<Hierarchy>(h.parent);
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

            //const auto currentSpeed2  = glm::dot(projectile.velocity, projectile.velocity);
            //const auto energyFraction = (currentSpeed2) / (projectile.initialSpeed * projectile.initialSpeed);
            const auto energyFraction = glm::length(projectile.velocity) / projectile.initialSpeed;

            const auto effectiveKnockback = damage.knockback * energyFraction;
            world.DamageEntity(entity1, damage.damage * energyFraction);
            auto pushDir = projectile.velocity;
            pushDir.y    = 0;
            if (glm::length(pushDir) > 1e-3f)
            {
              pushDir = glm::normalize(pushDir);
            }
            pushDir *= effectiveKnockback * 3;
            pushDir.y = effectiveKnockback;
            //world.SetLinearVelocity(entity1, pushDir);
            const auto prevVelocity = world.GetLinearVelocity(entity1);
            pushDir.y /= exp2(glm::max(0.0f, prevVelocity.y * 1.0f)); // Reduce velocity gain (prevent stuff from flying super high- subject to change).
            world.SetLinearVelocity(entity1, prevVelocity + pushDir);
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
      if (world.GetRegistry().all_of<Player, Inventory>(entity1) && world.GetRegistry().all_of<DroppedItem>(entity2))
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
          auto pos1          = world.GetRegistry().get<GlobalTransform>(entity1).position;
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
          const auto prevVelocity = world.GetLinearVelocity(entity1);
          pushDir.y /= exp2(glm::max(0.0f, prevVelocity.y * 1.0f)); // Reduce velocity gain (prevent stuff from flying super high- subject to change).
          world.SetLinearVelocity(entity1, prevVelocity + pushDir);
          world.GetRegistry().get_or_emplace<CannotDamageEntities>(entity2).entities[entity1] = 0.2f;
        }
        return true;
      }
      return false;
    });
}

void OnNoclipCharacterControllerConstruct(entt::registry& registry, entt::entity entity)
{
  registry.remove<Physics::CharacterController>(entity);
  registry.remove<Physics::CharacterControllerShrimple>(entity);
}

void OnCharacterControllerConstruct(entt::registry& registry, entt::entity entity)
{
  registry.remove<NoclipCharacterController>(entity);
  registry.remove<Physics::CharacterControllerShrimple>(entity);
}

void OnCharacterControllerShrimpleConstruct(entt::registry& registry, entt::entity entity)
{
  registry.remove<NoclipCharacterController>(entity);
  registry.remove<Physics::CharacterController>(entity);
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

  world_->GetRegistry().on_construct<DeferredDelete>().connect<&OnDeferredDeleteConstruct>();
  world_->GetRegistry().on_construct<NoclipCharacterController>().connect<&OnNoclipCharacterControllerConstruct>();
  world_->GetRegistry().on_construct<Physics::CharacterController>().connect<&OnCharacterControllerConstruct>();
  world_->GetRegistry().on_construct<Physics::CharacterControllerShrimple>().connect<&OnCharacterControllerShrimpleConstruct>();
}

Game::~Game()
{
  Physics::Terminate();
}

void Game::Run()
{
  isRunning_ = true;

  auto previousTimestamp  = std::chrono::steady_clock::now();
  double fixedUpdateAccum = 0;

  while (isRunning_)
  {
    try
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

      dt.fraction = float(fixedUpdateAccum / tickDuration);

      if (head_)
      {
        head_->VariableUpdatePost(dt, *world_);
      }

      if (world_->GetRegistry().ctx().contains<CloseApplication>())
      {
        isRunning_ = false;
      }
    }
    catch(std::exception& e)
    {
      fprintf(stderr, "Exception caught: %s\n", e.what());
      throw;
    }
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

    // Generate input for enemies
    {
      ZoneScopedN("Pathfinding");
      // Won't work if entity is a child.
      for (auto&& [entity, input, transform] : registry_.view<InputState, LocalTransform>(entt::exclude<Player>).each())
      {
        // Pick a player to go towards (in this case it is just the first)
        auto players = registry_.view<Player>(entt::exclude<GhostPlayer>);
        if (players.begin() != players.end())
        {
          auto pe = players.front();
          if (auto* pt = registry_.try_get<GlobalTransform>(pe))
          {
            if (registry_.all_of<SimpleEnemyBehavior>(entity))
            {
              if (pt->position.y > transform.position.y)
              {
                input.jump = true;
              }

              transform.rotation = glm::quatLookAtRH(glm::normalize(pt->position - transform.position), {0, 1, 0});

              input.forward = 1;
            }

            if (registry_.all_of<PathfindingEnemyBehavior>(entity))
            {
              auto* cp            = registry_.try_get<Pathfinding::CachedPath>(entity);
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

              const auto myFootPos = glm::ivec3(GetFootPosition({registry_, entity}));

              // Get cached path.
              if (cp && !shouldFindPath)
              {
                path = cp->path;
              }
              else
              {
                // For ground characters, cast the player down. That way, if the player is in the air, the character will at least try to get under them instead of giving up.
                auto targetFootPos          = glm::ivec3(pt->position);
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

                const auto myHeight = (int)std::ceil(GetHeight({registry_, entity}));
                // path                 = Pathfinding::FindPath(*this, {.start = myFootPos, .goal = targetFootPos, .height = myHeight, .w = 1.5f});
                path = registry_.ctx().get<Pathfinding::PathCache>().FindOrGetCachedPath(*this,
                  {
                    .start  = glm::ivec3(myFootPos),
                    .goal   = glm::ivec3(targetFootPos),
                    .height = myHeight,
                    .w      = 1.5f,
                  });

                if (cp)
                {
                  cp->path = path;
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
                  if (cp->progress < path.size() - 1 && glm::distance(nextNode, glm::vec3(myFootPos)) <= 1.0f)
                  {
                    cp->progress++;
                  }
                }

                if (nextNode.y > myFootPos.y + 0.5f)
                {
                  input.jump = true;
                }
                transform.rotation = glm::quatLookAtRH(glm::normalize(nextNode - transform.position), {0, 1, 0});
                input.forward      = 1;
              }
            }

            UpdateLocalTransform({registry_, entity});
          }
        }
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
        auto tempCameraSpeed = 4.5f * dt;
        tempCameraSpeed *= input.sprint ? 4.0f : 1.0f;
        tempCameraSpeed *= input.walk ? 0.25f : 1.0f;
        transform.position += input.forward * forward * tempCameraSpeed;
        transform.position += input.strafe * right * tempCameraSpeed;
        transform.position.y += input.elevate * tempCameraSpeed;
        UpdateLocalTransform({registry_, entity});
      }

      if (registry_.any_of<Physics::CharacterController, Physics::CharacterControllerShrimple>(entity))
      {
        const auto rot   = glm::mat3_cast(transform.rotation);
        const auto right = rot[0];
        const auto gUp   = glm::vec3(0, 1, 0);
        // right and up will never be collinear if roll doesn't change
        const auto forward = glm::normalize(glm::cross(gUp, right));

        // Physics engine factors in deltaTime already
        float tempSpeed = 2;
        tempSpeed *= input.sprint ? 3.0f : 1.0f;
        tempSpeed *= input.walk ? 0.5f : 1.0f;

        auto deltaVelocity = glm::vec3(0);
        deltaVelocity += input.forward * forward * tempSpeed;
        deltaVelocity += input.strafe * right * tempSpeed;

        if (auto* cc = registry_.try_get<Physics::CharacterController>(entity))
        {
          auto prevVelocity = Physics::ToGlm(cc->character->GetLinearVelocity());
          if (cc->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround)
          {
            deltaVelocity += input.jump ? gUp * 8.0f : glm::vec3(0);
            prevVelocity.y = 0;
          }
          else // if (cc->character->GetGroundState() == JPH::CharacterBase::EGroundState::InAir)
          {
            const auto prevY = cc->character->GetLinearVelocity().GetY();
            //const auto prevY = cc->character->GetPosition().GetY() - cc->previousPosition.y;
            deltaVelocity += glm::vec3{0, prevY - 15 * dt, 0};
            // velocity += glm::vec3{0, -15 * dt, 0};
          }
          
          // cc->character->CheckCollision(cc->character->GetPosition(), cc->character->GetRotation(), Physics::ToJolt(velocity), 1e-4f, )
          cc->character->SetLinearVelocity(Physics::ToJolt(deltaVelocity));
          // printf("ground state: %d. height = %f. velocity.y = %f\n", (int)cc->character->GetGroundState(), cc->character->GetPosition().GetY(), velocity.y);
          // cc->character->AddLinearVelocity(Physics::ToJolt(velocity ));
          // cc->character->AddImpulse(Physics::ToJolt(velocity));
        }

        if (auto* cs = registry_.try_get<Physics::CharacterControllerShrimple>(entity))
        {
          auto velocity                  = Physics::ToGlm(cs->character->GetLinearVelocity());
          constexpr float groundFriction = 5.0f;
          float friction                 = groundFriction;
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
            constexpr float airControl = 0.25f;
            constexpr float airFriction = 0.05f;
            friction                    = airFriction;
            deltaVelocity.x *= airControl;
            deltaVelocity.z *= airControl;
            //const auto prevY = cs->character->GetLinearVelocity().GetY();
            //deltaVelocity += glm::vec3{0, prevY - 15 * dt, 0};
            deltaVelocity += glm::vec3{0, -15 * dt, 0};
          }
          constexpr float maxSpeed = 5;

          // Apply friction
          auto newVelocity = glm::vec3(velocity.x, 0, velocity.z);
          newVelocity -= friction * newVelocity * dt;

          // Apply dv
          newVelocity.x += deltaVelocity.x;
          newVelocity.z += deltaVelocity.z;

          // Clamp xz speed
          const float speed = glm::length(newVelocity);
          if (speed > maxSpeed)
          {
            newVelocity = glm::normalize(newVelocity) * maxSpeed;
          }

          // Y is not affected by speed clamp or friction
          newVelocity.y = velocity.y + deltaVelocity.y;
          
          cs->character->SetLinearVelocity(Physics::ToJolt(newVelocity));
        }
      }
    }

    // Player interaction
    for (auto&& [entity, player, transform, input, inventory] : registry_.view<Player, GlobalTransform, InputState, Inventory>(entt::exclude<GhostPlayer>).each())
    {
      if (input.interact)
      {
        //auto sphereSettings = JPH::SphereShapeSettings(0.4f);
        //sphereSettings.SetEmbedded();
        //auto sphere = sphereSettings.Create().Get();
        //auto sphere   = JPH::Ref(new JPH::BoxShape({0.4f, 1.9f, 0.4f}));
        auto sphere   = JPH::Ref(new JPH::CapsuleShape(0.4f, 0.4f));
        //auto sphere   = JPH::Ref(new JPH::CylinderShape(0.6f, 0.4f));
        sphere->SetDensity(.33f);

        auto e = CreateRenderableEntity(transform.position + GetForward(transform.rotation) * 5.0f, {1, 0, 0, 0}, 0.4f);
        registry_.emplace<Mesh>(e).name = "frog";
        registry_.emplace<Name>(e, "Fall ball");
        registry_.emplace<Health>(e) = {100, 100};
        //registry_.emplace<SimpleEnemyBehavior>(e);
        registry_.emplace<PathfindingEnemyBehavior>(e);
        registry_.emplace<Pathfinding::CachedPath>(e).timeBetweenUpdates = 1;
        registry_.emplace<InputState>(e);
        registry_.emplace<Loot>(e).name = "standard";
        registry_.emplace<TeamFlags>(e, TeamFlagBits::ENEMY);

        auto& contactDamage = registry_.emplace<ContactDamage>(e);
        contactDamage.damage = 10;
        //registry_.emplace<Lifetime>(e).remainingSeconds = 5;
        //Physics::AddCharacterController({registry_, e}, {sphere});
        Physics::AddCharacterControllerShrimple({registry_, e}, {.shape = sphere});
        //Physics::AddRigidBody({registry_, e},
        //  {
        //    .shape      = sphere,
        //    .activate   = true,
        //    .motionType = JPH::EMotionType::Dynamic,
        //    .layer      = Physics::Layers::CHARACTER,
        //  });

        auto e2 = CreateRenderableEntity({1.0f, 0.3f, -0.8f}, {1, 0, 0, 0}, 1.5f);
        registry_.emplace<Name>(e2).name = "Child";
        registry_.emplace<Mesh>(e2).name = "ar15";
        SetParent({registry_, e2}, e);

        auto hitboxShape = JPH::Ref(new JPH::SphereShape(1.0f));

        auto eHitbox                          = registry_.create();
        registry_.emplace<Mesh>(eHitbox).name = "frog";
        registry_.emplace<Name>(eHitbox).name = "Frog hitbox";
        registry_.emplace<ForwardCollisionsToParent>(eHitbox);
        registry_.emplace<RenderTransform>(eHitbox);
        registry_.emplace<PreviousGlobalTransform>(eHitbox);
        auto& tpHitbox    = registry_.emplace<LocalTransform>(eHitbox);
        tpHitbox.position = {};
        tpHitbox.rotation = glm::identity<glm::quat>();
        tpHitbox.scale                              = 1;
        registry_.emplace<GlobalTransform>(eHitbox) = {{}, glm::identity<glm::quat>(), 1};
        registry_.emplace<Hierarchy>(eHitbox);
        Physics::AddRigidBody({registry_, eHitbox},
          {
            .shape      = hitboxShape,
            .isSensor   = true,
            .motionType = JPH::EMotionType::Kinematic,
            .layer      = Physics::Layers::CHARACTER_SENSOR,
          });
        SetParent({registry_, eHitbox}, e);
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
      for (auto& row : inventory.slots)
      {
        for (auto& slot : row)
        {
          if (slot.id != nullItem)
          {
            registry_.ctx().get<ItemRegistry>().Get(slot.id).Update(dt, *this, slot);
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
    for (auto&& [entity, health] : registry_.view<Health>(entt::exclude<GhostPlayer>).each())
    {
      if (health.hp <= 0)
      {
        if (auto* loot = registry_.try_get<Loot>(entity))
        {
          const auto& transform = registry_.get<GlobalTransform>(entity);
          auto* table = registry_.ctx().get<LootRegistry>().Get(loot->name);
          assert(table);
          const auto& itemRegistry = registry_.ctx().get<ItemRegistry>();
          for (auto drop : table->Collect(Rng()))
          {
            const auto& def = itemRegistry.Get(drop.item);
            auto droppedEntity = def.Materialize(*this);
            def.GiveCollider(*this, droppedEntity);
            registry_.emplace<DroppedItem>(droppedEntity, DroppedItem{{.id = drop.item, .count = drop.count}});
            SetLocalPosition(droppedEntity, transform.position);
            const auto velocity = GetLinearVelocity(entity);
            SetLinearVelocity(droppedEntity, velocity + Rng().RandFloat(1, 3) * Math::RandVecInCone({Rng().RandFloat(), Rng().RandFloat()}, glm::vec3(0, 1, 0), glm::half_pi<float>()));
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

    // Process destroyed entities
    auto entitiesToDestroy = std::stack<entt::entity>();
    for (auto entity : registry_.view<DeferredDelete>())
    {
      entitiesToDestroy.push(entity);
    }

    // "Recursively" destroy entities in hierarchies
    while (!entitiesToDestroy.empty())
    {
      auto entity = entitiesToDestroy.top();
      entitiesToDestroy.pop();

      if (auto* h = registry_.try_get<Hierarchy>(entity))
      {
        for (auto child : h->children)
        {
          entitiesToDestroy.push(child);
        }
      }
      
      registry_.destroy(entity);
    }
  }
  
  ticks_++;
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
  for (auto e : registry_.view<entt::entity>())
  {
    registry_.destroy(e);
  }
  
  // Reset item registry
  auto& items = registry_.ctx().insert_or_assign<ItemRegistry>({});
  [[maybe_unused]] const auto gunId = items.Add(new Gun());
  [[maybe_unused]] const auto gun2Id = items.Add(new Gun2());
  [[maybe_unused]] const auto pickaxeId = items.Add(new Pickaxe());
  [[maybe_unused]] const auto blockId = items.Add(new Block(1));
  [[maybe_unused]] const auto spearId = items.Add(new Spear());
  
  auto& crafting = registry_.ctx().insert_or_assign<Crafting>({});
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{blockId, 1}},
    {{gunId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {{blockId, 5}, {gunId, 1}},
    {{gun2Id, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {},
    {{blockId, 1}},
  });
  crafting.recipes.emplace_back(Crafting::Recipe{
    {},
    {{spearId, 1}},
  });

  auto& loot = registry_.ctx().insert_or_assign<LootRegistry>({});
  auto standardLoot = std::make_unique<LootDrops>();
  standardLoot->drops.emplace_back(RandomLootDrop{
    .item = blockId,
    .count = 6,
    .chanceForOne = 0.5f,
  });
  standardLoot->drops.emplace_back(RandomLootDrop{
    .item         = gunId,
    .count        = 2,
    .chanceForOne = 0.125f,
  });
  standardLoot->drops.emplace_back(RandomLootDrop{
    .item         = gun2Id,
    .count        = 2,
    .chanceForOne = 0.125f,
  });
  loot.Add("standard", std::move(standardLoot));

  // Reset RNG
  registry_.ctx().insert_or_assign<PCG::Rng>(1234);

  auto& grid = registry_.ctx().insert_or_assign(TwoLevelGrid(glm::vec3{1, 1, 1}));
  // Top level bricks
  for (int k = 0; k < grid.topLevelBricksDims_.z; k++)
    for (int j = 0; j < grid.topLevelBricksDims_.y; j++)
      for (int i = 0; i < grid.topLevelBricksDims_.x; i++)
      {
        const auto tl = glm::ivec3{i, j, k};

        // Bottom level bricks
        for (int c = 0; c < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; c++)
          for (int b = 0; b < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; b++)
            for (int a = 0; a < TwoLevelGrid::TL_BRICK_SIDE_LENGTH; a++)
            {
              const auto bl = glm::ivec3{a, b, c};

              // Voxels
              for (int z = 0; z < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; z++)
                for (int y = 0; y < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; y++)
                  for (int x = 0; x < TwoLevelGrid::BL_BRICK_SIDE_LENGTH; x++)
                  {
                    const auto local = glm::ivec3{x, y, z};
                    const auto p     = tl * TwoLevelGrid::TL_BRICK_VOXELS_PER_SIDE + bl * TwoLevelGrid::BL_BRICK_SIDE_LENGTH + local;
                    // const auto left  = glm::vec3(50, 30, 20);
                    // const auto right = glm::vec3(90, 30, 20);
                    // if ((glm::distance(p, left) < 10) || (glm::distance(p, right) < 10) || (p.y > 30 && glm::distance(glm::vec2(p.x, p.z), glm::vec2(70, 20)) < 10))
                    if (de2(glm::vec3(p) / 10.f + 2.0f) < 0.011f)
                    // if (de3(p) < 0.011f)
                    {
                      grid.SetVoxelAt(p, 1);
                    }
                    else
                    {
                      grid.SetVoxelAt(p, 0);
                    }
                  }
            }

        grid.CoalesceDirtyBricks();
      }

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
  GivePlayerCharacterController(p);
  //registry_.emplace<NoclipCharacterController>(p);
  //cc.character->SetMaxStrength(10000000);
  auto& inventory = registry_.emplace<Inventory>(p, *this);
  inventory.OverwriteSlot({0, 0}, {gunId}, p);
  inventory.OverwriteSlot({0, 1}, {gun2Id}, p);
  inventory.OverwriteSlot({0, 2}, {pickaxeId}, p);

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

  auto twoLevelGridShape = JPH::Ref(new Physics::TwoLevelGridShape(grid));

  auto ve = registry_.create();
  registry_.emplace<Name>(ve).name = "Voxels";
  Physics::AddRigidBody({registry_, ve}, {
    .shape = twoLevelGridShape,
    .activate = false,
    .motionType = JPH::EMotionType::Static,
    .layer = Physics::Layers::WORLD,
  });

  auto sphereSettings = JPH::SphereShapeSettings(1);
  sphereSettings.SetEmbedded();
  auto sphere = sphereSettings.Create().Get();

  auto prevBody = std::optional<JPH::BodyID>();
  for (int i = 0; i < 10; i++)
  {
    auto a = CreateRenderableEntity({0, 5 + i * 2, i * .1f});
    registry_.emplace<Name>(a).name = "Ball";
    registry_.emplace<Mesh>(a).name = "frog";
    auto rb = Physics::AddRigidBody({registry_, a}, {
      .shape = sphere,
      .activate = true,
      .motionType = JPH::EMotionType::Dynamic,
      .layer = Physics::Layers::DEBRIS,
    });

    //// Constraint test
    //if (prevBody)
    //{
    //  auto settings   = JPH::DistanceConstraintSettings();
    //  //settings.SetEmbedded();
    //  settings.mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
    //  settings.mMinDistance = 5;
    //  settings.mMaxDistance = 8;
    //  
    //  auto& bd        = Physics::GetBodyInterface();
    //  [[maybe_unused]] auto constraint = bd.CreateConstraint(&settings, *prevBody, rb.body);
    //  //bd.ActivateConstraint(constraint);
    //  Physics::GetPhysicsSystem().AddConstraint(constraint);
    //}
    prevBody = rb.body; 
  }
}

entt::entity World::CreateRenderableEntity(glm::vec3 position, glm::quat rotation, float scale)
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

void World::SetLocalPosition(entt::entity entity, glm::vec3 position)
{
  auto& lt = registry_.get<LocalTransform>(entity);
  lt.position = position;
  if (auto* rb = registry_.try_get<Physics::RigidBody>(entity))
  {
    // Using the local position, which is fine because entities with a rigid body should never have a parent.
    Physics::GetBodyInterface().SetPosition(rb->body, Physics::ToJolt(position), JPH::EActivation::Activate);
  }
  UpdateLocalTransform({registry_, entity});
}

void World::SetLocalScale(entt::entity entity, float scale)
{
  auto& lt = registry_.get<LocalTransform>(entity);
  lt.scale = scale;
  UpdateLocalTransform({registry_, entity});
}

void World::SetLinearVelocity(entt::entity entity, glm::vec3 velocity)
{
  assert(registry_.get<Hierarchy>(entity).parent == entt::null);

  if (auto* rb = registry_.try_get<Physics::RigidBody>(entity))
  {
    Physics::GetBodyInterface().SetLinearVelocity(rb->body, Physics::ToJolt(velocity));
  }
  if (auto* cc = registry_.try_get<Physics::CharacterController>(entity))
  {
    cc->character->SetLinearVelocity(Physics::ToJolt(velocity));
  }
  if (auto* cc = registry_.try_get<Physics::CharacterControllerShrimple>(entity))
  {
    cc->character->SetLinearVelocity(Physics::ToJolt(velocity));
    if (velocity.y > 0)
    {
      cc->previousGroundState = JPH::CharacterBase::EGroundState::InAir;
    }
  }
}

void World::AddLinearVelocity(entt::entity entity, glm::vec3 velocity)
{
  assert(registry_.get<Hierarchy>(entity).parent == entt::null);

  if (auto* rb = registry_.try_get<Physics::RigidBody>(entity))
  {
    Physics::GetBodyInterface().AddLinearVelocity(rb->body, Physics::ToJolt(velocity));
  }
  if (auto* cc = registry_.try_get<Physics::CharacterController>(entity))
  {
    auto oldVel = cc->character->GetLinearVelocity();
    cc->character->SetLinearVelocity(Physics::ToJolt(velocity) + oldVel);
  }
  if (auto* cc = registry_.try_get<Physics::CharacterControllerShrimple>(entity))
  {
    cc->character->AddLinearVelocity(Physics::ToJolt(velocity));
  }
}

glm::vec3 World::GetLinearVelocity(entt::entity entity)
{
  assert(registry_.get<Hierarchy>(entity).parent == entt::null);

  if (auto* rb = registry_.try_get<Physics::RigidBody>(entity))
  {
    return Physics::ToGlm(Physics::GetBodyInterface().GetLinearVelocity(rb->body));
  }
  if (auto* cc = registry_.try_get<Physics::CharacterController>(entity))
  {
    return Physics::ToGlm(cc->character->GetLinearVelocity());
  }
  if (auto* cc = registry_.try_get<Physics::CharacterControllerShrimple>(entity))
  {
    return Physics::ToGlm(cc->character->GetLinearVelocity());
  }

  return {};
}

entt::entity World::GetChildNamed(entt::entity entity, std::string_view name)
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

TeamFlags* World::GetTeamFlags(entt::entity entity)
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

void World::GivePlayerColliders(entt::entity playerEntity)
{
  constexpr float playerHalfHeight = 0.8f * 1.5f;
  constexpr float playerHalfWidth  = 0.3f * 3.0f;
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
      .layer      = Physics::Layers::CHARACTER_SENSOR,
    });
  SetParent({registry_, pHitbox}, playerEntity);
}

void World::KillPlayer(entt::entity playerEntity)
{
  registry_.remove<NoclipCharacterController, Physics::CharacterController, Physics::CharacterControllerShrimple>(playerEntity);
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

bool World::CanEntityDamageEntity(entt::entity entitySource, entt::entity entityTarget)
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

bool World::AreEntitiesEnemies(entt::entity entity1, entt::entity entity2)
{
  auto* team1 = GetTeamFlags(entity1);
  auto* team2 = GetTeamFlags(entity2);
  if (!team1 || !team2 || !(*team1 & *team2))
  {
    return true;
  }
  return false;
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
  }

  for (auto child : h.children)
  {
    RefreshGlobalTransform({*handle.registry(), child});
  }
}

void UpdateLocalTransform(entt::handle handle)
{
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
  const bool firstIsActive = first == activeSlotCoord;
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

  if (activeSlotCoord == slot)
  {
    def.Dematerialize(*world, activeSlotEntity);
    activeSlotEntity = entt::null;
  }

  auto entity = def.Materialize(*world);
  def.GiveCollider(*world, entity);
  world->GetRegistry().emplace<DroppedItem>(entity).item = std::exchange(item, {});
  return entity;
}

void Inventory::OverwriteSlot(glm::ivec2 rowCol, ItemState itemState, entt::entity parent)
{
  const bool dstIsActive = rowCol == activeSlotCoord;
  if (dstIsActive && ActiveSlot().id != nullItem)
  {
    world->GetRegistry().ctx().get<ItemRegistry>().Get(ActiveSlot().id).Dematerialize(*world, activeSlotEntity);
  }
  slots[rowCol[0]][rowCol[1]] = itemState;
  if (dstIsActive && itemState.id != nullItem)
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
  auto self                                    = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f});
  world.GetRegistry().emplace<Mesh>(self).name = "ar15";
  world.GetRegistry().emplace<Name>(self).name = "Gun";
  return self;
}

void Gun::Dematerialize(World& world, entt::entity self) const
{
  world.GetRegistry().emplace<DeferredDelete>(self);
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

    for (int i = 0; i < bullets; i++)
    {
      const float bulletScale = 0.05f;
      auto bulletShape        = JPH::Ref(new JPH::SphereShape(.04f));
      bulletShape->SetDensity(11000);
      const auto dir = Math::RandVecInCone({world.Rng().RandFloat(), world.Rng().RandFloat()}, GetForward(transform.rotation), glm::radians(accuracyMoa / 60.0f));
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

      auto& projectile       = registry.emplace<Projectile>(b);
      projectile.initialSpeed = velocity;
      projectile.velocity    = dir * velocity;
      projectile.drag        = 0.25f;
      projectile.restitution = 0.25f;

      auto& contactDamage     = registry.emplace<ContactDamage>(b);
      contactDamage.damage    = damage;
      contactDamage.knockback = knockback;

      if (auto* team = world.GetTeamFlags(self))
      {
        registry.emplace<TeamFlags>(b, *team);
      }
    }

    // If parent is player, apply recoil
    if (auto* h = registry.try_get<Hierarchy>(self); h && h->parent != entt::null)
    {
      const auto vr = glm::radians(vrecoil + world.Rng().RandFloat(-vrecoilDev, vrecoilDev));
      const auto hr = glm::radians(hrecoil + world.Rng().RandFloat(-hrecoilDev, hrecoilDev));
      if (auto* is = registry.try_get<InputLookState>(h->parent))
      {
        is->pitch += vr;
        is->yaw += hr;
        UpdateLocalTransform({registry, h->parent});
      }
    }
  }
}

entt::entity Gun2::Materialize(World& world) const
{
  auto self                                = Gun::Materialize(world);
  world.GetRegistry().get<Mesh>(self).name = "frog";
  world.SetLocalScale(self, 0.125f);
  return self;
}

entt::entity Pickaxe::Materialize(World& world) const
{
  auto self = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f}, glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0)));
  world.GetRegistry().emplace<Mesh>(self).name = "ar15";
  world.GetRegistry().emplace<Name>(self).name = "Pickaxe";
  //SetParent({world->GetRegistry(), self}, world);
  return self;
}

void Pickaxe::Dematerialize(World& world, entt::entity self) const
{
  world.GetRegistry().emplace<DeferredDelete>(self);
}

void Pickaxe::UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const
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
  if (grid.TraceRaySimple(pos, dir, 10, hit))
  {
    //auto prevVoxel = grid.GetVoxelAt(glm::ivec3(hit.voxelPosition)); // TODO: use this
    grid.SetVoxelAt(glm::ivec3(hit.voxelPosition), 0);
    
    auto itemId         = reg.ctx().get<ItemRegistry>().GetId("Block");
    const auto& itemDef = reg.ctx().get<ItemRegistry>().Get(itemId);
    auto itemSelf       = itemDef.Materialize(world);

    reg.get<LocalTransform>(itemSelf).position = hit.voxelPosition + 0.5f;
    UpdateLocalTransform({reg, itemSelf});
    auto& rb = itemDef.GiveCollider(world, itemSelf);
    reg.emplace<DroppedItem>(itemSelf).item = ItemState{itemId};

    const auto throwdir = glm::vec3(world.Rng().RandFloat(-0.25f, 0.25f), 1, world.Rng().RandFloat(-0.25f, 0.25f));
    Physics::GetBodyInterface().SetLinearVelocity(rb.body, Physics::ToJolt(throwdir * 2.0f));

    // Awaken bodies that are adjacent to destroyed voxel in case they were resting on it.
    // TODO: This doesn't seem to be robust. Setting mTimeBeforeSleep to 0 in PhysicsSettings seems to disable sleeping, which fixes this issue.
    Physics::GetBodyInterface().ActivateBodiesInAABox({Physics::ToJolt(hit.voxelPosition + 0.5f), 2.0f}, {}, {});
  }
}

entt::entity Block::Materialize(World& world) const
{
  auto self = world.CreateRenderableEntity({0.2f, -0.2f, -0.5f}, glm::identity<glm::quat>(), 0.25f);
  world.GetRegistry().emplace<Mesh>(self).name = "cube";
  world.GetRegistry().emplace<Name>(self).name = "Block";
  return self;
}

void Block::Dematerialize(World& world, entt::entity self) const
{
  world.GetRegistry().emplace<DeferredDelete>(self);
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
        state.count--;
        grid.SetVoxelAt(newPos, voxel);
      }
    }
  }
}

Physics::RigidBody& ItemDefinition::GiveCollider(World& world, entt::entity self) const
{
  assert(self != entt::null);
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

void Spear::Dematerialize(World& world, entt::entity self) const
{
  world.GetRegistry().emplace<DeferredDelete>(self);
}
