#pragma once
#include <cstdint>
#include <memory>

#include "entt/entity/registry.hpp"
#include "entt/entity/entity.hpp"

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
  void FixedUpdate(float dt);

  template<typename T>
  decltype(auto) CreateSingletonComponent()
  {
    assert(registry_.view<T>().size() == 0);
    auto entity = registry_.create();
    return registry_.emplace<T>(entity);
  }

  template<typename T>
  [[nodiscard]] decltype(auto) GetSingletonComponent()
  {
    auto view = registry_.view<T>();
    assert(view.size() == 1);
    auto&& [_, c] = *view.each().begin();
    return c;
  }

  entt::registry& GetRegistry()
  {
    return registry_;
  }

private:
  uint64_t ticks_ = 0;
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

  // Before FixedUpdate
  virtual void VariableUpdatePre(DeltaTime dt, World& world) = 0;

  // After FixedUpdate
  virtual void VariableUpdatePost(DeltaTime dt, World& world) = 0;
};

class NullHead final : public Head
{
public:
  void VariableUpdatePre(DeltaTime, World&) override {}
  void VariableUpdatePost(DeltaTime, World&) override {}
};

struct QuitGame {};

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
