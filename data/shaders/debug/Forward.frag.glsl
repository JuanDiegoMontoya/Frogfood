#include "../Resources.h.glsl"
#define VISBUFFER_NO_PUSH_CONSTANTS
#include "../visbuffer/VisbufferCommon.h.glsl"

FVOG_DECLARE_SAMPLERS;
FVOG_DECLARE_SAMPLED_IMAGES(texture2D);

FVOG_DECLARE_ARGUMENTS(DebugForwardArgs)
{
  uint argsBufferIndex;
};

FVOG_DECLARE_STORAGE_BUFFERS(ArgsBuffers)
{
  mat4 clipFromWorld;
  mat4 worldFromObject;
  VertexBuffer vertexBuffer;
  FVOG_UINT32 materialId;
  FVOG_UINT32 materialBufferIndex;
  FVOG_UINT32 samplerIndex;
}argsBuffers[];

#define pc argsBuffers[argsBufferIndex]

layout(location = 0) in vec2 i_uv;
layout(location = 1) in vec3 i_normal;

layout(location = 0) out vec4 o_color;

void main()
{
  o_color.a = 1.0;

  GpuMaterial material = MaterialBuffers[pc.materialBufferIndex].materials[pc.materialId];
  o_color.rgb = material.baseColorFactor.rgb;

  if (bool(material.flags & MATERIAL_HAS_BASE_COLOR))
  {
    o_color.rgb *= texture(Fvog_sampler2D(material.baseColorTextureIndex, pc.samplerIndex), i_uv).rgb;
  }

  //o_color.rgb = vec3(i_uv, 0);
}