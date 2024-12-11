//#define KERNEL_5x5
#include "Kernels.h.glsl"
#include "Common.h.glsl"
#include "../../Resources.h.glsl"
#include "../../Math.h.glsl"

FVOG_DECLARE_STORAGE_BUFFERS(BilateralUniformBuffers)
{
  mat4 proj;
  mat4 invViewProj;
  vec3 viewPos;
  uint _padding;
  ivec2 targetDim;
  ivec2 direction; // either (1, 0) or (0, 1)
  float phiNormal;
  float phiDepth;
}bilateralUniforms[];

FVOG_DECLARE_ARGUMENTS(BilateralArgs)
{
  FVOG_UINT32 bilateralUniformsIdx;
  Texture2D sceneNormal;
  Texture2D sceneIlluminance;
  Texture2D sceneDepth;
  Image2D sceneIlluminancePingPong;
  float stepWidth;
}pc;

#define uniforms bilateralUniforms[pc.bilateralUniformsIdx]

void AddFilterContribution(inout vec3 accumIlluminance,
  inout float accumWeight,
  vec3 cColor,
  vec3 cNormal,
  float cDepth,
  vec3 rayDir,
  ivec2 baseOffset,
  ivec2 offset,
  ivec2 kernelStep,
  float kernelWeight,
  ivec2 id,
  ivec2 gid)
{
  vec3 oColor = texelFetch(pc.sceneIlluminance, id, 0).rgb;
  vec3 oNormal = texelFetch(pc.sceneNormal, id, 0).xyz;
  float oDepth = texelFetch(pc.sceneDepth, id, 0).x;
  if (oDepth == 0)
  {
    return;
  }
  float phiDepth = offset == ivec2(0) ? 1.0 : length(vec2(baseOffset));
  phiDepth *= uniforms.phiDepth;

  float normalWeight = NormalWeight(oNormal, cNormal, uniforms.phiNormal);
  float depthWeight = DepthWeight(oDepth, cDepth, cNormal, rayDir, uniforms.proj, phiDepth);
  
  float weight = normalWeight * depthWeight;
  accumIlluminance += oColor * weight * kernelWeight;
  accumWeight += weight * kernelWeight;
}

layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
  ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(gid, uniforms.targetDim)))
  {
    return;
  }

  vec3 cColor = texelFetch(pc.sceneIlluminance, gid, 0).rgb;
  vec3 cNormal = texelFetch(pc.sceneNormal, gid, 0).xyz;
  float cDepth = texelFetch(pc.sceneDepth, gid, 0).x;
  if (cDepth == 0)
  {
    imageStore(pc.sceneIlluminancePingPong, gid, vec4(cColor, 0.0));
    return;
  }

  vec2 uv = (vec2(gid) + 0.5) / uniforms.targetDim;
  vec3 point = UnprojectUV_ZO(0.1, uv, uniforms.invViewProj);
  vec3 rayDir = normalize(point - uniforms.viewPos);

  vec3 accumIlluminance = vec3(0);
  float accumWeight = 0;

  if (uniforms.direction == ivec2(0))
  {
    for (int col = 0; col < kWidth; col++)
    {
      for (int row = 0; row < kWidth; row++)
      {
        ivec2 kernelStep = ivec2(pc.stepWidth);
        ivec2 baseOffset = ivec2(row - kRadius, col - kRadius);
        ivec2 offset = baseOffset * kernelStep;
        ivec2 id = gid + offset;
        
        if (any(greaterThanEqual(id, uniforms.targetDim)) || any(lessThan(id, ivec2(0))))
        {
          continue;
        }

        float kernelWeight = kernel[row][col];
        AddFilterContribution(accumIlluminance, accumWeight, cColor, cNormal, cDepth, rayDir, baseOffset, offset, kernelStep, kernelWeight, id, gid);
      }
    }
  }
  else
  {
    // Separable bilateral filter. Cheaper, but worse quality on edges
    for (int i = 0; i < kWidth; i++)
    {
      ivec2 kernelStep = ivec2(pc.stepWidth);
      ivec2 baseOffset = ivec2(i - kRadius) * uniforms.direction;
      ivec2 offset = baseOffset * kernelStep;
      ivec2 id = gid + offset;
      
      if (any(greaterThanEqual(id, uniforms.targetDim)) || any(lessThan(id, ivec2(0))))
      {
        continue;
      }

      float kernelWeight = kernel1D[i];
      AddFilterContribution(accumIlluminance, accumWeight, cColor, cNormal, cDepth, rayDir, baseOffset, offset, kernelStep, kernelWeight, id, gid);
    }
  }

  imageStore(pc.sceneIlluminancePingPong, gid, vec4(accumIlluminance / accumWeight, 0.0));
}