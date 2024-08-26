#include "../Resources.h.glsl"
#include "../Utility.h.glsl"

struct Vertex
{
  vec3 position;
  uint normal;
  vec2 uv;
};

FVOG_DECLARE_BUFFER_REFERENCE(VertexBuffer)
{
  Vertex vertices[];
};

FVOG_DECLARE_STORAGE_BUFFERS(ArgsBuffers)
{
  mat4 clipFromWorld;
  mat4 worldFromObject;
  VertexBuffer vertexBuffer;
}argsBuffers[];

FVOG_DECLARE_ARGUMENTS(DebugForwardArgs)
{
  uint argsBufferIndex;
};

#define pc argsBuffers[argsBufferIndex]

layout(location = 0) out vec2 o_uv;
layout(location = 1) out vec3 o_normal;

void main()
{
  Vertex vertex = pc.vertexBuffer.vertices[gl_VertexIndex];

  o_uv = vertex.uv;
  o_normal = OctToVec3(unpackSnorm2x16(vertex.normal));

  gl_Position = pc.clipFromWorld * pc.worldFromObject * vec4(vertex.position, 1.0);
}