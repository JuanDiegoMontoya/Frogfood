#version 450 core

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_leftColor;
layout(location = 2) in vec4 v_rightColor;
layout(location = 3) in float v_middle;

layout(location = 0) out vec4 o_color;

// FSR 2 reactive mask. Unused when FSR 2 is disabled
layout(location = 1) out float o_reactiveMask;

void main()
{
  o_color = mix(v_leftColor, v_rightColor, step(v_middle, v_uv.x));
  
  // Values above 0.9 are not recommended for use, as they are "unlikely [...] to ever produce good results"
  o_reactiveMask = min(o_color.a, 0.9);
}