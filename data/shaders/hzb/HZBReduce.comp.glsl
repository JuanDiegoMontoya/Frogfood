#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "HZBCommon.h.glsl"

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout (r32f, binding = 0) uniform restrict readonly image2D prev_hzb;
layout (r32f, binding = 1) uniform restrict writeonly image2D curr_hzb;

void main() {
  const vec2 position = vec2(gl_GlobalInvocationID.xy);
  const vec2 prev_hzb_size = vec2(imageSize(prev_hzb));
  const vec2 curr_hzb_size = vec2(imageSize(curr_hzb));
  const vec2 scaled_pos = position * (prev_hzb_size / curr_hzb_size);
  const float[] depths = float[](
    imageLoad(prev_hzb, ivec2(scaled_pos + vec2(0.0, 0.0) + 0.5)).r,
    imageLoad(prev_hzb, ivec2(scaled_pos + vec2(1.0, 0.0) + 0.5)).r,
    imageLoad(prev_hzb, ivec2(scaled_pos + vec2(0.0, 1.0) + 0.5)).r,
    imageLoad(prev_hzb, ivec2(scaled_pos + vec2(1.0, 1.0) + 0.5)).r
  );
  const float depth = REDUCE_FAR(REDUCE_FAR(REDUCE_FAR(depths[0], depths[1]), depths[2]), depths[3]);
  imageStore(curr_hzb, ivec2(position), vec4(depth));
}
