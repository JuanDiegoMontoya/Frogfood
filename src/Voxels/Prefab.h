#pragma once
#include "ClassImplMacros.h"

#include "glm/vec3.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using PrefabId = uint32_t;
//class World;

class PrefabDefinition
{
public:
  struct CreateInfo
  {
    std::string name;
  };

  explicit PrefabDefinition(const CreateInfo& info) : createInfo_(info) {}

  virtual ~PrefabDefinition() = default;

  virtual std::string GetName() const
  {
    return createInfo_.name;
  }

  NO_COPY_NO_MOVE(PrefabDefinition);

  // Generates a list of voxels in object space. `worldPos` is used as the RNG seed.
  virtual std::vector<std::pair<glm::ivec3, uint32_t>> GetVoxels(glm::ivec3 worldPos) const = 0;

private:
  CreateInfo createInfo_;
};

class SimplePrefab : public PrefabDefinition
{
public:
  using PrefabDefinition::PrefabDefinition;

  std::vector<std::pair<glm::ivec3, uint32_t>> GetVoxels(glm::ivec3) const override
  {
    return voxels;
  }

  std::vector<std::pair<glm::ivec3, uint32_t>> voxels;
};

class PrefabRegistry
{
public:
  PrefabRegistry() = default;

  ~PrefabRegistry() = default;

  NO_COPY(PrefabRegistry);
  DEFAULT_MOVE(PrefabRegistry);

  [[nodiscard]] const PrefabDefinition& Get(const std::string& name) const
  {
    return *idToDefinition_.at(nameToId_.at(name));
  }

  [[nodiscard]] const PrefabDefinition& Get(PrefabId id) const
  {
    return *idToDefinition_.at(id);
  }

  [[nodiscard]] PrefabId GetId(const std::string& name) const
  {
    return nameToId_.at(name);
  }

  PrefabId Add(PrefabDefinition* prefabDefinition)
  {
    assert(prefabDefinition);
    assert(!nameToId_.contains(prefabDefinition->GetName()));

    const auto id = static_cast<PrefabId>(idToDefinition_.size());
    nameToId_.try_emplace(prefabDefinition->GetName(), id);
    idToDefinition_.emplace_back(prefabDefinition);

    return id;
  }

  [[nodiscard]] std::span<const std::unique_ptr<PrefabDefinition>> GetAllDefinitions() const
  {
    return std::span(idToDefinition_);
  }

private:
  std::unordered_map<std::string, PrefabId> nameToId_;
  std::vector<std::unique_ptr<PrefabDefinition>> idToDefinition_;
};