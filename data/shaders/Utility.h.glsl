#ifndef UTILITY_H
#define UTILITY_H

vec3 OctToFloat32x3(vec2 e)
{
  vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
  vec2 signNotZero = vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
  if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * signNotZero;
  return normalize(v);
}

// 14-vertex CCW triangle strip in [0, 1]
vec3 CreateCube(in uint vertexID)
{
  uint b = 1u << vertexID;
  return vec3((0x287au & b) != 0u, (0x02afu & b) != 0u, (0x31e3u & b) != 0u);
}

vec3 hsv_to_rgb(in vec3 hsv)
{
  vec3 rgb = clamp(abs(mod(hsv.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
  return hsv.z * mix(vec3(1.0), rgb, hsv.y);
}

// Gets the linear luminance, spectrally weighted for human perception, of a tristimulus value
float luminance(vec3 c)
{
  return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

#endif // UTILITY_H