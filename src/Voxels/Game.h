#pragma once
#include <cstdint>
#include <memory>

#include "entt/entt.hpp"

#include "ClassImplMacros.h"

struct DeltaTime
{
  float game; // Affected by game effects that scale the passage of time.
  float real; // Real time, unaffected by gameplay, inexorably marching on.
};

class World
{
public:
  NO_COPY_NO_MOVE(World);
  explicit World() = default;
  void FixedUpdate(DeltaTime dt);

private:
  uint64_t ticks = 0;
  entt::registry registry_;
};

class Networking
{
public:
  NO_COPY_NO_MOVE(Networking);
  explicit Networking() = default;
  virtual ~Networking() = default;

  virtual void SendState()    = 0;
  virtual void ReceiveState() = 0;
};

// Windowing, input polling, and rendering (if applicable)
class Head
{
public:
  NO_COPY_NO_MOVE(Head);
  explicit Head() = default;
  virtual ~Head() = default;

  virtual void VariableUpdate(DeltaTime dt, World& world) = 0;
};

class NullHead final : public Head
{
public:

  void VariableUpdate(DeltaTime, World&) override {}
};

// Game class used for client and server
class Game
{
public:
  // If a head is supplied, the game instance is not running headlessly (i.e. someone is playing)
  explicit Game(uint32_t tickHz);
  void Run();

private:
  uint32_t tickHz_{};
  bool isRunning_ = false;
  float gameDeltaTimeScale = 1;
  std::unique_ptr<Head> head_;
  std::unique_ptr<Networking> networking_;
  std::unique_ptr<World> world_;
};
