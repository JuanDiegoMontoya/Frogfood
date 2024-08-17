#ifndef TONEMAP_AND_DITHER_H
#define TONEMAP_AND_DITHER_H

#include "../Resources.h.glsl"
#include "../Color.h.glsl"

FVOG_DECLARE_ARGUMENTS(TonemapArguments)
{
  FVOG_UINT32 sceneColorIndex;
  FVOG_UINT32 noiseIndex;
  FVOG_UINT32 nearestSamplerIndex;
  FVOG_UINT32 linearClampSamplerIndex;

  FVOG_UINT32 exposureIndex;
  FVOG_UINT32 tonemapUniformsIndex;
  FVOG_UINT32 outputImageIndex;

  FVOG_UINT32 tonyMcMapfaceIndex;
};

struct AgXMapperSettings
{
  float saturation;
  float linear;
  float compression;
};

struct GTMapperSettings
{
  float contrast;
  float startOfLinearSection;
  float lengthOfLinearSection;
  float toeCurviness;
  float toeFloor;
};

struct TonemapUniforms
{
#ifdef __cplusplus
  TonemapUniforms()
    : tonemapper(0),
      enableDithering(true),
      quantizeBits(8),
      maxDisplayNits(200),
      tonemapOutputColorSpace(COLOR_SPACE_sRGB_NONLINEAR),
      agx(AgXMapperSettings{
        .saturation  = 1.0f,
        .linear      = 0.10f,
        .compression = 0.15f,
      }),
      gt(GTMapperSettings{
        .contrast              = 1.00f,
        .startOfLinearSection  = 0.22f,
        .lengthOfLinearSection = 0.40f,
        .toeCurviness          = 1.33f,
        .toeFloor              = 0.00f,
      })
  {
  }
#endif

  FVOG_UINT32 tonemapper; // 0 = AgX, 1 = Tony, 2 = Linear, 3 = GT
  FVOG_UINT32 enableDithering;
  FVOG_UINT32 quantizeBits;
  FVOG_FLOAT maxDisplayNits;
  FVOG_UINT32 shadingInternalColorSpace; // Tonemap input color space
  FVOG_UINT32 tonemapOutputColorSpace;
  AgXMapperSettings agx;
  GTMapperSettings gt;
};

#endif // TONEMAP_AND_DITHER_H