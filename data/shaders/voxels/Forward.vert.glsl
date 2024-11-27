#include "../Resources.h.glsl"
#include "../Utility.h.glsl"

struct Vertex
{
  vec3 position;
  vec3 normal;
  vec3 color;
};

FVOG_DECLARE_BUFFER_REFERENCE(VertexBuffer)
{
  Vertex vertices[];
};

FVOG_DECLARE_BUFFER_REFERENCE(FrameUniformsBuffer)
{
  mat4 clipFromWorld;
};

struct ObjectUniforms
{
  mat4 worldFromObject;
  VertexBuffer vertexBuffer;
  //FVOG_UINT32 materialId;
  //FVOG_UINT32 materialBufferIndex;
  //FVOG_UINT32 samplerIndex;
};

FVOG_DECLARE_BUFFER_REFERENCE(ObjectUniformsBuffer)
{
  ObjectUniforms uniforms[];
};

FVOG_DECLARE_ARGUMENTS(Args)
{
  ObjectUniformsBuffer objects;
  FrameUniformsBuffer frame;
}pc;

layout(location = 0) out vec3 o_color;
layout(location = 1) out vec3 o_normal;

void main()
{
  ObjectUniforms object = pc.objects.uniforms[gl_InstanceIndex];
  Vertex vertex = object.vertexBuffer.vertices[gl_VertexIndex];

  o_color = vertex.color;
  o_normal = vertex.normal;

  gl_Position = pc.frame.clipFromWorld * pc.objects.uniforms[gl_InstanceIndex].worldFromObject * vec4(vertex.position, 1.0);
}