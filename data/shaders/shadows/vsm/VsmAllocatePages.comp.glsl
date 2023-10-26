#version 450 core

#extension GL_GOOGLE_include_directive : enable

#include "VsmCommon.h.glsl"
#include "VsmAllocRequest.h.glsl"

void AllocatePageSimple(VsmPageAllocRequest request)
{
  // Scan for available (not visible) page (we will ignore off-screen caching for now)
  for (uint i = 0; i < visiblePagesBitmask.data.length(); i++)
  {
    // Find least significant (rightmost) zero bit
    const int lsb = findLSB(~visiblePagesBitmask.data[i]);
    if (lsb != -1)
    {
      const uint pageIndex = i * 32 + lsb;

      // TODO: use inout var to indicate start pos instead of writing to global mem
      visiblePagesBitmask.data[i] |= 1 << lsb;

      uint pageData = imageLoad(i_pageTables, request.pageTableAddress).x;
      pageData = SetPagePhysicalAddress(pageData, pageIndex);
      pageData = SetIsPageDirty(pageData, true);
      pageData = SetIsPageBacked(pageData, true);
      imageStore(i_pageTables, request.pageTableAddress, uvec4(pageData, 0, 0, 0));
      return;
    }
  }

  // Failed to find free page, should never happen
}

layout(local_size_x = 1, local_size_y = 1) in;
void main()
{
  for (uint i = 0; i < allocRequests.count; i++)
  {
    VsmPageAllocRequest request = allocRequests.data[i];
    AllocatePageSimple(request);
  }

  allocRequests.count = 0;
}