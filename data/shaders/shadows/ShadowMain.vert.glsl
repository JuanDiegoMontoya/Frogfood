#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "../visbuffer/VisbufferCommon.h.glsl"

layout(location = 0) out vec2 v_uv;
layout(location = 1) out uint v_meshletId;
layout(location = 2) out vec3 i_objectSpacePos;

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
  const mat4 transform = transforms[instanceId].modelCurrent;

  v_meshletId = meshletId;
  v_uv = PackedToVec2(vertex.uv);
  i_objectSpacePos = position;
  gl_Position = currentView.viewProj * transform * vec4(position, 1.0);
}