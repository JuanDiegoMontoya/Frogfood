#pragma once
#include "Fvog/Shader2.h"

#include <filesystem>

namespace Fvog
{
  class Device;
}

Fvog::Shader LoadShaderWithIncludes2(Fvog::Device& device, Fvog::PipelineStage stage, const std::filesystem::path& path);