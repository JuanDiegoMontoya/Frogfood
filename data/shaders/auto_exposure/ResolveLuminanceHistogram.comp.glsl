#version 460 core
#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_arithmetic : require

#include "../Math.h.glsl"
#include "AutoExposureCommon.h.glsl"

// gl_SubgroupSize is not a constant, so assume subgroup size is 1 (worst case)
// Most likely, we will only use 1/32 of this storage
shared uint sh_scratch[NUM_BUCKETS];

layout (local_size_x = NUM_BUCKETS, local_size_y = 1, local_size_z = 1) in;
void main()
{
  const uint i = gl_GlobalInvocationID.x;

  // Weighted
  const uint firstReduce = subgroupAdd(autoExposure.histogramBuckets[i] * (i + 1));

  // Do a cheeky reduction here to skip an iteration of the loop
  if (subgroupElect())
  {
    sh_scratch[gl_SubgroupID] = firstReduce;
  }

  // Reset bucket for next frame
  autoExposure.histogramBuckets[i] = 0;

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
    float meanLuminance = exp(Remap(float(sh_scratch[0]) / float(autoExposure.numPixels), 0.0, NUM_BUCKETS, autoExposure.logMinLuminance, autoExposure.logMaxLuminance));
    float exposureTarget = autoExposure.targetLuminance / meanLuminance;
    readExposure = mix(readExposure, exposureTarget, autoExposure.deltaTime * autoExposure.adjustmentSpeed);
  }
}