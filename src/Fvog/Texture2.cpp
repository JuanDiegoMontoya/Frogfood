#include "Texture2.h"
#include "detail/Common.h"
#include "detail/ApiToEnum2.h"

#include <volk.h>

#include <vk_mem_alloc.h>

namespace Fvog
{
  Texture::Texture(Device& device, const TextureCreateInfo& createInfo, std::string_view /*name*/)
    : device_(device),
      createInfo_(createInfo)
  {
    using namespace detail;
    CheckVkResult(vmaCreateImage(
      device_.allocator_,
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

    CheckVkResult(vkCreateImageView(device_.device_, Address(VkImageViewCreateInfo{
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
      device_.imageDeletionQueue_.emplace_back(device_.frameNumber, allocation_, image_, imageView_);
    }
  }
} // namespace Fvog