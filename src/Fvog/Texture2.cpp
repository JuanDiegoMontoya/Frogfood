#include "Texture2.h"
#include "Device.h"
#include "detail/Common.h"
#include "detail/ApiToEnum2.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <utility>

namespace Fvog
{
  Sampler::Sampler(Device& device, const SamplerCreateInfo& samplerState)
    : Sampler(device.samplerCache_.CreateOrGetCachedTextureSampler(samplerState))
  {
  }

  Texture::Texture(Device& device, const TextureCreateInfo& createInfo, std::string_view /*name*/)
    : device_(&device),
      createInfo_(createInfo)
  {
    using namespace detail;
    CheckVkResult(vmaCreateImage(
      device_->allocator_,
      Address(VkImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .imageType = ViewTypeToImageType(createInfo.imageViewType),
        .format = createInfo.format,
        .extent = createInfo.extent,
        .mipLevels = createInfo.mipLevels,
        .arrayLayers = createInfo.arrayLayers,
        .samples = createInfo.sampleCount,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = createInfo.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      }),
      Address(VmaAllocationCreateInfo{
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      }),
      &image_,
      &allocation_,
      nullptr
    ));

    auto aspectFlags = VkImageAspectFlags{};
    aspectFlags |= FormatIsDepth(createInfo.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    aspectFlags |= FormatIsStencil(createInfo.format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    aspectFlags |= FormatIsColor(createInfo.format) ? VK_IMAGE_ASPECT_COLOR_BIT : 0;

    CheckVkResult(vkCreateImageView(device_->device_, Address(VkImageViewCreateInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image_,
      .viewType = createInfo.imageViewType,
      .format = createInfo.format,
      .components = VkComponentMapping{VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
      .subresourceRange = VkImageSubresourceRange{
        .aspectMask = aspectFlags,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
      }
    }), nullptr, &imageView_));
  }

  Texture::~Texture()
  {
    if (image_ != VK_NULL_HANDLE)
    {
      device_->imageDeletionQueue_.emplace_back(device_->frameNumber, allocation_, image_, imageView_);
    }
  }

  Texture::Texture(Texture&& old) noexcept
    : device_(std::exchange(old.device_, nullptr)),
      createInfo_(std::exchange(old.createInfo_, {})),
      image_(std::exchange(old.image_, VK_NULL_HANDLE)),
      imageView_(std::exchange(old.imageView_, VK_NULL_HANDLE)),
      allocation_(std::exchange(old.allocation_, nullptr))
  {
  }

  Texture& Texture::operator=(Texture&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~Texture();
    return *new (this) Texture(std::move(old));
  }

  Texture CreateTexture2D(Device& device, VkExtent2D size, VkFormat format, VkImageUsageFlags usage, std::string_view name)
  {
    TextureCreateInfo createInfo{
      .imageViewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .extent = {size.width, size.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .sampleCount = VK_SAMPLE_COUNT_1_BIT,
      .usage = usage,
    };
    return Texture(device, createInfo, name);
  }

  Texture CreateTexture2DMip(Device& device, VkExtent2D size, VkFormat format, uint32_t mipLevels, VkImageUsageFlags usage, std::string_view name)
  {
    TextureCreateInfo createInfo{
      .imageViewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .extent = {size.width, size.height, 1},
      .mipLevels = mipLevels,
      .arrayLayers = 1,
      .sampleCount = VK_SAMPLE_COUNT_1_BIT,
      .usage = usage,
    };
    return Texture(device, createInfo, name);
  }
} // namespace Fvog