#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/packing.hpp>

#include <cstdint>

namespace Math
{
  inline glm::mat4 InfReverseZPerspectiveRH(float fovY_radians, float aspectWbyH, float zNear)
  {
    float f = 1.0f / tan(fovY_radians / 2.0f);
    return {
      f / aspectWbyH, 0.0f, 0.0f, 0.0f, 
      0.0f, -f, 0.0f, 0.0f, // Negate [1][1] to work with Vulkan
      0.0f, 0.0f, 0.0f, -1.0f, 
      0.0f, 0.0f, zNear, 0.0f
    };
  }

  inline constexpr uint32_t PreviousPower2(uint32_t x)
  {
    uint32_t v = 1;
    while ((v << 1) < x)
    {
      v <<= 1;
    }
    return v;
  }

  inline void MakeFrustumPlanes(const glm::mat4& viewProj, glm::vec4 (&planes)[6])
  {
    for (auto i = 0; i < 4; ++i)
    {
      planes[0][i] = viewProj[i][3] + viewProj[i][0];
    }
    for (auto i = 0; i < 4; ++i)
    {
      planes[1][i] = viewProj[i][3] - viewProj[i][0];
    }
    for (auto i = 0; i < 4; ++i)
    {
      planes[2][i] = viewProj[i][3] + viewProj[i][1];
    }
    for (auto i = 0; i < 4; ++i)
    {
      planes[3][i] = viewProj[i][3] - viewProj[i][1];
    }
    for (auto i = 0; i < 4; ++i)
    {
      planes[4][i] = viewProj[i][3] + viewProj[i][2];
    }
    for (auto i = 0; i < 4; ++i)
    {
      planes[5][i] = viewProj[i][3] - viewProj[i][2];
    }

    for (auto& plane : planes)
    {
      plane /= glm::length(glm::vec3(plane));
      plane.w = -plane.w;
    }
  }

  // Zero-origin unprojection. E.g., pass sampled depth, screen UV, and invViewProj to get a world-space pos
  inline glm::vec3 UnprojectUV_ZO(float depth, glm::vec2 uv, const glm::mat4& invXProj)
  {
    glm::vec4 ndc = glm::vec4(uv * 2.0f - 1.0f, depth, 1.0f);
    glm::vec4 world = invXProj * ndc;
    return glm::vec3(world) / world.w;
  }

  inline glm::vec2 SignNotZero(glm::vec2 v)
  {
    return glm::vec2((v.x >= 0.0f) ? +1.0f : -1.0f, (v.y >= 0.0f) ? +1.0f : -1.0f);
  }

  inline glm::vec2 Vec3ToOct(glm::vec3 v)
  {
    glm::vec2 p = glm::vec2{v.x, v.y} * (1.0f / (abs(v.x) + abs(v.y) + abs(v.z)));
    return (v.z <= 0.0f) ? ((1.0f - glm::abs(glm::vec2{p.y, p.x})) * SignNotZero(p)) : p;
  }

  inline glm::vec3 OctToVec3(glm::vec2 e)
  {
    using glm::vec2;
    using glm::vec3;
    vec3 v           = vec3(vec2(e.x, e.y), 1.0f - abs(e.x) - abs(e.y));
    vec2 signNotZero = vec2((v.x >= 0.0f) ? +1.0f : -1.0f, (v.y >= 0.0f) ? +1.0f : -1.0f);
    if (v.z < 0.0f) { vec2(v.x, v.y) = (1.0f - abs(vec2(v.y, v.x))) * signNotZero; }
    return normalize(v);
  }

  inline glm::vec3 OctToVec3(uint32_t snorm)
  {
    return OctToVec3(glm::unpackSnorm2x16(snorm));
  }
} // namespace Math