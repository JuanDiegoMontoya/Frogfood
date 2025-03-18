#pragma once
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/packing.hpp>
#include <glm/gtc/constants.hpp>

#include <cstdint>
#include <utility>

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
  
  inline glm::mat4 InfReverseZPerspectiveLH(float fovY_radians, float aspectWbyH, float zNear)
  {
    auto mat = InfReverseZPerspectiveRH(fovY_radians, aspectWbyH, zNear);
    mat[2][3] *= -1;
    return mat;
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

  struct SuffixAndDivisor
  {
    const char* suffix;
    double divisor;
  };

  inline SuffixAndDivisor BytesToSuffixAndDivisor(uint64_t bytes)
  {
    const auto* suffix = "B";
    double divisor      = 1.0;
    if (bytes > 1000)
    {
      suffix  = "KB";
      divisor = 1000;
    }
    if (bytes > 1'000'000)
    {
      suffix  = "MB";
      divisor = 1'000'000;
    }
    if (bytes > 1'000'000'000)
    {
      suffix  = "GB";
      divisor = 1'000'000'000;
    }
    return {suffix, divisor};
  }

  inline glm::vec3 RandVecInCone(glm::vec2 xi, glm::vec3 N, float angle)
  {
    float phi = 2.0f * glm::pi<float>() * xi.x;

    float theta    = sqrt(xi.y) * angle;
    float cosTheta = cos(theta);
    float sinTheta = sin(theta);

    glm::vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    glm::vec3 up        = abs(N.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    glm::vec3 tangent   = normalize(cross(up, N));
    glm::vec3 bitangent = cross(N, tangent);
    glm::mat3 tbn       = glm::mat3(tangent, bitangent, N);

    glm::vec3 sampleVec = tbn * H;
    return normalize(sampleVec);
  }

  inline float Distance2(glm::vec3 a, glm::vec3 b)
  {
    return glm::dot(a - b, a - b);
  }

  // Vector projection of a onto b
  inline glm::vec3 Project(glm::vec3 a, glm::vec3 b)
  {
    return b * glm::dot(a, b) / glm::dot(b, b);
  }

  inline float PointLineSegmentDistance(glm::vec3 p, glm::vec3 a, glm::vec3 b)
  {
    assert(glm::any(glm::greaterThan(glm::abs(b - a), glm::vec3(1e-4f))));
    glm::vec3 pa = p - a, ba = b - a;
    // Vector projection but fraction is clamped.
    float h = glm::clamp(dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
    return glm::distance(pa, ba * h);
  }

  inline glm::vec3 SphericalToCartesian(float elevation, float azimuth, float radius = 1.0f)
  {
    return {
      radius * std::sin(elevation) * std::cos(azimuth),
      radius * std::cos(elevation),
      radius * std::sin(elevation) * std::sin(azimuth)
    };
  }

  enum class Easing : uint32_t
  {
    LINEAR,
    EASE_IN_SINE,
    EASE_OUT_SINE,
    EASE_IN_OUT_BACK,
    EASE_IN_CUBIC,
    EASE_OUT_CUBIC,
  };

  float Ease(float t, Easing easing);

  float EaseInSine(float t);
  float EaseOutSine(float t);
  float EaseInOutBack(float t);
  float EaseInCubic(float t);
  float EaseOutCubic(float t);
} // namespace Math
