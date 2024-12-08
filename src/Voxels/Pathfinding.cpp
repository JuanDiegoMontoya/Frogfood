#include "Pathfinding.h"

#include "TwoLevelGrid.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/hash.hpp"

#include <queue>
#include <unordered_set>
#include <unordered_map>

namespace Pathfinding
{
  namespace
  {
    // height = number of blocks of clearance above the floor to fit through a gap
    auto GetNeighbors(const TwoLevelGrid& grid, glm::ivec3 pos, int height)
    {
      auto neighbors = std::vector<glm::ivec3>();

      // See if this node is a suitable place to stand for a character of a given height. If not, there are no neighbors.
      for (int i = 0; i < height; i++)
      {
        if (grid.GetVoxelAt(pos + glm::ivec3{0, i, 0}) != 0)
        {
          return neighbors;
        }
      }

      // Does the current node have a floor below it?
      const bool hasFloor = grid.GetVoxelAt(pos + glm::ivec3{0, -1, 0}) != 0;

      // Add all nodes directly adjacent on the walkable plane (von Neumann) if there is a surface below them.
      // We don't care if the nodes are walkable here. If they're unwalkable, they'll be discarded when GetNeighbors is invoked on them.
      if (hasFloor || grid.GetVoxelAt(pos + glm::ivec3{1, -1, 0}) != 0)
      {
        neighbors.push_back(pos + glm::ivec3{1, 0, 0});
      }
      if (hasFloor || grid.GetVoxelAt(pos + glm::ivec3{-1, -1, 0}) != 0)
      {
        neighbors.push_back(pos + glm::ivec3{-1, 0, 0});
      }
      if (hasFloor || grid.GetVoxelAt(pos + glm::ivec3{0, -1, 1}) != 0)
      {
        neighbors.push_back(pos + glm::ivec3{0, 0, 1});
      }
      if (hasFloor || grid.GetVoxelAt(pos + glm::ivec3{0, -1, -1}) != 0)
      {
        neighbors.push_back(pos + glm::ivec3{0, 0, -1});
      }

      // Add the nodes directly above and below to enable jumping and falling behavior.
      if (hasFloor)
      {
        neighbors.push_back(pos + glm::ivec3{0, 1, 0});
      }
      neighbors.push_back(pos + glm::ivec3{0,-1, 0});

      return neighbors;
    }

    auto ReconstructPath(const std::unordered_map<glm::ivec3, glm::ivec3>& cameFrom, glm::ivec3 start, glm::ivec3 goal)
    {
      assert(cameFrom.contains(goal));
      auto path = std::vector<glm::vec3>();
      auto current = goal;

      while (current != start)
      {
        path.push_back(current);
        current = cameFrom.at(current);
      }

      return std::vector(path.rbegin(), path.rend());
    }
  } // namespace

  std::vector<glm::vec3> FindPath(const TwoLevelGrid& grid, glm::ivec3 startPos, int height, glm::ivec3 goal)
  {
    auto frontier = std::queue<glm::ivec3>();
    auto cameFrom = std::unordered_map<glm::ivec3, glm::ivec3>();

    frontier.push(startPos);
    cameFrom.emplace(startPos, startPos);

    constexpr auto MAX_ITERATIONS = 1000;
    for (int i = 0; !frontier.empty() && i < MAX_ITERATIONS; i++)
    {
      auto current = frontier.front();
      frontier.pop();

      for (auto next : GetNeighbors(grid, current, height))
      {
        if (!cameFrom.contains(next))
        {
          frontier.push(next);
          cameFrom.emplace(next, current);
        }

        if (next == goal)
        {
          return ReconstructPath(cameFrom, startPos, goal);
        }
      }
    }

    // No path found.
    return {};
  }
} // namespace Pathfinding
