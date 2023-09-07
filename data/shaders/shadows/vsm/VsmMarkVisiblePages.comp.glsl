#version 450 core

#extension GL_GOOGLE_include_directive : enable

#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "VsmCommon.h.glsl"

layout(binding = 0) uniform sampler2D s_gDepth;

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  const ivec2 gid = ivec2(gl_GlobalInvocationID.xy);

  if (any(greaterThanEqual(gid, textureSize(s_gDepth, 0))))
  {
    return;
  }

  const PageDataAndAddress info = GetClipmapPageFromDepth(s_gDepth, gid);

  if (GetIsPageBacked(info.pageData) == false)
  {
    return;
  }

  if (GetIsPageVisible(info.pageData) == false)
  {
    VsmPageAllocRequest request;
    request.pageTableAddress = info.pageAddress;
    request.pageTableLevel = 0; // TODO: change for lower-res clipmaps
    TryPushAllocRequest(request);
  }
}