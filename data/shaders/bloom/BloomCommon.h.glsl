#ifndef BLOOM_COMMON_H
#define BLOOM_COMMON_H

layout(binding = 0, std140) uniform UniformBuffer
{
  ivec2 sourceDim;
  ivec2 targetDim;
  float width;
  float strength;
  float sourceLod;
  float targetLod;
}uniforms;

#endif // BLOOM_COMMON_H