#include "../Resources.h.glsl"
#include "HZBCommon.h.glsl"

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

FVOG_DECLARE_ARGUMENTS(HzbCopyPushConstants)
{
  FVOG_UINT32 hzbIndex;
  FVOG_UINT32 depthIndex;
  FVOG_UINT32 depthSamplerIndex;
};

void main() {
  const vec2 position = vec2(gl_GlobalInvocationID.xy);
  const vec2 hzb_size = vec2(imageSize(FvogGetStorageImage(image2D, hzbIndex)));
  const vec2 uv = (position + 0.5) / hzb_size;
  const float[] depth = float[](
    textureLodOffset(Fvog_sampler2D(depthIndex, depthSamplerIndex), uv, 0.0, ivec2(0.0, 0.0)).r,
    textureLodOffset(Fvog_sampler2D(depthIndex, depthSamplerIndex), uv, 0.0, ivec2(1.0, 0.0)).r,
    textureLodOffset(Fvog_sampler2D(depthIndex, depthSamplerIndex), uv, 0.0, ivec2(0.0, 1.0)).r,
    textureLodOffset(Fvog_sampler2D(depthIndex, depthSamplerIndex), uv, 0.0, ivec2(1.0, 1.0)).r
  );
  const float depth_sample = REDUCE_FAR(REDUCE_FAR(REDUCE_FAR(depth[0], depth[1]), depth[2]), depth[3]);
  imageStore(FvogGetStorageImage(image2D, hzbIndex), ivec2(position), vec4(depth_sample));
}
