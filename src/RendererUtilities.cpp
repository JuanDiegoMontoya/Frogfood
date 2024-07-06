#include "RendererUtilities.h"

#include "Fvog/Device.h"
#include "Fvog/Shader2.h"
#include "Fvog/Texture2.h"

#define STB_INCLUDE_IMPLEMENTATION
#define STB_INCLUDE_LINE_GLSL
#include "stb_include.h"
#include "stb_image.h"

// Experimental includer that uses glslang includer
Fvog::Shader LoadShaderWithIncludes2(Fvog::Device& device, Fvog::PipelineStage stage, const std::filesystem::path& path)
{
  if (!std::filesystem::exists(path) || std::filesystem::is_directory(path))
  {
    throw std::runtime_error("Path does not refer to a file");
  }
  return Fvog::Shader(device.device_, stage, path, path.filename().string().c_str());
}

Fvog::Texture LoadTextureShrimple(Fvog::Device& device, const std::filesystem::path& path)
{
  int x{};
  int y{};
  auto* pixels = stbi_load(path.string().c_str(), &x, &y, nullptr, 4);
  if (!pixels)
  {
    throw std::runtime_error("Texture not found");
  }
  auto texture = Fvog::CreateTexture2D(device, {(uint32_t)x, (uint32_t)y}, Fvog::Format::R8G8B8A8_SRGB, Fvog::TextureUsage::READ_ONLY, path.string());
  texture.UpdateImageSLOW({
    .extent = texture.GetCreateInfo().extent,
    .data = pixels,
  });
  stbi_image_free(pixels);
  return texture;
}

//Fvog::Shader LoadShaderWithIncludes2(Fvog::Device& device, Fvog::PipelineStage stage, const std::filesystem::path& path)
//{
//  if (!std::filesystem::exists(path) || std::filesystem::is_directory(path))
//  {
//    throw std::runtime_error("Path does not refer to a file");
//  }
//
//  auto pathStr = path.string();
//  auto parentPathStr = path.parent_path().string();
//  char error[256]{};
//  auto processedSource = std::unique_ptr<char, decltype([](char* p) { free(p); })>(stb_include_file(pathStr.c_str(), nullptr, parentPathStr.c_str(), error));
//  if (!processedSource)
//  {
//    throw std::runtime_error("Failed to process includes");
//  }
//  return Fvog::Shader(device.device_, stage, std::string_view(processedSource.get()), path.filename().string().c_str());
//}
