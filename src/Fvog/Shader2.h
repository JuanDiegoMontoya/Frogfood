#pragma once

#include <volk.h>
#include <string_view>
#include "BasicTypes2.h"

namespace Fvog
{
  enum class PipelineStage
  {
    VERTEX_SHADER,
    FRAGMENT_SHADER,
    COMPUTE_SHADER
  };

  /// @brief A shader object to be used in one or more GraphicsPipeline or ComputePipeline objects
  class Shader
  {
  public:
    /// @brief Constructs the shader
    /// @param stage A pipeline stage
    /// @param source A GLSL source string
    /// @throws ShaderCompilationException if the shader is malformed
    explicit Shader(VkDevice device, PipelineStage stage, std::string_view source, std::string_view name = "");
    Shader(const Shader&) = delete;
    Shader(Shader&& old) noexcept;
    Shader& operator=(const Shader&) = delete;
    Shader& operator=(Shader&& old) noexcept;
    ~Shader();

    /// @brief Gets the handle of the underlying OpenGL shader object
    /// @return The shader
    [[nodiscard]] VkShaderModule Handle() const
    {
      return shaderModule_;
    }

    [[nodiscard]] Extent3D WorkgroupSize() const
    {
      return workgroupSize_;
    }

  private:
    VkDevice device_;
    VkShaderModule shaderModule_;
    Extent3D workgroupSize_{};
  };
}