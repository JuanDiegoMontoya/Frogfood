#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "Common.h.glsl"

layout (location = 0) out flat uint o_meshletId;

void main() {
  const uint meshletId = (uint(gl_VertexID) >> MESHLET_PRIMITIVE_BITS) & MESHLET_ID_MASK;
  const uint primitiveId = uint(gl_VertexID) & MESHLET_PRIMITIVE_MASK;
  const uint vertexOffset = meshlets[meshletId].vertexOffset;
  const uint indexOffset = meshlets[meshletId].indexOffset;
  const uint instanceId = meshlets[meshletId].instanceId;

  const uint index = indexBuffer.data[indexOffset + primitiveId];
  const vec3 position = packedToVec3(vertices[vertexOffset + index].position);
  const mat4 transform = transforms[instanceId];

  o_meshletId = meshletId;

  gl_Position = viewProj * transform * vec4(position, 1.0);
}