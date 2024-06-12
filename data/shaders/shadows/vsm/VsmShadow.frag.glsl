#version 460 core
#extension GL_GOOGLE_include_directive : enable

#define VISBUFFER_NO_PUSH_CONSTANTS
#include "../../visbuffer/VisbufferCommon.h.glsl"
#include "VsmCommon.h.glsl"

layout(set = 0, binding = FVOG_STORAGE_IMAGE_BINDING, r32ui) uniform uimage2D physicalPagesUintImages[];
#define i_physicalPagesUint physicalPagesUintImages[physicalPagesUintIndex]

FVOG_DECLARE_SAMPLED_IMAGES(texture2D);

layout(location = 0) in vec2 v_uv;
layout(location = 1) in flat uint v_materialId;

void main()
{
  const GpuMaterial material = d_materials[NonUniformIndex(v_materialId)];

  vec2 dxuv = dFdx(v_uv);
  vec2 dyuv = dFdy(v_uv);

  if (material.baseColorTextureIndex != 0 && material.alphaCutoff > 0)
  {
    // Apply a mip/lod bias to the sampled value
    if (perFrameUniforms.bindlessSamplerLodBias != 0)
    {
      ApplyLodBiasToGradient(dxuv, dyuv, perFrameUniforms.bindlessSamplerLodBias);
    }

    float alpha = material.baseColorFactor.a;
    if (bool(material.flags & MATERIAL_HAS_BASE_COLOR))
    {
      alpha *= textureGrad(Fvog_sampler2D(material.baseColorTextureIndex, materialSamplerIndex), v_uv, dxuv, dyuv).a;
    }
    
    if (alpha < material.alphaCutoff)
    {
      discard;
    }
  }

  const uint clipmapIndex = clipmapUniforms.clipmapTableIndices[clipmapLod];
  const ivec2 pageOffset = clipmapUniforms.clipmapPageOffsets[clipmapLod];
  const ivec2 pageAddressXy = ivec2(mod(vec2(ivec2(gl_FragCoord.xy) / PAGE_SIZE + pageOffset), vec2(imageSize(i_pageTables).xy)));
  const uint pageData = imageLoad(i_pageTables, ivec3(pageAddressXy, clipmapIndex)).x;
  if (GetIsPageBacked(pageData) && GetIsPageDirty(pageData))
  {
    const ivec2 pageTexel = ivec2(gl_FragCoord.xy) % PAGE_SIZE;
    const uint page = GetPagePhysicalAddress(pageData);
    const int atlasWidth = imageSize(i_physicalPagesUint).x / PAGE_SIZE;
    const ivec2 pageCorner = PAGE_SIZE * ivec2(page / atlasWidth, page % atlasWidth);
    const uint depthUint = floatBitsToUint(gl_FragCoord.z);
    imageAtomicMin(i_physicalPagesUint, pageCorner + pageTexel, depthUint);
  }
}