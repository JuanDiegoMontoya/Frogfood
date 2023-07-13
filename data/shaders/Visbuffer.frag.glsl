#version 460 core
#define MAX_INDICES 64
#define MAX_PRIMITIVES 64
#define MESHLET_ID_BITS 26u
#define MESHLET_PRIMITIVE_BITS 6u
#define MESHLET_PRIMITIVE_MASK ((1u << MESHLET_PRIMITIVE_BITS) - 1u)
#define MESHLET_ID_MASK ((1u << MESHLET_ID_BITS) - 1u)

layout (early_fragment_tests) in;

layout (location = 0) in flat uint i_meshletId;

layout (location = 0) out uint o_pixel;

void main() {
  o_pixel = (i_meshletId << MESHLET_PRIMITIVE_BITS) | (uint(gl_PrimitiveID) & MESHLET_PRIMITIVE_MASK);
}
