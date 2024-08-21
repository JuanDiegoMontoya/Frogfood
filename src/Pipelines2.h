#pragma once
#include "Fvog/Pipeline2.h"

namespace Pipelines2
{
  [[nodiscard]] Fvog::ComputePipeline CullMeshlets(Fvog::Device& device);
  [[nodiscard]] Fvog::ComputePipeline CullTriangles(Fvog::Device& device);
  [[nodiscard]] Fvog::ComputePipeline HzbCopy(Fvog::Device& device);
  [[nodiscard]] Fvog::ComputePipeline HzbReduce(Fvog::Device& device);
  [[nodiscard]] Fvog::GraphicsPipeline Visbuffer(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline VisbufferResolve(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline Shading(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::ComputePipeline Tonemap(Fvog::Device& device);
  [[nodiscard]] Fvog::ComputePipeline CalibrateHdr(Fvog::Device& device);
  [[nodiscard]] Fvog::GraphicsPipeline DebugTexture(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ShadowMain(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ShadowVsm(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline DebugLines(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline DebugAabbs(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline DebugRects(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ViewerVsm(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ViewerVsmPhysicalPages(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ViewerVsmBitmaskHzb(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
  [[nodiscard]] Fvog::GraphicsPipeline ViewerVsmPhysicalPagesOverdraw(Fvog::Device& device, const Fvog::RenderTargetFormats& renderTargetFormats);
} // namespace Pipelines2