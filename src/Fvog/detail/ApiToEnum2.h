#pragma once

#include <vulkan/vulkan_core.h>

namespace Fvog::detail
{
  VkImageType ViewTypeToImageType(VkImageViewType viewType);

  bool FormatIsDepth(VkFormat format);

  bool FormatIsStencil(VkFormat format);

  bool FormatIsColor(VkFormat format);
}
