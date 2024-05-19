#pragma once

#include "../BasicTypes2.h"

#include <vulkan/vulkan_core.h>

namespace Fvog::detail
{
  VkImageType ViewTypeToImageType(VkImageViewType viewType);

  bool FormatIsDepth(Format format);

  bool FormatIsStencil(Format format);

  bool FormatIsColor(Format format);

  bool FormatIsSrgb(Format format);

  VkFormat FormatToVk(Format format);

  Format VkToFormat(VkFormat format);

  bool FormatIsBlockCompressed(Format format);

  struct ImageSizes
  {
    uint64_t size;
    Extent3D extent;
  };

  ImageSizes BlockCompressedImageSize(Format bcFormat, uint32_t width, uint32_t height, uint32_t depth);

  // Returns the size, in bytes, of a single pixel or block (for compressed formats) of the input format
  uint32_t FormatStorageSize(Format format);
}
