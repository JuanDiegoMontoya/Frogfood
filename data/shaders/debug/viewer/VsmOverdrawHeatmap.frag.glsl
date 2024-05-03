#version 460 core

#include "../../Config.shared.h"
#include "../../shadows/vsm/VsmCommon.h.glsl"

layout(binding = 0) uniform ViewerUniforms
{
  int texLayer;
  int texLevel;
}uniforms;

layout(binding = 0) uniform usampler2D s_texture;

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

void main()
{
  const uint overdraw = textureLod(s_texture, v_uv, uniforms.texLevel).x;

  o_color = vec4(vec3(TurboColormap(overdraw / VSM_MAX_OVERDRAW)), 1);
}