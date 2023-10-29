#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "VisbufferCommon.h.glsl"
#include "../Math.h.glsl"
#include "../debug/DebugCommon.h.glsl"

layout(std430, binding = 9) restrict buffer MeshletVisbilityBuffer
{
  uint indices[];
} visibleMeshlets;

layout (std430, binding = 7) writeonly buffer MeshletPackedBuffer
{
  uint data[];
} indexBuffer;

shared uint sh_baseIndex;
shared uint sh_primitivesPassed;
shared mat4 sh_mvp;

// Returns true if the triangle is visible
// https://www.slideshare.net/gwihlidal/optimizing-the-graphics-pipeline-with-compute-gdc-2016
bool CullTriangle(Meshlet meshlet, uint localId)
{
  const uint primitiveId = localId * 3;

  // TODO: SoA vertices
  const uint vertexOffset = meshlet.vertexOffset;
  const uint indexOffset = meshlet.indexOffset;
  const uint primitiveOffset = meshlet.primitiveOffset;
  const uint primitive0 = uint(primitives[primitiveOffset + primitiveId + 0]);
  const uint primitive1 = uint(primitives[primitiveOffset + primitiveId + 1]);
  const uint primitive2 = uint(primitives[primitiveOffset + primitiveId + 2]);
  const uint index0 = indices[indexOffset + primitive0];
  const uint index1 = indices[indexOffset + primitive1];
  const uint index2 = indices[indexOffset + primitive2];
  const Vertex vertex0 = vertices[vertexOffset + index0];
  const Vertex vertex1 = vertices[vertexOffset + index1];
  const Vertex vertex2 = vertices[vertexOffset + index2];
  const vec3 position0 = PackedToVec3(vertex0.position);
  const vec3 position1 = PackedToVec3(vertex1.position);
  const vec3 position2 = PackedToVec3(vertex2.position);
  const vec4 posClip0 = sh_mvp * vec4(position0, 1.0);
  const vec4 posClip1 = sh_mvp * vec4(position1, 1.0);
  const vec4 posClip2 = sh_mvp * vec4(position2, 1.0);

  // Backfacing and zero-area culling
  // https://redirect.cs.umbc.edu/~olano/papers/2dh-tri/
  // This is equivalent to the HLSL code that was ported, except the mat3 is transposed.
  // However, the determinant of a matrix and its transpose are the same, so this is fine.
  const float det = determinant(mat3(posClip0.xyw, posClip1.xyw, posClip2.xyw));
  if (det <= 0)
  {
    return false;
  }

  const vec3 posNdc0 = posClip0.xyz / posClip0.w;
  const vec3 posNdc1 = posClip1.xyz / posClip1.w;
  const vec3 posNdc2 = posClip2.xyz / posClip2.w;
  
  const vec2 bboxNdcMin = min(posNdc0.xy, min(posNdc1.xy, posNdc2.xy));
  const vec2 bboxNdcMax = max(posNdc0.xy, max(posNdc1.xy, posNdc2.xy));

  // Frustum culling
  const bool allBehind = posNdc0.z < 0 && posNdc1.z < 0 && posNdc2.z < 0;
  if (allBehind)
  {
    return false;
  }

  const bool anyBehind = posNdc0.z < 0 || posNdc1.z < 0 || posNdc2.z < 0;
  if (!anyBehind && !RectIntersectRect(bboxNdcMin, bboxNdcMax, vec2(-1.0), vec2(1.0)))
  {
    return false;
  }
  
  if (currentView.type == VIEW_TYPE_MAIN)
  {
    DebugRect rect;
    rect.minOffset = Vec2ToPacked(bboxNdcMin * 0.5 + 0.5);
    rect.maxOffset = Vec2ToPacked(bboxNdcMax * 0.5 + 0.5);
    const float GOLDEN_CONJ = 0.6180339887498948482045868343656;
    vec4 color = vec4(2.0 * hsv_to_rgb(vec3(float(primitiveId) * GOLDEN_CONJ, 0.875, 0.85)), 1.0);
    rect.color = Vec4ToPacked(color);
    rect.depth = posNdc0.z;
    TryPushDebugRect(rect);
  }

  // We only care about the viewport's extent here
  const vec2 halfViewportExtent = currentView.viewport.zw * 0.5;

  // Small primitive culling
  // TODO: fix
  if (any(equal(round(bboxNdcMin * halfViewportExtent), round(bboxNdcMax * halfViewportExtent))))
  {
    return false;
  }

  return true;
}

layout(local_size_x = MAX_PRIMITIVES) in;
void main()
{
  const uint meshletId = visibleMeshlets.indices[gl_WorkGroupID.x];
  const Meshlet meshlet = meshlets[meshletId];
  const uint localId = gl_LocalInvocationID.x;
  const uint primitiveId = localId * 3;

  if (localId == 0)
  {
    sh_primitivesPassed = 0;
    sh_mvp = currentView.viewProj * transforms[meshlet.instanceId];
  }

  barrier();

  bool primitivePassed = false;
  uint frfrPrimitiveId = 0;
  if (localId < meshlet.primitiveCount)
  {
    primitivePassed = CullTriangle(meshlet, localId);
    if (primitivePassed)
    {
      frfrPrimitiveId = atomicAdd(sh_primitivesPassed, 1);
    }
  }
  
  barrier();

  if (localId == 0)
  {
    sh_baseIndex = atomicAdd(indirectCommand.indexCount, sh_primitivesPassed * 3);
  }

  barrier();

  if (primitivePassed)
  {
    const uint indexOffset = sh_baseIndex + frfrPrimitiveId * 3;
    indexBuffer.data[indexOffset + 0] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((primitiveId + 0) & MESHLET_PRIMITIVE_MASK);
    indexBuffer.data[indexOffset + 1] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((primitiveId + 1) & MESHLET_PRIMITIVE_MASK);
    indexBuffer.data[indexOffset + 2] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((primitiveId + 2) & MESHLET_PRIMITIVE_MASK);
  }
}