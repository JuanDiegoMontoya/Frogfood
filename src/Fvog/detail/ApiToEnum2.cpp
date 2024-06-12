#include "ApiToEnum2.h"

#include <cassert>

namespace Fvog::detail
{
  VkImageType ViewTypeToImageType(VkImageViewType viewType)
  {
    switch (viewType)
    {
    case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
    case VK_IMAGE_VIEW_TYPE_1D: return VK_IMAGE_TYPE_1D;
    case VK_IMAGE_VIEW_TYPE_CUBE:
    case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
    case VK_IMAGE_VIEW_TYPE_2D: return VK_IMAGE_TYPE_2D;
    case VK_IMAGE_VIEW_TYPE_3D: return VK_IMAGE_TYPE_3D;
    default: assert(0); return {};
    }
  }

  bool FormatIsDepth(Format format)
  {
    switch (format)
    {
    case Fvog::Format::D16_UNORM:
    case Fvog::Format::D24_UNORM_S8_UINT:
    case Fvog::Format::X8_D24_UNORM:
    case Fvog::Format::D32_SFLOAT: [[fallthrough]];
    case Fvog::Format::D32_SFLOAT_S8_UINT: return true;
    default: return false;
    }
  }

  bool FormatIsStencil(Format format)
  {
    switch (format)
    {
    case Fvog::Format::D24_UNORM_S8_UINT: [[fallthrough]];
    case Fvog::Format::D32_SFLOAT_S8_UINT: return true;
    default: return false;
    }
  }

  bool FormatIsColor(Format format)
  {
    return !(FormatIsDepth(format) || FormatIsStencil(format));
  }

  bool FormatIsSrgb(Format format)
  {
    switch (format)
    {
    case Fvog::Format::R8G8B8A8_SRGB:
    case Fvog::Format::B8G8R8A8_SRGB:
    case Fvog::Format::BC1_RGBA_SRGB:
    case Fvog::Format::BC1_RGB_SRGB:
    case Fvog::Format::BC2_RGBA_SRGB:
    case Fvog::Format::BC3_RGBA_SRGB:
    case Fvog::Format::BC7_RGBA_SRGB:
      return true;
    default: return false;
    }
  }

  VkFormat FormatToVk(Format format)
  {
    switch (format)
    {
    case Format::UNDEFINED: return VK_FORMAT_UNDEFINED;
    case Format::R8_UNORM: return VK_FORMAT_R8_UNORM;
    case Format::R8_SNORM: return VK_FORMAT_R8_SNORM;
    case Format::R16_UNORM: return VK_FORMAT_R16_UNORM;
    case Format::R16_SNORM: return VK_FORMAT_R16_SNORM;
    case Format::R8G8_UNORM: return VK_FORMAT_R8G8_UNORM;
    case Format::R8G8_SNORM: return VK_FORMAT_R8G8_SNORM;
    case Format::R16G16_UNORM: return VK_FORMAT_R16G16_UNORM;
    case Format::R16G16_SNORM: return VK_FORMAT_R16G16_SNORM;
    case Format::R4G4B4A4_UNORM: return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
    case Format::R5G5B5A1_UNORM: return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
    case Format::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::R8G8B8A8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
    case Format::A2R10G10B10_UNORM: return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    case Format::A2R10G10B10_UINT: return VK_FORMAT_A2R10G10B10_UINT_PACK32;
    case Format::R16G16B16A16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
    case Format::R16G16B16A16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
    case Format::R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::R16_SFLOAT: return VK_FORMAT_R16_SFLOAT;
    case Format::R16G16_SFLOAT: return VK_FORMAT_R16G16_SFLOAT;
    case Format::R16G16B16A16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32_SFLOAT: return VK_FORMAT_R32_SFLOAT;
    case Format::R32G32_SFLOAT: return VK_FORMAT_R32G32_SFLOAT;
    case Format::R32G32B32A32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::B10G11R11_UFLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case Format::E5B9G9R9_UFLOAT: return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    case Format::R8_SINT: return VK_FORMAT_R8_SINT;
    case Format::R8_UINT: return VK_FORMAT_R8_UINT;
    case Format::R16_SINT: return VK_FORMAT_R16_SINT;
    case Format::R16_UINT: return VK_FORMAT_R16_UINT;
    case Format::R32_SINT: return VK_FORMAT_R32_SINT;
    case Format::R32_UINT: return VK_FORMAT_R32_UINT;
    case Format::R8G8_SINT: return VK_FORMAT_R8G8_SINT;
    case Format::R8G8_UINT: return VK_FORMAT_R8G8_UINT;
    case Format::R16G16_SINT: return VK_FORMAT_R16G16_SINT;
    case Format::R16G16_UINT: return VK_FORMAT_R16G16_UINT;
    case Format::R32G32_SINT: return VK_FORMAT_R32G32_SINT;
    case Format::R32G32_UINT: return VK_FORMAT_R32G32_UINT;
    case Format::R8G8B8A8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
    case Format::R8G8B8A8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
    case Format::R16G16B16A16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
    case Format::R16G16B16A16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
    case Format::R32G32B32A32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
    case Format::R32G32B32A32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
    case Format::D32_SFLOAT: return VK_FORMAT_D32_SFLOAT;
    case Format::X8_D24_UNORM: return VK_FORMAT_X8_D24_UNORM_PACK32;
    case Format::D16_UNORM: return VK_FORMAT_D16_UNORM;
    case Format::D32_SFLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case Format::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::BC1_RGB_UNORM: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    case Format::BC1_RGB_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
    case Format::BC1_RGBA_UNORM: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Format::BC1_RGBA_SRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case Format::BC2_RGBA_UNORM: return VK_FORMAT_BC2_UNORM_BLOCK;
    case Format::BC2_RGBA_SRGB: return VK_FORMAT_BC2_SRGB_BLOCK;
    case Format::BC3_RGBA_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
    case Format::BC3_RGBA_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
    case Format::BC4_R_UNORM: return VK_FORMAT_BC4_UNORM_BLOCK;
    case Format::BC4_R_SNORM: return VK_FORMAT_BC4_SNORM_BLOCK;
    case Format::BC5_RG_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
    case Format::BC5_RG_SNORM: return VK_FORMAT_BC5_SNORM_BLOCK;
    case Format::BC6H_RGB_UFLOAT: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case Format::BC6H_RGB_SFLOAT: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    case Format::BC7_RGBA_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
    case Format::BC7_RGBA_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
    }

    assert(false);
    return VK_FORMAT_UNDEFINED;
  }

  Format VkToFormat(VkFormat format)
  {
    switch (format)
    {
    case VK_FORMAT_UNDEFINED: return Format::UNDEFINED;
    case VK_FORMAT_R8_UNORM: return Format::R8_UNORM;
    case VK_FORMAT_R8_SNORM: return Format::R8_SNORM;
    case VK_FORMAT_R16_UNORM: return Format::R16_UNORM;
    case VK_FORMAT_R16_SNORM: return Format::R16_SNORM;
    case VK_FORMAT_R8G8_UNORM: return Format::R8G8_UNORM;
    case VK_FORMAT_R8G8_SNORM: return Format::R8G8_SNORM;
    case VK_FORMAT_R16G16_UNORM: return Format::R16G16_UNORM;
    case VK_FORMAT_R16G16_SNORM: return Format::R16G16_SNORM;
    case VK_FORMAT_R4G4B4A4_UNORM_PACK16: return Format::R4G4B4A4_UNORM;
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16: return Format::R5G5B5A1_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM: return Format::R8G8B8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_UNORM: return Format::B8G8R8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SNORM: return Format::R8G8B8A8_SNORM;
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return Format::A2R10G10B10_UNORM;
    case VK_FORMAT_A2R10G10B10_UINT_PACK32: return Format::A2R10G10B10_UINT;
    case VK_FORMAT_R16G16B16A16_UNORM: return Format::R16G16B16A16_UNORM;
    case VK_FORMAT_R16G16B16A16_SNORM: return Format::R16G16B16A16_SNORM;
    case VK_FORMAT_R8G8B8A8_SRGB: return Format::R8G8B8A8_SRGB;
    case VK_FORMAT_B8G8R8A8_SRGB: return Format::B8G8R8A8_SRGB;
    case VK_FORMAT_R16_SFLOAT: return Format::R16_SFLOAT;
    case VK_FORMAT_R16G16_SFLOAT: return Format::R16G16_SFLOAT;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::R16G16B16A16_SFLOAT;
    case VK_FORMAT_R32_SFLOAT: return Format::R32_SFLOAT;
    case VK_FORMAT_R32G32_SFLOAT: return Format::R32G32_SFLOAT;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::R32G32B32A32_SFLOAT;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return Format::B10G11R11_UFLOAT;
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: return Format::E5B9G9R9_UFLOAT;
    case VK_FORMAT_R8_SINT: return Format::R8_SINT;
    case VK_FORMAT_R8_UINT: return Format::R8_UINT;
    case VK_FORMAT_R16_SINT: return Format::R16_SINT;
    case VK_FORMAT_R16_UINT: return Format::R16_UINT;
    case VK_FORMAT_R32_SINT: return Format::R32_SINT;
    case VK_FORMAT_R32_UINT: return Format::R32_UINT;
    case VK_FORMAT_R8G8_SINT: return Format::R8G8_SINT;
    case VK_FORMAT_R8G8_UINT: return Format::R8G8_UINT;
    case VK_FORMAT_R16G16_SINT: return Format::R16G16_SINT;
    case VK_FORMAT_R16G16_UINT: return Format::R16G16_UINT;
    case VK_FORMAT_R32G32_SINT: return Format::R32G32_SINT;
    case VK_FORMAT_R32G32_UINT: return Format::R32G32_UINT;
    case VK_FORMAT_R8G8B8A8_SINT: return Format::R8G8B8A8_SINT;
    case VK_FORMAT_R8G8B8A8_UINT: return Format::R8G8B8A8_UINT;
    case VK_FORMAT_R16G16B16A16_SINT: return Format::R16G16B16A16_SINT;
    case VK_FORMAT_R16G16B16A16_UINT: return Format::R16G16B16A16_UINT;
    case VK_FORMAT_R32G32B32A32_SINT: return Format::R32G32B32A32_SINT;
    case VK_FORMAT_R32G32B32A32_UINT: return Format::R32G32B32A32_UINT;
    case VK_FORMAT_D32_SFLOAT: return Format::D32_SFLOAT;
    case VK_FORMAT_X8_D24_UNORM_PACK32: return Format::X8_D24_UNORM;
    case VK_FORMAT_D16_UNORM: return Format::D16_UNORM;
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return Format::D32_SFLOAT_S8_UINT;
    case VK_FORMAT_D24_UNORM_S8_UINT: return Format::D24_UNORM_S8_UINT;
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return Format::BC1_RGB_UNORM;
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return Format::BC1_RGB_SRGB;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return Format::BC1_RGBA_UNORM;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return Format::BC1_RGBA_SRGB;
    case VK_FORMAT_BC2_UNORM_BLOCK: return Format::BC2_RGBA_UNORM;
    case VK_FORMAT_BC2_SRGB_BLOCK: return Format::BC2_RGBA_SRGB;
    case VK_FORMAT_BC3_UNORM_BLOCK: return Format::BC3_RGBA_UNORM;
    case VK_FORMAT_BC3_SRGB_BLOCK: return Format::BC3_RGBA_SRGB;
    case VK_FORMAT_BC4_UNORM_BLOCK: return Format::BC4_R_UNORM;
    case VK_FORMAT_BC4_SNORM_BLOCK: return Format::BC4_R_SNORM;
    case VK_FORMAT_BC5_UNORM_BLOCK: return Format::BC5_RG_UNORM;
    case VK_FORMAT_BC5_SNORM_BLOCK: return Format::BC5_RG_SNORM;
    case VK_FORMAT_BC6H_UFLOAT_BLOCK: return Format::BC6H_RGB_UFLOAT;
    case VK_FORMAT_BC6H_SFLOAT_BLOCK: return Format::BC6H_RGB_SFLOAT;
    case VK_FORMAT_BC7_UNORM_BLOCK: return Format::BC7_RGBA_UNORM;
    case VK_FORMAT_BC7_SRGB_BLOCK: return Format::BC7_RGBA_SRGB;
    }

    assert(false);
    return Fvog::Format::UNDEFINED;
  }

  bool FormatIsBlockCompressed(Format format)
  {
    switch (format)
    {
    case Fvog::Format::BC1_RGB_UNORM:
    case Fvog::Format::BC1_RGB_SRGB:
    case Fvog::Format::BC1_RGBA_UNORM:
    case Fvog::Format::BC1_RGBA_SRGB:
    case Fvog::Format::BC2_RGBA_UNORM:
    case Fvog::Format::BC2_RGBA_SRGB:
    case Fvog::Format::BC3_RGBA_UNORM:
    case Fvog::Format::BC3_RGBA_SRGB:
    case Fvog::Format::BC4_R_UNORM:
    case Fvog::Format::BC4_R_SNORM:
    case Fvog::Format::BC5_RG_UNORM:
    case Fvog::Format::BC5_RG_SNORM:
    case Fvog::Format::BC6H_RGB_UFLOAT:
    case Fvog::Format::BC6H_RGB_SFLOAT:
    case Fvog::Format::BC7_RGBA_UNORM:
    case Fvog::Format::BC7_RGBA_SRGB:
      return true;
    }

    return false;
  }

  uint64_t BlockCompressedImageSize(Format bcFormat, uint32_t width, uint32_t height, uint32_t depth)
  {
    assert(FormatIsBlockCompressed(bcFormat));

    // BCn formats store 4x4 blocks of pixels, even if the dimensions aren't a multiple of 4
    // We round up to the nearest multiple of 4 for width and height, but not depth, since
    // 3D BCn images are just multiple 2D images stacked
    auto roundedWidth = (width + 4 - 1) & -4;
    auto roundedHeight = (height + 4 - 1) & -4;

    switch (bcFormat)
    {
    // BC1 and BC4 store 4x4 blocks with 64 bits (8 bytes)
    case Format::BC1_RGB_UNORM:
    case Format::BC1_RGBA_UNORM:
    case Format::BC1_RGB_SRGB:
    case Format::BC1_RGBA_SRGB:
    case Format::BC4_R_UNORM:
    case Format::BC4_R_SNORM: return roundedWidth * roundedHeight * depth / 2;

    // BC2, BC3, BC5, BC6, and BC7 store 4x4 blocks with 128 bits (16 bytes)
    case Format::BC2_RGBA_UNORM:
    case Format::BC2_RGBA_SRGB:
    case Format::BC3_RGBA_UNORM:
    case Format::BC3_RGBA_SRGB:
    case Format::BC5_RG_UNORM:
    case Format::BC5_RG_SNORM:
    case Format::BC6H_RGB_UFLOAT:
    case Format::BC6H_RGB_SFLOAT:
    case Format::BC7_RGBA_UNORM:
    case Format::BC7_RGBA_SRGB: return roundedWidth * roundedHeight * depth;
    }

    assert(false);
    return {};
  }

  uint32_t FormatStorageSize(Format format)
  {
    switch (format)
    {
    case Format::R8_UNORM:
    case Format::R8_SNORM:
    case Format::R8_SINT:
    case Format::R8_UINT:
      return 1;

    case Format::R16_UNORM:
    case Format::R16_SNORM:
    case Format::R8G8_UNORM:
    case Format::R8G8_SNORM:
    case Format::R4G4B4A4_UNORM:
    case Format::R5G5B5A1_UNORM:
    case Format::R16_SFLOAT:
    case Format::R16_SINT:
    case Format::R16_UINT:
    case Format::R8G8_SINT:
    case Format::R8G8_UINT:
    case Format::D16_UNORM:
      return 2;

    case Format::R16G16_UNORM:
    case Format::R16G16_SNORM:
    case Format::R8G8B8A8_UNORM:
    case Format::B8G8R8A8_UNORM:
    case Format::R8G8B8A8_SNORM:
    case Format::A2R10G10B10_UNORM:
    case Format::A2R10G10B10_UINT:
    case Format::R8G8B8A8_SRGB:
    case Format::B8G8R8A8_SRGB:
    case Format::R16G16_SFLOAT:
    case Format::R32_SFLOAT:
    case Format::B10G11R11_UFLOAT:
    case Format::E5B9G9R9_UFLOAT:
    case Format::R32_SINT:
    case Format::R32_UINT:
    case Format::R16G16_SINT:
    case Format::R16G16_UINT:
    case Format::R8G8B8A8_SINT:
    case Format::R8G8B8A8_UINT:
    case Format::D32_SFLOAT:
    case Format::X8_D24_UNORM:
    case Format::D24_UNORM_S8_UINT:
      return 4;

    case Format::D32_SFLOAT_S8_UINT:
      return 5;

    case Format::R16G16B16A16_UNORM:
    case Format::R16G16B16A16_SNORM:
    case Format::R16G16B16A16_SFLOAT:
    case Format::R32G32_SFLOAT:
    case Format::R32G32_SINT:
    case Format::R32G32_UINT:
    case Format::R16G16B16A16_SINT:
    case Format::R16G16B16A16_UINT:
    case Format::BC1_RGB_UNORM:
    case Format::BC1_RGB_SRGB:
    case Format::BC1_RGBA_UNORM:
    case Format::BC1_RGBA_SRGB:
    case Format::BC4_R_UNORM:
    case Format::BC4_R_SNORM:
      return 8;

    case Format::R32G32B32A32_SFLOAT:
    case Format::R32G32B32A32_SINT:
    case Format::R32G32B32A32_UINT:
    case Format::BC2_RGBA_UNORM:
    case Format::BC2_RGBA_SRGB:
    case Format::BC3_RGBA_UNORM:
    case Format::BC3_RGBA_SRGB:
    case Format::BC5_RG_UNORM:
    case Format::BC5_RG_SNORM:
    case Format::BC6H_RGB_UFLOAT:
    case Format::BC6H_RGB_SFLOAT:
    case Format::BC7_RGBA_UNORM:
    case Format::BC7_RGBA_SRGB:
      return 16;
    }

    assert(false);
    return 0;
  }
} // namespace Fvog::detail
