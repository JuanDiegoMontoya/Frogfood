#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "VsmCommon.h.glsl"

layout(binding = 0, std140) uniform VsmShadowUniforms
{
  uint clipmapLod;
};

void main()
{
  const uint clipmapIndex = clipmapUniforms.clipmapTableIndices[clipmapLod];
  const ivec2 pageOffset = clipmapUniforms.clipmapPageOffsets[clipmapLod];
  const ivec2 pageAddressXy = (ivec2(gl_FragCoord.xy) / PAGE_SIZE + pageOffset) % imageSize(i_pageTables).xy;
  const uint pageData = imageLoad(i_pageTables, ivec3(pageAddressXy, clipmapLod)).x;
  const ivec2 pageTexel = ivec2(gl_FragCoord.xy) % PAGE_SIZE;
  if (GetIsPageBacked(pageData) && GetIsPageDirty(pageData))
  {
    AtomicMinPageTexel(pageTexel, GetPagePhysicalAddress(pageData), floatBitsToUint(gl_FragCoord.z));
  }
}