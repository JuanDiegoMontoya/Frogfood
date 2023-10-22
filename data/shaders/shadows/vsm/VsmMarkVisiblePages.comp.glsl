#version 450 core

#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "../../Config.shared.h"
#include "VsmCommon.h.glsl"
#include "VsmAllocRequest.h.glsl"

layout(binding = 0) uniform sampler2D s_gDepth;

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, textureSize(s_gDepth, 0))))
  {
    return;
  }

  const float depthSample = texelFetch(s_gDepth, gid, 0).x;

  if (depthSample == FAR_DEPTH)
  {
    return;
  }

  PageAddressInfo addr = GetClipmapPageFromDepth(depthSample, gid, textureSize(s_gDepth, 0));

  const uint pageData = imageAtomicOr(i_pageTables, addr.pageAddress, PAGE_VISIBLE_BIT);

  if ((vsmUniforms.debugFlags & VSM_FORCE_DIRTY_VISIBLE_PAGES) != 0)
  {
    imageAtomicOr(i_pageTables, addr.pageAddress, PAGE_DIRTY_BIT);
  }

  if (!GetIsPageVisible(pageData))
  {
    if (GetIsPageBacked(pageData))
    {
      // Mark visible in bitmask so allocator doesn't overwrite
      const uint physicalAddress = GetPagePhysicalAddress(pageData);
      atomicOr(visiblePagesBitmask.data[physicalAddress / 32], 1 << (physicalAddress % 32));
    }
    else
    {
      VsmPageAllocRequest request;
      request.pageTableAddress = addr.pageAddress;
      request.pageTableLevel = 0; // TODO: change for lower-res mipmaps
      TryPushAllocRequest(request);
    }
  }
}