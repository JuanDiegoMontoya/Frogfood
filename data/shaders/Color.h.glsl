//#version 450
#ifndef COLOR_H_GLSL
#define COLOR_H_GLSL

//////////////////// Defines /////////////////////
#define COLOR_SPACE_sRGB_NONLINEAR 0
#define COLOR_SPACE_scRGB_LINEAR 1
#define COLOR_SPACE_HDR10_ST2084 2
#define COLOR_SPACE_BT2020_LINEAR 3
#define COLOR_SPACE_sRGB_LINEAR 4





//////////////////// Color space manipulation /////////////////////
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

  mat3 temp = mat3(XYZ_red.x,    1.0, XYZ_red.z,
                    XYZ_green.x, 1.f, XYZ_green.z,
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

mat3 color_make_sRGB_to_XYZ_matrix()
{
  return color_PrimariesToMatrix(vec2(0.64, 0.33), vec2(0.3, 0.6), vec2(0.15, 0.06), vec2(0.3127, 0.3290));
}

mat3 color_make_BT2020_to_XYZ_matrix()
{
  return color_PrimariesToMatrix(vec2(0.708, 0.292), vec2(0.170, 0.797), vec2(0.131, 0.046), vec2(0.3127, 0.3290));
}

mat3 color_make_sRGB_to_BT2020_matrix()
{
  mat3 sRGB_to_XYZ = color_make_sRGB_to_XYZ_matrix();
  mat3 XYZ_to_BT2020 = inverse(color_make_BT2020_to_XYZ_matrix());
  return sRGB_to_XYZ * XYZ_to_BT2020;
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
  if (srgb_nonlinear <= -0.0031308)
  {
    return -1.055 * pow(-srgb_nonlinear, 1.0 / 2.4) + 0.055;
  }
  if (srgb_nonlinear >= 0.0031308)
  {
    return 1.055 * pow(srgb_nonlinear, 1.0 / 2.4) - 0.055;
  }
  // else if (-0.0031308 < srgb_nonlinear && srgb_nonlinear < 0.0031308)
  return srgb_nonlinear * 12.92;
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
  if (srgb_linear < -0.04045)
  {
    return -pow(((0.055 - srgb_linear) / 1.055), 2.4);
  }
  if (srgb_linear > 0.04045)
  {
    return pow(((srgb_linear + 0.055) / 1.055), 2.4);
  }
  // else if (-0.04045 <= srgb_linear && srgb_linear <= 0.04045)
  return srgb_linear / 12.92;
}

vec3 color_scRGB_OETF(vec3 srgb_linear)
{
  return vec3(
    color_scRGB_OETF(srgb_linear.r),
    color_scRGB_OETF(srgb_linear.g),
    color_scRGB_OETF(srgb_linear.b)
  );
}

// Input should be in [0, 10000] nits
float color_InversePQ(float x)
{
  const float m1 = 0.1593017578125;
  const float m2 = 78.84375;
  const float c1 = 0.8359375;
  const float c2 = 18.8515625;
  const float c3 = 18.6875;
  float ym       = pow(x, m1);
  return pow((c1 + c2 * ym) / (1.0 + c3 * ym), m2);
}

vec3 color_InversePQ(vec3 c)
{
  return vec3(
    color_InversePQ(c.r),
    color_InversePQ(c.g),
    color_InversePQ(c.b)
  );
}

// Input should be in [0, 100] nits
float color_InversePQ_approx(float x)
{
  float k = pow((x * 0.01f), 0.1593017578125);
  return (3.61972 * (1e-8) + k * (0.00102859 + k * (-0.101284 + 2.05784 * k))) / (0.0495245 + k * (0.135214 + k * (0.772669 + k)));
}

vec3 color_InversePQ_approx(vec3 c)
{
  return vec3(
    color_InversePQ_approx(c.r),
    color_InversePQ_approx(c.g),
    color_InversePQ_approx(c.b)
  );
}

#endif