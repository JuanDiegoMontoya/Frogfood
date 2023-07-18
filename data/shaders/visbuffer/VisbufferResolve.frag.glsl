#version 460 core
#extension GL_GOOGLE_include_directive : enable

#define M_GOLDEN_CONJ 0.6180339887498948482045868343656
#include "Common.h.glsl"

layout (early_fragment_tests) in;

layout (location = 0) in vec2 i_uv;
layout (location = 1) in flat uint i_materialId;

layout (location = 0) out vec4 o_albedo;
layout (location = 1) out vec3 o_metallicRoughnessAo;
layout (location = 2) out vec3 o_normal;
layout (location = 3) out vec3 o_emission;
layout (location = 4) out vec2 o_motion;

layout (location = 0) uniform sampler2D s_baseColor;
layout (location = 1) uniform sampler2D s_metallicRoughness;
layout (location = 2) uniform sampler2D s_normal;
layout (location = 3) uniform sampler2D s_occlusion;
layout (location = 4) uniform sampler2D s_emission;

layout (r32ui, binding = 0) uniform restrict readonly uimage2D visbuffer;

struct PartialDerivatives
{
  vec3 lambda; // Barycentric coord for those whomst be wonderin'
  vec3 ddx;
  vec3 ddy;
};

struct UvGradient
{
  vec2 uv;
  vec2 ddx;
  vec2 ddy;
};

PartialDerivatives ComputeDerivatives(in vec4[3] clip, in vec2 ndcUv, in vec2 resolution)
{
  PartialDerivatives result;
  const vec3 invW = 1.0 / vec3(clip[0].w, clip[1].w, clip[2].w);
  const vec2 ndc0 = clip[0].xy * invW[0];
  const vec2 ndc1 = clip[1].xy * invW[1];
  const vec2 ndc2 = clip[2].xy * invW[2];

  const float invDet = 1.0 / determinant(mat2(ndc2 - ndc1, ndc0 - ndc1));
  result.ddx = vec3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
  result.ddy = vec3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;

  float ddxSum = dot(result.ddx, vec3(1.0));
  float ddySum = dot(result.ddy, vec3(1.0));

  const vec2 deltaV = ndcUv - ndc0;
  const float interpInvW = invW.x + deltaV.x * ddxSum + deltaV.y * ddySum;
  const float interpW = 1.0 / interpInvW;

  result.lambda = vec3(
    interpW * (deltaV.x * result.ddx.x + deltaV.y * result.ddy.x + invW.x),
    interpW * (deltaV.x * result.ddx.y + deltaV.y * result.ddy.y),
    interpW * (deltaV.x * result.ddx.z + deltaV.y * result.ddy.z)
  );

  result.ddx *= 2.0 / resolution.x;
  result.ddy *= 2.0 / resolution.y;
  ddxSum *= 2.0 / resolution.x;
  ddySum *= 2.0 / resolution.y;

  const float interpDdxW = 1.0 / (interpInvW + ddxSum);
  const float interpDdyW = 1.0 / (interpInvW + ddySum);

  result.ddx = interpDdxW * (result.lambda * interpInvW + result.ddx) - result.lambda;
  result.ddy = interpDdyW * (result.lambda * interpInvW + result.ddy) - result.lambda;
  return result;
}

vec3 Interpolate(in PartialDerivatives derivatives, in float[3] values)
{
  const vec3 v = vec3(values[0], values[1], values[2]);
  return vec3(
    dot(v, derivatives.lambda),
    dot(v, derivatives.ddx),
    dot(v, derivatives.ddy)
  );
}

vec3 HsvToRgb(in vec3 hsv)
{
  const vec3 rgb = clamp(abs(mod(hsv.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
  return hsv.z * mix(vec3(1.0), rgb, hsv.y);
}

uint[3] VisbufferLoadIndexIds(in Meshlet meshlet, in uint primitiveId)
{
  const uint vertexOffset = meshlet.vertexOffset;
  const uint indexOffset = meshlet.indexOffset;
  const uint primitiveOffset = meshlet.primitiveOffset;
  const uint[] primitiveIds = uint[](
    primitives[primitiveOffset + primitiveId * 3 + 0],
    primitives[primitiveOffset + primitiveId * 3 + 1],
    primitives[primitiveOffset + primitiveId * 3 + 2]
  );
  return uint[3](
    indices[indexOffset + primitiveIds[0]],
    indices[indexOffset + primitiveIds[1]],
    indices[indexOffset + primitiveIds[2]]
  );
}

vec3[3] VisbufferLoadPosition(in uint[3] indexIds, in uint vertexOffset)
{
  return vec3[3](
    PackedToVec3(vertices[vertexOffset + indexIds[0]].position),
    PackedToVec3(vertices[vertexOffset + indexIds[1]].position),
    PackedToVec3(vertices[vertexOffset + indexIds[2]].position)
  );
}

vec2[3] VisbufferLoadUv(in uint[3] indexIds, in uint vertexOffset)
{
  return vec2[3](
    PackedToVec2(vertices[vertexOffset + indexIds[0]].uv),
    PackedToVec2(vertices[vertexOffset + indexIds[1]].uv),
    PackedToVec2(vertices[vertexOffset + indexIds[2]].uv)
  );
}

vec3[3] VisbufferLoadNormal(in uint[3] indexIds, in uint vertexOffset)
{
  return vec3[3](
    OctToFloat32x3(unpackSnorm2x16(vertices[vertexOffset + indexIds[0]].normal)),
    OctToFloat32x3(unpackSnorm2x16(vertices[vertexOffset + indexIds[1]].normal)),
    OctToFloat32x3(unpackSnorm2x16(vertices[vertexOffset + indexIds[2]].normal))
  );
}

UvGradient MakeUvGradient(in PartialDerivatives derivatives, in vec2[3] uvs)
{
  const vec3[] interpUvs = vec3[](
    Interpolate(derivatives, float[](uvs[0].x, uvs[1].x, uvs[2].x)),
    Interpolate(derivatives, float[](uvs[0].y, uvs[1].y, uvs[2].y))
  );

  return UvGradient(
    vec2(interpUvs[0].x, interpUvs[1].x),
    vec2(interpUvs[0].y, interpUvs[1].y),
    vec2(interpUvs[0].z, interpUvs[1].z)
  );
}

vec3 MakeSmoothNormal(in PartialDerivatives derivatives, in vec3[3] rawNormal)
{
  return vec3(
    Interpolate(derivatives, float[3](rawNormal[0].x, rawNormal[1].x, rawNormal[2].x)).x,
    Interpolate(derivatives, float[3](rawNormal[0].y, rawNormal[1].y, rawNormal[2].y)).x,
    Interpolate(derivatives, float[3](rawNormal[0].z, rawNormal[1].z, rawNormal[2].z)).x
  );
}

vec4 InterpolateVec4(in PartialDerivatives derivatives, in vec4[3] vec4Data)
{
  return vec4(
    Interpolate(derivatives, float[3](vec4Data[0].x, vec4Data[1].x, vec4Data[2].x)).x,
    Interpolate(derivatives, float[3](vec4Data[0].y, vec4Data[1].y, vec4Data[2].y)).x,
    Interpolate(derivatives, float[3](vec4Data[0].z, vec4Data[1].z, vec4Data[2].z)).x,
    Interpolate(derivatives, float[3](vec4Data[0].w, vec4Data[1].w, vec4Data[2].w)).x
  );
}

vec2 MakeSmoothMotion(in PartialDerivatives derivatives, vec4[3] worldPosition)
{
  // Probably not the most efficient way to do this, but this is a port of a shader that is known to work
  vec4[3] v_curPos = vec4[](
    viewProjUnjittered * worldPosition[0],
    viewProjUnjittered * worldPosition[1],
    viewProjUnjittered * worldPosition[2]
  );
  
  vec4[3] v_oldPos = vec4[](
    oldViewProjUnjittered * worldPosition[0],
    oldViewProjUnjittered * worldPosition[1],
    oldViewProjUnjittered * worldPosition[2]
  );

  vec4 smoothCurPos = InterpolateVec4(derivatives, v_curPos);
  vec4 smoothOldPos = InterpolateVec4(derivatives, v_oldPos);
  return ((smoothOldPos.xy / smoothOldPos.w) - (smoothCurPos.xy / smoothCurPos.w)) * 0.5;
}

vec4 SampleBaseColor(in GpuMaterial material, in UvGradient uvGrad)
{
  if (!bool(material.flags & MATERIAL_HAS_BASE_COLOR))
  {
    return material.baseColorFactor.rgba;
  }
  return
    material.baseColorFactor.rgba *
    textureGrad(s_baseColor, uvGrad.uv, uvGrad.ddx, uvGrad.ddy).rgba;
}

vec3 SampleNormal(in GpuMaterial material, in UvGradient uvGrad, vec3 faceNormal, mat3 tbn)
{
  if (!bool(material.flags & MATERIAL_HAS_NORMAL))
  {
    return faceNormal;
  }
  vec3 tangentNormal = textureGrad(s_baseColor, uvGrad.uv, uvGrad.ddx, uvGrad.ddy).rgb * 2.0 - 1.0;
  return tbn * faceNormal;
}

vec2 SampleMetallicRoughness(in GpuMaterial material, in UvGradient uvGrad)
{
  vec2 metallicRoughnessFactor = vec2(material.metallicFactor, material.roughnessFactor);
  if (!bool(material.flags & MATERIAL_HAS_METALLIC_ROUGHNESS))
  {
    return metallicRoughnessFactor;
  }
  return
    metallicRoughnessFactor *
    textureGrad(s_metallicRoughness, uvGrad.uv, uvGrad.ddx, uvGrad.ddy).rg;
}

float SampleOcclusion(in GpuMaterial material, in UvGradient uvGrad)
{
  if (!bool(material.flags & MATERIAL_HAS_OCCLUSION))
  {
    return 1.0;
  }
  return textureGrad(s_occlusion, uvGrad.uv, uvGrad.ddx, uvGrad.ddy).r;
}

vec3 SampleEmission(in GpuMaterial material, in UvGradient uvGrad)
{
  if (!bool(material.flags & MATERIAL_HAS_EMISSION))
  {
    return material.emissiveFactor * material.emissiveStrength;
  }
  return
    material.emissiveFactor * material.emissiveStrength *
    textureGrad(s_emission, uvGrad.uv, uvGrad.ddx, uvGrad.ddy).rgb;
}

void main()
{
  const ivec2 position = ivec2(gl_FragCoord.xy);
  const uint payload = imageLoad(visbuffer, position).x;
  const uint meshletId = (payload >> MESHLET_PRIMITIVE_BITS) & MESHLET_ID_MASK;
  const uint primitiveId = payload & MESHLET_PRIMITIVE_MASK;
  const Meshlet meshlet = meshlets[meshletId];
  const GpuMaterial material = materials[meshlet.materialId];
  const mat4 transform = transforms[meshlet.instanceId];

  const vec2 resolution = vec2(imageSize(visbuffer));
  const uint[] indexIDs = VisbufferLoadIndexIds(meshlet, primitiveId);
  const vec3[] rawPosition = VisbufferLoadPosition(indexIDs, meshlet.vertexOffset);
  const vec2[] rawUv = VisbufferLoadUv(indexIDs, meshlet.vertexOffset);
  const vec3[] rawNormal = VisbufferLoadNormal(indexIDs, meshlet.vertexOffset);
  const vec4[] worldPosition = vec4[](
    transform * vec4(rawPosition[0], 1.0),
    transform * vec4(rawPosition[1], 1.0),
    transform * vec4(rawPosition[2], 1.0)
  );
  const vec4[] clipPosition = vec4[](
    viewProj * worldPosition[0],
    viewProj * worldPosition[1],
    viewProj * worldPosition[2]
  );
  const PartialDerivatives partialDerivatives = ComputeDerivatives(clipPosition, i_uv * 2.0 - 1.0, resolution);
  const UvGradient uvGrad = MakeUvGradient(partialDerivatives, rawUv);
  //const vec3 normal = normalize(cross(rawPosition[1] - rawPosition[0], rawPosition[2] - rawPosition[0]));

  o_albedo = vec4(SampleBaseColor(material, uvGrad).rgb, 1.0);
  o_metallicRoughnessAo = vec3(
    SampleMetallicRoughness(material, uvGrad),
    SampleOcclusion(material, uvGrad));
  o_normal = normalize(MakeSmoothNormal(partialDerivatives, rawNormal));
  o_emission = SampleEmission(material, uvGrad);
  o_motion = MakeSmoothMotion(partialDerivatives, worldPosition);
}
