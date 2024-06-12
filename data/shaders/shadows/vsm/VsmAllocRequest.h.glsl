// #version 450 core
// #extension GL_GOOGLE_include_directive : enable
// #include "../../Math.h.glsl"
// #include "../../GlobalUniforms.h.glsl"
#ifndef VSM_ALLOC_REQUEST_H
#define VSM_ALLOC_REQUEST_H

struct VsmPageAllocRequest
{
  // Address of the requester
  ivec3 pageTableAddress;

  // Unused until local lights are supported
  uint pageTableLevel;
};

FVOG_DECLARE_STORAGE_BUFFERS(VsmPageAllocRequests)
{
  uint count;
  VsmPageAllocRequest data[];
}allocRequestsBuffers[];

#define allocRequests allocRequestsBuffers[allocRequestsIndex]

FVOG_DECLARE_STORAGE_BUFFERS(VsmVisiblePagesBitmask)
{
  uint data[];
}visiblePagesBitmaskBuffers[];

#define visiblePagesBitmask visiblePagesBitmaskBuffers[visiblePagesBitmaskIndex]

// layout(binding = 2, std430) restrict buffer VsmVisibleTimeTree
// {
//   uint time[];
// }lastTimeVisible;

bool TryPushAllocRequest(VsmPageAllocRequest request)
{
  uint index = atomicAdd(allocRequests.count, 1);

  if (index >= allocRequests.data.length())
  {
    atomicAdd(allocRequests.count, -1);
    return false;
  }

  allocRequests.data[index] = request;
  return true;
}

#endif // VSM_ALLOC_REQUEST_H