#include "Forward.h.glsl"

layout(location = 0) in vec3 i_color;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec3 i_worldPosition;

layout(location = 0) out vec4 o_color;

void main()
{
  vx_Init(pc.voxels);
  const vec3 indirect = TraceIndirectLighting(ivec2(gl_FragCoord), i_worldPosition, normalize(i_normal));
  o_color = vec4(i_color * indirect, 1.0);

  //GpuMaterial material = MaterialBuffers[pc.materialBufferIndex].materials[pc.materialId];
  //o_color.rgb = material.baseColorFactor.rgb;

  // if (bool(material.flags & MATERIAL_HAS_BASE_COLOR))
  // {
  //   o_color.rgb *= texture(Fvog_sampler2D(material.baseColorTextureIndex, pc.samplerIndex), i_uv).rgb;
  // }

  //o_color.rgb = vec3(i_uv, 0);
}