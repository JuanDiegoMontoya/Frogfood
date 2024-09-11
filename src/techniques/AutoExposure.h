#pragma once
//#include <Fwog/Texture.h>
//#include <Fwog/Buffer.h>
//#include <Fwog/Pipeline.h>
#include "Fvog/Texture2.h"
#include "Fvog/Buffer2.h"
#include "Fvog/Pipeline2.h"

#include "shaders/Resources.h.glsl"

namespace Fvog
{
  class Device;
}

namespace Techniques
{
  class AutoExposure
  {
  public:
    explicit AutoExposure();

    struct ApplyParams
    {
      // Image whose average luminance is to be computed
      Fvog::Texture& image;

      // Buffer containing the output exposure value
      Fvog::Buffer& exposureBuffer;

      float deltaTime;

      float adjustmentSpeed;

      float targetLuminance;
      
      float logMinLuminance;

      float logMaxLuminance;
    };

    void Apply(VkCommandBuffer cmd, const ApplyParams& params);

  private:
    static constexpr uint32_t numBuckets = 128;

    FVOG_DECLARE_ARGUMENTS(AutoExposurePushConstants)
    {
      FVOG_UINT32 autoExposureBufferIndex;
      FVOG_UINT32 exposureBufferIndex;
      FVOG_UINT32 hdrBufferIndex;
    };

    // Read-only data
    struct AutoExposureUniforms
    {
      float deltaTime;
      float adjustmentSpeed;
      float logMinLuminance;
      float logMaxLuminance;
      float targetLuminance;
      uint32_t numPixels;
    };

    struct AutoExposureBufferData
    {
      AutoExposureUniforms uniforms;
      uint32_t histogramBuckets[numBuckets];
    };
    
    Fvog::NDeviceBuffer<AutoExposureBufferData> dataBuffer_;

    Fvog::ComputePipeline generateLuminanceHistogramPipeline_;
    Fvog::ComputePipeline resolveLuminanceHistogramPipeline_;
  };
}