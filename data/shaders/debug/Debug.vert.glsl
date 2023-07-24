#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "../GlobalUniforms.h.glsl"

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_color;

layout(location = 0) out vec4 v_color;

void main()
{
  v_color = a_color;
  gl_Position = perFrameUniforms.viewProj * vec4(a_pos, 1.0);
}