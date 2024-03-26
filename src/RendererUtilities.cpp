#include "RendererUtilities.h"

#define STB_INCLUDE_IMPLEMENTATION
#define STB_INCLUDE_LINE_GLSL
#include "stb_include.h"

Fwog::Shader LoadShaderWithIncludes(Fwog::PipelineStage stage, const std::filesystem::path& path)
{
  if (!std::filesystem::exists(path) || std::filesystem::is_directory(path))
  {
    throw std::runtime_error("Path does not refer to a file");
  }
  auto pathStr = path.string();
  auto parentPathStr = path.parent_path().string();
  char error[256]{};
  auto processedSource = std::unique_ptr<char, decltype([](char* p) { free(p); })>(stb_include_file(pathStr.c_str(), nullptr, parentPathStr.c_str(), error));
  if (!processedSource)
  {
    throw std::runtime_error("Failed to process includes");
  }
  return Fwog::Shader(stage, processedSource.get());
}

Fvog::Shader LoadShaderWithIncludes2(VkDevice device, Fvog::PipelineStage stage, const std::filesystem::path& path)
{
  if (!std::filesystem::exists(path) || std::filesystem::is_directory(path))
  {
    throw std::runtime_error("Path does not refer to a file");
  }
  auto pathStr = path.string();
  auto parentPathStr = path.parent_path().string();
  char error[256]{};
  auto processedSource = std::unique_ptr<char, decltype([](char* p) { free(p); })>(stb_include_file(pathStr.c_str(), nullptr, parentPathStr.c_str(), error));
  if (!processedSource)
  {
    throw std::runtime_error("Failed to process includes");
  }
  return Fvog::Shader(device, stage, processedSource.get(), path.filename().string().c_str());
}