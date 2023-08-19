#ifndef HZB_COMMON_H
#define HZB_COMMON_H

#include "../Config.shared.h"

#ifdef REVERSE_Z
  #define REDUCE_NEAR max
  #define REDUCE_FAR min
#else
  #define REDUCE_NEAR min
  #define REDUCE_FAR max
#endif

#endif // HZB_COMMON_H