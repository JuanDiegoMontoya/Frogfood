#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "VisbufferCommon.h.glsl"
#include "../debug/DebugCommon.h.glsl"

#define WG_SIZE 256
#define MESHLET_PER_WG (WG_SIZE / MAX_PRIMITIVES)

layout (local_size_x = WG_SIZE, local_size_y = 1, local_size_z = 1) in;

layout (location = 0) uniform sampler2D hzb;

layout (std430, binding = 3) writeonly buffer MeshletPackedBuffer
{
  uint data[];
} indexBuffer;

shared uint baseIndex[MESHLET_PER_WG];
shared uint primitiveCount[MESHLET_PER_WG];

bool IsAABBInsidePlane(in vec3 center, in vec3 extent, in vec4 plane)
{
  const vec3 normal = plane.xyz;
  const float radius = dot(extent, abs(normal));
  return (dot(normal, center) - plane.w) >= -radius;
}

bool IsMeshletOccluded(in vec3 aabbMin, in vec3 aabbMax, in mat4 transform)
{
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
  float minZ = 1.0;
  vec2 minXY = vec2(1.0);
  vec2 maxXY = vec2(0.0);
  for (uint i = 0; i < 8; ++i)
  {
    vec4 clip = oldViewProjUnjittered * transform * vec4(aabbCorners[i], 1.0);
    clip.z = max(clip.z, 0.0);
    clip /= clip.w;
    clip.xy = clamp(clip.xy, -1.0, 1.0);
    clip.xy = clip.xy * 0.5 + 0.5;
    minXY = min(minXY, clip.xy);
    maxXY = max(maxXY, clip.xy);
    minZ = clamp(min(minZ, clip.z), 0.0, 1.0);
  }
  const vec4 boxUvs = vec4(minXY, maxXY);
  const vec2 hzbSize = vec2(textureSize(hzb, 0));
  const float width = (boxUvs.z - boxUvs.x) * hzbSize.x;
  const float height = (boxUvs.w - boxUvs.y) * hzbSize.y;
  const float level = floor(log2(max(width, height)));
  const float[] depth = float[](
    textureLod(hzb, boxUvs.xy, level).x,
    textureLod(hzb, boxUvs.zy, level).x,
    textureLod(hzb, boxUvs.xw, level).x,
    textureLod(hzb, boxUvs.zw, level).x);
  const float maxHZB = max(max(max(depth[0], depth[1]), depth[2]), depth[3]);
  return minZ >= maxHZB;
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
  return viewId == 0 || !IsMeshletOccluded(aabbMin, aabbMax, transform);
}

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

void main()
{
  const uint viewId = gl_WorkGroupID.y;
  const uint meshletBaseId = gl_WorkGroupID.x * MESHLET_PER_WG;
  const uint meshletOffset = gl_LocalInvocationID.x / MAX_PRIMITIVES;
  const uint localId = gl_LocalInvocationID.x % MAX_PRIMITIVES;
  const uint meshletId = meshletBaseId + meshletOffset;
  const bool isMeshletValid = meshletId < meshletCount && IsMeshletVisible(meshletId, viewId);

  if (isMeshletValid && localId == 0)
  {
    // IMPORTANT: comment out this line when profiling!
    DebugDrawMeshletAabb(meshletId);

    baseIndex[meshletOffset] = atomicAdd(commands[viewId].indexCount, meshlets[meshletId].primitiveCount * 3);
    primitiveCount[meshletOffset] = meshlets[meshletId].primitiveCount;
  }
  barrier();

  if (isMeshletValid && localId < primitiveCount[meshletOffset])
  {
    const uint baseId = localId * 3;
    const uint indexOffset = baseIndex[meshletOffset] + baseId;
    indexBuffer.data[maxIndices * viewId + indexOffset + 0] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((baseId + 0) & MESHLET_PRIMITIVE_MASK);
    indexBuffer.data[maxIndices * viewId + indexOffset + 1] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((baseId + 1) & MESHLET_PRIMITIVE_MASK);
    indexBuffer.data[maxIndices * viewId + indexOffset + 2] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((baseId + 2) & MESHLET_PRIMITIVE_MASK);
  }
}