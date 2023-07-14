#ifndef VISBUFFER_COMMON_H
#define VISBUFFER_COMMON_H

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_NV_gpu_shader5 : enable

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

vec3 packedToVec3(in packedVec3 v) {
  return vec3(v.x, v.y, v.z);
}

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

layout (std430, binding = 0) restrict readonly buffer MeshletDataBuffer
{
  Meshlet meshlets[];
};

layout (std430, binding = 1) restrict readonly buffer MeshletPrimitiveBuffer
{
  uint8_t primitives[];
};

layout (std430, binding = 2) restrict readonly buffer MeshletVertexBuffer
{
  Vertex vertices[];
};

layout (std430, binding = 3) restrict buffer MeshletIndexBuffer
{
  uint data[];
} indexBuffer;

layout (std430, binding = 4) restrict readonly buffer TransformBuffer
{
  mat4 transforms[];
};

layout (binding = 5) uniform UBO
{
  mat4 viewProj;
  mat4 oldViewProjUnjittered;
  mat4 viewProjUnjittered;
  mat4 invViewProj;
  mat4 proj;
  vec4 cameraPos;
  uint meshletCount;
};

layout (std430, binding = 6) restrict buffer IndirectDrawCommand
{
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int baseVertex;
  uint baseInstance;
} command;

#endif // VISBUFFER_COMMON_H