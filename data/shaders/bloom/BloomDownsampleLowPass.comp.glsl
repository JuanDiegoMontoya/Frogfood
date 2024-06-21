#version 460 core

#extension GL_GOOGLE_include_directive : enable

#define LOCAL_SIZE_X 16
#define LOCAL_SIZE_Y 16
#include "BloomDownsampleCommon.h.glsl"

#include "../Utility.h.glsl"

// Reduce the dynamic range of the input samples
vec3 KarisAverage(vec3 c1, vec3 c2, vec3 c3, vec3 c4)
{
  float w1 = 1.0 / (Luminance(c1.rgb) + 1.0);
  float w2 = 1.0 / (Luminance(c2.rgb) + 1.0);
  float w3 = 1.0 / (Luminance(c3.rgb) + 1.0);
  float w4 = 1.0 / (Luminance(c4.rgb) + 1.0);

  return (c1 * w1 + c2 * w2 + c3 * w3 + c4 * w4) / (w1 + w2 + w3 + w4);	
}

void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const ivec2 lid = ivec2(gl_LocalInvocationID.xy);

  // Center of written pixel
  const vec2 uv = (vec2(gid) + 0.5) / uniforms.targetDim;

  InitializeSharedMemory(uniforms.targetDim, uniforms.sourceDim, uniforms.sourceLod);

  barrier();

  if (any(greaterThanEqual(gid, uniforms.targetDim)))
  {
    return;
  }

  vec3 samples[13];
  samples[0 ] = sh_coarseSamples[lid.x + 0][lid.y + 0];  //  (-2, -2)
  samples[1 ] = sh_coarseSamples[lid.x + 1][lid.y + 0];  //  (0, -2)
  samples[2 ] = sh_coarseSamples[lid.x + 2][lid.y + 0];  //  (2, -2)
  samples[3 ] = sh_preciseSamples[lid.x + 0][lid.y + 0]; //  (-1, -1)
  samples[4 ] = sh_preciseSamples[lid.x + 1][lid.y + 0]; //  (1, -1)
  samples[5 ] = sh_coarseSamples[lid.x + 0][lid.y + 1];  //  (-2, 0)
  samples[6 ] = sh_coarseSamples[lid.x + 1][lid.y + 1];  //  (0, 0)
  samples[7 ] = sh_coarseSamples[lid.x + 2][lid.y + 1];  //  (2, 0)
  samples[8 ] = sh_preciseSamples[lid.x + 0][lid.y + 1]; //  (-1, 1)
  samples[9 ] = sh_preciseSamples[lid.x + 1][lid.y + 1]; //  (1, 1)
  samples[10] = sh_coarseSamples[lid.x + 0][lid.y + 2];  //  (-2, 2)
  samples[11] = sh_coarseSamples[lid.x + 1][lid.y + 2];  //  (0, 2)
  samples[12] = sh_coarseSamples[lid.x + 2][lid.y + 2];  //  (2, 2)

  // Uncomment to debug
  // vec2 texel = 1.0 / uniforms.sourceDim;
  // samples[0 ] = textureLod(s_source, uv + texel * vec2(-2, -2), uniforms.sourceLod).rgb;
  // samples[1 ] = textureLod(s_source, uv + texel * vec2(0, -2) , uniforms.sourceLod).rgb;
  // samples[2 ] = textureLod(s_source, uv + texel * vec2(2, -2) , uniforms.sourceLod).rgb;
  // samples[3 ] = textureLod(s_source, uv + texel * vec2(-1, -1), uniforms.sourceLod).rgb;
  // samples[4 ] = textureLod(s_source, uv + texel * vec2(1, -1) , uniforms.sourceLod).rgb;
  // samples[5 ] = textureLod(s_source, uv + texel * vec2(-2, 0) , uniforms.sourceLod).rgb;
  // samples[6 ] = textureLod(s_source, uv + texel * vec2(0, 0)  , uniforms.sourceLod).rgb;
  // samples[7 ] = textureLod(s_source, uv + texel * vec2(2, 0)  , uniforms.sourceLod).rgb;
  // samples[8 ] = textureLod(s_source, uv + texel * vec2(-1, 1) , uniforms.sourceLod).rgb;
  // samples[9 ] = textureLod(s_source, uv + texel * vec2(1, 1)  , uniforms.sourceLod).rgb;
  // samples[10] = textureLod(s_source, uv + texel * vec2(-2, 2) , uniforms.sourceLod).rgb;
  // samples[11] = textureLod(s_source, uv + texel * vec2(0, 2)  , uniforms.sourceLod).rgb;
  // samples[12] = textureLod(s_source, uv + texel * vec2(2, 2)  , uniforms.sourceLod).rgb;

  vec3 filterSum = vec3(0);
  filterSum += KarisAverage(samples[3], samples[4], samples[8 ], samples[9 ]) * 0.5;
  filterSum += KarisAverage(samples[0], samples[1], samples[5 ], samples[6 ]) * 0.125;
  filterSum += KarisAverage(samples[1], samples[2], samples[6 ], samples[7 ]) * 0.125;
  filterSum += KarisAverage(samples[5], samples[6], samples[10], samples[11]) * 0.125;
  filterSum += KarisAverage(samples[6], samples[7], samples[11], samples[12]) * 0.125;
  
  imageStore(i_target, gid, vec4(filterSum, 1.0));
}