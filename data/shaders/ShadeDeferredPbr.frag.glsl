#version 460 core

#define SHADING_PUSH_CONSTANTS
#include "ShadeDeferredPbr.h.glsl"

#include "Config.shared.h"
#include "GlobalUniforms.h.glsl"
#include "Math.h.glsl"
#include "Pbr.h.glsl"
#define VSM_NO_PUSH_CONSTANTS
#include "shadows/vsm/VsmCommon.h.glsl"
#include "Utility.h.glsl"
#include "Color.h.glsl"

// TODO: temp for rt
#define VISBUFFER_NO_PUSH_CONSTANTS
#include "visbuffer/VisbufferCommon.h.glsl"

#define d_perFrameUniforms perFrameUniformsBuffers[globalUniformsIndex]

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec3 o_color;

FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly ShadingUniformsBuffers)
{
  ShadingUniforms uniforms;
}shadingUniformsBuffers[];

#define shadingUniforms shadingUniformsBuffers[shadingUniformsIndex].uniforms

FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly ShadowUniformsBuffers)
{
  ShadowUniforms uniforms;
}shadowUniformsBuffers[];

#define shadowUniforms shadowUniformsBuffers[shadowUniformsIndex].uniforms

FVOG_DECLARE_STORAGE_BUFFERS(restrict readonly LightBuffer)
{
  GpuLight lights[];
}lightBuffers[];

#define d_lightBuffer lightBuffers[lightBufferIndex]

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
  uint overdraw;
};

ShadowVsmOut ShadowVsm(vec3 fragWorldPos, vec3 normal)
{
  ShadowVsmOut ret;
  ret.pageData = 0;
  ret.vsmUv = vec2(0);
  ret.shadowDepth = 0;
  ret.projectedDepth = 0;
  ret.overdraw = 0;

  const ivec2 gid = ivec2(gl_FragCoord.xy);
  const float depthSample = texelFetch(FvogGetSampledImage(texture2D, gDepthIndex), gid, 0).x;
  PageAddressInfo addr = GetClipmapPageFromDepth(depthSample, gid, textureSize(FvogGetSampledImage(texture2D, gDepthIndex), 0));

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

#if VSM_RENDER_OVERDRAW
  ret.overdraw = imageLoad(i_physicalPagesOverdrawHeatmap, GetPhysicalTexelAddress(pageTexel, physicalAddress)).x;
#endif

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
  const float depthSample = texelFetch(FvogGetSampledImage(texture2D, gDepthIndex), gid, 0).x;
  const PageAddressInfo addr = GetClipmapPageFromDepth(depthSample, gid, textureSize(FvogGetSampledImage(texture2D, gDepthIndex), 0));

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





#ifdef FROGRENDER_RAYTRACING_ENABLE
// glslang doesn't like having an acceleration structure as a parameter (it generates invalid spirv)
// flatNormal: used for shadow offset
// actualNormal: used for cosine term
float ShadowSunRayTraced(vec3 worldPos, vec3 dirToSun, vec3 flatNormal, vec3 shadingNormal, float diameterRadians, vec2 noise, uint numRays, uint64_t tlasAddress)
{
  if (numRays == 0)
  {
    return 0;
  }

  float visibility = 0.0;
  for (uint i = 0; i < numRays; i++)
  {
    const vec2 xi = fract(noise + Hammersley(i, numRays));
    const vec3 rayDir = RandVecInCone(xi, dirToSun, diameterRadians);

    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(rayQuery, accelerationStructureEXT(tlasAddress), 
      gl_RayFlagsOpaqueEXT,
      0xFF, worldPos + flatNormal * .001, 0.001, rayDir, 10000);
      
    while (rayQueryProceedEXT(rayQuery))
    {
      if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionTriangleEXT)
      {
        rayQueryConfirmIntersectionEXT(rayQuery);
      }
    }

    if (!(rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT))
    {
      const float cosTheta = clamp(dot(rayDir, shadingNormal), 0.0, 1.0);
      visibility += cosTheta;
    }
  }

  return visibility / numRays;
}

vec3 LocalLightIntensityRayTraced(vec3 viewDir, Surface surface, uint64_t tlasAddress)
{
  vec3 color = { 0, 0, 0 };

  for (uint i = 0; i < shadingUniforms.numberOfLights; i++)
  {
    GpuLight light = d_lightBuffer.lights[i];

    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(rayQuery, accelerationStructureEXT(tlasAddress), 
      gl_RayFlagsOpaqueEXT,
      0xFF, surface.position + surface.normal * .01, 0.001, normalize(light.position - surface.position), distance(light.position, surface.position));
      
    while (rayQueryProceedEXT(rayQuery))
    {
      if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionTriangleEXT)
      {
        rayQueryConfirmIntersectionEXT(rayQuery);
      }
    }

    if (!(rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT))
    {
      color += EvaluatePunctualLight(viewDir, light, surface, shadingUniforms.shadingInternalColorSpace);
    }
  }

  return color;
}
#endif

vec3 LocalLightIntensity(vec3 viewDir, Surface surface)
{
  vec3 color = { 0, 0, 0 };

  for (uint i = 0; i < shadingUniforms.numberOfLights; i++)
  {
    GpuLight light = d_lightBuffer.lights[i];

    color += EvaluatePunctualLight(viewDir, light, surface, shadingUniforms.shadingInternalColorSpace);
  }

  return color;
}

void main()
{
  const ivec2 gid = ivec2(gl_FragCoord.xy);
  const vec4 normalOctAndFlatNormalOct = texelFetch(FvogGetSampledImage(texture2D, gNormalAndFaceNormalIndex), gid, 0).xyzw;
  const vec3 mappedNormal = OctToVec3(normalOctAndFlatNormalOct.xy);
  const vec3 flatNormal = OctToVec3(normalOctAndFlatNormalOct.zw);
  const vec3 smoothNormal = OctToVec3(texelFetch(FvogGetSampledImage(texture2D, gSmoothVertexNormalIndex), gid, 0).xy);
  const float depth = texelFetch(FvogGetSampledImage(texture2D, gDepthIndex), gid, 0).x;
  const vec3 metallicRoughnessAo = texelFetch(FvogGetSampledImage(texture2D, gMetallicRoughnessAoIndex), gid, 0).rgb;
  const float ao = textureLod(ambientOcclusion, Sampler(nearestSamplerIndex), v_uv, 0).x; // AO could be a 1x1 white texture, so do not use texelFetch (% with texture size would work too)

  const vec3 albedo_internal = color_convert_src_to_dst(texelFetch(FvogGetSampledImage(texture2D, gAlbedoIndex), gid, 0).rgb, 
    COLOR_SPACE_sRGB_LINEAR,
    shadingUniforms.shadingInternalColorSpace);
  const vec3 emission_internal = color_convert_src_to_dst(texelFetch(FvogGetSampledImage(texture2D, gEmissionIndex), gid, 0).rgb,
    COLOR_SPACE_sRGB_LINEAR,
    shadingUniforms.shadingInternalColorSpace);

  if (depth == FAR_DEPTH)
  {
    discard;
  }

  const vec3 fragWorldPos = UnprojectUV_ZO(depth, v_uv, d_perFrameUniforms.invViewProj);
  



  float shadowSun = 0;

  vec3 normal = mappedNormal;

  const float NoL_sun = clamp(dot(normal, -shadingUniforms.sunDir.xyz), 0.0, 1.0);

  // shadowVsm is only used for debugging.
  ShadowVsmOut shadowVsm;
  if (shadowUniforms.shadowMode == SHADOW_MODE_VIRTUAL_SHADOW_MAP)
  {
    shadowVsm = ShadowVsm(fragWorldPos, flatNormal);
    if (shadowUniforms.shadowMapFilter == SHADOW_MAP_FILTER_NONE)
    {
      shadowSun = shadowVsm.shadow * NoL_sun;
    }
    else if (shadowUniforms.shadowMapFilter == SHADOW_MAP_FILTER_PCSS)
    {
      shadowSun = ShadowVsmPcss(fragWorldPos, flatNormal) * NoL_sun;
    }
    else if (shadowUniforms.shadowMapFilter == SHADOW_MAP_FILTER_SMRT)
    {
      ASSERT_MSG(false, "SMRT is not implemented\n");
    }
  }
#ifdef FROGRENDER_RAYTRACING_ENABLE
  else if (shadowUniforms.shadowMode == SHADOW_MODE_RAY_TRACED)
  {
    uint randState = PCG_Hash(PCG_Hash(gid.y + PCG_Hash(gid.x)));
    const vec2 noise = shadingUniforms.random + vec2(PCG_RandFloat(randState, 0, 1), PCG_RandFloat(randState, 0, 1));
    shadowSun = ShadowSunRayTraced(fragWorldPos,
                  -shadingUniforms.sunDir.xyz,
                  flatNormal,
                  normal,
                  shadowUniforms.rtSunDiameterRadians,
                  noise,
                  shadowUniforms.rtNumSunShadowRays,
                  shadingUniforms.tlasAddress);
  }
#endif

  if ((shadingUniforms.debugFlags & BLEND_NORMALS) != 0)
  {
    // https://marmosetco.tumblr.com/post/81245981087
    const float NoL_sun_flat = dot(smoothNormal, -shadingUniforms.sunDir.xyz);
    float horiz = 1.0 - NoL_sun_flat;
    horiz *= horiz;
    normal = normalize(mix(mappedNormal, smoothNormal, clamp(horiz, 0.0, 1.0)));
  }

  const vec3 fragToCameraDir = normalize(d_perFrameUniforms.cameraPos.xyz - fragWorldPos);
  
  Surface surface;
  surface.albedo = albedo_internal;
  surface.normal = normal;
  surface.position = fragWorldPos;
  surface.metallic = metallicRoughnessAo.x;
  surface.perceptualRoughness = metallicRoughnessAo.y;
  // Common materials have an IOR of 1.5 which works out to a reflectance of 0.5 in the following equation:
  // f0 = 0.16 * reflectance^2 = ((IOR - 1) / (IOR + 1)) ^ 2
  // (Reminder: this is an artist-friendly mapping for many common materials' physical reflectances to fit within [0, 1])
  surface.reflectance = 0.5;
  surface.f90 = 1.0;

  vec3 indirectIlluminance = vec3(0);

  if (shadingUniforms.globalIlluminationMethod == GI_METHOD_CONSTANT_AMBIENT)
  {
    indirectIlluminance = vec3(.03) * albedo_internal * metallicRoughnessAo.z * ao; // Ambient lighting
  }
#ifdef FROGRENDER_RAYTRACING_ENABLE
  else if (shadingUniforms.globalIlluminationMethod == GI_METHOD_PATH_TRACED)
  {
    const uint BOUNCES = 3;
    
    for (int j = 0; j < shadingUniforms.numGiRays; j++)
    {
      uint randState = PCG_Hash(j + PCG_Hash(gid.y + PCG_Hash(gid.x)));
      vec2 noise = shadingUniforms.random + vec2(PCG_RandFloat(randState, 0, 1), PCG_RandFloat(randState, 0, 1));

      vec3 prevRayDir = -fragToCameraDir;
      vec3 curRayPos = fragWorldPos;
      Surface curSurface = surface;
      vec3 curSurfaceFlatNormal = flatNormal;
      vec3 throughput = vec3(1);
      for (uint i = 0; i < BOUNCES; i++)
      {
        const vec2 xi = fract(noise + Hammersley(i, BOUNCES));
        const vec3 curRayDir = normalize(map_to_unit_hemisphere_cosine_weighted(xi, curSurface.normal));
        const float cos_theta = clamp(dot(curSurface.normal, curRayDir), 0.00001, 1.0);
        const float pdf = cosine_weighted_hemisphere_PDF(cos_theta);
        ASSERT_MSG(isfinite(pdf), "PDF is not finite!\n");
        //const vec3 brdf_over_pdf = curSurface.albedo / M_PI / pdf; // Lambertian
        const vec3 brdf_over_pdf = BRDF(-prevRayDir, curRayDir, curSurface) / pdf;

        // Throughput of previous hit
        throughput *= cos_theta * brdf_over_pdf;

        rayQueryEXT rayQuery;
        rayQueryInitializeEXT(rayQuery, accelerationStructureEXT(shadingUniforms.tlasAddress), 
          gl_RayFlagsOpaqueEXT,
          0xFF, curRayPos + curSurfaceFlatNormal * .001, 0.0, curRayDir, 10000.0);
          //0xFF, curRayPos, 0.001, curRayDir, 10000.0);
          
        while (rayQueryProceedEXT(rayQuery))
        {
          if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionTriangleEXT)
          {
            // TODO: for alpha masked materials, perform alpha test
            rayQueryConfirmIntersectionEXT(rayQuery);
          }
        }

        // Hit
        if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
        {
          // TODO: check if surface is emissive. If it is, set indirect to emission * throughput, then exit. (this is a hack, doesn't work for slightly emissive materials)
          // TODO: add contribution for all lights in scene, or one randomly picked (maybe with some smart weighting like ReSTIR)
          const int primitiveId = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
          const int instanceId = rayQueryGetIntersectionInstanceCustomIndexEXT(rayQuery, true);
          ObjectUniforms obj = TransformBuffers[NonUniformIndex(shadingUniforms.instanceBufferIndex)].transforms[instanceId];
          const Vertex v0 = obj.vertexBuffer.vertices[obj.indexBuffer.indices[primitiveId * 3 + 0]];
          const Vertex v1 = obj.vertexBuffer.vertices[obj.indexBuffer.indices[primitiveId * 3 + 1]];
          const Vertex v2 = obj.vertexBuffer.vertices[obj.indexBuffer.indices[primitiveId * 3 + 2]];
          
          const vec2 baryBC = rayQueryGetIntersectionBarycentricsEXT(rayQuery, true);
          const vec3 bary = vec3(1.0 - baryBC.x - baryBC.y, baryBC.x, baryBC.y);

          const vec2 uv = PackedToVec2(v0.uv) * bary.x + PackedToVec2(v1.uv) * bary.y + PackedToVec2(v2.uv) * bary.z;
          const vec3 smooth_normal_object = OctToVec3(unpackSnorm2x16(v0.normal)) * bary.x + OctToVec3(unpackSnorm2x16(v1.normal)) * bary.y + OctToVec3(unpackSnorm2x16(v2.normal)) * bary.z;
          const vec3 position_object = PackedToVec3(v0.position) * bary.x + PackedToVec3(v1.position) * bary.y + PackedToVec3(v2.position) * bary.z;
          const vec3 flat_normal_object = normalize(cross(PackedToVec3(v2.position) - PackedToVec3(v1.position), PackedToVec3(v2.position) - PackedToVec3(v2.position)));
          //const mat4x3 world_from_object = rayQueryGetIntersectionObjectToWorldEXT(rayQuery, true);
          const mat3 world_from_object_normal = transpose(inverse(mat3(obj.modelCurrent)));

          const vec3 smooth_normal_world = world_from_object_normal * smooth_normal_object;
          const vec3 flat_normal_world = world_from_object_normal * flat_normal_object;
          const vec3 position_world = vec3(obj.modelCurrent * vec4(position_object, 1.0));

          const GpuMaterial material = MaterialBuffers[NonUniformIndex(shadingUniforms.materialBufferIndex)].materials[obj.materialId];

          vec4 materialColorSrgb = material.baseColorFactor;
          if (bool(material.flags & MATERIAL_HAS_BASE_COLOR))
          {
            materialColorSrgb *= textureLod(Fvog_sampler2D(material.baseColorTextureIndex, nearestSamplerIndex), uv, 0.0);
          }

          // TODO: handle opacity

          const vec4 materialColorInternal = 
            vec4(color_convert_src_to_dst(materialColorSrgb.rgb,
              COLOR_SPACE_sRGB_LINEAR,
              shadingUniforms.shadingInternalColorSpace),
            materialColorSrgb.a);

          // Intentionally skip normal mapping (we don't yet have vertex tangents and the difference for GI is probably negligible)

          prevRayDir = curRayDir;
          curRayPos = position_world;
          curSurfaceFlatNormal = flat_normal_world;

          curSurface.albedo = materialColorInternal.rgb;
          curSurface.normal = smooth_normal_world;
          curSurface.position = position_world;
          curSurface.metallic = 0.0; // TODO
          curSurface.perceptualRoughness = 0.0; // TODO
          curSurface.reflectance = 0.5;
          curSurface.f90 = 1.0;
        }
        else
        {
          // Miss, add sky contribution
          const vec3 skyEmittance = color_convert_src_to_dst(vec3(.1, .3, .5),
          //const vec3 skyEmittance = color_convert_src_to_dst(vec3(100),
            COLOR_SPACE_sRGB_LINEAR,
            shadingUniforms.shadingInternalColorSpace);
          indirectIlluminance += skyEmittance * throughput;
          break;
        }
      }
    }

    indirectIlluminance /= shadingUniforms.numGiRays;
  }
#endif

  vec3 finalColor = indirectIlluminance;

  const vec3 sunColor_internal_space = color_convert_src_to_dst(shadingUniforms.sunIlluminance.rgb,
    COLOR_SPACE_sRGB_LINEAR,
    shadingUniforms.shadingInternalColorSpace);
  // Treat sun solid angle as constant 0.5 degrees for PDF. Otherwise, changing the diameter of the sun will change the scene brightness.
  // That is realistic because the sun's intensity is defined as lux (lm/m^2), but it's annoying to work with.
  finalColor += BRDF(fragToCameraDir, -shadingUniforms.sunDir.xyz, surface) * sunColor_internal_space * shadowSun / solid_angle_mapping_PDF(radians(0.5));
  #ifdef FROGRENDER_RAYTRACING_ENABLE
  if (shadowUniforms.rtTraceLocalLights == 1)
  {
    finalColor += LocalLightIntensityRayTraced(fragToCameraDir, surface, shadingUniforms.tlasAddress);
  }
  else
  #endif
  {
    finalColor += LocalLightIntensity(fragToCameraDir, surface);
  }
  finalColor += emission_internal;

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
    if (GetIsPageVisible(shadowVsm.pageData) && GetIsPageBacked(shadowVsm.pageData))
    {
      o_color.rgb += 3.0 * shadowVsm.shadowDepth.rrr;
    }
  }
  
  // Overdraw heatmap
  if ((shadingUniforms.debugFlags & VSM_SHOW_OVERDRAW) != 0)
  {
    if (shadowVsm.overdraw > 0 && GetIsPageVisible(shadowVsm.pageData) && GetIsPageBacked(shadowVsm.pageData))
    {
      o_color.rgb += 3.0 * TurboColormap(shadowVsm.overdraw / VSM_MAX_OVERDRAW);
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

  if ((shadingUniforms.debugFlags & SHOW_AO_ONLY) != 0)
  {
    o_color.rgb = vec3(ao);
  }
}
