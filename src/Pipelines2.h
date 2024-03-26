#pragma once
#include "Fvog/Pipeline2.h"

namespace Pipelines2
{
  // TODO: disgusting, remove
  extern VkPipelineLayout pipelineLayout;

  void InitPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout);
  void DestroyPipelineLayout(VkDevice device);

  Fvog::ComputePipeline CullMeshlets(VkDevice device);
  Fvog::ComputePipeline CullTriangles(VkDevice device);
  Fvog::ComputePipeline HzbCopy(VkDevice device);
  Fvog::ComputePipeline HzbReduce(VkDevice device);
  Fvog::GraphicsPipeline Visbuffer(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline MaterialDepth(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline VisbufferResolve(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline Shading(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::ComputePipeline Tonemap(VkDevice device);
  Fvog::GraphicsPipeline DebugTexture(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline ShadowMain(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline ShadowVsm(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline DebugLines(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline DebugAabbs(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline DebugRects(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline ViewerVsm(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline ViewerVsmPhysicalPages(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
  Fvog::GraphicsPipeline ViewerVsmBitmaskHzb(VkDevice device, const Fvog::RenderTargetFormats& renderTargetFormats);
} // namespace Pipelines2