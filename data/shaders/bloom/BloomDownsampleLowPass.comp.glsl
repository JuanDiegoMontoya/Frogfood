#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "BloomCommon.h.glsl"
#include "../Utility.h.glsl"

layout(binding = 0) uniform sampler2D s_source;
layout(binding = 0) uniform writeonly image2D i_target;

vec3 karisAverage(vec3 c1, vec3 c2, vec3 c3, vec3 c4)
{
  float w1 = 1.0 / (luminance(c1.rgb) + 1.0);
  float w2 = 1.0 / (luminance(c2.rgb) + 1.0);
  float w3 = 1.0 / (luminance(c3.rgb) + 1.0);
  float w4 = 1.0 / (luminance(c4.rgb) + 1.0);

  return (c1 * w1 + c2 * w2 + c3 * w3 + c4 * w4) / (w1 + w2 + w3 + w4);	
}

layout(local_size_x = 16, local_size_y = 16) in;
void main()
{
  ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, uniforms.targetDim)))
    return;

  vec2 texel = 1.0 / uniforms.sourceDim;

  // center of written pixel
  vec2 uv = (vec2(gid) + 0.5) / uniforms.targetDim;

  vec3 samples[13];
  samples[0 ] = textureLod(s_source, uv + texel * vec2(-2, -2), uniforms.sourceLod).rgb;
  samples[1 ] = textureLod(s_source, uv + texel * vec2(0, -2) , uniforms.sourceLod).rgb;
  samples[2 ] = textureLod(s_source, uv + texel * vec2(2, -2) , uniforms.sourceLod).rgb;
  samples[3 ] = textureLod(s_source, uv + texel * vec2(-1, -1), uniforms.sourceLod).rgb;
  samples[4 ] = textureLod(s_source, uv + texel * vec2(1, -1) , uniforms.sourceLod).rgb;
  samples[5 ] = textureLod(s_source, uv + texel * vec2(-2, 0) , uniforms.sourceLod).rgb;
  samples[6 ] = textureLod(s_source, uv + texel * vec2(0, 0)  , uniforms.sourceLod).rgb;
  samples[7 ] = textureLod(s_source, uv + texel * vec2(2, 0)  , uniforms.sourceLod).rgb;
  samples[8 ] = textureLod(s_source, uv + texel * vec2(-1, 1) , uniforms.sourceLod).rgb;
  samples[9 ] = textureLod(s_source, uv + texel * vec2(1, 1)  , uniforms.sourceLod).rgb;
  samples[10] = textureLod(s_source, uv + texel * vec2(-2, 2) , uniforms.sourceLod).rgb;
  samples[11] = textureLod(s_source, uv + texel * vec2(0, 2)  , uniforms.sourceLod).rgb;
  samples[12] = textureLod(s_source, uv + texel * vec2(2, 2)  , uniforms.sourceLod).rgb;

  vec3 filterSum = vec3(0);
  filterSum += karisAverage(samples[3], samples[4], samples[8 ], samples[9 ]) * 0.5;
  filterSum += karisAverage(samples[0], samples[1], samples[5 ], samples[6 ]) * 0.125;
  filterSum += karisAverage(samples[1], samples[2], samples[6 ], samples[7 ]) * 0.125;
  filterSum += karisAverage(samples[5], samples[6], samples[10], samples[11]) * 0.125;
  filterSum += karisAverage(samples[6], samples[7], samples[11], samples[12]) * 0.125;
  
  imageStore(i_target, gid, vec4(filterSum, 1.0));
}