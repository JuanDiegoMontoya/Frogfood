#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "../Math.h.glsl"
#include "AutoExposureCommon.h.glsl"

// TODO: do a multi-lane reduction
layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main()
{
  uint sum = 0;
  for (int i = 0; i < NUM_BUCKETS; i++)
  {
    sum += autoExposure.histogramBuckets[i] * (i + 1);
    autoExposure.histogramBuckets[i] = 0;
  }
  
  float meanLuminance = exp(Remap(float(sum) / float(autoExposure.numPixels), 0.0, NUM_BUCKETS, autoExposure.logMinLuminance, autoExposure.logMaxLuminance));
  float exposureTarget = autoExposure.targetLuminance / meanLuminance;
  readExposure = mix(readExposure, exposureTarget, autoExposure.deltaTime * autoExposure.adjustmentSpeed);
}