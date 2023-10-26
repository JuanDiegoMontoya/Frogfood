#ifndef CONFIG_SHARED_H
#define CONFIG_SHARED_H

#define DEPTH_ZERO_TO_ONE 1
#define REVERSE_Z         1

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