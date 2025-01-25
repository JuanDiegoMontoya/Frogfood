#pragma once
#include "Fvog/Pipeline2.h"
#include "Fvog/Shader2.h"

#include <cassert>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <vector>
#include <optional>

// The purpose of this class is to serve as a central place to manage shader and pipeline compilation.
// The interface should be such that multithreaded compilation and shader deduplication could be transparently performed,
// i.e. the user gets indirect handles to pipelines which can act as an IOU.
// This system additionally allows pipelines to be swapped out without invalidating any of the user's handles,
// enabling simple runtime shader compilation.
class PipelineManager
{
public:
  PipelineManager() = default;

  // Moving would invalidate references from child objects, so forbid it (this is probably very smelly)
  PipelineManager(PipelineManager&&) noexcept = delete;
  PipelineManager& operator=(PipelineManager&&) noexcept = delete;

  class GraphicsPipelineKey
  {
  public:
    GraphicsPipelineKey() = default;
    
    operator bool() const noexcept
    {
      return id_ != 0;
    }

    [[nodiscard]] Fvog::GraphicsPipeline& GetPipeline() const
    {
      assert(pipelineManager_);
      auto& pipeline = pipelineManager_->graphicsPipelines_.at(id_).pipeline;
      assert(pipeline);
      return *pipeline;
    }

  private:
    friend class PipelineManager;
    GraphicsPipelineKey(uint64_t id, PipelineManager* pipelineManager)
      : id_(id), pipelineManager_(pipelineManager){}

    uint64_t id_{};
    PipelineManager* pipelineManager_{};
  };

  class ComputePipelineKey
  {
  public:
    ComputePipelineKey() = default;

    operator bool() const noexcept
    {
      return id_ != 0;
    }

    [[nodiscard]] Fvog::ComputePipeline& GetPipeline() const
    {
      assert(pipelineManager_);
      auto& pipeline = pipelineManager_->computePipelines_.at(id_).pipeline;
      assert(pipeline);
      return *pipeline;
    }

    //[[nodiscard]] bool QueryIsCompiled

  private:
    friend class PipelineManager;
    ComputePipelineKey(uint64_t id, PipelineManager* pipelineManager)
      : id_(id), pipelineManager_(pipelineManager){}

    uint64_t id_{};
    PipelineManager* pipelineManager_{};
  };

  struct ShaderModuleCreateInfo
  {
    bool operator==(const ShaderModuleCreateInfo&) const noexcept = default;
    Fvog::PipelineStage stage = Fvog::PipelineStage::COMPUTE_SHADER;
    std::filesystem::path path;
    // TODO: Defines, specialization stuff
  };

  struct ComputePipelineCreateInfo
  {
    std::string name = {};
    ShaderModuleCreateInfo shaderModuleInfo;
  };

  struct GraphicsPipelineState
  {
    Fvog::InputAssemblyState inputAssemblyState   = {};
    Fvog::RasterizationState rasterizationState   = {};
    Fvog::MultisampleState multisampleState       = {};
    Fvog::DepthState depthState                   = {};
    Fvog::StencilState stencilState               = {};
    Fvog::ColorBlendState colorBlendState         = {};
    Fvog::RenderTargetFormats renderTargetFormats = {};
  };

  struct GraphicsPipelineCreateInfo
  {
    std::string name = {};
    std::optional<ShaderModuleCreateInfo> vertexModuleInfo;
    std::optional<ShaderModuleCreateInfo> fragmentModuleInfo;
    GraphicsPipelineState state;
  };

  //GraphicsPipelineKey EnqueueCompileGraphicsPipeline();

  // Returns an opaque handle to a compute pipeline.
  [[nodiscard]] ComputePipelineKey EnqueueCompileComputePipeline(const ComputePipelineCreateInfo& createInfo);

  [[nodiscard]] GraphicsPipelineKey EnqueueCompileGraphicsPipeline(const GraphicsPipelineCreateInfo& createInfo);

  void EnqueueRecompileShader(const ShaderModuleCreateInfo& shaderInfo);

  void PollModifiedShaders();

  void EnqueueModifiedShaders();

  enum class Status
  {
    PENDING,
    SUCCESS,
    FAILED,
  };

  struct ShaderModuleValue
  {
    Status status = Status::PENDING;
    // Duplicate of map key, but I'm too dumb to figure out a cleaner solution (set won't work due to immutability constraint)
    ShaderModuleCreateInfo info;
    // TODO: file watcher?
    std::unique_ptr<Fvog::Shader> shader;
    std::filesystem::file_time_type lastWriteTime{};
    bool isOutOfDate = false; // If true, current shader is older than file contents
  };

  [[nodiscard]] std::vector<const ShaderModuleValue*> GetShaderModules() const;
  [[nodiscard]] std::vector<Fvog::GraphicsPipeline*> GetGraphicsPipelines() const;
  [[nodiscard]] std::vector<Fvog::ComputePipeline*> GetComputePipelines() const;

private:
  uint64_t nextId_ = 1;

  struct GraphicsPipelineValue
  {
    std::unique_ptr<Fvog::GraphicsPipeline> pipeline;
    std::vector<ShaderModuleCreateInfo> stages;
    GraphicsPipelineState state;
  };

  struct ComputePipelineValue
  {
    std::unique_ptr<Fvog::ComputePipeline> pipeline;
    ShaderModuleValue* shaderModuleValue;
  };

  ShaderModuleValue& EmplaceOrGetShaderModuleValue(const ShaderModuleCreateInfo& createInfo);

  //struct ShaderModuleValue;
  //struct GraphicsPipelineValue;
  //struct ComputePipelineValue;

  struct HashShaderModuleCreateInfo
  {
    std::size_t operator()(const ShaderModuleCreateInfo& s) const noexcept;
  };

  // Caches the compilation of shader modules
  std::unordered_map<ShaderModuleCreateInfo, ShaderModuleValue, HashShaderModuleCreateInfo> shaderModules_;
  std::unordered_map<uint64_t, GraphicsPipelineValue> graphicsPipelines_;
  std::unordered_map<uint64_t, ComputePipelineValue> computePipelines_;
};

void CreateGlobalPipelineManager();
[[nodiscard]] PipelineManager& GetPipelineManager();
void DestroyGlobalPipelineManager();
