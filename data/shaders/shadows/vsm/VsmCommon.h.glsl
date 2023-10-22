// #version 450 core
// #extension GL_GOOGLE_include_directive : enable
// #include "../../Math.h.glsl"
// #include "../../GlobalUniforms.h.glsl"
#ifndef VSM_COMMON_H
#define VSM_COMMON_H

#define PAGE_SIZE 128
#define MAX_CLIPMAPS 32

#define PAGE_VISIBLE_BIT (1u)
#define PAGE_DIRTY_BIT (2u)
#define PAGE_BACKED_BIT (4u)

////////////// Resources
layout(binding = 0, r32ui) uniform restrict uimage2DArray i_pageTables; // Level 0
layout(binding = 1, r32f) uniform restrict image2D i_physicalPages;   // Level 0
layout(binding = 10) uniform usampler2DArray s_vsmBitmaskHzb;

layout(binding = 5, std430) restrict readonly buffer VsmMarkPagesDirectionalUniforms
{
  mat4 clipmapViewProjections[MAX_CLIPMAPS];
  uint clipmapTableIndices[MAX_CLIPMAPS];
  ivec2 clipmapPageOffsets[MAX_CLIPMAPS];
  uint numClipmaps;

  // The length, in world space, of a side of single (square) texel in the first clipmap
  float firstClipmapTexelLength;
  float projectionZLength;
}clipmapUniforms;

#define VSM_HZB_PHYSICAL_RETURN_ONE (1 << 0)
#define VSM_HZB_VIRTUAL_RETURN_ONE (1 << 1)
#define VSM_FORCE_DIRTY_VISIBLE_PAGES (1 << 2)

layout(binding = 6, std140) uniform VsmGlobalUniforms
{
  float lodBias;
  uint debugFlags;
}vsmUniforms;

layout(binding = 3, std430) restrict buffer VsmDirtyPageList
{
  uint count;
  uint data[];
}dirtyPageList;

////////////// Helpers
bool GetIsPageVisible(uint pageData)
{
  return (pageData & PAGE_VISIBLE_BIT) != 0u;
}

bool GetIsPageDirty(uint pageData)
{
  return (pageData & PAGE_DIRTY_BIT) != 0u;
}

bool GetIsPageBacked(uint pageData)
{
  return (pageData & PAGE_BACKED_BIT) != 0u;
}

uint GetPagePhysicalAddress(uint pageData)
{
  return pageData >> 16;
}

uint SetIsPageVisible(uint pageData, bool isVisible)
{
  return (pageData & ~PAGE_VISIBLE_BIT) | (PAGE_VISIBLE_BIT * uint(isVisible));
}

uint SetIsPageDirty(uint pageData, bool isDirty)
{
  return (pageData & ~PAGE_DIRTY_BIT) | (PAGE_DIRTY_BIT * uint(isDirty));
}

uint SetIsPageBacked(uint pageData, bool isBacked)
{
  return (pageData & ~PAGE_BACKED_BIT) | (PAGE_BACKED_BIT * uint(isBacked));
}

uint SetPagePhysicalAddress(uint pageData, uint physicalAddress)
{
  return (pageData & 65535u) | (physicalAddress << 16);
}

bool TryPushPageClear(uint pageIndex)
{
  uint index = atomicAdd(dirtyPageList.count, 1);

  if (index >= dirtyPageList.data.length())
  {
    atomicAdd(dirtyPageList.count, -1);
    return false;
  }

  dirtyPageList.data[index] = pageIndex;
  return true;
}

float LoadPageTexel(ivec2 texel, uint page)
{
  const int atlasWidth = imageSize(i_physicalPages).x / PAGE_SIZE;
  const ivec2 pageCorner = PAGE_SIZE * ivec2(page / atlasWidth, page % atlasWidth);
  return imageLoad(i_physicalPages, pageCorner + texel).x;
}

void StorePageTexel(ivec2 texel, uint page, float value)
{
  const int atlasWidth = imageSize(i_physicalPages).x / PAGE_SIZE;
  const ivec2 pageCorner = PAGE_SIZE * ivec2(page / atlasWidth, page % atlasWidth);
  imageStore(i_physicalPages, pageCorner + texel, vec4(value, 0, 0, 0));
}

bool SampleVsmBitmaskHzb(uint vsmIndex, vec2 uv, int level)
{
  return bool(textureLod(s_vsmBitmaskHzb, vec3(fract(uv), vsmIndex), level).x);
}

struct PageAddressInfo
{
  ivec3 pageAddress;
  vec2 pageUv;
  float projectedDepth;
  uint clipmapLevel;
};

// Analyzes the provided depth buffer and returns and address and data of a page.
// Works for clipmaps only.
PageAddressInfo GetClipmapPageFromDepth(float depth, ivec2 gid, ivec2 depthBufferSize)
{
  const vec2 texel = 1.0 / depthBufferSize;
  const vec2 uvCenter = (vec2(gid) + 0.5) * texel;
  // Unproject arbitrary, but opposing sides of the pixel (assume square) to compute side length
  const vec2 uvLeft = uvCenter + vec2(-texel.x, 0) * 0.5;
  const vec2 uvRight = uvCenter + vec2(texel.x, 0) * 0.5;

  const mat4 invProj = inverse(perFrameUniforms.proj);
  const vec3 topLeftV = UnprojectUV_ZO(depth, uvLeft, invProj);
  const vec3 topRightV = UnprojectUV_ZO(depth, uvRight, invProj);

  const float projLength = distance(topLeftV, topRightV);

  // Assume each clipmap is 2x the side length of the previous
  const uint clipmapLevel = clamp(uint(ceil(vsmUniforms.lodBias + log2(projLength / clipmapUniforms.firstClipmapTexelLength))), 0, clipmapUniforms.numClipmaps - 1);
  const uint clipmapIndex = clipmapUniforms.clipmapTableIndices[clipmapLevel];

  const vec3 posW = UnprojectUV_ZO(depth, uvCenter, perFrameUniforms.invViewProj);
  const vec4 posLightC = clipmapUniforms.clipmapViewProjections[clipmapLevel] * vec4(posW, 1.0);
  const vec2 posLightUv = fract((posLightC.xy / posLightC.w) * 0.5 + 0.5);

  const ivec2 posLightTexel = ivec2(posLightUv * imageSize(i_pageTables).xy);
  const ivec3 pageAddress = ivec3(posLightTexel, clipmapIndex);

  PageAddressInfo addr;
  addr.pageAddress = pageAddress;
  // Tile UV across VSM
  const ivec2 tableSize = imageSize(i_pageTables).xy;
  addr.pageUv = tableSize * mod(posLightUv, 1.0 / tableSize);
  addr.projectedDepth = posLightC.z / posLightC.w;
  addr.clipmapLevel = clipmapLevel;

  return addr;
}

#endif // VSM_COMMON_H