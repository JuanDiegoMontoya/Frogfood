#ifndef VISBUFFER_COMMON_H
#define VISBUFFER_COMMON_H

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_NV_gpu_shader5 : enable
#extension GL_EXT_nonuniform_qualifier : enable

// On modern AMD platforms, this extension is implicitly supported.
// On all NV platforms, GL_NV_gpu_shader5 is sufficient to non-uniformly index sampler arrays.
// On older AMD or Intel platforms, another solution (such as a manual waterfall loop) should be used.
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

struct View {
  mat4 oldProj;
  mat4 oldView;
  mat4 oldViewProj;
  mat4 proj;
  mat4 view;
  mat4 viewProj;
  vec4 cameraPos;
  vec4 frustumPlanes[6];
  vec4 viewport;
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
  uint meshletCount;
  uint maxIndices;
  float bindlessSamplerLodBias;
};

layout (std430, binding = 6) restrict buffer IndirectDrawCommand
{
  DrawElementsIndirectCommand[] commands;
};

layout (std140, binding = 7) restrict readonly buffer MaterialBuffer
{
  GpuMaterial materials[];
};

layout (std140, binding = 8) restrict readonly buffer ViewBuffer
{
  View views[];
};

#endif // VISBUFFER_COMMON_H