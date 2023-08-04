#ifndef BLOOM_DOWNSAMPLE_COMMON_H
#define BLOOM_DOWNSAMPLE_COMMON_H

// FP16 support
#if 0
  // #extension GL_EXT_shader_explicit_arithmetic_types : enable
  #extension GL_NV_gpu_shader5 : enable
  #extension GL_AMD_gpu_shader_half_float : enable
  #if !defined(GL_NV_gpu_shader5) && !defined(GL_AMD_gpu_shader_half_float)
    #define f16vec3 vec3
  #endif
#else
  #define f16vec3 vec3
#endif

layout(binding = 0) uniform sampler2D s_source;
layout(binding = 0) uniform writeonly image2D i_target;

// Should be safe to have up to 32x32 (1024) threads (minimum guaranteed).
// 32x32 threads will use slightly less than 32KB shared memory (minimum guaranteed) with full precision vectors (worst case).
// Large groups suffer from barrier() overhead, especially when computing smaller mips.
layout(local_size_x = LOCAL_SIZE_X, local_size_y = LOCAL_SIZE_Y) in;

/*
We take 13 bilinear samples of the source texture as such:

 O   O   O
   o   o
 O   X   O
   o   o
 O   O   O

 where X is the position of the pixel we are computing.
 Samples are intentionally staggered.
*/

// Cached samples corresponding to the large 'O's in the above image
shared f16vec3 sh_coarseSamples[gl_WorkGroupSize.x + 2][gl_WorkGroupSize.y + 2];

// Cached samples corresponding to the small 'o's in the above image
shared f16vec3 sh_preciseSamples[gl_WorkGroupSize.x + 1][gl_WorkGroupSize.y + 1];

void InitializeSharedMemory(ivec2 targetDim, ivec2 sourceDim, float sourceLod)
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  const ivec2 lid = ivec2(gl_LocalInvocationID.xy);

  // Center of written pixel
  const vec2 uv = (vec2(gid) + 0.5) / targetDim;

  // Minimum caching for each output pixel
  sh_coarseSamples[lid.x + 1][lid.y + 1] = f16vec3(textureLod(s_source, uv + vec2(0, 0) / sourceDim, sourceLod).rgb);
  sh_preciseSamples[lid.x + 0][lid.y + 0] = f16vec3(textureLod(s_source, uv + vec2(-1, -1) / sourceDim, sourceLod).rgb);

  // Pixels on the edge of the thread group
  // Left
  if (lid.x == 0)
  {
    sh_coarseSamples[lid.x + 0][lid.y + 1] = f16vec3(textureLod(s_source, uv + vec2(-2, 0) / sourceDim, sourceLod).rgb);
  }

  // Right
  if (lid.x == gl_WorkGroupSize.x - 1)
  {
    sh_coarseSamples[lid.x + 2][lid.y + 1] = f16vec3(textureLod(s_source, uv + vec2(2, 0) / sourceDim, sourceLod).rgb);
    sh_preciseSamples[lid.x + 1][lid.y + 0] = f16vec3(textureLod(s_source, uv + vec2(1, -1) / sourceDim, sourceLod).rgb);
  }

  // Bottom
  if (lid.y == 0)
  {
    sh_coarseSamples[lid.x + 1][lid.y + 0] = f16vec3(textureLod(s_source, uv + vec2(0, -2) / sourceDim, sourceLod).rgb);
  }
  
  // Top
  if (lid.y == gl_WorkGroupSize.y - 1)
  {
    sh_coarseSamples[lid.x + 1][lid.y + 2] = f16vec3(textureLod(s_source, uv + vec2(0, 2) / sourceDim, sourceLod).rgb);
    sh_preciseSamples[lid.x + 0][lid.y + 1] = f16vec3(textureLod(s_source, uv + vec2(-1, 1) / sourceDim, sourceLod).rgb);
  }

  // Bottom-left
  if (lid.x == 0 && lid.y == 0)
  {
    sh_coarseSamples[lid.x + 0][lid.y + 0] = f16vec3(textureLod(s_source, uv + vec2(-2, -2) / sourceDim, sourceLod).rgb);
  }

  // Bottom-right
  if (lid.x == gl_WorkGroupSize.x - 1 && lid.y == 0)
  {
    sh_coarseSamples[lid.x + 2][lid.y + 0] = f16vec3(textureLod(s_source, uv + vec2(2, -2) / sourceDim, sourceLod).rgb);
  }

  // Top-left
  if (lid.x == 0 && lid.y == gl_WorkGroupSize.y - 1)
  {
    sh_coarseSamples[lid.x + 0][lid.y + 2] = f16vec3(textureLod(s_source, uv + vec2(-2, 2) / sourceDim, sourceLod).rgb);
  }
  
  // Top-right
  if (lid == gl_WorkGroupSize.xy - 1)
  {
    sh_coarseSamples[lid.x + 2][lid.y + 2] = f16vec3(textureLod(s_source, uv + vec2(2, 2) / sourceDim, sourceLod).rgb);
    sh_preciseSamples[lid.x + 1][lid.y + 1] = f16vec3(textureLod(s_source, uv + vec2(1, 1) / sourceDim, sourceLod).rgb);
  }
}

#endif // BLOOM_DOWNSAMPLE_COMMON_H