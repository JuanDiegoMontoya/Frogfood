#include "Shader2.h"
#include "detail/Common.h"

#include <volk.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <vector>
#include <cassert>
#include <stdexcept>

namespace Fvog
{
  namespace
  {
    VkShaderStageFlagBits PipelineStageToVK(PipelineStage stage)
    {
      switch (stage)
      {
      case PipelineStage::VERTEX_SHADER: return VK_SHADER_STAGE_VERTEX_BIT;
      case PipelineStage::FRAGMENT_SHADER: return VK_SHADER_STAGE_FRAGMENT_BIT;
      case PipelineStage::COMPUTE_SHADER: return VK_SHADER_STAGE_COMPUTE_BIT;
      default: assert(0); return {};
      }
    }

    TBuiltInResource GetGlslangResourceLimits()
    {
      // TODO: populate this structure with actual device limits
      return {
        .maxLights = 32,
        .maxClipPlanes = 6,
        .maxTextureUnits = 32,
        .maxTextureCoords = 32,
        .maxVertexAttribs = 64,
        .maxVertexUniformComponents = 4096,
        .maxVaryingFloats = 64,
        .maxVertexTextureImageUnits = 32,
        .maxCombinedTextureImageUnits = 80,
        .maxTextureImageUnits = 32,
        .maxFragmentUniformComponents = 4096,
        .maxDrawBuffers = 32,
        .maxVertexUniformVectors = 128,
        .maxVaryingVectors = 8,
        .maxFragmentUniformVectors = 16,
        .maxVertexOutputVectors = 16,
        .maxFragmentInputVectors = 15,
        .minProgramTexelOffset = -8,
        .maxProgramTexelOffset = 7,
        .maxClipDistances = 8,
        .maxComputeWorkGroupCountX = 65535,
        .maxComputeWorkGroupCountY = 65535,
        .maxComputeWorkGroupCountZ = 65535,
        .maxComputeWorkGroupSizeX = 1024,
        .maxComputeWorkGroupSizeY = 1024,
        .maxComputeWorkGroupSizeZ = 64,
        .maxComputeUniformComponents = 1024,
        .maxComputeTextureImageUnits = 16,
        .maxComputeImageUniforms = 8,
        .maxComputeAtomicCounters = 8,
        .maxComputeAtomicCounterBuffers = 1,
        .maxVaryingComponents = 60,
        .maxVertexOutputComponents = 64,
        .maxGeometryInputComponents = 64,
        .maxGeometryOutputComponents = 128,
        .maxFragmentInputComponents = 128,
        .maxImageUnits = 8,
        .maxCombinedImageUnitsAndFragmentOutputs = 8,
        .maxCombinedShaderOutputResources = 8,
        .maxImageSamples = 0,
        .maxVertexImageUniforms = 0,
        .maxTessControlImageUniforms = 0,
        .maxTessEvaluationImageUniforms = 0,
        .maxGeometryImageUniforms = 0,
        .maxFragmentImageUniforms = 8,
        .maxCombinedImageUniforms = 8,
        .maxGeometryTextureImageUnits = 16,
        .maxGeometryOutputVertices = 256,
        .maxGeometryTotalOutputComponents = 1024,
        .maxGeometryUniformComponents = 1024,
        .maxGeometryVaryingComponents = 64,
        .maxTessControlInputComponents = 128,
        .maxTessControlOutputComponents = 128,
        .maxTessControlTextureImageUnits = 16,
        .maxTessControlUniformComponents = 1024,
        .maxTessControlTotalOutputComponents = 4096,
        .maxTessEvaluationInputComponents = 128,
        .maxTessEvaluationOutputComponents = 128,
        .maxTessEvaluationTextureImageUnits = 16,
        .maxTessEvaluationUniformComponents = 1024,
        .maxTessPatchComponents = 120,
        .maxPatchVertices = 32,
        .maxTessGenLevel = 64,
        .maxViewports = 16,
        .maxVertexAtomicCounters = 0,
        .maxTessControlAtomicCounters = 0,
        .maxTessEvaluationAtomicCounters = 0,
        .maxGeometryAtomicCounters = 0,
        .maxFragmentAtomicCounters = 8,
        .maxCombinedAtomicCounters = 8,
        .maxAtomicCounterBindings = 1,
        .maxVertexAtomicCounterBuffers = 0,
        .maxTessControlAtomicCounterBuffers = 0,
        .maxTessEvaluationAtomicCounterBuffers = 0,
        .maxGeometryAtomicCounterBuffers = 0,
        .maxFragmentAtomicCounterBuffers = 1,
        .maxCombinedAtomicCounterBuffers = 1,
        .maxAtomicCounterBufferSize = 16384,
        .maxTransformFeedbackBuffers = 4,
        .maxTransformFeedbackInterleavedComponents = 64,
        .maxCullDistances = 8,
        .maxCombinedClipAndCullDistances = 8,
        .maxSamples = 4,
        .maxMeshOutputVerticesNV = 256,
        .maxMeshOutputPrimitivesNV = 512,
        .maxMeshWorkGroupSizeX_NV = 32,
        .maxMeshWorkGroupSizeY_NV = 1,
        .maxMeshWorkGroupSizeZ_NV = 1,
        .maxTaskWorkGroupSizeX_NV = 32,
        .maxTaskWorkGroupSizeY_NV = 1,
        .maxTaskWorkGroupSizeZ_NV = 1,
        .maxMeshViewCountNV = 4,
        .limits =
          {
            .nonInductiveForLoops = true,
            .whileLoops = true,
            .doWhileLoops = true,
            .generalUniformIndexing = true,
            .generalAttributeMatrixVectorIndexing = true,
            .generalVaryingIndexing = true,
            .generalSamplerIndexing = true,
            .generalVariableIndexing = true,
            .generalConstantMatrixVectorIndexing = true,
          },
      };
    }

    constexpr EShLanguage VkShaderStageToGlslang(VkShaderStageFlagBits stage)
    {
      switch (stage)
      {
      case VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT: return EShLanguage::EShLangVertex;
      case VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT: return EShLanguage::EShLangFragment;
      case VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT: return EShLanguage::EShLangCompute;
      }
      return static_cast<EShLanguage>(-1);
    }

    struct ShaderInfo
    {
      std::vector<uint32_t> binarySpv;
      Extent3D workgroupSize_{};
    };

    ShaderInfo CompileShaderToSpirv(VkShaderStageFlagBits stage, std::string_view source)
    {
      constexpr auto compilerMessages = EShMessages(EShMessages::EShMsgSpvRules | EShMessages::EShMsgVulkanRules);
      const auto resourceLimits = GetGlslangResourceLimits();
      const auto glslangStage = VkShaderStageToGlslang(stage);

      auto shader = glslang::TShader(glslangStage);
      int length = static_cast<int>(source.size());
      const char* data = source.data();
      shader.setStringsWithLengths(&data, &length, 1);
      shader.setEnvInput(glslang::EShSource::EShSourceGlsl, glslangStage, glslang::EShClient::EShClientVulkan, 460);
      shader.setEnvClient(glslang::EShClient::EShClientVulkan, glslang::EShTargetClientVersion::EShTargetVulkan_1_3);
      shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv, glslang::EShTargetLanguageVersion::EShTargetSpv_1_6);
      if (!shader.parse(&resourceLimits, 450, EProfile::ECoreProfile, false, false, compilerMessages))
      {
        printf("Info log: %s\nDebug log: %s\n", shader.getInfoLog(), shader.getInfoDebugLog());
        // TODO: throw shader compile error
        throw std::runtime_error("rip");
      }

      auto program = glslang::TProgram();
      program.addShader(&shader);
      
      if (!program.link(EShMessages::EShMsgDefault))
      {
        printf("Info log: %s\nDebug log: %s\n", shader.getInfoLog(), shader.getInfoDebugLog());
        // TODO: throw shader compile error
        throw std::runtime_error("rip");
      }

      program.buildReflection();

      ShaderInfo info;

      // TODO: task/mesh stages
      if (stage == VK_SHADER_STAGE_COMPUTE_BIT)
      {
        info.workgroupSize_.width = program.getLocalSize(0);
        info.workgroupSize_.height = program.getLocalSize(1);
        info.workgroupSize_.depth = program.getLocalSize(2);
      }

      auto options = glslang::SpvOptions{};
      glslang::GlslangToSpv(*program.getIntermediate(glslangStage), info.binarySpv, &options);

      return info;
    }
  } // namespace

  Shader::Shader(VkDevice device, PipelineStage stage, std::string_view source, const char* name)
    : device_(device)
  {
    using namespace detail;

    const auto info = CompileShaderToSpirv(PipelineStageToVK(stage), source);
    CheckVkResult(
      vkCreateShaderModule(
        device,
        Address(VkShaderModuleCreateInfo{
          .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
          .codeSize = info.binarySpv.size() * sizeof(uint32_t),
          .pCode = info.binarySpv.data(),
        }),
        nullptr,
        &shaderModule_));

    workgroupSize_ = info.workgroupSize_;
    
    // TODO: gate behind compile-time switch
    vkSetDebugUtilsObjectNameEXT(device_, detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_SHADER_MODULE,
      .objectHandle = reinterpret_cast<uint64_t>(shaderModule_),
      .pObjectName = name ? (name + std::string(" (shader)")).c_str() : nullptr,
    }));
  }

  Shader::Shader(Shader&& old) noexcept
    : device_(std::exchange(old.device_, VK_NULL_HANDLE)),
      shaderModule_(std::exchange(old.shaderModule_, VK_NULL_HANDLE)) {}

  Shader& Shader::operator=(Shader&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~Shader();
    return *new (this) Shader(std::move(old));
  }

  Shader::~Shader()
  {
    if (device_)
    {
      vkDestroyShaderModule(device_, shaderModule_, nullptr);
    }
  }
}