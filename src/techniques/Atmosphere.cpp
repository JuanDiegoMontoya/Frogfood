#include <vulkan/vulkan_core.h>
#include "Atmosphere.h"

#include "RendererUtilities.h"

namespace Techniques
{
  namespace
  {
    Fvog::ComputePipeline CreateTransmittancePipeline(Fvog::Device& device)
    {
      auto cs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/atmosphere/PLACEHOLDER.comp.glsl");

      return Fvog::ComputePipeline(device,
        {
          .name   = "Generate Transmittance LUT Pipeline",
          .shader = &cs,
        });
    }

    Fvog::ComputePipeline CreateSkyViewPipeline(Fvog::Device& device)
    {
      auto cs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/atmosphere/PLACEHOLDER.comp.glsl");

      return Fvog::ComputePipeline(device,
        {
          .name   = "Generate Sky View LUT Pipeline",
          .shader = &cs,
        });
    }
    
    Fvog::ComputePipeline CreateAerialPerspectivePipeline(Fvog::Device& device)
    {
      auto cs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/atmosphere/PLACEHOLDER.comp.glsl");

      return Fvog::ComputePipeline(device,
        {
          .name   = "Generate Aerial Perspective LUT Pipeline",
          .shader = &cs,
        });
    }
    
    Fvog::ComputePipeline CreateMultipleScatteringPipeline(Fvog::Device& device)
    {
      auto cs = LoadShaderWithIncludes2(device, Fvog::PipelineStage::COMPUTE_SHADER, "shaders/atmosphere/PLACEHOLDER.comp.glsl");

      return Fvog::ComputePipeline(device,
        {
          .name   = "Generate Multiple Scattering LUT Pipeline",
          .shader = &cs,
        });
    }
  } // namespace

  Atmosphere::Atmosphere(Fvog::Device& device) 
    : device_(&device),
      settingsBuffer_(device, {}, "Atmosphere Settings Buffer"),
      generateTransmittanceLutPipeline_(CreateTransmittancePipeline(device)),
      generateSkyViewLutPipeline_(CreateSkyViewPipeline(device)),
      generateAerialPerspectiveLutPipeline_(CreateAerialPerspectivePipeline(device)),
      generateMultipleScattreingLutPipeline_(CreateMultipleScatteringPipeline(device))
  {
  }

  void Atmosphere::GenerateLuts([[maybe_unused]] VkCommandBuffer commandBuffer, [[maybe_unused]] const Settings& settings)
  {
    CreateTexturesIfSizeChanged();
  }

  void Atmosphere::CreateTexturesIfSizeChanged()
  {
    if (!transmittanceLut_ || Fvog::Extent2D(transmittanceLut_->GetCreateInfo().extent) != settings_.transmittanceLutDim)
    {
      transmittanceLut_ = Fvog::CreateTexture2D(*device_, settings_.transmittanceLutDim, transmittanceLutFormat, Fvog::TextureUsage::GENERAL, "Transmittance LUT");
    }
    
    if (!skyViewLut_ || Fvog::Extent2D(skyViewLut_->GetCreateInfo().extent) != settings_.skyViewLutDim)
    {
      skyViewLut_ = Fvog::CreateTexture2D(*device_, settings_.skyViewLutDim, skyViewLutFormat, Fvog::TextureUsage::GENERAL, "Sky-View LUT");
    }
    
    if (!aerialPerspectiveLut_ || Fvog::Extent2D(aerialPerspectiveLut_->GetCreateInfo().extent) != settings_.aerialPerspectiveLutDim)
    {
      aerialPerspectiveLut_ = Fvog::CreateTexture2D(*device_, settings_.aerialPerspectiveLutDim, aerialPerspectiveLutFormat, Fvog::TextureUsage::GENERAL, "Aerial Perspective LUT");
    }
    
    if (!multipleScatteringLut_ || multipleScatteringLut_->GetCreateInfo().extent != settings_.multipleScatteringLutDim)
    {
      multipleScatteringLut_ = Fvog::Texture(*device_, Fvog::TextureCreateInfo{
        .viewType = VK_IMAGE_VIEW_TYPE_3D,
        .format = multipleScatteringLutFormat,
        .extent = settings_.multipleScatteringLutDim,
        .mipLevels = 1,
        .arrayLayers = 1,
        .sampleCount = VK_SAMPLE_COUNT_1_BIT,
        .usage = Fvog::TextureUsage::GENERAL,
      }, "Multiple Scattering LUT");
    }
  }
}
