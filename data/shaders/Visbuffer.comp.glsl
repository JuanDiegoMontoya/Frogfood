#version 460 core
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_NV_gpu_shader5 : enable

#define WG_SIZE 256
#define MAX_INDICES 64
#define MAX_PRIMITIVES 64
#define MESHLET_ID_BITS 26u
#define MESHLET_PRIMITIVE_BITS 6u
#define MESHLET_PRIMITIVE_MASK ((1u << MESHLET_PRIMITIVE_BITS) - 1u)
#define MESHLET_ID_MASK ((1u << MESHLET_ID_BITS) - 1u)
#define MESHLET_PER_WG (WG_SIZE / MAX_PRIMITIVES)

layout (local_size_x = WG_SIZE, local_size_y = 1, local_size_z = 1) in;

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
  Meshlet[] meshlets;
};

layout (std430, binding = 1) restrict readonly buffer MeshletPrimitiveBuffer
{
  uint8_t[] primitives;
};

layout (std430, binding = 2) restrict writeonly buffer MeshletIndexBuffer
{
  uint[] data;
} indexBuffer;

layout (std430, binding = 3) restrict buffer IndirectDrawCommand
{
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int baseVertex;
  uint baseInstance;
} command;

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

shared uint baseIndex[MESHLET_PER_WG];
shared uint basePrimitive[MESHLET_PER_WG];
shared uint primitiveCount[MESHLET_PER_WG];

void main()
{
  const uint meshletBaseId = gl_WorkGroupID.x * MESHLET_PER_WG;
  const uint meshletOffset = gl_LocalInvocationID.x / MAX_PRIMITIVES;
  const uint localId = gl_LocalInvocationID.x % MAX_PRIMITIVES;
  const uint meshletId = meshletBaseId + meshletOffset;

  if (meshletId < meshletCount && localId == 0)
  {
    baseIndex[meshletOffset] = atomicAdd(command.indexCount, meshlets[meshletId].primitiveCount * 3);
    basePrimitive[meshletOffset] = meshlets[meshletId].primitiveOffset;
    primitiveCount[meshletOffset] = meshlets[meshletId].primitiveCount;
  }
  barrier();

  if (meshletId < meshletCount && localId < primitiveCount[meshletOffset])
  {
    const uint baseId = localId * 3;
    const uint indexOffset = baseIndex[meshletOffset] + baseId;
    const uint primitiveOffset = basePrimitive[meshletOffset] + baseId;
    indexBuffer.data[indexOffset + 0] = (meshletId << MESHLET_PRIMITIVE_BITS) | (primitives[primitiveOffset + 0] & MESHLET_PRIMITIVE_MASK);
    indexBuffer.data[indexOffset + 1] = (meshletId << MESHLET_PRIMITIVE_BITS) | (primitives[primitiveOffset + 1] & MESHLET_PRIMITIVE_MASK);
    indexBuffer.data[indexOffset + 2] = (meshletId << MESHLET_PRIMITIVE_BITS) | (primitives[primitiveOffset + 2] & MESHLET_PRIMITIVE_MASK);
  }
}