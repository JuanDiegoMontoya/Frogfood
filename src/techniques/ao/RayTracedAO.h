#pragma once
#include <vulkan/vulkan_core.h>
#include "Fvog/Texture2.h"
#include "Fvog/Pipeline2.h"
#include "PipelineManager.h"

#include <glm/mat4x4.hpp>

#include <optional>

namespace Fvog
{
  class Device;
  class Tlas;
}

namespace Techniques
{
  class RayTracedAO
  {
  public:
    RayTracedAO();

    struct ComputeParams
    {
      Fvog::Tlas* tlas;
      Fvog::Texture* inputDepth;
      Fvog::Texture* inputNormalAndFaceNormal;
      Fvog::Extent2D outputSize;
      uint32_t numRays{5};
      float rayLength{1};
      glm::mat4 world_from_clip;
      uint32_t frameNumber{};

      // TODO: scale factor and denoising params
    };

    [[nodiscard]] Fvog::Texture& ComputeAO(VkCommandBuffer commandBuffer, const ComputeParams& params);

  private:
    PipelineManager::ComputePipelineKey rtaoPipeline_;
    std::optional<Fvog::Texture> aoTexture_;
  };
}