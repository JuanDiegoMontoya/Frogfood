#include "AutoExposure.h"
#include "AutoExposure.h"

#include "Fwog/Rendering.h"
#include "Fwog/Shader.h"

#include "../RendererUtilities.h"

#include <bit>

namespace Techniques
{
  static Fwog::ComputePipeline CreateGenerateLuminanceHistogramPipeline()
  {
    auto cs = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/auto_exposure/GenerateLuminanceHistogram.comp.glsl");

    return Fwog::ComputePipeline({
      .name = "Generate Luminance Histogram",
      .shader = &cs,
    });
  }

  static Fwog::ComputePipeline CreateResolveLuminanceHistogramPipeline()
  {
    auto cs = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/auto_exposure/ResolveLuminanceHistogram.comp.glsl");

    return Fwog::ComputePipeline({
      .name = "Resolve Luminance Histogram",
      .shader = &cs,
    });
  }

  AutoExposure::AutoExposure()
    : dataBuffer_(sizeof(AutoExposureBufferData), Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
      generateLuminanceHistogramPipeline_(CreateGenerateLuminanceHistogramPipeline()),
      resolveLuminanceHistogramPipeline_(CreateResolveLuminanceHistogramPipeline())
  {
    // Initialize buckets to zero
    dataBuffer_.FillData();
  }

  void AutoExposure::Apply(const ApplyParams& params)
  {
    Fwog::Compute(
      "Auto Exposure",
      [&]
      {
        auto uniforms = AutoExposureUniforms{
          .deltaTime = params.deltaTime,
          .adjustmentSpeed = params.adjustmentSpeed,
          .logMinLuminance = log(params.targetLuminance / params.maxExposure),
          .logMaxLuminance = log(params.targetLuminance / params.minExposure),
          .targetLuminance = params.targetLuminance,
          .numPixels = params.image.Extent().width * params.image.Extent().height,
        };
        dataBuffer_.UpdateData(std::span{&uniforms, 1});

        Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT | Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT);

        // Generate histogram
        Fwog::Cmd::BindStorageBuffer(0, dataBuffer_);
        Fwog::Cmd::BindStorageBuffer(1, params.exposureBuffer);
        Fwog::Cmd::BindSampledImage(0, params.image, Fwog::Sampler(Fwog::SamplerState{}));

        Fwog::Cmd::BindComputePipeline(generateLuminanceHistogramPipeline_);
        Fwog::Cmd::DispatchInvocations(params.image.Extent());

        Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::SHADER_STORAGE_BIT);

        // Resolve histogram
        Fwog::Cmd::BindComputePipeline(resolveLuminanceHistogramPipeline_);
        Fwog::Cmd::Dispatch(1, 1, 1);
      }
    );
  }
} // namespace Techniques