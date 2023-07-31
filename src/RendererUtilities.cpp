#include "RendererUtilities.h"

#include "stb_include.h"

Fwog::Shader LoadShaderWithIncludes(Fwog::PipelineStage stage, const std::filesystem::path& path)
{
  if (!std::filesystem::exists(path) || std::filesystem::is_directory(path))
  {
    throw std::runtime_error("Path does not refer to a file");
  }
  auto pathStr = path.string();
  auto parentPathStr = path.parent_path().string();
  auto processedSource = std::unique_ptr<char, decltype([](char* p) { free(p); })>(stb_include_file(pathStr.c_str(), nullptr, parentPathStr.c_str(), nullptr));
  return Fwog::Shader(stage, processedSource.get());
}