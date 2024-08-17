//#version 450
#ifndef COLOR_H_GLSL
#define COLOR_H_GLSL

//////////////////// Defines /////////////////////
#define COLOR_SPACE_sRGB_NONLINEAR 0u
#define COLOR_SPACE_scRGB_LINEAR   1u
#define COLOR_SPACE_HDR10_ST2084   2u
#define COLOR_SPACE_BT2020_LINEAR  3u
#define COLOR_SPACE_sRGB_LINEAR    4u
#define COLOR_SPACE_MAX_ENUM       5u




#ifndef __cplusplus
#include "Resources.h.glsl"

//////////////////// Color space manipulation /////////////////////
bool color_is_color_space(uint color_space)
{
  return color_space < COLOR_SPACE_MAX_ENUM;
}

bool color_is_color_space_linear(uint color_space)
{
  ASSERT_MSG(color_is_color_space(color_space), "Not a color space!\n");
  return !(color_space == COLOR_SPACE_sRGB_NONLINEAR || color_space == COLOR_SPACE_HDR10_ST2084);
}

vec3 color_xyYToXYZ(vec3 xyY)
{
  float Y = xyY.z;
  float X = (xyY.x * Y) / xyY.y;
  float Z = ((1.0f - xyY.x - xyY.y) * Y) / xyY.y;

  return vec3(X, Y, Z);
}

vec3 color_Unproject(vec2 xy)
{
  return color_xyYToXYZ(vec3(xy.x, xy.y, 1));
}

// Useful for creating foo-to-XYZ matrices
mat3 color_PrimariesToMatrix(vec2 xy_red, vec2 xy_green, vec2 xy_blue, vec2 xy_white)
{
  vec3 XYZ_red = color_Unproject(xy_red);
  vec3 XYZ_green = color_Unproject(xy_green);
  vec3 XYZ_blue = color_Unproject(xy_blue);
  vec3 XYZ_white = color_Unproject(xy_white);

  mat3 temp = mat3(XYZ_red.x,   1.0, XYZ_red.z,
                   XYZ_green.x, 1.0, XYZ_green.z,
                   XYZ_blue.x,  1.0, XYZ_blue.z);
  vec3 scale = inverse(temp) * XYZ_white;

  return mat3(XYZ_red * scale.x, XYZ_green * scale.y, XYZ_blue * scale.z);
}

// Gamut compression (useful for alleviating gamut clipping)
mat3 color_ComputeCompressionMatrix(vec2 xyR, vec2 xyG, vec2 xyB, vec2 xyW, float compression)
{
  float scale_factor = 1.0 / (1.0 - compression);
  vec2 R = mix(xyW, xyR, scale_factor);
  vec2 G = mix(xyW, xyG, scale_factor);
  vec2 B = mix(xyW, xyB, scale_factor);
  vec2 W = xyW;

  return color_PrimariesToMatrix(R, G, B, W);
}

// https://www.russellcottrell.com/photo/matrixCalculator.htm
mat3 color_make_sRGB_to_XYZ_matrix()
{
  return transpose(mat3(0.4123908, 0.3575843, 0.1804808,
                        0.2126390, 0.7151687, 0.0721923,
                        0.0193308, 0.1191948, 0.9505322));
}

mat3 color_make_XYZ_to_sRGB_matrix()
{
  return transpose(mat3(3.2409699, -1.5373832, -0.4986108,
                        -0.9692436, 1.8759675, 0.0415551,
                         0.0556301, -0.2039770, 1.0569715));
}

mat3 color_make_BT2020_to_XYZ_matrix()
{
  return transpose(mat3(0.6369580, 0.1446169, 0.1688810,
                        0.2627002, 0.6779981, 0.0593017,
                        0.0000000, 0.0280727, 1.0609851));
}

mat3 color_make_XYZ_to_BT2020_matrix()
{
  return transpose(mat3(1.7166512, -0.3556708, -0.2533663,
                        -0.6666844, 1.6164812, 0.0157685,
                        0.0176399, -0.0427706, 0.9421031));
}

mat3 color_make_sRGB_to_BT2020_matrix()
{
  return color_make_XYZ_to_BT2020_matrix() * color_make_sRGB_to_XYZ_matrix();
}

mat3 color_make_BT2020_to_sRGB_matrix()
{
  return color_make_XYZ_to_sRGB_matrix() * color_make_BT2020_to_XYZ_matrix();
}

vec3 color_convert_sRGB_to_BT2020(vec3 srgb_linear)
{
  return color_make_sRGB_to_BT2020_matrix() * srgb_linear;
}

vec3 color_convert_BT2020_to_sRGB(vec3 bt2020_linear)
{
  return color_make_BT2020_to_sRGB_matrix() * bt2020_linear;
}

// Linear color spaces only.
vec3 color_convert_src_to_dst(vec3 color_src, uint src_color_space, uint dst_color_space)
{
  ASSERT_MSG(color_is_color_space_linear(src_color_space), "src_color_space is not linear!\n");
  ASSERT_MSG(color_is_color_space_linear(dst_color_space), "dst_color_space is not linear!\n");

  if (src_color_space == dst_color_space)
  {
    return color_src;
  }

  mat3 XYZ_from_src;
  switch (src_color_space)
  {
  case COLOR_SPACE_sRGB_LINEAR: XYZ_from_src = color_make_sRGB_to_XYZ_matrix(); break;
  case COLOR_SPACE_BT2020_LINEAR: XYZ_from_src = color_make_BT2020_to_XYZ_matrix(); break;
  }

  mat3 dst_from_XYZ;
  switch (dst_color_space)
  {
  case COLOR_SPACE_sRGB_LINEAR: dst_from_XYZ = color_make_XYZ_to_sRGB_matrix(); break;
  case COLOR_SPACE_BT2020_LINEAR: dst_from_XYZ = color_make_XYZ_to_BT2020_matrix(); break;
  }

  return dst_from_XYZ * XYZ_from_src * color_src;
}





//////////////////// Transfer functions /////////////////////
// https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.html#TRANSFER_SRGB
vec3 color_sRGB_EOTF(vec3 srgb_nonlinear)
{
  bvec3 cutoff = lessThanEqual(srgb_nonlinear, vec3(0.04045));
  vec3 higher = pow((srgb_nonlinear + vec3(0.055)) / vec3(1.055), vec3(2.4));
  vec3 lower = srgb_nonlinear / vec3(12.92);

  return mix(higher, lower, cutoff);
}

vec3 color_sRGB_OETF(vec3 srgb_linear)
{
  bvec3 cutoff = lessThan(srgb_linear, vec3(0.0031308));
  vec3 higher = vec3(1.055) * pow(srgb_linear, vec3(1.0 / 2.4)) - vec3(0.055);
  vec3 lower = srgb_linear * vec3(12.92);

  return mix(higher, lower, cutoff);
}

// Note: sRGB and scRGB have the same white point and primaries and only differ in their transfer functions
float color_scRGB_EOTF(float srgb_nonlinear)
{
  if (srgb_nonlinear < -0.04045)
  {
    return -pow(((0.055 - srgb_nonlinear) / 1.055), 2.4);
  }
  if (srgb_nonlinear > 0.04045)
  {
    return pow(((srgb_nonlinear + 0.055) / 1.055), 2.4);
  }
  // else if (-0.04045 <= srgb_nonlinear && srgb_nonlinear <= 0.04045)
  return srgb_nonlinear / 12.92;
}

vec3 color_scRGB_EOTF(vec3 srgb_nonlinear)
{
  return vec3(
    color_scRGB_EOTF(srgb_nonlinear.r),
    color_scRGB_EOTF(srgb_nonlinear.g),
    color_scRGB_EOTF(srgb_nonlinear.b)
  );
}

float color_scRGB_OETF(float srgb_linear)
{
  if (srgb_linear <= -0.0031308)
  {
    return -1.055 * pow(-srgb_linear, 1.0 / 2.4) + 0.055;
  }
  if (srgb_linear >= 0.0031308)
  {
    return 1.055 * pow(srgb_linear, 1.0 / 2.4) - 0.055;
  }
  // else if (-0.0031308 < srgb_linear && srgb_linear < 0.0031308)
  return srgb_linear * 12.92;
}

vec3 color_scRGB_OETF(vec3 srgb_linear)
{
  return vec3(
    color_scRGB_OETF(srgb_linear.r),
    color_scRGB_OETF(srgb_linear.g),
    color_scRGB_OETF(srgb_linear.b)
  );
}

vec3 color_BT1886_EOTF(vec3 x)
{
  return 100.0 * pow(x, vec3(2.4));
}

// Similar to, but not the same as the sRGB OETF.
vec3 color_BT709_OETF(vec3 x)
{
  bvec3 cutoff = lessThanEqual(x, vec3(0.018));
  vec3 higher = pow(x, vec3(1.0 / 2.2)) * 1.099 - 0.099;
  vec3 lower = x * 4.5;
  
  return mix(higher, lower, cutoff);
}

vec3 color_PQ_OOTF(vec3 x)
{
  float BT709_scale_factor = 59.5208;
  return color_BT1886_EOTF(color_BT709_OETF(x * BT709_scale_factor));
}

// Input should be in [0, 10000] nits. Usage: color_PQ_OETF(x * max_nits / 10000.0);
float color_PQ_OETF(float x)
{
  const float m1 = 0.1593017578125;
  const float m2 = 78.84375;
  const float c1 = 0.8359375;
  const float c2 = 18.8515625;
  const float c3 = 18.6875;
  float ym = pow(x, m1);
  return pow((c1 + c2 * ym) / (1.0 + c3 * ym), m2);
}

vec3 color_PQ_OETF(vec3 c)
{
  return vec3(
    color_PQ_OETF(c.r),
    color_PQ_OETF(c.g),
    color_PQ_OETF(c.b)
  );
}

// Input should be in [0, 100] nits
float color_PQ_OETF_approx(float x)
{
  float k = pow((x * 0.01), 0.1593017578125);
  return (3.61972 * (1e-8) + k * (0.00102859 + k * (-0.101284 + 2.05784 * k))) / (0.0495245 + k * (0.135214 + k * (0.772669 + k)));
}

vec3 color_PQ_OETF_approx(vec3 c)
{
  return vec3(
    color_PQ_OETF_approx(c.r),
    color_PQ_OETF_approx(c.g),
    color_PQ_OETF_approx(c.b)
  );
}

// Usage: color_PQ_EOTF(x) * (10000.0 / max_nits);
float color_PQ_EOTF(float x)
{
  const float m1 = 0.1593017578125;
  const float m2 = 78.84375;
  const float c1 = 0.8359375;
  const float c2 = 18.8515625;
  const float c3 = 18.6875;
  float em2 = pow(x, 1.0 / m2);
  return pow(max((em2 - c1), 0.0) / (c2 - c3 * em2), 1.0 / m1);
}

vec3 color_PQ_EOTF(vec3 c)
{
  return vec3(
    color_PQ_EOTF(c.r),
    color_PQ_EOTF(c.g),
    color_PQ_EOTF(c.b)
  );
}

#endif
#endif