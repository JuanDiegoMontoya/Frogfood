#ifndef GLOBAL_UNIFORMS_H
#define GLOBAL_UNIFORMS_H

#define DEBUG_SKIP_MESHLET_FRUSTUM_CULL    (1 << 0)
#define DEBUG_SKIP_MESHLET_HIZ_CULL        (1 << 1)
#define DEBUG_SKIP_PRIMITIVE_BACKFACE_CULL (1 << 2)
#define DEBUG_SKIP_PRIMITIVE_FRUSTUM_CULL  (1 << 3)
#define DEBUG_SKIP_SMALL_PRIMITIVE_CULL    (1 << 4)

layout (binding = 0, std140) uniform PerFrameUniformsBuffer
{
  mat4 viewProj;
  mat4 oldViewProjUnjittered;
  mat4 viewProjUnjittered;
  mat4 invViewProj;
  mat4 proj;
  mat4 invProj;
  vec4 cameraPos;
  uint meshletCount;
  uint maxIndices;
  float bindlessSamplerLodBias;
  //uint debugFlags;
} perFrameUniforms;

#endif // GLOBAL_UNIFORMS_H