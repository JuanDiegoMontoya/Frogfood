#version 450 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "../Color.h.glsl"

layout(push_constant, scalar) uniform PushConstants
{
  uint vertexBufferIndex;
  uint textureIndex;
  uint samplerIndex;
  uint textureColorSpace;
  uint displayColorSpace;
  float maxDisplayNits;
  vec2 scale;
  vec2 translation;
} pc;

layout(set = 0, binding = 3) uniform texture2D textures[];
layout(set = 0, binding = 4) uniform sampler samplers[];

layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout(location = 0) out vec4 fColor;

void main()
{
  // Assumes tint and texture are in the same color space
  vec4 color_raw = In.Color * texture(sampler2D(textures[pc.textureIndex], samplers[pc.samplerIndex]), In.UV);
  fColor = color_raw;
  
  if ((pc.textureColorSpace == COLOR_SPACE_sRGB_NONLINEAR) || (pc.textureColorSpace == COLOR_SPACE_sRGB_LINEAR))
  {
    vec3 color_srgb_linear;
    if (pc.textureColorSpace == COLOR_SPACE_sRGB_LINEAR)
    {
      color_srgb_linear = color_raw.rgb;
      color_srgb_linear = vec3(1);
    }
    else //if (pc.textureColorSpace == COLOR_SPACE_sRGB_NONLINEAR)
    {
      color_srgb_linear = color_sRGB_EOTF(color_raw.rgb);
    }

    // If the output is sRGB_NONLINEAR, no transform is required.
    if (pc.displayColorSpace == COLOR_SPACE_sRGB_NONLINEAR)
    {
      fColor.rgb = color_sRGB_OETF(color_srgb_linear);
    }
    if (pc.displayColorSpace == COLOR_SPACE_sRGB_LINEAR)
    {
      fColor.rgb = color_srgb_linear;
    }
    if (pc.displayColorSpace == COLOR_SPACE_scRGB_LINEAR)
    {
      fColor.rgb = color_srgb_linear;
    }
    if (pc.displayColorSpace == COLOR_SPACE_HDR10_ST2084) // BT.2020 with PQ EOTF
    {
      mat3 sRGB_to_BT2020      = color_make_sRGB_to_BT2020_matrix();
      vec3 color_bt2020_linear = sRGB_to_BT2020 * color_srgb_linear;
      fColor.rgb               = color_PQ_inv_eotf(color_bt2020_linear * pc.maxDisplayNits / 10000.0);
    }
    if (pc.displayColorSpace == COLOR_SPACE_BT2020_LINEAR) // BT.2020 with no transfer function
    {
      mat3 sRGB_to_BT2020    = color_make_sRGB_to_BT2020_matrix();
      fColor.rgb             = sRGB_to_BT2020 * color_srgb_linear;

      //fColor.rgb = color_sRGB_OETF(color_srgb_linear); // This works, but I don't know why, so it's commented out.
    }
  }
}