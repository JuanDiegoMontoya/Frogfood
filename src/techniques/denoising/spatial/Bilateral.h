#pragma once
#include "PipelineManager.h"
#include "Fvog/Texture2.h"
#include "Fvog/Buffer2.h"

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/mat4x4.hpp"

namespace Techniques
{
  class Bilateral
  {
  public:
    Bilateral();

    struct DenoiseIlluminanceArgs
    {
      Fvog::Texture* sceneAlbedo;
      Fvog::Texture* sceneNormal;
      Fvog::Texture* sceneDepth;

      Fvog::Texture* sceneRadiance; // Direct lighting
      Fvog::Texture* sceneIlluminance; // Will be overwritten with 2+ passes
      Fvog::Texture* sceneIlluminancePingPong; // Always overwritten
      Fvog::Texture* sceneColor; // Output

      glm::mat4 clip_from_view; // proj
      glm::mat4 world_from_clip; // invViewProj
      glm::vec3 cameraPos; // TODO: unnecessary
    };

    void DenoiseIlluminance(const DenoiseIlluminanceArgs& args, VkCommandBuffer commandBuffer);

  private:
    PipelineManager::ComputePipelineKey bilateralPipeline;
    PipelineManager::ComputePipelineKey modulatePipeline;

    struct BilateralUniforms
    {
      glm::mat4 proj;
      glm::mat4 invViewProj;
      glm::vec3 viewPos;
      uint32_t _padding;
      glm::ivec2 targetDim;
      glm::ivec2 direction; // either (1, 0) or (0, 1)
      float phiNormal;
      float phiDepth;
    };

    Fvog::NDeviceBuffer<BilateralUniforms> bilateralUniformsBuffer_;
  };
} // namespace Techniques
