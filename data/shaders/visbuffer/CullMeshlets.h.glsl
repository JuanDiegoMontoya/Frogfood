#ifndef CULL_MESHLETS_H
#define CULL_MESHLETS_H

#include "../Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(CullMeshletsPushConstants)
{
  FVOG_UINT32 globalUniformsIndex;
  FVOG_UINT32 meshletInstancesIndex;
  FVOG_UINT32 meshletDataIndex;
  FVOG_UINT32 meshletPrimitivesIndex;
  FVOG_UINT32 meshletVerticesIndex;
  FVOG_UINT32 meshletIndicesIndex;
  FVOG_UINT32 transformsIndex;
  FVOG_UINT32 indirectDrawIndex;
  FVOG_UINT32 materialsIndex;
  FVOG_UINT32 viewIndex;
  
  FVOG_UINT32 pageTablesIndex;
  FVOG_UINT32 physicalPagesIndex;
  FVOG_UINT32 vsmBitmaskHzbIndex;
  FVOG_UINT32 vsmUniformsBufferIndex;
  FVOG_UINT32 dirtyPageListBufferIndex;
  FVOG_UINT32 clipmapUniformsBufferIndex;
  FVOG_UINT32 nearestSamplerIndex;
  FVOG_UINT32 pageClearDispatchIndex;
  
  FVOG_UINT32 hzbIndex;
  FVOG_UINT32 hzbSamplerIndex;
  FVOG_UINT32 cullTrianglesDispatchIndex;

  FVOG_UINT32 visibleMeshletsIndex;
  
  // CullTriangles.comp
  FVOG_UINT32 indexBufferIndex;
  
  // Debug
  FVOG_UINT32 debugAabbBufferIndex;
  FVOG_UINT32 debugRectBufferIndex;
};

#endif // CULL_MESHLETS_H