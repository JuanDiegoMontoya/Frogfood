#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "../visbuffer/VisbufferCommon.h.glsl"

void main()
{
  const uint meshletId = (uint(gl_VertexID) >> MESHLET_PRIMITIVE_BITS) & MESHLET_ID_MASK;
  const uint primitiveId = uint(gl_VertexID) & MESHLET_PRIMITIVE_MASK;
  const uint vertexOffset = meshlets[meshletId].vertexOffset;
  const uint indexOffset = meshlets[meshletId].indexOffset;
  const uint primitiveOffset = meshlets[meshletId].primitiveOffset;
  const uint instanceId = meshlets[meshletId].instanceId;
  const uint viewId = uint(gl_BaseInstance);

  const uint primitive = uint(primitives[primitiveOffset + primitiveId]);
  const uint index = indices[indexOffset + primitive];
  const Vertex vertex = vertices[vertexOffset + index];
  const vec3 position = PackedToVec3(vertex.position);
  const vec2 uv = PackedToVec2(vertex.uv);
  const mat4 transform = transforms[instanceId];

  gl_Position = view.viewProj * transform * vec4(position, 1.0);
}