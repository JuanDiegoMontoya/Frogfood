// #version 450
#ifndef VISBUFFER_COMMON_H
#define VISBUFFER_COMMON_H

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_8bit_storage : require

#include "../Resources.h.glsl"
#include "../BasicTypes.h.glsl"
#include "../Utility.h.glsl"
#include "../GlobalUniforms.h.glsl"

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

#define VIEW_TYPE_MAIN    (0)
#define VIEW_TYPE_VIRTUAL (1)

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
  //uint instanceId;
  PackedVec3 aabbMin;
  PackedVec3 aabbMax;
};

struct MeshletInstance
{
  uint meshletId;
  uint instanceId;
  uint materialId;
};

struct View
{
  mat4 oldProj;
  mat4 oldView;
  mat4 oldViewProj;
  mat4 proj;
  mat4 view;
  mat4 viewProj;
  mat4 viewProjStableForVsmOnly;
  vec4 cameraPos;
  vec4 frustumPlanes[6];
  vec4 viewport;
  uint type;
  uint virtualTableIndex;
  uvec2 _padding;
};

struct GpuMaterial
{
  FVOG_UINT32 flags;
  FVOG_FLOAT alphaCutoff;
  FVOG_FLOAT metallicFactor;
  FVOG_FLOAT roughnessFactor;
  FVOG_VEC4 baseColorFactor;
  FVOG_VEC3 emissiveFactor;
  FVOG_FLOAT emissiveStrength;
  FVOG_FLOAT normalXyScale;
  FVOG_UINT32 baseColorTextureIndex;
  FVOG_UINT32 metallicRoughnessTextureIndex;
  FVOG_UINT32 normalTextureIndex;
  FVOG_UINT32 occlusionTextureIndex;
  FVOG_UINT32 emissionTextureIndex;
  FVOG_UINT32 _padding[2];
};

#ifndef VISBUFFER_NO_PUSH_CONSTANTS
FVOG_DECLARE_ARGUMENTS(VisbufferPushConstants)
{
  // Common
  FVOG_UINT32 globalUniformsIndex;
  FVOG_UINT32 meshletInstancesIndex;
  FVOG_UINT32 meshletDataIndex;
  FVOG_UINT32 meshletPrimitivesIndex;
  FVOG_UINT32 meshletVerticesIndex;
  FVOG_UINT32 meshletIndicesIndex;
  FVOG_UINT32 transformsIndex;
  FVOG_UINT32 indirectDrawIndex;
  FVOG_UINT32 materialsIndex;
  FVOG_UINT32 viewIndex;

  // CullMeshlets.comp
  FVOG_UINT32 hzbIndex;
  FVOG_UINT32 hzbSamplerIndex;
  FVOG_UINT32 cullTrianglesDispatchIndex;

  // CullMeshlets.comp and CullTriangles.comp
  FVOG_UINT32 visibleMeshletsIndex;

  // CullTriangles.comp
  FVOG_UINT32 indexBufferIndex;

  // Visbuffer.frag
  FVOG_UINT32 materialSamplerIndex;
  
  // VisbufferMaterialDepth.frag
  FVOG_UINT32 visbufferIndex;

  // Debug
  FVOG_UINT32 debugAabbBufferIndex;
  FVOG_UINT32 debugRectBufferIndex;
};
#endif

#define d_perFrameUniforms perFrameUniformsBuffers[globalUniformsIndex]

//layout (std430, binding = 0) restrict readonly buffer MeshletDataBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly MeshletDataBuffer)
{
  Meshlet meshlets[];
}MeshletDataBuffers[];

#define d_meshlets MeshletDataBuffers[meshletDataIndex].meshlets

FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly MeshletInstancesBuffer)
{
  MeshletInstance instances[];
}MeshletInstancesBuffers[];

#define d_meshletInstances MeshletInstancesBuffers[meshletInstancesIndex].instances

//layout (std430, binding = 1) restrict readonly buffer MeshletPrimitiveBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly MeshletPrimitiveBuffer)
{
  uint8_t primitives[];
}MeshletPrimitiveBuffers[];

#define d_primitives MeshletPrimitiveBuffers[meshletPrimitivesIndex].primitives

//layout (std430, binding = 2) restrict readonly buffer MeshletVertexBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly MeshletVertexBuffer)
{
  Vertex vertices[];
}MeshletVertexBuffers[];

#define d_vertices MeshletVertexBuffers[meshletVerticesIndex].vertices

//layout (std430, binding = 3) restrict readonly buffer MeshletIndexBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly MeshletIndexBuffer)
{
  uint indices[];
}MeshletIndexBuffers[];

#define d_indices MeshletIndexBuffers[meshletIndicesIndex].indices

struct ObjectUniforms
{
  mat4 modelPrevious;
  mat4 modelCurrent;
};

//layout (std430, binding = 4) restrict readonly buffer TransformBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly TransformBuffer)
{
  ObjectUniforms transforms[];
}TransformBuffers[];

#define d_transforms TransformBuffers[transformsIndex].transforms

//layout (std430, binding = 6) restrict buffer IndirectDrawCommand
FVOG_DECLARE_STORAGE_BUFFERS(restrict IndirectDrawCommand)
{
  DrawElementsIndirectCommand indirectCommand;
}IndirectDrawCommands[];

#define d_indirectCommand IndirectDrawCommands[indirectDrawIndex].indirectCommand

//layout (std140, binding = 7) restrict readonly buffer MaterialBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly MaterialBuffer)
{
  GpuMaterial materials[];
}MaterialBuffers[];

#define d_materials MaterialBuffers[materialsIndex].materials

//layout (std140, binding = 8) restrict readonly buffer ViewBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly ViewBuffer)
{
  View currentView;
}ViewBuffers[];

#define d_currentView ViewBuffers[viewIndex].currentView

#endif // VISBUFFER_COMMON_H