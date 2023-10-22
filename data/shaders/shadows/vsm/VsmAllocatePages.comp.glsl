#version 450 core

#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "VsmCommon.h.glsl"
#include "VsmAllocRequest.h.glsl"

/*
// Traverse the linearized tree with the following indices:
//              0
//        /           \
//       1             2
//     /   \         /   \
//    3     4       5     6
//   /\     /\     /\     /\
//  7  8   9 10   11 12 13 14
//
uint AllocatePage(VsmPageAllocRequest request)
{
  const uint oldest = lastTimeVisible.time[0];
  
  // Current (parent) index
  uint currentTreeIndex = 0;

  // TODO: temp
  const uint numLevels = 4;

  // Traverse down tree to find real index
  for (uint level = 0; level < numLevels - 1; level++)
  {
    // Compare age of children
    const uint leftIndex = currentTreeIndex * 2 + 1;
    const uint rightIndex = currentTreeIndex * 2 + 2;

    const uint lTime = lastTimeVisible.time[leftIndex];
    const uint rTime = lastTimeVisible.time[rightIndex];

    if (rTime < lTime)
    {
      currentTreeIndex = rightIndex;
    }
    else
    {
      currentTreeIndex = leftIndex;
    }
  }

  // The real index is the current tree index minus the number of non-leaf nodes
  const uint realIndex = currentTreeIndex - uint(exp2(numLevels)) - 1;

  uint currentOldest = oldest;

  // Update tree by traversing back up
  for (uint level = 0; level < numLevels; level++)
  {
    // Children are stored at sequential indices (see above diagram).
    // Children with an even index are always the second child, so we can subtract one to find the other child.
    // Likewise, children with an odd index are the first child, so we can just add one to find the other.
    const uint siblingIndex = currentTreeIndex + (currentTreeIndex % 2) == 1 ? 1 : -1;

    const uint sTime = lastTimeVisible.time[siblingIndex];

    // TODO: fix
    if (sTime >= currentOldest)
    {
      break;
    }

    // Traverse to the parent
    currentTreeIndex = (currentTreeIndex - 1) / 2;
  }

  return realIndex;
}
*/

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