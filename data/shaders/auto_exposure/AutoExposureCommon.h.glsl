#ifndef AUTO_EXPOSURE_COMMON_H
#define AUTO_EXPOSURE_COMMON_H

#define NUM_BUCKETS 128

layout(std430, binding = 0) restrict buffer AutoExposureBuffer
{
  readonly float deltaTime;
  readonly float adjustmentSpeed;
  readonly float logMinLuminance;
  readonly float logMaxLuminance;
  readonly float targetLuminance; // 0.184 = 50% perceived lightness (L*)
  readonly uint numPixels; // Number of pixels considered in the reduction
  coherent uint histogramBuckets[NUM_BUCKETS];
} autoExposure;

layout (std430, binding = 1) buffer ExposureBuffer
{
  float readExposure;
};

#endif // AUTO_EXPOSURE_COMMON_H