#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "VisbufferCommon.h.glsl"

#define M_GOLDEN_CONJ 0.6180339887498948482045868343656

//layout(r32ui, binding = 0) uniform restrict readonly uimage2D visbuffer;
FVOG_DECLARE_STORAGE_IMAGES(uimage2D);

layout (location = 0) in vec2 i_uv;

void main()
{
  const ivec2 position = ivec2(gl_FragCoord.xy);
  const uint payload = imageLoad(FvogGetStorageImage(uimage2D, visbufferIndex), position).x;
  if (payload == ~0u)
  {
    discard;
  }
  const uint meshletId = (payload >> MESHLET_PRIMITIVE_BITS) & MESHLET_ID_MASK;
  const uint materialId = d_meshlets[meshletId].materialId;
  gl_FragDepth = uintBitsToFloat(0x3f7fffffu - (materialId & MESHLET_MATERIAL_ID_MASK));
}
