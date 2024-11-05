#ifndef BLOOM_COMMON_H
#define BLOOM_COMMON_H

#include "../Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(BloomUniforms)
{
  FVOG_UINT32 sourceSampledImageIdx;
  FVOG_UINT32 targetSampledImageIdx; // For upsample pass
  FVOG_UINT32 targetStorageImageIdx;
  FVOG_UINT32 linearSamplerIdx;

  FVOG_IVEC2 sourceDim;
  FVOG_IVEC2 targetDim;
  FVOG_FLOAT width;
  FVOG_FLOAT strength;
  FVOG_FLOAT sourceLod;
  FVOG_FLOAT targetLod;
  FVOG_UINT32 numPasses;
  FVOG_UINT32 isFinalPass;
}uniforms;

#ifndef __cplusplus

#define s_source Fvog_sampler2D(uniforms.sourceSampledImageIdx, uniforms.linearSamplerIdx)
#define s_target Fvog_sampler2D(uniforms.targetSampledImageIdx, uniforms.linearSamplerIdx)
#define i_target Fvog_image2D(uniforms.targetStorageImageIdx)

#endif // __cplusplus

#endif // BLOOM_COMMON_H