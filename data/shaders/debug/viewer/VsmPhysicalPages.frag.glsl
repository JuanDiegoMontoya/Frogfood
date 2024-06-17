#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "../../Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(ViewerUniforms)
{
  uint textureIndex;
  uint samplerIndex;
  int texLayer;
  int texLevel;
}pc;

FVOG_DECLARE_SAMPLERS;
FVOG_DECLARE_SAMPLED_IMAGES(texture2D);

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

void main()
{
  const float depth = textureLod(Fvog_sampler2D(pc.textureIndex, pc.samplerIndex), v_uv, pc.texLevel).x;

  o_color = vec4(vec3(depth), 1);
}