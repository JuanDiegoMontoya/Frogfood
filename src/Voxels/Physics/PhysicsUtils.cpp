#include "PhysicsUtils.h"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

namespace Physics
{
  JPH::Vec3 ToJolt(glm::vec3 v)
  {
    return {v.x, v.y, v.z};
  }

  JPH::Vec4 ToJolt(glm::vec4 v)
  {
    return {v.x, v.y, v.z, v.w};
  }

  JPH::Quat ToJolt(glm::quat q)
  {
    return {q.x, q.y, q.z, q.w};
  }

  glm::vec3 ToGlm(JPH::Vec3 v)
  {
    return {v.GetX(), v.GetY(), v.GetZ()};
  }

  glm::vec4 ToGlm(JPH::Vec4 v)
  {
    return {v.GetX(), v.GetY(), v.GetZ(), v.GetW()};
  }

  glm::quat ToGlm(JPH::Quat q)
  {
    return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()};
  }
}