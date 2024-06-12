#ifndef SHADE_DEFERRED_PBR_H
#define SHADE_DEFERRED_PBR_H

#include "Resources.h.glsl"
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
  
  FVOG_UINT32 pageTablesIndex;
  FVOG_UINT32 physicalPagesIndex;
  FVOG_UINT32 vsmBitmaskHzbIndex;
  FVOG_UINT32 vsmUniformsBufferIndex;
  FVOG_UINT32 dirtyPageListBufferIndex;
  FVOG_UINT32 clipmapUniformsBufferIndex;
  FVOG_UINT32 nearestSamplerIndex;
};

#endif // SHADE_DEFERRED_PBR_H