#ifndef DEBUG_COMMON_H
#define DEBUG_COMMON_H

#include "../BasicTypes.h.glsl"

struct DebugAabb
{
  PackedVec3 center;
  PackedVec3 extent;
  PackedVec4 color;
};

layout(binding = 10, std430) restrict buffer DebugAabbBuffer
{
  DrawIndirectCommand drawCommand;
  DebugAabb aabbs[];
} debugAabbBuffer;

bool TryPushDebugAabb(DebugAabb box)
{
  uint index = atomicAdd(debugAabbBuffer.drawCommand.instanceCount, 1);

  // Check if buffer is full
  if (index >= debugAabbBuffer.aabbs.length())
  {
    atomicAdd(debugAabbBuffer.drawCommand.instanceCount, -1);
    return false;
  }

  debugAabbBuffer.aabbs[index] = box;
  return true;
}

#endif // DEBUG_COMMON_H