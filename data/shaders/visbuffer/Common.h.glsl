#ifndef VISBUFFER_COMMON_H
#define VISBUFFER_COMMON_H

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_NV_gpu_shader5 : enable

#define MAX_INDICES 64
#define MAX_PRIMITIVES 64
#define MESHLET_ID_BITS 24u
#define MESHLET_MATERIAL_ID_BITS 23u
#define MESHLET_PRIMITIVE_BITS 8u
#define MESHLET_ID_MASK ((1u << MESHLET_ID_BITS) - 1u)
#define MESHLET_MATERIAL_ID_MASK ((1u << MESHLET_MATERIAL_ID_BITS) - 1u)
#define MESHLET_PRIMITIVE_MASK ((1u << MESHLET_PRIMITIVE_BITS) - 1u)

#define MATERIAL_HAS_BASE_COLOR (1u << 0u)

struct PackedVec2
{
  float x, y;
};

struct PackedVec3
{
  float x, y, z;
};

vec2 PackedToVec2(in PackedVec2 v)
{
  return vec2(v.x, v.y);
}

vec3 PackedToVec3(in PackedVec3 v)
{
  return vec3(v.x, v.y, v.z);
}

struct Vertex
{
  PackedVec3 position;
  uint normal;
  PackedVec2 uv;
};

struct Meshlet
{
  uint vertexOffset;
  uint indexOffset;
  uint primitiveOffset;
  uint indexCount;
  uint primitiveCount;
  uint materialId;
  uint instanceId;
};

struct GpuMaterial
{
  uint flags;
  float alphaCutoff;
  uint pad01;
  uint pad02;
  vec4 baseColorFactor;
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

#ifdef USE_MESHLET_PACKED_BUFFER
layout (std430, binding = 3) restrict writeonly buffer MeshletPackedBuffer
{
  uint data[];
} indexBuffer;
#else
layout (std430, binding = 3) restrict readonly buffer MeshletIndexBuffer
{
  uint indices[];
};
#endif

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

layout (std140, binding = 7) restrict readonly buffer MaterialBuffer
{
  GpuMaterial materials[];
};

#endif // VISBUFFER_COMMON_H