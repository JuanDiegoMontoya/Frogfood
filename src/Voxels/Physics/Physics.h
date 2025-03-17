#pragma once
#include "Jolt/Jolt.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Body/MotionType.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"
#include "Jolt/Physics/Collision/ObjectLayer.h"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#include "Jolt/Physics/Character/Character.h"
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Collision/ShapeCast.h"
#include "Jolt/Physics/Collision/CastResult.h"
#include "Jolt/Physics/Collision/CollisionCollector.h"
#include "Jolt/Physics/Constraints/Constraint.h"

#include "entt/fwd.hpp"

#include <optional>
#include <vector>
#include <variant>

class World;
struct TwoLevelGrid;

namespace Physics
{
  namespace Layers
  {
    constexpr JPH::ObjectLayer WORLD            = 0;
    constexpr JPH::ObjectLayer CHARACTER        = 1;
    constexpr JPH::ObjectLayer PROJECTILE       = 3;
    constexpr JPH::ObjectLayer DROPPED_ITEM     = 4;
    constexpr JPH::ObjectLayer DEBRIS           = 5;
    constexpr JPH::ObjectLayer HITBOX           = 6;
    // For damage-dealing colliders
    constexpr JPH::ObjectLayer HURTBOX          = 7;
    constexpr JPH::ObjectLayer HITBOX_AND_HURTBOX = 8;
    constexpr JPH::ObjectLayer NUM_LAYERS       = 9;

    // Cast-only layers
    constexpr JPH::ObjectLayer CAST_WORLD       = 10;
    constexpr JPH::ObjectLayer CAST_PROJECTILE  = 11;
    constexpr JPH::ObjectLayer CAST_CHARACTER   = 12;
  }

  struct Sphere
  {
    bool operator==(const Sphere&) const = default;
    float radius{};
  };

  struct Capsule
  {
    bool operator==(const Capsule&) const = default;
    float radius{};
    float cylinderHalfHeight{};
  };

  struct Box
  {
    bool operator==(const Box&) const = default;
    glm::vec3 halfExtent{};
  };

  struct Plane
  {
    bool operator==(const Plane&) const = default;
    glm::vec3 normal{};
    float constant{};
  };

  struct UseTwoLevelGrid
  {
    bool operator==(const UseTwoLevelGrid&) const = default;
  };

  using PolyShape = std::variant<std::monostate, Sphere, Capsule, Box, Plane, UseTwoLevelGrid>;

  struct ShapeSettings
  {
    PolyShape shape;
    float density         = 1000; // kg/m^3
    glm::vec3 translation = {0, 0, 0};
    glm::quat rotation    = glm::identity<glm::quat>();
  };

  struct RigidBodySettings
  {
    ShapeSettings shape{};
    bool activate = true;
    bool isSensor = false;
    float gravityFactor = 1;
    JPH::EMotionType motionType = JPH::EMotionType::Dynamic;
    JPH::EMotionQuality motionQuality  = JPH::EMotionQuality::Discrete;
    JPH::ObjectLayer layer = Layers::DEBRIS;
    JPH::EAllowedDOFs degreesOfFreedom = JPH::EAllowedDOFs::All;
  };

  struct RigidBody
  {
    JPH::BodyID body;
  };

  struct CharacterControllerSettings
  {
    ShapeSettings shape;
  };

  struct CharacterController
  {
    JPH::CharacterVirtual* character;
    JPH::CharacterBase::EGroundState previousGroundState;
  };

  struct CharacterControllerShrimpleSettings
  {
    ShapeSettings shape;
  };

  struct CharacterControllerShrimple
  {
    JPH::Character* character;
    JPH::CharacterBase::EGroundState previousGroundState;
  };

  // Automatically added when one of the Add* functions is called.
  struct Shape
  {
    JPH::RefConst<JPH::Shape> shape;
  };
  
  void RegisterConstraint(JPH::Ref<JPH::Constraint> constraint, JPH::BodyID body1, JPH::BodyID body2);
  [[nodiscard]] std::unique_ptr<JPH::IgnoreMultipleBodiesFilter> GetIgnoreEntityAndChildrenFilter(entt::handle handle);

  const JPH::NarrowPhaseQuery& GetNarrowPhaseQuery();
  JPH::BodyInterface& GetBodyInterface();
  JPH::PhysicsSystem& GetPhysicsSystem();

  struct ContactAddedPair
  {
    entt::entity entity1;
    entt::entity entity2;
  };

  struct ContactPersistedPair
  {
    entt::entity entity1;
    entt::entity entity2;
  };

  entt::dispatcher& GetDispatcher();

  void Initialize(World& world);
  void Terminate();
  void FixedUpdate(float dt, World& world);

  struct NearestHitCollector : JPH::CastShapeCollector
  {
    void AddHit(const ResultType& inResult) override;

    std::optional<ResultType> nearest;
  };

  struct NearestRayCollector : JPH::CastRayCollector
  {
    void AddHit(const ResultType& inResult) override;

    std::optional<ResultType> nearest;
  };

  struct AllRayCollector : JPH::CastRayCollector
  {
    void AddHit(const ResultType& inResult) override;

    std::vector<ResultType> unorderedHits;
  };

  void CreateObservers(entt::registry& registry);
}
