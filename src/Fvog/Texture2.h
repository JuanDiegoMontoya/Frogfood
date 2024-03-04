#pragma once

#include "BasicTypes2.h"

#include <vulkan/vulkan_core.h>

#include <string_view>

typedef struct VmaAllocation_T* VmaAllocation;

namespace Fvog
{
  class Device;
  class TextureView;

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

  struct TextureCreateInfo
  {
    VkImageViewType viewType = {};
    Format format = {};
    VkExtent3D extent = {};
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = {};
  };

  struct TextureViewCreateInfo
  {
    VkImageViewType viewType = {};
    Format format = {};
    VkComponentMapping components = {};
    VkImageSubresourceRange subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .levelCount = VK_REMAINING_MIP_LEVELS,
      .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };
  };

  /// @brief Parameters for Texture::UpdateImage
  struct TextureUpdateInfo
  {
    uint32_t level = 0;
    VkOffset3D offset = {};
    VkExtent3D extent = {};
    const void* data = nullptr;

    /// @brief Specifies, in texels, the size of rows in the array (for 2D and 3D images). If zero, it is assumed to be tightly packed according to size
    uint32_t rowLength = 0;

    /// @brief Specifies, in texels, the number of rows in the array (for 3D images. If zero, it is assumed to be tightly packed according to size
    uint32_t imageHeight = 0;
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

  class Texture
  {
  public:
    // Verbose constructor
    explicit Texture(Device& device, const TextureCreateInfo& createInfo, std::string_view name = {});
    ~Texture();

    TextureView CreateFormatView(Format format, std::string_view name = "") const;

    /// @brief Updates a subresource of the image
    /// @param info The subresource and data to upload
    /// @note info.data must be in a compatible image format
    /// @note This function is provided for backwards compatibility only
    void UpdateImageSLOW(const TextureUpdateInfo& info);

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

  class TextureView
  {
  public:
    explicit TextureView(Device& device, const Texture& texture, const TextureViewCreateInfo& createInfo, std::string_view name = "");
    ~TextureView();

    TextureView(const TextureView&) = delete;
    TextureView& operator=(const TextureView&) = delete;
    TextureView(TextureView&&) noexcept;
    TextureView& operator=(TextureView&&) noexcept;

    [[nodiscard]] VkImageView ImageView() const noexcept
    {
      return imageView_;
    }

    [[nodiscard]] TextureViewCreateInfo GetViewCreateInfo() const noexcept
    {
      return createInfo_;
    }

    [[nodiscard]] const Texture* GetTexture() const noexcept
    {
      return texture_;
    }

  private:
    Device* device_{};
    const Texture* texture_{};
    TextureViewCreateInfo createInfo_{};
    VkImageView imageView_{};
  };

  // convenience functions
  Texture CreateTexture2D(Device& device, VkExtent2D size, Format format, VkImageUsageFlags usage, std::string_view name = "");
  Texture CreateTexture2DMip(Device& device, VkExtent2D size, Format format, uint32_t mipLevels, VkImageUsageFlags usage, std::string_view name = "");
}