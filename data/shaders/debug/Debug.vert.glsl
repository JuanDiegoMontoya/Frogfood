#version 460 core

#extension GL_GOOGLE_include_directive : enable

#include "../Resources.h.glsl"
#include "../GlobalUniforms.h.glsl"
#include "../BasicTypes.h.glsl"

struct Vertex
{
  PackedVec3 position;
  PackedVec4 color;
};

FVOG_DECLARE_STORAGE_BUFFERS(VertexBuffers)
{
  Vertex vertices[];
}vertexBuffers[];

FVOG_DECLARE_ARGUMENTS(DebugLinesPushConstants)
{
  FVOG_UINT32 vertexBufferIndex;
  FVOG_UINT32 globalUniformsIndex;
};

layout(location = 0) out vec4 v_color;

void main()
{
  Vertex vertex = vertexBuffers[vertexBufferIndex].vertices[gl_VertexIndex];
  v_color = PackedToVec4(vertex.color);
  gl_Position = perFrameUniformsBuffers[globalUniformsIndex].viewProj * vec4(PackedToVec3(vertex.position), 1.0);
}