#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "VisbufferCommon.h.glsl"

layout(std430, binding = 9) restrict buffer MeshletVisbilityBuffer
{
  uint indices[];
} visibleMeshlets;

layout (std430, binding = 3) writeonly buffer MeshletPackedBuffer
{
  uint data[];
} indexBuffer;

shared uint sh_baseIndex;

layout(local_size_x = MAX_PRIMITIVES) in;
void main()
{
  const uint meshletId = visibleMeshlets.indices[gl_WorkGroupID.x];
  const Meshlet meshlet = meshlets[meshletId];

  if (gl_LocalInvocationID.x == 0)
  {
    sh_baseIndex = atomicAdd(indirectCommand.indexCount, meshlet.primitiveCount * 3);
  }

  barrier();

  if (gl_LocalInvocationID.x >= meshlet.primitiveCount)
  {
    return;
  }
  
  const uint baseId = gl_LocalInvocationID.x * 3;
  const uint indexOffset = sh_baseIndex + baseId;
  indexBuffer.data[indexOffset + 0] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((baseId + 0) & MESHLET_PRIMITIVE_MASK);
  indexBuffer.data[indexOffset + 1] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((baseId + 1) & MESHLET_PRIMITIVE_MASK);
  indexBuffer.data[indexOffset + 2] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((baseId + 2) & MESHLET_PRIMITIVE_MASK);
}