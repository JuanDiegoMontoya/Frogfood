#ifndef SHADE_DEFERRED_PBR_H
#define SHADE_DEFERRED_PBR_H

#include "Resources.h.glsl"
#include "Color.h.glsl"

#if defined(__cplusplus) || defined(SHADING_PUSH_CONSTANTS)
#ifdef __cplusplus
using namespace shared;
#endif
FVOG_DECLARE_ARGUMENTS(ShadingPushConstants)
{
  FVOG_UINT32 globalUniformsIndex;
  FVOG_UINT32 shadingUniformsIndex;
  FVOG_UINT32 shadowUniformsIndex;
  FVOG_UINT32 lightBufferIndex;

  FVOG_UINT32 gAlbedoIndex;
  FVOG_UINT32 gNormalAndFaceNormalIndex;
  FVOG_UINT32 gDepthIndex;
  FVOG_UINT32 gSmoothVertexNormalIndex;
  FVOG_UINT32 gEmissionIndex;
  FVOG_UINT32 gMetallicRoughnessAoIndex;
  Texture2D ambientOcclusion;
  
  FVOG_UINT32 pageTablesIndex;
  FVOG_UINT32 physicalPagesIndex;
  FVOG_UINT32 vsmBitmaskHzbIndex;
  FVOG_UINT32 vsmUniformsBufferIndex;
  FVOG_UINT32 dirtyPageListBufferIndex;
  FVOG_UINT32 clipmapUniformsBufferIndex;
  FVOG_UINT32 nearestSamplerIndex;
  
  FVOG_UINT32 physicalPagesOverdrawIndex;
  Buffer debugLinesBuffer;
};
#endif

#define LIGHT_TYPE_DIRECTIONAL 0u
#define LIGHT_TYPE_POINT       1u
#define LIGHT_TYPE_SPOT        2u

struct GpuLight
{
#ifdef __cplusplus
  GpuLight() : 
    colorSpace(COLOR_SPACE_sRGB_LINEAR)
  {}
  bool operator==(const GpuLight&) const noexcept = default;
#endif
  FVOG_VEC3 color;
  FVOG_UINT32 type;
  FVOG_VEC3 direction;  // Directional and spot only
  // Point and spot lights use candela (lm/sr) while directional use lux (lm/m^2)
  FVOG_FLOAT intensity;
  FVOG_VEC3 position;        // Point and spot only
  FVOG_FLOAT range;          // Point and spot only
  FVOG_FLOAT innerConeAngle; // Spot only
  FVOG_FLOAT outerConeAngle; // Spot only
  FVOG_UINT32 colorSpace;    // sRGB_LINEAR or BT2020_LINEAR only
  FVOG_UINT32 _padding;
};

#define SHADOW_MODE_VIRTUAL_SHADOW_MAP 0
#define SHADOW_MODE_RAY_TRACED         1

#define SHADOW_MAP_FILTER_NONE 0
#define SHADOW_MAP_FILTER_PCSS 1
#define SHADOW_MAP_FILTER_SMRT 2

struct ShadowUniforms
{
#ifdef __cplusplus
  ShadowUniforms() :
    shadowMode(SHADOW_MODE_VIRTUAL_SHADOW_MAP),
    shadowMapFilter(SHADOW_MAP_FILTER_PCSS),

    pcfSamples(16),
    lightWidth(0.002f), // The sun's real angular radius is about 0.0087 radians.
    maxPcfRadius(0.032f),
    blockerSearchSamples(16),
    blockerSearchRadius(0.032f),

    shadowRays(7),
    stepsPerRay(7),
    rayStepSize(0.1f),
    heightmapThickness(0.5f),
    sourceAngleRad(0.05f),

    rtNumSunShadowRays(1),
    rtSunDiameterRadians(0.0087f),
    rtTraceLocalLights(false)
    {}
#endif

  FVOG_UINT32 shadowMode;
  FVOG_UINT32 shadowMapFilter;

  // PCSS
  FVOG_UINT32 pcfSamples;
  FVOG_FLOAT lightWidth;
  FVOG_FLOAT maxPcfRadius;
  FVOG_UINT32 blockerSearchSamples;
  FVOG_FLOAT blockerSearchRadius;

  // SMRT
  FVOG_UINT32 shadowRays;
  FVOG_UINT32 stepsPerRay;
  FVOG_FLOAT rayStepSize;
  FVOG_FLOAT heightmapThickness;
  FVOG_FLOAT sourceAngleRad;

  // Ray traced
  FVOG_UINT32 rtNumSunShadowRays;
  FVOG_FLOAT rtSunDiameterRadians;
  FVOG_UINT32 rtTraceLocalLights;
};

#define VSM_SHOW_CLIPMAP_ID    (1 << 0)
#define VSM_SHOW_PAGE_ADDRESS  (1 << 1)
#define VSM_SHOW_PAGE_OUTLINES (1 << 2)
#define VSM_SHOW_SHADOW_DEPTH  (1 << 3)
#define VSM_SHOW_DIRTY_PAGES   (1 << 4)
#define BLEND_NORMALS          (1 << 5)
#define VSM_SHOW_OVERDRAW      (1 << 6)
#define SHOW_AO_ONLY           (1 << 7)

#define GI_METHOD_CONSTANT_AMBIENT 1
#define GI_METHOD_PATH_TRACED      2

struct ShadingUniforms
{
#ifdef __cplusplus
  ShadingUniforms() :
    ambientIlluminance(1, 1, 1, 0.03f),
    skyIlluminance(.1f, .3f, .5f, 1.0f),
    debugFlags(0),
    shadingInternalColorSpace(COLOR_SPACE_BT2020_LINEAR),
    captureActive(0),
    globalIlluminationMethod(GI_METHOD_CONSTANT_AMBIENT),
    numGiRays(1),
    numGiBounces(3)
  {}
#endif

  FVOG_VEC4 sunDir;
  FVOG_VEC4 sunIlluminance;
  FVOG_VEC4 ambientIlluminance;
  FVOG_VEC4 skyIlluminance;
  FVOG_VEC2 random;
  FVOG_UINT32 numberOfLights;
  FVOG_UINT32 debugFlags;
  FVOG_UINT32 shadingInternalColorSpace;
  
  // TODO: temp rt stuff
  FVOG_UINT32 materialBufferIndex;
  FVOG_UINT32 instanceBufferIndex;
  FVOG_UINT32 tlasIndex;
  FVOG_IVEC2  captureRayPos;
  FVOG_UINT64 tlasAddress;
  FVOG_BOOL32 captureActive; // 1 == capture rays from one invocation, 2 == capture rays from all invocations

  FVOG_UINT32 globalIlluminationMethod; // For indirect lighting only, but named this way for consistency
  FVOG_UINT32 numGiRays; // TEMP
  FVOG_UINT32 numGiBounces; // TEMP
  Texture2D noiseTexture;
  FVOG_UINT32 frameNumber;
};

#endif // SHADE_DEFERRED_PBR_H