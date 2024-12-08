#pragma once
#include <vector>

#include "glm/vec3.hpp"

struct TwoLevelGrid;

namespace Pathfinding
{
  std::vector<glm::vec3> FindPath(const TwoLevelGrid& grid, glm::ivec3 startPos, int height, glm::ivec3 goal);
}
