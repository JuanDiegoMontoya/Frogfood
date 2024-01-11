#pragma once

#include <volk.h>
#include "BasicTypes2.h"

#include <span>
#include <string_view>

namespace Fvog
{
  // clang-format off
  class Shader;

  struct InputAssemblyState
  {
    VkPrimitiveTopology topology  = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool primitiveRestartEnable = false;
  };

  //struct VertexInputBindingDescription
  //{
  //  uint32_t location;
  //  uint32_t binding;
  //  VkFormat format;
  //  uint32_t offset;
  //};

  //struct VertexInputState
  //{
  //  std::span<const VertexInputBindingDescription> vertexBindingDescriptions = {};
  //};

  struct RasterizationState
  {
    bool depthClampEnable         = false;
    VkPolygonMode polygonMode     = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode      = VK_CULL_MODE_NONE;
    VkFrontFace frontFace         = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool depthBiasEnable          = false;
    float depthBiasConstantFactor = 0;
    float depthBiasSlopeFactor    = 0;
    float lineWidth               = 1;
    //float pointSize               = 1; // point size must be set in the VS
  };

  struct MultisampleState
  {
    VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    bool sampleShadingEnable                   = false;
    float minSampleShading                     = 1;
    uint32_t sampleMask                        = 0xFFFFFFFF;
    bool alphaToCoverageEnable                 = false;
    bool alphaToOneEnable                      = false;
  };

  struct DepthState
  {
    bool depthTestEnable       = false;
    bool depthWriteEnable      = false;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
  };

  //struct StencilOpState
  //{
  //  VkStencilOp passOp      = VK_STENCIL_OP_KEEP;
  //  VkStencilOp failOp      = VK_STENCIL_OP_KEEP;
  //  VkStencilOp depthFailOp = VK_STENCIL_OP_KEEP;
  //  VkCompareOp compareOp   = VK_COMPARE_OP_ALWAYS;
  //  uint32_t compareMask  = 0;
  //  uint32_t writeMask    = 0;
  //  uint32_t reference    = 0;

  //  bool operator==(const StencilOpState&) const noexcept = default;
  //};

  struct StencilState
  {
    bool stencilTestEnable = false;
    VkStencilOpState front   = {};
    VkStencilOpState back    = {};
  };

  struct ColorBlendAttachmentState
  {
    bool blendEnable = false;
    VkBlendFactor srcColorBlendFactor    = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColorBlendFactor    = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorBlendOp               = VK_BLEND_OP_ADD;
    VkBlendFactor srcAlphaBlendFactor    = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlendFactor    = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaBlendOp               = VK_BLEND_OP_ADD;
    VkColorComponentFlags colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    bool operator==(const ColorBlendAttachmentState&) const noexcept = default;
    operator VkPipelineColorBlendAttachmentState() const
    {
      return {
        .blendEnable = blendEnable,
        .srcColorBlendFactor = srcColorBlendFactor,
        .dstColorBlendFactor = dstColorBlendFactor,
        .colorBlendOp = colorBlendOp,
        .srcAlphaBlendFactor = srcAlphaBlendFactor,
        .dstAlphaBlendFactor = dstAlphaBlendFactor,
        .alphaBlendOp = alphaBlendOp,
        .colorWriteMask = colorWriteMask,
      };
    }
  };
  
  struct ColorBlendState
  {
    bool logicOpEnable                                     = false;
    VkLogicOp logicOp                                      = VK_LOGIC_OP_COPY;
    std::span<const ColorBlendAttachmentState> attachments = {};
    float blendConstants[4]                                = { 0, 0, 0, 0 };
  };

  struct RenderTargetFormats
  {
    std::span<const VkFormat> colorAttachmentFormats{};
    VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    VkFormat stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
  };

  /// @brief Parameters for the constructor of GraphicsPipeline
  struct GraphicsPipelineInfo
  {
    /// @brief An optional name for viewing in a graphics debugger
    std::string_view name;

    /// @brief Non-null pointer to a vertex shader
    const Shader* vertexShader            = nullptr;

    /// @brief Optional pointer to a fragment shader
    const Shader* fragmentShader          = nullptr;

    InputAssemblyState inputAssemblyState   = {};
    //VertexInputState vertexInputState       = {};
    RasterizationState rasterizationState   = {};
    MultisampleState multisampleState       = {};
    DepthState depthState                   = {};
    StencilState stencilState               = {};
    ColorBlendState colorBlendState         = {};
    RenderTargetFormats renderTargetFormats = {};
  };

  /// @brief Parameters for the constructor of ComputePipeline
  struct ComputePipelineInfo
  {
    /// @brief An optional name for viewing in a graphics debugger
    std::string_view name;

    /// @brief Non-null pointer to a compute shader
    const Shader* shader;
  };

  /// @brief An object that encapsulates the state needed to issue draws
  struct GraphicsPipeline
  {
    /// @throws PipelineCompilationException
    explicit GraphicsPipeline(VkDevice device, VkPipelineLayout pipelineLayout, const GraphicsPipelineInfo& info);
    ~GraphicsPipeline();
    GraphicsPipeline(GraphicsPipeline&& old) noexcept;
    GraphicsPipeline& operator=(GraphicsPipeline&& old) noexcept;
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    bool operator==(const GraphicsPipeline&) const = default;

    /// @brief Gets the handle of the underlying OpenGL program object
    /// @return The program
    [[nodiscard]] VkPipeline Handle() const
    {
      return pipeline_;
    }

  private:
    VkDevice device_;
    VkPipeline pipeline_{};
  };

  /// @brief An object that encapsulates the state needed to issue dispatches
  struct ComputePipeline
  {
    /// @throws PipelineCompilationException
    explicit ComputePipeline(VkDevice device, VkPipelineLayout pipelineLayout, const ComputePipelineInfo& info);
    ~ComputePipeline();
    ComputePipeline(ComputePipeline&& old) noexcept;
    ComputePipeline& operator=(ComputePipeline&& old) noexcept;
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    bool operator==(const ComputePipeline&) const = default;
    
    [[nodiscard]] Extent3D WorkgroupSize() const;
    
    /// @brief Gets the handle of the underlying OpenGL program object
    /// @return The program
    [[nodiscard]] VkPipeline Handle() const
    {
      return pipeline_;
    }

  private:
    VkDevice device_;
    VkPipeline pipeline_{};
    Extent3D workgroupSize_;
  };
  // clang-format on
} // namespace Fvog