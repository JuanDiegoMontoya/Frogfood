#include "VisbufferCommon.h.glsl"
#include "../hzb/HZBCommon.h.glsl"

#include "../Utility.h.glsl" // Debug

layout(early_fragment_tests) in;

layout(location = 0) in vec2 i_uv;

layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec3 o_metallicRoughnessAo;
layout(location = 2) out vec4 o_normalAndFaceNormal;
layout(location = 3) out vec2 o_smoothVertexNormal;
layout(location = 4) out vec3 o_emission;
layout(location = 5) out vec2 o_motion;

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

uint[3] VisbufferLoadIndexIds(in Meshlet meshlet, in uint primitiveId)
{
  const uint vertexOffset = meshlet.vertexOffset;
  const uint indexOffset = meshlet.indexOffset;
  const uint primitiveOffset = meshlet.primitiveOffset;
  const uint[] primitiveIds = uint[](
    uint(d_primitives[primitiveOffset + primitiveId * 3 + 0]),
    uint(d_primitives[primitiveOffset + primitiveId * 3 + 1]),
    uint(d_primitives[primitiveOffset + primitiveId * 3 + 2])
  );
  return uint[3](
    d_indices[indexOffset + primitiveIds[0]],
    d_indices[indexOffset + primitiveIds[1]],
    d_indices[indexOffset + primitiveIds[2]]
  );
}

vec3[3] VisbufferLoadPosition(in uint[3] indexIds, in uint vertexOffset)
{
  return vec3[3](
    PackedToVec3(d_vertices[vertexOffset + indexIds[0]].position),
    PackedToVec3(d_vertices[vertexOffset + indexIds[1]].position),
    PackedToVec3(d_vertices[vertexOffset + indexIds[2]].position)
  );
}

vec2[3] VisbufferLoadUv(in uint[3] indexIds, in uint vertexOffset)
{
  return vec2[3](
    PackedToVec2(d_vertices[vertexOffset + indexIds[0]].uv),
    PackedToVec2(d_vertices[vertexOffset + indexIds[1]].uv),
    PackedToVec2(d_vertices[vertexOffset + indexIds[2]].uv)
  );
}

vec3[3] VisbufferLoadNormal(in uint[3] indexIds, in uint vertexOffset)
{
  return vec3[3](
    OctToVec3(unpackSnorm2x16(d_vertices[vertexOffset + indexIds[0]].normal)),
    OctToVec3(unpackSnorm2x16(d_vertices[vertexOffset + indexIds[1]].normal)),
    OctToVec3(unpackSnorm2x16(d_vertices[vertexOffset + indexIds[2]].normal))
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

vec3 InterpolateVec3(in PartialDerivatives derivatives, in vec3[3] vec3Data)
{
  return vec3(
    Interpolate(derivatives, float[3](vec3Data[0].x, vec3Data[1].x, vec3Data[2].x)).x,
    Interpolate(derivatives, float[3](vec3Data[0].y, vec3Data[1].y, vec3Data[2].y)).x,
    Interpolate(derivatives, float[3](vec3Data[0].z, vec3Data[1].z, vec3Data[2].z)).x
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

vec2 MakeSmoothMotion(in PartialDerivatives derivatives, vec4[3] worldPosition, vec4[3] worldPositionOld)
{
  // Probably not the most efficient way to do this, but this is a port of a shader that is known to work
  vec4[3] v_curPos = vec4[](
    d_perFrameUniforms.viewProjUnjittered * worldPosition[0],
    d_perFrameUniforms.viewProjUnjittered * worldPosition[1],
    d_perFrameUniforms.viewProjUnjittered * worldPosition[2]
  );
  
  vec4[3] v_oldPos = vec4[](
    d_perFrameUniforms.oldViewProjUnjittered * worldPositionOld[0],
    d_perFrameUniforms.oldViewProjUnjittered * worldPositionOld[1],
    d_perFrameUniforms.oldViewProjUnjittered * worldPositionOld[2]
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
    textureGrad(Fvog_sampler2D(material.baseColorTextureIndex, materialSamplerIndex), uvGrad.uv, uvGrad.ddx, uvGrad.ddy).rgba;
}

vec2 SampleMetallicRoughness(in GpuMaterial material, in UvGradient uvGrad)
{
  vec2 metallicRoughnessFactor = vec2(material.metallicFactor, material.roughnessFactor);
  if (!bool(material.flags & MATERIAL_HAS_METALLIC_ROUGHNESS))
  {
    return metallicRoughnessFactor;
  }
  // Metallic is stored in the B channel
  // Roughness is stored in the G channel
  return
    metallicRoughnessFactor *
    textureGrad(Fvog_sampler2D(material.metallicRoughnessTextureIndex, materialSamplerIndex), uvGrad.uv, uvGrad.ddx, uvGrad.ddy).bg;
}

float SampleOcclusion(in GpuMaterial material, in UvGradient uvGrad)
{
  if (!bool(material.flags & MATERIAL_HAS_OCCLUSION))
  {
    return 1.0;
  }
  return textureGrad(Fvog_sampler2D(material.occlusionTextureIndex, materialSamplerIndex), uvGrad.uv, uvGrad.ddx, uvGrad.ddy).r;
}

vec3 SampleEmission(in GpuMaterial material, in UvGradient uvGrad)
{
  if (!bool(material.flags & MATERIAL_HAS_EMISSION))
  {
    return material.emissiveFactor * material.emissiveStrength;
  }
  return
    material.emissiveFactor * material.emissiveStrength *
    textureGrad(Fvog_sampler2D(material.emissionTextureIndex, materialSamplerIndex), uvGrad.uv, uvGrad.ddx, uvGrad.ddy).rgb;
}

vec3 SampleNormal(in GpuMaterial material, in UvGradient uvGrad)
{
  if (!bool(material.flags & MATERIAL_HAS_NORMAL))
  {
    return vec3(0, 0, 1);
  }
  // We assume the normal is encoded with just X and Y components, since we can trivially reconstruct the third.
  // This allows compatibility with both RG and RGB tangent space normal maps.
  vec2 xy = textureGrad(Fvog_sampler2D(material.normalTextureIndex, materialSamplerIndex), uvGrad.uv, uvGrad.ddx, uvGrad.ddy).rg * 2.0 - 1.0;
  float z = sqrt(max(1.0 - xy.x * xy.x - xy.y * xy.y, 0.0));
  return vec3(xy * material.normalXyScale, z);
}

void main()
{
  const ivec2 position = ivec2(gl_FragCoord.xy);
  const uint payload = texelFetch(Fvog_utexture2D(visbufferIndex), position, 0).x;
  if (payload == ~0u)
  {
    discard;
  }
  const uint visibleMeshletId = (payload >> MESHLET_PRIMITIVE_BITS) & MESHLET_ID_MASK;
  const uint meshletInstanceId = d_visibleMeshlets.indices[visibleMeshletId];
  const uint primitiveId = payload & MESHLET_PRIMITIVE_MASK;
  const MeshletInstance meshletInstance = d_meshletInstances[meshletInstanceId];
  const Meshlet meshlet = d_meshlets[meshletInstance.meshletId];
  const GpuMaterial material = d_materials[d_transforms[meshletInstance.instanceId].materialId];
  const mat4 transform = d_transforms[meshletInstance.instanceId].modelCurrent;
  const mat4 transformPrevious = d_transforms[meshletInstance.instanceId].modelPrevious;

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
    d_perFrameUniforms.viewProj * worldPosition[0],
    d_perFrameUniforms.viewProj * worldPosition[1],
    d_perFrameUniforms.viewProj * worldPosition[2]
  );
  
  const vec4[] worldPositionPrevious = vec4[](
    transformPrevious * vec4(rawPosition[0], 1.0),
    transformPrevious * vec4(rawPosition[1], 1.0),
    transformPrevious * vec4(rawPosition[2], 1.0)
  );

  const vec2 resolution = vec2(textureSize(Fvog_utexture2D(visbufferIndex), 0));
  const PartialDerivatives partialDerivatives = ComputeDerivatives(clipPosition, i_uv * 2.0 - 1.0, resolution);
  const UvGradient uvGrad = MakeUvGradient(partialDerivatives, rawUv);
  const vec3 flatNormal = normalize(cross(rawPosition[1] - rawPosition[0], rawPosition[2] - rawPosition[0]));

  const vec3 smoothObjectNormal = normalize(InterpolateVec3(partialDerivatives, rawNormal));
  const mat3 normalMatrix = inverse(transpose(mat3(transform)));
  const vec3 smoothWorldNormal = normalize(normalMatrix * smoothObjectNormal);
  vec3 normal = smoothWorldNormal;

  // TODO: use view-space positions to maintain precision
  vec3 iwp[] = vec3[](
    Interpolate(partialDerivatives, float[3](worldPosition[0].x, worldPosition[1].x, worldPosition[2].x)),
    Interpolate(partialDerivatives, float[3](worldPosition[0].y, worldPosition[1].y, worldPosition[2].y)),
    Interpolate(partialDerivatives, float[3](worldPosition[0].z, worldPosition[1].z, worldPosition[2].z))
  );

  if (bool(material.flags & MATERIAL_HAS_NORMAL))
  {
    mat3 TBN = mat3(0.0);
    const vec3 ddx_position = vec3(iwp[0].y, iwp[1].y, iwp[2].y);
    const vec3 ddy_position = vec3(iwp[0].z, iwp[1].z, iwp[2].z);
    const vec2 ddx_uv = uvGrad.ddx;
    const vec2 ddy_uv = uvGrad.ddy;

    const vec3 N = normal;
    const vec3 T = normalize(ddx_position * ddy_uv.y - ddy_position * ddx_uv.y);
    const vec3 B = -normalize(cross(N, T));

    TBN = mat3(T, B, N);

    vec3 sampledNormal = normalize(SampleNormal(material, uvGrad));
    normal = normalize(TBN * sampledNormal);
  }

  o_albedo = vec4(SampleBaseColor(material, uvGrad).rgb, 1.0);
  o_metallicRoughnessAo = vec3(
    SampleMetallicRoughness(material, uvGrad),
    SampleOcclusion(material, uvGrad));
  o_normalAndFaceNormal.xy = Vec3ToOct(normal);
  o_normalAndFaceNormal.zw = Vec3ToOct(flatNormal);
  o_smoothVertexNormal = Vec3ToOct(smoothWorldNormal);
  o_emission = SampleEmission(material, uvGrad);
  o_motion = MakeSmoothMotion(partialDerivatives, worldPosition, worldPositionPrevious);
}
