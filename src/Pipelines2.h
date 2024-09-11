#pragma once
#include "Fvog/Pipeline2.h"

namespace Pipelines2
{
  [[nodiscard]] Fvog::ComputePipeline CullMeshlets();
  [[nodiscard]] Fvog::ComputePipeline CullTriangles();
  [[nodiscard]] Fvog::ComputePipeline HzbCopy();
  [[nodiscard]] Fvog::ComputePipeline HzbReduce();
  [[nodiscard]] Fvog::GraphicsPipeline Visbuffer(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline VisbufferResolve(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline Shading(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::ComputePipeline Tonemap();
  [[nodiscard]] Fvog::ComputePipeline CalibrateHdr();
  [[nodiscard]] Fvog::GraphicsPipeline DebugTexture(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ShadowMain(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ShadowVsm(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline DebugLines(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline DebugAabbs(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline DebugRects(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ViewerVsm(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ViewerVsmPhysicalPages(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ViewerVsmBitmaskHzb(const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ViewerVsmPhysicalPagesOverdraw(const Fvog::RenderTargetFormats& renderTargetFormats);

  // TODO: remove
  [[nodiscard]] Fvog::RayTracingPipeline TestRayTracingPipeline();
} // namespace Pipelines2