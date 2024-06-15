#include <vulkan/vulkan_core.h>
#include "Texture2.h"
#include "Buffer2.h"
#include "Rendering2.h"
#include "Device.h"
#include "detail/Common.h"
#include "detail/ApiToEnum2.h"
#include "detail/SamplerCache2.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <tracy/Tracy.hpp>

#include <utility>

namespace Fvog
{
  Sampler::Sampler(Device& device, const SamplerCreateInfo& samplerState, std::string name)
    : Sampler(device.samplerCache_->CreateOrGetCachedTextureSampler(samplerState, std::move(name)))
  {
  }

  Texture::Texture(Device& device, const TextureCreateInfo& createInfo, std::string name)
    : currentLayout(std::make_unique<VkImageLayout>(VK_IMAGE_LAYOUT_UNDEFINED)),
      device_(&device),
      createInfo_(createInfo),
      name_(std::move(name))
  {
    using namespace detail;
    ZoneScoped;

    // Inferred usages
    // TextureUsage::GENERAL
    uint32_t colorOrDepthStencilUsage = FormatIsColor(createInfo.format) ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    // Storage is only supported for non-sRGB color formats on most hardware
    uint32_t storageUsage = (FormatIsColor(createInfo.format) && !FormatIsSrgb(createInfo.format)) ? VK_IMAGE_USAGE_STORAGE_BIT : 0;

    uint32_t usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | 
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
                     VK_IMAGE_USAGE_SAMPLED_BIT |
                     storageUsage |
                     colorOrDepthStencilUsage;

    if (createInfo.usage == TextureUsage::READ_ONLY)
    {
      usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    
    if (createInfo.usage == TextureUsage::ATTACHMENT_READ_ONLY)
    {
      usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT |
              colorOrDepthStencilUsage;
    }

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
        .usage = usage,
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



    // TODO: gate behind compile-time switch
    vkSetDebugUtilsObjectNameEXT(device_->device_, detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_IMAGE,
      .objectHandle = reinterpret_cast<uint64_t>(image_),
      .pObjectName = name_.data(),
    }));

    // Identity view for convenience
    textureView_ = CreateFormatView(createInfo.format, name_);
  }

  Texture::~Texture()
  {
    if (device_ != nullptr && image_ != VK_NULL_HANDLE)
    {
      device_->imageDeletionQueue_.emplace_back(device_->frameNumber, allocation_, image_, std::move(name_));
    }
  }

  TextureView Texture::CreateFormatView(Format format, std::string name) const
  {
    ZoneScoped;
    using namespace detail;
    auto aspectFlags = VkImageAspectFlags{};
    aspectFlags |= FormatIsDepth(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    aspectFlags |= FormatIsStencil(format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    aspectFlags |= FormatIsColor(format) ? VK_IMAGE_ASPECT_COLOR_BIT : 0;

    return TextureView(*device_, *this, {
      .viewType = createInfo_.viewType,
      .format = format,
      .subresourceRange = VkImageSubresourceRange{
        .aspectMask = aspectFlags,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
      },
    }, std::move(name));
  }

  TextureView& Texture::CreateSingleMipView(uint32_t level, std::string name)
  {
    ZoneScoped;
    assert(level < singleMipViews_.size());

    if (singleMipViews_[level].has_value())
    {
      return singleMipViews_[level].value();
    }

    using namespace detail;
    auto format = createInfo_.format;
    auto aspectFlags = VkImageAspectFlags{};
    aspectFlags |= FormatIsDepth(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    aspectFlags |= FormatIsStencil(format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    aspectFlags |= FormatIsColor(format) ? VK_IMAGE_ASPECT_COLOR_BIT : 0;

    return singleMipViews_[level].emplace(*device_, *this, TextureViewCreateInfo{
      .viewType = createInfo_.viewType,
      .format = createInfo_.format,
      .subresourceRange = VkImageSubresourceRange{
        .aspectMask = aspectFlags,
        .baseMipLevel = level,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
      },
    }, name + std::string(" mip ") + std::to_string(level));
  }

  TextureView Texture::CreateSwizzleView(VkComponentMapping components, std::string name)
  {
    ZoneScoped;
    using namespace detail;
    auto format = createInfo_.format;
    auto aspectFlags = VkImageAspectFlags{};
    aspectFlags |= FormatIsDepth(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    aspectFlags |= FormatIsStencil(format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    aspectFlags |= FormatIsColor(format) ? VK_IMAGE_ASPECT_COLOR_BIT : 0;

    return TextureView(*device_, *this, {
      .viewType = createInfo_.viewType,
      .format = format,
      .components = components,
      .subresourceRange = VkImageSubresourceRange{
        .aspectMask = aspectFlags,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
      },
    }, std::move(name));
  }

  void Texture::UpdateImageSLOW(const TextureUpdateInfo& info)
  {
    ZoneScoped;
    device_->ImmediateSubmit(
    [this, &info](VkCommandBuffer commandBuffer)
    {
      // Convenience- so the user doesn't have to explicitly specify 1 for height or depth when writing 1D or 2D images
      auto extent = info.extent;
      extent.height = std::max(extent.height, 1u);
      extent.depth = std::max(extent.depth, 1u);

      uint64_t size;
      if (detail::FormatIsBlockCompressed(createInfo_.format))
      {
        size = detail::BlockCompressedImageSize(createInfo_.format, extent.width, extent.height, extent.depth);
      }
      else
      {
        size = extent.width * extent.height * extent.depth * detail::FormatStorageSize(createInfo_.format);
      }
      auto uploadBuffer = Buffer(*device_, {.size = size, .flag = BufferFlagThingy::MAP_SEQUENTIAL_WRITE}, "UpdateImageSLOW Staging Buffer");
      // TODO: account for row length and image height here
      std::memcpy(uploadBuffer.GetMappedMemory(), info.data, size);
      auto ctx = Fvog::Context(*device_, commandBuffer);
      ctx.ImageBarrier(*this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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
            .mipLevel = info.level,
            .layerCount = 1,
          },
          .imageOffset = info.offset,
          .imageExtent = extent,
        }),
      }));
      ctx.ImageBarrier(*this, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    });
  }

  Texture::Texture(Texture&& old) noexcept
    : currentLayout(std::move(old.currentLayout)),
      device_(std::exchange(old.device_, nullptr)),
      createInfo_(std::exchange(old.createInfo_, {})),
      image_(std::exchange(old.image_, VK_NULL_HANDLE)),
      textureView_(std::move(old.textureView_)),
      allocation_(std::exchange(old.allocation_, nullptr)),
      name_(std::move(old.name_))
  {
  }

  Texture& Texture::operator=(Texture&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~Texture();
    return *new (this) Texture(std::move(old));
  }

  TextureView::TextureView(Device& device, const Texture& texture, const TextureViewCreateInfo& createInfo, std::string name)
    : currentLayout(texture.currentLayout.get()),
      device_(&device),
      createInfo_(createInfo),
      parentCreateInfo_(texture.GetCreateInfo()),
      image_(texture.Image()),
      name_(std::move(name))
  {
    ZoneScoped;
    detail::CheckVkResult(vkCreateImageView(device.device_, detail::Address(VkImageViewCreateInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = texture.Image(),
      .viewType = createInfo.viewType,
      .format = detail::FormatToVk(createInfo.format),
      .components = createInfo.components,
      .subresourceRange = createInfo.subresourceRange,
    }), nullptr, &imageView_));

    // TODO: gate behind compile-time switch
    vkSetDebugUtilsObjectNameEXT(device_->device_, detail::Address(VkDebugUtilsObjectNameInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = VK_OBJECT_TYPE_IMAGE_VIEW,
      .objectHandle = reinterpret_cast<uint64_t>(imageView_),
      .pObjectName = name_.data(),
    }));

    const auto usage = parentCreateInfo_.usage;

    // Storage is only allowed for GENERAL images with a color format
    auto layout = VK_IMAGE_LAYOUT_GENERAL;
    if (usage == TextureUsage::GENERAL && detail::FormatIsColor(createInfo.format))
    {
      storageDescriptorInfo_ = device.AllocateStorageImageDescriptor(imageView_, layout);
    }
    else
    {
      layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
    }
    
    sampledDescriptorInfo_ = device.AllocateSampledImageDescriptor(imageView_, layout);
  }

  TextureView::~TextureView()
  {
    if (device_ != nullptr && imageView_ != VK_NULL_HANDLE)
    {
      // It's safe to pass VMA null allocators and/or handles, so we can reuse the image deletion queue here
      device_->imageViewDeletionQueue_.emplace_back(device_->frameNumber, imageView_, std::move(name_));
    }
  }

  TextureView::TextureView(TextureView&& old) noexcept
    : currentLayout(std::exchange(old.currentLayout, nullptr)),
      device_(std::exchange(old.device_, nullptr)),
      createInfo_(std::exchange(old.createInfo_, {})),
      imageView_(std::exchange(old.imageView_, VK_NULL_HANDLE)),
      sampledDescriptorInfo_(std::move(old.sampledDescriptorInfo_)),
      storageDescriptorInfo_(std::move(old.storageDescriptorInfo_)),
      parentCreateInfo_(std::exchange(old.parentCreateInfo_, {})),
      image_(std::exchange(old.image_, VK_NULL_HANDLE)),
      name_(std::move(old.name_))
  {}

  TextureView& TextureView::operator=(TextureView&& old) noexcept
  {
    if (&old == this)
      return *this;
    this->~TextureView();
    return *new (this) TextureView(std::move(old));
  }

  Texture CreateTexture2D(Device& device, VkExtent2D size, Format format, TextureUsage usage, std::string name)
  {
    ZoneScoped;
    TextureCreateInfo createInfo{
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .extent = {size.width, size.height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .sampleCount = VK_SAMPLE_COUNT_1_BIT,
      .usage = usage,
    };
    return Texture(device, createInfo, std::move(name));
  }

  Texture CreateTexture2DMip(Device& device, VkExtent2D size, Format format, uint32_t mipLevels, TextureUsage usage, std::string name)
  {
    ZoneScoped;
    TextureCreateInfo createInfo{
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .extent = {size.width, size.height, 1},
      .mipLevels = mipLevels,
      .arrayLayers = 1,
      .sampleCount = VK_SAMPLE_COUNT_1_BIT,
      .usage = usage,
    };
    return Texture(device, createInfo, std::move(name));
  }
} // namespace Fvog