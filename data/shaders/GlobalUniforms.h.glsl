#ifndef GLOBAL_UNIFORMS_H
#define GLOBAL_UNIFORMS_H

#define CULL_MESHLET_FRUSTUM    (1 << 0)
#define CULL_MESHLET_HIZ        (1 << 1)
#define CULL_PRIMITIVE_BACKFACE (1 << 2)
#define CULL_PRIMITIVE_FRUSTUM  (1 << 3)
#define CULL_PRIMITIVE_SMALL    (1 << 4)
#define CULL_PRIMITIVE_VSM      (1 << 5)
#define USE_HASHED_TRANSPARENCY (1 << 6)

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
  uint flags;
  float alphaHashScale;
} perFrameUniforms;

#endif // GLOBAL_UNIFORMS_H