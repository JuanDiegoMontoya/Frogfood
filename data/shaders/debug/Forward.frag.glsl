#include "../Resources.h.glsl"

layout(location = 0) in vec2 o_uv;
layout(location = 1) in vec3 o_normal;

layout(location = 0) out vec4 o_color;

void main()
{
  o_color = vec4(o_normal * 0.5 + 0.5, 1.0);
}