#include "Game.h"

#include <chrono>

Game::Game(uint32_t tickHz) : tickHz_(tickHz)
{
  world_ = std::make_unique<World>();
#ifdef GAME_HEADLESS
  head_ = std::make_unique<NullHead>();
#else
  head_ = std::make_unique<NullHead>();
#endif
}

void Game::Run()
{
  isRunning_ = true;

  const auto start = std::chrono::steady_clock::now();
  const double tickLength = 1.0 / tickHz_;
  double fixedUpdateAccum = 0;

  while (isRunning_)
  {
    const auto now = std::chrono::steady_clock::now();
    const auto realDeltaTime = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() / 1'000'000.0;
    fixedUpdateAccum += realDeltaTime * gameDeltaTimeScale;

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
  }
}

void World::FixedUpdate(float)
{

  ticks++;
}
