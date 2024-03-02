#include "Pipelines.h"

#include "FrogRenderer2.h"

#include "RendererUtilities.h"

#include "shaders/Config.shared.h"

#include "Fvog/detail/Common.h"

#include <array>
#include "Pipelines2.h"


namespace Pipelines2
{
  static VkPipelineLayout pipelineLayout{};

  void InitPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout)
  {
    Fvog::detail::CheckVkResult(
      vkCreatePipelineLayout(
        device,
        Fvog::detail::Address(VkPipelineLayoutCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = 1,
          .pSetLayouts = &descriptorSetLayout,
          .pushConstantRangeCount = 1,
          .pPushConstantRanges = Fvog::detail::Address(VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_ALL,
            .offset = 0,
            .size = 128,
          }),
        }),
        nullptr,
        &pipelineLayout));
  }

  void DestroyPipelineLayout(VkDevice device)
  {
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  }

  Fvog::ComputePipeline CullMeshlets(VkDevice device)
  {
    auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/visbuffer/CullMeshlets.comp.glsl");

    return Fvog::ComputePipeline(device, pipelineLayout, {
      .shader = &comp,
    });
  }

  Fvog::ComputePipeline CullTriangles(VkDevice device)
  {
    auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/visbuffer/CullTriangles.comp.glsl");

    return Fvog::ComputePipeline(device, pipelineLayout, {
      .shader = &comp,
    });
  }

  Fvog::ComputePipeline HzbCopy(VkDevice device)
  {
    auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/hzb/HZBCopy.comp.glsl");

    return Fvog::ComputePipeline(device, pipelineLayout, {
      .name = "HZB Copy",
      .shader = &comp,
    });
  }

  Fvog::ComputePipeline HzbReduce(VkDevice device)
  {
    auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/hzb/HZBReduce.comp.glsl");

    return Fvog::ComputePipeline(device, pipelineLayout, {
      .name = "HZB Reduce",
      .shader = &comp,
    });
  }

  Fvog::GraphicsPipeline Visbuffer(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/visbuffer/Visbuffer.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/Visbuffer.frag.glsl");

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      // TODO: "temp" until more material types are supported (transparent, opaque, masked, and two-sided versions of each)
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .depthState =
        {
          .depthTestEnable = true,
          .depthWriteEnable = true,
          .depthCompareOp = FVOG_COMPARE_OP_NEARER,
        },
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline MaterialDepth(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/VisbufferMaterialDepth.frag.glsl");

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .depthState = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = VK_COMPARE_OP_ALWAYS},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline VisbufferResolve(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/visbuffer/VisbufferResolve.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/VisbufferResolve.frag.glsl");

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .depthState = {.depthTestEnable = true, .depthWriteEnable = false, .depthCompareOp = VK_COMPARE_OP_EQUAL},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline Shading(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/ShadeDeferredPbr.frag.glsl");

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::ComputePipeline Tonemap(VkDevice device)
  {
    auto comp = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/TonemapAndDither.comp.glsl");

    return Fvog::ComputePipeline(device, pipelineLayout, {
      .name = "Image Formation",
      .shader = &comp,
    });
  }

  Fvog::GraphicsPipeline DebugTexture(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/Texture.frag.glsl");

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline ShadowMain(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/shadows/ShadowMain.vert.glsl");

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = nullptr,
      .rasterizationState = {.cullMode = VK_CULL_MODE_BACK_BIT},
      .depthState =
        {
          .depthTestEnable = true,
          .depthWriteEnable = true,
        },
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline ShadowVsm(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/shadows/ShadowMain.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/shadows/vsm/VsmShadow.frag.glsl");

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_BACK_BIT},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline DebugLines(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/debug/Debug.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/VertexColor.frag.glsl");

    //auto positionBinding = Fvog::VertexInputBindingDescription{
    //  .location = 0,
    //  .binding = 0,
    //  .format = Fvog::Format::R32G32B32_FLOAT,
    //  .offset = offsetof(Debug::Line, aPosition),
    //};

    //auto colorBinding = Fvog::VertexInputBindingDescription{
    //  .location = 1,
    //  .binding = 0,
    //  .format = Fvog::Format::R32G32B32A32_FLOAT,
    //  .offset = offsetof(Debug::Line, aColor),
    //};

    //auto bindings = {positionBinding, colorBinding};

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST},
      //.vertexInputState = bindings,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE, .lineWidth = 2},
      .depthState =
        {
          .depthTestEnable = true,
          .depthWriteEnable = false,
          .depthCompareOp = FVOG_COMPARE_OP_NEARER,
        },
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline DebugAabbs(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/debug/DebugAabb.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/VertexColor.frag.glsl");

    auto blend0 = Fvog::ColorBlendAttachmentState{
      .blendEnable = true,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    };

    auto blend1 = Fvog::ColorBlendAttachmentState{
      .blendEnable = false,
    };

    auto blends = {blend0, blend1};

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP},
      .rasterizationState =
        {
          .polygonMode = VK_POLYGON_MODE_LINE,
          .cullMode = VK_CULL_MODE_NONE,
          .depthBiasEnable = true,
          .depthBiasConstantFactor = 50.0f,
        },
      .depthState =
        {
          .depthTestEnable = true,
          .depthWriteEnable = false,
          .depthCompareOp = FVOG_COMPARE_OP_NEARER,
        },
      .colorBlendState =
        {
          .attachments = blends,
        },
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline DebugRects(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/debug/DebugRect.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/VertexColor.frag.glsl");

    auto blend0 = Fvog::ColorBlendAttachmentState{
      .blendEnable = true,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    };

    auto blend1 = Fvog::ColorBlendAttachmentState{
      .blendEnable = false,
    };

    auto blends = {blend0, blend1};

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .inputAssemblyState = {.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN},
      .rasterizationState =
        {
          .polygonMode = VK_POLYGON_MODE_FILL,
          .cullMode = VK_CULL_MODE_NONE,
        },
      .depthState =
        {
          .depthTestEnable = true,
          .depthWriteEnable = false,
          .depthCompareOp = FVOG_COMPARE_OP_NEARER,
        },
      .colorBlendState =
        {
          .attachments = blends,
        },
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline ViewerVsm(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/viewer/VsmDebugPageTable.frag.glsl");

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline ViewerVsmPhysicalPages(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/viewer/VsmPhysicalPages.frag.glsl");

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline ViewerVsmBitmaskHzb(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/viewer/VsmBitmaskHzb.frag.glsl");

    return Fvog::GraphicsPipeline(device, pipelineLayout, {
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }
} // namespace Pipelines2