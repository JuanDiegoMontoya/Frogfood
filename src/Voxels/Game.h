#pragma once
#include "ClassImplMacros.h"

#include "entt/entity/registry.hpp"
#include "entt/entity/entity.hpp"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

struct DeltaTime
{
  float game; // Affected by game effects that scale the passage of time.
  float real; // Real time, unaffected by gameplay, inexorably marching on.
};

struct Singleton {};

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
    registry_.emplace<Singleton>(entity);
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

  void InitializeGameState();

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

// Return to desktop
struct CloseApplication {};

// Close server (if applicable), then return to main menu if head, or close app if headless
struct ReturnToMenu {};

enum class GameState
{
  MENU,
  GAME,
  PAUSED,
};

enum class InputAxis
{
  STRAFE,
  FORWARD,
  SPRINT,
  WALK,
};

// Networked (client -> server)
// When a server receives these, they attach it to the corresponding player.
struct InputState
{
  float strafe  = 0;
  float forward = 0;
  float elevate = 0; // For flying controller
  bool sprint   = false;
  bool walk     = false;
};

// Networked (client -> server)
struct InputLookState
{
  float pitch = 0;
  float yaw   = 0;
};

struct Player
{
  uint32_t id = 0;
};

struct Transform
{
  glm::vec3 position;
  float scale;
  glm::quat rotation;
};

struct NoclipCharacterController {};

// Game class used for client and server
class Game
{
public:
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
