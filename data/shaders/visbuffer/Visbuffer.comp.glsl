#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "VisbufferCommon.h.glsl"
#include "../hzb/HZBCommon.h.glsl"

//#define ENABLE_DEBUG_DRAWING

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

#define WG_SIZE 256
#define MESHLET_PER_WG (WG_SIZE / MAX_PRIMITIVES)

layout (local_size_x = WG_SIZE, local_size_y = 1, local_size_z = 1) in;

layout (binding = 0) uniform sampler2D hzb;

layout (std430, binding = 3) writeonly buffer MeshletPackedBuffer
{
  uint data[];
} indexBuffer;

bool IsAABBInsidePlane(in vec3 center, in vec3 extent, in vec4 plane)
{
  const vec3 normal = plane.xyz;
  const float radius = dot(extent, abs(normal));
  return (dot(normal, center) - plane.w) >= -radius;
}

bool IsMeshletOccluded(in uint meshletId, in uint viewId)
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

  // The nearest projected depth of the object's AABB
  float nearestZ = FAR_DEPTH;

  // Min and max projected coordinates of the object's AABB in UV space
  vec2 minXY = vec2(1.0);
  vec2 maxXY = vec2(0.0);
  for (uint i = 0; i < 8; ++i)
  {
    vec4 clip = oldViewProjUnjittered * transform * vec4(aabbCorners[i], 1.0);

    // AABBs that go behind the camera at all are considered visible
    if (clip.w <= 0)
    {
      return false;
    }

    clip.z = max(clip.z, 0.0);
    clip /= clip.w;
    clip.xy = clamp(clip.xy, -1.0, 1.0);
    clip.xy = clip.xy * 0.5 + 0.5;
    minXY = min(minXY, clip.xy);
    maxXY = max(maxXY, clip.xy);
    nearestZ = clamp(REDUCE_NEAR(nearestZ, clip.z), 0.0, 1.0);
  }

  const vec4 boxUvs = vec4(minXY, maxXY);
  const vec2 hzbSize = vec2(textureSize(hzb, 0));
  const float width = (boxUvs.z - boxUvs.x) * hzbSize.x;
  const float height = (boxUvs.w - boxUvs.y) * hzbSize.y;
  // TODO: the +1 is a hack! But I couldn't figure out another way to fix it without disabling hi-z.
  // Whatever the bug is, it's probably in here since making the hi-z reduction insanely conservative
  // did not fix it.
  const float level = floor(log2(max(width, height))) + 1;
  const float[] depth = float[](
    textureLod(hzb, boxUvs.xy, level).x,
    textureLod(hzb, boxUvs.zy, level).x,
    textureLod(hzb, boxUvs.xw, level).x,
    textureLod(hzb, boxUvs.zw, level).x);
  const float farHZB = REDUCE_FAR(REDUCE_FAR(REDUCE_FAR(depth[0], depth[1]), depth[2]), depth[3]);

  // Object is occluded if its nearest depth is farther away from the camera than the farthest sampled depth
  if (nearestZ Z_COMPARE_OP_FARTHER farHZB)
  {
    return true;
  }
  
#ifdef ENABLE_DEBUG_DRAWING
  if (gl_LocalInvocationID.x % MAX_PRIMITIVES == 0)
  {
    DebugRect rect;
    rect.minOffset = Vec2ToPacked(minXY);
    rect.maxOffset = Vec2ToPacked(maxXY);
    const float GOLDEN_CONJ = 0.6180339887498948482045868343656;
    vec4 color = vec4(2.0 * hsv_to_rgb(vec3(float(meshletId) * GOLDEN_CONJ, 0.875, 0.85)), 1.0);
    rect.color = Vec4ToPacked(color);
    rect.depth = nearestZ;
    TryPushDebugRect(rect);
  }
#endif // ENABLE_DEBUG_DRAWING

  return false;
}

bool IsMeshletVisible(in uint meshletId, in uint viewId)
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
    if (!IsAABBInsidePlane(worldAabbCenter, worldExtent, views[viewId].frustumPlanes[i]))
    {
      return false;
    }
  }
  
  // TODO: do occlusion culling for other views as well
  if (viewId != 0)
    return true;
  
  return !IsMeshletOccluded(meshletId, viewId);
}

shared uint sh_baseIndex[MESHLET_PER_WG];
shared uint sh_primitiveCount[MESHLET_PER_WG];
shared bool sh_isMeshletValid[MESHLET_PER_WG];

void main()
{
  const uint viewId = gl_WorkGroupID.y;
  const uint meshletBaseId = gl_WorkGroupID.x * MESHLET_PER_WG;
  const uint meshletOffset = gl_LocalInvocationID.x / MAX_PRIMITIVES;
  const uint localId = gl_LocalInvocationID.x % MAX_PRIMITIVES;
  const uint meshletId = meshletBaseId + meshletOffset;

  if (localId == 0)
  {
    sh_isMeshletValid[meshletOffset] = meshletId < meshletCount && IsMeshletVisible(meshletId, viewId);

    if (sh_isMeshletValid[meshletOffset])
    {
#ifdef ENABLE_DEBUG_DRAWING
      DebugDrawMeshletAabb(meshletId);
#endif // ENABLE_DEBUG_DRAWING

      sh_baseIndex[meshletOffset] = atomicAdd(commands[viewId].indexCount, meshlets[meshletId].primitiveCount * 3);
      sh_primitiveCount[meshletOffset] = meshlets[meshletId].primitiveCount;
    }
  }

  barrier();

  if (sh_isMeshletValid[meshletOffset] && localId < sh_primitiveCount[meshletOffset])
  {
    const uint baseId = localId * 3;
    const uint indexOffset = sh_baseIndex[meshletOffset] + baseId;
    indexBuffer.data[maxIndices * viewId + indexOffset + 0] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((baseId + 0) & MESHLET_PRIMITIVE_MASK);
    indexBuffer.data[maxIndices * viewId + indexOffset + 1] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((baseId + 1) & MESHLET_PRIMITIVE_MASK);
    indexBuffer.data[maxIndices * viewId + indexOffset + 2] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((baseId + 2) & MESHLET_PRIMITIVE_MASK);
  }
}