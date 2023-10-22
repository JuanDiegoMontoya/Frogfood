#ifndef PBR_H
#define PBR_H

// Punctual light attenuation factor
float GetSquareFalloffAttenuation(vec3 posToLight, float lightInvRadius)
{
  float distanceSquared = dot(posToLight, posToLight);
  float factor = distanceSquared * lightInvRadius * lightInvRadius;
  float smoothFactor = max(1.0 - factor * factor, 0.0);
  return (smoothFactor * smoothFactor) / max(distanceSquared, 1e-4);
}

// Spot light angle attenuation factor.
float GetSpotAngleAttenuation(float innerConeAngle, float outerConeAngle, vec3 lightDirection, vec3 L)
{
  float lightAngleScale = 1.0f / max(0.001, cos(innerConeAngle) - cos(outerConeAngle));
  float lightAngleOffset = -cos(outerConeAngle) * lightAngleScale;

  float cd = dot(-lightDirection, L);
  float angularAttenuation = clamp(cd * lightAngleScale + lightAngleOffset, 0.0, 1.0);

  // Real spot angle attenuation is complex, but we note that it generally declines rapidly, so squaring linear attenutation is a reasonable approximation
  return angularAttenuation * angularAttenuation;
}

// BRDF
// Normal distribution function (appearance of specular highlights)
float D_GGX(float NoH, float roughness)
{
  // Hack to make perfectly smooth materials display a non-infinitesimal specular highlight
  //roughness = max(roughness, 1e-3);
  
  // Hack to prevent zero in the denominator
  NoH = min(NoH, 0.9999);

  float a = NoH * roughness;
  float k = roughness / (1.0 - NoH * NoH + a * a);
  return k * k * (1.0 / 3.1415926);
}

// Visibility (geometric shadowing and masking)
float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
  float a2 = max(a * a, 1e-5);
  float ggxl = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
  float ggxv = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
  return 0.5 / (ggxv + ggxl);
}

// Fresnel. f0 = reflectance of material at normal incidence. f90 = same thing, but 90 degrees (grazing) incidence
// TODO: use better Fresnel term
float F_Schlick1(float u, float f0, float f90)
{
  return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}

vec3 F_Schlick3(float u, vec3 f0, float f90)
{
  return f0 + (vec3(f90) - f0) * pow(1.0 - u, 5.0);
}

// Lambertian diffuse BRDF
float Fd_Lambert()
{
  return 1.0 / 3.1415926;
}

// Disney diffuse BRDF. Apparently not energy-conserving, but takes into account surface roughness, unlike Lambert's
float Fd_Burley(float NoV, float NoL, float LoH, float roughness)
{
  float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
  float lightScatter = F_Schlick1(NoL, 1.0, f90);
  float viewScatter = F_Schlick1(NoV, 1.0, f90);
  return lightScatter * viewScatter * (1.0 / 3.1415926);
}

struct Surface
{
  vec3 albedo;
  vec3 normal;
  vec3 position;
  float metallic;
  float perceptualRoughness;
  float reflectance; // = 0.5
  float f90;
};

vec3 BRDF(vec3 viewDir, vec3 L, Surface surface)
{
  vec3 f0 = 0.16 * surface.reflectance * surface.reflectance * (1.0 - surface.metallic) + surface.albedo * surface.metallic;
  vec3 H = normalize(viewDir + L);

  float NoV = abs(dot(surface.normal, viewDir)) + 1e-5;
  float NoL = clamp(dot(surface.normal, L), 0.0, 1.0);
  float NoH = clamp(dot(surface.normal, H), 0.0, 1.0);
  float LoH = clamp(dot(L, H), 0.0, 1.0);

  // Perceptually linear roughness to roughness (see parameterization)
  float roughness = surface.perceptualRoughness * surface.perceptualRoughness;

  float D = D_GGX(NoH, roughness);
  vec3  F = F_Schlick3(LoH, f0, 1.0);
  float V = V_SmithGGXCorrelated(NoV, NoL, roughness);

  // Specular BRDF (Cook-Torrance)
  vec3 Fr = D * V * F;

  // Diffuse BRDF
  vec3 Fd = surface.albedo * Fd_Lambert();
  //vec3 Fd = surface.albedo * Fd_Burley(NoV, NoL, LoH, roughness);

  // Combine BRDFs according to very scientific formula
  return Fr + Fd * (vec3(1) - F) * (1.0 - surface.metallic);
}

#define LIGHT_TYPE_DIRECTIONAL 0u
#define LIGHT_TYPE_POINT 1u
#define LIGHT_TYPE_SPOT 2u

struct GpuLight
{
  vec3 color;
  uint type;
  vec3 direction;  // Directional and spot only
  // Point and spot lights use candela (lm/sr) while directional use lux (lm/m^2)
  float intensity;
  vec3 position;        // Point and spot only
  float range;          // Point and spot only
  float innerConeAngle; // Spot only
  float outerConeAngle; // Spot only
  uint _padding[2];
};

vec3 EvaluatePunctualLight(vec3 viewDir, GpuLight light, Surface surface)
{
  vec3 surfaceToLight = light.position - surface.position;
  vec3 L = normalize(surfaceToLight);
  float NoL = clamp(dot(surface.normal, L), 0.0, 1.0);

  float attenuation = GetSquareFalloffAttenuation(surfaceToLight, 1.0 / light.range);

  if (light.type == LIGHT_TYPE_SPOT)
  {
    attenuation *= GetSpotAngleAttenuation(light.innerConeAngle, light.outerConeAngle, light.direction, L);
  }

  return BRDF(viewDir, L, surface) * attenuation * NoL * light.color * light.intensity;
}

#endif // PBR_H