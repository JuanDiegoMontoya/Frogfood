#include "Shader2.h"
#include "detail/Common.h"
#include "TriviallyCopyableByteSpan.h"
#include "Device.h"

#include <volk.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>

#include <tracy/Tracy.hpp>

#include <vector>
#include <cassert>
#include <stdexcept>
#include <fstream>
#include <stdexcept>
#include <memory>
#include <span>
#include <cstddef>

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
      case PipelineStage::RAYGEN_SHADER: return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
      case PipelineStage::MISS_SHADER: return VK_SHADER_STAGE_MISS_BIT_KHR;
      case PipelineStage::CLOSEST_HIT_SHADER: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
      case PipelineStage::ANY_HIT_SHADER: return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
      case PipelineStage::INTERSECTION_SHADER: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
      default: assert(0); return {};
      }
    }

    std::string LoadFile(const std::filesystem::path& path)
    {
      std::ifstream file{path};
      if (!file)
      {
        throw std::runtime_error("File not found");
      }
      return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    // For debugging
    void WriteBinaryFile(const std::filesystem::path& path, TriviallyCopyableByteSpan bytes)
    {
      auto file = std::ofstream(path, std::ios::out | std::ios::binary);
      if (!file)
      {
        throw std::runtime_error("Could not open file for writing");
      }
      file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size_bytes());
    }
    
    // Hackity hack
    size_t NumberOfPathComponents(std::filesystem::path path)
    {
      size_t parents = 0;
      while (!path.empty())
      {
        parents++;
        path = path.parent_path();
      }
      return parents > 0 ? parents - 1 : 0; // The path will contain a filename, which we will ignore
    }
    
    class IncludeHandler final : public glslang::TShader::Includer
    {
    public:
      IncludeHandler(const std::filesystem::path& sourcePath)
      {
        // Seed the "stack" with just the parent directory of the top-level source
        currentIncluderDir_ /= sourcePath.parent_path();
      }

      glslang::TShader::Includer::IncludeResult* includeLocal(
        const char* requested_source,
        [[maybe_unused]] const char* requesting_source,
        [[maybe_unused]] size_t include_depth) override
      {
        ZoneScoped;
        // Everything will explode if this is not relative
        assert(std::filesystem::path(requested_source).is_relative());
        auto fullRequestedSource = currentIncluderDir_ / requested_source;
        currentIncluderDir_ = fullRequestedSource.parent_path();

        auto contentPtr = std::make_unique<std::string>(LoadFile(fullRequestedSource));
        auto content = contentPtr.get();
        auto sourcePathPtr = std::make_unique<std::string>(requested_source);
        //auto sourcePath = sourcePathPtr.get();

        contentStrings_.emplace_back(std::move(contentPtr));
        sourcePathStrings_.emplace_back(std::move(sourcePathPtr));

        return new glslang::TShader::Includer::IncludeResult(requested_source, content->c_str(), content->size(), nullptr);
      }

      void releaseInclude(glslang::TShader::Includer::IncludeResult* data) override
      {
        for (size_t i = 0; i < NumberOfPathComponents(data->headerName); i++)
        {
          currentIncluderDir_ = currentIncluderDir_.parent_path();
        }
        
        delete data;
      }

    private:
      // Acts like a stack that we "push" path components to when include{Local, System} are invoked, and "pop" when releaseInclude is invoked
      std::filesystem::path currentIncluderDir_;
      std::vector<std::unique_ptr<std::string>> contentStrings_;
      std::vector<std::unique_ptr<std::string>> sourcePathStrings_;
    };

    constexpr EShLanguage VkShaderStageToGlslang(VkShaderStageFlagBits stage)
    {
      switch (stage)
      {
      case VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT: return EShLanguage::EShLangVertex;
      case VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT: return EShLanguage::EShLangFragment;
      case VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT: return EShLanguage::EShLangCompute;
      case VkShaderStageFlagBits::VK_SHADER_STAGE_RAYGEN_BIT_KHR: return EShLanguage::EShLangRayGen;
      case VkShaderStageFlagBits::VK_SHADER_STAGE_MISS_BIT_KHR: return EShLanguage::EShLangMiss;
      case VkShaderStageFlagBits::VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR: return EShLanguage::EShLangClosestHit;
      case VkShaderStageFlagBits::VK_SHADER_STAGE_ANY_HIT_BIT_KHR: return EShLanguage::EShLangAnyHit;
      case VkShaderStageFlagBits::VK_SHADER_STAGE_INTERSECTION_BIT_KHR: return EShLanguage::EShLangIntersect;
      }
      return static_cast<EShLanguage>(-1);
    }

    detail::ShaderCompileInfo CompileShaderToSpirv(VkShaderStageFlagBits stage, std::string_view source, glslang::TShader::Includer* includer)
    {
      ZoneScoped;
      const auto glslangStage = VkShaderStageToGlslang(stage);

      auto shader = glslang::TShader(glslangStage);
      int length = static_cast<int>(source.size());
      const char* data = source.data();
      shader.setStringsWithLengths(&data, &length, 1);
      shader.setEnvInput(glslang::EShSource::EShSourceGlsl, glslangStage, glslang::EShClient::EShClientVulkan, 100);
      shader.setEnvClient(glslang::EShClient::EShClientVulkan, glslang::EShTargetClientVersion::EShTargetVulkan_1_3);
      shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv, glslang::EShTargetLanguageVersion::EShTargetSpv_1_6);
      std::string preamble = "#extension GL_GOOGLE_include_directive : enable\n";
      if (GetDevice().supportsRayTracing)
      {
        preamble += "#define FROGRENDER_RAYTRACING_ENABLE 1\n";
      }
      shader.setPreamble(preamble.c_str());
      shader.setOverrideVersion(460);
      if (GetDevice().supportsRelaxedExtendedInstruction)
      {
        shader.setDebugInfo(true);
      }

      bool parseResult;
      {
        ZoneScopedN("Parse shader");
        constexpr auto compilerMessages = EShMessages(
          EShMsgSpvRules | EShMsgVulkanRules | EShMsgDebugInfo | EShMsgBuiltinSymbolTable | EShMsgEnhanced | EShMsgAbsolutePath | EShMsgDisplayErrorColumn);
        if (includer)
        {
          parseResult = shader.parse(GetDefaultResources(), 460, EProfile::ECoreProfile, false, false, compilerMessages, *includer);
        }
        else
        {
          parseResult = shader.parse(GetDefaultResources(), 460, EProfile::ECoreProfile, false, false, compilerMessages);
        }
      }

      if (!parseResult)
      {
        printf("Info log: %s\nDebug log: %s\n", shader.getInfoLog(), shader.getInfoDebugLog());
        // TODO: throw shader compile error
        throw std::runtime_error("rip");
      }

      auto program = glslang::TProgram();
      program.addShader(&shader);

      {
        ZoneScopedN("Link program");
        if (!program.link(EShMessages::EShMsgDefault))
        {
          printf("Info log: %s\nDebug log: %s\n", program.getInfoLog(), program.getInfoDebugLog());
          // TODO: throw shader compile error
          throw std::runtime_error("rip");
        }
      }
      
      program.buildReflection();
      detail::ShaderCompileInfo info;

      // TODO: task/mesh stages
      if (stage == VK_SHADER_STAGE_COMPUTE_BIT)
      {
        info.workgroupSize_.width = program.getLocalSize(0);
        info.workgroupSize_.height = program.getLocalSize(1);
        info.workgroupSize_.depth = program.getLocalSize(2);
      }

      // Equivalent to -gVS, which should be sufficient for RenderDoc to debug our shaders.
      // https://github.com/KhronosGroup/glslang/blob/vulkan-sdk-1.3.283.0/StandAlone/StandAlone.cpp#L998-L1016
      auto options = glslang::SpvOptions{
        .generateDebugInfo = true,
        .stripDebugInfo = false,
        .disableOptimizer = true,
        .emitNonSemanticShaderDebugInfo = GetDevice().supportsRelaxedExtendedInstruction,
        .emitNonSemanticShaderDebugSource = GetDevice().supportsRelaxedExtendedInstruction,
      };

      {
        
        ZoneScopedN("GlslangToSpv");
        spv::SpvBuildLogger logger;
        glslang::GlslangToSpv(*shader.getIntermediate(), info.binarySpv, &logger, &options);
        auto loggerMessages = logger.getAllMessages();
        if (!loggerMessages.empty())
        {
          printf("spv logger messages: %s", loggerMessages.c_str());
        }
      }

      // For debug-dumping SPIR-V to a file
      //WriteBinaryFile("TEST.spv", std::span(info.binarySpv));

      return info;
    }
  } // namespace

  void Shader::Initialize(const detail::ShaderCompileInfo& info)
  {
    using namespace detail;
    ZoneScoped;
    
    CheckVkResult(
      vkCreateShaderModule(Fvog::GetDevice().device_,
        Address(VkShaderModuleCreateInfo{
          .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
          .codeSize = info.binarySpv.size() * sizeof(uint32_t),
          .pCode = info.binarySpv.data(),
        }),
        nullptr,
        &shaderModule_));

    workgroupSize_ = info.workgroupSize_;
    
    // TODO: gate behind compile-time switch
    vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
      detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_SHADER_MODULE,
      .objectHandle = reinterpret_cast<uint64_t>(shaderModule_),
      .pObjectName = name_.data(),
    }));
  }

  Shader::Shader(PipelineStage stage, std::string_view source, std::string name)
    : name_(std::move(name)),
      stage_(stage)
  {
    ZoneScoped;
    ZoneNamed(_, true);
    ZoneNameV(_, name_.data(), name_.size());
    Initialize(CompileShaderToSpirv(PipelineStageToVK(stage), source, nullptr));
  }
  
  Shader::Shader(PipelineStage stage, const std::filesystem::path& path, std::string name)
    : name_(std::move(name)),
      stage_(stage)
  {
    ZoneScoped;
    ZoneNamed(_, true);
    ZoneNameV(_, name_.data(), name_.size());
    Initialize(CompileShaderToSpirv(PipelineStageToVK(stage), LoadFile(path), detail::Address(IncludeHandler(path))));
  }

  Shader::Shader(Shader&& old) noexcept
    : name_(std::move(old.name_)),
      stage_(old.stage_),
      shaderModule_(std::exchange(old.shaderModule_, VK_NULL_HANDLE)),
      workgroupSize_(std::exchange(old.workgroupSize_, {}))
  {}

  Shader& Shader::operator=(Shader&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~Shader();
    return *new (this) Shader(std::move(old));
  }

  Shader::~Shader()
  {
    if (shaderModule_ != VK_NULL_HANDLE)
    {
      vkDestroyShaderModule(Fvog::GetDevice().device_, shaderModule_, nullptr);
    }
  }
}
