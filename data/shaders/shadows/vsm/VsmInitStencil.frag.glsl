#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "../../Config.shared.h"
#include "VsmCommon.h.glsl"

layout(binding = 1, r32ui) uniform restrict uimage2D i_physicalPagesUint;

layout(binding = 1, std140) uniform VsmShadowUniforms
{
  uint clipmapLod;
};

layout(location = 0) in vec2 v_uv;

void main()
{
  const uint clipmapIndex = clipmapUniforms.clipmapTableIndices[clipmapLod];
  const ivec2 pageOffset = clipmapUniforms.clipmapPageOffsets[clipmapLod];
  const ivec2 pageAddressXy = ivec2(mod(vec2(ivec2(gl_FragCoord.xy) / PAGE_SIZE + pageOffset), vec2(imageSize(i_pageTables).xy)));
  const uint pageData = imageLoad(i_pageTables, ivec3(pageAddressXy, clipmapIndex)).x;
  const bool pageIsActive = GetIsPageBacked(pageData) && GetIsPageDirty(pageData);

  if (!pageIsActive) // write stencil ref to active pages only
  {
    discard;
  }
}