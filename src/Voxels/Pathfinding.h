#pragma once
#include "LRUCache.h"

#include "glm/vec3.hpp"

#include <vector>

class World;

namespace Pathfinding
{
  struct FindPathParams;
}

template<>
struct std::hash<Pathfinding::FindPathParams>
{
  std::size_t operator()(const Pathfinding::FindPathParams& p) const noexcept;
};

namespace Pathfinding
{
  struct FindPathParams
  {
    glm::ivec3 start;
    glm::ivec3 goal;
    int height = 1;
    float w = 1;
    bool canFly = false;
    int maxNodesToSearch = 1000;

    bool operator==(const FindPathParams&) const noexcept = default;
  };

  using Path = std::vector<glm::vec3>;

  // Intended to be used as a component.
  struct CachedPath
  {
    Path path; // TODO: Use shared_ptr?
    uint32_t progress = 0; // Index of path segment to move towards.
    float updateAccum = 1000;
    float timeBetweenUpdates = 1;
    // float jitter = ? // Randomize time between updates so they don't happen simultaneously.
  };

  // w: WA* weight factor. When w = 1, algorithm is A*. When w < 0, algorithm approaches Dijkstra's. When w > 1, algorithm approaches greedy BFS.
  Path FindPath(const World& world, const FindPathParams& params);

  class PathCache
  {
  public:
    const Path& FindOrGetCachedPath(const World& world, const FindPathParams& params);

  private:

    LRUCache<FindPathParams, Path> cache_;
  };
} // namespace Pathfinding

