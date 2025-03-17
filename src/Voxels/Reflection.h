#pragma once
#include "Game.h"

#include "entt/fwd.hpp"

#include <cstdint>
#include <unordered_map>
#include <filesystem>

namespace Core::Reflection
{
  enum Traits : uint16_t
  {
    SERIALIZE   = 1 << 0,
    EDITOR      = 1 << 1,
    EDITOR_READ = 1 << 2,
    COMPONENT   = 1 << 3,
    VARIANT     = 1 << 4,
  };

  inline Traits operator|(Traits a, Traits b)
  {
    return static_cast<Traits>((uint32_t)a | (uint32_t)b);
  }

  using PropertiesMap = std::unordered_map<entt::id_type, entt::meta_any>;

  void InitializeReflection();

  void SaveRegistryToFile(const World& world, const std::filesystem::path& path);
  void LoadRegistryFromFile(World& world, const std::filesystem::path& path);
}
