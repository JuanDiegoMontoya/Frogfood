//? #version 460 core
#ifndef RESOURCES_H_GLSL
#define RESOURCES_H_GLSL

#ifdef __cplusplus
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

#define NonUniformIndex nonuniformEXT

// TODO: the bindings should come from a shared header
#define FVOG_DECLARE_SAMPLED_IMAGES(type) \
  layout(set = 0, binding = 3) uniform type t_sampledImages_##type[]

#define FVOG_DECLARE_SAMPLERS \
  layout(set = 0, binding = 4) uniform sampler s_samplers[]

#define FVOG_DECLARE_STORAGE_IMAGES(type) \
  layout(set = 0, binding = 2) uniform type i_storageImages_##type[]

#define FVOG_DECLARE_BUFFER_REFERENCE(typename) \
  layout(buffer_reference, scalar) buffer typename

// Qualifiers can be put in the block name
#define FVOG_DECLARE_STORAGE_BUFFERS(blockname) \
  layout(set = 0, binding = 0, scalar) buffer blockname

#define FvogGetSampledImage(type, index) \
  t_sampledImages_##type[index]

#define FvogGetSampler(index) \
  s_samplers[index]

#define FvogGetStorageImage(name, index) \
  i_storageImages_##name[index]

#define Fvog_sampler2D(textureIndex, samplerIndex) \
  sampler2D(FvogGetSampledImage(texture2D, textureIndex), FvogGetSampler(samplerIndex))

#define Fvog_image2D(imageIndex) \
  FvogGetStorageImage(image2D, imageIndex)

#define Fvog_uimage2D(imageIndex) \
  FvogGetStorageImage(uimage2D, imageIndex)

#endif // __cplusplus
#endif // RESOURCES_H_GLSL
