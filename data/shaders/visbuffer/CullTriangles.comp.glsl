#version 460 core
#extension GL_GOOGLE_include_directive : enable

#include "VisbufferCommon.h.glsl"
#include "../Math.h.glsl"
#include "../debug/DebugCommon.h.glsl"
#include "../shadows/vsm/VsmCommon.h.glsl"

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

// Taken from:
// https://github.com/GPUOpen-Effects/GeometryFX/blob/master/amd_geometryfx/src/Shaders/AMD_GeometryFX_Filtering.hlsl
// Parameters: vertices in UV space, viewport extent
bool CullSmallPrimitive(vec2 vertices[3], vec2 viewportExtent)
{
  const uint SUBPIXEL_BITS = 8;
  const uint SUBPIXEL_MASK = 0xFF;
  const uint SUBPIXEL_SAMPLES = 1 << SUBPIXEL_BITS;
  /**
  Computing this in float-point is not precise enough
  We switch to a 23.8 representation here which should match the
  HW subpixel resolution.
  We use a 8-bit wide guard-band to avoid clipping. If
  a triangle is outside the guard-band, it will be ignored.

  That is, the actual viewport supported here is 31 bit, one bit is
  unused, and the guard band is 1 << 23 bit large (8388608 pixels)
  */

  ivec2 minBB = ivec2(1 << 30, 1 << 30);
  ivec2 maxBB = ivec2(-(1 << 30), -(1 << 30));

  for (uint i = 0; i < 3; ++i)
  {
    vec2 screenSpacePositionFP = vertices[i].xy * viewportExtent;
    // Check if we would overflow after conversion
    if ( screenSpacePositionFP.x < -(1 << 23)
      || screenSpacePositionFP.x >  (1 << 23)
      || screenSpacePositionFP.y < -(1 << 23)
      || screenSpacePositionFP.y >  (1 << 23))
    {
      return true;
    }

    ivec2 screenSpacePosition = ivec2(screenSpacePositionFP * SUBPIXEL_SAMPLES);
    minBB = min(screenSpacePosition, minBB);
    maxBB = max(screenSpacePosition, maxBB);
  }

  /**
  Test is:

  Is the minimum of the bounding box right or above the sample
  point and is the width less than the pixel width in samples in
  one direction.

  This will also cull very long triangles which fall between
  multiple samples.
  */
  return !(
      (
          ((minBB.x & SUBPIXEL_MASK) > SUBPIXEL_SAMPLES/2)
      &&  ((maxBB.x - ((minBB.x & ~SUBPIXEL_MASK) + SUBPIXEL_SAMPLES/2)) < (SUBPIXEL_SAMPLES - 1)))
  || (
          ((minBB.y & SUBPIXEL_MASK) > SUBPIXEL_SAMPLES/2)
      &&  ((maxBB.y - ((minBB.y & ~SUBPIXEL_MASK) + SUBPIXEL_SAMPLES/2)) < (SUBPIXEL_SAMPLES - 1))));
}

// Returns true if the triangle is visible
// https://www.slideshare.net/gwihlidal/optimizing-the-graphics-pipeline-with-compute-gdc-2016
bool CullTriangle(Meshlet meshlet, uint localId)
{
  // Skip if no culling flags are enabled
  if ((perFrameUniforms.flags & (CULL_PRIMITIVE_BACKFACE | CULL_PRIMITIVE_FRUSTUM | CULL_PRIMITIVE_SMALL | CULL_PRIMITIVE_VSM)) == 0)
  {
    return true;
  }

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
  if ((perFrameUniforms.flags & CULL_PRIMITIVE_BACKFACE) != 0)
  {
    const float det = determinant(mat3(posClip0.xyw, posClip1.xyw, posClip2.xyw));
    if (det <= 0)
    {
      return false;
    }
  }

  const vec3 posNdc0 = posClip0.xyz / posClip0.w;
  const vec3 posNdc1 = posClip1.xyz / posClip1.w;
  const vec3 posNdc2 = posClip2.xyz / posClip2.w;
  
  const vec2 bboxNdcMin = min(posNdc0.xy, min(posNdc1.xy, posNdc2.xy));
  const vec2 bboxNdcMax = max(posNdc0.xy, max(posNdc1.xy, posNdc2.xy));

  const bool allBehind = posNdc0.z < 0 && posNdc1.z < 0 && posNdc2.z < 0;
  if (allBehind)
  {
    return false;
  }
  
  const bool anyBehind = posNdc0.z < 0 || posNdc1.z < 0 || posNdc2.z < 0;
  if (anyBehind)
  {
    return true;
  }

  // Frustum culling
  if ((perFrameUniforms.flags & CULL_PRIMITIVE_FRUSTUM) != 0)
  {
    if (!RectIntersectRect(bboxNdcMin, bboxNdcMax, vec2(-1.0), vec2(1.0)))
    {
      return false;
    }
  }
  
  // if (currentView.type == VIEW_TYPE_MAIN)
  // {
  //   DebugRect rect;
  //   rect.minOffset = Vec2ToPacked(bboxNdcMin * 0.5 + 0.5);
  //   rect.maxOffset = Vec2ToPacked(bboxNdcMax * 0.5 + 0.5);
  //   const float GOLDEN_CONJ = 0.6180339887498948482045868343656;
  //   vec4 color = vec4(2.0 * hsv_to_rgb(vec3(float(primitiveId) * GOLDEN_CONJ, 0.875, 0.85)), 1.0);
  //   rect.color = Vec4ToPacked(color);
  //   rect.depth = posNdc0.z;
  //   TryPushDebugRect(rect);
  // }

  // Small primitive culling
  if ((perFrameUniforms.flags & CULL_PRIMITIVE_SMALL) != 0)
  {
    const vec2 posUv0 = posNdc0.xy * 0.5 + 0.5;
    const vec2 posUv1 = posNdc1.xy * 0.5 + 0.5;
    const vec2 posUv2 = posNdc2.xy * 0.5 + 0.5;
    if (!CullSmallPrimitive(vec2[3](posUv0, posUv1, posUv2), currentView.viewport.zw))
    {
      return false;
    }
  }
  
  if ((perFrameUniforms.flags & CULL_PRIMITIVE_VSM) != 0)
  {
    if (currentView.type == VIEW_TYPE_VIRTUAL)
    {
      if (!CullQuadVsm(bboxNdcMin * 0.5 + 0.5, bboxNdcMax * 0.5 + 0.5, currentView.virtualTableIndex))
      {
        return false;
      }
    }
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
    sh_mvp = currentView.viewProj * transforms[meshlet.instanceId].modelCurrent;
  }

  barrier();

  bool primitivePassed = false;
  uint activePrimitiveId = 0;
  if (localId < meshlet.primitiveCount)
  {
    primitivePassed = CullTriangle(meshlet, localId);
    if (primitivePassed)
    {
      activePrimitiveId = atomicAdd(sh_primitivesPassed, 1);
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
    const uint indexOffset = sh_baseIndex + activePrimitiveId * 3;
    indexBuffer.data[indexOffset + 0] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((primitiveId + 0) & MESHLET_PRIMITIVE_MASK);
    indexBuffer.data[indexOffset + 1] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((primitiveId + 1) & MESHLET_PRIMITIVE_MASK);
    indexBuffer.data[indexOffset + 2] = (meshletId << MESHLET_PRIMITIVE_BITS) | ((primitiveId + 2) & MESHLET_PRIMITIVE_MASK);
  }
}