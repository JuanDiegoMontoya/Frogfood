#ifndef UTILITY_H
#define UTILITY_H

vec3 OctToFloat32x3(vec2 e)
{
  vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
  vec2 signNotZero = vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
  if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * signNotZero;
  return normalize(v);
}

#endif // UTILITY_H