#pragma once
#include "Fvog/Texture2.h"
#include "Fvog/Buffer2.h"
#include "Fvog/Pipeline2.h"

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include <vulkan/vulkan_core.h>

#include <optional>

namespace Fvog
{
  class Device;
}

namespace Techniques
{
  class Atmosphere
  {
  public:
    // An atmosphere layer of width 'width', and whose density is defined as
    // 'exp_term' * exp('exp_scale' * h) + 'linear_term' * h + 'constant_term',
    // clamped to [0,1], and where h is the altitude.
    struct DensityProfileLayer
    {
      float width;
      float expTerm;
      float expScale;   // inverse length
      float linearTerm; // inverse length
      float constantTerm;
    };

    // An atmosphere density profile made of several layers on top of each other
    // (from bottom to top). The width of the last layer is ignored, i.e. it always
    // extend to the top atmosphere boundary. The profile values vary between 0
    // (null density) to 1 (maximum density).
    struct DensityProfile
    {
      DensityProfileLayer layers[2];
    };

    struct Settings
    {
      Fvog::Extent2D transmittanceLutDim;
      Fvog::Extent2D skyViewLutDim;
      Fvog::Extent2D aerialPerspectiveLutDim;
      Fvog::Extent3D multipleScatteringLutDim;

      uint32_t transmittanceSteps;
      uint32_t skyViewSteps;
      uint32_t aerialPerspectiveSteps;
      uint32_t multipleScatteringSteps;

      glm::vec3 cameraPos;
      glm::vec3 cameraDir;
      glm::mat4 view;
      glm::mat4 proj;
      glm::mat4 viewProj;
      glm::mat4 invViewProj;

      // https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/definitions.glsl.html
      glm::vec3 solarIrradiance; // ?
      float sunAngularRadius;
      float bottomRadius;
      float topRadius;
      DensityProfile rayleighDensity;
      glm::vec3 rayleighScattering;
      DensityProfile mieDensity;
      glm::vec3 mieScattering;
      glm::vec3 mieExtinction;
      float miePhaseFunctionG;
      DensityProfile absorptionDensity; // ozone
      glm::vec3 absorptionExtinction;
      glm::vec3 groundAlbedo;
      float mu_s_min; // see link above- solid choice is -0.2
    };

    explicit Atmosphere(Fvog::Device& device);

    void GenerateLuts(VkCommandBuffer commandBuffer, const Settings& settings);

    Fvog::Texture& GetTransmittanceLut()
    {
      return transmittanceLut_.value();
    }

    Fvog::Texture& GetSkyViewLut()
    {
      return skyViewLut_.value();
    }

    Fvog::Texture& GetAerialPerspectiveLut()
    {
      return aerialPerspectiveLut_.value();
    }

    Fvog::Texture& GetMultipleScatteringLut()
    {
      return multipleScatteringLut_.value();
    }

  private:
    Fvog::Device* device_;

    Settings settings_{};
    Fvog::TypedBuffer<Settings> settingsBuffer_;
    Fvog::ComputePipeline generateTransmittanceLutPipeline_;
    Fvog::ComputePipeline generateSkyViewLutPipeline_;
    Fvog::ComputePipeline generateAerialPerspectiveLutPipeline_;
    Fvog::ComputePipeline generateMultipleScattreingLutPipeline_;

    std::optional<Fvog::Texture> transmittanceLut_;
    std::optional<Fvog::Texture> skyViewLut_;
    std::optional<Fvog::Texture> aerialPerspectiveLut_;
    std::optional<Fvog::Texture> multipleScatteringLut_;

    constexpr static Fvog::Format transmittanceLutFormat      = Fvog::Format::B10G11R11_UFLOAT;
    constexpr static Fvog::Format skyViewLutFormat            = Fvog::Format::B10G11R11_UFLOAT;
    constexpr static Fvog::Format aerialPerspectiveLutFormat  = Fvog::Format::B10G11R11_UFLOAT;
    constexpr static Fvog::Format multipleScatteringLutFormat = Fvog::Format::B10G11R11_UFLOAT;

    void CreateTexturesIfSizeChanged();
  };
}