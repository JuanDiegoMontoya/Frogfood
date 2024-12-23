#pragma once
#include "ClassImplMacros.h"
#include "PCG.h"
#include "TwoLevelGrid.h"
#include "Physics/Physics.h"

#include "entt/entity/registry.hpp"
#include "entt/entity/entity.hpp"
#include "entt/entity/handle.hpp"
#include "glm/vec3.hpp"
#include "glm/vec2.hpp"
#include "glm/gtc/quaternion.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

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

  void SetLocalPosition(entt::entity entity, glm::vec3 position);
  void SetLocalScale(entt::entity entity, float scale);

private:
  uint64_t ticks_ = 0;
  entt::registry registry_;
};

glm::vec3 GetFootPosition(entt::handle handle);
float GetHeight(entt::handle handle);

using ItemId              = uint32_t;
constexpr ItemId nullItem = ~0u;

struct ItemState
{
  ItemId id        = nullItem;
  float useAccum   = 1000;
  size_t stackSize = 1;
};

class ItemDefinition
{
public:
  virtual ~ItemDefinition() = default;
  
  virtual std::string GetName() const = 0;

  // Create an entity
  [[nodiscard]] virtual entt::entity Materialize(World& world) const = 0;
  virtual void Dematerialize(World& world, entt::entity self) const = 0;

  // Spawn the entity if necessary, give it physics, and unparent it from the player
  virtual Physics::RigidBody& GiveCollider(World& world, entt::entity self) const;

  // Perform an action with the entity
  virtual void UsePrimary([[maybe_unused]] float dt, World&, entt::entity, ItemState&) const {}

  [[nodiscard]] virtual float GetUseDt() const
  {
    return 0.25f;
  }

  virtual void Update(float dt, World&, ItemState& state) const
  {
    state.useAccum += dt;
  }

  [[nodiscard]] virtual size_t GetMaxStackSize() const
  {
    return 1;
  }

  [[nodiscard]] virtual glm::vec3 GetDroppedColliderSize() const
  {
    return glm::vec3{0.3f};
  }
};

class ItemRegistry
{
public:
  ItemRegistry() = default;

  // Even though this type is already noncopyable, this is required because C++ is goofy.
  // https://github.com/skypjack/entt/issues/1067
  NO_COPY(ItemRegistry);

  const ItemDefinition& Get(const std::string& name) const;
  const ItemDefinition& Get(ItemId id) const;
  ItemId GetId(const std::string& name) const;

  ItemId Add(ItemDefinition* itemDefinition);

private:
  std::unordered_map<std::string, ItemId> nameToId_;
  std::vector<std::unique_ptr<ItemDefinition>> idToDefinition_;
};

class Gun : public ItemDefinition
{
public:
  std::string GetName() const override
  {
    return "Gun";
  }

  [[nodiscard]] entt::entity Materialize(World& world) const override;
  void Dematerialize(World& world, entt::entity self) const override;

  void UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const override;

  [[nodiscard]] float GetUseDt() const override
  {
    return 1.0f / (fireRateRpm / 60.0f);
  }

  float fireRateRpm = 800;
  float bullets     = 1;
  float velocity    = 300;
  float accuracyMoa = 4;
  float vrecoil     = 1.0f; // Degrees
  float vrecoilDev  = 0.25f;
  float hrecoil     = 0.0f;
  float hrecoilDev  = 0.25f;
};

class Gun2 : public Gun
{
public:
  Gun2() : Gun()
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

  std::string GetName() const override
  {
    return "Gun2";
  }

  [[nodiscard]] entt::entity Materialize(World& world) const override;
};

class Pickaxe : public ItemDefinition
{
public:
  std::string GetName() const override
  {
    return "Pickaxe";
  }

  [[nodiscard]] entt::entity Materialize(World& world) const override;
  void Dematerialize(World& world, entt::entity self) const override;

  void UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const override;

  float useDt = 0.25f;
};

class Block : public ItemDefinition
{
public:
  Block(TwoLevelGrid::voxel_t voxel) : voxel(voxel) {}

  [[nodiscard]] size_t GetMaxStackSize() const override
  {
    return 100;
  }

  [[nodiscard]] glm::vec3 GetDroppedColliderSize() const override
  {
    return glm::vec3(0.125);
  }

  std::string GetName() const override
  {
    return "Block";
  }

  [[nodiscard]] entt::entity Materialize(World& world) const override;
  void Dematerialize(World& world, entt::entity self) const override;

  void UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const override;

  [[nodiscard]] float GetUseDt() const override
  {
    return 0.125f;
  }

  TwoLevelGrid::voxel_t voxel;
};

struct DroppedItem
{
  ItemState item;
};

struct Inventory
{
  Inventory(World& world) : world(&world) {}
  World* world{};

  static constexpr size_t height = 4;
  static constexpr size_t width  = 8;

  // (row, col) of equipped slot
  glm::ivec2 activeSlotCoord    = {0, 0};

  // The held item
  entt::entity activeSlotEntity = entt::null;

  std::array<std::array<ItemState, width>, height> slots{};

  auto& ActiveSlot()
  {
    return slots[activeSlotCoord.x][activeSlotCoord.y];
  }

  void SetActiveSlot(glm::ivec2 rowCol, entt::entity parent);
  
  void SwapSlots(glm::ivec2 first, glm::ivec2 second, entt::entity parent);

  // If necessary, materializes the item. Then, the item is given a RigidBody and is moved into the new entity.
  entt::entity DropItem(glm::ivec2 slot);

  // Completely deletes the old item, replacing it with the new. New item can be null.
  void OverwriteSlot(glm::ivec2 rowCol, ItemState itemState, entt::entity parent);

  bool TryStackItem(const ItemState& item);
  std::optional<glm::ivec2> GetFirstEmptySlot();
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

struct Crafting
{
  using Ingredient = std::variant<TwoLevelGrid::voxel_t, std::unique_ptr<ItemDefinition>>;

  struct Recipe
  {
    Ingredient output;
    std::vector<Ingredient> ingredients;
  };

  std::vector<Recipe> recipes;
};
