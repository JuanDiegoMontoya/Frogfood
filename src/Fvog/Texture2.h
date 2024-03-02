#pragma once

#include <vulkan/vulkan_core.h>

#include <string_view>

typedef struct VmaAllocation_T* VmaAllocation;

namespace Fvog
{
  class Device;

  namespace detail
  {
    class SamplerCache;
  }

  struct SamplerCreateInfo
  {
    VkFilter magFilter = VK_FILTER_NEAREST;
    VkFilter minFilter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float mipLodBias = 0;
    float maxAnisotropy = 0;
    VkBool32 compareEnable = VK_FALSE;
    VkCompareOp compareOp = VK_COMPARE_OP_NEVER;
    float minLod = -1000;
    float maxLod = 1000;
    VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

    bool operator==(const SamplerCreateInfo&) const noexcept = default;
  };

  class Sampler
  {
  public:
    explicit Sampler(Device& device, const SamplerCreateInfo& samplerState);
    
    [[nodiscard]] VkSampler Handle() const noexcept
    {
      return sampler_;
    }

  private:
    friend class detail::SamplerCache;
    //Sampler() = default; // you cannot create samplers out of thin air
    explicit Sampler(VkSampler sampler) : sampler_(sampler) {}
    
    VkSampler sampler_{};
  };

  struct TextureCreateInfo
  {
    VkImageViewType imageViewType = {};
    VkFormat format = {};
    VkExtent3D extent = {};
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = {};
  };

  class Texture
  {
  public:
    // Verbose constructor
    explicit Texture(Device& device, const TextureCreateInfo& createInfo, std::string_view name = {});
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) noexcept;

    [[nodiscard]] VkImage Image() const noexcept
    {
      return image_;
    }

    [[nodiscard]] VkImageView ImageView() const noexcept
    {
      return imageView_;
    }

    [[nodiscard]] TextureCreateInfo GetCreateInfo() const noexcept
    {
      return createInfo_;
    }

  private:
    Device* device_{};
    TextureCreateInfo createInfo_{};
    VkImage image_{};
    VkImageView imageView_{};
    VmaAllocation allocation_{};
  };

  // convenience functions
  Texture CreateTexture2D(Device& device, VkExtent2D size, VkFormat format, VkImageUsageFlags usage, std::string_view name = "");
  Texture CreateTexture2DMip(Device& device, VkExtent2D size, VkFormat format, uint32_t mipLevels, VkImageUsageFlags usage, std::string_view name = "");
}