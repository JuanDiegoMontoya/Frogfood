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

struct DebugLine
{
  PackedVec3 aPosition;
  PackedVec4 aColor;
  PackedVec3 bPosition;
  PackedVec4 bColor;
};

FVOG_DECLARE_STORAGE_BUFFERS(restrict DebugAabbBuffer)
{
  DrawIndirectCommand drawCommand;
  DebugAabb aabbs[];
} debugAabbBuffers[];

FVOG_DECLARE_STORAGE_BUFFERS(restrict DebugRectBuffer)
{
  DrawIndirectCommand drawCommand;
  DebugRect rects[];
} debugRectBuffers[];

FVOG_DECLARE_STORAGE_BUFFERS(restrict DebugLineBuffer)
{
  DrawIndirectCommand drawCommand;
  DebugLine lines[];
} debugLineBuffers[];

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

// World-space line
bool TryPushDebugLine(Buffer lineBuffer, DebugLine line)
{
  uint index = atomicAdd(debugLineBuffers[lineBuffer.bufIdx].drawCommand.instanceCount, 1);

  // Check if buffer is full
  if (index >= debugLineBuffers[lineBuffer.bufIdx].lines.length())
  {
    atomicAdd(debugLineBuffers[lineBuffer.bufIdx].drawCommand.instanceCount, -1);
    return false;
  }

  debugLineBuffers[lineBuffer.bufIdx].lines[index] = line;
  return true;
}
#endif // DEBUG_COMMON_H