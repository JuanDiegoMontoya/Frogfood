#include "ApiToEnum2.h"

#include <cassert>

namespace Fvog::detail
{
  VkImageType ViewTypeToImageType(VkImageViewType viewType)
  {
    switch (viewType)
    {
    case VK_IMAGE_VIEW_TYPE_1D: return VK_IMAGE_TYPE_1D;
    case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
    case VK_IMAGE_VIEW_TYPE_CUBE:
    case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
    case VK_IMAGE_VIEW_TYPE_2D: return VK_IMAGE_TYPE_2D;
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
    case VK_IMAGE_VIEW_TYPE_3D: return VK_IMAGE_TYPE_3D;
    default: assert(0); return {};
    }
  }

  bool FormatIsDepth(VkFormat format)
  {
    switch (format)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT: [[fallthrough]];
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return true;
    default: return false;
    }
  }

  bool FormatIsStencil(VkFormat format)
  {
    switch (format)
    {
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT: [[fallthrough]];
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return true;
    default: return false;
    }
  }

  bool FormatIsColor(VkFormat format)
  {
    return !(FormatIsDepth(format) || FormatIsStencil(format));
  }
} // namespace Fvog::detail
