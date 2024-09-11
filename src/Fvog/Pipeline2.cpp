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
  GraphicsPipeline::GraphicsPipeline(VkPipelineLayout pipelineLayout, const GraphicsPipelineInfo& info)
    : name_(info.name)
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

    CheckVkResult(vkCreateGraphicsPipelines(Fvog::GetDevice().device_,
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
    vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
      detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_PIPELINE,
      .objectHandle = reinterpret_cast<uint64_t>(pipeline_),
      .pObjectName = name_.data(),
    }));
  }

  GraphicsPipeline::GraphicsPipeline(const GraphicsPipelineInfo& info)
    : GraphicsPipeline(Fvog::GetDevice().defaultPipelineLayout, info)
  {}

  GraphicsPipeline::~GraphicsPipeline()
  {
    // TODO: put this into a queue for delayed deletion
    if (pipeline_ != VK_NULL_HANDLE)
    {
      Fvog::GetDevice().genericDeletionQueue_.emplace_back(
        [pipeline = pipeline_, frameOfLastUse = Fvog::GetDevice().frameNumber](uint64_t value) -> bool
        {
          if (value >= frameOfLastUse)
          {
            vkDestroyPipeline(Fvog::GetDevice().device_, pipeline, nullptr);
            return true;
          }
          return false;
        });
    }
  }

  GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& old) noexcept
    : pipeline_(std::exchange(old.pipeline_, VK_NULL_HANDLE)),
      name_(std::move(old.name_))
  {}

  GraphicsPipeline& GraphicsPipeline::operator=(GraphicsPipeline&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~GraphicsPipeline();
    return *new (this) GraphicsPipeline(std::move(old));
  }

  ComputePipeline::ComputePipeline(VkPipelineLayout pipelineLayout, const ComputePipelineInfo& info)
    : workgroupSize_(info.shader->WorkgroupSize()),
      name_(info.name)
  {
    using namespace detail;
    ZoneScoped;
    ZoneNamed(_, true);
    ZoneNameV(_, name_.data(), name_.size());

    CheckVkResult(vkCreateComputePipelines(Fvog::GetDevice().device_,
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
    vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
      detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_PIPELINE,
      .objectHandle = reinterpret_cast<uint64_t>(pipeline_),
      .pObjectName = name_.data(),
    }));
  }

  ComputePipeline::ComputePipeline(const ComputePipelineInfo& info)
    : ComputePipeline(Fvog::GetDevice().defaultPipelineLayout, info)
  {}

  ComputePipeline::~ComputePipeline()
  {
    // TODO: put this into a queue for delayed deletion
    if (pipeline_ != VK_NULL_HANDLE)
    {
      vkDestroyPipeline(Fvog::GetDevice().device_, pipeline_, nullptr);
    }
  }

  ComputePipeline::ComputePipeline(ComputePipeline&& old) noexcept
    : pipeline_(std::exchange(old.pipeline_, VK_NULL_HANDLE)),
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

  RayTracingPipeline::RayTracingPipeline(VkPipelineLayout pipelineLayout, const RayTracingPipelineInfo& info)
    : name_(info.name)
  {
    using namespace detail;
    ZoneScoped;
    ZoneNamed(_, true);
    ZoneNameV(_, name_.data(), name_.size());

    assert(info.rayGenShader);

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> rayTracingGroups;

    stages.emplace_back(VkPipelineShaderStageCreateInfo{
      .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
      .module = info.rayGenShader->Handle(),
      .pName  = "main",
    });
    rayTracingGroups.emplace_back(VkRayTracingShaderGroupCreateInfoKHR{
      .sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
      .type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
      .generalShader      = static_cast<uint32_t>(stages.size() - 1),
      .closestHitShader   = VK_SHADER_UNUSED_KHR,
      .anyHitShader       = VK_SHADER_UNUSED_KHR,
      .intersectionShader = VK_SHADER_UNUSED_KHR,
    });

    for (const auto& missShader : info.missShaders)
    {
      stages.emplace_back(VkPipelineShaderStageCreateInfo{
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = VK_SHADER_STAGE_MISS_BIT_KHR,
        .module = missShader->Handle(),
        .pName  = "main",
      });
      rayTracingGroups.emplace_back(VkRayTracingShaderGroupCreateInfoKHR{
        .sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader      = static_cast<uint32_t>(stages.size() - 1),
        .closestHitShader   = VK_SHADER_UNUSED_KHR,
        .anyHitShader       = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
      });
    }

    for (const auto& [closestHit, anyHit, intersection] : info.hitGroups)
    {
      // If we have a hit group, we must have a closest hit shader
      assert(closestHit);
      VkRayTracingShaderGroupCreateInfoKHR hitGroupInfo{
        .sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
        .generalShader      = VK_SHADER_UNUSED_KHR,
        .closestHitShader   = VK_SHADER_UNUSED_KHR,
        .anyHitShader       = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
      };

      stages.emplace_back(VkPipelineShaderStageCreateInfo{
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        .module = closestHit->Handle(),
        .pName  = "main",
      });
      hitGroupInfo.closestHitShader = static_cast<uint32_t>(stages.size() - 1);

      if (anyHit)
      {
        stages.emplace_back(VkPipelineShaderStageCreateInfo{
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
          .module = anyHit->Handle(),
          .pName  = "main",
        });
        hitGroupInfo.anyHitShader = static_cast<uint32_t>(stages.size() - 1);
      }
      if (intersection)
      {
        stages.emplace_back(VkPipelineShaderStageCreateInfo{
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
          .module = intersection->Handle(),
          .pName  = "main",
        });
        hitGroupInfo.intersectionShader = static_cast<uint32_t>(stages.size() - 1);
      }
      rayTracingGroups.emplace_back(hitGroupInfo);
    }

    CheckVkResult(vkCreateRayTracingPipelinesKHR(Fvog::GetDevice().device_,
      nullptr,
      nullptr,
      1,
      Address(VkRayTracingPipelineCreateInfoKHR{
        .sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .stageCount                   = static_cast<uint32_t>(stages.size()),
        .pStages                      = stages.data(),
        .groupCount                   = static_cast<uint32_t>(rayTracingGroups.size()),
        .pGroups                      = rayTracingGroups.data(),
        .maxPipelineRayRecursionDepth = 1, // TODO: put this in CreateInfo?
        .layout                       = pipelineLayout,
      }),
      nullptr,
      &pipeline_));

    vkSetDebugUtilsObjectNameEXT(Fvog::GetDevice().device_,
      detail::Address(VkDebugUtilsObjectNameInfoEXT{
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType   = VK_OBJECT_TYPE_PIPELINE,
        .objectHandle = reinterpret_cast<uint64_t>(pipeline_),
        .pObjectName  = name_.data(),
      }));

    // Fetch ray tracing properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
    };
    vkGetPhysicalDeviceProperties2(Fvog::GetDevice().physicalDevice_,
      Address(VkPhysicalDeviceProperties2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &rayTracingProperties,
      }));

    const auto shaderHandleAlignment     = rayTracingProperties.shaderGroupHandleAlignment;
    const auto shaderHandleBaseAlignment = rayTracingProperties.shaderGroupBaseAlignment;
    const auto shaderHandleSize          = AlignUp(rayTracingProperties.shaderGroupHandleSize, shaderHandleAlignment);
    const auto shaderHandleBaseSize      = AlignUp(rayTracingProperties.shaderGroupHandleSize, shaderHandleBaseAlignment);

    // We don't want the aligned size here as the data returned is packed
    std::vector<uint8_t> shaderGroupHandles(rayTracingGroups.size() * rayTracingProperties.shaderGroupHandleSize);
    CheckVkResult(vkGetRayTracingShaderGroupHandlesKHR(Fvog::GetDevice().device_,
      pipeline_,
      0,
      static_cast<uint32_t>(rayTracingGroups.size()),
      shaderGroupHandles.size(),
      shaderGroupHandles.data()));

    // RayGen shader is the base, followed by Miss shaders, then Hit groups, therefore we need aligned base size here
    VkStridedDeviceAddressRegionKHR rayGenRegion{
      .stride = shaderHandleBaseSize,
      // The size of the raygen region must be equal to its stride
      .size = shaderHandleBaseSize,
    };

    VkStridedDeviceAddressRegionKHR missRegion{
      // Non-base aligned stride
      .stride = shaderHandleSize,
      // Base aligned size
      .size = AlignUp(shaderHandleSize * info.missShaders.size(), shaderHandleBaseAlignment),
    };

    VkStridedDeviceAddressRegionKHR hitGroupRegion{
      // Non-base aligned stride
      .stride = shaderHandleSize,
      // Base aligned size
      .size = AlignUp(shaderHandleSize * info.hitGroups.size(), shaderHandleBaseAlignment),
    };

    Buffer shaderBindingTableBuffer(
      BufferCreateInfo{
        .size = rayGenRegion.size + missRegion.size + hitGroupRegion.size,
        .flag = BufferFlagThingy::NO_DESCRIPTOR | BufferFlagThingy::MAP_SEQUENTIAL_WRITE_DEVICE,
      },
      info.name + "ShaderBindingTable");

    auto* shaderHandleBasePtr       = shaderGroupHandles.data();
    auto* shaderBindingTableBasePtr = static_cast<uint8_t*>(shaderBindingTableBuffer.GetMappedMemory());

    // Write RayGen shader
    std::memcpy(shaderBindingTableBasePtr, shaderHandleBasePtr, shaderHandleSize);
    shaderBindingTableBasePtr += rayGenRegion.size;
    shaderHandleBasePtr += shaderHandleSize;
    rayGenRegion.deviceAddress = shaderBindingTableBuffer.GetDeviceAddress();

    // Write Miss shaders
    if (!info.missShaders.empty())
    {
      std::memcpy(shaderBindingTableBasePtr, shaderHandleBasePtr, shaderHandleSize);
      shaderBindingTableBasePtr += missRegion.size;
      shaderHandleBasePtr += shaderHandleSize * info.missShaders.size();
    }
    missRegion.deviceAddress = rayGenRegion.deviceAddress + rayGenRegion.size;

    // Write Hit groups
    if (!info.hitGroups.empty())
    {
      std::memcpy(shaderBindingTableBasePtr, shaderHandleBasePtr, shaderHandleSize);
    }
    hitGroupRegion.deviceAddress = rayGenRegion.deviceAddress + rayGenRegion.size + missRegion.size;

    shaderBindingTable_.buffer         = std::move(shaderBindingTableBuffer);
    shaderBindingTable_.rayGenRegion   = rayGenRegion;
    shaderBindingTable_.missRegion     = missRegion;
    shaderBindingTable_.hitGroupRegion = hitGroupRegion;
  }

  RayTracingPipeline::RayTracingPipeline(const RayTracingPipelineInfo& info)
    : RayTracingPipeline(Fvog::GetDevice().defaultPipelineLayout, info)
  {}

  RayTracingPipeline::~RayTracingPipeline()
  {
    // TODO: put this into a queue for delayed deletion
    if (pipeline_ != VK_NULL_HANDLE)
    {
      vkDestroyPipeline(Fvog::GetDevice().device_, pipeline_, nullptr);
    }
  }

  RayTracingPipeline::RayTracingPipeline(RayTracingPipeline&& old) noexcept
    : pipeline_(std::exchange(old.pipeline_, VK_NULL_HANDLE)),
      shaderBindingTable_(std::move(old.shaderBindingTable_)),
      name_(std::move(old.name_))
  {
  }

  RayTracingPipeline& RayTracingPipeline::operator=(RayTracingPipeline&& old) noexcept
  {
    if (&old == this)
    {
      return *this;
    }
    this->~RayTracingPipeline();
    return *new (this) RayTracingPipeline(std::move(old));
  }
} // namespace Fvog