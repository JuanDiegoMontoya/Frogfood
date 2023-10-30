#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "VisbufferCommon.h.glsl"
#include "../hzb/HZBCommon.h.glsl"
#include "../shadows/vsm/VsmCommon.h.glsl"

#define ENABLE_DEBUG_DRAWING

#ifdef ENABLE_DEBUG_DRAWING

#include "../debug/DebugCommon.h.glsl"

void DebugDrawMeshletAabb(in uint meshletId)
{
  const uint instanceId = meshlets[meshletId].instanceId;
  const mat4 transform = transforms[instanceId];
  const vec3 aabbMin = PackedToVec3(meshlets[meshletId].aabbMin);
  const vec3 aabbMax = PackedToVec3(meshlets[meshletId].aabbMax);
  const vec3 aabbSize = aabbMax - aabbMin;
  const vec3[] aabbCorners = vec3[](
    aabbMin,
    aabbMin + vec3(aabbSize.x, 0.0, 0.0),
    aabbMin + vec3(0.0, aabbSize.y, 0.0),
    aabbMin + vec3(0.0, 0.0, aabbSize.z),
    aabbMin + vec3(aabbSize.xy, 0.0),
    aabbMin + vec3(0.0, aabbSize.yz),
    aabbMin + vec3(aabbSize.x, 0.0, aabbSize.z),
    aabbMin + aabbSize);

  vec3 worldAabbMin = vec3(1e20);
  vec3 worldAabbMax = vec3(-1e20);
  for (uint i = 0; i < 8; ++i)
  {
    vec3 world = vec3(transform * vec4(aabbCorners[i], 1.0));
    worldAabbMin = min(worldAabbMin, world);
    worldAabbMax = max(worldAabbMax, world);
  }

  const vec3 aabbCenter = (worldAabbMin + worldAabbMax) / 2.0;
  const vec3 extent = (worldAabbMax - worldAabbMin);
  
  const float GOLDEN_CONJ = 0.6180339887498948482045868343656;
  vec4 color = vec4(2.0 * hsv_to_rgb(vec3(float(meshletId) * GOLDEN_CONJ, 0.875, 0.85)), 1.0);
  TryPushDebugAabb(DebugAabb(Vec3ToPacked(aabbCenter), Vec3ToPacked(extent), Vec4ToPacked(color)));
}
#endif // ENABLE_DEBUG_DRAWING

layout (binding = 0) uniform sampler2D s_hzb;

layout (std430, binding = 3) writeonly buffer MeshletPackedBuffer
{
  uint data[];
} indexBuffer;

layout(std430, binding = 9) restrict buffer MeshletVisibilityBuffer
{
  uint indices[];
} visibleMeshlets;

layout(std430, binding = 10) restrict buffer CullTrianglesDispatchParams
{
  uint groupCountX;
  uint groupCountY;
  uint groupCountZ;
} cullTrianglesDispatch;

bool IsAABBInsidePlane(in vec3 center, in vec3 extent, in vec4 plane)
{
  const vec3 normal = plane.xyz;
  const float radius = dot(extent, abs(normal));
  return (dot(normal, center) - plane.w) >= -radius;
}

struct GetMeshletUvBoundsParams
{
  uint meshletId;
  mat4 viewProj;
  bool clampNdc;
  bool reverseZ;
};

void GetMeshletUvBounds(GetMeshletUvBoundsParams params, out vec2 minXY, out vec2 maxXY, out float nearestZ, out bool intersectsNearPlane)
{
  const uint instanceId = meshlets[params.meshletId].instanceId;
  const mat4 transform = transforms[instanceId];
  const vec3 aabbMin = PackedToVec3(meshlets[params.meshletId].aabbMin);
  const vec3 aabbMax = PackedToVec3(meshlets[params.meshletId].aabbMax);
  const vec3 aabbSize = aabbMax - aabbMin;
  const vec3[] aabbCorners = vec3[](
    aabbMin,
    aabbMin + vec3(aabbSize.x, 0.0, 0.0),
    aabbMin + vec3(0.0, aabbSize.y, 0.0),
    aabbMin + vec3(0.0, 0.0, aabbSize.z),
    aabbMin + vec3(aabbSize.xy, 0.0),
    aabbMin + vec3(0.0, aabbSize.yz),
    aabbMin + vec3(aabbSize.x, 0.0, aabbSize.z),
    aabbMin + aabbSize);

  // The nearest projected depth of the object's AABB
  if (params.reverseZ)
  {
    nearestZ = 0;
  }
  else
  {
    nearestZ = 1;
  }

  // Min and max projected coordinates of the object's AABB in UV space
  minXY = vec2(1e20);
  maxXY = vec2(-1e20);
  const mat4 mvp = params.viewProj * transform;
  for (uint i = 0; i < 8; ++i)
  {
    vec4 clip = mvp * vec4(aabbCorners[i], 1.0);

    // AABBs that go behind the camera at all are considered visible
    if (clip.w <= 0)
    {
      intersectsNearPlane = true;
      return;
    }

    clip.z = max(clip.z, 0.0);
    clip /= clip.w;
    if (params.clampNdc)
    {
      clip.xy = clamp(clip.xy, -1.0, 1.0);
    }
    clip.xy = clip.xy * 0.5 + 0.5;
    minXY = min(minXY, clip.xy);
    maxXY = max(maxXY, clip.xy);
    if (params.reverseZ)
    {
      nearestZ = clamp(max(nearestZ, clip.z), 0.0, 1.0);
    }
    else
    {
      nearestZ = clamp(min(nearestZ, clip.z), 0.0, 1.0);
    }
  }

  intersectsNearPlane = false;
}

bool CullQuadHiz(vec2 minXY, vec2 maxXY, float nearestZ)
{
  const vec4 boxUvs = vec4(minXY, maxXY);
  const vec2 hzbSize = vec2(textureSize(s_hzb, 0));
  const float width = (boxUvs.z - boxUvs.x) * hzbSize.x;
  const float height = (boxUvs.w - boxUvs.y) * hzbSize.y;
  
  // Select next level so the box is always in [0.5, 1.0) of a texel of the current level.
  // If the box is larger than a single texel of the current level, then it could touch nine
  // texels rather than four! So we need to round up to the next level.
  const float level = ceil(log2(max(width, height)));
  const float[4] depth = float[](
    textureLod(s_hzb, boxUvs.xy, level).x,
    textureLod(s_hzb, boxUvs.zy, level).x,
    textureLod(s_hzb, boxUvs.xw, level).x,
    textureLod(s_hzb, boxUvs.zw, level).x);
  const float farHZB = REDUCE_FAR(REDUCE_FAR(REDUCE_FAR(depth[0], depth[1]), depth[2]), depth[3]);

  // Object is occluded if its nearest depth is farther away from the camera than the farthest sampled depth
  if (nearestZ Z_COMPARE_OP_FARTHER farHZB)
  {
    return false;
  }

  return true;
}

bool CullMeshletFrustum(in uint meshletId, View view)
{
  const uint instanceId = meshlets[meshletId].instanceId;
  const mat4 transform = transforms[instanceId];
  const vec3 aabbMin = PackedToVec3(meshlets[meshletId].aabbMin);
  const vec3 aabbMax = PackedToVec3(meshlets[meshletId].aabbMax);
  const vec3 aabbCenter = (aabbMin + aabbMax) / 2.0;
  const vec3 aabbExtent = aabbMax - aabbCenter;
  const vec3 worldAabbCenter = vec3(transform * vec4(aabbCenter, 1.0));
  const vec3 right = vec3(transform[0]) * aabbExtent.x;
  const vec3 up = vec3(transform[1]) * aabbExtent.y;
  const vec3 forward = vec3(-transform[2]) * aabbExtent.z;

  const vec3 worldExtent = vec3(
    abs(dot(vec3(1.0, 0.0, 0.0), right)) +
    abs(dot(vec3(1.0, 0.0, 0.0), up)) +
    abs(dot(vec3(1.0, 0.0, 0.0), forward)),

    abs(dot(vec3(0.0, 1.0, 0.0), right)) +
    abs(dot(vec3(0.0, 1.0, 0.0), up)) +
    abs(dot(vec3(0.0, 1.0, 0.0), forward)),

    abs(dot(vec3(0.0, 0.0, 1.0), right)) +
    abs(dot(vec3(0.0, 0.0, 1.0), up)) +
    abs(dot(vec3(0.0, 0.0, 1.0), forward)));
  for (uint i = 0; i < 6; ++i)
  {
    if (!IsAABBInsidePlane(worldAabbCenter, worldExtent, view.frustumPlanes[i]))
    {
      return false;
    }
  }

  return true;
}

layout (local_size_x = 128) in;
void main()
{
  const uint meshletId = gl_GlobalInvocationID.x;

  if (meshletId >= perFrameUniforms.meshletCount)
  {
    return;
  }

  if ((perFrameUniforms.flags & CULL_MESHLET_FRUSTUM) == 0 || CullMeshletFrustum(meshletId, currentView))
  {
    bool isVisible = false;
    
    GetMeshletUvBoundsParams params;
    params.meshletId = meshletId;

    if (currentView.type == VIEW_TYPE_MAIN)
    {
      params.viewProj = perFrameUniforms.oldViewProjUnjittered;
      params.clampNdc = true;
      params.reverseZ = bool(REVERSE_Z);
    }
    else if (currentView.type == VIEW_TYPE_VIRTUAL)
    {
      params.viewProj = currentView.viewProjStableForVsmOnly;
      params.clampNdc = false;
      params.reverseZ = false;
    }

    vec2 minXY;
    vec2 maxXY;
    float nearestZ;
    bool intersectsNearPlane;
    GetMeshletUvBounds(params, minXY, maxXY, nearestZ, intersectsNearPlane);
    isVisible = intersectsNearPlane;

    if (!isVisible)
    {
      if (currentView.type == VIEW_TYPE_MAIN)
      {
        if ((perFrameUniforms.flags & CULL_MESHLET_HIZ) == 0)
        {
          isVisible = true;
        }
        else
        {
          // Hack to get around apparent precision issue for tiny meshlets
          isVisible = CullQuadHiz(minXY, maxXY, nearestZ + 0.0001);
        }
      }
      else if (currentView.type == VIEW_TYPE_VIRTUAL)
      {
        isVisible = CullQuadVsm(minXY, maxXY, currentView.virtualTableIndex);
      }
    }

    if (isVisible)
    {
      const uint idx = atomicAdd(cullTrianglesDispatch.groupCountX, 1);
      visibleMeshlets.indices[idx] = meshletId;

 #ifdef ENABLE_DEBUG_DRAWING
      if (currentView.type == VIEW_TYPE_MAIN)
      {
        DebugDrawMeshletAabb(meshletId);
        // Uncommenting this causes a segfault in NV drivers (a crash, not a compilation error!)
        // DebugRect rect;
        // rect.minOffset = Vec2ToPacked(minXY);
        // rect.maxOffset = Vec2ToPacked(maxXY);
        // const float GOLDEN_CONJ = 0.6180339887498948482045868343656;
        // vec4 color = vec4(2.0 * hsv_to_rgb(vec3(float(meshletId) * GOLDEN_CONJ, 0.875, 0.85)), 1.0);
        // rect.color = Vec4ToPacked(color);
        // rect.depth = nearestZ;
        //TryPushDebugRect(rect);
      }
 #endif // ENABLE_DEBUG_DRAWING
    }
  }
}