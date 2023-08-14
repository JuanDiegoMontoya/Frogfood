#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "../GlobalUniforms.h.glsl"
#include "../Utility.h.glsl"
#include "DebugCommon.h.glsl"

layout(location = 0) out vec4 v_color;

void main()
{
  DebugRect rect = debugRectBuffer.rects[gl_InstanceID + gl_BaseInstance];
  v_color = PackedToVec4(rect.color);

  PackedVec2 uvPos;
  if (gl_VertexID == 0)
    uvPos = rect.minOffset;
  else if (gl_VertexID == 1)
    uvPos = PackedVec2(rect.maxOffset.x, rect.minOffset.y);
  else if (gl_VertexID == 2)
    uvPos = rect.maxOffset;
  else
    uvPos = PackedVec2(rect.minOffset.x, rect.maxOffset.y);
  
  vec2 clip = PackedToVec2(uvPos) * 2.0 - 1.0;

  gl_Position = vec4(clip, rect.depth, 1.0);
}