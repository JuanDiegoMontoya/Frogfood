#ifndef GLOBAL_UNIFORMS_H
#define GLOBAL_UNIFORMS_H

layout (binding = 0, std140) uniform PerFrameUniformsBuffer
{
  mat4 viewProj;
  mat4 oldViewProjUnjittered;
  mat4 viewProjUnjittered;
  mat4 invViewProj;
  mat4 proj;
  vec4 cameraPos;
  vec4 frustumPlanes[6];
  uint meshletCount;
  float bindlessSamplerLodBias;
} perFrameUniforms;

#endif // GLOBAL_UNIFORMS_H