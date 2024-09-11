#include "FrogRenderer2.h"

#include "RendererUtilities.h"

#include "shaders/Config.shared.h"

#include "Fvog/detail/Common.h"

#include <array>
#include "Pipelines2.h"


namespace Pipelines2
{
  Fvog::ComputePipeline CullMeshlets()
  {
    auto comp = LoadShaderWithIncludes2(Fvog::PipelineStage::COMPUTE_SHADER, "shaders/visbuffer/CullMeshlets.comp.glsl");

    return Fvog::ComputePipeline({
      .name = "Cull Meshlets",
      .shader = &comp,
    });
  }

  Fvog::ComputePipeline CullTriangles()
  {
    auto comp = LoadShaderWithIncludes2(Fvog::PipelineStage::COMPUTE_SHADER, "shaders/visbuffer/CullTriangles.comp.glsl");

    return Fvog::ComputePipeline({
      .name = "Cull Triangles",
      .shader = &comp,
    });
  }

  Fvog::ComputePipeline HzbCopy()
  {
    auto comp = LoadShaderWithIncludes2(Fvog::PipelineStage::COMPUTE_SHADER, "shaders/hzb/HZBCopy.comp.glsl");

    return Fvog::ComputePipeline({
      .name = "HZB Copy",
      .shader = &comp,
    });
  }

  Fvog::ComputePipeline HzbReduce()
  {
    auto comp = LoadShaderWithIncludes2(Fvog::PipelineStage::COMPUTE_SHADER, "shaders/hzb/HZBReduce.comp.glsl");

    return Fvog::ComputePipeline({
      .name = "HZB Reduce",
      .shader = &comp,
    });
  }

  Fvog::GraphicsPipeline Visbuffer(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/visbuffer/Visbuffer.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/Visbuffer.frag.glsl");

    return Fvog::GraphicsPipeline({
      .name = "Visbuffer",
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

  Fvog::GraphicsPipeline VisbufferResolve(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/visbuffer/VisbufferResolve.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/VisbufferResolve.frag.glsl");

    return Fvog::GraphicsPipeline({
      .name = "Visbuffer Resolve",
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .depthState = {.depthTestEnable = false, .depthWriteEnable = false},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline Shading(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/ShadeDeferredPbr.frag.glsl");

    return Fvog::GraphicsPipeline({
      .name = "Shading",
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::ComputePipeline Tonemap()
  {
    auto comp = LoadShaderWithIncludes2(Fvog::PipelineStage::COMPUTE_SHADER, "shaders/post/TonemapAndDither.comp.glsl");

    return Fvog::ComputePipeline({
      .name = "Tonemap and dither",
      .shader = &comp,
    });
  }

  Fvog::ComputePipeline CalibrateHdr()
  {
    auto comp = LoadShaderWithIncludes2(Fvog::PipelineStage::COMPUTE_SHADER, "shaders/CalibrateHdr.comp.glsl");

    return Fvog::ComputePipeline({
        .name   = "Calibrate HDR",
        .shader = &comp,
      });
  }

  Fvog::GraphicsPipeline DebugTexture(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/Texture.frag.glsl");

    return Fvog::GraphicsPipeline({
      .name = "Debug Texture",
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline ShadowMain(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/shadows/ShadowMain.vert.glsl");

    return Fvog::GraphicsPipeline({
      .name = "Shadow Main",
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

  Fvog::GraphicsPipeline ShadowVsm(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/shadows/ShadowMain.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/shadows/vsm/VsmShadow.frag.glsl");

    return Fvog::GraphicsPipeline({
      .name = "Shadow VSM",
      .vertexShader = &vs,
      .fragmentShader = &fs,
      //.rasterizationState = {.cullMode = VK_CULL_MODE_BACK_BIT},
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
#if VSM_USE_TEMP_ZBUFFER
      .depthState =
        {
          .depthTestEnable  = true,
          .depthWriteEnable = true,
        },
#endif
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline DebugLines(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/debug/Debug.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/VertexColor.frag.glsl");

    //auto positionBinding = Fvog::VertexInputBindingDescription{
    //  .location = 0,
    //  .binding = 0,
    //  .format = Fvog::Format::R32G32B32_FLOAT,
    //  .offset = offsetof(Debug::Line, aPosition),
    //};

    //auto colorBinding = Fvog::VertexInputBindingDescription{
    //  .location = 1,
    //  .binding = 0,
    //  .format = Fvog::Format::R32G32B32A32_SFLOAT,
    //  .offset = offsetof(Debug::Line, aColor),
    //};

    //auto bindings = {positionBinding, colorBinding};

    return Fvog::GraphicsPipeline({
      .name = "Debug Lines",
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

  Fvog::GraphicsPipeline DebugAabbs(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/debug/DebugAabb.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/VertexColor.frag.glsl");

    auto blend0 = Fvog::ColorBlendAttachmentState{
      .blendEnable = true,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    };

    auto blend1 = Fvog::ColorBlendAttachmentState{
      .blendEnable = false,
    };

    auto blends = {blend0, blend1};

    return Fvog::GraphicsPipeline({
      .name = "Indirect Debug AABBs",
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

  Fvog::GraphicsPipeline DebugRects(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/debug/DebugRect.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/VertexColor.frag.glsl");

    auto blend0 = Fvog::ColorBlendAttachmentState{
      .blendEnable = true,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    };

    auto blend1 = Fvog::ColorBlendAttachmentState{
      .blendEnable = false,
    };

    auto blends = {blend0, blend1};

    return Fvog::GraphicsPipeline({
      .name = "Indirect Debug Rects",
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

  Fvog::GraphicsPipeline ViewerVsm(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/viewer/VsmDebugPageTable.frag.glsl");

    return Fvog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline ViewerVsmPhysicalPages(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/viewer/VsmPhysicalPages.frag.glsl");

    return Fvog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline ViewerVsmBitmaskHzb(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/viewer/VsmBitmaskHzb.frag.glsl");

    return Fvog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::GraphicsPipeline ViewerVsmPhysicalPagesOverdraw(const Fvog::RenderTargetFormats& renderTargetFormats)
  {
    auto vs = LoadShaderWithIncludes2(Fvog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes2(Fvog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/viewer/VsmOverdrawHeatmap.frag.glsl");

    return Fvog::GraphicsPipeline({
      .vertexShader       = &vs,
      .fragmentShader     = &fs,
      .rasterizationState = {.cullMode = VK_CULL_MODE_NONE},
      .renderTargetFormats = renderTargetFormats,
    });
  }

  Fvog::RayTracingPipeline TestRayTracingPipeline()
  {
    auto rayGen = LoadShaderWithIncludes2(Fvog::PipelineStage::RAYGEN_SHADER, "shaders/raytracing/Test.rgen.glsl");
    return Fvog::RayTracingPipeline({
      .name = "Test RayTracingPipeline",
      .rayGenShader = &rayGen,
    });
  }
} // namespace Pipelines2