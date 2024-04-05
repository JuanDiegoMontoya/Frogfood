#ifndef CONFIG_SHARED_H
#define CONFIG_SHARED_H

#define DEPTH_ZERO_TO_ONE 1
#define REVERSE_Z         1

// Adds a depth buffer that is cleared before each VSM clipmap is drawn and forces early fragment tests.
// Reduces overdraw and improves perf in certain situations.
#define VSM_USE_TEMP_ZBUFFER 1

// Adds a stencil buffer that is cleared, then primed before each VSM clipmap is drawn and forces early fragment tests.
// Prevents FS invocations in inactive pages, but hurts perf in tests.
#define VSM_USE_TEMP_SBUFFER 0

// If true, VSM will properly support alpha masked geometry (e.g. foliage) that requires sampling and discarding.
// WARNING: because the above two options enable early fragment tests, rendering will be incorrect if used with those.
#define VSM_SUPPORT_ALPHA_MASKED_GEOMETRY 0

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
  #if REVERSE_Z
    inline constexpr auto FWOG_COMPARE_OP_NEARER = Fwog::CompareOp::GREATER;
  #else
    inline constexpr auto FWOG_COMPARE_OP_NEARER = Fwog::CompareOp::LESS;
  #endif
#endif

#endif // CONFIG_SHARED_H
