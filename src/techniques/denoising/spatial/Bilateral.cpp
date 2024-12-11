#include "Bilateral.h"
#include "Fvog/Rendering2.h"
#include "Voxels/Assets.h"

namespace Techniques
{
  namespace
  {
    FVOG_DECLARE_ARGUMENTS(BilateralArgs)
    {
      FVOG_UINT32 bilateralUniformsIdx;
      shared::Texture2D sceneNormal;
      shared::Texture2D sceneIlluminance;
      shared::Texture2D sceneDepth;
      shared::Image2D sceneIlluminancePingPong; // Output
      float stepWidth;
    };

    FVOG_DECLARE_ARGUMENTS(ModulateArgs)
    {
      shared::Texture2D albedo;
      shared::Texture2D illuminance;
      shared::Image2D sceneColor;
    };
  }

  Bilateral::Bilateral()
  {
    bilateralPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name             = "Bilateral",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "denoising/spatial/Bilateral5x5.comp.glsl",
        },
    });

    modulatePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "Bilateral",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "denoising/spatial/Modulate.comp.glsl",
        },
    });
  }

  void Bilateral::DenoiseIlluminance(const DenoiseIlluminanceArgs& args, VkCommandBuffer commandBuffer)
  {
    assert(args.sceneAlbedo);
    assert(args.sceneNormal);
    assert(args.sceneDepth);
    assert(args.sceneIlluminance);
    assert(args.sceneIlluminancePingPong);
    assert(args.sceneColor);
    auto ctx = Fvog::Context(commandBuffer);

    ctx.Barrier();
    ctx.ImageBarrier(*args.sceneIlluminance, VK_IMAGE_LAYOUT_GENERAL);
    ctx.ImageBarrierDiscard(*args.sceneIlluminancePingPong, VK_IMAGE_LAYOUT_GENERAL);
    auto marker = ctx.MakeScopedDebugMarker("DenoiseIlluminance");

    {
      auto marker2 = ctx.MakeScopedDebugMarker("Bilateral filter");
      bilateralUniformsBuffer_.UpdateData(commandBuffer,
        BilateralUniforms{
          .proj        = args.clip_from_view,
          .invViewProj = args.world_from_clip,
          .viewPos     = args.cameraPos,
          .targetDim   = {args.sceneIlluminance->GetCreateInfo().extent.width, args.sceneIlluminance->GetCreateInfo().extent.height}, // TODO: unnecessary
          .direction   = {0, 0},
          .phiNormal   = 0.3f,
          .phiDepth    = 0.2f,
        });

      const int PASSES = 4;
      for (int i = 0; i < PASSES; i++)
      {
        ctx.BindComputePipeline(bilateralPipeline.GetPipeline());
        ctx.SetPushConstants(BilateralArgs{
          .bilateralUniformsIdx     = bilateralUniformsBuffer_.GetDeviceBuffer().GetResourceHandle().index,
          .sceneNormal              = args.sceneNormal->ImageView().GetTexture2D(),
          .sceneIlluminance         = args.sceneIlluminance->ImageView().GetTexture2D(),
          .sceneDepth               = args.sceneDepth->ImageView().GetTexture2D(),
          .sceneIlluminancePingPong = args.sceneIlluminancePingPong->ImageView().GetImage2D(),
          .stepWidth                = float(1 << i),
        });
        ctx.DispatchInvocations(args.sceneIlluminance->GetCreateInfo().extent);
        ctx.Barrier();
        std::swap(*args.sceneIlluminance, *args.sceneIlluminancePingPong);
      }
    }

    ctx.ImageBarrierDiscard(*args.sceneColor, VK_IMAGE_LAYOUT_GENERAL);

    {
      auto marker2 = ctx.MakeScopedDebugMarker("Modulate albedo");
      ctx.BindComputePipeline(modulatePipeline.GetPipeline());
      ctx.SetPushConstants(ModulateArgs{
        .albedo      = args.sceneAlbedo->ImageView().GetTexture2D(),
        .illuminance = args.sceneIlluminance->ImageView().GetTexture2D(),
        .sceneColor  = args.sceneColor->ImageView().GetImage2D(),
      });
      ctx.DispatchInvocations(args.sceneColor->GetCreateInfo().extent);
    }
  }
} // namespace Techniques
