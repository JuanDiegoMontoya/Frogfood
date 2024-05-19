#version 460 core
#include "Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(DebugTextureArguments)
{
  FVOG_UINT32 textureIndex;
  FVOG_UINT32 samplerIndex;
};

layout(location = 0) in vec2 v_uv;

//layout(location = 0) uniform sampler2D s_texture;
FVOG_DECLARE_SAMPLED_IMAGES(texture2D);
FVOG_DECLARE_SAMPLERS;

layout(location = 0) out vec4 o_color;

void main()
{
  o_color = texture(Fvog_sampler2D(textureIndex, samplerIndex), v_uv);
}