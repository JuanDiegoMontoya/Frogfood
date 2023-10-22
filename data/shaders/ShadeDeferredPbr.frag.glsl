#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "Config.shared.h"
#include "GlobalUniforms.h.glsl"
#include "Math.h.glsl"
#include "Pbr.h.glsl"
#include "shadows/vsm/VsmCommon.h.glsl"
#include "Utility.h.glsl"

layout(binding = 0) uniform sampler2D s_gAlbedo;
layout(binding = 1) uniform sampler2D s_gNormal;
layout(binding = 2) uniform sampler2D s_gDepth;
layout(binding = 3) uniform sampler2D s_rsmIndirect;
layout(binding = 4) uniform sampler2D s_rsmDepth;
layout(binding = 5) uniform sampler2DShadow s_rsmDepthShadow;
layout(binding = 6) uniform sampler2D s_emission;
layout(binding = 7) uniform sampler2D s_metallicRoughnessAo;

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec3 o_color;

#define VSM_SHOW_CLIPMAP_ID    (1 << 0)
#define VSM_SHOW_PAGE_ADDRESS  (1 << 1)
#define VSM_SHOW_PAGE_OUTLINES (1 << 2)
#define VSM_SHOW_SHADOW_DEPTH  (1 << 3)
#define VSM_SHOW_DIRTY_PAGES   (1 << 4)

layout(binding = 1, std140) uniform ShadingUniforms
{
  vec4 sunDir;
  vec4 sunStrength;
  vec2 random;
  uint numberOfLights;
  uint debugFlags;
}shadingUniforms;

layout(binding = 2, std140) uniform ShadowUniforms
{
  uint shadowMode; // 0 = PCF, 1 = SMRT

  // PCF
  uint pcfSamples;
  float pcfRadius;

  // SMRT
  uint shadowRays;
  uint stepsPerRay;
  float rayStepSize;
  float heightmapThickness;
  float sourceAngleRad;
}shadowUniforms;

layout(binding = 6, std430) readonly buffer LightBuffer
{
  GpuLight lights[];
}lightBuffer;

// Returns a shadow bias in the space of whatever texelWidth is in.
// For example, if texelWidth is 0.125 units in world space, the bias will be in world space too.
float GetShadowBias(vec3 N, vec3 L, float texelWidth)
{
  const float quantize = 2.0 / (1 << 23);
  const float b = texelWidth / 2.0;
  const float NoL = clamp(dot(N, L), 0.0, 1.0);
  return quantize + b * length(cross(L, N)) / NoL;
}

struct ShadowVsmOut
{
  float shadow;
  uint pageData;
  vec2 vsmUv;
  float shadowDepth;
  float projectedDepth;
  uint clipmapLevel;
};

ShadowVsmOut ShadowVsm(vec3 fragWorldPos, vec3 normal)
{
  ShadowVsmOut ret;
  ret.pageData = 0;
  ret.vsmUv = vec2(0);
  ret.shadowDepth = 0;
  ret.projectedDepth = 0;

  const ivec2 gid = ivec2(gl_FragCoord.xy);
  const float depthSample = texelFetch(s_gDepth, gid, 0).x;
  PageAddressInfo addr = GetClipmapPageFromDepth(depthSample, gid, textureSize(s_gDepth, 0));

  ret.vsmUv = addr.pageUv;
  ret.projectedDepth = addr.projectedDepth;
  ret.clipmapLevel = addr.clipmapLevel;

  ret.pageData = imageLoad(i_pageTables, addr.pageAddress).x;
  if (!GetIsPageBacked(ret.pageData))
  {
    ret.shadow = 1.0;
    return ret;
  }
  
  const ivec2 pageTexel = ivec2(addr.pageUv * PAGE_SIZE);
  const uint physicalAddress = GetPagePhysicalAddress(ret.pageData);
  ret.shadowDepth = LoadPageTexel(pageTexel, physicalAddress);

  const float magicClipmapLevelBias = 0.1;
  const float magicConstantBias = 2.0 / (1 << 23);
  const float halfOrthoFrustumLength = clipmapUniforms.projectionZLength / 2;
  const float shadowTexelSize = exp2(addr.clipmapLevel + (addr.clipmapLevel * magicClipmapLevelBias)) * clipmapUniforms.firstClipmapTexelLength;
  const float bias = magicConstantBias + GetShadowBias(normal, -shadingUniforms.sunDir.xyz, shadowTexelSize) / halfOrthoFrustumLength;
  if (ret.shadowDepth + bias < ret.projectedDepth)
  {
    ret.shadow = 0.0;
    return ret;
  }

  ret.shadow = 1.0;
  return ret;
}

float ShadowPCF(vec2 uv, float viewDepth, float bias)
{
  float lightOcclusion = 0.0;

  for (uint i = 0; i < shadowUniforms.pcfSamples; i++)
  {
    vec2 xi = fract(Hammersley(i, shadowUniforms.pcfSamples) + hash(gl_FragCoord.xy) + shadingUniforms.random);
    float r = sqrt(xi.x);
    float theta = xi.y * 2.0 * 3.14159;
    vec2 offset = shadowUniforms.pcfRadius * vec2(r * cos(theta), r * sin(theta));
    // float lightDepth = textureLod(s_rsmDepth, uv + offset, 0).x;
    // lightDepth += bias;
    // if (lightDepth >= viewDepth)
    // {
    //   lightOcclusion += 1.0;
    // }
    lightOcclusion += textureLod(s_rsmDepthShadow, vec3(uv + offset, viewDepth - bias), 0);
  }

  return lightOcclusion / shadowUniforms.pcfSamples;
}

// Marches a ray in view space until it collides with the height field defined by the shadow map.
// We assume the height field has a certain thickness so rays can pass behind it
float MarchShadowRay(vec3 rayLightViewPos, vec3 rayLightViewDir, float bias, mat4 lightProj, mat4 lightInvProj)
{
  for (int stepIdx = 0; stepIdx < shadowUniforms.stepsPerRay; stepIdx++)
  {
    rayLightViewPos += rayLightViewDir * shadowUniforms.rayStepSize;

    vec4 rayLightClipPos = lightProj * vec4(rayLightViewPos, 1.0);
    rayLightClipPos.xy /= rayLightClipPos.w; // to NDC
    rayLightClipPos.xy = rayLightClipPos.xy * 0.5 + 0.5; // to UV
    float shadowMapWindowZ = /*bias*/ + textureLod(s_rsmDepth, rayLightClipPos.xy, 0.0).x;
    // Note: view Z gets *smaller* as we go deeper into the frusum (farther from the camera)
    float shadowMapViewZ = UnprojectUV_ZO(shadowMapWindowZ, rayLightClipPos.xy, lightInvProj).z;

    // Positive dDepth: tested position is below the shadow map
    // Negative dDepth: tested position is above
    float dDepth = shadowMapViewZ - rayLightViewPos.z;

    // Ray is under the shadow map height field
    if (dDepth > 0)
    {
      // Ray intersected some geometry
      // OR
      // The ray hasn't collided with anything on the last step (we're already under the height field, assume infinite thickness so there is at least some shadow)
      if (dDepth < shadowUniforms.heightmapThickness || stepIdx == shadowUniforms.stepsPerRay - 1)
      {
        return 0.0;
      }
    }
  }

  return 1.0;
}

float ShadowRayTraced(vec3 fragWorldPos, vec3 lightDir, float bias, mat4 lightView, mat4 lightProj, mat4 lightInvProj)
{
  float lightOcclusion = 0.0;

  for (int rayIdx = 0; rayIdx < shadowUniforms.shadowRays; rayIdx++)
  {
    vec2 xi = Hammersley(rayIdx, shadowUniforms.shadowRays);
    xi = fract(xi + hash(gl_FragCoord.xy) + shadingUniforms.random);
    vec3 newLightDir = RandVecInCone(xi, lightDir, shadowUniforms.sourceAngleRad);

    vec3 rayLightViewDir = (lightView * vec4(newLightDir, 0.0)).xyz;
    vec3 rayLightViewPos = (lightView * vec4(fragWorldPos, 1.0)).xyz;

    lightOcclusion += MarchShadowRay(rayLightViewPos, rayLightViewDir, bias, lightProj, lightInvProj);
  }

  return lightOcclusion / shadowUniforms.shadowRays;
}

float Shadow(vec3 fragWorldPos, vec3 normal, vec3 lightDir, mat4 lightViewProj)
{
  vec4 clip = lightViewProj * vec4(fragWorldPos, 1.0);
  vec2 uv = clip.xy * .5 + .5;
  if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
  {
    return 0;
  }

  // Analytically compute slope-scaled bias
  const float maxBias = 0.0008;
  float bias = maxBias;
  //float bias = GetShadowBias(normal, -shadingUniforms.sunDir.xyz, textureSize(s_rsmDepthShadow, 0));
  //bias = min(bias, maxBias);

  switch (shadowUniforms.shadowMode)
  {
    //case 0: return ShadowPCF(uv, clip.z * .5 + .5, bias);
    //case 1: return ShadowRayTraced(fragWorldPos, lightDir, bias);
    default: return 1.0;
  }
}

vec3 LocalLightIntensity(vec3 viewDir, Surface surface)
{
  vec3 color = { 0, 0, 0 };

  for (uint i = 0; i < shadingUniforms.numberOfLights; i++)
  {
    GpuLight light = lightBuffer.lights[i];

    color += EvaluatePunctualLight(viewDir, light, surface);
  }

  return color;
}

void main()
{
  vec3 albedo = textureLod(s_gAlbedo, v_uv, 0.0).rgb;
  vec3 normal = textureLod(s_gNormal, v_uv, 0.0).xyz;
  float depth = textureLod(s_gDepth, v_uv, 0.0).x;
  vec3 emission = textureLod(s_emission, v_uv, 0.0).rgb;
  vec3 metallicRoughnessAo = textureLod(s_metallicRoughnessAo, v_uv, 0.0).rgb;

  if (depth == FAR_DEPTH)
  {
    discard;
  }

  vec3 fragWorldPos = UnprojectUV_ZO(depth, v_uv, perFrameUniforms.invViewProj);
  
  vec3 incidentDir = -shadingUniforms.sunDir.xyz;
  float cosTheta = max(0.0, dot(incidentDir, normal));
  vec3 diffuse = albedo * cosTheta * shadingUniforms.sunStrength.rgb;

  //float shadow = Shadow(fragWorldPos, normal, -shadingUniforms.sunDir.xyz);
  ShadowVsmOut shadowVsm = ShadowVsm(fragWorldPos, normal);
  float shadowSun = shadowVsm.shadow;
  //shadowSun = 0;
  
  vec3 viewDir = normalize(perFrameUniforms.cameraPos.xyz - fragWorldPos);
  
  Surface surface;
  surface.albedo = albedo;
  surface.normal = normal;
  surface.position = fragWorldPos;
  surface.metallic = metallicRoughnessAo.x;
  surface.perceptualRoughness = metallicRoughnessAo.y;
  // Common materials have an IOR of 1.5 which works out to a reflectance of 0.5 in the following equation:
  // f0 = 0.16 * reflectance^2 = ((IOR - 1) / (IOR + 1)) ^ 2
  // (Reminder: this is an artist-friendly mapping for many common materials' physical reflectances to fit within [0, 1])
  surface.reflectance = 0.5;
  surface.f90 = 1.0;

  vec3 finalColor = vec3(.03) * albedo * metallicRoughnessAo.z; // Ambient lighting

  float NoL_sun = clamp(dot(normal, -shadingUniforms.sunDir.xyz), 0.0, 1.0);
  finalColor += BRDF(viewDir, -shadingUniforms.sunDir.xyz, surface) * shadingUniforms.sunStrength.rgb * NoL_sun * shadowSun;
  finalColor += LocalLightIntensity(viewDir, surface);
  finalColor += emission;

  o_color = finalColor;

  // Disco view (hashed physical address or clipmap level)
  if (GetIsPageVisible(shadowVsm.pageData))
  {
    const float GOLDEN_CONJ = 0.6180339887498948482045868343656;
    if ((shadingUniforms.debugFlags & VSM_SHOW_CLIPMAP_ID) != 0)
    {
      o_color.rgb += 2.0 * hsv_to_rgb(vec3(shadowVsm.clipmapLevel*2 * GOLDEN_CONJ, 0.875, 0.85));
    }
    
    if ((shadingUniforms.debugFlags & VSM_SHOW_PAGE_ADDRESS) != 0)
    {
      o_color.rgb += 1.0 * hsv_to_rgb(vec3(float(GetPagePhysicalAddress(shadowVsm.pageData)) * GOLDEN_CONJ, 0.875, 0.85));
    }
  }

  // UV + shadow map depth view
  if ((shadingUniforms.debugFlags & VSM_SHOW_SHADOW_DEPTH) != 0)
  {
    if (GetIsPageVisible(shadowVsm.pageData))
    {
      if (GetIsPageBacked(shadowVsm.pageData))
        o_color.rgb += 3.0 * shadowVsm.shadowDepth.rrr;
    }
  }

  if ((shadingUniforms.debugFlags & VSM_SHOW_DIRTY_PAGES) != 0)
  {
    if (GetIsPageDirty(shadowVsm.pageData))
    {
      o_color.r = 2.0;
    }
  }

  // Page outlines
  if ((shadingUniforms.debugFlags & VSM_SHOW_PAGE_OUTLINES) != 0)
  {
    const vec2 pageUv = fract(shadowVsm.vsmUv);
    if (pageUv.x < .02 || pageUv.y < .02 || pageUv.x > .98 || pageUv.y > .98)
    {
      o_color.rgb = vec3(1, 0, 0);
    }
  }
}