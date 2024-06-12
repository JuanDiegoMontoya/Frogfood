#version 450 core

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "VsmCommon.h.glsl"

// Indirect dispatch params for clearing dirty pages (these pages will then be rendered)
FVOG_DECLARE_STORAGE_BUFFERS(VsmPageClearDispatchParamsBuffers)
{
  uint groupCountX;
  uint groupCountY;
  uint groupCountZ;
}pageClearDispatchBuffers[];

#define pageClearDispatch pageClearDispatchBuffers[pageClearDispatchIndex]

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
  const ivec3 gid = ivec3(gl_GlobalInvocationID.xyz);

  if (any(greaterThanEqual(gid, imageSize(i_pageTables))))
  {
    return;
  }

  const uint pageData = imageLoad(i_pageTables, gid).x;

  if (GetIsPageBacked(pageData) && GetIsPageDirty(pageData))
  {
    if (TryPushPageClear(GetPagePhysicalAddress(pageData)))
    {
      atomicAdd(pageClearDispatch.groupCountZ, 1);
    }
  }
}