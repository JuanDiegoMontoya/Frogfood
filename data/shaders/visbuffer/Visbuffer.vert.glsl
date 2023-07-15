#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "Common.h.glsl"

layout (location = 0) out flat uint o_meshletId;
layout (location = 1) out flat uint o_primitiveId;

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
  const vec3 position = PackedToVec3(vertices[vertexOffset + index].position);
  const mat4 transform = transforms[instanceId];

  o_meshletId = meshletId;
  o_primitiveId = primitiveId / 3;

  gl_Position = viewProj * transform * vec4(position, 1.0);
}