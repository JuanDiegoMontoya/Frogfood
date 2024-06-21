#pragma once
#include <Fvog/Pipeline2.h>
#include <Fvog/Texture2.h>
#include <Fvog/Buffer2.h>

#include <glm/vec2.hpp>

#include <vulkan/vulkan_core.h>

namespace Fvog
{
  class Device;
}

namespace Techniques
{
  class Bloom
  {
  public:
    explicit Bloom(Fvog::Device& device);

    struct ApplyParams
    {
      // The input and output texture.
      Fvog::Texture& target;

      // A scratch texture to be used for intermediate storage.
      // Its dimensions should be _half_ those of the target.
      Fvog::Texture& scratchTexture;

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

    void Apply(VkCommandBuffer commandBuffer, const ApplyParams& params);

  private:
    Fvog::Device* device_;

    Fvog::ComputePipeline downsampleLowPassPipeline;
    Fvog::ComputePipeline downsamplePipeline;
    Fvog::ComputePipeline upsamplePipeline;
  };
} // namespace Techniques