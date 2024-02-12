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

    /// @brief Gets the handle of the underlying OpenGL sampler object
    /// @return The sampler
    [[nodiscard]] VkSampler Handle() const
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
    Texture(Device& device, const TextureCreateInfo& createInfo, std::string_view name = {});
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) noexcept; // TODO
    Texture& operator=(Texture&&) noexcept; // TODO

    [[nodiscard]] VkImage Image() const
    {
      return image_;
    }

    [[nodiscard]] VkImageView ImageView() const
    {
      return imageView_;
    }

    [[nodiscard]] TextureCreateInfo GetCreateInfo() const
    {
      return createInfo_;
    }

  private:
    Device& device_;
    TextureCreateInfo createInfo_;
    VkImage image_;
    VkImageView imageView_;
    VmaAllocation allocation_;
  };
}