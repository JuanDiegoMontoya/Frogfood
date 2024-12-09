#pragma once
#include <vector>

#include "glm/vec3.hpp"

class World;

namespace Pathfinding
{
  std::vector<glm::vec3> FindPath(const World& world, glm::ivec3 startPos, int height, glm::ivec3 goal);
}
