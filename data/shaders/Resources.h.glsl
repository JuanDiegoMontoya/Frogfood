//? #version 460 core
#ifndef DESCRIPTORS_H_GLSL
#define DESCRIPTORS_H_GLSL

#extension GL_EXT_nonuniform_qualifier : require          // descriptor indexing
#extension GL_EXT_scalar_block_layout : require	          // sane buffer layout
#extension GL_EXT_buffer_reference : require              // BDA
#extension GL_EXT_shader_image_load_formatted : require   // readable images without explicit format
#extension GL_EXT_samplerless_texture_functions : require // texelFetch on sampled images

// TODO: the bindings should come from a shared header
#define FVOG_DECLARE_SAMPLED_IMAGE_DESCRIPTOR(type, name) \
  layout(set = 0, binding = 3) uniform type t_sampledImages_##name[]

#define FVOG_DECLARE_SAMPLER_DESCRIPTOR(name) \
  layout(set = 0, binding = 4) uniform sampler s_samplers[]

#define FVOG_DECLARE_STORAGE_IMAGE_DESCRIPTOR(type, name) \
  layout(set = 0, binding = 2) uniform type i_storageImages_##name[]

#define FVOG_DECLARE_BUFFER_REFERENCE(name) \
  layout(buffer_reference, scalar) buffer name

#define FvogGetSampledImage(name, index) \
  t_sampledImages_##name[index]

#define FvogGetSampler(index) \
  s_samplers[index]

#define FvogGetStorageImage(name, index) \
  i_storageImages_##name[index]

#endif // DESCRIPTORS_H_GLSL
