#include "Assets.h"
#include <optional>

std::filesystem::path GetAssetDirectory()
{
  static std::optional<std::filesystem::path> assetsPath;
  if (!assetsPath)
  {
    auto dir = std::filesystem::current_path();
    while (!dir.empty())
    {
      auto maybeAssets = dir / "data";
      if (exists(maybeAssets) && is_directory(maybeAssets))
      {
        assetsPath = maybeAssets;
        break;
      }

      if (!dir.has_parent_path())
      {
        break;
      }

      dir = dir.parent_path();
    }
  }
  return assetsPath.value(); // Will throw if asset directory wasn't found.
}

std::filesystem::path GetShaderDirectory()
{
  return GetAssetDirectory() / "shaders";
}

std::filesystem::path GetTextureDirectory()
{
  return GetAssetDirectory() / "textures";
}

std::filesystem::path GetConfigDirectory()
{
  return GetAssetDirectory() / "config";
}