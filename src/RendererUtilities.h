#pragma once
#include "Fvog/Device.h"
#include "Fvog/Shader2.h"

#include <filesystem>

namespace Fvog
{
  class Device;
  class Texture;
}

Fvog::Shader LoadShaderWithIncludes2(Fvog::Device& device, Fvog::PipelineStage stage, const std::filesystem::path& path);

Fvog::Texture LoadTextureShrimple(Fvog::Device& device, const std::filesystem::path& path);