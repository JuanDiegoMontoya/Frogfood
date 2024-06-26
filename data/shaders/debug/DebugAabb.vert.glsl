#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "../GlobalUniforms.h.glsl"
#include "../Utility.h.glsl"
#include "DebugCommon.h.glsl"

FVOG_DECLARE_ARGUMENTS(DebugAabbArguments)
{
  FVOG_UINT32 globalUniformsIndex;
  FVOG_UINT32 debugAabbBufferIndex;
};

layout(location = 0) out vec4 v_color;

void main()
{
  DebugAabb box = debugAabbBuffers[debugAabbBufferIndex].aabbs[gl_InstanceIndex + gl_BaseInstance];
  v_color = PackedToVec4(box.color);

  vec3 a_pos = CreateCube(gl_VertexIndex) - 0.5;
  vec3 worldPos = a_pos * PackedToVec3(box.extent) + PackedToVec3(box.center);
  gl_Position = perFrameUniformsBuffers[globalUniformsIndex].viewProj * vec4(worldPos, 1.0);
}