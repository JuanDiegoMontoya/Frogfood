#version 460 core
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_NV_gpu_shader5 : enable

#define M_GOLDEN_CONJ 0.6180339887498948482045868343656

#define MAX_INDICES 64
#define MAX_PRIMITIVES 64
#define MESHLET_ID_BITS 26u
#define MESHLET_PRIMITIVE_BITS 6u
#define MESHLET_PRIMITIVE_MASK ((1u << MESHLET_PRIMITIVE_BITS) - 1u)
#define MESHLET_ID_MASK ((1u << MESHLET_ID_BITS) - 1u)

layout (r32ui, binding = 0) uniform restrict readonly uimage2D visbuffer;

layout (location = 0) in vec2 i_uv;

layout (location = 0) out vec4 o_pixel;

vec3 hsv_to_rgb(in vec3 hsv) {
    const vec3 rgb = clamp(abs(mod(hsv.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return hsv.z * mix(vec3(1.0), rgb, hsv.y);
}

void main() {
  const ivec2 position = ivec2(gl_FragCoord.xy);
  const uint payload = imageLoad(visbuffer, position).x;
  if (payload == ~0u) {
    discard;
  }
  const uint meshletId = (payload >> MESHLET_PRIMITIVE_BITS) & MESHLET_ID_MASK;
  const uint primitiveId = payload & MESHLET_PRIMITIVE_MASK;
  o_pixel = vec4(hsv_to_rgb(vec3(float(primitiveId) * M_GOLDEN_CONJ, 0.875, 0.85)), 1.0);
}
