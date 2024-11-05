#include "AutoExposure.h"
#include "Application.h"

#include "Fvog/Device.h"
#include "Fvog/Rendering2.h"
#include "Fvog/Shader2.h"

#include "../RendererUtilities.h"

#include <cmath>

namespace Techniques
{
  AutoExposure::AutoExposure()
    : dataBuffer_(1, "Auto Exposure Data")
  {
    generateLuminanceHistogramPipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "Generate Luminance Histogram",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "auto_exposure/GenerateLuminanceHistogram.comp.glsl",
        },
    });

    resolveLuminanceHistogramPipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "Resolve Luminance Histogram",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "auto_exposure/ResolveLuminanceHistogram.comp.glsl",
        },
    });


    // Initialize buckets to zero
    //dataBuffer_.FillData();
    Fvog::GetDevice().ImmediateSubmit(
      [this](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, dataBuffer_.GetDeviceBuffer().Handle(), 0, VK_WHOLE_SIZE, 0);
      });
  }

  void AutoExposure::Apply(VkCommandBuffer cmd, const ApplyParams& params)
  {
    auto ctx = Fvog::Context(cmd);
    auto d = ctx.MakeScopedDebugMarker("Auto Exposure");

    auto uniforms = AutoExposureUniforms{
      .deltaTime = params.deltaTime,
      .adjustmentSpeed = params.adjustmentSpeed,
      .logMinLuminance = std::log2(params.targetLuminance / std::exp2(params.logMinLuminance)),
      .logMaxLuminance = std::log2(params.targetLuminance / std::exp2(params.logMaxLuminance)),
      .targetLuminance = params.targetLuminance,
      .numPixels = params.image.GetCreateInfo().extent.width * params.image.GetCreateInfo().extent.height,
    };
    auto uploaded = AutoExposureBufferData{uniforms};
    dataBuffer_.UpdateData(cmd, {&uploaded, 1});
    
    ctx.Barrier();

    // Generate histogram
    ctx.SetPushConstants(AutoExposurePushConstants{
      .autoExposureBufferIndex = dataBuffer_.GetDeviceBuffer().GetResourceHandle().index,
      .exposureBufferIndex = params.exposureBuffer.GetResourceHandle().index,
      .hdrBufferIndex = params.image.ImageView().GetSampledResourceHandle().index,
    });

    ctx.BindComputePipeline(generateLuminanceHistogramPipeline_.GetPipeline());
    ctx.DispatchInvocations(params.image.GetCreateInfo().extent);
    
    ctx.Barrier();

    // Resolve histogram
    ctx.BindComputePipeline(resolveLuminanceHistogramPipeline_.GetPipeline());
    ctx.Dispatch(1, 1, 1);

    ctx.Barrier();
  }
} // namespace Techniques