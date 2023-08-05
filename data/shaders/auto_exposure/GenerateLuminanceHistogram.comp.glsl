#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "AutoExposureCommon.h.glsl"

layout(location = 0) uniform sampler2D u_hdrBuffer;

uint ColorToBucket(vec3 color)
{
  const float luminance = Luminance(color);
  if (luminance < 1e-5)
    return 0;
  float mapped = Remap(log2(luminance), autoExposure.logMinLuminance, autoExposure.logMaxLuminance, 1.0, float(NUM_BUCKETS - 1));
  return clamp(int(mapped), 0, NUM_BUCKETS - 1);
}

shared int sh_buckets[NUM_BUCKETS];

layout(local_size_x = 32, local_size_y = 32) in;
void main()
{
  const uint localId = gl_LocalInvocationIndex;
  sh_buckets[localId] = 0;
  
  barrier();

  const uvec2 texSize = textureSize(u_hdrBuffer, 0);
  const uvec2 coords = uvec2(gl_GlobalInvocationID.xy);
  if (all(lessThan(coords, texSize)))
  {
    vec3 color = texelFetch(u_hdrBuffer, ivec2(coords), 0).rgb;
    uint bucket = ColorToBucket(color);
    atomicAdd(sh_buckets[bucket], 1);
  }

  barrier();

  // Only do one global atomic add per bucket
  if (localId < NUM_BUCKETS)
  {
    atomicAdd(autoExposure.histogramBuckets[localId], sh_buckets[localId]);
  }
}
