#include "Game.h"
#ifndef GAME_HEADLESS
#include "PlayerHead.h"
#include "Input.h"
#endif
#include "Physics/Physics.h"
#include "Physics/TwoLevelGridShape.h"
#include "TwoLevelGrid.h"

#include "entt/entity/handle.hpp"

#include <chrono>

Game::Game(uint32_t tickHz)
{
  world_ = std::make_unique<World>();
  Physics::Initialize(*world_);
#ifdef GAME_HEADLESS
  head_ = std::make_unique<NullHead>();
  world_->CreateSingletonComponent<GameState>() = GameState::GAME;
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
#endif

  world_->GetRegistry().ctx().emplace<Debugging>();
  world_->GetRegistry().ctx().emplace<TimeScale>();
  world_->GetRegistry().ctx().emplace<TickRate>().hz = tickHz;
  world_->GetRegistry().ctx().emplace_as<float>("time"_hs) = 0; // TODO: TEMP
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
    const auto timeScale      = world_->GetRegistry().ctx().get<TimeScale>().scale;
    const auto tickHz         = world_->GetRegistry().ctx().get<TickRate>().hz;
    const double tickDuration = 1.0 / tickHz;

    const auto currentTimestamp = std::chrono::steady_clock::now();
    const auto realDeltaTime    = std::chrono::duration_cast<std::chrono::microseconds>(currentTimestamp - previousTimestamp).count() / 1'000'000.0;
    previousTimestamp = currentTimestamp;

    const auto dt = DeltaTime{
      .game = static_cast<float>(realDeltaTime * timeScale),
      .real = static_cast<float>(realDeltaTime),
    };

    if (head_)
    {
      head_->VariableUpdatePre(dt, *world_);
    }

    fixedUpdateAccum += realDeltaTime * timeScale;
    while (fixedUpdateAccum > tickDuration)
    {
      fixedUpdateAccum -= tickDuration;
      // TODO: Networking update before FixedUpdate
      world_->FixedUpdate(static_cast<float>(tickDuration));
    }

    if (head_)
    {
      head_->VariableUpdatePost(dt, *world_);
    }

    if (world_->GetRegistry().ctx().contains<CloseApplication>())
    {
      isRunning_ = false;
    }
  }
}

#include "Jolt/Physics/Collision/Shape/PlaneShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"

void World::FixedUpdate(float dt)
{
  if (registry_.ctx().get<GameState>() == GameState::GAME)
  {
    registry_.ctx().get<float>("time"_hs) += dt;
    // Update previous transforms before updating it (this should be done after updating the game state from networking)
    for (auto&& [entity, transform, interpolatedTransform] : registry_.view<Transform, InterpolatedTransform>().each())
    {
      interpolatedTransform.accumulator = 0;
      interpolatedTransform.previousTransform = transform;
    }
    
    Physics::FixedUpdate(dt, *this);

    // Clamp movement input
    for (auto&& [entity, input] : registry_.view<InputState>().each())
    {
      input.strafe  = glm::clamp(input.strafe, -1.0f, 1.0f);
      input.forward = glm::clamp(input.forward, -1.0f, 1.0f);
      input.elevate = glm::clamp(input.elevate, -1.0f, 1.0f);
    }

    // Apply movement input
    auto playerTransform = Transform{};
    for (auto&& [entity, player, input, transform] : registry_.view<Player, InputState, Transform, NoclipCharacterController>().each())
    {
      if (player.id == 0)
      {
        const auto rot       = glm::mat3_cast(transform.rotation);
        const auto right     = rot[0];
        const auto forward   = rot[2];
        auto tempCameraSpeed = 4.5f * dt;
        tempCameraSpeed *= input.sprint ? 4.0f : 1.0f;
        tempCameraSpeed *= input.walk ? 0.25f : 1.0f;
        transform.position += input.forward * forward * tempCameraSpeed;
        transform.position += input.strafe * right * tempCameraSpeed;
        transform.position.y += input.elevate * tempCameraSpeed;
        playerTransform = transform;

        if (input.interact)
        {
          auto sphereSettings = JPH::SphereShapeSettings(1);
          sphereSettings.SetEmbedded();
          auto sphere = sphereSettings.Create().Get();

          auto e      = registry_.create();
          auto& et    = registry_.emplace<Transform>(e);
          et.position = playerTransform.position + glm::mat3_cast(playerTransform.rotation)[2] * 5.0f;
          et.rotation = glm::identity<glm::quat>();
          et.scale    = 1;
          registry_.emplace<InterpolatedTransform>(e);
          registry_.emplace<RenderTransform>(e);
          registry_.emplace<TempMesh>(e);
          registry_.emplace<Name>(e, "Fall ball");
          Physics::AddRigidBody({registry_, e}, {
            .shape = sphere,
            .activate = true,
            .motionType = JPH::EMotionType::Dynamic,
            .layer = Physics::Layers::MOVING,
          });
        }
      }
    }

    // End of tick, reset input
    for (auto&& [entity, input] : registry_.view<InputState>().each())
    {
      input = {};
    }

    // Player "holds" entity
    //for (auto&& [entity, transform] : registry_.view<Transform, TempMesh>().each())
    //{
    //  transform.position = playerTransform.position + glm::mat3_cast(playerTransform.rotation)[2] * 5.0f;
    //}
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

  auto& grid = registry_.ctx().emplace<TwoLevelGrid>(glm::vec3{1, 1, 1});
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
  registry_.emplace<InputState>(p);
  registry_.emplace<InputLookState>(p);
  auto& tp = registry_.emplace<Transform>(p);
  tp.position = {0, 0, 0};
  tp.rotation = glm::identity<glm::quat>();
  tp.scale    = 1;
  registry_.emplace<NoclipCharacterController>(p);
  registry_.emplace<InterpolatedTransform>(p);
  registry_.emplace<RenderTransform>(p);

  auto e = registry_.create();
  registry_.emplace<Name>(e).name = "Test";
  auto& ep    = registry_.emplace<Transform>(e);
  ep.position = {0, 0, 0};
  ep.rotation = glm::identity<glm::quat>();
  ep.scale    = 1;
  registry_.emplace<TempMesh>(e);
  registry_.emplace<InterpolatedTransform>(e);
  registry_.emplace<RenderTransform>(e);

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
    .layer = Physics::Layers::NON_MOVING,
  });

  auto twoLevelGridShape = JPH::Ref(new Physics::TwoLevelGridShape(grid));

  auto ve = registry_.create();
  registry_.emplace<Name>(ve).name = "Voxels";
  Physics::AddRigidBody({registry_, ve}, {
    .shape = twoLevelGridShape,
    .activate = false,
    .motionType = JPH::EMotionType::Static,
    .layer = Physics::Layers::NON_MOVING,
  });

  auto sphereSettings = JPH::SphereShapeSettings(1);
  sphereSettings.SetEmbedded();
  auto sphere = sphereSettings.Create().Get();

  for (int i = 0; i < 10; i++)
  {
    auto a = registry_.create();
    registry_.emplace<Name>(a).name = "Ball";
    auto& at = registry_.emplace<Transform>(a);
    at.position = {0, 5 + i * 2, i * .1f};
    at.rotation = glm::identity<glm::quat>();
    at.scale    = 1;
    registry_.emplace<TempMesh>(a);
    registry_.emplace<InterpolatedTransform>(a);
    registry_.emplace<RenderTransform>(a);
    Physics::AddRigidBody({registry_, a}, {
      .shape = sphere,
      .activate = true,
      .motionType = JPH::EMotionType::Dynamic,
      .layer = Physics::Layers::MOVING,
    });
  }
}
