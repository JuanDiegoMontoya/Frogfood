#include "Forward.h.glsl"


layout(location = 0) out vec3 o_color;
layout(location = 1) out vec3 o_normal;
layout(location = 2) out vec3 o_worldPosition;

void main()
{
  ObjectUniforms object = pc.objects.uniforms[gl_InstanceIndex];
  Vertex vertex = object.vertexBuffer.vertices[gl_VertexIndex];

  o_color = vertex.color;
  o_normal = vertex.normal;
  const vec4 worldPos = pc.objects.uniforms[gl_InstanceIndex].worldFromObject * vec4(vertex.position, 1.0);
  o_worldPosition = worldPos.xyz;

  gl_Position = pc.frame.clipFromWorld * worldPos;
}