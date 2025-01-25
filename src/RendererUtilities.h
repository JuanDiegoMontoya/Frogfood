#pragma once
#include "Fvog/Device.h"
#include "Fvog/Shader2.h"

#include <filesystem>

namespace Fvog
{
  class Device;
  class Texture;
}

Fvog::Shader LoadShaderWithIncludes2(Fvog::PipelineStage stage, const std::filesystem::path& path);

Fvog::Texture LoadTextureShrimple(const std::filesystem::path& path);
