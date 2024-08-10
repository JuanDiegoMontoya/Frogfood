#version 450 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "../Color.h.glsl"

layout(push_constant, scalar) uniform PushConstants
{
  uint vertexBufferIndex;
  uint textureIndex;
  uint samplerIndex;
  uint displayColorSpace;
  vec2 scale;
  vec2 translation;
} pc;

layout(set = 0, binding = 3) uniform texture2D textures[];
layout(set = 0, binding = 4) uniform sampler samplers[];

layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout(location = 0) out vec4 fColor;

void main()
{
  vec4 color_raw = In.Color * texture(sampler2D(textures[pc.textureIndex], samplers[pc.samplerIndex]), In.UV);
  fColor = color_raw;

  vec3 color_srgb_nonlinear = color_raw.rgb;

  // If the output is sRGB_NONLINEAR, no transform is required
  if (pc.displayColorSpace == COLOR_SPACE_sRGB_NONLINEAR)
  {
    fColor.rgb = color_srgb_nonlinear;
  }
  if (pc.displayColorSpace == COLOR_SPACE_scRGB_LINEAR)
  {
    vec3 color_srgb_linear = color_sRGB_EOTF(color_srgb_nonlinear);
    fColor.rgb             = color_scRGB_OETF(color_srgb_linear);
  }
  if (pc.displayColorSpace == COLOR_SPACE_HDR10_ST2084) // BT.2020 with PQ EOTF
  {
    vec3 color_srgb_linear   = color_sRGB_EOTF(color_srgb_nonlinear);
    mat3 sRGB_to_BT2020      = color_make_sRGB_to_BT2020_matrix();
    vec3 color_bt2020_linear = sRGB_to_BT2020 * color_srgb_linear;
    fColor.rgb               = color_InversePQ(color_bt2020_linear);
  }
  if (pc.displayColorSpace == COLOR_SPACE_BT2020_LINEAR) // BT.2020 with no transfer function
  {
    vec3 color_srgb_linear = color_sRGB_EOTF(color_srgb_nonlinear);
    mat3 sRGB_to_BT2020    = color_make_sRGB_to_BT2020_matrix();
    fColor.rgb             = sRGB_to_BT2020 * color_srgb_linear;
  }
}