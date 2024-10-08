#include "Pipeline2.h"

#include "Device.h"
#include "Shader2.h"
#include "detail/ApiToEnum2.h"
#include "detail/Common.h"

#include <volk.h>

#include <tracy/Tracy.hpp>

#include <array>
#include <cassert>
#include <utility>
#include <vector>
#include <memory>

namespace Fvog
{
  GraphicsPipeline::GraphicsPipeline(Device& device, VkPipelineLayout pipelineLayout, const GraphicsPipelineInfo& info)
    : device_(&device),
      name_(info.name)
  {
    using namespace detail;
    ZoneScoped;
    ZoneNamed(_, true);
    ZoneNameV(_, name_.data(), name_.size());

    auto stages = std::vector<VkPipelineShaderStageCreateInfo>();
    
    assert(info.vertexShader);
    stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = info.vertexShader->Handle(),
      .pName = "main",
    });

    if (info.fragmentShader)
    {
      stages.emplace_back(VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = info.fragmentShader->Handle(),
        .pName = "main",
      });
    }

    const std::array dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    assert(info.multisampleState.rasterizationSamples <= VK_SAMPLE_COUNT_32_BIT);

    // Default construct color blend states, then maybe override the first ones with explicit state
    auto colorBlendAttachmentStates = std::vector<VkPipelineColorBlendAttachmentState>(info.renderTargetFormats.colorAttachmentFormats.size(), ColorBlendAttachmentState{});
    assert(info.colorBlendState.attachments.size() <= info.renderTargetFormats.colorAttachmentFormats.size());
    for (size_t i = 0; i < info.colorBlendState.attachments.size(); i++)
    {
      colorBlendAttachmentStates[i] = {
        .blendEnable = info.colorBlendState.attachments[i].blendEnable,
        .srcColorBlendFactor = info.colorBlendState.attachments[i].srcColorBlendFactor,
        .dstColorBlendFactor = info.colorBlendState.attachments[i].dstColorBlendFactor,
        .colorBlendOp = info.colorBlendState.attachments[i].colorBlendOp,
        .srcAlphaBlendFactor = info.colorBlendState.attachments[i].srcAlphaBlendFactor,
        .dstAlphaBlendFactor = info.colorBlendState.attachments[i].dstAlphaBlendFactor,
        .alphaBlendOp = info.colorBlendState.attachments[i].alphaBlendOp,
        .colorWriteMask = info.colorBlendState.attachments[i].colorWriteMask,
      };
    }

    auto colorAttachmentFormatsVk = std::make_unique<VkFormat[]>(info.renderTargetFormats.colorAttachmentFormats.size());
    for (size_t i = 0; i < info.renderTargetFormats.colorAttachmentFormats.size(); i++)
    {
      colorAttachmentFormatsVk[i] = detail::FormatToVk(info.renderTargetFormats.colorAttachmentFormats[i]);
    }

    CheckVkResult(vkCreateGraphicsPipelines(
      device.device_,
      nullptr,
      1,
      Address(VkGraphicsPipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = Address(VkPipelineRenderingCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
          .colorAttachmentCount = static_cast<uint32_t>(info.renderTargetFormats.colorAttachmentFormats.size()),
          .pColorAttachmentFormats = colorAttachmentFormatsVk.get(),
          .depthAttachmentFormat = detail::FormatToVk(info.renderTargetFormats.depthAttachmentFormat),
          .stencilAttachmentFormat = detail::FormatToVk(info.renderTargetFormats.stencilAttachmentFormat),
        }),
        .stageCount = (uint32_t)stages.size(),
        .pStages = stages.data(),
        .pVertexInputState = Address(VkPipelineVertexInputStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        }),
        .pInputAssemblyState = Address(VkPipelineInputAssemblyStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
          .topology = info.inputAssemblyState.topology,
          .primitiveRestartEnable = info.inputAssemblyState.primitiveRestartEnable,
        }),
        .pViewportState = Address(VkPipelineViewportStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
          .viewportCount = 1,
          .pViewports = nullptr, // VK_DYNAMIC_STATE_VIEWPORT
          .scissorCount = 1,
          .pScissors = nullptr, // VK_DYNAMIC_STATE_SCISSOR
        }),
        .pRasterizationState = Address(VkPipelineRasterizationStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
          .depthClampEnable = info.rasterizationState.depthClampEnable,
          .polygonMode = info.rasterizationState.polygonMode,
          .cullMode = info.rasterizationState.cullMode,
          .frontFace = info.rasterizationState.frontFace,
          .depthBiasEnable = info.rasterizationState.depthBiasEnable,
          .depthBiasConstantFactor = info.rasterizationState.depthBiasConstantFactor,
          .depthBiasSlopeFactor = info.rasterizationState.depthBiasSlopeFactor,
          .lineWidth = info.rasterizationState.lineWidth,
        }),
        .pMultisampleState = Address(VkPipelineMultisampleStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
          .rasterizationSamples = info.multisampleState.rasterizationSamples,
          .minSampleShading = info.multisampleState.minSampleShading,
          .pSampleMask = &info.multisampleState.sampleMask,
          .alphaToCoverageEnable = info.multisampleState.alphaToCoverageEnable,
          .alphaToOneEnable = info.multisampleState.alphaToOneEnable,
        }),
        .pDepthStencilState = Address(VkPipelineDepthStencilStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
          .depthTestEnable = info.depthState.depthTestEnable,
          .depthWriteEnable = info.depthState.depthWriteEnable,
          .depthCompareOp = info.depthState.depthCompareOp,
          .stencilTestEnable = info.stencilState.stencilTestEnable,
          .front = info.stencilState.front,
          .back = info.stencilState.back,
        }),
        .pColorBlendState = Address(VkPipelineColorBlendStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
          .logicOpEnable = info.colorBlendState.logicOpEnable,
          .logicOp = info.colorBlendState.logicOp,
          .attachmentCount = static_cast<uint32_t>(colorBlendAttachmentStates.size()),
          .pAttachments = colorBlendAttachmentStates.data(),
          .blendConstants = {info.colorBlendState.blendConstants[0],
                             info.colorBlendState.blendConstants[1],
                             info.colorBlendState.blendConstants[2],
                             info.colorBlendState.blendConstants[3]},
        }),
        .pDynamicState = Address(VkPipelineDynamicStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
          .dynamicStateCount = (uint32_t)dynamicStates.size(),
          .pDynamicStates = dynamicStates.data(),
        }),
        .layout = pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
      }),
      nullptr,
      &pipeline_));
    
    // TODO: gate behind compile-time switch
    vkSetDebugUtilsObjectNameEXT(device_->device_, detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_PIPELINE,
      .objectHandle = reinterpret_cast<uint64_t>(pipeline_),
      .pObjectName = name_.data(),
    }));
  }

  GraphicsPipeline::GraphicsPipeline(Device& device, const GraphicsPipelineInfo& info)
    : GraphicsPipeline(device, device.defaultPipelineLayout, info)
  {}

  GraphicsPipeline::~GraphicsPipeline()
  {
    // TODO: put this into a queue for delayed deletion
    if (device_)
    {
      device_->genericDeletionQueue_.emplace_back(
        [device = device_, pipeline = pipeline_, frameOfLastUse = device_->frameNumber](uint64_t value) -> bool {
          if (value >= frameOfLastUse)
          {
            vkDestroyPipeline(device->device_, pipeline, nullptr);
            return true;
          }
          return false;
        });
    }
  }

  GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& old) noexcept
    : device_(std::exchange(old.device_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(old.pipeline_, VK_NULL_HANDLE)),
      name_(std::move(old.name_))
  {}

  GraphicsPipeline& GraphicsPipeline::operator=(GraphicsPipeline&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~GraphicsPipeline();
    return *new (this) GraphicsPipeline(std::move(old));
  }

  ComputePipeline::ComputePipeline(Device& device, VkPipelineLayout pipelineLayout, const ComputePipelineInfo& info)
    : device_(&device),
      workgroupSize_(info.shader->WorkgroupSize()),
      name_(info.name)
  {
    using namespace detail;
    ZoneScoped;
    ZoneNamed(_, true);
    ZoneNameV(_, name_.data(), name_.size());

    CheckVkResult(vkCreateComputePipelines(
      device_->device_,
      nullptr,
      1,
      Address(VkComputePipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_COMPUTE_BIT,
          .module = info.shader->Handle(),
          .pName = "main",
        },
        .layout = pipelineLayout,
      }),
      nullptr,
      &pipeline_));
    
    // TODO: gate behind compile-time switch
    vkSetDebugUtilsObjectNameEXT(device_->device_, detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_PIPELINE,
      .objectHandle = reinterpret_cast<uint64_t>(pipeline_),
      .pObjectName = name_.data(),
    }));
  }

  ComputePipeline::ComputePipeline(Device& device, const ComputePipelineInfo& info)
    : ComputePipeline(device, device.defaultPipelineLayout, info)
  {}

  ComputePipeline::~ComputePipeline()
  {
    // TODO: put this into a queue for delayed deletion
    if (device_)
    {
      vkDestroyPipeline(device_->device_, pipeline_, nullptr);
    }
  }

  ComputePipeline::ComputePipeline(ComputePipeline&& old) noexcept
    : device_(std::exchange(old.device_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(old.pipeline_, VK_NULL_HANDLE)),
      workgroupSize_(std::exchange(old.workgroupSize_, {})),
      name_(std::move(old.name_))
  {}

  ComputePipeline& ComputePipeline::operator=(ComputePipeline&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~ComputePipeline();
    return *new (this) ComputePipeline(std::move(old));
  }

  Extent3D ComputePipeline::WorkgroupSize() const
  {
    return workgroupSize_;
  }
} // namespace Fvog