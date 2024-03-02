#pragma once
#include <Fwog/Shader.h>
#include "Fvog/Shader2.h"

#include <filesystem>

Fwog::Shader LoadShaderWithIncludes(Fwog::PipelineStage stage, const std::filesystem::path& path);
Fvog::Shader LoadShaderWithIncludes2(VkDevice device, Fvog::PipelineStage stage, const std::filesystem::path& path);