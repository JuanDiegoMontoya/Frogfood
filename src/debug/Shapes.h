#pragma once
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

namespace Debug
{
  struct Line
  {
    glm::vec3 aPosition;
    glm::vec4 aColor;
    glm::vec3 bPosition;
    glm::vec4 bColor;
  };

  struct Aabb
  {
    glm::vec3 center;
    glm::vec3 halfExtent;
    glm::vec4 color;
  };

  struct Rect
  {
    glm::vec2 minOffset;
    glm::vec2 maxOffset;
    glm::vec4 color;
    float depth;
  };
} // namespace Debug
