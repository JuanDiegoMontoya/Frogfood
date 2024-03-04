#include "Texture2.h"
#include "Buffer2.h"
#include "Rendering2.h"
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
        .imageType = ViewTypeToImageType(createInfo.viewType),
        .format = detail::FormatToVk(createInfo.format),
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
      .viewType = createInfo.viewType,
      .format = detail::FormatToVk(createInfo.format),
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
    if (device_ != nullptr && image_ != VK_NULL_HANDLE)
    {
      device_->imageDeletionQueue_.emplace_back(device_->frameNumber, allocation_, image_, imageView_);
    }
  }

  TextureView Texture::CreateFormatView(Format format, std::string_view /*name*/) const
  {
    return TextureView(*device_, *this, {
      .viewType = createInfo_.viewType,
      .format = format,
      // TODO: aspect mask is hardcoded to color. How important are depth/stencil views of different formats? Should at least assert that this has a color format
    });
  }

  void Texture::UpdateImageSLOW(const TextureUpdateInfo& info)
  {
    device_->ImmediateSubmit(
    [this, &info](VkCommandBuffer commandBuffer)
    {
      size_t size;
      if (detail::FormatIsBlockCompressed(createInfo_.format))
      {
        size = detail::BlockCompressedImageSize(createInfo_.format, info.extent.width, info.extent.height, info.extent.depth);
      }
      else
      {
        size = info.extent.width * info.extent.height * info.extent.depth * detail::FormatStorageSize(createInfo_.format);
      }
      auto uploadBuffer = Buffer(*this->device_, {.size = size, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE});
      // TODO: account for row length and image height here
      std::memcpy(uploadBuffer.GetMappedMemory(), info.data, size);
      auto ctx = Fvog::Context(commandBuffer);
      ctx.ImageBarrier(*this, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      vkCmdCopyBufferToImage2(commandBuffer, detail::Address(VkCopyBufferToImageInfo2{
        .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
        .srcBuffer = uploadBuffer.Handle(),
        .dstImage = image_,
        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .regionCount = 1,
        .pRegions = detail::Address(VkBufferImageCopy2{
          .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
          .bufferOffset = 0,
          .bufferRowLength = info.rowLength,
          .bufferImageHeight = info.imageHeight,
          .imageSubresource = VkImageSubresourceLayers{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
          },
          .imageOffset = info.offset,
          .imageExtent = info.extent,
        }),
      }));
      ctx.ImageBarrier(*this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    });
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

  TextureView::TextureView(Device& device, const Texture& texture, const TextureViewCreateInfo& createInfo, std::string_view)
    : device_(&device),
      texture_(&texture),
      createInfo_(createInfo)
  {
    detail::CheckVkResult(vkCreateImageView(device.device_, detail::Address(VkImageViewCreateInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = texture.Image(),
      .viewType = createInfo.viewType,
      .format = detail::FormatToVk(createInfo.format),
      .components = createInfo.components,
      .subresourceRange = createInfo.subresourceRange,
    }), nullptr, &imageView_));
  }

  TextureView::~TextureView()
  {
    if (device_ != nullptr && imageView_ != VK_NULL_HANDLE)
    {
      // It's safe to pass VMA null allocators and/or handles, so we can reuse the image deletion queue here
      device_->imageDeletionQueue_.emplace_back(device_->frameNumber, nullptr, VK_NULL_HANDLE, imageView_);
    }
  }

  TextureView::TextureView(TextureView&& old) noexcept
    : device_(std::exchange(old.device_, nullptr)),
      texture_(std::exchange(old.texture_, nullptr)),
      createInfo_(std::exchange(old.createInfo_, {})),
      imageView_(std::exchange(old.imageView_, VK_NULL_HANDLE))
  {}

  TextureView& TextureView::operator=(TextureView&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~TextureView();
    return *new (this) TextureView(std::move(old));
  }

  Texture CreateTexture2D(Device& device, VkExtent2D size, Format format, VkImageUsageFlags usage, std::string_view name)
  {
    TextureCreateInfo createInfo{
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .extent = {size.width, size.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .sampleCount = VK_SAMPLE_COUNT_1_BIT,
      .usage = usage,
    };
    return Texture(device, createInfo, name);
  }

  Texture CreateTexture2DMip(Device& device, VkExtent2D size, Format format, uint32_t mipLevels, VkImageUsageFlags usage, std::string_view name)
  {
    TextureCreateInfo createInfo{
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
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