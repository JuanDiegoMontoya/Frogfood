#include "Bloom.h"

#include "Fwog/Rendering.h"
#include "Fwog/Shader.h"

#include "../RendererUtilities.h"

namespace Techniques
{
  static Fwog::ComputePipeline CreateBloomDownsampleLowPassPipeline()
  {
    auto cs = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/bloom/BloomDownsampleLowPass.comp.glsl");

    return Fwog::ComputePipeline({
      .name = "Bloom Downsample (low-pass)",
      .shader = &cs,
    });
  }

  static Fwog::ComputePipeline CreateBloomDownsamplePipeline()
  {
    auto cs = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/bloom/BloomDownsample.comp.glsl");

    return Fwog::ComputePipeline({
      .name = "Bloom Downsample",
      .shader = &cs,
    });
  }

  static Fwog::ComputePipeline CreateBloomUpsamplePipeline()
  {
    auto cs = LoadShaderWithIncludes(Fwog::PipelineStage::COMPUTE_SHADER, "shaders/bloom/BloomUpsample.comp.glsl");

    return Fwog::ComputePipeline({
      .name = "Bloom Upsample",
      .shader = &cs,
    });
  }

  Bloom::Bloom()
    : uniformBuffer(Fwog::BufferStorageFlag::DYNAMIC_STORAGE),
      downsampleLowPassPipeline(CreateBloomDownsampleLowPassPipeline()),
      downsamplePipeline(CreateBloomDownsamplePipeline()),
      upsamplePipeline(CreateBloomUpsamplePipeline())
  {
  }

  void Bloom::Apply(const ApplyParams& params)
  {
    FWOG_ASSERT(params.passes <= params.scratchTexture.GetCreateInfo().mipLevels && "Bloom target is too small for the number of passes");
    
    auto linearSampler = Fwog::Sampler({
      .minFilter = Fwog::Filter::LINEAR,
      .magFilter = Fwog::Filter::LINEAR,
      .mipmapFilter = Fwog::Filter::NEAREST,
      .addressModeU = Fwog::AddressMode::MIRRORED_REPEAT,
      .addressModeV = Fwog::AddressMode::MIRRORED_REPEAT,
    });

    Fwog::Compute(
      "Bloom",
      [&]
      {
        Fwog::Cmd::BindUniformBuffer(0, uniformBuffer);
        for (uint32_t i = 0; i < params.passes; i++)
        {
          Fwog::Extent2D sourceDim{};
          Fwog::Extent2D targetDim = params.target.Extent() >> (i + 1);
          float sourceLod{};

          const Fwog::Texture* sourceTex = nullptr;

          // first pass, use downsampling with low-pass filter
          if (i == 0)
          {
            // the low pass filter prevents single pixels/thin lines from being bright
            Fwog::Cmd::BindComputePipeline(downsampleLowPassPipeline);
            //Fwog::Cmd::BindComputePipeline(downsamplePipeline);

            sourceLod = 0;
            sourceTex = &params.target;
            //sourceDim = {params.target.Extent().width, params.target.Extent().height};
            sourceDim = params.target.Extent();
          }
          else
          {
            Fwog::Cmd::BindComputePipeline(downsamplePipeline);

            sourceLod = static_cast<float>(i - 1);
            sourceTex = &params.scratchTexture;
            sourceDim = params.target.Extent() >> i;
          }

          Fwog::Cmd::BindSampledImage(0, *sourceTex, linearSampler);
          Fwog::Cmd::BindImage(0, params.scratchTexture, i);

          auto uniforms = BloomUniforms{
            .sourceDim = {sourceDim.width, sourceDim.height},
            .targetDim = {targetDim.width, targetDim.height},
            .sourceLod = sourceLod,
          };
          uniformBuffer.UpdateData(uniforms);

          Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT | Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT);
          Fwog::Cmd::DispatchInvocations(targetDim.width, targetDim.height, 1);
        }

        Fwog::Cmd::BindComputePipeline(upsamplePipeline);
        Fwog::Cmd::BindUniformBuffer(0, uniformBuffer);
        for (int32_t i = params.passes - 1; i >= 0; i--)
        {
          Fwog::Extent2D sourceDim = params.target.Extent() >> (i + 1);
          Fwog::Extent2D targetDim{};
          const Fwog::Texture* targetTex = nullptr;
          uint32_t targetLod{};

          // final pass
          if (i == 0)
          {
            targetLod = 0;
            targetTex = &params.target;
            targetDim = params.target.Extent();
          }
          else
          {
            targetLod = i - 1;
            targetTex = &params.scratchTexture;
            targetDim = params.target.Extent() >> i;
          }

          Fwog::Cmd::BindSampledImage(0, params.scratchTexture, linearSampler);
          Fwog::Cmd::BindImage(0, *targetTex, targetLod);

          BloomUniforms uniforms{
            .sourceDim = {sourceDim.width, sourceDim.height},
            .targetDim = {targetDim.width, targetDim.height},
            .width = params.width,
            .strength = params.strength,
            .sourceLod = static_cast<float>(i),
            .numPasses = params.passes,
            .isFinalPass = i == 0,
          };
          uniformBuffer.UpdateData(uniforms);
          
          Fwog::MemoryBarrier(Fwog::MemoryBarrierBit::TEXTURE_FETCH_BIT | Fwog::MemoryBarrierBit::IMAGE_ACCESS_BIT);
          Fwog::Cmd::DispatchInvocations(targetDim.width, targetDim.height, 1);
        }
      });
  }

} // namespace Techniques