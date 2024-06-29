#ifndef CONFIG_SHARED_H
#define CONFIG_SHARED_H

#define DEPTH_ZERO_TO_ONE 1
#define REVERSE_Z         1

// Adds a depth buffer that is cleared before each VSM clipmap is drawn and forces early fragment tests.
// Reduces overdraw and improves perf in certain situations.
#define VSM_USE_TEMP_ZBUFFER 0

// If true, VSM will properly support alpha masked geometry (e.g. foliage) that requires sampling and discarding.
// WARNING: because the above option enables early fragment tests, rendering will be incorrect if used with it.
#define VSM_SUPPORT_ALPHA_MASKED_GEOMETRY 1

// When enabled VsmShadow.frag will increment a per-texel overdraw counter.
// This allows for overdraw debug visualizations at the expense of some perf.
#define VSM_RENDER_OVERDRAW 1

// When VSM_RENDER_OVERDRAW is enabled, clamp the colormap to this amount of overdraw.
// This number should be a float.
#define VSM_MAX_OVERDRAW 32.0f

#if REVERSE_Z
  #define NEAR_DEPTH 1.0f
  #define FAR_DEPTH 0.0f
  #define Z_COMPARE_OP_FARTHER <
#else
  #define NEAR_DEPTH 0.0f
  #define FAR_DEPTH 1.0f
  #define Z_COMPARE_OP_FARTHER >
#endif

#ifdef __cplusplus
#include <Fwog/BasicTypes.h>
#include <volk.h>
  #if REVERSE_Z
    inline constexpr auto FWOG_COMPARE_OP_NEARER = Fwog::CompareOp::GREATER;
    inline constexpr auto FVOG_COMPARE_OP_NEARER = VK_COMPARE_OP_GREATER;
  #else
    inline constexpr auto FWOG_COMPARE_OP_NEARER = Fwog::CompareOp::LESS;
  #endif
#endif

#endif // CONFIG_SHARED_H
