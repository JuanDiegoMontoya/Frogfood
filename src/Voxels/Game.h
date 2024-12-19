#pragma once
#include "ClassImplMacros.h"
#include "PCG.h"
#include "TwoLevelGrid.h"

#include "entt/entity/registry.hpp"
#include "entt/entity/entity.hpp"
#include "entt/entity/handle.hpp"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <typeinfo> // TODO: remove

using namespace entt::literals;

struct Name
{
  // TODO: Should be variant<const char*, std::string> and have helper to extract C string.
  // We don't want to force an allocation for a name that's probably going to be a literal.
  std::string name;
};

struct DeltaTime
{
  float game; // Affected by game effects that scale the passage of time.
  float real; // Real time, unaffected by gameplay, inexorably marching on.
  float fraction; // For variable rate updates, fraction 
};

struct Debugging
{
  bool showDebugGui    = false;
  bool forceShowCursor = false;
};

struct LocalTransform
{
  glm::vec3 position;
  glm::quat rotation;
  float scale;
};

struct GlobalTransform
{
  glm::vec3 position;
  glm::quat rotation;
  float scale;
};

class World
{
public:
  NO_COPY_NO_MOVE(World);
  explicit World() = default;
  void FixedUpdate(float dt);

  entt::registry& GetRegistry()
  {
    return registry_;
  }

  const entt::registry& GetRegistry() const
  {
    return registry_;
  }

  PCG::Rng& Rng()
  {
    return registry_.ctx().get<PCG::Rng>();
  }

  void InitializeGameState();

  // Adds LocalTransform, GlobalTransform, InterpolatedTransform, RenderTransform, and Hierarchy components.
  entt::entity CreateRenderableEntity(glm::vec3 position, glm::quat rotation = glm::quat(1, 0, 0, 0), float scale = 1);

  [[nodiscard]] GlobalTransform* TryGetLocalPlayerTransform();

private:
  uint64_t ticks_ = 0;
  entt::registry registry_;
};

glm::vec3 GetFootPosition(entt::handle handle);
float GetHeight(entt::handle handle);

class Item
{
public:
  NO_COPY(Item);

  Item(World& world) : world(&world) {}
  virtual ~Item() = default;

  virtual const char* GetName() const = 0;

  // Create an entity
  virtual void Materialize(entt::entity parent) = 0;
  virtual void Dematerialize()                  = 0;

  // Perform an action with the entity
  virtual void UsePrimary() {}

  virtual void Update([[maybe_unused]] float dt) {}

  World* world = nullptr;

  // The materialized self. Usage: set in Materialize, nullify in Dematerialize.
  entt::entity self = entt::null;

  size_t stackSize = 1;
  size_t maxStack  = 1;
};

class Gun : public Item
{
public:
  NO_COPY(Gun);

  Gun(World& w) : Item(w) {}

  const char* GetName() const override
  {
    return "Gun";
  }

  void Materialize(entt::entity parent) override;
  void Dematerialize() override;

  void UsePrimary() override;
  void Update(float dt) override;

  float fireRateRpm = 800;
  float bullets     = 1;
  float velocity    = 300;
  float accuracyMoa = 4;
  float vrecoil     = 1.0f; // Degrees
  float vrecoilDev  = 0.25f;
  float hrecoil     = 0.0f;
  float hrecoilDev  = 0.25f;
  float accum       = 1000.0f;
  bool pressed      = false;
};

class Gun2 : public Gun
{
public:
  Gun2(World& w) : Gun(w)
  {
    fireRateRpm = 80;
    bullets     = 9;
    velocity    = 100;
    accuracyMoa = 300;
    vrecoil     = 10;
    vrecoilDev  = 3;
    hrecoil     = 1;
    hrecoilDev  = 1;
  }

  const char* GetName() const override
  {
    return "Gun2";
  }

  void Materialize(entt::entity parent) override;
};

class Pickaxe : public Item
{
public:

  Pickaxe(World& w) : Item(w) {}

  const char* GetName() const override
  {
    return "Pickaxe";
  }

  void Materialize(entt::entity parent) override;
  void Dematerialize() override;

  void UsePrimary() override;
  void Update(float dt) override;

  float useDt = 0.25f;
  float accum  = 100;
};

class Block : public Item
{
public:

  Block(World& w, TwoLevelGrid::voxel_t voxel) : Item(w), voxel(voxel)
  {
    stackSize = 1;
    maxStack = 100;
  }

  const char* GetName() const override
  {
    return "Block";
  }

  void Materialize(entt::entity parent) override;
  void Dematerialize() override;

  void UsePrimary() override;
  void Update(float dt) override;

  TwoLevelGrid::voxel_t voxel;
  float placementDt = 0.125f;
  float accum       = 100;
};

struct DroppedItem
{
  std::unique_ptr<Item> item;
};

struct Inventory
{
  static constexpr size_t height = 4;
  static constexpr size_t width  = 8;

  // Coordinate of equipped slot
  size_t activeCol = 0;
  size_t activeRow = 0;

  std::array<std::array<std::unique_ptr<Item>, width>, height> slots{};

  auto& ActiveSlot()
  {
    return slots[activeRow][activeCol];
  }

  void SetActiveSlot(size_t row, size_t col, entt::entity parent);

  bool TryStackItem(const std::type_info& type);
  std::unique_ptr<Item>* GetFirstEmptySlot();
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

inline const char* GameStateToStr(GameState g)
{
  if (g == GameState::MENU)
    return "MENU";
  if (g == GameState::GAME)
    return "GAME";
  if (g == GameState::PAUSED)
    return "PAUSED";
  return "UNKNOWN";
}

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
  float strafe      = 0;
  float forward     = 0;
  float elevate     = 0; // For flying controller
  bool jump         = false;
  bool sprint       = false;
  bool walk         = false;
  bool usePrimary   = false;
  bool useSecondary = false;
  bool interact     = false;
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
  bool inventoryIsOpen = false;
};

struct DeferredDelete {};
struct Lifetime
{
  float remainingSeconds = 0;
};

// Call to propagate local transform updates to global transform and children.
void UpdateLocalTransform(entt::handle handle);

glm::vec3 GetForward(glm::quat rotation);
glm::vec3 GetUp(glm::quat rotation);
glm::vec3 GetRight(glm::quat rotation);

void SetParent(entt::handle handle, entt::entity parent);

struct Hierarchy
{
  void AddChild(entt::entity child);
  void RemoveChild(entt::entity child);

  entt::entity parent = entt::null;
  std::vector<entt::entity> children;
};

// Use with GlobalTransform for smooth object movement
struct PreviousGlobalTransform
{
  glm::vec3 position{};
  glm::quat rotation{};
  float scale{};
};

struct Health
{
  float hp;
};

struct RenderTransform
{
  GlobalTransform transform;
};

struct NoclipCharacterController {};

struct Projectile {};

struct TimeScale
{
  float scale = 1;
};

struct TickRate
{
  uint32_t hz;
};

struct Mesh
{
  std::string name;
};

// Placed on root entity belonging to the player
struct LocalPlayer {};

struct SimpleEnemyBehavior {};
struct PathfindingEnemyBehavior {};

// Game class used for client and server
class Game
{
public:
  NO_COPY_NO_MOVE(Game);
  explicit Game(uint32_t tickHz);
  ~Game();
  void Run();

private:
  bool isRunning_ = false;
  std::unique_ptr<Head> head_;
  std::unique_ptr<Networking> networking_;
  std::unique_ptr<World> world_;
};
