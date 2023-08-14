#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "HZBCommon.h.glsl"

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout (r32f, binding = 0) uniform restrict writeonly image2D hzb;
layout (binding = 1) uniform sampler2D depth;

void main() {
  const vec2 position = vec2(gl_GlobalInvocationID.xy);
  const vec2 hzb_size = vec2(imageSize(hzb));
  const vec2 uv = (position + 0.5) / hzb_size;
  const float[] depth = float[](
    textureLodOffset(depth, uv, 0.0, ivec2(0.0, 0.0)).r,
    textureLodOffset(depth, uv, 0.0, ivec2(1.0, 0.0)).r,
    textureLodOffset(depth, uv, 0.0, ivec2(0.0, 1.0)).r,
    textureLodOffset(depth, uv, 0.0, ivec2(1.0, 1.0)).r
  );
  const float depth_sample = REDUCE_FAR(REDUCE_FAR(REDUCE_FAR(depth[0], depth[1]), depth[2]), depth[3]);
  imageStore(hzb, ivec2(position), vec4(depth_sample));
}
