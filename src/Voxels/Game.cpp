#include "Game.h"
#ifndef GAME_HEADLESS
#include "PlayerHead.h"
#include "Input.h"
#endif
#include "Physics.h"

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

#include "Jolt/Physics/Collision/Shape/PlaneShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"

void World::InitializeGameState()
{
  for (auto e : registry_.view<entt::entity>())
  {
    registry_.destroy(e);
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
