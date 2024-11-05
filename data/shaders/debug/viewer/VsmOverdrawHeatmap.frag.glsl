#include "../../Config.shared.h"
#include "../../Math.h.glsl"
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
  const uint overdraw = textureLod(Fvog_usampler2D(pc.textureIndex, pc.samplerIndex), v_uv, float(pc.texLevel)).x;

  o_color = vec4(vec3(TurboColormap(overdraw / VSM_MAX_OVERDRAW)), 1);
}