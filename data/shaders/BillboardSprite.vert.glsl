#version 460 core

#include "Resources.h.glsl"
#include "GlobalUniforms.h.glsl"

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out Texture2D v_texture;
layout(location = 2) out vec3 v_tint;

// vertices in [0, 1]
vec2 CreateQuad(uint vertexID) // triangle list
{
  uint b = 1 << vertexID;
  return vec2((0x32 & b) != 0, (0x26 & b) != 0);
}

struct BillboardSpriteInstance
{
  vec3 position;
  vec2 scale;
  vec3 tint;
  Texture2D texture;
};

FVOG_DECLARE_STORAGE_BUFFERS_2(BillboardUniforms)
{
  BillboardSpriteInstance instances[];
}billboardBuffers[];

FVOG_DECLARE_ARGUMENTS(BillboardPushConstants)
{
  FVOG_UINT32 billboardsIndex;
  FVOG_UINT32 globalUniformsIndex;
  FVOG_VEC3 cameraRight;
  FVOG_VEC3 cameraUp;
  Sampler texSampler;
}pc;

void main()
{
  v_uv = CreateQuad(gl_VertexIndex % 6);
  vec2 aPos = v_uv - 0.5;

  const int index = gl_VertexIndex / 6;

  const BillboardSpriteInstance instance = billboardBuffers[pc.billboardsIndex].instances[index];
  v_texture = instance.texture;
  v_tint    = instance.tint;

  vec3 vertexPosition_worldspace =
    instance.position +
    pc.cameraRight * aPos.x * instance.scale.x +
    pc.cameraUp * aPos.y * instance.scale.y;

  const mat4 clip_from_world = perFrameUniformsBuffers[pc.globalUniformsIndex].viewProj;
  gl_Position = clip_from_world * vec4(vertexPosition_worldspace, 1.0);
}
