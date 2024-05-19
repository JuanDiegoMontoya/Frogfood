#ifndef AUTO_EXPOSURE_COMMON_H
#define AUTO_EXPOSURE_COMMON_H

#extension GL_GOOGLE_include_directive : enable

#define NUM_BUCKETS 128

#include "../Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(AutoExposurePushConstants)
{
  FVOG_UINT32 autoExposureBufferIndex;
  FVOG_UINT32 exposureBufferIndex;
  FVOG_UINT32 hdrBufferIndex;
};

//layout(std430, binding = 0) restrict buffer AutoExposureBuffer
FVOG_DECLARE_STORAGE_BUFFERS(AutoExposureBuffers)
{
  readonly float deltaTime;
  readonly float adjustmentSpeed;
  readonly float logMinLuminance;
  readonly float logMaxLuminance;
  readonly float targetLuminance; // 0.184 = 50% perceived lightness (L*)
  readonly uint numPixels; // Number of pixels considered in the reduction
  coherent uint histogramBuckets[NUM_BUCKETS];
} autoExposureBuffers[];

#define d_autoExposure autoExposureBuffers[autoExposureBufferIndex]

//layout (std430, binding = 1) buffer ExposureBuffer
FVOG_DECLARE_STORAGE_BUFFERS(ExposureBuffers)
{
  float exposure;
} exposureBuffers[];

#define d_exposureBuffer exposureBuffers[exposureBufferIndex]

#endif // AUTO_EXPOSURE_COMMON_H