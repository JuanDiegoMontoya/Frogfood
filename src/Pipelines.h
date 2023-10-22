#pragma once
#include <Fwog/Pipeline.h>

namespace Pipelines
{
  Fwog::ComputePipeline MeshletGenerate();
  Fwog::ComputePipeline HzbCopy();
  Fwog::ComputePipeline HzbReduce();
  Fwog::GraphicsPipeline Visbuffer();
  Fwog::GraphicsPipeline MaterialDepth();
  Fwog::GraphicsPipeline VisbufferResolve();
  Fwog::GraphicsPipeline Shading();
  Fwog::ComputePipeline Tonemap();
  Fwog::GraphicsPipeline DebugTexture();
  Fwog::GraphicsPipeline ShadowMain();
  Fwog::GraphicsPipeline ShadowVsm();
  Fwog::GraphicsPipeline DebugLines();
  Fwog::GraphicsPipeline DebugAabbs();
  Fwog::GraphicsPipeline DebugRects();
  Fwog::GraphicsPipeline ViewerVsm();
  Fwog::GraphicsPipeline ViewerVsmPhysicalPages();
  Fwog::GraphicsPipeline ViewerVsmBitmaskHzb();
}