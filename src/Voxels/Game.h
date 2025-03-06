#pragma once
#include "ClassImplMacros.h"
#include "PCG.h"
#include "TwoLevelGrid.h"
#include "Physics/Physics.h"
#include "Fvog/detail/Flags.h"
#include "shaders/Light.h.glsl" // "TEMP"

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
#include <functional>

struct ItemState;
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
  bool showDebugGui        = false;
  bool forceShowCursor     = false;
  bool drawDebugProbe      = false;
  bool drawPhysicsShapes   = false;
  bool drawPhysicsVelocity = false;
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

struct BlockEntity {};

// Similar to noclip character controller, but has inertia.
struct FlyingCharacterController
{
  float maxSpeed;
  float acceleration;
};

struct Hierarchy
{
  void AddChild(entt::entity child);
  void RemoveChild(entt::entity child);

  entt::entity parent = entt::null;
  std::vector<entt::entity> children;

  bool useLocalPositionAsGlobal = false;
  bool useLocalRotationAsGlobal = false;
};

enum class TeamFlagBits
{
  NEUTRAL  = 0,
  FRIENDLY = 1 << 0,
  ENEMY    = 1 << 1,
  // TODO: mask and flags for PvP
};

FVOG_DECLARE_FLAG_TYPE(TeamFlags, TeamFlagBits, uint32_t);

using ItemId              = uint32_t;
constexpr ItemId nullItem = ~0u;

struct ItemState
{
  ItemId id      = nullItem;
  int count      = 1;
  float useAccum = 1000;
};

using BlockId               = TwoLevelGrid::voxel_t;
constexpr BlockId nullBlock = ~0u;

using EntityPrefabId = uint32_t;

enum class BlockDamageFlagBit
{
  NONE    = 0,
  PICKAXE = 1 << 0,
  AXE     = 1 << 1,
  ALL_TOOLS = PICKAXE | AXE,
  NO_LOOT = 1 << 2,
  NO_LOOT_95_PERCENT = 1 << 3,
};

FVOG_DECLARE_FLAG_TYPE(BlockDamageFlags, BlockDamageFlagBit, uint32_t);

struct VoxelMaterialDesc
{
  bool isInvisible               = false;
  bool randomizeTexcoordRotation = false;
  std::optional<std::string> baseColorTexture;
  glm::vec3 baseColorFactor = {1, 1, 1};
  std::optional<std::string> emissionTexture;
  glm::vec3 emissionFactor = {0, 0, 0};
};

struct DropSelf {};

class BlockDefinition
{
public:
  struct CreateInfo
  {
    std::string name;
    float initialHealth = 100;
    int damageTier{};
    BlockDamageFlags damageFlags = BlockDamageFlagBit::ALL_TOOLS;
    std::variant<std::monostate, DropSelf, ItemState, std::string> lootDrop = DropSelf{};
    // Giving every block a unique set of materials isn't ideal, but it will suffice in the short run.
    VoxelMaterialDesc voxelMaterialDesc;
  };

  explicit BlockDefinition(const CreateInfo& info);
  virtual ~BlockDefinition() = default;
  
  NO_COPY_NO_MOVE(BlockDefinition);

  // Weakly attempt to place the block at the given position.
  // Returns whether the attempt succeeded (could fail due to
  // insufficient space, e.g. for multiblock structures or if
  // there's an entity in the way.
  virtual bool OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition) const;

  virtual void OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const;

  // What the block drops when it is destroyed. Options: nothing, an item,
  // or pick a loot drop from a string. By default, a block will drop the item associated
  // with itself.
  virtual std::variant<std::monostate, ItemState, std::string> GetLootDropType() const
  {
    if (std::get_if<DropSelf>(&createInfo_.lootDrop))
    {
      return ItemState{.id = itemId_};
    }
    if (auto* is = std::get_if<ItemState>(&createInfo_.lootDrop))
    {
      return *is;
    }
    if (auto* s = std::get_if<std::string>(&createInfo_.lootDrop))
    {
      return *s;
    }
    return std::monostate{};
  }

  [[nodiscard]] std::string GetName() const
  {
    return createInfo_.name;
  }

  [[nodiscard]] VoxelMaterialDesc GetMaterialDesc() const
  {
    return createInfo_.voxelMaterialDesc;
  }

  [[nodiscard]] float GetInitialHealth() const
  {
    return createInfo_.initialHealth;
  }

  [[nodiscard]] int GetDamageTier() const
  {
    return createInfo_.damageTier;
  }

  [[nodiscard]] BlockDamageFlags GetDamageFlags() const
  {
    return createInfo_.damageFlags;
  }

  [[nodiscard]] ItemId GetItemId() const
  {
    return itemId_;
  }

  [[nodiscard]] BlockId GetBlockId() const
  {
    return blockId_;
  }

protected:
  CreateInfo createInfo_;

  // Item and block IDs are set when added to registry
  friend class BlockRegistry;
  ItemId itemId_{};
  BlockId blockId_{};
};

class ExplodeyBlockDefinition : public BlockDefinition
{
public:
  struct ExplodeyCreateInfo
  {
    float radius{};
    float damage{};
    int damageTier{};
    float pushForce{};
    BlockDamageFlags damageFlags{};
  };

  explicit ExplodeyBlockDefinition(const CreateInfo& info, const ExplodeyCreateInfo& explodey)
    : BlockDefinition(info), explodeyInfo_(explodey) {}
  void OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const override;
  std::variant<std::monostate, ItemState, std::string> GetLootDropType() const override
  {
    return std::monostate{};
  }

private:
  ExplodeyCreateInfo explodeyInfo_;
};

class BlockEntityDefinition : public BlockDefinition
{
public:

  struct BlockEntityCreateInfo
  {
    EntityPrefabId id;
  };

  explicit BlockEntityDefinition(const CreateInfo& info, const BlockEntityCreateInfo& blockEntityInfo)
    : BlockDefinition(info), blockEntityInfo_(blockEntityInfo) {}

  bool OnTryPlaceBlock(World& world, glm::ivec3 voxelPosition) const override;
  void OnDestroyBlock(World& world, glm::ivec3 voxelPosition) const override;

private:
  BlockEntityCreateInfo blockEntityInfo_;
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

  void GenerateMap();

  // Adds LocalTransform, GlobalTransform, InterpolatedTransform, RenderTransform, and Hierarchy components.
  entt::entity CreateRenderableEntityNoHashGrid(glm::vec3 position, glm::quat rotation = glm::quat(1, 0, 0, 0), float scale = 1);
  entt::entity CreateRenderableEntity(glm::vec3 position, glm::quat rotation = glm::quat(1, 0, 0, 0), float scale = 1);
  entt::entity CreateDroppedItem(ItemState item, glm::vec3 position, glm::quat rotation = {1, 0, 0, 0}, float scale = 1);

  [[nodiscard]] GlobalTransform* TryGetLocalPlayerTransform();
  
  void SetLocalScale(entt::entity entity, float scale);
  [[nodiscard]] entt::entity GetChildNamed(entt::entity entity, std::string_view name) const;

  // Look for ancestor with LinearVelocity and return its value. Otherwise, return 0.
  [[nodiscard]] glm::vec3 GetInheritedLinearVelocity(entt::entity entity);

  // Travels up hierarchy, searching for TeamFlags component.
  [[nodiscard]] const TeamFlags* GetTeamFlags(entt::entity entity) const;

  template<typename T>
  [[nodiscard]] std::pair<entt::entity, T*> GetComponentFromAncestor(entt::entity entity)
  {
    assert(registry_.valid(entity));
    if (auto* component = registry_.try_get<T>(entity))
    {
      return {entity, component};
    }
    if (auto* h = registry_.try_get<Hierarchy>(entity); h && h->parent != entt::null)
    {
      return GetComponentFromAncestor<T>(h->parent);
    }
    return {entt::null, nullptr};
  }

  template<typename T>
  [[nodiscard]] std::pair<entt::entity, T*> GetComponentFromDescendant(entt::entity entity)
  {
    assert(registry_.valid(entity));
    if (auto* component = registry_.try_get<T>(entity))
    {
      return {entity, component};
    }
    if (auto* h = registry_.try_get<Hierarchy>(entity))
    {
      for (auto child : h->children)
      {
        if (auto pair = GetComponentFromDescendant<T>(child); pair.first != entt::null)
        {
          return pair;
        }
      }
    }
    return {entt::null, nullptr};
  }

  template<typename T>
  [[nodiscard]] std::pair<entt::entity, T*> GetComponentFromAncestorOrDescendant(entt::entity entity)
  {
    if (auto pair = GetComponentFromAncestor<T>(entity); pair.first != entt::null)
    {
      return pair;
    }
    return GetComponentFromDescendant<T>(entity);
  }

  Physics::CharacterController& GivePlayerCharacterController(entt::entity playerEntity);
  Physics::CharacterControllerShrimple& GivePlayerCharacterControllerShrimple(entt::entity playerEntity);
  FlyingCharacterController& GivePlayerFlyingCharacterController(entt::entity playerEntity);

  void GivePlayerColliders(entt::entity playerEntity);

  // Remove character controller and collision, and give ghost component
  void KillPlayer(entt::entity playerEntity);

  // Restore character controller and collision, and remove ghost component
  void RespawnPlayer(entt::entity playerEntity);

  // Apply damage and resistances to an entity. Does not apply knockback.
  // Returns the amount of damage actually applied.
  float DamageEntity(entt::entity entity, float damage);

  [[nodiscard]] bool CanEntityDamageEntity(entt::entity entitySource, entt::entity entityTarget) const;

  [[nodiscard]] bool AreEntitiesEnemies(entt::entity entity1, entt::entity entity2) const;

  [[nodiscard]] std::vector<entt::entity> GetEntitiesInSphere(glm::vec3 center, float radius) const;
  [[nodiscard]] std::vector<entt::entity> GetEntitiesInCapsule(glm::vec3 start, glm::vec3 end, float radius);

  [[nodiscard]] entt::entity GetNearestPlayer(glm::vec3 position);

  // Returns the amount of damage successfully inflicted.
  float DamageBlock(glm::ivec3 voxelPos, float damage, int damageTier, BlockDamageFlags damageType);

  const BlockDefinition& GetBlockDefinitionFromItem(ItemId item);
  ItemId GetItemIdFromBlock(BlockId block);

  uint64_t GetTicks() const
  {
    return ticks_;
  }

private:
  uint64_t ticks_ = 0;
  entt::registry registry_;
};

class EntityPrefabDefinition
{
public:
  struct CreateInfo
  {
    float spawnChance      = 0;
    float minSpawnDistance = 30;
    float maxSpawnDistance = 90;
    bool canSpawnFloating  = false;
  };

  explicit EntityPrefabDefinition(const CreateInfo& createInfo = {}) : info_(createInfo) {}
  DEFAULT_MOVE(EntityPrefabDefinition);
  NO_COPY(EntityPrefabDefinition);

  virtual ~EntityPrefabDefinition() = default;

  virtual entt::entity Spawn(World& world, glm::vec3 position, glm::quat rotation = glm::identity<glm::quat>()) const = 0;

  [[nodiscard]] const CreateInfo& GetCreateInfo() const
  {
    return info_;
  }

protected:
  CreateInfo info_;
};

class EntityPrefabRegistry
{
public:
  EntityPrefabRegistry() = default;

  ~EntityPrefabRegistry() = default;

  NO_COPY(EntityPrefabRegistry);
  DEFAULT_MOVE(EntityPrefabRegistry);

  [[nodiscard]] const EntityPrefabDefinition& Get(const std::string& name) const
  {
    return *idToDefinition_.at(nameToId_.at(name));
  }
  [[nodiscard]] const EntityPrefabDefinition& Get(EntityPrefabId id) const
  {
    return *idToDefinition_.at(id);
  }
  [[nodiscard]] EntityPrefabId GetId(const std::string& name) const;

  EntityPrefabId Add(const std::string& name, EntityPrefabDefinition* entityPrefabDefinition)
  {
    const auto myId = static_cast<uint32_t>(idToDefinition_.size());
    nameToId_.emplace(name, myId);
    idToDefinition_.emplace_back(entityPrefabDefinition);
    return myId;
  }

  std::span<const std::unique_ptr<EntityPrefabDefinition>> GetAllPrefabs() const
  {
    return std::span(idToDefinition_);
  }

private:
  std::unordered_map<std::string, EntityPrefabId> nameToId_;
  std::vector<std::unique_ptr<EntityPrefabDefinition>> idToDefinition_;
};

class BlockRegistry
{
public:
  BlockRegistry(World& world) : world_(&world)
  {
    // Hardcode air as the first block.
    Add(new BlockDefinition({.name = "air", .voxelMaterialDesc = {.isInvisible = true}}));
  }

  ~BlockRegistry() = default;

  NO_COPY(BlockRegistry);
  DEFAULT_MOVE(BlockRegistry);

  [[nodiscard]] const BlockDefinition& Get(const std::string& name) const;
  [[nodiscard]] const BlockDefinition& Get(BlockId id) const;
  [[nodiscard]] BlockId GetId(const std::string& name) const;

  BlockId Add(BlockDefinition* blockDefinition);

  std::span<const std::unique_ptr<BlockDefinition>> GetAllDefinitions() const
  {
    return std::span(idToDefinition_);
  }

private:
  World* world_;
  std::unordered_map<std::string, BlockId> nameToId_;
  std::vector<std::unique_ptr<BlockDefinition>> idToDefinition_;
};

glm::vec3 GetFootPosition(entt::handle handle);
float GetHeight(entt::handle handle);

class ItemDefinition
{
public:
  ItemDefinition(std::string_view name) : name_(name) {}
  virtual ~ItemDefinition() = default;
  
  virtual std::string GetName() const
  {
    return name_;
  }

  // Create an entity
  [[nodiscard]] virtual entt::entity Materialize(World&) const = 0;

  virtual void Dematerialize(World& world, entt::entity self) const;

  // Spawn the entity if necessary, give it physics, and unparent it from the player
  virtual Physics::RigidBody& GiveCollider(World& world, entt::entity self) const;

  // Perform an action with the entity
  virtual void UsePrimary([[maybe_unused]] float dt, [[maybe_unused]] World& world, [[maybe_unused]] entt::entity self, [[maybe_unused]] ItemState& state) const
  {
  }

  [[nodiscard]] virtual float GetUseDt() const
  {
    return 0.25f;
  }

  virtual void Update(float dt, World&, [[maybe_unused]] entt::entity self, ItemState& state) const
  {
    state.useAccum += dt;
  }

  [[nodiscard]] virtual int GetMaxStackSize() const
  {
    return 1;
  }

  [[nodiscard]] virtual glm::vec3 GetDroppedColliderSize() const
  {
    return glm::vec3{0.25f};
  }

protected:
  std::string name_;
};

class ItemRegistry
{
public:
  ItemRegistry() = default;

  // Even though this type is already noncopyable, this is required because C++ is goofy.
  // https://github.com/skypjack/entt/issues/1067
  NO_COPY(ItemRegistry);

  ItemRegistry(ItemRegistry&&) noexcept = default;
  ItemRegistry& operator=(ItemRegistry&&) noexcept = default;

  const ItemDefinition& Get(const std::string& name) const;
  const ItemDefinition& Get(ItemId id) const;
  ItemId GetId(const std::string& name) const;

  ItemId Add(ItemDefinition* itemDefinition);

  std::span<const std::unique_ptr<ItemDefinition>> GetAllItemDefinitions() const
  {
    return idToDefinition_;
  }

private:
  std::unordered_map<std::string, ItemId> nameToId_;
  std::vector<std::unique_ptr<ItemDefinition>> idToDefinition_;
};

class SpriteItem : public ItemDefinition
{
public:
  SpriteItem(std::string_view name, std::string_view sprite, glm::vec3 tint = glm::vec3(1))
    : ItemDefinition(name),
      sprite_(sprite),
      tint_(tint) {}

  entt::entity Materialize(World&) const override;

  glm::vec3 GetDroppedColliderSize() const override
  {
    return glm::vec3(0.125f);
  }

  int GetMaxStackSize() const override
  {
    return 100;
  }

protected:
  std::string sprite_;
  glm::vec3 tint_;
};

class Gun : public ItemDefinition
{
public:
  struct CreateInfo
  {
    std::string model = "ar15";
    glm::vec3 tint    = {1, 1, 1};
    float scale       = 1;
    float damage      = 20;
    float knockback   = 3;
    float fireRateRpm = 800;
    float bullets     = 1;
    float velocity    = 300;
    float accuracyMoa = 4;
    float vrecoil     = 1.0f; // Degrees
    float vrecoilDev  = 0.25f;
    float hrecoil     = 0.0f;
    float hrecoilDev  = 0.25f;
    std::optional<GpuLight> light;
  };

  explicit Gun(std::string_view name, const CreateInfo& createInfo) : ItemDefinition(name), createInfo_(createInfo) {}

  [[nodiscard]] entt::entity Materialize(World& world) const override;

  void UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const override;

  [[nodiscard]] float GetUseDt() const override
  {
    return 1.0f / (createInfo_.fireRateRpm / 60.0f);
  }

private:
  CreateInfo createInfo_;
};

class ToolDefinition : public ItemDefinition
{
public:
  struct CreateInfo
  {
    std::optional<std::string> meshName;
    glm::vec3 meshTint;
    float blockDamage;
    int blockDamageTier;
    BlockDamageFlags blockDamageFlags;
    float useDt = 0.25f;
  };

  ToolDefinition(std::string_view name, const CreateInfo& createInfo)
    : ItemDefinition(name),
      createInfo_(createInfo)
  {
  }

  [[nodiscard]] entt::entity Materialize(World& world) const override;

  void UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const override;

  float GetUseDt() const override
  {
    return createInfo_.useDt;
  }

protected:
  CreateInfo createInfo_;
};

class RainbowTool : public ToolDefinition
{
public:
  using ToolDefinition::ToolDefinition;

  void Update(float dt, World& world, entt::entity self, ItemState& state) const override;
};

class Block : public ItemDefinition
{
public:
  Block(BlockId voxel, std::string_view name) : ItemDefinition(name), voxel(voxel) {}

  [[nodiscard]] int GetMaxStackSize() const override
  {
    return 100;
  }

  [[nodiscard]] glm::vec3 GetDroppedColliderSize() const override
  {
    return glm::vec3(0.125);
  }

  std::string GetName() const override
  {
    return name_;
  }

  [[nodiscard]] entt::entity Materialize(World& world) const override;

  void UsePrimary(float dt, World& world, entt::entity self, ItemState& state) const override;

  [[nodiscard]] float GetUseDt() const override
  {
    return 0.125f;
  }

  TwoLevelGrid::voxel_t voxel;
};

class Spear : public ItemDefinition
{
public:
  using ItemDefinition::ItemDefinition;

  void UsePrimary(float dt, World&, entt::entity, ItemState&) const override;

  [[nodiscard]] entt::entity Materialize(World& world) const override;

  float GetUseDt() const override
  {
    return 0.35f;
  }
};

struct ItemIdAndCount
{
  ItemId item = nullItem;
  int count   = 1;
};

// Loot type for simple independent random drops.
struct RandomLootDrop
{
  // Use individual probabilities for spawning each of count items.
  [[nodiscard]] std::vector<ItemIdAndCount> Sample(PCG::Rng& rng) const;

  ItemId item = nullItem;
  int count = 1;
  float chanceForOne = 1;
  // TODO: distribution type (normal, uniform)
};

// Loot type that selects a single element from a pool of potential drops.
// Intended for allowing bosses to drop exactly one item or set of items.
struct PoolLootDrop
{
  [[nodiscard]] std::vector<ItemIdAndCount> Sample(PCG::Rng& rng) const;

  [[nodiscard]] int GetTotalWeight() const;

  struct ItemsAndWeight
  {
    std::vector<ItemIdAndCount> items; // The items given if selected.
    int weight = 1;                    // Chance to select this item from the pool.
  };

  // Probability that the pool will be sampled.
  float chance = 1;
  std::vector<ItemsAndWeight> pool;
};

// What a mob can drop when it dies.
struct LootDrops
{
  [[nodiscard]] std::vector<ItemIdAndCount> Collect(PCG::Rng& rng) const;

  std::vector<std::variant<RandomLootDrop, PoolLootDrop>> drops;
};

class LootRegistry
{
public:
  LootRegistry() = default;
  NO_COPY(LootRegistry);

  LootRegistry(LootRegistry&&) noexcept = default;
  LootRegistry& operator=(LootRegistry&&) noexcept = default;

  void Add(std::string name, std::unique_ptr<LootDrops>&& lootDrops);
  [[nodiscard]] const LootDrops* Get(const std::string& name);

private:
  std::unordered_map<std::string, std::unique_ptr<LootDrops>> nameToLoot_;
};

struct DroppedItem
{
  ItemState item;
};

// Used to look up what a mob drops when it dies.
struct Loot
{
  std::string name;
};

struct Crafting
{
  struct Recipe
  {
    std::vector<ItemIdAndCount> ingredients;
    std::vector<ItemIdAndCount> output;
    BlockId craftingStation = 0;
  };

  std::vector<Recipe> recipes;
};

struct Inventory
{
  Inventory(World& world) : world(&world) {}
  World* world{};

  static constexpr size_t height = 4;
  static constexpr size_t width  = 8;

  // (row, col) of equipped slot
  glm::ivec2 activeSlotCoord    = {0, 0};

  bool canHaveActiveItem = true;

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
  void OverwriteSlot(glm::ivec2 rowCol, ItemState itemState, entt::entity parent = entt::null);

  void TryStackItem(ItemState& item);
  std::optional<glm::ivec2> GetFirstEmptySlot() const;

  bool CanCraftRecipe(Crafting::Recipe recipe) const;
  void CraftRecipe(Crafting::Recipe recipe, entt::entity parent);
};

// If parent1 and parent2 both have an inventory, swaps items between them.
bool SwapInventorySlots(World& world, entt::entity parent1, glm::ivec2 parent1Slot, entt::entity parent2, glm::ivec2 parent2Slot);

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

  virtual void CreateRenderingMaterials(std::span<const std::unique_ptr<BlockDefinition>>) {}
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
  LOADING,
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
  entt::entity openContainerId = entt::null;
  bool showInteractPrompt = false; // TODO: Move to LocalPlayer since this info is purely visual.
};

// Tag for systems to exclude.
// Intended for preventing the player from influencing the world while dead.
struct GhostPlayer
{
  float remainingSeconds{};
};

struct Invulnerability
{
  float remainingSeconds{};
};

// Map of entities that cannot be damaged by this one to remaining time.
struct CannotDamageEntities
{
  std::unordered_map<entt::entity, float> entities;
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

// Use with GlobalTransform and RenderTransform for smooth object movement.
struct PreviousGlobalTransform
{
  glm::vec3 position{};
  glm::quat rotation{};
  float scale{};
};

struct Health
{
  float hp = -1;
  float maxHp = -1;
};

struct RenderTransform
{
  GlobalTransform transform;
};

struct NoclipCharacterController {};

struct Projectile
{
  float initialSpeed{}; // Used to calculate damage.
  float drag = 0; // TODO: remove (use Friction)
  float restitution = 0.25f;
};

struct LinearVelocity
{
  glm::vec3 v{};

  operator glm::vec3&()
  {
    return v;
  }
  operator const glm::vec3&() const
  {
    return v;
  }
};

// Velocity attenuation.
struct Friction
{
  glm::vec3 axes; // Amount to apply to each axis
};

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

struct Tint
{
  glm::vec3 color = {1, 1, 1};
};

struct Billboard
{
  std::string name;
};

// Use when you want a child entity's collide events to be counted as the parent's.
struct ForwardCollisionsToParent {};

// For entities that deal damage to other entities they collide with.
struct ContactDamage
{
  float damage    = 0;
  float knockback = 5;
};

// Linearly interpolate between samples.
struct LinearPath
{
  // Defines a transform offset and duration.
  // Postfix sum of "offsetSeconds" defines timestamp on which the sample appears.
  // First keyframe is blended with identity transform if its position is not at 0.
  struct KeyFrame
  {
    glm::vec3 position = {};
    glm::quat rotation = glm::identity<glm::quat>();
    float scale = 1;
    float offsetSeconds;
  };
  std::vector<KeyFrame> frames;

  float secondsElapsed = 0;

  // Preserve local transform before this component was added.
  LocalTransform originalLocalTransform;
};

struct BlockHealth
{
  float health = 100;
};

// Placed on root entity belonging to this client's player.
struct LocalPlayer {};

struct SimpleEnemyBehavior {};
struct SimplePathfindingEnemyBehavior {};

struct AiWanderBehavior
{
  float minWanderDistance  = 3;
  float maxWanderDistance  = 6;
  float timeBetweenMoves   = 8;
  float accumulator        = 0;
  bool targetCanBeFloating = false;
};

struct AiTarget
{
  entt::entity currentTarget = entt::null;
};

struct AiVision
{
  // Spherical cone in which an AI actor can detect another entity.
  float coneAngleRad = glm::half_pi<float>();
  float distance     = 20;
  float invAcuity    = 1; // Time taken, at max distance, for target in cone to be spotted.
  float accumulator  = 0;
};

struct AiHearing
{
  // Absolute distance in which an AI actor can automatically detect another entity.
  float distance = 5;
};

struct PredatoryBirdBehavior
{
  enum class State
  {
    IDLE,
    CIRCLING,
    SWOOPING,
  };
  State state = State::IDLE;
  float accum = 0;
  entt::entity target = entt::null;
  glm::vec3 idlePosition{};
  float lineOfSightDuration = 0;
};

struct WormEnemyBehavior
{
  float maxTurnSpeedDegPerSec = 180;
};

struct WalkingMovementAttributes
{
  float runBaseSpeed = 5;
  float walkModifier = 0.35f;
  float runMaxSpeed  = 5;
};

struct KnockbackMultiplier
{
  float factor = 1;
};

// So recently-dropped items do not get magnetized to players.
struct CannotBePickedUp
{
  float remainingSeconds = 0.5f;
};

struct NoHashGrid {};

struct DespawnWhenFarFromPlayer
{
  float maxDistance = 60;
  float gracePeriod = 10;
};

class NpcSpawnDirector
{
public:
  explicit NpcSpawnDirector(World& world) : world_(&world) {}

  void Update(float dt);

private:
  World* world_;
  float accumulator = 0;
  float timeBetweenSpawns = 1;
};

struct Enemy {};

// This component exists solely to check if a physics ray hit the voxel world.
struct Voxels {};

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
