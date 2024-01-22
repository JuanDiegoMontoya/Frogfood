#pragma once

#include "Device.h"
#include <vk_mem_alloc.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include <string_view>

namespace Fvog
{
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
    Texture(Texture&&) = delete; // TODO
    Texture& operator=(Texture&&) = delete; // TODO

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