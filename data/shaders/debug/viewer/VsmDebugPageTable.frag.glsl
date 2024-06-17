#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "../../Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(ViewerUniforms)
{
  uint textureIndex;
  uint samplerIndex;
  int texLayer;
  int texLevel;
}pc;

FVOG_DECLARE_SAMPLERS;
FVOG_DECLARE_SAMPLED_IMAGES(utexture2DArray);

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

// TODO: these helpers will break if they change in VsmCommon.h.glsl
#define PAGE_VISIBLE_BIT (1u)
#define PAGE_DIRTY_BIT (2u)
#define PAGE_BACKED_BIT (4u)
bool GetIsPageVisible(uint pageData)
{
  return (pageData & PAGE_VISIBLE_BIT) != 0u;
}

bool GetIsPageDirty(uint pageData)
{
  return (pageData & PAGE_DIRTY_BIT) != 0u;
}

bool GetIsPageBacked(uint pageData)
{
  return (pageData & PAGE_BACKED_BIT) != 0u;
}

void main()
{
  const uint pageData = textureLod(Fvog_usampler2DArray(pc.textureIndex, pc.samplerIndex), vec3(v_uv, pc.texLayer), pc.texLevel).x;

  o_color = vec4(0, 0, 0, 1);

  if (GetIsPageVisible(pageData))
  {
    o_color.r = 1;
  }

  if (GetIsPageDirty(pageData))
  {
    o_color.g = 1;
  }
  
  if (GetIsPageBacked(pageData))
  {
    o_color.b = 1;
  }
}