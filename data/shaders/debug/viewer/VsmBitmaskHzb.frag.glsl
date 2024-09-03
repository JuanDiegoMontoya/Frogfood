#include "../../Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(ViewerUniforms)
{
  uint textureIndex;
  uint samplerIndex;
  int texLayer;
  int texLevel;
}pc;

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

void main()
{
  const float val = (textureLod(Fvog_usampler2DArray(pc.textureIndex, pc.samplerIndex), vec3(v_uv, pc.texLayer), float(pc.texLevel)).x);

  o_color = vec4(vec3(val), 1);
}