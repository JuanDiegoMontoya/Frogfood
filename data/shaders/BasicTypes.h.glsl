#ifndef BASIC_TYPES_H
#define BASIC_TYPES_H

struct DrawElementsIndirectCommand
{
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int baseVertex;
  uint baseInstance;
};

struct DrawIndirectCommand
{
  uint vertexCount;
  uint instanceCount;
  uint firstVertex;
  uint firstInstance;
};

// Packed vector types with scalar alignment
struct PackedVec2
{
  float x, y;
};

struct PackedVec3
{
  float x, y, z;
};

struct PackedVec4
{
  float x, y, z, w;
};

vec2 PackedToVec2(in PackedVec2 v)
{
  return vec2(v.x, v.y);
}

PackedVec2 Vec2ToPacked(in vec2 v)
{
  return PackedVec2(v.x, v.y);
}

vec3 PackedToVec3(in PackedVec3 v)
{
  return vec3(v.x, v.y, v.z);
}

PackedVec3 Vec3ToPacked(in vec3 v)
{
  return PackedVec3(v.x, v.y, v.z);
}

vec4 PackedToVec4(in PackedVec4 v)
{
  return vec4(v.x, v.y, v.z, v.w);
}

PackedVec4 Vec4ToPacked(in vec4 v)
{
  return PackedVec4(v.x, v.y, v.z, v.w);
}

#endif // BASIC_TYPES_H