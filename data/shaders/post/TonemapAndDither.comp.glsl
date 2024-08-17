#version 450 core

#extension GL_GOOGLE_include_directive : enable

#include "TonemapAndDither.shared.h"
#include "../Color.h.glsl"

layout(local_size_x = 8, local_size_y = 8) in;

//layout(binding = 0) uniform sampler2D s_sceneColor;
//layout(binding = 1) uniform sampler2D s_noise;
FVOG_DECLARE_SAMPLED_IMAGES(texture2D);
FVOG_DECLARE_SAMPLED_IMAGES(texture3D);
FVOG_DECLARE_SAMPLERS;

//layout(std140, binding = 0) uniform ExposureBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly ExposureBuffer)
{
  float exposure;
} exposureBuffers[];

#define d_exposureBuffer exposureBuffers[exposureIndex]

//layout(std140, binding = 1) uniform TonemapUniformBuffer
FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly TonemapUniformBuffer)
{
  TonemapUniforms data;
} uniformsBuffers[];

#define uniforms uniformsBuffers[tonemapUniformsIndex].data

//layout(binding = 0) uniform writeonly image2D i_output;
FVOG_DECLARE_STORAGE_IMAGES(image2D);

// AgX implementation from here: https://www.shadertoy.com/view/Dt3XDr
float DualSection(float x, float linear, float peak)
{
	// Length of linear section
	float S = (peak * linear);
	if (x < S) {
		return x;
	} else {
		float C = peak / (peak - S);
		return peak - (peak - S) * exp((-C * (x - S)) / peak);
	}
}

vec3 DualSection(vec3 x, float linear, float peak)
{
	x.x = DualSection(x.x, linear, peak);
	x.y = DualSection(x.y, linear, peak);
	x.z = DualSection(x.z, linear, peak);
	return x;
}

vec3 AgX_DS(vec3 color_srgb, AgXMapperSettings agx, float maxDisplayNits)
{
  vec3 workingColor = max(color_srgb, 0.0);

  mat3 sRGB_to_XYZ      = color_PrimariesToMatrix(vec2(0.64, 0.33), vec2(0.3, 0.6), vec2(0.15, 0.06), vec2(0.3127, 0.3290));
  mat3 adjusted_to_XYZ  = color_ComputeCompressionMatrix(vec2(0.64, 0.33), vec2(0.3, 0.6), vec2(0.15, 0.06), vec2(0.3127, 0.3290), agx.compression);
  mat3 XYZ_to_adjusted  = inverse(adjusted_to_XYZ);
  mat3 sRGB_to_adjusted = sRGB_to_XYZ * XYZ_to_adjusted;

  workingColor = sRGB_to_adjusted * workingColor;
  //workingColor = clamp(DualSection(workingColor, agx.linear, agx.peak), 0.0, 1.0);
  workingColor = DualSection(workingColor, agx.linear, maxDisplayNits);

  vec3 luminanceWeight = vec3(0.2126729, 0.7151522, 0.0721750);
  vec3 desaturation    = vec3(dot(workingColor, luminanceWeight));
  workingColor         = mix(desaturation, workingColor, agx.saturation);
  //workingColor         = clamp(workingColor, 0.0, 1.0);

  workingColor = inverse(sRGB_to_adjusted) * workingColor;

  return workingColor;
}

// Tony McMapface from https://github.com/h3r2tic/tony-mc-mapface/blob/main/shader/tony_mc_mapface.hlsl
vec3 TonyMcMapface(vec3 color_srgb)
{
  vec3 workingColor = max(color_srgb, 0.0f);
  vec3 encoded = workingColor / (1.0 + workingColor);

  vec3 dims = vec3(textureSize(Fvog_texture3D(tonyMcMapfaceIndex), 0));
  vec3 uv = encoded * ((dims - 1.0) / dims) + 0.5 / dims;

  return textureLod(Fvog_sampler3D(tonyMcMapfaceIndex, linearClampSamplerIndex), uv, 0).rgb;
}

vec3 apply_dither(vec3 color, vec2 uv, uint quantizeBits)
{
  vec2 uvNoise = uv * (vec2(textureSize(Fvog_sampler2D(sceneColorIndex, nearestSamplerIndex), 0)) / vec2(textureSize(Fvog_sampler2D(noiseIndex, nearestSamplerIndex), 0)));
  vec3 noiseSample = textureLod(Fvog_sampler2D(noiseIndex, nearestSamplerIndex), uvNoise, 0).rgb;
  //return color + vec3((noiseSample - 0.5) / 255.0);
  return color + vec3((noiseSample - 0.5) / ((1u << quantizeBits) - 1));
}

// TODO: replace with something better
float H_f(float x, float e0, float e1)
{
  if (x <= e0)
    return 0;
  if (x >= e1)
    return 1;
  return (x - e0) / (e1 - e0);
}

// Display mapper suited for HDR (also works for LDR)
// http://cdn2.gran-turismo.com/data/www/pdi_publications/PracticalHDRandWCGinGTS.pdf
// https://www.desmos.com/calculator/mbkwnuihbd
/* Defaults:
 *   maxDisplayNits = 1
 *   contrast = 1
 *   startOfLinearSection = 0.22
 *   lengthOfLinearSection = 0.4
 *   toeCurviness = 1.33 ("black tightness")
 *   toeFloor = 0 (darkest black)
*/
float GTMapper(float x, GTMapperSettings gt, float maxDisplayNits)
{
  float P = maxDisplayNits;
  float a = gt.contrast;
  float m = gt.startOfLinearSection;
  float l = gt.lengthOfLinearSection;
  float c = gt.toeCurviness;
  float b = gt.toeFloor;

  // Linear section
  float l0 = (P - m)*l / a;
  float L0 = m - m / a;
  float L1 = m + (1 - m) / a;
  float L_x = m + a * (x - m);

  // Toe
  float T_x = m * pow(x / m, c) + b;

  // Shoulder
  float S0 = m + l0;
  float S1 = m + a * l0;
  float C2 = a * P / (P - S1);
  float S_x = P - (P - S1) * exp(-(C2*(x-S0)/P));

  float w0_x = 1 - smoothstep(x, 0, m);
  float w2_x = H_f(x, m + l0, m + l0);
  float w1_x = 1 - w0_x - w2_x;
  float f_x = T_x * w0_x + L_x * w1_x + S_x * w2_x;
  return f_x;
}

vec3 GTMapper(vec3 c, GTMapperSettings gt, float maxDisplayNits)
{
  return vec3(
    GTMapper(c.r, gt, maxDisplayNits),
    GTMapper(c.g, gt, maxDisplayNits),
    GTMapper(c.b, gt, maxDisplayNits)
  );
}

vec3 ConvertShadingToTonemapOutputColorSpace(vec3 color_in, uint in_color_space, uint out_color_space)
{
  switch (in_color_space)
  {
  case COLOR_SPACE_sRGB_LINEAR:
    switch (out_color_space)
    {
    case COLOR_SPACE_sRGB_LINEAR:    return color_in;
    case COLOR_SPACE_sRGB_NONLINEAR: return color_sRGB_OETF(color_in);
    case COLOR_SPACE_scRGB_LINEAR:   return color_in * uniforms.maxDisplayNits / 80.0;
    case COLOR_SPACE_BT2020_LINEAR:  return color_convert_sRGB_to_BT2020(color_in);
    case COLOR_SPACE_HDR10_ST2084:   return color_PQ_OETF(color_convert_sRGB_to_BT2020(color_in) * uniforms.maxDisplayNits / 10000.0);
    }
    break;
  case COLOR_SPACE_BT2020_LINEAR:
    switch (out_color_space)
    {
    case COLOR_SPACE_sRGB_LINEAR:    return color_convert_BT2020_to_sRGB(color_in);
    case COLOR_SPACE_sRGB_NONLINEAR: return color_sRGB_OETF(color_convert_BT2020_to_sRGB(color_in));
    case COLOR_SPACE_scRGB_LINEAR:   return color_convert_BT2020_to_sRGB(color_in) * uniforms.maxDisplayNits / 80.0;
    case COLOR_SPACE_BT2020_LINEAR:  return color_in;
    case COLOR_SPACE_HDR10_ST2084:   return color_PQ_OETF(color_in * uniforms.maxDisplayNits / 10000.0);
    }
    break;
  }

  UNREACHABLE;
  return color_in;
}

void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const ivec2 targetDim = imageSize(Fvog_image2D(outputImageIndex));

  if (any(greaterThanEqual(gid, targetDim)))
  {
    return;
  }

  const vec2 uv = (vec2(gid) + 0.5) / targetDim;
  
  vec3 hdrColor = textureLod(Fvog_sampler2D(sceneColorIndex, nearestSamplerIndex), uv, 0).rgb;
  hdrColor *= pow(2.0, d_exposureBuffer.exposure); // Apply exposure
  vec3 tonemappedColor = hdrColor;
  
  // sRGB/SDR display mappers (AgX actually works okay as an HDR tonemapper despite assuming BT.709 input)
  if (uniforms.tonemapper == 0)
  {
    tonemappedColor = AgX_DS(hdrColor, uniforms.agx, uniforms.maxDisplayNits);
  }
  if (uniforms.tonemapper == 1)
  {
    tonemappedColor = TonyMcMapface(hdrColor);
  }
  if (uniforms.tonemapper == 2)
  {
	  tonemappedColor = clamp(hdrColor, vec3(0), vec3(1));
  }

  // Hybrid/HDR-compatible diplay mappers
  if (uniforms.tonemapper == 3)
  {
    tonemappedColor = GTMapper(hdrColor, uniforms.gt, uniforms.maxDisplayNits);
  }

  vec3 outputColor = ConvertShadingToTonemapOutputColorSpace(tonemappedColor, uniforms.shadingInternalColorSpace, uniforms.tonemapOutputColorSpace);

  vec3 ditheredColor = outputColor;
  
  if (bool(uniforms.enableDithering))
  {
    ditheredColor = apply_dither(outputColor, uv, uniforms.quantizeBits);
  }
  imageStore(Fvog_image2D(outputImageIndex), gid, vec4(ditheredColor, 1.0));
}
