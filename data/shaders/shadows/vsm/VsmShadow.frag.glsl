#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "VsmCommon.h.glsl"

layout(binding = 0, std140) uniform VsmShadowUniforms
{
  uint vsmIndex;
};

void main()
{
  const ivec2 pageAddressXy = ivec2(gl_FragCoord.xy) / PAGE_SIZE;
  // TODO: implement for multiple clipmaps
  //const uint pageData = imageLoad(i_pageTables, ivec3(pageAddressXy, vsmIndex)).x;
  const uint pageData = imageLoad(i_pageTables, ivec3(pageAddressXy, 0)).x;
  const ivec2 pageTexel = ivec2(gl_FragCoord.xy) % PAGE_SIZE;
  if (GetIsPageBacked(pageData) && GetIsPageDirty(pageData))
  {
    AtomicMinPageTexel(pageTexel, GetPagePhysicalAddress(pageData), floatBitsToUint(gl_FragCoord.z));
  }
}