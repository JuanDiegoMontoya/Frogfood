#pragma once

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
} // namespace Math