#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "VisbufferCommon.h.glsl"

layout (location = 0) out vec2 v_uv;
layout (location = 1) out flat uint v_materialId;

void main()
{
  v_materialId = gl_BaseInstance;
  const highp float materialId = uintBitsToFloat(0x3f7fffff - (uint(gl_BaseInstance) & MESHLET_MATERIAL_ID_MASK));
  vec2 pos = vec2(gl_VertexIndex == 0, gl_VertexIndex == 2);
  v_uv = pos.xy * 2.0;
  gl_Position = vec4(pos * 4.0 - 1.0, materialId, 1.0);
}
