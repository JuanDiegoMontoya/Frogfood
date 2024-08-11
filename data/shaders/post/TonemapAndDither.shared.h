#ifndef TONEMAP_AND_DITHER_H
#define TONEMAP_AND_DITHER_H

#include "../Resources.h.glsl"

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
  float peak;
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
  FVOG_UINT32 tonemapper; // 0 = AgX, 1 = Tony, 2 = Linear, 3 = GT
  FVOG_UINT32 enableDithering;
  FVOG_UINT32 quantizeBits;
  float maxDisplayNits;
  AgXMapperSettings agx;
  GTMapperSettings gt;
};

#endif // TONEMAP_AND_DITHER_H