#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "VsmCommon.h.glsl"

layout(binding = 1, r32ui) uniform restrict uimage2D i_physicalPagesUint;

layout(binding = 0, std140) uniform VsmShadowUniforms
{
  uint clipmapLod;
};

uint AtomicMinPageTexel(ivec2 texel, uint page, float value)
{
  const int atlasWidth = imageSize(i_physicalPagesUint).x / PAGE_SIZE;
  const ivec2 pageCorner = PAGE_SIZE * ivec2(page / atlasWidth, page % atlasWidth);
  return imageAtomicMin(i_physicalPagesUint, pageCorner + texel, floatBitsToUint(value));
}

void main()
{
  const uint clipmapIndex = clipmapUniforms.clipmapTableIndices[clipmapLod];
  const ivec2 pageOffset = clipmapUniforms.clipmapPageOffsets[clipmapLod];
  const ivec2 pageAddressXy = (ivec2(gl_FragCoord.xy) / PAGE_SIZE + pageOffset) % imageSize(i_pageTables).xy;
  const uint pageData = imageLoad(i_pageTables, ivec3(pageAddressXy, clipmapIndex)).x;
  if (GetIsPageBacked(pageData) && GetIsPageDirty(pageData))
  {
    const ivec2 pageTexel = ivec2(gl_FragCoord.xy) % PAGE_SIZE;
    AtomicMinPageTexel(pageTexel, GetPagePhysicalAddress(pageData), gl_FragCoord.z);
  }
}