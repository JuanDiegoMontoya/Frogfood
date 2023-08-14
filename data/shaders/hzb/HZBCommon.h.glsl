#ifndef HZB_COMMON_H
#define HZB_COMMON_H

#include "../Config.h.glsl"

#ifdef REVERSE_Z
  #define REDUCE_NEAR max
  #define REDUCE_FAR min
  #define Z_COMPARE_OP_FARTHER <
#else
  #define REDUCE_NEAR min
  #define REDUCE_FAR max
  #define Z_COMPARE_OP_FARTHER >
#endif

#endif // HZB_COMMON_H