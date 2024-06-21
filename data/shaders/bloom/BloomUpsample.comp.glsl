#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "BloomCommon.h.glsl"

layout(local_size_x = 16, local_size_y = 16) in;
void main()
{
  ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, uniforms.targetDim)))
    return;

  vec2 texel = 1.0 / uniforms.sourceDim;

  // center of written pixel
  vec2 uv = (vec2(gid) + 0.5) / uniforms.targetDim;

  vec4 rgba = texelFetch(s_target, gid, int(uniforms.targetLod));

  vec4 blurSum = vec4(0);
  blurSum += textureLod(s_source, uv + vec2(-1, -1) * texel * uniforms.width, uniforms.sourceLod) * 1.0 / 16.0;
  blurSum += textureLod(s_source, uv + vec2(0, -1)  * texel * uniforms.width, uniforms.sourceLod) * 2.0 / 16.0;
  blurSum += textureLod(s_source, uv + vec2(1, -1)  * texel * uniforms.width, uniforms.sourceLod) * 1.0 / 16.0;
  blurSum += textureLod(s_source, uv + vec2(-1, 0)  * texel * uniforms.width, uniforms.sourceLod) * 2.0 / 16.0;
  blurSum += textureLod(s_source, uv + vec2(0, 0)   * texel * uniforms.width, uniforms.sourceLod) * 4.0 / 16.0;
  blurSum += textureLod(s_source, uv + vec2(1, 0)   * texel * uniforms.width, uniforms.sourceLod) * 2.0 / 16.0;
  blurSum += textureLod(s_source, uv + vec2(-1, 1)  * texel * uniforms.width, uniforms.sourceLod) * 1.0 / 16.0;
  blurSum += textureLod(s_source, uv + vec2(0, 1)   * texel * uniforms.width, uniforms.sourceLod) * 2.0 / 16.0;
  blurSum += textureLod(s_source, uv + vec2(1, 1)   * texel * uniforms.width, uniforms.sourceLod) * 1.0 / 16.0;
  
  if (bool(uniforms.isFinalPass))
  {
    // Conserve energy
    rgba = mix(rgba, blurSum / uniforms.numPasses, uniforms.strength);
  }
  else
  {
    // Accumulate
    rgba += blurSum;
  }

  imageStore(i_target, gid, rgba);
}