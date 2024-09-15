#ifndef MATH_H
#define MATH_H

#include "Hash.h.glsl"

// Constants
const float M_PI = 3.141592654;


// Functions

// PDF: solid_angle_mapping_PDF (see bottom of this file)
vec3 RandVecInCone(vec2 xi, vec3 N, float angle)
{
  float phi = 2.0 * M_PI * xi.x;
  
  float theta = sqrt(xi.y) * angle;
  float cosTheta = cos(theta);
  float sinTheta = sin(theta);

  vec3 H;
  H.x = cos(phi) * sinTheta;
  H.y = sin(phi) * sinTheta;
  H.z = cosTheta;

  vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  vec3 tangent = normalize(cross(up, N));
  vec3 bitangent = cross(N, tangent);
  mat3 tbn = mat3(tangent, bitangent, N);

  vec3 sampleVec = tbn * H;
  return normalize(sampleVec);
}

float hash(vec2 n)
{ 
  return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453);
}

vec2 Hammersley(uint i, uint N)
{
  return vec2(float(i) / float(N), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

// Zero-to-one depth convention
vec3 UnprojectUV_ZO(float depth, vec2 uv, mat4 invXProj)
{
  vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
  vec4 world = invXProj * ndc;
  return world.xyz / world.w;
}

float Remap(float val, float start1, float end1, float start2, float end2)
{
  return (val - start1) / (end1 - start1) * (end2 - start2) + start2;
}

// TODO: this should probably just multiply dxuv and dyuv by exp2(bias)
void ApplyLodBiasToGradient(inout vec2 dxuv, inout vec2 dyuv, float bias)
{
  float ddx2 = dot(dxuv, dxuv);
  float ddy2 = dot(dyuv, dyuv);
  float actual_mip = exp2(bias + 0.5 * log2(max(ddx2, ddy2)));
  float min_mip = sqrt(min(ddx2, ddy2));
  dxuv *= actual_mip / min_mip;
  dyuv *= actual_mip / min_mip;
}

bool RectIntersectRect(vec2 bottomLeft0, vec2 topRight0, vec2 bottomLeft1, vec2 topRight1)
{
  return !(any(lessThan(topRight0, bottomLeft1)) || any(greaterThan(bottomLeft0, topRight1)));
}

// Hashed Alpha Testing
// https://casual-effects.com/research/Wyman2017Hashed/Wyman2017Hashed.pdf
// maxObjSpaceDerivLen = max(length(dFdx(i_objectSpacePos)), length(dFdy(i_objectSpacePos)));
float ComputeHashedAlphaThreshold(vec3 objectSpacePos, float maxObjSpaceDerivLen, float hashScale)
{
  float pixScale = 1.0 / (hashScale + maxObjSpaceDerivLen);
  float pixScaleMin = exp2(floor(log2(pixScale)));
  float pixScaleMax = exp2(ceil(log2(pixScale)));
  vec2 alpha = vec2(MM_Hash3(floor(pixScaleMin * objectSpacePos)), MM_Hash3(floor(pixScaleMax * objectSpacePos)));
  float lerpFactor = fract(log2(pixScale));
  float x = (1.0 - lerpFactor) * alpha.x + lerpFactor * alpha.y;
  float a = min(lerpFactor, 1.0 - lerpFactor);
  vec3 cases = vec3(x * x / (2.0 * a * (1.0 - a)), (x - 0.5 * a) / (1.0 - a), 1.0 - ((1.0 - x) * (1.0 - x) / (2.0 * a * (1.0 - a))));
  float threshold = (x < (1.0 - a)) ? ((x < a) ? cases.x : cases.y) : cases.z;
  return clamp(threshold, 1e-6, 1.0);
}

// Copyright 2019 Google LLC.
// SPDX-License-Identifier: Apache-2.0

// Polynomial approximation in GLSL for the Turbo colormap
// Original LUT: https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f

// Authors:
//   Colormap Design: Anton Mikhailov (mikhailov@google.com)
//   GLSL Approximation: Ruofei Du (ruofei@google.com)

vec3 TurboColormap(float x)
{
  const vec4 kRedVec4 = vec4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
  const vec4 kGreenVec4 = vec4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
  const vec4 kBlueVec4 = vec4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
  const vec2 kRedVec2 = vec2(-152.94239396, 59.28637943);
  const vec2 kGreenVec2 = vec2(4.27729857, 2.82956604);
  const vec2 kBlueVec2 = vec2(-89.90310912, 27.34824973);
  
  x = clamp(x, 0, 1);
  vec4 v4 = vec4( 1.0, x, x * x, x * x * x);
  vec2 v2 = v4.zw * v4.z;
  return vec3(
    dot(v4, kRedVec4)   + dot(v2, kRedVec2),
    dot(v4, kGreenVec4) + dot(v2, kGreenVec2),
    dot(v4, kBlueVec4)  + dot(v2, kBlueVec2)
  );
}

bool isfinite(float x)
{
  return !isnan(x) && !isinf(x);
}

bvec2 isfinite(vec2 x)
{
  return bvec2(isfinite(x.x), isfinite(x.y));
}

bvec3 isfinite(vec3 x)
{
  return bvec3(isfinite(x.x), isfinite(x.y), isfinite(x.z));
}

bvec4 isfinite(vec4 x)
{
  return bvec4(isfinite(x.x), isfinite(x.y), isfinite(x.z), isfinite(x.w));
}

// Stolen from void
vec3 map_to_unit_sphere(vec2 uv)
{
  float cos_theta = 2.0 * uv.x - 1.0;
  float phi = 2.0 * M_PI * uv.y;
  float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
  float sin_phi = sin(phi);
  float cos_phi = cos(phi);
  
  return vec3(sin_theta * cos_phi, cos_theta, sin_theta * sin_phi);
}

vec3 map_to_unit_hemisphere_cosine_weighted(vec2 uv, vec3 n)
{
  vec3 p = map_to_unit_sphere(uv);
  return n + p;
}

// From void
float solid_angle_mapping_PDF(float theta_max)
{
  return 1.0 / (2.0 * M_PI * (1.0 - cos(theta_max)));
}

float uniform_hemisphere_PDF()
{
  return 1.0 / (2.0 * M_PI);
}

float cosine_weighted_hemisphere_PDF(float cosTheta)
{
  return cosTheta / M_PI;
}

#endif // MATH_H