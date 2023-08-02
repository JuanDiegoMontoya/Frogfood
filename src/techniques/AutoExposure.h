#pragma once
#include <Fwog/Texture.h>
#include <Fwog/Buffer.h>
#include <Fwog/Pipeline.h>

namespace Techniques
{
  class AutoExposure
  {
  public:
    explicit AutoExposure();

    struct ApplyParams
    {
      // Image whose average luminance is to be computed
      const Fwog::Texture& image;

      // Buffer containing the output exposure value
      Fwog::Buffer& exposureBuffer;

      float deltaTime;

      float adjustmentSpeed;

      float targetLuminance;

      // TODO: parameterize min/max exposure a different way (perhaps via # of stops from target)
      float minExposure;

      float maxExposure;
    };

    void Apply(const ApplyParams& params);

  private:
    static constexpr uint32_t numBuckets = 128;

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
    
    Fwog::Buffer dataBuffer_;

    Fwog::ComputePipeline generateLuminanceHistogramPipeline_;
    Fwog::ComputePipeline resolveLuminanceHistogramPipeline_;
  };
}