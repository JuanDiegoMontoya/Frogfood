#pragma once
#include "Jolt/Jolt.h"
#include "Jolt/Math/Vec3.h"
#include "Jolt/Math/Vec4.h"
#include "Jolt/Math/Quat.h"
#include "Jolt/Math/Mat44.h"
#include "glm/fwd.hpp"

namespace Physics
{
  JPH::Vec3 ToJolt(glm::vec3 v);
  JPH::Vec4 ToJolt(glm::vec4 v);
  JPH::Quat ToJolt(glm::quat q);
  JPH::Mat44 ToJolt(const glm::mat4& m);

  glm::vec3 ToGlm(JPH::Vec3 v);
  glm::vec4 ToGlm(JPH::Vec4 v);
  glm::quat ToGlm(JPH::Quat q);
}
