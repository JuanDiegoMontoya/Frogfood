#pragma once
#include "Jolt/Jolt.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Body/MotionType.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"
#include "Jolt/Physics/Collision/ObjectLayer.h"

#include "entt/fwd.hpp"

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
    JPH::Shape* shape{};
    bool activate = true;
    JPH::EMotionType motionType = JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = Layers::MOVING;
  };

  struct RigidBody
  {
    JPH::BodyID body;
  };

  RigidBody& AddRigidBody(entt::handle handle, const RigidBodySettings& settings);

  void Initialize(World& world);
  void Terminate();
  void FixedUpdate(float dt, World& world);
}
