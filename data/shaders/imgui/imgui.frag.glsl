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

vec3 ConvertTextureToOutputColorSpace(vec3 color_in, uint in_color_space, uint out_color_space)
{
  if (in_color_space == out_color_space)
  {
    return color_in;
  }

  // When the input texture is scRGB, we assume it's encoded as color * maxDisplayNits / 80.
  // To invert that (when necessary), we do color * 80 / maxDisplayNits.
  // Normally, this assumption wouldn't be correct, but we can assume that any texture encoded as scRGB will have been the output of the tonemap shader.
  switch(in_color_space)
  {
  case COLOR_SPACE_sRGB_NONLINEAR:
    switch(out_color_space)
    {
    case COLOR_SPACE_sRGB_LINEAR:    return color_sRGB_EOTF(color_in);
    case COLOR_SPACE_sRGB_NONLINEAR: return color_in;
    case COLOR_SPACE_scRGB_LINEAR:   return color_sRGB_EOTF(color_in) * pc.maxDisplayNits / 80.0;
    case COLOR_SPACE_BT2020_LINEAR:  return color_convert_sRGB_to_BT2020(color_sRGB_EOTF(color_in));
    case COLOR_SPACE_HDR10_ST2084:   return color_PQ_OETF(color_convert_sRGB_to_BT2020(color_sRGB_EOTF(color_in)) * pc.maxDisplayNits / 10000.0);
    }
    break;
  case COLOR_SPACE_scRGB_LINEAR:
    switch(out_color_space)
    {
    case COLOR_SPACE_sRGB_LINEAR:    return color_in * 80.0 / pc.maxDisplayNits;
    case COLOR_SPACE_sRGB_NONLINEAR: return color_sRGB_OETF(color_in * 80.0 / pc.maxDisplayNits);
    case COLOR_SPACE_scRGB_LINEAR:   return color_in;
    case COLOR_SPACE_BT2020_LINEAR:  return color_convert_sRGB_to_BT2020(color_in * 80.0 / pc.maxDisplayNits);
    case COLOR_SPACE_HDR10_ST2084:   return color_PQ_OETF(color_convert_sRGB_to_BT2020(color_in * 80.0 / pc.maxDisplayNits) * pc.maxDisplayNits / 10000.0);
    }
    break;
  case COLOR_SPACE_HDR10_ST2084:
    switch(out_color_space)
    {
    case COLOR_SPACE_sRGB_LINEAR:    return color_convert_BT2020_to_sRGB(color_PQ_EOTF(color_in) * 10000.0 / pc.maxDisplayNits);
    case COLOR_SPACE_sRGB_NONLINEAR: return color_sRGB_OETF(color_convert_BT2020_to_sRGB(color_PQ_EOTF(color_in) * 10000.0 / pc.maxDisplayNits));
    case COLOR_SPACE_scRGB_LINEAR:   return color_convert_BT2020_to_sRGB(color_PQ_EOTF(color_in) * 10000.0 / pc.maxDisplayNits) * pc.maxDisplayNits / 80.0;
    case COLOR_SPACE_BT2020_LINEAR:  return color_PQ_EOTF(color_in) * 10000.0 / pc.maxDisplayNits;
    case COLOR_SPACE_HDR10_ST2084:   return color_in;
    }
    break;
  case COLOR_SPACE_BT2020_LINEAR:
    switch(out_color_space)
    {
    case COLOR_SPACE_sRGB_LINEAR:    return color_convert_BT2020_to_sRGB(color_in);
    case COLOR_SPACE_sRGB_NONLINEAR: return color_sRGB_OETF(color_convert_BT2020_to_sRGB(color_in));
    case COLOR_SPACE_scRGB_LINEAR:   return color_convert_BT2020_to_sRGB(color_in) * pc.maxDisplayNits / 80.0;
    case COLOR_SPACE_BT2020_LINEAR:  return color_in;
    case COLOR_SPACE_HDR10_ST2084:   return color_PQ_OETF(color_in * pc.maxDisplayNits / 10000.0);
    }
    break;
  case COLOR_SPACE_sRGB_LINEAR:
    switch(out_color_space)
    {
    case COLOR_SPACE_sRGB_LINEAR:    return color_in;
    case COLOR_SPACE_sRGB_NONLINEAR: return color_sRGB_OETF(color_in);
    case COLOR_SPACE_scRGB_LINEAR:   return color_in * pc.maxDisplayNits / 80.0;
    case COLOR_SPACE_BT2020_LINEAR:  return color_convert_sRGB_to_BT2020(color_in);
    case COLOR_SPACE_HDR10_ST2084:   return color_PQ_OETF(color_convert_sRGB_to_BT2020(color_in) * pc.maxDisplayNits / 10000.0);
    }
    break;
  }

  UNREACHABLE;
  return color_in;
}

void main()
{
  // Assumes tint and texture are in the same color space
  vec4 color_raw = In.Color * texture(sampler2D(textures[pc.textureIndex], samplers[pc.samplerIndex]), In.UV);
  fColor.rgb = ConvertTextureToOutputColorSpace(color_raw.rgb, pc.textureColorSpace, pc.displayColorSpace);
  fColor.a = color_raw.a;
}