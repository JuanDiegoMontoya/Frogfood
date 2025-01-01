#version 460 core

#include "Resources.h.glsl"
#include "GlobalUniforms.h.glsl"

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_leftColor;
layout(location = 2) out vec4 v_rightColor;
layout(location = 3) out float v_middle;

// vertices in [0, 1]
vec2 CreateQuad(uint vertexID) // triangle list
{
  uint b = 1 << vertexID;
  return vec2((0x32 & b) != 0, (0x26 & b) != 0);
}

struct BillboardInstance
{
  vec3 position;
  vec2 scale;
  vec4 leftColor;
  vec4 rightColor;
  float middle;
};

FVOG_DECLARE_STORAGE_BUFFERS_2(BillboardUniforms)
{
  BillboardInstance instances[];
}billboardBuffers[];

FVOG_DECLARE_ARGUMENTS(BillboardPushConstants)
{
  FVOG_UINT32 billboardsIndex;
  FVOG_UINT32 globalUniformsIndex;
  FVOG_VEC3 cameraRight;
  FVOG_VEC3 cameraUp;
}pc;

void main()
{
  v_uv = CreateQuad(gl_VertexIndex % 6);
  vec2 aPos = v_uv - 0.5;

  const int index = gl_VertexIndex / 6;

  const BillboardInstance instance = billboardBuffers[pc.billboardsIndex].instances[index];
  v_leftColor = instance.leftColor;
  v_rightColor = instance.rightColor;
  v_middle = instance.middle;

  vec3 vertexPosition_worldspace =
    instance.position +
    pc.cameraRight * aPos.x * instance.scale.x +
    pc.cameraUp * aPos.y * instance.scale.y;

  const mat4 clip_from_world = perFrameUniformsBuffers[pc.globalUniformsIndex].viewProj;
  gl_Position = clip_from_world * vec4(vertexPosition_worldspace, 1.0);
}
