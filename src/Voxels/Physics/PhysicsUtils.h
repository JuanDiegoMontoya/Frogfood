#pragma once
#include "Jolt/Jolt.h"
#include "Jolt/Math/Vec3.h"
#include "glm/fwd.hpp"

namespace Physics
{
  JPH::Vec3 ToJolt(glm::vec3 v);

  JPH::Quat ToJolt(glm::quat q);

  glm::vec3 ToGlm(JPH::Vec3 v);

  glm::quat ToGlm(JPH::Quat q);
}
