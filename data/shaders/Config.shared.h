#ifndef CONFIG_SHARED_H
#define CONFIG_SHARED_H

#define DEPTH_ZERO_TO_ONE
#define REVERSE_Z

#ifdef REVERSE_Z
  #define NEAR_DEPTH 1.0
  #define FAR_DEPTH 0.0
  #define Z_COMPARE_OP_FARTHER <
#else
  #define NEAR_DEPTH 0.0
  #define FAR_DEPTH 1.0
  #define Z_COMPARE_OP_FARTHER >
#endif

#ifdef __cplusplus
#include <Fwog/BasicTypes.h>
  #ifdef REVERSE_Z
    inline constexpr auto FWOG_COMPARE_OP_NEARER = Fwog::CompareOp::GREATER;
  #else
    inline constexpr auto FWOG_COMPARE_OP_NEARER = Fwog::CompareOp::LESS;
  #endif
#endif

#endif // CONFIG_SHARED_H