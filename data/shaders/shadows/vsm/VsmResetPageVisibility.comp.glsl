#version 450 core

#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "VsmCommon.h.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
  const ivec3 gid = ivec3(gl_GlobalInvocationID.xyz);

  if (any(greaterThanEqual(gid, imageSize(i_pageTables))))
  {
    return;
  }

  uint pageData = imageLoad(i_pageTables, gid).x;
  pageData = SetIsPageVisible(pageData, false);
  pageData = SetIsPageDirty(pageData, false);
  imageStore(i_pageTables, gid, uvec4(pageData, 0, 0, 0));
}