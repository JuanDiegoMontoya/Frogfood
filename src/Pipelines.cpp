#include "Pipelines.h"

#include "RendererUtilities.h"

// TODO: remove
#include "SceneLoader.h"
#include "FrogRenderer.h"

#include "shaders/Config.shared.h"

#include <array>

namespace Pipelines
{
  Fwog::ComputePipeline MeshletGenerate()
  {
    auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/visbuffer/Visbuffer.comp.glsl");

    return Fwog::ComputePipeline({
      .shader = &comp,
    });
  }

  Fwog::ComputePipeline HzbCopy()
  {
    auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/hzb/HZBCopy.comp.glsl");

    return Fwog::ComputePipeline({
      .name = "HZB Copy",
      .shader = &comp,
    });
  }

  Fwog::ComputePipeline HzbReduce()
  {
    auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/hzb/HZBReduce.comp.glsl");

    return Fwog::ComputePipeline({
      .name = "HZB Reduce",
      .shader = &comp,
    });
  }

  Fwog::GraphicsPipeline Visbuffer()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/visbuffer/Visbuffer.vert.glsl");
    auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/Visbuffer.frag.glsl");

    return Fwog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = Fwog::CullMode::BACK},
      .depthState =
        {
          .depthTestEnable = true,
          .depthWriteEnable = true,
          .depthCompareOp = FWOG_COMPARE_OP_NEARER,
        },
    });
  }

  Fwog::GraphicsPipeline MaterialDepth()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/VisbufferMaterialDepth.frag.glsl");

    return Fwog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .vertexInputState = {},
      .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
      .depthState = {.depthTestEnable = true, .depthWriteEnable = true, .depthCompareOp = Fwog::CompareOp::ALWAYS},
    });
  }

  Fwog::GraphicsPipeline VisbufferResolve()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/visbuffer/VisbufferResolve.vert.glsl");
    auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/visbuffer/VisbufferResolve.frag.glsl");

    return Fwog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .vertexInputState = {},
      .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
      .depthState = {.depthTestEnable = true, .depthWriteEnable = false, .depthCompareOp = Fwog::CompareOp::EQUAL},
    });
  }

  /*static Fwog::GraphicsPipeline CreateShadowPipeline()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/SceneDeferredPbr.vert.glsl");
    auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/RSMScenePbr.frag.glsl");

    return Fwog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .vertexInputState = {sceneInputBindingDescs},
      .depthState = {.depthTestEnable = true, .depthWriteEnable = true},
    });
  }*/

  Fwog::GraphicsPipeline Shading()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/ShadeDeferredPbr.frag.glsl");

    return Fwog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
    });
  }

  Fwog::ComputePipeline Tonemap()
  {
    auto comp = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/TonemapAndDither.comp.glsl");

    return Fwog::ComputePipeline({
      .name = "Image Formation",
      .shader = &comp,
    });
  }

  Fwog::GraphicsPipeline DebugTexture()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/FullScreenTri.vert.glsl");
    auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/Texture.frag.glsl");

    return Fwog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = Fwog::CullMode::NONE},
    });
  }

  Fwog::GraphicsPipeline ShadowMain()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/shadows/ShadowMain.vert.glsl");

    return Fwog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = nullptr,
      .rasterizationState = {.cullMode = Fwog::CullMode::BACK},
      .depthState =
        {
          .depthTestEnable = true,
          .depthWriteEnable = true,
        },
    });
  }

  Fwog::GraphicsPipeline ShadowVsm()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/shadows/ShadowMain.vert.glsl");
    auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/shadows/vsm/VsmShadow.frag.glsl");

    return Fwog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .rasterizationState = {.cullMode = Fwog::CullMode::BACK},
    });
  }

  Fwog::GraphicsPipeline DebugLines()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/debug/Debug.vert.glsl");
    auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/VertexColor.frag.glsl");

    auto positionBinding = Fwog::VertexInputBindingDescription{
      .location = 0,
      .binding = 0,
      .format = Fwog::Format::R32G32B32_FLOAT,
      .offset = offsetof(Debug::Line, aPosition),
    };

    auto colorBinding = Fwog::VertexInputBindingDescription{
      .location = 1,
      .binding = 0,
      .format = Fwog::Format::R32G32B32A32_FLOAT,
      .offset = offsetof(Debug::Line, aColor),
    };

    auto bindings = {positionBinding, colorBinding};

    return Fwog::GraphicsPipeline({
      .vertexShader = &vs,
      .fragmentShader = &fs,
      .inputAssemblyState = {.topology = Fwog::PrimitiveTopology::LINE_LIST},
      .vertexInputState = bindings,
      .rasterizationState = {.cullMode = Fwog::CullMode::NONE, .lineWidth = 2},
      .depthState =
        {
          .depthTestEnable = true,
          .depthWriteEnable = false,
          .depthCompareOp = FWOG_COMPARE_OP_NEARER,
        },
    });
  }

  Fwog::GraphicsPipeline DebugAabbs()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/debug/DebugAabb.vert.glsl");
    auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/VertexColor.frag.glsl");

    auto blend0 = Fwog::ColorBlendAttachmentState{
      .blendEnable = true,
      .srcColorBlendFactor = Fwog::BlendFactor::SRC_ALPHA,
      .dstColorBlendFactor = Fwog::BlendFactor::ONE,
    };

    auto blend1 = Fwog::ColorBlendAttachmentState{
      .blendEnable = false,
    };

    auto blends = {blend0, blend1};

    return Fwog::GraphicsPipeline({.vertexShader = &vs,
                                   .fragmentShader = &fs,
                                   .inputAssemblyState = {.topology = Fwog::PrimitiveTopology::TRIANGLE_STRIP},
                                   .rasterizationState =
                                     {
                                       .polygonMode = Fwog::PolygonMode::LINE,
                                       .cullMode = Fwog::CullMode::NONE,
                                       .depthBiasEnable = true,
                                       .depthBiasConstantFactor = 50.0f,
                                     },
                                   .depthState =
                                     {
                                       .depthTestEnable = true,
                                       .depthWriteEnable = false,
                                       .depthCompareOp = FWOG_COMPARE_OP_NEARER,
                                     },
                                   .colorBlendState = {
                                     .attachments = blends,
                                   }});
  }

  Fwog::GraphicsPipeline DebugRects()
  {
    auto vs = LoadShaderWithIncludes(Fwog::PipelineStage::VERTEX_SHADER, "shaders/debug/DebugRect.vert.glsl");
    auto fs = LoadShaderWithIncludes(Fwog::PipelineStage::FRAGMENT_SHADER, "shaders/debug/VertexColor.frag.glsl");

    auto blend0 = Fwog::ColorBlendAttachmentState{
      .blendEnable = true,
      .srcColorBlendFactor = Fwog::BlendFactor::SRC_ALPHA,
      .dstColorBlendFactor = Fwog::BlendFactor::ONE,
    };

    auto blend1 = Fwog::ColorBlendAttachmentState{
      .blendEnable = false,
    };

    auto blends = {blend0, blend1};

    return Fwog::GraphicsPipeline({.vertexShader = &vs,
                                   .fragmentShader = &fs,
                                   .inputAssemblyState = {.topology = Fwog::PrimitiveTopology::TRIANGLE_FAN},
                                   .rasterizationState =
                                     {
                                       .polygonMode = Fwog::PolygonMode::FILL,
                                       .cullMode = Fwog::CullMode::NONE,
                                     },
                                   .depthState =
                                     {
                                       .depthTestEnable = true,
                                       .depthWriteEnable = false,
                                       .depthCompareOp = FWOG_COMPARE_OP_NEARER,
                                     },
                                   .colorBlendState = {
                                     .attachments = blends,
                                   }});
  }
}