#version 460 core
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_bindless_texture : require
#include "Common.h.glsl"

layout (location = 0) in flat uint i_meshletId;
layout (location = 1) in flat uint i_primitiveId;
layout (location = 2) in vec2 i_uv;

layout (location = 0) out uint o_pixel;

void main()
{
  const Meshlet meshlet = meshlets[i_meshletId];
  const GpuMaterial material = materials[NonUniformIndex(meshlet.materialId)];

  vec2 dxuv = dFdx(i_uv);
  vec2 dyuv = dFdy(i_uv);

  if (material.baseColorTextureHandle != uvec2(0))
  {
    // Apply a mip/lod bias to the sampled value
    if (bindlessSamplerLodBias != 0)
    {
      const float ddx2 = dot(dxuv, dxuv);
      const float ddy2 = dot(dyuv, dyuv);
      const float actual_mip = pow(2.0, bindlessSamplerLodBias + 0.5 * log2(max(ddx2, ddy2)));
      const float min_mip = sqrt(min(ddx2, ddy2));
      dxuv = dxuv * (actual_mip / min_mip);
      dyuv = dyuv * (actual_mip / min_mip);
    }

    float alpha = material.baseColorFactor.a;
    if (bool(material.flags & MATERIAL_HAS_BASE_COLOR))
    {
      alpha *= textureGrad(sampler2D(material.baseColorTextureHandle), i_uv, dxuv, dyuv).a;
    }
    
    if (alpha < material.alphaCutoff)
    {
      discard;
    }
  }

  o_pixel = (i_meshletId << MESHLET_PRIMITIVE_BITS) | (i_primitiveId & MESHLET_PRIMITIVE_MASK);
}
