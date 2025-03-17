#pragma once
#include <filesystem>

class World;

namespace Core::Serialization
{
  void Initialize();

  void SaveRegistryToFile(const World& world, const std::filesystem::path& path);
  void LoadRegistryFromFile(World& world, const std::filesystem::path& path);

  namespace detail
  {
    template<typename Archive, typename T>
    void Serialize2(Archive& ar, T& value)
    {
      ar(value);
    }
  }
}