#ifndef LIGHT_H_GLSL
#define LIGHT_H_GLSL

#include "Resources.h.glsl"
#include "Color.h.glsl"

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

#endif // LIGHT_H_GLSL