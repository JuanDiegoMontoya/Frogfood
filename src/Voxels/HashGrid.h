#pragma once
#include <entt/entity/entity.hpp>

#include "tracy/Tracy.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/vec3.hpp>

#include <unordered_map>

class HashGrid
{
public:
  using Key   = glm::vec3;
  using ActualKey = glm::ivec3; // Provided "keys" are quantized
  using Value = entt::entity;

  HashGrid(int chunkSize) : chunkSize_(chunkSize) {}

  int ChunkSize() const
  {
    return chunkSize_;
  }

  // Get all entities 
  [[nodiscard]] auto equal_range(const Key& position)
  {
    return positionToEntities_.equal_range(QuantizeKey(position));
  }

  [[nodiscard]] auto equal_range(const Key& position) const
  {
    return positionToEntities_.equal_range(QuantizeKey(position));
  }

  [[nodiscard]] auto equal_range_chunk(const ActualKey& position)
  {
    return positionToEntities_.equal_range(position);
  }

  [[nodiscard]] auto equal_range_chunk(const ActualKey& position) const
  {
    return positionToEntities_.equal_range(position);
  }

  // entities are guaranteed to only show up once.
  void erase(const Value& value)
  {
    ZoneScoped;
    if (auto itit = entityToPosition_.find(value); itit != entityToPosition_.end())
    {
      positionToEntities_.erase(itit->second);
    }
  }

  // Add an element or change its position.
  void set(const Key& position, const Value& value)
  {
    ZoneScoped;
    auto it = entityToPosition_.find(value);
    if (it != entityToPosition_.end())
    {
      positionToEntities_.erase(it->second);
    }

    auto newIt = positionToEntities_.emplace(QuantizeKey(position), value);

    if (it == entityToPosition_.end())
    {
      entityToPosition_.emplace(value, newIt);
    }
    else
    {
      it->second = newIt;
    }
  }
  

  ActualKey QuantizeKey(const Key& position) const
  {
    return glm::ivec3(glm::floor(position / (float)chunkSize_));
  }

private:
  using Multimap = std::unordered_multimap<ActualKey, Value>;
  Multimap positionToEntities_;
  std::unordered_map<Value, Multimap::iterator> entityToPosition_;
  int chunkSize_;
};