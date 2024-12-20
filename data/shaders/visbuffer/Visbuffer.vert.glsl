#include "VisbufferCommon.h.glsl"
#include "../hzb/HZBCommon.h.glsl"

layout (location = 0) out flat uint o_visibleMeshletId;
layout (location = 1) out flat uint o_primitiveId;
layout (location = 2) out vec2 o_uv;
layout (location = 3) out vec3 o_objectSpacePos;
layout (location = 4) out flat uint o_materialId;

void main()
{
  const uint visibleMeshletId = (uint(gl_VertexIndex) >> MESHLET_PRIMITIVE_BITS) & MESHLET_ID_MASK;
  const uint meshletInstanceId = d_visibleMeshlets.indices[visibleMeshletId];
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
  const vec2 uv = PackedToVec2(vertex.uv);
  
  o_visibleMeshletId = visibleMeshletId;
  o_primitiveId = primitiveId / 3;
  o_uv = uv;
  o_objectSpacePos = position;
  o_materialId = d_transforms[instanceId].materialId;

  gl_Position = d_perFrameUniforms.viewProj * transform * vec4(position, 1.0);
}