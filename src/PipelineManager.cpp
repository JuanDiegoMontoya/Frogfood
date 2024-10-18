#include "PipelineManager.h"
#include "Fvog/detail/Hash2.h"

#include "tracy/Tracy.hpp"

PipelineManager::ComputePipelineKey PipelineManager::EnqueueCompileComputePipeline(const ComputePipelineCreateInfo& createInfo)
{
  auto& shaderModuleValue = EmplaceOrGetShaderModuleValue(createInfo.shaderModuleInfo);

  auto myId = nextId_++;
  computePipelines_.emplace(myId,
    ComputePipelineValue{
      .pipeline = std::make_unique<Fvog::ComputePipeline>(
        // TODO: defer pipeline creation
        Fvog::ComputePipelineInfo{.name = createInfo.name, .shader = shaderModuleValue.shader.get()}),
      .shaderModuleValue = &shaderModuleValue,
    });
  return ComputePipelineKey{myId, this};
}

PipelineManager::GraphicsPipelineKey PipelineManager::EnqueueCompileGraphicsPipeline(const GraphicsPipelineCreateInfo& createInfo)
{
  ShaderModuleValue* fragmentModule{};
  ShaderModuleValue* vertexModule{};

  auto stages = std::vector<ShaderModuleCreateInfo>();

  if (createInfo.fragmentModuleInfo)
  {
    fragmentModule = &EmplaceOrGetShaderModuleValue(*createInfo.fragmentModuleInfo);
    stages.push_back(*createInfo.fragmentModuleInfo);
  }

  if (createInfo.vertexModuleInfo)
  {
    vertexModule = &EmplaceOrGetShaderModuleValue(*createInfo.vertexModuleInfo);
    stages.push_back(*createInfo.vertexModuleInfo);
  }

  auto myId = nextId_++;
  graphicsPipelines_.emplace(myId,
    GraphicsPipelineValue{
      .pipeline = std::make_unique<Fvog::GraphicsPipeline>(Fvog::GraphicsPipelineInfo{
        .name                = createInfo.name,
        .vertexShader        = vertexModule ? vertexModule->shader.get() : nullptr,
        .fragmentShader      = fragmentModule ? fragmentModule->shader.get() : nullptr,
        .inputAssemblyState  = createInfo.state.inputAssemblyState,
        .rasterizationState  = createInfo.state.rasterizationState,
        .multisampleState    = createInfo.state.multisampleState,
        .depthState          = createInfo.state.depthState,
        .stencilState        = createInfo.state.stencilState,
        .colorBlendState     = createInfo.state.colorBlendState,
        .renderTargetFormats = createInfo.state.renderTargetFormats,
      }),
      .stages   = std::move(stages),
      .state    = createInfo.state,
    });
  return GraphicsPipelineKey{myId, this};
}

void PipelineManager::EnqueueRecompileShader(const ShaderModuleCreateInfo& shaderInfo)
{
  ZoneScoped;

  try
  {
    auto& shaderModule = shaderModules_.at(shaderInfo);
    shaderModule.status = Status::PENDING;
    shaderModule.isOutOfDate = false;
    shaderModule.lastWriteTime = std::filesystem::directory_entry(shaderInfo.path).last_write_time();

    // TODO: defer, name (see below)

    try
    {
      auto newShader       = Fvog::Shader(shaderInfo.stage, shaderInfo.path);
      *shaderModule.shader = std::move(newShader);
      shaderModule.status  = Status::SUCCESS;
    }
    catch (std::exception&)
    {
      shaderModule.status = Status::FAILED;
      throw;
    }

    // Recompile all pipelines that use this shader
    for (auto& [_, v] : computePipelines_)
    {
      if (v.shaderModuleValue == &shaderModule)
      {
        try
        {
          auto newPipeline = Fvog::ComputePipeline({
            .name   = "temp", // TODO: reuse actual pipeline name
            .shader = shaderModule.shader.get(),
          });

          *v.pipeline = std::move(newPipeline);
        }
        catch (std::exception& e)
        {
          // TODO: invoke pipeline completion handler or something
          printf("Failed to compile compute pipeline. Reason: %s\n", e.what());
        }
      }
    }

    for (auto& [_, v] : graphicsPipelines_)
    {
      if (std::find(v.stages.begin(), v.stages.end(), shaderModule.info) != v.stages.end())
      {
        try
        {
          Fvog::Shader* vs{};
          Fvog::Shader* fs{};
          for (const auto& stage : v.stages)
          {
            switch (stage.stage)
            {
            case Fvog::PipelineStage::VERTEX_SHADER: vs = shaderModules_.at(stage).shader.get(); break;
            case Fvog::PipelineStage::FRAGMENT_SHADER: fs = shaderModules_.at(stage).shader.get(); break;
            default: assert(false);
            }
          }

          auto newPipeline = Fvog::GraphicsPipeline({
            .name                = "temp",
            .vertexShader        = vs,
            .fragmentShader      = fs,
            .inputAssemblyState  = v.state.inputAssemblyState,
            .rasterizationState  = v.state.rasterizationState,
            .multisampleState    = v.state.multisampleState,
            .depthState          = v.state.depthState,
            .stencilState        = v.state.stencilState,
            .colorBlendState     = v.state.colorBlendState,
            .renderTargetFormats = v.state.renderTargetFormats,
          });

          *v.pipeline = std::move(newPipeline);
        }
        catch (std::exception& e)
        {
          // TODO: invoke pipeline completion handler or something
          printf("Failed to compile compute pipeline. Reason: %s\n", e.what());
        }
      }
    }
  }
  catch(std::exception& e)
  {
    // TODO: invoke shader completion handler or something
    printf("Failed to compile shader. Reason: %s\n", e.what());
  }
}

void PipelineManager::PollModifiedShaders()
{
  ZoneScoped;

  for (auto& [_, shaderModule] : shaderModules_)
  {
    auto lastWriteTime = std::filesystem::directory_entry(shaderModule.info.path).last_write_time();
    if (shaderModule.lastWriteTime < lastWriteTime)
    {
      shaderModule.isOutOfDate = true;
    }
  }
}

void PipelineManager::EnqueueModifiedShaders()
{
  ZoneScoped;

  for (auto& [_, shaderModule] : shaderModules_)
  {
    if (shaderModule.isOutOfDate)
    {
      EnqueueRecompileShader(shaderModule.info);
    }
  }
}

std::vector<const PipelineManager::ShaderModuleValue*> PipelineManager::GetShaderModules() const
{
  ZoneScoped;

  auto modules = std::vector<const ShaderModuleValue*>();
  modules.reserve(shaderModules_.size());

  for (auto& [_, value] : shaderModules_)
  {
    modules.push_back(&value);
  }

  return modules;
}

std::vector<Fvog::GraphicsPipeline*> PipelineManager::GetGraphicsPipelines() const
{
  ZoneScoped;

  auto pipelines = std::vector<Fvog::GraphicsPipeline*>();
  pipelines.reserve(graphicsPipelines_.size());

  for (auto& [_, pipelineInfo] : graphicsPipelines_)
  {
    pipelines.push_back(pipelineInfo.pipeline.get());
  }

  return pipelines;
}

std::vector<Fvog::ComputePipeline*> PipelineManager::GetComputePipelines() const
{
  ZoneScoped;

  auto pipelines = std::vector<Fvog::ComputePipeline*>();
  pipelines.reserve(computePipelines_.size());

  for (auto& [_, pipelineInfo] : computePipelines_)
  {
    pipelines.push_back(pipelineInfo.pipeline.get());
  }

  return pipelines;
}

PipelineManager::ShaderModuleValue& PipelineManager::EmplaceOrGetShaderModuleValue(const ShaderModuleCreateInfo& createInfo)
{
  ZoneScoped;

  // TODO: defer shader creation
  if (auto it = shaderModules_.find(createInfo); it != shaderModules_.end())
  {
    return it->second;
  }
  
  auto shaderModule = ShaderModuleValue{
    .info = createInfo,
    // TODO: defer shader creation
    // TODO: pass name to shader (derive from path?)
  };
  try
  {
    shaderModule.shader = std::make_unique<Fvog::Shader>(createInfo.stage, createInfo.path);
    shaderModule.status = Status::SUCCESS;
    shaderModule.lastWriteTime = std::filesystem::directory_entry(createInfo.path).last_write_time();
  }
  catch (std::exception&)
  {
    shaderModule.status = Status::FAILED;
  }

  return shaderModules_.emplace(createInfo, std::move(shaderModule)).first->second;
}

std::size_t PipelineManager::HashShaderModuleCreateInfo::operator()(const ShaderModuleCreateInfo& s) const noexcept
{
  auto hashed = std::make_tuple(s.stage, s.path);

  return Fvog::detail::hashing::hash<decltype(hashed)>{}(hashed);
}
