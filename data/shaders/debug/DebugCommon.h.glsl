#ifndef DEBUG_COMMON_H
#define DEBUG_COMMON_H

#include "../BasicTypes.h.glsl"

struct DebugAabb
{
  PackedVec3 center;
  PackedVec3 extent;
  PackedVec4 color;
};

struct DebugRect
{
  PackedVec2 minOffset;
  PackedVec2 maxOffset;
  PackedVec4 color;
  float depth;
};

layout(binding = 11, std430) restrict buffer DebugAabbBuffer
{
  DrawIndirectCommand drawCommand;
  DebugAabb aabbs[];
} debugAabbBuffer;

layout(binding = 12, std430) restrict buffer DebugRectBuffer
{
  DrawIndirectCommand drawCommand;
  DebugRect rects[];
} debugRectBuffer;

// World-space box
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

// UV-space rect
bool TryPushDebugRect(DebugRect rect)
{
  uint index = atomicAdd(debugRectBuffer.drawCommand.instanceCount, 1);

  // Check if buffer is full
  if (index >= debugRectBuffer.rects.length())
  {
    atomicAdd(debugRectBuffer.drawCommand.instanceCount, -1);
    return false;
  }

  debugRectBuffer.rects[index] = rect;
  return true;
}

#endif // DEBUG_COMMON_H