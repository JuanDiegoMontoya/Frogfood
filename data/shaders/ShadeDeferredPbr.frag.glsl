#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "Config.h.glsl"
#include "GlobalUniforms.h.glsl"
#include "Math.h.glsl"
#include "Pbr.h.glsl"

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

layout(binding = 1, std140) uniform ShadingUniforms
{
  mat4 sunViewProj;
  vec4 sunDir;
  vec4 sunStrength;
  mat4 sunView;
  mat4 sunProj;
  vec2 random;
  uint numberOfLights;
  uint _padding;
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

layout(binding = 0, std430) readonly buffer LightBuffer
{
  GpuLight lights[];
}lightBuffer;

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
float MarchShadowRay(vec3 rayLightViewPos, vec3 rayLightViewDir, float bias)
{
  for (int stepIdx = 0; stepIdx < shadowUniforms.stepsPerRay; stepIdx++)
  {
    rayLightViewPos += rayLightViewDir * shadowUniforms.rayStepSize;

    vec4 rayLightClipPos = shadingUniforms.sunProj * vec4(rayLightViewPos, 1.0);
    rayLightClipPos.xy /= rayLightClipPos.w; // to NDC
    rayLightClipPos.xy = rayLightClipPos.xy * 0.5 + 0.5; // to UV
    float shadowMapWindowZ = /*bias*/ + textureLod(s_rsmDepth, rayLightClipPos.xy, 0.0).x;
    // Note: view Z gets *smaller* as we go deeper into the frusum (farther from the camera)
    float shadowMapViewZ = UnprojectUV_ZO(shadowMapWindowZ, rayLightClipPos.xy, inverse(shadingUniforms.sunProj)).z;

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

float ShadowRayTraced(vec3 fragWorldPos, vec3 lightDir, float bias)
{
  float lightOcclusion = 0.0;

  for (int rayIdx = 0; rayIdx < shadowUniforms.shadowRays; rayIdx++)
  {
    vec2 xi = Hammersley(rayIdx, shadowUniforms.shadowRays);
    xi = fract(xi + hash(gl_FragCoord.xy) + shadingUniforms.random);
    vec3 newLightDir = RandVecInCone(xi, lightDir, shadowUniforms.sourceAngleRad);

    vec3 rayLightViewDir = (shadingUniforms.sunView * vec4(newLightDir, 0.0)).xyz;
    vec3 rayLightViewPos = (shadingUniforms.sunView * vec4(fragWorldPos, 1.0)).xyz;

    lightOcclusion += MarchShadowRay(rayLightViewPos, rayLightViewDir, bias);
  }

  return lightOcclusion / shadowUniforms.shadowRays;
}

float Shadow(vec3 fragWorldPos, vec3 normal, vec3 lightDir)
{
  vec4 clip = shadingUniforms.sunViewProj * vec4(fragWorldPos, 1.0);
  vec2 uv = clip.xy * .5 + .5;
  if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
  {
    return 0;
  }

  // Analytically compute slope-scaled bias
  const float maxBias = 0.0008;
  const float quantize = 2.0 / (1 << 23);
  ivec2 res = textureSize(s_rsmDepth, 0);
  float b = 1.0 / max(res.x, res.y) / 2.0;
  float NoD = clamp(-dot(shadingUniforms.sunDir.xyz, normal), 0.0, 1.0);
  float bias = quantize + b * length(cross(-shadingUniforms.sunDir.xyz, normal)) / NoD;
  bias = min(bias, maxBias);

  switch (shadowUniforms.shadowMode)
  {
    case 0: return ShadowPCF(uv, clip.z * .5 + .5, bias);
    case 1: return ShadowRayTraced(fragWorldPos, lightDir, bias);
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

  float shadow = Shadow(fragWorldPos, normal, -shadingUniforms.sunDir.xyz);
  
  vec3 viewDir = normalize(perFrameUniforms.cameraPos.xyz - fragWorldPos);
  
  Surface surface;
  surface.albedo = albedo;
  surface.normal = normal;
  surface.position = fragWorldPos;
  surface.metallic = metallicRoughnessAo.x;
  surface.perceptualRoughness = metallicRoughnessAo.y;
  surface.reflectance = 0.04;
  surface.f90 = 1.0;

  vec3 finalColor = vec3(.03) * albedo * metallicRoughnessAo.z; // Ambient lighting

  float NoL_sun = clamp(dot(normal, -shadingUniforms.sunDir.xyz), 0.0, 1.0);
  finalColor += BRDF(viewDir, -shadingUniforms.sunDir.xyz, surface) * shadingUniforms.sunStrength.rgb * NoL_sun;
  finalColor += LocalLightIntensity(viewDir, surface);
  finalColor += emission;

  o_color = finalColor;
}