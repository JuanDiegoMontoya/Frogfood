#pragma once
#include <Fwog/Shader.h>

#include <filesystem>

Fwog::Shader LoadShaderWithIncludes(Fwog::PipelineStage stage, const std::filesystem::path& path);