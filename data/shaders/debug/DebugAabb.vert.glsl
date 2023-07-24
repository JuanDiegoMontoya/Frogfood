#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "../GlobalUniforms.h.glsl"
#include "../Utility.h.glsl"
#include "DebugCommon.h.glsl"

layout(location = 0) out vec4 v_color;

void main()
{
  DebugAabb box = debugAabbBuffer.aabbs[gl_InstanceID + gl_BaseInstance];
  v_color = PackedToVec4(box.color);

  vec3 a_pos = CreateCube(gl_VertexID) - 0.5;
  vec3 worldPos = a_pos * PackedToVec3(box.extent) + PackedToVec3(box.center);
  gl_Position = perFrameUniforms.viewProj * vec4(worldPos, 1.0);
}