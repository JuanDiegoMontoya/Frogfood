#ifndef VISBUFFER_COMMON_H
#define VISBUFFER_COMMON_H

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_NV_gpu_shader5 : enable
#extension GL_EXT_nonuniform_qualifier : enable

#ifdef GL_EXT_nonuniform_qualifier
#define NonUniformIndex(x) nonuniformEXT(x)
#else
#define NonUniformIndex(x) (x)
#endif

#include "../BasicTypes.h.glsl"
#include "../Utility.h.glsl"

#define MAX_INDICES 64
#define MAX_PRIMITIVES 64
#define MESHLET_ID_BITS 24u
#define MESHLET_MATERIAL_ID_BITS 23u
#define MESHLET_PRIMITIVE_BITS 8u
#define MESHLET_ID_MASK ((1u << MESHLET_ID_BITS) - 1u)
#define MESHLET_MATERIAL_ID_MASK ((1u << MESHLET_MATERIAL_ID_BITS) - 1u)
#define MESHLET_PRIMITIVE_MASK ((1u << MESHLET_PRIMITIVE_BITS) - 1u)

#define MATERIAL_HAS_BASE_COLOR         (1u << 0u)
#define MATERIAL_HAS_METALLIC_ROUGHNESS (1u << 1u)
#define MATERIAL_HAS_NORMAL             (1u << 2u)
#define MATERIAL_HAS_OCCLUSION          (1u << 3u)
#define MATERIAL_HAS_EMISSION           (1u << 4u)

struct Vertex
{
  PackedVec3 position;
  uint normal; // Octahedral encoding: decode with unpackSnorm2x16 and OctToFloat32x3
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
  PackedVec3 aabbMin;
  PackedVec3 aabbMax;
};

struct GpuMaterial
{
  uint flags;
  float alphaCutoff;
  float metallicFactor;
  float roughnessFactor;
  vec4 baseColorFactor;
  vec3 emissiveFactor;
  float emissiveStrength;
  uvec2 baseColorTextureHandle;
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

layout (std430, binding = 3) restrict readonly buffer MeshletIndexBuffer
{
  uint indices[];
};

layout (std430, binding = 4) restrict readonly buffer TransformBuffer
{
  mat4 transforms[];
};

layout (binding = 5, std140) uniform PerFrameUniforms
{
  mat4 viewProj;
  mat4 oldViewProjUnjittered;
  mat4 viewProjUnjittered;
  mat4 invViewProj;
  mat4 proj;
  vec4 cameraPos;
  vec4 frustumPlanes[6];
  uint meshletCount;
  float bindlessSamplerLodBias;
};

layout (std430, binding = 6) restrict buffer IndirectDrawCommand
{
  DrawElementsIndirectCommand command;
};

layout (std140, binding = 7) restrict readonly buffer MaterialBuffer
{
  GpuMaterial materials[];
};

#endif // VISBUFFER_COMMON_H