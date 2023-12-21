#ifndef MATH_H
#define MATH_H

#include "Hash.h.glsl"

// Constants
const float M_PI = 3.141592654;

// Functions
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

#endif // MATH_H