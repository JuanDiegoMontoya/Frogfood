#extension GL_EXT_ray_tracing : require

#include "../Resources.h.glsl"

FVOG_DECLARE_STORAGE_IMAGES(image2D);

FVOG_DECLARE_ARGUMENTS(PushConstant)
{
  uint imageIndex;
};

#define g_image FvogGetStorageImage(image2D, imageIndex)

void main()
{
  const vec2 uv = vec2(gl_LaunchIDEXT.xy) / vec2(gl_LaunchSizeEXT.xy);
  imageStore(g_image, ivec2(gl_LaunchIDEXT.xy), vec4(uv, 0.0, 1.0));
}
