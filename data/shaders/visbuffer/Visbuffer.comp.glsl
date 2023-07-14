#version 460 core
#extension GL_GOOGLE_include_directive : enable
#include "Common.h.glsl"

#define WG_SIZE 256
#define MESHLET_PER_WG (WG_SIZE / MAX_PRIMITIVES)

layout (local_size_x = WG_SIZE, local_size_y = 1, local_size_z = 1) in;

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