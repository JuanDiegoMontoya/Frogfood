#pragma once

#include <volk.h>
#include <string>
#include <string_view>
#include <filesystem>
#include <vector>
#include "BasicTypes2.h"

namespace Fvog
{
  namespace detail
  {
    struct ShaderCompileInfo
    {
      std::vector<uint32_t> binarySpv;
      Extent3D workgroupSize_{};
    };
  }

  enum class PipelineStage
  {
    VERTEX_SHADER,
    FRAGMENT_SHADER,
    COMPUTE_SHADER,
    RAYGEN_SHADER,
    MISS_SHADER,
    CLOSEST_HIT_SHADER,
    ANY_HIT_SHADER,
    INTERSECTION_SHADER,
  };

  /// @brief A shader object to be used in one or more GraphicsPipeline or ComputePipeline objects
  class Shader
  {
  public:
    /// @brief Constructs the shader
    /// @param stage A pipeline stage
    /// @param source A GLSL source string
    /// @throws ShaderCompilationException if the shader is malformed
    // Already-processed source constructor
    explicit Shader(VkDevice device, PipelineStage stage, std::string_view source, std::string name = {});

    // Path constructor (uses glslang include handling)
    explicit Shader(VkDevice device, PipelineStage stage, const std::filesystem::path& path, std::string name = {});
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
    void Initialize(VkDevice device, const detail::ShaderCompileInfo& info);
    VkDevice device_;
    VkShaderModule shaderModule_;
    Extent3D workgroupSize_{};
    std::string name_;
  };
}