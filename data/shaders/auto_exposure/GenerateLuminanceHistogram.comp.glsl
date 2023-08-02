#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "../Math.h.glsl"
#include "../Utility.h.glsl"
#include "AutoExposureCommon.h.glsl"

#define LOCAL_X 16
#define LOCAL_Y 8
#define LOCAL_Z 1
#define WORKGROUPSIZE LOCAL_X * LOCAL_Y * LOCAL_Z

layout(location = 0) uniform sampler2D u_hdrBuffer;

uint colorToBucket(vec3 color)
{
  float luminance = Luminance(color);
  if (luminance < 1e-4)
    return 0;
  float mapped = Remap(log(luminance), autoExposure.logMinLuminance, autoExposure.logMaxLuminance, 0.0, float(NUM_BUCKETS - 1));
  return clamp(int(mapped), 0, NUM_BUCKETS - 1);
}

shared int shared_buckets[NUM_BUCKETS];

layout (local_size_x = LOCAL_X, local_size_y = LOCAL_Y, local_size_z = LOCAL_Z) in;
void main()
{
  shared_buckets[gl_LocalInvocationIndex] = 0;
  
  barrier();

  uvec2 texSize = textureSize(u_hdrBuffer, 0);
  vec2 upscaleFactor = vec2(texSize) / (gl_NumWorkGroups.xy * gl_WorkGroupSize.xy);
  uvec2 coords = uvec2(gl_GlobalInvocationID.xy * upscaleFactor);
  if (!any(greaterThanEqual(coords, texSize)))
  {
    vec3 color = texelFetch(u_hdrBuffer, ivec2(coords), 0).rgb;
    uint bucket = colorToBucket(color);
    atomicAdd(shared_buckets[bucket], 1);
  }

  barrier();

  atomicAdd(autoExposure.histogramBuckets[gl_LocalInvocationIndex], shared_buckets[gl_LocalInvocationIndex]);
}