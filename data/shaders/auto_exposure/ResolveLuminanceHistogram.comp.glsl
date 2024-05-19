#version 460 core
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_arithmetic : require

#include "AutoExposureCommon.h.glsl"
#include "../Math.h.glsl"

// gl_SubgroupSize is not a constant, so assume subgroup size is 1 (worst case)
// Most likely, we will only use 1/32 of this storage
shared uint sh_scratch[NUM_BUCKETS];

layout (local_size_x = NUM_BUCKETS) in;
void main()
{
  const uint i = gl_GlobalInvocationID.x;

  const uint currentBucketCount = d_autoExposure.histogramBuckets[i];

  {
    // Weighted by the bucket index
    const uint firstReduce = subgroupAdd(currentBucketCount * i);

    // Do a cheeky reduction here to skip an iteration of the loop
    if (subgroupElect())
    {
      sh_scratch[gl_SubgroupID] = firstReduce;
    }
  }

  // Reset bucket for next frame
  d_autoExposure.histogramBuckets[i] = 0;

  barrier();

  // Most likely, just one iteration of this loop will run.
  // E.g., 128 buckets / 32 lanes in a subgroup = 4 work items left
  for (uint work = NUM_BUCKETS / gl_SubgroupSize; work > 0; work /= gl_SubgroupSize)
  {
    uint localSum = 0;
    if (i < work)
    {
      localSum = subgroupAdd(sh_scratch[i]);
    }

    barrier();

    if (subgroupElect())
    {
      sh_scratch[gl_SubgroupID] = localSum;
    }

    barrier();
  }

  if (gl_GlobalInvocationID.x == 0)
  {
    // Since our global ID is zero, we know that this is the number of black (or nearly black) pixels on the screen
    const uint numBlackPixels = currentBucketCount;
    const float log2MeanLuminance = Remap(float(sh_scratch[0]) / max(float(d_autoExposure.numPixels) - numBlackPixels, 1.0), 1.0, NUM_BUCKETS, d_autoExposure.logMinLuminance, d_autoExposure.logMaxLuminance);
    // Lerp in linear space (lerping in log space seems to cause visually nonuniform adaptation in different lighting conditions)
    const float exposureTarget = log2(d_autoExposure.targetLuminance / exp2(log2MeanLuminance));
    // Framerate-independent exponential decay
    const float alpha = clamp(1 - exp(-d_autoExposure.deltaTime * d_autoExposure.adjustmentSpeed), 0.0, 1.0);
    d_exposureBuffer.exposure = mix(d_exposureBuffer.exposure, exposureTarget, alpha);
  }
}