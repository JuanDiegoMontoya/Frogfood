#ifndef FORWARD_H_GLSL
#define FORWARD_H_GLSL
#include "../Resources.h.glsl"
#include "../Utility.h.glsl"
#include "Voxels.h.glsl"

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
  Voxels voxels;
  Texture2D noiseTexture;
}pc;

#endif // FORWARD_H_GLSL