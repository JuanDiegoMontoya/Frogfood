//? #version 460 core
#ifndef RESOURCES_H_GLSL
#define RESOURCES_H_GLSL

#ifdef __cplusplus

#include <cstdint>

#define FVOG_FLOAT float
#define FVOG_VEC2 glm::vec2
#define FVOG_VEC3 glm::vec3
#define FVOG_VEC4 glm::vec4

#define FVOG_INT32 int32_t
#define FVOG_IVEC2 glm::ivec2
#define FVOG_IVEC3 glm::ivec3
#define FVOG_IVEC4 glm::ivec4

#define FVOG_UINT32 uint32_t
#define FVOG_UVEC2 glm::uvec2
#define FVOG_UVEC3 glm::uvec3
#define FVOG_UVEC4 glm::uvec4

#define FVOG_MAT4 glm::mat4

#define FVOG_DECLARE_ARGUMENTS(name) \
  struct name

#else // GLSL

#define FVOG_FLOAT float
#define FVOG_VEC2 vec2
#define FVOG_VEC3 vec3
#define FVOG_VEC4 vec4

#define FVOG_INT32 int
#define FVOG_IVEC2 ivec2
#define FVOG_IVEC3 ivec3
#define FVOG_IVEC4 ivec4

#define FVOG_UINT32 uint
#define FVOG_UVEC2 uvec2
#define FVOG_UVEC3 uvec3
#define FVOG_UVEC4 uvec4

#define FVOG_MAT4 mat4

#define FVOG_DECLARE_ARGUMENTS(name) \
  layout(push_constant, scalar) uniform name

#extension GL_EXT_nonuniform_qualifier : require          // descriptor indexing
#extension GL_EXT_scalar_block_layout : require	          // sane buffer layout
#extension GL_EXT_buffer_reference : require              // BDA
#extension GL_EXT_shader_image_load_formatted : require   // readable images without explicit format
#extension GL_EXT_samplerless_texture_functions : require // texelFetch on sampled images
#extension GL_EXT_debug_printf : enable                   // printf

#define NonUniformIndex nonuniformEXT
#define printf debugPrintfEXT

#ifdef GL_EXT_debug_printf
  #define ASSERT_MSG(x, msg) do { if (!bool(x)) { printf(msg); } } while(false)
  #define ASSERT(x) ASSERT_MSG(x, "Assertion failed!\n")
  #define UNREACHABLE ASSERT_MSG(false, "Unreachable path taken!\n")
#else
  #define ASSERT_MSG(x, msg) (int(0))
  #define ASSERT(x) (int(0))
  #define UNREACHABLE (int(0))
#endif

#define FVOG_STORAGE_BUFFER_BINDING 0
#define FVOG_STORAGE_IMAGE_BINDING 2
#define FVOG_SAMPLED_IMAGE_BINDING 3
#define FVOG_SAMPLER_BINDING 4

// TODO: the bindings should come from a shared header
#define FVOG_DECLARE_SAMPLED_IMAGES(type) \
  layout(set = 0, binding = FVOG_SAMPLED_IMAGE_BINDING) uniform type t_sampledImages_##type[]

#define FVOG_DECLARE_SAMPLERS \
  layout(set = 0, binding = FVOG_SAMPLER_BINDING) uniform sampler s_samplers[]

#define FVOG_DECLARE_STORAGE_IMAGES(type) \
  layout(set = 0, binding = FVOG_STORAGE_IMAGE_BINDING) uniform type i_storageImages_##type[]

#define FVOG_DECLARE_BUFFER_REFERENCE(typename) \
  layout(buffer_reference, scalar) buffer typename

// Qualifiers can be put in the block name
#define FVOG_DECLARE_STORAGE_BUFFERS(blockname) \
  layout(set = 0, binding = FVOG_STORAGE_BUFFER_BINDING, std430) buffer blockname

#define FvogGetSampledImage(type, index) \
  t_sampledImages_##type[index]

#define FvogGetSampler(index) \
  s_samplers[index]

#define FvogGetStorageImage(name, index) \
  i_storageImages_##name[index]

#define Fvog_sampler2D(textureIndex, samplerIndex) \
  NonUniformIndex(sampler2D(FvogGetSampledImage(texture2D, textureIndex), FvogGetSampler(samplerIndex)))
  
#define Fvog_usampler2D(textureIndex, samplerIndex) \
  NonUniformIndex(usampler2D(FvogGetSampledImage(utexture2D, textureIndex), FvogGetSampler(samplerIndex)))

#define Fvog_usampler2DArray(textureIndex, samplerIndex) \
  NonUniformIndex(usampler2DArray(FvogGetSampledImage(utexture2DArray, textureIndex), FvogGetSampler(samplerIndex)))

#define Fvog_sampler3D(textureIndex, samplerIndex) \
  NonUniformIndex(sampler3D(Fvog_texture3D(textureIndex), FvogGetSampler(samplerIndex)))

#define Fvog_image2D(imageIndex) \
  FvogGetStorageImage(image2D, imageIndex)

#define Fvog_uimage2D(imageIndex) \
  FvogGetStorageImage(uimage2D, imageIndex)
  
#define Fvog_uimage2DArray(imageIndex) \
  FvogGetStorageImage(uimage2DArray, imageIndex)

#define Fvog_utexture2D(textureIndex) \
  FvogGetSampledImage(utexture2D, textureIndex)

#define Fvog_texture3D(textureIndex) \
  FvogGetSampledImage(texture3D, textureIndex)

#endif // __cplusplus
#endif // RESOURCES_H_GLSL
