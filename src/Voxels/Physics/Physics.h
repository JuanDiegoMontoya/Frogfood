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

#include "entt/fwd.hpp"

#include <optional>

class World;

namespace Physics
{
  namespace Layers
  {
    constexpr JPH::ObjectLayer NON_MOVING = 0;
    constexpr JPH::ObjectLayer MOVING     = 1;
    constexpr JPH::ObjectLayer NUM_LAYERS = 2;
  }; // namespace Layers

  struct RigidBodySettings
  {
    const JPH::Shape* shape{};
    bool activate = true;
    JPH::EMotionType motionType = JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = Layers::MOVING;
  };

  struct RigidBody
  {
    JPH::BodyID body;
  };

  struct CharacterControllerSettings
  {
    const JPH::Shape* shape{};
  };

  struct CharacterController
  {
    JPH::CharacterVirtual* character;
  };

  struct CharacterControllerShrimpleSettings
  {
    const JPH::Shape* shape{};
  };

  struct CharacterControllerShrimple
  {
    JPH::Character* character;
  };

  // Automatically added when one of the Add* functions is called.
  struct Shape
  {
    JPH::RefConst<JPH::Shape> shape;
  };

  RigidBody& AddRigidBody(entt::handle handle, const RigidBodySettings& settings);
  CharacterController& AddCharacterController(entt::handle handle, const CharacterControllerSettings& settings);
  CharacterControllerShrimple& AddCharacterControllerShrimple(entt::handle handle, const CharacterControllerShrimpleSettings& settings);

  struct NearestHitCollector : JPH::CastShapeCollector
  {
    void AddHit(const ResultType& inResult) override;

    std::optional<ResultType> nearest;
  };

  const JPH::NarrowPhaseQuery& GetNarrowPhaseQuery();
  const JPH::BodyInterface& GetBodyInterface();

  void Initialize(World& world);
  void Terminate();
  void FixedUpdate(float dt, World& world);
}
