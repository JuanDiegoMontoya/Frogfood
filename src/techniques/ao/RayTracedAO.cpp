#include "RayTracedAO.h"
#include "Application.h"
#include "Fvog/Rendering2.h"
#include "../../RendererUtilities.h"
#include "Fvog/AccelerationStructure.h"
#include "Voxels/Assets.h"
#include "shaders/ao/rtao/RayTracedAO.comp.glsl"

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

namespace Techniques
{
  RayTracedAO::RayTracedAO()
  {
    rtaoPipeline_ = GetPipelineManager().EnqueueCompileComputePipeline({
      .name             = "Ray Traced AO",
      .shaderModuleInfo = {.path = GetShaderDirectory() / "ao/rtao/RayTracedAO.comp.glsl"},
    });
  }

  Fvog::Texture& RayTracedAO::ComputeAO(VkCommandBuffer commandBuffer, const ComputeParams& params)
  {
    assert(params.tlas);
    assert(params.inputDepth);

    auto ctx = Fvog::Context(commandBuffer);

    if (!aoTexture_ || Fvog::Extent2D(aoTexture_->GetCreateInfo().extent) != params.outputSize)
    {
      aoTexture_ = Fvog::CreateTexture2D(params.outputSize, Fvog::Format::R16_UNORM, Fvog::TextureUsage::GENERAL, "AO Texture");
    }

    ctx.ImageBarrierDiscard(aoTexture_.value(), VK_IMAGE_LAYOUT_GENERAL);
    auto marker = ctx.MakeScopedDebugMarker("Ray Traced AO");
    ctx.BindComputePipeline(rtaoPipeline_.GetPipeline());
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
