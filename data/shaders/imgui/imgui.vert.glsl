#version 450 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#define COLOR_SPACE_sRGB_NONLINEAR 0
#define COLOR_SPACE_scRGB_LINEAR 1
#define COLOR_SPACE_HDR10_ST2084 2
#define COLOR_SPACE_BT2020_LINEAR 3

layout(push_constant, scalar) uniform PushConstants
{
  uint vertexBufferIndex;
  uint textureIndex;
  uint samplerIndex;
  uint displayColorSpace;
  vec2 scale;
  vec2 translation;
} pc;

struct Vertex
{
  vec2 position;
  vec2 texcoord;
  uint color;
};

layout(set = 0, binding = 0, scalar) readonly buffer VertexBuffer
{
  Vertex vertices[];
}buffers[];

out gl_PerVertex { vec4 gl_Position; };
layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;
void main()
{
  Vertex vertex = buffers[pc.vertexBufferIndex].vertices[gl_VertexIndex];
  Out.Color = unpackUnorm4x8(vertex.color);
  Out.UV = vertex.texcoord;
  gl_Position = vec4(vertex.position * pc.scale + pc.translation, 0, 1);
}