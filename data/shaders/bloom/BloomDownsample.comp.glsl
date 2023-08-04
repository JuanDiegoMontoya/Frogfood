#version 460 core

#extension GL_GOOGLE_include_directive : enable

#define LOCAL_SIZE_X 8
#define LOCAL_SIZE_Y 8
#include "BloomDownsampleCommon.h.glsl"

#include "BloomCommon.h.glsl"

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

  vec3 filterSum = vec3(0);
  filterSum += sh_coarseSamples[lid.x + 0][lid.y + 0] * (1.0 / 32.0);
  filterSum += sh_coarseSamples[lid.x + 2][lid.y + 0] * (1.0 / 32.0);
  filterSum += sh_coarseSamples[lid.x + 0][lid.y + 2] * (1.0 / 32.0);
  filterSum += sh_coarseSamples[lid.x + 2][lid.y + 2] * (1.0 / 32.0);

  filterSum += sh_coarseSamples[lid.x + 1][lid.y + 2] * (2.0 / 32.0);
  filterSum += sh_coarseSamples[lid.x + 1][lid.y + 0] * (2.0 / 32.0);
  filterSum += sh_coarseSamples[lid.x + 2][lid.y + 1] * (2.0 / 32.0);
  filterSum += sh_coarseSamples[lid.x + 0][lid.y + 1] * (2.0 / 32.0);

  filterSum += sh_coarseSamples[lid.x + 1][lid.y + 1] * (4.0 / 32.0);

  filterSum += sh_preciseSamples[lid.x + 0][lid.y + 0] * (4.0 / 32.0);
  filterSum += sh_preciseSamples[lid.x + 1][lid.y + 0] * (4.0 / 32.0);
  filterSum += sh_preciseSamples[lid.x + 0][lid.y + 1] * (4.0 / 32.0);
  filterSum += sh_preciseSamples[lid.x + 1][lid.y + 1] * (4.0 / 32.0);

  // Uncomment to debug
  // filterSum = vec3(0);
  // vec2 texel = 1.0 / uniforms.sourceDim;
  // filterSum += textureLod(s_source, uv + texel * vec2(-2, -2), uniforms.sourceLod).rgb * (1.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(2, -2) , uniforms.sourceLod).rgb * (1.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(-2, 2) , uniforms.sourceLod).rgb * (1.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(2, 2)  , uniforms.sourceLod).rgb * (1.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(0, 2)  , uniforms.sourceLod).rgb * (2.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(0, -2) , uniforms.sourceLod).rgb * (2.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(2, 0)  , uniforms.sourceLod).rgb * (2.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(-2, 0) , uniforms.sourceLod).rgb * (2.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(0, 0)  , uniforms.sourceLod).rgb * (4.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(-1, -1), uniforms.sourceLod).rgb * (4.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(1, -1) , uniforms.sourceLod).rgb * (4.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(-1, 1) , uniforms.sourceLod).rgb * (4.0 / 32.0);
  // filterSum += textureLod(s_source, uv + texel * vec2(1, 1)  , uniforms.sourceLod).rgb * (4.0 / 32.0);
  
  imageStore(i_target, gid, vec4(filterSum, 1.0));
}