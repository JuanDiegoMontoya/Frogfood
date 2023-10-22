#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "../../shadows/vsm/VsmCommon.h.glsl"

layout(binding = 0) uniform ViewerUniforms
{
  int texLayer;
  int texLevel;
}uniforms;

layout(binding = 0) uniform usampler2DArray s_texture;

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

void main()
{
  const uint pageData = textureLod(s_texture, vec3(v_uv, uniforms.texLayer), uniforms.texLevel).x;

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