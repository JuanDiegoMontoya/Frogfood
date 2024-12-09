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
    int height;
    float w;

    bool operator==(const FindPathParams&) const noexcept = default;
  };


  using Path = std::vector<glm::vec3>;

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

