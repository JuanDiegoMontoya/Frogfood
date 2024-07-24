#pragma once

#include "BasicTypes2.h"
#include "Device.h"

#include <vulkan/vulkan_core.h>

#include <array>
#include <string>
#include <string_view>
#include <optional>
#include <memory>

typedef struct VmaAllocation_T* VmaAllocation;

namespace Fvog
{
  class Device;
  class TextureView;
  class Texture;
  class Buffer;

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

  enum class TextureUsage
  {
    GENERAL,              // Compatible with everything (e.g., use for written storage images)
    READ_ONLY,            // Ideal for loaded materials
    ATTACHMENT_READ_ONLY, // Ideal for gbuffer attachments that are later read
  };

  struct TextureCreateInfo
  {
    VkImageViewType viewType = {};
    Format format = {};
    Extent3D extent = {};
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    TextureUsage usage = TextureUsage::GENERAL;
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
    Offset3D offset = {};
    Extent3D extent = {};
    const void* data = nullptr;

    /// @brief Specifies, in texels, the size of rows in the array (for 2D and 3D images). If zero, it is assumed to be tightly packed according to size
    uint32_t rowLength = 0;

    /// @brief Specifies, in texels, the number of rows in the array (for 3D images. If zero, it is assumed to be tightly packed according to size
    uint32_t imageHeight = 0;
  };

  class Sampler
  {
  public:
    // Note: name MAY be ignored if samplerState matches a cached entry
    explicit Sampler(Device& device, const SamplerCreateInfo& samplerState, std::string name = {});
    
    [[nodiscard]] VkSampler Handle() const noexcept
    {
      return sampler_;
    }

    Device::DescriptorInfo::ResourceHandle GetResourceHandle() const noexcept
    {
      return descriptorInfo_->GpuResource();
    }

  private:
    friend class detail::SamplerCache;

    explicit Sampler(VkSampler sampler, Device::DescriptorInfo& descriptorInfo)
      : sampler_(sampler),
        descriptorInfo_(&descriptorInfo) {}
    
    VkSampler sampler_{};
    Device::DescriptorInfo* descriptorInfo_;
  };

  class TextureView
  {
  public:
    explicit TextureView(Device& device, const Texture& texture, const TextureViewCreateInfo& createInfo, std::string name = {});
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

    //[[nodiscard]] const Texture* GetTexture() const noexcept
    //{
    //  return texture_;
    //}

    operator VkImageView() const noexcept
    {
      return imageView_;
    }

    [[nodiscard]] Device::DescriptorInfo::ResourceHandle GetSampledResourceHandle() noexcept
    {
      return sampledDescriptorInfo_.value().GpuResource();
    }

    [[nodiscard]] Device::DescriptorInfo::ResourceHandle GetStorageResourceHandle() noexcept
    {
      return storageDescriptorInfo_.value().GpuResource();
    }

    [[nodiscard]] TextureCreateInfo GetTextureCreateInfo() const noexcept
    {
      return parentCreateInfo_;
    }

    [[nodiscard]] VkImage Image() noexcept
    {
      return image_;
    }

    // Non-owning, becomes dangling if owning Texture is destroyed first
    VkImageLayout* currentLayout{};

  private:
    Device* device_{};
    TextureViewCreateInfo createInfo_{};
    VkImageView imageView_{};
    std::optional<Device::DescriptorInfo> sampledDescriptorInfo_;
    std::optional<Device::DescriptorInfo> storageDescriptorInfo_;

    // Duplicate of parent data to avoid potential dangling pointers
    TextureCreateInfo parentCreateInfo_{};
    VkImage image_{};

    std::string name_;
  };

  class Texture
  {
  public:
    // Verbose constructor
    explicit Texture(Device& device, const TextureCreateInfo& createInfo, std::string name = {});
    ~Texture();

    [[nodiscard]] TextureView CreateFormatView(Format format, std::string name = {}) const;

    // Returns a cached view of a single mip
    [[nodiscard]] TextureView& CreateSingleMipView(uint32_t level, std::string name = {});

    [[nodiscard]] TextureView CreateSwizzleView(VkComponentMapping components, std::string name = {});

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

    [[nodiscard]] TextureView& ImageView() noexcept
    {
      return textureView_.value();
    }

    [[nodiscard]] TextureCreateInfo GetCreateInfo() const noexcept
    {
      return createInfo_;
    }

    [[nodiscard]] bool operator==(const Texture& other) const noexcept
    {
      return image_ == other.image_;
    }

    // TODO: Make layout per subresource and track it in the command buffer
    std::unique_ptr<VkImageLayout> currentLayout{};

  private:
    Device* device_{};
    TextureCreateInfo createInfo_{};
    VkImage image_{};
    std::optional<TextureView> textureView_;
    std::array<std::optional<TextureView>, 14> singleMipViews_;
    VmaAllocation allocation_{};
    std::string name_;
  };

  // convenience functions
  [[nodiscard]] Texture CreateTexture2D(Device& device, VkExtent2D size, Format format, TextureUsage usage, std::string name = {});
  [[nodiscard]] Texture CreateTexture2DMip(Device& device, VkExtent2D size, Format format, uint32_t mipLevels, TextureUsage usage, std::string name = {});
}