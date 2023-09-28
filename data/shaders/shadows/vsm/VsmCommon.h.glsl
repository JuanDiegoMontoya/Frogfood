// #version 450 core
// #extension GL_GOOGLE_include_directive : enable
// #include "../../Math.h.glsl"
// #include "../../GlobalUniforms.h.glsl"
#ifndef VSM_COMMON_H
#define VSM_COMMON_H

#define MAX_CLIPMAPS 32
#define PAGE_SIZE 128

#define PAGE_VISIBLE_BIT (1u)
#define PAGE_DIRTY_BIT (2u)
#define PAGE_BACKED_BIT (4u)

////////////// Resources
layout(binding = 0, r32ui) uniform restrict uimage2DArray i_pageTables;
layout(binding = 1, r32ui) uniform restrict uimage2D i_physicalPages;

layout(binding = 5, std430) restrict readonly buffer VsmMarkPagesDirectionalUniforms
{
  mat4 clipmapViewProjections[MAX_CLIPMAPS];
  uint clipmapTableIndices[MAX_CLIPMAPS];
  ivec2 clipmapPageOffsets[MAX_CLIPMAPS];
  uint numClipmaps;

  // The length, in world space, of a side of single (square) texel in the first clipmap
  float firstClipmapTexelLength;
}clipmapUniforms;

layout(binding = 6, std140) uniform VsmGlobalUniforms
{
  float lodBias;
}vsmUniforms;

struct VsmPageAllocRequest
{
  // Address of the requester
  ivec3 pageTableAddress;

  // Unused until local lights are supported
  uint pageTableLevel;
};

layout(binding = 0, std430) restrict buffer VsmPageAllocRequests
{
  uint count;
  VsmPageAllocRequest data[];
}allocRequests;

layout(binding = 1, std430) restrict buffer VsmVisiblePagesBitmask
{
  uint data[];
}visiblePagesBitmask;

layout(binding = 2, std430) restrict buffer VsmVisibleTimeTree
{
  uint time[];
}lastTimeVisible;

layout(binding = 3, std430) restrict buffer VsmDirtyPageList
{
  uint count;
  uint data[];
}dirtyPageList;

// Indirect dispatch params for clearing dirty pages (these pages will then be rendered)
layout(binding = 4, std430) restrict buffer VsmPageClearDispatchParams
{
  uint groupCountX;
  uint groupCountY;
  uint groupCountZ;
}pageClearDispatch;

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

uint LoadPageTexel(ivec2 texel, uint page)
{
  const int atlasWidth = imageSize(i_physicalPages).x / PAGE_SIZE;
  const ivec2 pageCorner = PAGE_SIZE * ivec2(page / atlasWidth, page % atlasWidth);
  return imageLoad(i_physicalPages, pageCorner + texel).x;
}

void StorePageTexel(ivec2 texel, uint page, uint value)
{
  const int atlasWidth = imageSize(i_physicalPages).x / PAGE_SIZE;
  const ivec2 pageCorner = PAGE_SIZE * ivec2(page / atlasWidth, page % atlasWidth);
  imageStore(i_physicalPages, pageCorner + texel, uvec4(value, 0, 0, 0));
}

uint AtomicMinPageTexel(ivec2 texel, uint page, uint value)
{
  const int atlasWidth = imageSize(i_physicalPages).x / PAGE_SIZE;
  const ivec2 pageCorner = PAGE_SIZE * ivec2(page / atlasWidth, page % atlasWidth);
  return imageAtomicMin(i_physicalPages, pageCorner + texel, value);
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
PageAddressInfo GetClipmapPageFromDepth(sampler2D depthBuffer, ivec2 gid)
{
  const vec2 texel = 1.0 / textureSize(depthBuffer, 0);
  const vec2 uvCenter = (vec2(gid) + 0.5) * texel;
  // Unproject arbitrary, but opposing sides of the pixel (assume square) to compute side length
  const vec2 uvLeft = uvCenter + vec2(-texel.x, 0) * 0.5;
  const vec2 uvRight = uvCenter + vec2(texel.x, 0) * 0.5;

  const float depth = texelFetch(depthBuffer, gid, 0).x;

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