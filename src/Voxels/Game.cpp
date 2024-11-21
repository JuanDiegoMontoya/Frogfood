#include "Game.h"
#ifndef GAME_HEADLESS
#include "PlayerHead.h"
#endif

#include <chrono>

Game::Game(uint32_t tickHz) : tickHz_(tickHz)
{
  world_ = std::make_unique<World>();
#ifdef GAME_HEADLESS
  head_ = std::make_unique<NullHead>();
#else
  head_ = std::make_unique<PlayerHead>(PlayerHead::CreateInfo{
    .name        = "Gabagool",
    .maximize    = false,
    .decorate    = true,
    .presentMode = VK_PRESENT_MODE_FIFO_KHR,
    .world       = world_.get(),
  });
#endif
}

void Game::Run()
{
  isRunning_ = true;

  auto previousTimestamp  = std::chrono::steady_clock::now();
  const double tickLength = 1.0 / tickHz_;
  double fixedUpdateAccum = 0;

  while (isRunning_)
  {
    const auto currentTimestamp = std::chrono::steady_clock::now();
    const auto realDeltaTime    = std::chrono::duration_cast<std::chrono::microseconds>(currentTimestamp - previousTimestamp).count() / 1'000'000.0;
    fixedUpdateAccum += realDeltaTime * gameDeltaTimeScale;
    previousTimestamp = currentTimestamp;

    const auto dt = DeltaTime{
      .game = static_cast<float>(realDeltaTime * gameDeltaTimeScale),
      .real = static_cast<float>(realDeltaTime),
    };

    if (head_)
    {
      head_->VariableUpdatePre(dt, *world_);
    }

    while (fixedUpdateAccum > tickLength)
    {
      fixedUpdateAccum -= tickLength;
      // TODO: call game FixedUpdate
      world_->FixedUpdate(static_cast<float>(tickLength));
    }

    if (head_)
    {
      head_->VariableUpdatePost(dt, *world_);
    }

    if (!world_->GetRegistry().view<QuitGame>().empty())
    {
      isRunning_ = false;
    }
  }
}

void World::FixedUpdate(float)
{
  ticks_++;
}
