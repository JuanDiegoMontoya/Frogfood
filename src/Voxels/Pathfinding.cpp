#include "Pathfinding.h"

#include "TwoLevelGrid.h"

#ifndef GAME_HEADLESS
#include "debug/Shapes.h"
#endif

#include "Game.h"
#include "HashUtilities.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/hash.hpp"
#include "glm/gtx/component_wise.hpp"
#include "glm/gtc/epsilon.hpp"
#include "ankerl/unordered_dense.h"

#include "tracy/Tracy.hpp"

#include <queue>

namespace Pathfinding
{
  namespace
  {
    // height = number of blocks of clearance above the floor to fit through a gap
    auto GetNeighbors(const TwoLevelGrid& grid, glm::ivec3 pos, int height)
    {
      ZoneScoped;
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

    auto GetNeighborsForFlying(const TwoLevelGrid& grid, glm::ivec3 pos, int height)
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

      neighbors.emplace_back(pos + glm::ivec3(1, 0, 0));
      neighbors.emplace_back(pos + glm::ivec3(-1, 0, 0));
      neighbors.emplace_back(pos + glm::ivec3(0, 1, 0));
      neighbors.emplace_back(pos + glm::ivec3(0, -1, 0));
      neighbors.emplace_back(pos + glm::ivec3(0, 0, 1));
      neighbors.emplace_back(pos + glm::ivec3(0, 0, -1));

      return neighbors;
    }

    auto ReconstructPath(const ankerl::unordered_dense::map<glm::ivec3, glm::ivec3>& cameFrom, glm::ivec3 start, glm::ivec3 goal)
    {
      assert(cameFrom.contains(goal));
      auto path = std::vector<glm::vec3>();
      auto current = goal;

      while (current != start)
      {
        path.emplace_back(glm::vec3(current) + glm::vec3(0.5f));
        current = cameFrom.at(current);
      }

      return std::vector(path.rbegin(), path.rend());
    }

    // The actual cost to move from posFrom to posTo (they must be adjacent)
    float DetermineCost([[maybe_unused]] const TwoLevelGrid& grid, [[maybe_unused]] glm::ivec3 posFrom, [[maybe_unused]] glm::ivec3 posTo)
    {
      // Reduce cost of falling.
      if (posFrom.y > posTo.y)
      {
        return 0.5;
      }

      // If not falling, but there is no air under destination, increase cost.
      if (grid.GetVoxelAt(posTo - glm::ivec3(0, 1, 0)) == 0)
      {
        return 1.125f;
      }

      return 1;
    }

    float DetermineCostForFlying([[maybe_unused]] const TwoLevelGrid& grid, [[maybe_unused]] glm::ivec3 posFrom, [[maybe_unused]] glm::ivec3 posTo)
    {
      return 1;
    }

    // The approximate cost to move from pos0 to pos1. If it exceeds the actual cost, imperfect paths will be generated.
    float HeuristicCost([[maybe_unused]] const TwoLevelGrid& grid, [[maybe_unused]] glm::ivec3 pos0, [[maybe_unused]] glm::ivec3 pos1)
    {
      return (float)glm::compAdd(glm::abs(pos0 - pos1)); // Manhattan
    }
  } // namespace

  std::vector<glm::vec3> FindPath(const World& world, const FindPathParams& params)
  {
    ZoneScoped;
    struct FNode
    {
      glm::ivec3 pos;
      float priority;
    };

    const auto cmp = [&](FNode left, FNode right)
    {
      // Euclidean distance^2 tiebreaker leads to significantly better paths.
      if (glm::epsilonEqual(left.priority, right.priority, 1e-3f))
      {
        auto a = glm::vec3(left.pos) - glm::vec3(params.goal);
        auto b = glm::vec3(right.pos) - glm::vec3(params.goal);
        return glm::dot(a, a) > glm::dot(b, b);
      }
      return left.priority > right.priority;
    };
    auto frontier  = std::priority_queue<FNode, std::vector<FNode>, decltype(cmp)>(cmp);
    auto cameFrom  = ankerl::unordered_dense::map<glm::ivec3, glm::ivec3>();
    auto costSoFar = ankerl::unordered_dense::map<glm::ivec3, float>();
    
    frontier.emplace(params.start, 0.0f);
    costSoFar.emplace(params.start, 0.0f);
    cameFrom.emplace(params.start, params.start);
    
    const auto& grid = world.GetRegistry().ctx().get<TwoLevelGrid>();
    for (int i = 0; !frontier.empty() && i < params.maxNodesToSearch; i++)
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
      for (auto next : params.canFly ? GetNeighborsForFlying(grid, current, params.height) : GetNeighbors(grid, current, params.height))
      {
        const auto newCost = currentCost + (params.canFly ? DetermineCostForFlying(grid, current, next) : DetermineCost(grid, current, next));
        if (auto it = costSoFar.find(next); it == costSoFar.end() || newCost < it->second)
        {
          frontier.emplace(next, newCost + params.w * HeuristicCost(grid, next, params.goal));
          costSoFar[next] = newCost;
          cameFrom[next] = current;
        }

        if (next == params.goal)
        {
          ZoneTextF("Iterations: %d", i);
          return ReconstructPath(cameFrom, params.start, params.goal);
        }
      }
    }

    // No path found.
    [[maybe_unused]] const char* text = "No path found";
    ZoneText(text, sizeof(text));
    return {};
  }

  const Path& PathCache::FindOrGetCachedPath(const World& world, const FindPathParams& params)
  {
    ZoneScoped;
    if (auto* p = cache_.get(params))
    {
      return *p;
    }

    auto path = FindPath(world, params);
    return cache_.set(params, path);
  }
} // namespace Pathfinding

std::size_t std::hash<Pathfinding::FindPathParams>::operator()(const Pathfinding::FindPathParams& p) const noexcept
{
  auto tup = std::make_tuple(p.start, p.goal, p.height, p.w, p.maxNodesToSearch);
  return ::hash<decltype(tup)>{}(tup);
}
