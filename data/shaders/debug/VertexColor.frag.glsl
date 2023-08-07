#version 450 core

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 o_color;

// FSR 2 reactive mask. Unused when FSR 2 is disabled
layout(location = 1) out float o_reactiveMask;

void main()
{
  o_color = v_color;
  
  // Values above 0.9 are not recommended for use, as they are "unlikely [...] to ever produce good results"
  o_reactiveMask = min(o_color.a, 0.9);
}