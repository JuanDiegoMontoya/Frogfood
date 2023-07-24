#version 450 core

layout (binding = 0, std140) uniform PerFrameUniforms
{
  mat4 viewProj;
  mat4 oldViewProjUnjittered;
  mat4 viewProjUnjittered;
  mat4 invViewProj;
  mat4 proj;
  vec4 cameraPos;
  vec4 frustumPlanes[6];
  uint meshletCount;
  float bindlessSamplerLodBias;
};

layout(location = 0) in vec3 a_pos;

void main()
{
  gl_Position = viewProj * vec4(a_pos, 1.0);
}