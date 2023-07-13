#version 460 core
#define MAX_INDICES 64
#define MAX_PRIMITIVES 64
#define MESHLET_ID_BITS 26u
#define MESHLET_PRIMITIVE_BITS 6u
#define MESHLET_PRIMITIVE_MASK ((1u << MESHLET_PRIMITIVE_BITS) - 1u)
#define MESHLET_ID_MASK ((1u << MESHLET_ID_BITS) - 1u)

struct packedVec2 {
  float x, y;
};

struct packedVec3 {
  float x, y, z;
};

struct Vertex {
  packedVec3 position;
  uint normal;
  packedVec2 tangent;
};

struct Meshlet {
  uint vertexOffset;
  uint indexOffset;
  uint primitiveOffset;
  uint indexCount;
  uint primitiveCount;
  uint materialId;
  uint instanceId;
};

layout (location = 0) out flat uint o_meshletId;

layout (std430, binding = 0) restrict readonly buffer MeshletDataBuffer
{
  Meshlet[] meshlets;
};

layout (std430, binding = 1) restrict readonly buffer MeshletVertexBuffer
{
  Vertex[] vertices;
};

layout (std430, binding = 2) restrict readonly buffer MeshletIndexBuffer
{
  uint[] indices;
};

layout (std430, binding = 3) restrict readonly buffer TransformBuffer
{
  mat4[] transforms;
};

layout (binding = 4) uniform UBO
{
  mat4 viewProj;
  mat4 oldViewProjUnjittered;
  mat4 viewProjUnjittered;
  mat4 invViewProj;
  mat4 proj;
  vec4 cameraPos;
  uint meshletCount;
};

vec3 packedToVec3(in packedVec3 v) {
  return vec3(v.x, v.y, v.z);
}

void main() {
  const uint meshletId = (uint(gl_VertexID) >> MESHLET_PRIMITIVE_BITS) & MESHLET_ID_MASK;
  const uint primitiveId = uint(gl_VertexID) & MESHLET_PRIMITIVE_MASK;
  const uint vertexOffset = meshlets[meshletId].vertexOffset;
  const uint indexOffset = meshlets[meshletId].indexOffset;
  const uint instanceId = meshlets[meshletId].instanceId;

  const uint index = indices[indexOffset + primitiveId];
  const vec3 position = packedToVec3(vertices[vertexOffset + index].position);
  const mat4 transform = transforms[instanceId];

  o_meshletId = meshletId;

  gl_Position = viewProj * transform * vec4(position, 1.0);
}