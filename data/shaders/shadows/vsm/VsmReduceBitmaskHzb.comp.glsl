#include "../../Math.h.glsl"
#include "../../GlobalUniforms.h.glsl"
#include "VsmCommon.h.glsl"

#define i_srcVsmBitmaskHzb Fvog_uimage2DArray(srcVsmBitmaskHzbIndex)
#define i_dstVsmBitmaskHzb Fvog_uimage2DArray(dstVsmBitmaskHzbIndex)

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
  const ivec3 gid = ivec3(gl_GlobalInvocationID.xyz);
  
  if (any(greaterThanEqual(gid, imageSize(i_dstVsmBitmaskHzb))))
  {
    return;
  }

  uint seen = 0;

  // Read from virtual pages if first pass
  if (currentPass == 0)
  {
    const uint pageData = imageLoad(i_pageTables, gid).x;

    if (GetIsPageBacked(pageData) && GetIsPageVisible(pageData) && GetIsPageDirty(pageData))
    {
      seen = 1;
    }
  }
  // Read from previous level of virtual HZB
  else
  {
    const uint bl = imageLoad(i_srcVsmBitmaskHzb, ivec3(gid.xy * 2 + ivec2(0, 0), gid.z)).x;
    const uint br = imageLoad(i_srcVsmBitmaskHzb, ivec3(gid.xy * 2 + ivec2(0, 1), gid.z)).x;
    const uint tl = imageLoad(i_srcVsmBitmaskHzb, ivec3(gid.xy * 2 + ivec2(1, 0), gid.z)).x;
    const uint tr = imageLoad(i_srcVsmBitmaskHzb, ivec3(gid.xy * 2 + ivec2(1, 1), gid.z)).x;

    seen = bl | br | tl | tr;
  }

  imageStore(i_dstVsmBitmaskHzb, gid, uvec4(seen, 0, 0, 0));
}