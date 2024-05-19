#include "AutoExposure.h"

#include "Fvog/Device.h"
#include "Fvog/Rendering2.h"
#include "Fvog/Shader2.h"

#include "../RendererUtilities.h"

#include <cmath>

namespace Techniques
{
  static Fvog::ComputePipeline CreateGenerateLuminanceHistogramPipeline(Fvog::Device& device)
  {
    auto cs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/auto_exposure/GenerateLuminanceHistogram.comp.glsl");

    return Fvog::ComputePipeline(device, {
      .name = "Generate Luminance Histogram",
      .shader = &cs,
    });
  }

  static Fvog::ComputePipeline CreateResolveLuminanceHistogramPipeline(Fvog::Device& device)
  {
    auto cs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/auto_exposure/ResolveLuminanceHistogram.comp.glsl");

    return Fvog::ComputePipeline(device, {
      .name = "Resolve Luminance Histogram",
      .shader = &cs,
    });
  }

  AutoExposure::AutoExposure(Fvog::Device& device)
    : device_(&device),
      dataBuffer_(device, 1, "Auto Exposure Data"),
      generateLuminanceHistogramPipeline_(CreateGenerateLuminanceHistogramPipeline(device)),
      resolveLuminanceHistogramPipeline_(CreateResolveLuminanceHistogramPipeline(device))
  {
    // Initialize buckets to zero
    //dataBuffer_.FillData();
    device.ImmediateSubmit(
      [this](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, dataBuffer_.GetDeviceBuffer().Handle(), 0, VK_WHOLE_SIZE, 0);
      });
  }

  void AutoExposure::Apply(VkCommandBuffer cmd, const ApplyParams& params)
  {
    auto ctx = Fvog::Context(*device_, cmd);
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

    //Fvog::MemoryBarrier(Fvog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fvog::MemoryBarrierBit::TEXTURE_FETCH_BIT);
    ctx.Barrier();

    // Generate histogram
    //Fvog::Cmd::BindStorageBuffer(0, dataBuffer_);
    //Fvog::Cmd::BindStorageBuffer(1, params.exposureBuffer);
    //Fvog::Cmd::BindSampledImage(0, params.image, Fvog::Sampler(*device_, Fvog::SamplerCreateInfo{}));
    ctx.SetPushConstants(AutoExposurePushConstants{
      .autoExposureBufferIndex = dataBuffer_.GetDeviceBuffer().GetResourceHandle().index,
      .exposureBufferIndex = params.exposureBuffer.GetResourceHandle().index,
      .hdrBufferIndex = params.image.ImageView().GetSampledResourceHandle().index,
    });

    ctx.BindComputePipeline(generateLuminanceHistogramPipeline_);
    ctx.DispatchInvocations(params.image.GetCreateInfo().extent);

    //Fvog::MemoryBarrier(Fvog::MemoryBarrierBit::SHADER_STORAGE_BIT);
    ctx.Barrier();

    // Resolve histogram
    ctx.BindComputePipeline(resolveLuminanceHistogramPipeline_);
    ctx.Dispatch(1, 1, 1);

    ctx.Barrier();
  }
} // namespace Techniques