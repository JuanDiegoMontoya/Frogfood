#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "VisbufferCommon.h.glsl"

layout (location = 0) out flat uint o_meshletId;
layout (location = 1) out flat uint o_primitiveId;
layout (location = 2) out vec2 o_uv;

void main()
{
  const uint meshletId = (uint(gl_VertexID) >> MESHLET_PRIMITIVE_BITS) & MESHLET_ID_MASK;
  const uint primitiveId = uint(gl_VertexID) & MESHLET_PRIMITIVE_MASK;
  const uint vertexOffset = meshlets[meshletId].vertexOffset;
  const uint indexOffset = meshlets[meshletId].indexOffset;
  const uint primitiveOffset = meshlets[meshletId].primitiveOffset;
  const uint instanceId = meshlets[meshletId].instanceId;

  const uint primitive = uint(primitives[primitiveOffset + primitiveId]);
  const uint index = indices[indexOffset + primitive];
  const Vertex vertex = vertices[vertexOffset + index];
  const vec3 position = PackedToVec3(vertex.position);
  const vec2 uv = PackedToVec2(vertex.uv);
  const mat4 transform = transforms[instanceId];

  o_meshletId = meshletId;
  o_primitiveId = primitiveId / 3;
  o_uv = uv;

  gl_Position = viewProj * transform * vec4(position, 1.0);
}