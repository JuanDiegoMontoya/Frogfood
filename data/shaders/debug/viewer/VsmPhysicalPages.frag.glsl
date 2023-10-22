#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "../../shadows/vsm/VsmCommon.h.glsl"

layout(binding = 0) uniform ViewerUniforms
{
  int texLayer;
  int texLevel;
}uniforms;

layout(binding = 0) uniform sampler2D s_texture;

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

void main()
{
  const float depth = textureLod(s_texture, v_uv, uniforms.texLevel).x;

  o_color = vec4(vec3(depth), 1);
}