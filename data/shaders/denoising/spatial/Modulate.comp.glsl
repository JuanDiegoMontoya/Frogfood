#version 460 core
#include "../../Resources.h.glsl"

FVOG_DECLARE_ARGUMENTS(ModulateArgs)
{
  Texture2D albedo;
  Texture2D illuminance;
  Texture2D radiance;
  Image2D sceneColor;
}pc;

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  ivec2 targetDim = imageSize(pc.sceneColor);
  if (any(greaterThanEqual(gid, targetDim)))
  {
    return;
  }

  vec3 albedo = texelFetch(pc.albedo, gid, 0).rgb;
  vec3 ambient = texelFetch(pc.illuminance, gid, 0).rgb;
  vec3 direct = texelFetch(pc.radiance, gid, 0).rgb;

  imageStore(pc.sceneColor, gid, vec4(pow(direct + ambient * albedo, vec3(1 / 2.2)), 1.0));
}