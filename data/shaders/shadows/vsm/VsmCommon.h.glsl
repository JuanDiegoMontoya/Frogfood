#ifndef VSM_COMMON_H
#define VSM_COMMON_H

#include "../../Resources.h.glsl"

////////////// Globals

#ifndef VSM_NO_PUSH_CONSTANTS
FVOG_DECLARE_ARGUMENTS(VsmPushConstants)
{
  FVOG_UINT32 globalUniformsIndex;
  FVOG_UINT32 pageTablesIndex;
  FVOG_UINT32 physicalPagesIndex;
  FVOG_UINT32 vsmBitmaskHzbIndex;
  FVOG_UINT32 vsmUniformsBufferIndex;
  FVOG_UINT32 dirtyPageListBufferIndex;
  FVOG_UINT32 clipmapUniformsBufferIndex;
  FVOG_UINT32 nearestSamplerIndex;
  FVOG_UINT32 pageClearDispatchIndex;

  // VsmMarkVisiblePages
  FVOG_UINT32 gDepthIndex;

  //VsmReduceBitmaskHzb
  FVOG_UINT32 srcVsmBitmaskHzbIndex;
  FVOG_UINT32 dstVsmBitmaskHzbIndex;
  FVOG_INT32 currentPass;

  // VsmShadow + ShadowMain
  FVOG_UINT32 visibleMeshletsIndex;
  FVOG_UINT32 meshletInstancesIndex;
  FVOG_UINT32 meshletDataIndex;
  FVOG_UINT32 meshletPrimitivesIndex;
  FVOG_UINT32 meshletVerticesIndex;
  FVOG_UINT32 meshletIndicesIndex;
  FVOG_UINT32 transformsIndex;
  FVOG_UINT32 materialsIndex;
  FVOG_UINT32 materialSamplerIndex;
  FVOG_UINT32 clipmapLod; // Not a resource
  FVOG_UINT32 viewIndex;

  // VsmAllocRequests.h (VsmMarkVisiblePages and VsmAllocatePages)
  FVOG_UINT32 allocRequestsIndex;
  FVOG_UINT32 visiblePagesBitmaskIndex;
  FVOG_UINT32 physicalPagesUintIndex;

  FVOG_UINT32 physicalPagesOverdrawIndex;
};
#endif

#ifndef __cplusplus
#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"

#define PAGE_SIZE 128
#define MAX_CLIPMAPS 32

#define PAGE_VISIBLE_BIT (1u)
#define PAGE_DIRTY_BIT (2u)
#define PAGE_BACKED_BIT (4u)

////////////// Resources
layout(set = 0, binding = FVOG_STORAGE_IMAGE_BINDING, r32ui) uniform uimage2DArray pageTablesImages[];
layout(set = 0, binding = FVOG_STORAGE_IMAGE_BINDING, r32f) uniform image2D physicalPagesImages[];
layout(set = 0, binding = FVOG_STORAGE_IMAGE_BINDING, r32ui) uniform restrict uimage2D physicalPagesOverdrawHeatmapImages[];

#define i_physicalPagesOverdrawHeatmap physicalPagesOverdrawHeatmapImages[physicalPagesOverdrawIndex]
#define i_pageTables pageTablesImages[pageTablesIndex]
#define i_physicalPages physicalPagesImages[physicalPagesIndex]
#define s_vsmBitmaskHzb Fvog_usampler2DArray(vsmBitmaskHzbIndex, nearestSamplerIndex)
#define perFrameUniforms perFrameUniformsBuffers[globalUniformsIndex]

#define clipmapUniforms clipmapUniformsBuffers[clipmapUniformsBufferIndex]
#define vsmUniforms vsmUniformsBuffers[vsmUniformsBufferIndex]
#define dirtyPageList dirtyPageListBuffers[dirtyPageListBufferIndex]

FVOG_DECLARE_STORAGE_BUFFERS(ClipmapUniforms)
{
  mat4 clipmapViewProjections[MAX_CLIPMAPS];
  uint clipmapTableIndices[MAX_CLIPMAPS];
  ivec2 clipmapPageOffsets[MAX_CLIPMAPS];
  uint numClipmaps;

  // The length, in world space, of a side of single (square) texel in the first clipmap
  float firstClipmapTexelLength;
  float projectionZLength;
}clipmapUniformsBuffers[];

#define VSM_HZB_FORCE_SUCCESS (1 << 0)
#define VSM_FORCE_DIRTY_VISIBLE_PAGES (1 << 1)

FVOG_DECLARE_STORAGE_BUFFERS(VsmGlobalUniforms)
{
  float lodBias;
  uint debugFlags;
}vsmUniformsBuffers[];

FVOG_DECLARE_STORAGE_BUFFERS(VsmDirtyPageList)
{
  uint count;
  uint data[];
}dirtyPageListBuffers[];

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

ivec2 GetPhysicalTexelAddress(ivec2 texel, uint page)
{
  const int atlasWidth = imageSize(i_physicalPages).x / PAGE_SIZE;
  const ivec2 pageCorner = PAGE_SIZE * ivec2(page / atlasWidth, page % atlasWidth);
  return pageCorner + texel;
}

float LoadPageTexel(ivec2 texel, uint page)
{
  return imageLoad(i_physicalPages, GetPhysicalTexelAddress(texel, page)).x;
}

void StorePageTexel(ivec2 texel, uint page, float value)
{
  imageStore(i_physicalPages, GetPhysicalTexelAddress(texel, page), vec4(value, 0, 0, 0));
}

bool SampleVsmBitmaskHzb(uint vsmIndex, vec2 uv, int level)
{
  if ((vsmUniforms.debugFlags & VSM_HZB_FORCE_SUCCESS) != 0)
  {
    return true;
  }
  return bool(textureLod(s_vsmBitmaskHzb, vec3(fract(uv), vsmIndex), float(level)).x);
}

bool CullQuadVsm(vec2 minXY, vec2 maxXY, uint virtualTableIndex)
{
  const vec4 boxUvs = vec4(minXY, maxXY);
  const vec2 hzbSize = vec2(textureSize(s_vsmBitmaskHzb, 0));
  const float width = (boxUvs.z - boxUvs.x) * hzbSize.x;
  const float height = (boxUvs.w - boxUvs.y) * hzbSize.y;
  
  // Select next level so the box is always in [0.5, 1.0) of a texel of the current level.
  // If the box is larger than a single texel of the current level, then it could touch nine
  // texels rather than four! So we need to round up to the next level.
  const float level = ceil(log2(max(width, height)));
  const bool[4] vis = bool[](
    SampleVsmBitmaskHzb(virtualTableIndex, boxUvs.xy, int(level)),
    SampleVsmBitmaskHzb(virtualTableIndex, boxUvs.zy, int(level)),
    SampleVsmBitmaskHzb(virtualTableIndex, boxUvs.xw, int(level)),
    SampleVsmBitmaskHzb(virtualTableIndex, boxUvs.zw, int(level)));
  const bool isVisible = vis[0] || vis[1] || vis[2] || vis[3];

  // Object is visible if it may overlap at least one active page
  if (isVisible)
  {
    return true;
  }

  return false;
}

struct PageAddressInfo
{
  ivec3 pageAddress;
  vec2 pageUv; // UV within a single page
  float projectedDepth;
  uint clipmapLevel;
  vec2 vsmUv; // UV within the whole VSM
  vec3 posLightNdc;
};

PageAddressInfo GetClipmapPageFromDepth1(float depth, ivec2 gid, ivec2 depthBufferSize)
{
  const vec2 texel = 1.0 / depthBufferSize;
  const vec2 uvCenter = (vec2(gid) + 0.5) * texel;
  // Unproject arbitrary, but opposing sides of the pixel (assume square) to compute side length
  const vec2 uvLeft = uvCenter + vec2(-texel.x, 0) * 0.5;
  const vec2 uvRight = uvCenter + vec2(texel.x, 0) * 0.5;

  const mat4 invProj = perFrameUniforms.invProj;
  const vec3 leftV = UnprojectUV_ZO(depth, uvLeft, invProj);
  const vec3 rightV = UnprojectUV_ZO(depth, uvRight, invProj);

  const float projLength = distance(leftV, rightV);

  // Assume each clipmap is 2x the side length of the previous
  precise const uint clipmapLevel = clamp(uint(ceil(vsmUniforms.lodBias + log2(projLength / clipmapUniforms.firstClipmapTexelLength))), 0, clipmapUniforms.numClipmaps - 1);
  const uint clipmapIndex = clipmapUniforms.clipmapTableIndices[clipmapLevel];

  const vec3 posW = UnprojectUV_ZO(depth, uvCenter, perFrameUniforms.invViewProj);
  const vec4 posLightC = clipmapUniforms.clipmapViewProjections[clipmapLevel] * vec4(posW, 1.0);
  const vec3 posLightNdc = posLightC.xyz / posLightC.w;
  const vec2 posLightUv = fract(posLightNdc.xy * 0.5 + 0.5);

  const ivec2 posLightTexel = ivec2(posLightUv * imageSize(i_pageTables).xy);
  const ivec3 pageAddress = ivec3(posLightTexel, clipmapIndex);

  PageAddressInfo addr;
  addr.pageAddress = pageAddress;
  // Tile UV across VSM
  const ivec2 tableSize = imageSize(i_pageTables).xy;
  addr.pageUv = tableSize * mod(posLightUv, 1.0 / tableSize);
  addr.projectedDepth = posLightC.z / posLightC.w;
  addr.clipmapLevel = clipmapLevel;
  addr.vsmUv = posLightUv;
  addr.posLightNdc = posLightNdc;

  return addr;
}

PageAddressInfo GetClipmapPageFromDepth2(float depth, ivec2 gid, ivec2 depthBufferSize)
{
  const vec2 texel = 1.0 / depthBufferSize;
  const vec2 uvCenter = (vec2(gid) + 0.5) * texel;

  const vec3 posW = UnprojectUV_ZO(depth, uvCenter, perFrameUniforms.invViewProj);
  const float dist = distance(posW, perFrameUniforms.cameraPos.xyz);

  // Assume each clipmap is 2x the side length of the previous. Additional bias of 2.5 is arbitrary
  const ivec2 tableSize = imageSize(i_pageTables).xy;
  const float firstClipmapWidth = tableSize.x * PAGE_SIZE * clipmapUniforms.firstClipmapTexelLength;
  const uint clipmapLevel = clamp(uint(ceil(2.5 + vsmUniforms.lodBias + log2(2.0 * dist / firstClipmapWidth))), 0, clipmapUniforms.numClipmaps - 1);
  const uint clipmapIndex = clipmapUniforms.clipmapTableIndices[clipmapLevel];

  const vec4 posLightC = clipmapUniforms.clipmapViewProjections[clipmapLevel] * vec4(posW, 1.0);
  const vec3 posLightNdc = posLightC.xyz / posLightC.w;
  const vec2 posLightUv = fract(posLightNdc.xy * 0.5 + 0.5);

  const ivec2 posLightTexel = ivec2(posLightUv * imageSize(i_pageTables).xy);
  const ivec3 pageAddress = ivec3(posLightTexel, clipmapIndex);

  PageAddressInfo addr;
  addr.pageAddress = pageAddress;
  // Tile UV across VSM
  addr.pageUv = tableSize * mod(posLightUv, 1.0 / tableSize);
  addr.projectedDepth = posLightC.z / posLightC.w;
  addr.clipmapLevel = clipmapLevel;
  addr.vsmUv = posLightUv;
  addr.posLightNdc = posLightNdc;

  return addr;
}

PageAddressInfo GetClipmapPageFromDepth3(float depth, ivec2 gid, ivec2 depthBufferSize)
{
  const vec2 texel = 1.0 / depthBufferSize;
  const vec2 uvCenter = (vec2(gid) + 0.5) * texel;
  const vec2 uvCenter2 = vec2(0);
  // Unproject arbitrary, but opposing sides of the pixel (assume square) to compute side length
  const vec2 uvLeft = uvCenter2 + vec2(-texel.x, 0) * 0.5;
  const vec2 uvRight = uvCenter2 + vec2(texel.x, 0) * 0.5;

  const mat4 invProj = perFrameUniforms.invProj;
  const vec3 leftV = UnprojectUV_ZO(depth, uvLeft, invProj);
  const vec3 rightV = UnprojectUV_ZO(depth, uvRight, invProj);

  const float projLength = distance(leftV, rightV);

  // Assume each clipmap is 2x the side length of the previous
  precise const uint clipmapLevel = clamp(uint(ceil(vsmUniforms.lodBias + log2(projLength / clipmapUniforms.firstClipmapTexelLength))), 0, clipmapUniforms.numClipmaps - 1);
  const uint clipmapIndex = clipmapUniforms.clipmapTableIndices[clipmapLevel];

  const vec3 posW = UnprojectUV_ZO(depth, uvCenter, perFrameUniforms.invViewProj);
  const vec4 posLightC = clipmapUniforms.clipmapViewProjections[clipmapLevel] * vec4(posW, 1.0);
  const vec3 posLightNdc = posLightC.xyz / posLightC.w;
  const vec2 posLightUv = fract(posLightNdc.xy * 0.5 + 0.5);

  const ivec2 posLightTexel = ivec2(posLightUv * imageSize(i_pageTables).xy);
  const ivec3 pageAddress = ivec3(posLightTexel, clipmapIndex);

  PageAddressInfo addr;
  addr.pageAddress = pageAddress;
  // Tile UV across VSM
  const ivec2 tableSize = imageSize(i_pageTables).xy;
  addr.pageUv = tableSize * mod(posLightUv, 1.0 / tableSize);
  addr.projectedDepth = posLightC.z / posLightC.w;
  addr.clipmapLevel = clipmapLevel;
  addr.vsmUv = posLightUv;
  addr.posLightNdc = posLightNdc;

  return addr;
}

// Analyzes the provided depth buffer and returns and address and data of a page.
// Works for clipmaps only.
PageAddressInfo GetClipmapPageFromDepth(float depth, ivec2 gid, ivec2 depthBufferSize)
{
  return GetClipmapPageFromDepth1(depth, gid, depthBufferSize);
  //return GetClipmapPageFromDepth2(depth, gid, depthBufferSize);
  //return GetClipmapPageFromDepth3(depth, gid, depthBufferSize);
}

#endif // __cplusplus
#endif // VSM_COMMON_H
