#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "Config.shared.h"
#include "GlobalUniforms.h.glsl"
#include "Math.h.glsl"
#include "Pbr.h.glsl"
#include "shadows/vsm/VsmCommon.h.glsl"
#include "Utility.h.glsl"

layout(binding = 0) uniform sampler2D s_gAlbedo;
layout(binding = 1) uniform sampler2D s_gNormalAndFaceNormal;
layout(binding = 2) uniform sampler2D s_gDepth;
layout(binding = 3) uniform sampler2D s_gSmoothVertexNormal;
layout(binding = 6) uniform sampler2D s_emission;
layout(binding = 7) uniform sampler2D s_metallicRoughnessAo;

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec3 o_color;

#define VSM_SHOW_CLIPMAP_ID    (1 << 0)
#define VSM_SHOW_PAGE_ADDRESS  (1 << 1)
#define VSM_SHOW_PAGE_OUTLINES (1 << 2)
#define VSM_SHOW_SHADOW_DEPTH  (1 << 3)
#define VSM_SHOW_DIRTY_PAGES   (1 << 4)
#define BLEND_NORMALS          (1 << 5)

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
  uint shadowMode; // 0 = PCSS, 1 = SMRT

  // PCSS
  uint pcfSamples;
  float lightWidth;
  float maxPcfRadius;
  uint blockerSearchSamples;
  float blockerSearchRadius;

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

// Returns an exact shadow bias in the space of whatever texelWidth is in.
// N and L are expected to be normalized
float GetShadowBias(vec3 N, vec3 L, float texelWidth)
{
  const float sqrt2 = 1.41421356; // Mul by sqrt2 to get diagonal length
  const float quantize = 2.0 / (1 << 23); // Arbitrary constant that should help prevent most numerical issues
  const float b = sqrt2 * texelWidth / 2.0;
  const float NoL = clamp(abs(dot(N, L)), 0.0001, 1.0);
  return quantize + b * length(cross(N, L)) / NoL;
}

float CalcVsmShadowBias(uint clipmapLevel, vec3 faceNormal)
{
  const float shadowTexelSize = exp2(clipmapLevel) * clipmapUniforms.firstClipmapTexelLength;
  const float bias = GetShadowBias(faceNormal, -shadingUniforms.sunDir.xyz, shadowTexelSize);
  return bias;
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

  const float maxBias = exp2(ret.clipmapLevel) * 0.1;
  const float bias = min(maxBias, 0.01 + CalcVsmShadowBias(addr.clipmapLevel, normal)) / clipmapUniforms.projectionZLength;

  if (ret.shadowDepth + bias < ret.projectedDepth)
  {
    ret.shadow = 0.0;
    return ret;
  }

  ret.shadow = 1.0;
  return ret;
}

bool TrySampleVsmClipmap(int level, vec2 uv, vec2 worldOffset, out float depth)
{
  if (level < 0 || level >= clipmapUniforms.numClipmaps)
  {
    return false;
  }
  const uint clipmapIndex = clipmapUniforms.clipmapTableIndices[level];

  // World-to-UV translation for this level
  const ivec2 numClipmapTexels = imageSize(i_pageTables).xy * PAGE_SIZE;
  const vec2 clipmapSizeWorldSpace = exp2(level) * clipmapUniforms.firstClipmapTexelLength * numClipmapTexels;
  const vec2 vsmOffsetUv = fract(uv + worldOffset / clipmapSizeWorldSpace);

  const ivec2 vsmTexel = ivec2(vsmOffsetUv * numClipmapTexels);
  const ivec2 vsmPage = vsmTexel / PAGE_SIZE;
  const ivec2 pageTexel = vsmTexel % PAGE_SIZE;
  const uint pageData = imageLoad(i_pageTables, ivec3(vsmPage, clipmapIndex)).x;
  if (!GetIsPageBacked(pageData))
  {
    return false;
  }

  const uint physicalAddress = GetPagePhysicalAddress(pageData);
  depth = LoadPageTexel(pageTexel, physicalAddress);

  return true;
}

float ShadowVsmPcss(vec3 fragWorldPos, vec3 flatNormal)
{
  const ivec2 gid = ivec2(gl_FragCoord.xy);
  const float depthSample = texelFetch(s_gDepth, gid, 0).x;
  const PageAddressInfo addr = GetClipmapPageFromDepth(depthSample, gid, textureSize(s_gDepth, 0));

  const float maxBias = exp2(addr.clipmapLevel) * 0.02;
  const float baseBias = min(maxBias, CalcVsmShadowBias(addr.clipmapLevel, flatNormal));
  const float invProjZLength = 1.0 / clipmapUniforms.projectionZLength;
  
  // Blocker search
  float accumDepth = 0;
  uint blockers = 0;
  for (uint i = 0; i < shadowUniforms.blockerSearchSamples; i++)
  {
    const vec2 xi = fract(Hammersley(i, shadowUniforms.blockerSearchSamples) + hash(gl_FragCoord.xy) + shadingUniforms.random.yx);
    const float r = sqrt(xi.x) * shadowUniforms.blockerSearchRadius;
    const float theta = xi.y * 2.0 * 3.14159;
    const vec2 offset = r * vec2(cos(theta), sin(theta));
    // PCF puts some samples under the surface when L is not parallel to N
    const float pcfBias = 2.0 * r;
    const float realBias = invProjZLength * (baseBias + mix(pcfBias, 0.0, max(0.0, dot(flatNormal, -shadingUniforms.sunDir.xyz))));

    float depth;
    if (TrySampleVsmClipmap(int(addr.clipmapLevel), addr.posLightNdc.xy * 0.5 + 0.5, offset, depth))
    {
      if (depth + realBias < addr.projectedDepth)
      {
        accumDepth += depth * clipmapUniforms.projectionZLength;
        blockers++;
      }
      continue;
    }
    
    // Sample lower level (more detailed) (double device coordinate first)
    if (TrySampleVsmClipmap(int(addr.clipmapLevel) - 1, (addr.posLightNdc.xy * 2.0) * 0.5 + 0.5, offset, depth))
    {
      if (depth + realBias / 1.0 < addr.projectedDepth)
      {
        accumDepth += depth * clipmapUniforms.projectionZLength;
        blockers++;
      }
      continue;
    }
    
    // Sample higher level (less detailed) (halve device coordinate first)
    if (TrySampleVsmClipmap(int(addr.clipmapLevel) + 1, (addr.posLightNdc.xy / 2.0) * 0.5 + 0.5, offset, depth))
    {
      if (depth + realBias * 1.0 < addr.projectedDepth)
      {
        accumDepth += depth * clipmapUniforms.projectionZLength;
        blockers++;
      }
      continue;
    }
  }
  
  // No blockers: fully visible
  if (blockers == 0)
  {
    return 1.0;
  }

  // All blockers: not visible
  if (blockers == shadowUniforms.blockerSearchSamples)
  {
    return 0.0;
  }

  // Light width
  //const float w_light = tan(radians(0.5));// * clipmapUniforms.projectionZLength;
  const float w_light = shadowUniforms.lightWidth;
  const float d_blocker = accumDepth / blockers;
  const float d_receiver = addr.projectedDepth * clipmapUniforms.projectionZLength;

  const float pcfRadius = min(shadowUniforms.maxPcfRadius, (d_receiver - d_blocker) * w_light);

  // PCF
  float lightVisibility = 0.0;
  for (uint i = 0; i < shadowUniforms.pcfSamples; i++)
  {
    const vec2 xi = fract(Hammersley(i, shadowUniforms.pcfSamples) + hash(gl_FragCoord.xy) + shadingUniforms.random);
    const float r = sqrt(xi.x) * pcfRadius;
    const float theta = xi.y * 2.0 * 3.14159;
    const vec2 offset = r * vec2(cos(theta), sin(theta));
    // PCF puts some samples under the surface when L is not parallel to N
    const float pcfBias = 2.0 * r / clipmapUniforms.projectionZLength;
    const float realBias = baseBias + mix(pcfBias, 0.0, max(0.0, dot(flatNormal, -shadingUniforms.sunDir.xyz)));

    float depth;
    if (TrySampleVsmClipmap(int(addr.clipmapLevel), addr.posLightNdc.xy * 0.5 + 0.5, offset, depth))
    {
      depth += realBias;
      if (depth >= addr.projectedDepth)
      {
        lightVisibility += 1.0;
      }
      continue;
    }
    
    // Sample lower level (more detailed) (double device coordinate first)
    if (TrySampleVsmClipmap(int(addr.clipmapLevel) - 1, (addr.posLightNdc.xy * 2.0) * 0.5 + 0.5, offset, depth))
    {
      depth += realBias / 1.0;
      if (depth >= addr.projectedDepth)
      {
        lightVisibility += 1.0;
      }
      continue;
    }
    
    // Sample higher level (less detailed) (halve device coordinate first)
    if (TrySampleVsmClipmap(int(addr.clipmapLevel) + 1, (addr.posLightNdc.xy / 2.0) * 0.5 + 0.5, offset, depth))
    {
      depth += realBias * 1.0;
      if (depth >= addr.projectedDepth)
      {
        lightVisibility += 1.0;
      }
      continue;
    }
    
    lightVisibility += 1.0;
  }

  return lightVisibility / shadowUniforms.pcfSamples;
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
  const vec3 albedo = textureLod(s_gAlbedo, v_uv, 0.0).rgb;
  const vec4 normalOctAndFlatNormalOct = textureLod(s_gNormalAndFaceNormal, v_uv, 0.0).xyzw;
  const vec3 mappedNormal = OctToVec3(normalOctAndFlatNormalOct.xy);
  const vec3 flatNormal = OctToVec3(normalOctAndFlatNormalOct.zw);
  const vec3 smoothNormal = OctToVec3(textureLod(s_gSmoothVertexNormal, v_uv, 0.0).xy);
  const float depth = textureLod(s_gDepth, v_uv, 0.0).x;
  const vec3 emission = textureLod(s_emission, v_uv, 0.0).rgb;
  const vec3 metallicRoughnessAo = textureLod(s_metallicRoughnessAo, v_uv, 0.0).rgb;

  if (depth == FAR_DEPTH)
  {
    discard;
  }

  const vec3 fragWorldPos = UnprojectUV_ZO(depth, v_uv, perFrameUniforms.invViewProj);
  
  ShadowVsmOut shadowVsm = ShadowVsm(fragWorldPos, flatNormal);
  float shadowSun = shadowVsm.shadow;
  shadowSun = ShadowVsmPcss(fragWorldPos, flatNormal);

  vec3 normal = mappedNormal;

  if ((shadingUniforms.debugFlags & BLEND_NORMALS) != 0)
  {
    // https://marmosetco.tumblr.com/post/81245981087
    const float NoL_sun_flat = dot(smoothNormal, -shadingUniforms.sunDir.xyz);
    float horiz = 1.0 - NoL_sun_flat;
    horiz *= horiz;
    normal = normalize(mix(mappedNormal, smoothNormal, clamp(horiz, 0.0, 1.0)));
  }

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