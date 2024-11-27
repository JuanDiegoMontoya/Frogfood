#include "Game.h"
#ifndef GAME_HEADLESS
#include "PlayerHead.h"
#endif
#include "Input.h"

#include <chrono>

Game::Game(uint32_t tickHz)
{
  world_ = std::make_unique<World>();
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
  world_->CreateSingletonComponent<GameState>() = GameState::MENU;
#endif

  world_->CreateSingletonComponent<TimeScale>();
  world_->CreateSingletonComponent<TickRate>().hz = tickHz;
}

void Game::Run()
{
  isRunning_ = true;

  auto previousTimestamp  = std::chrono::steady_clock::now();
  double fixedUpdateAccum = 0;

  while (isRunning_)
  {
    const auto timeScale      = world_->GetSingletonComponent<TimeScale>().scale;
    const auto tickHz         = world_->GetSingletonComponent<TickRate>().hz;
    const double tickDuration = 1.0 / tickHz;

    const auto currentTimestamp = std::chrono::steady_clock::now();
    const auto realDeltaTime    = std::chrono::duration_cast<std::chrono::microseconds>(currentTimestamp - previousTimestamp).count() / 1'000'000.0;
    fixedUpdateAccum += realDeltaTime * timeScale;
    previousTimestamp = currentTimestamp;

    const auto dt = DeltaTime{
      .game = static_cast<float>(realDeltaTime * timeScale),
      .real = static_cast<float>(realDeltaTime),
    };

    if (head_)
    {
      head_->VariableUpdatePre(dt, *world_);
    }

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

    if (!world_->GetRegistry().view<CloseApplication>().empty())
    {
      isRunning_ = false;
    }
  }
}

void World::FixedUpdate(float dt)
{
  if (GetSingletonComponent<GameState>() == GameState::GAME)
  {
    // Update previous transforms before updating it (this should be done after updating the game state from networking)
    for (auto&& [entity, transform, interpolatedTransform] : registry_.view<Transform, InterpolatedTransform>().each())
    {
      interpolatedTransform.accumulator = 0;
      interpolatedTransform.previousTransform = transform;
    }

    // Clamp movement input
    for (auto&& [entity, input] : registry_.view<InputState>().each())
    {
      input.strafe  = glm::clamp(input.strafe, -1.0f, 1.0f);
      input.forward = glm::clamp(input.forward, -1.0f, 1.0f);
      input.elevate = glm::clamp(input.elevate, -1.0f, 1.0f);
    }

    // Apply movement input
    for (auto&& [entity, player, input, transform] : registry_.view<Player, InputState, Transform, NoclipCharacterController>().each())
    {
      const auto rot       = glm::mat3_cast(transform.rotation);
      const auto right     = rot[0];
      const auto forward   = rot[2];
      auto tempCameraSpeed = 4.5f;
      tempCameraSpeed *= input.sprint ? 4.0f : 1.0f;
      tempCameraSpeed *= input.walk ? 0.25f : 1.0f;
      transform.position += input.forward * forward * dt * tempCameraSpeed;
      transform.position += input.strafe * right * dt * tempCameraSpeed;
      transform.position.y += input.elevate * dt * tempCameraSpeed;
    }

    // End of tick, reset input
    for (auto&& [entity, input] : registry_.view<InputState>().each())
    {
      input = {};
    }
  }

  ticks_++;
}

void World::InitializeGameState()
{
  // Erase every entity that isn't holding a singleton component
  for (auto e : registry_.view<entt::entity>(entt::exclude<Singleton>))
  {
    registry_.destroy(e);
  }

  // Make player entity
  auto p = registry_.create();
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
  auto& ep    = registry_.emplace<Transform>(e);
  ep.position = {0, 0, 0};
  ep.rotation = glm::identity<glm::quat>();
  ep.scale    = 1;
  registry_.emplace<TempMesh>(e);
}
