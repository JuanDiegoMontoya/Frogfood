#pragma once
#include "Game.h"

#include "entt/meta/meta.hpp"
#include "entt/meta/factory.hpp"

#include <cstdint>
#include <unordered_map>

namespace Core::Reflection
{
  enum Traits : uint16_t
  {
    SERIALIZE   = 1 << 0,
    EDITOR      = 1 << 1,
    EDITOR_READ = 1 << 2,
    COMPONENT   = 1 << 3,
  };

  using PropertiesMap = std::unordered_map<entt::id_type, entt::meta_any>;

  void InitializeReflection();
}
