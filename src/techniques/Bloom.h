#pragma once
#include <Fwog/Pipeline.h>
#include <Fwog/Texture.h>
#include <Fwog/Buffer.h>

#include <glm/vec2.hpp>

namespace Techniques
{
  class Bloom
  {
  public:
    explicit Bloom();

    struct ApplyParams
    {
      // The input and output texture.
      Fwog::Texture& target;

      // A scratch texture to be used for intermediate storage.
      // Its dimensions should be _half_ those of the target.
      Fwog::Texture& scratchTexture;

      // Maximum number of times to downsample before upsampling.
      // A larger value means a wider blur.
      // Cannot exceed the number of mip levels in the target image.
      uint32_t passes;

      // How noticeable the effect should be.
      // A reasonable value for an HDR renderer would be less than 1.0/16.0
      float strength;

      // Width of the bloom upscale kernel.
      float width;

      // If true, a low-pass filter will be used on the first downsampling pass
      // to reduce the dynamic range and minimize temporal aliasing.
      bool useLowPassFilterOnFirstPass;
    };

    void Apply(const ApplyParams& params);

  private:
    struct BloomUniforms
    {
      glm::ivec2 sourceDim;
      glm::ivec2 targetDim;
      float width;
      float strength;
      float sourceLod;
      uint32_t numPasses;
      uint32_t isFinalPass;
      uint32_t _padding[3];
    };

    Fwog::TypedBuffer<BloomUniforms> uniformBuffer;

    Fwog::ComputePipeline downsampleLowPassPipeline;
    Fwog::ComputePipeline downsamplePipeline;
    Fwog::ComputePipeline upsamplePipeline;
  };
} // namespace Techniques