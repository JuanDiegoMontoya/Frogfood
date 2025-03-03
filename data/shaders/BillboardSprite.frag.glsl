#version 450 core
#include "Resources.h.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in Texture2D v_texture;
layout(location = 2) in vec3 v_tint;

layout(location = 0) out vec4 o_albedo;
layout(location = 2) out vec4 o_indirectIlluminance;
layout(location = 3) out vec4 o_radiance;

FVOG_DECLARE_ARGUMENTS(BillboardPushConstants)
{
  FVOG_UINT32 billboardsIndex;
  FVOG_UINT32 globalUniformsIndex;
  FVOG_VEC3 cameraRight;
  FVOG_VEC3 cameraUp;
  Sampler texSampler;
}pc;

// FSR 2 reactive mask. Unused when FSR 2 is disabled
//layout(location = 1) out float o_reactiveMask;

void main()
{
  o_albedo = texture(v_texture, pc.texSampler, v_uv);
  o_albedo.rgb *= v_tint;
  o_indirectIlluminance = vec4(0, 0, 0, 1);
  o_radiance = o_albedo;
  
  if (o_albedo.a < 0.01)
  {
    discard;
  }
  // Values above 0.9 are not recommended for use, as they are "unlikely [...] to ever produce good results"
  //o_reactiveMask = min(o_color.a, 0.9);
}