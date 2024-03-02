//? #version 450
#ifndef DEBUG_COMMON_H
#define DEBUG_COMMON_H

#include "../Resources.h.glsl"
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

//layout(binding = 11, std430) restrict buffer DebugAabbBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict DebugAabbBuffer)
{
  DrawIndirectCommand drawCommand;
  DebugAabb aabbs[];
} debugAabbBuffers[];

//layout(binding = 12, std430) restrict buffer DebugRectBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict DebugRectBuffer)
{
  DrawIndirectCommand drawCommand;
  DebugRect rects[];
} debugRectBuffers[];

// World-space box
bool TryPushDebugAabb(uint bufferIndex, DebugAabb box)
{
  uint index = atomicAdd(debugAabbBuffers[bufferIndex].drawCommand.instanceCount, 1);

  // Check if buffer is full
  if (index >= debugAabbBuffers[bufferIndex].aabbs.length())
  {
    atomicAdd(debugAabbBuffers[bufferIndex].drawCommand.instanceCount, -1);
    return false;
  }

  debugAabbBuffers[bufferIndex].aabbs[index] = box;
  return true;
}

// UV-space rect
bool TryPushDebugRect(uint bufferIndex, DebugRect rect)
{
  uint index = atomicAdd(debugRectBuffers[bufferIndex].drawCommand.instanceCount, 1);

  // Check if buffer is full
  if (index >= debugRectBuffers[bufferIndex].rects.length())
  {
    atomicAdd(debugRectBuffers[bufferIndex].drawCommand.instanceCount, -1);
    return false;
  }

  debugRectBuffers[bufferIndex].rects[index] = rect;
  return true;
}

#endif // DEBUG_COMMON_H