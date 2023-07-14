#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "Common.h.glsl"

layout (early_fragment_tests) in;

layout (location = 0) in flat uint i_meshletId;
layout (location = 1) in flat uint i_primitiveId;

layout (location = 0) out uint o_pixel;

void main()
{
  o_pixel = (i_meshletId << MESHLET_PRIMITIVE_BITS) | (i_primitiveId & MESHLET_PRIMITIVE_MASK);
}
