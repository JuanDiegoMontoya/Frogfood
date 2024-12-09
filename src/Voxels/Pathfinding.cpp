#include "Pathfinding.h"

#include "TwoLevelGrid.h"

#ifndef GAME_HEADLESS
#include "debug/Shapes.h"
#endif

#define GLM_ENABLE_EXPERIMENTAL
#include "Game.h"
#include "glm/gtx/hash.hpp"
#include "glm/gtx/component_wise.hpp"
#include "glm/gtc/epsilon.hpp"

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

    // The actual cost to move from pos0 to pos1 (they must be adjacent)
    float DetermineCost([[maybe_unused]] const TwoLevelGrid& grid, [[maybe_unused]] glm::ivec3 pos0, [[maybe_unused]] glm::ivec3 pos1)
    {
      return 1;
    }

    // The approximate cost to move from pos0 to pos1. If it exceeds the actual cost, imperfect paths will be generated.
    float HeuristicCost([[maybe_unused]] const TwoLevelGrid& grid, [[maybe_unused]] glm::ivec3 pos0, [[maybe_unused]] glm::ivec3 pos1)
    {
      return (float)glm::compAdd(glm::abs(pos0 - pos1)); // Manhattan
    }
  } // namespace

  std::vector<glm::vec3> FindPath(const World& world, glm::ivec3 startPos, int height, glm::ivec3 goal)
  {
    struct FNode
    {
      glm::ivec3 pos;
      float priority;
    };

    const auto cmp = [&](FNode left, FNode right)
    {
      // Euclidean distance tiebreaker leads to significantly better paths.
      if (glm::epsilonEqual(left.priority, right.priority, 1e-3f))
      {
        return glm::distance(glm::vec3(left.pos), glm::vec3(goal)) > glm::distance(glm::vec3(right.pos), glm::vec3(goal));
      }
      return left.priority > right.priority;
    };
    auto frontier  = std::priority_queue<FNode, std::vector<FNode>, decltype(cmp)>(cmp);
    auto cameFrom  = std::unordered_map<glm::ivec3, glm::ivec3>();
    auto costSoFar = std::unordered_map<glm::ivec3, float>();
    
    frontier.emplace(startPos, 0.0f);
    costSoFar.emplace(startPos, 0.0f);
    cameFrom.emplace(startPos, startPos);

    constexpr auto MAX_ITERATIONS = 1000;
    const auto& grid              = world.GetRegistry().ctx().get<TwoLevelGrid>();
    for (int i = 0; !frontier.empty() && i < MAX_ITERATIONS; i++)
    {
      const auto [current, currentPriority] = frontier.top();
      frontier.pop();

#ifndef GAME_HEADLESS
      auto& lines           = const_cast<World&>(world).GetRegistry().ctx().get<std::vector<Debug::Line>>();
      constexpr auto offset = glm::vec3(0.5f, 0, 0.5f);
      lines.emplace_back(Debug::Line{
        .aPosition = glm::vec3(current) + offset,
        .aColor    = glm::vec4(.8f, .4f, 0, 1),
        .bPosition = glm::vec3(current) + offset + glm::vec3(0, currentPriority * 0.01f, 0),
        .bColor    = glm::vec4(.8f, .8f, 0, 1),
      });
#endif

      const auto currentCost = costSoFar.at(current);
      for (auto next : GetNeighbors(grid, current, height))
      {
        const auto newCost = currentCost + DetermineCost(grid, current, next);
        if (auto it = costSoFar.find(next); it == costSoFar.end() || newCost < it->second)
        {
          frontier.emplace(next, newCost + HeuristicCost(grid, next, goal));
          costSoFar[next] = newCost;
          cameFrom[next] = current;
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
