#include "Prefab.h"
#include "Game.h"

void SimplePrefab::Instantiate(World& world, glm::ivec3 worldPos) const
{
  const auto& blocks = world.GetRegistry().ctx().get<BlockRegistry>();
  for (const auto& [blockPos, block] : voxels)
  {
    blocks.Get(block).OnTryPlaceBlock(world, worldPos + blockPos);
  }
}
