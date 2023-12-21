#version 460 core
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_bindless_texture : require
#include "VisbufferCommon.h.glsl"
#include "../Math.h.glsl"
#include "../Hash.h.glsl"

layout (location = 0) in flat uint i_meshletId;
layout (location = 1) in flat uint i_primitiveId;
layout (location = 2) in vec2 i_uv;
layout (location = 3) in vec3 i_objectSpacePos;

layout (location = 0) out uint o_pixel;


void main()
{
  const Meshlet meshlet = meshlets[i_meshletId];
  const GpuMaterial material = materials[NonUniformIndex(meshlet.materialId)];

  vec2 dxuv = dFdx(i_uv);
  vec2 dyuv = dFdy(i_uv);

  const float maxObjSpaceDerivLen = max(length(dFdx(i_objectSpacePos)), length(dFdy(i_objectSpacePos)));

  if (material.baseColorTextureHandle != uvec2(0))
  {
    // Apply a mip/lod bias to the sampled value
    if (perFrameUniforms.bindlessSamplerLodBias != 0)
    {
      ApplyLodBiasToGradient(dxuv, dyuv, perFrameUniforms.bindlessSamplerLodBias);
    }

    float alpha = material.baseColorFactor.a;
    float lod = 0;
    if (bool(material.flags & MATERIAL_HAS_BASE_COLOR))
    {
      sampler2D albedoSampler = sampler2D(material.baseColorTextureHandle);
      alpha *= textureGrad(albedoSampler, i_uv, dxuv, dyuv).a;

      // textureQueryLod at home
      const vec2 texSize = textureSize(albedoSampler, 0);
      const float maxSize = max(texSize.x, texSize.y);
      const float maxLod = floor(log2(maxSize));
      const float p = max(length(dxuv), length(dyuv));
      lod = clamp(log2(p * maxSize), 0, maxLod);
    }
    
    float threshold = material.alphaCutoff;
    if (bool(perFrameUniforms.flags & USE_HASHED_TRANSPARENCY))
    {
      threshold = material.alphaCutoff + min(1.0 - material.alphaCutoff, material.alphaCutoff) + (ComputeHashedAlphaThreshold(i_objectSpacePos, maxObjSpaceDerivLen, perFrameUniforms.alphaHashScale) * 2.0 - 1.0);
      //threshold = material.alphaCutoff - 0.5 + ComputeHashedAlphaThreshold(i_objectSpacePos, maxObjSpaceDerivLen, perFrameUniforms.alphaHashScale);
      //threshold = material.alphaCutoff + min(1.0 - material.alphaCutoff, material.alphaCutoff) + (MM_Hash3(i_objectSpacePos) * 2.0 - 1.0);
      //threshold = material.alphaCutoff - 0.5 + MM_Hash3(i_objectSpacePos);
      //threshold = MM_Hash3(i_objectSpacePos);
      threshold = ComputeHashedAlphaThreshold(i_objectSpacePos, maxObjSpaceDerivLen, perFrameUniforms.alphaHashScale);
    }

    if (alpha < clamp(threshold, 0.001, 1.0))
    //if (alpha < clamp(threshold, 0.1, 0.9))
    //if (alpha < clamp(threshold, 0.1 / (10.0 * (1.0 + lod)), 0.9))
    //if (alpha < clamp(threshold, 0.01 / exp2(lod), 1))
    {
      discard;
    }
  }

  o_pixel = (i_meshletId << MESHLET_PRIMITIVE_BITS) | (i_primitiveId & MESHLET_PRIMITIVE_MASK);
}
