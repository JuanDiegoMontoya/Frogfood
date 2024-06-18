#version 460 core

#define VISBUFFER_NO_PUSH_CONSTANTS
#include "../visbuffer/VisbufferCommon.h.glsl"
#include "vsm/VsmCommon.h.glsl"

layout(location = 0) out vec2 v_uv;
layout(location = 1) out uint v_materialId;

void main()
{
  const uint meshletInstanceId = (uint(gl_VertexIndex) >> MESHLET_PRIMITIVE_BITS) & MESHLET_ID_MASK;
  const uint primitiveId = uint(gl_VertexIndex) & MESHLET_PRIMITIVE_MASK;
  const MeshletInstance meshletInstance = d_meshletInstances[meshletInstanceId];
  const Meshlet meshlet = d_meshlets[meshletInstance.meshletId];
  const uint vertexOffset = meshlet.vertexOffset;
  const uint indexOffset = meshlet.indexOffset;
  const uint primitiveOffset = meshlet.primitiveOffset;
  const uint instanceId = meshletInstance.instanceId;

  const uint primitive = uint(d_primitives[primitiveOffset + primitiveId]);
  const uint index = d_indices[indexOffset + primitive];
  const Vertex vertex = d_vertices[vertexOffset + index];
  const vec3 position = PackedToVec3(vertex.position);
  const mat4 transform = d_transforms[instanceId].modelCurrent;

  v_materialId = meshletInstance.materialId;
  v_uv = PackedToVec2(vertex.uv);
  gl_Position = d_currentView.viewProj * transform * vec4(position, 1.0);
}