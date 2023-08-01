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
    Bloom();

    struct ApplyParams
    {
      // The input and output texture.
      const Fwog::Texture& target;

      // A scratch texture to be used for intermediate storage.
      // Its dimensions should be _half_ those of the target.
      const Fwog::Texture& scratchTexture;

      // Maximum number of times to downsample before upsampling.
      // A larger value means a wider blur.
      // Cannot exceed the number of mip levels in the target image.
      uint32_t passes;

      // How noticeable the effect should be.
      // A reasonable value for an HDR renderer would be less than 1.0/16.0
      float strength;

      // Width of the bloom upscale kernel.
      float width;
    };

    void Apply(const ApplyParams& params);

  private:
    Fwog::ComputePipeline downsampleLowPassPipeline;
    Fwog::ComputePipeline downsamplePipeline;
    Fwog::ComputePipeline upsamplePipeline;

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
  };
} // namespace Techniques