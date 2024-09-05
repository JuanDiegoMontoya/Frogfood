#include "RayTracedAO.h"
#include "Fvog/Rendering2.h"
#include "../../RendererUtilities.h"
#include "Fvog/AccelerationStructure.h"
#include "shaders/ao/rtao/RayTracedAO.comp.glsl"

namespace Techniques
{
  namespace
  {
    Fvog::ComputePipeline CreateRtaoPipeline(Fvog::Device& device)
    {
      auto cs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/ao/rtao/RayTracedAO.comp.glsl");

      return Fvog::ComputePipeline(device,
        {
          .name   = "Ray Traced AO",
          .shader = &cs,
        });
    }
  }

  RayTracedAO::RayTracedAO(Fvog::Device& device)
    : device_(&device),
      rtaoPipeline_(CreateRtaoPipeline(device))
  {
  }

  Fvog::Texture& RayTracedAO::ComputeAO(VkCommandBuffer commandBuffer, const ComputeParams& params)
  {
    assert(params.tlas);
    assert(params.inputDepth);

    auto ctx = Fvog::Context(*device_, commandBuffer);

    if (!aoTexture_ || Fvog::Extent2D(aoTexture_->GetCreateInfo().extent) != params.outputSize)
    {
      aoTexture_ = Fvog::CreateTexture2D(*device_, params.outputSize, Fvog::Format::R16_UNORM, Fvog::TextureUsage::GENERAL, "AO Texture");
    }

    ctx.ImageBarrierDiscard(aoTexture_.value(), VK_IMAGE_LAYOUT_GENERAL);
    ctx.BindComputePipeline(rtaoPipeline_);
    ctx.SetPushConstants(RtaoArguments{
      .tlasAddress          = params.tlas->GetAddress(),
      .gDepth               = params.inputDepth->ImageView().GetTexture2D(),
      .gNormalAndFaceNormal = params.inputNormalAndFaceNormal->ImageView().GetTexture2D(),
      .outputAo             = aoTexture_.value().ImageView().GetImage2D(),
      .world_from_clip      = params.world_from_clip,
      .numRays              = params.numRays,
      .rayLength            = params.rayLength,
      .frameNumber          = params.frameNumber,
    });
    ctx.DispatchInvocations(params.outputSize.width, params.outputSize.height, 1);
    ctx.Barrier();

    return aoTexture_.value();
  }
} // namespace Techniques
