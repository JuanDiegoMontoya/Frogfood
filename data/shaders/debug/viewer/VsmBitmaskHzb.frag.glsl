#version 460 core

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
  const float val = (textureLod(s_texture, vec3(v_uv, uniforms.texLayer), uniforms.texLevel).x);

  o_color = vec4(vec3(val), 1);
}