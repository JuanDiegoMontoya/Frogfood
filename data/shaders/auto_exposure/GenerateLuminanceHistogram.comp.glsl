#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "AutoExposureCommon.h.glsl"
#include "../Math.h.glsl"
#include "../Utility.h.glsl"

//layout(location = 0) uniform sampler2D u_hdrBuffer;
FVOG_DECLARE_SAMPLED_IMAGES(texture2D);

uint ColorToBucket(vec3 color)
{
  const float luminance = Luminance(color);
  if (luminance < 1e-5)
    return 0;
  float mapped = Remap(log2(luminance), d_autoExposure.logMinLuminance, d_autoExposure.logMaxLuminance, 1.0, float(NUM_BUCKETS - 1));
  return clamp(int(mapped), 0, NUM_BUCKETS - 1);
}

shared int sh_buckets[NUM_BUCKETS];

layout(local_size_x = 32, local_size_y = 32) in;
void main()
{
  const uint localId = gl_LocalInvocationIndex;
  sh_buckets[localId] = 0;
  
  barrier();

  const uvec2 texSize = textureSize(FvogGetSampledImage(texture2D, hdrBufferIndex), 0);
  const uvec2 coords = uvec2(gl_GlobalInvocationID.xy);
  if (all(lessThan(coords, texSize)))
  {
    vec3 color = texelFetch(FvogGetSampledImage(texture2D, hdrBufferIndex), ivec2(coords), 0).rgb;
    uint bucket = ColorToBucket(color);
    atomicAdd(sh_buckets[bucket], 1);
  }

  barrier();

  // Only do one global atomic add per bucket
  if (localId < NUM_BUCKETS)
  {
    atomicAdd(d_autoExposure.histogramBuckets[localId], sh_buckets[localId]);
  }
}
