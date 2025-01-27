#include "Forward.h.glsl"

layout(location = 0) in vec3 i_color;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec3 i_worldPosition;

layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_indirectIlluminance;
layout(location = 3) out vec4 o_radiance;

void main()
{
  vx_Init(pc.voxels);
  const vec3 normal = normalize(i_normal);
  const uint samples = 1;
  const uint bounces = 2;
  const vec3 indirect = TraceIndirectLighting(ivec2(gl_FragCoord), i_worldPosition + normal * 1e-4, normal, samples, bounces, pc.noiseTexture);
	const vec3 sunDir = normalize(vec3(.7, 1, .3));
  const float sun = TraceSunRay(i_worldPosition, sunDir);
  const vec3 direct = i_color * vec3(2 * sun * max(0, dot(sunDir, normal)));
  //o_color = vec4(direct + i_color * indirect, 1.0);
  o_albedo = vec4(i_color, 1);
  o_normal = vec4(normal, 1);
  o_indirectIlluminance = vec4(indirect, 1);
  o_radiance = vec4(direct, 1);

  //GpuMaterial material = MaterialBuffers[pc.materialBufferIndex].materials[pc.materialId];
  //o_color.rgb = material.baseColorFactor.rgb;

  // if (bool(material.flags & MATERIAL_HAS_BASE_COLOR))
  // {
  //   o_color.rgb *= texture(Fvog_sampler2D(material.baseColorTextureIndex, pc.samplerIndex), i_uv).rgb;
  // }

  //o_color.rgb = vec3(i_uv, 0);
}