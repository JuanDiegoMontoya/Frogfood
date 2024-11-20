#include "Bloom.h"

#include "shaders/bloom/BloomCommon.h.glsl"
#include "Application.h"

#include "Fvog/Rendering2.h"
#include "Fvog/Shader2.h"

#include "../RendererUtilities.h"
#include "Voxels/Assets.h"

namespace Techniques
{
  Bloom::Bloom()
  {
    downsampleLowPassPipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name             = "Bloom Downsample (low-pass)",
      .shaderModuleInfo = PipelineManager::ShaderModuleCreateInfo{
        .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "bloom/BloomDownsampleLowPass.comp.glsl",
      },
    });

    downsamplePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "Bloom Downsample",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "bloom/BloomDownsample.comp.glsl",
        },
    });

    upsamplePipeline = GetPipelineManager().EnqueueCompileComputePipeline({
      .name = "Bloom Upsample",
      .shaderModuleInfo =
        PipelineManager::ShaderModuleCreateInfo{
          .stage = Fvog::PipelineStage::COMPUTE_SHADER,
          .path  = GetShaderDirectory() / "bloom/BloomUpsample.comp.glsl",
        },
    });
  }

  void Bloom::Apply(VkCommandBuffer commandBuffer, const ApplyParams& params)
  {
    assert(params.passes <= params.scratchTexture.GetCreateInfo().mipLevels && "Bloom target is too small for the number of passes");
    
    auto linearSampler = Fvog::Sampler({
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    }, "Linear Mirror Sampler");

    auto ctx = Fvog::Context(commandBuffer);
    auto marker = ctx.MakeScopedDebugMarker("Bloom");

    ctx.ImageBarrier(params.target, VK_IMAGE_LAYOUT_GENERAL);
    ctx.ImageBarrierDiscard(params.scratchTexture, VK_IMAGE_LAYOUT_GENERAL);

    {
      auto marker2 = ctx.MakeScopedDebugMarker("Downsample");
      for (uint32_t i = 0; i < params.passes; i++)
      {
        ctx.Barrier();

        Fvog::Extent2D sourceDim{};
        Fvog::Extent2D targetDim = params.target.GetCreateInfo().extent >> (i + 1);
        float sourceLod{};

        Fvog::Texture* sourceTex = nullptr;
      
        // We use a low-pass filter on the first downsample (mip0 -> mip1) to resolve flickering/temporal aliasing that occurs otherwise
        if (i == 0)
        {
          if (params.useLowPassFilterOnFirstPass)
          {
            ctx.BindComputePipeline(downsampleLowPassPipeline.GetPipeline());
          }
          else
          {
            ctx.BindComputePipeline(downsamplePipeline.GetPipeline());
          }
        
          sourceLod = 0;
          sourceTex = &params.target;
          sourceDim = params.target.GetCreateInfo().extent;
        }
        else
        {
          ctx.BindComputePipeline(downsamplePipeline.GetPipeline());

          sourceLod = static_cast<float>(i - 1);
          sourceTex = &params.scratchTexture;
          sourceDim = params.target.GetCreateInfo().extent >> i;
        }
      
        ctx.SetPushConstants(BloomUniforms{
          .sourceSampledImageIdx = sourceTex->ImageView().GetSampledResourceHandle().index,
          .targetStorageImageIdx = params.scratchTexture.CreateSingleMipView(i).GetStorageResourceHandle().index,
          .linearSamplerIdx = linearSampler.GetResourceHandle().index,
          .sourceDim = {sourceDim.width, sourceDim.height},
          .targetDim = {targetDim.width, targetDim.height},
          .sourceLod = sourceLod,
        });
      
        ctx.DispatchInvocations(targetDim.width, targetDim.height, 1);
      }
    }

    {
      auto marker2 = ctx.MakeScopedDebugMarker("Upsample");
      ctx.BindComputePipeline(upsamplePipeline.GetPipeline());
      for (int32_t i = params.passes - 1; i >= 0; i--)
      {
        ctx.Barrier();

        Fvog::Extent2D sourceDim = params.target.GetCreateInfo().extent >> (i + 1);
        Fvog::Extent2D targetDim{};
        Fvog::Texture* targetTex = nullptr;
        uint32_t targetLod{};

        // final pass
        if (i == 0)
        {
          targetLod = 0;
          targetTex = &params.target;
          targetDim = params.target.GetCreateInfo().extent;
        }
        else
        {
          targetLod = i - 1;
          targetTex = &params.scratchTexture;
          targetDim = params.target.GetCreateInfo().extent >> i;
        }

        ctx.SetPushConstants(BloomUniforms{
          .sourceSampledImageIdx = params.scratchTexture.ImageView().GetSampledResourceHandle().index,
          .targetSampledImageIdx = targetTex->ImageView().GetSampledResourceHandle().index,
          .targetStorageImageIdx = targetTex->CreateSingleMipView(targetLod).GetStorageResourceHandle().index,
          .linearSamplerIdx = linearSampler.GetResourceHandle().index,
          .sourceDim = {sourceDim.width, sourceDim.height},
          .targetDim = {targetDim.width, targetDim.height},
          .width = params.width,
          .strength = params.strength,
          .sourceLod = static_cast<float>(i),
          .targetLod = static_cast<float>(targetLod),
          .numPasses = params.passes,
          .isFinalPass = i == 0,
        });
      
        ctx.DispatchInvocations(targetDim.width, targetDim.height, 1);
      }
    }
  }
} // namespace Techniques