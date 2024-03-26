#pragma once

#include "detail/Flags.h"
#include <cstdint>

namespace Fvog
{
  // clang-format off
  struct Extent2D
  {
    uint32_t width{};
    uint32_t height{};

#ifdef VK_HEADER_VERSION
    operator VkExtent2D() const { return {width, height}; }
#endif
    bool operator==(const Extent2D&) const noexcept = default;
    Extent2D operator+(const Extent2D& other) const { return { width + other.width, height + other.height }; }
    Extent2D operator-(const Extent2D& other) const { return { width - other.width, height - other.height }; }
    Extent2D operator*(const Extent2D& other) const { return { width * other.width, height * other.height }; }
    Extent2D operator/(const Extent2D& other) const { return { width / other.width, height / other.height }; }
    Extent2D operator>>(const Extent2D& other) const { return { width >> other.width, height >> other.height }; }
    Extent2D operator<<(const Extent2D& other) const { return { width << other.width, height << other.height }; }
    Extent2D operator+(uint32_t val) const { return *this + Extent2D{ val, val }; }
    Extent2D operator-(uint32_t val) const { return *this - Extent2D{ val, val }; }
    Extent2D operator*(uint32_t val) const { return *this * Extent2D{ val, val }; }
    Extent2D operator/(uint32_t val) const { return *this / Extent2D{ val, val }; }
    Extent2D operator>>(uint32_t val) const { return *this >> Extent2D{ val, val }; }
    Extent2D operator<<(uint32_t val) const { return *this << Extent2D{ val, val }; }
  };

  inline Extent2D operator+(uint32_t val, Extent2D ext) { return ext + val; }
  inline Extent2D operator-(uint32_t val, Extent2D ext) { return ext - val; }
  inline Extent2D operator*(uint32_t val, Extent2D ext) { return ext * val; }
  inline Extent2D operator/(uint32_t val, Extent2D ext) { return ext / val; }
  inline Extent2D operator>>(uint32_t val, Extent2D ext) { return ext >> val; }
  inline Extent2D operator<<(uint32_t val, Extent2D ext) { return ext << val; }

  struct Extent3D
  {
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{};
    
#ifdef VK_HEADER_VERSION
    operator VkExtent3D() const { return {width, height, depth}; }
#endif
    operator Extent2D() const { return { width, height }; }
    bool operator==(const Extent3D&) const noexcept = default;
    Extent3D operator+(const Extent3D& other) const { return { width + other.width, height + other.height, depth + other.depth }; }
    Extent3D operator-(const Extent3D& other) const { return { width - other.width, height - other.height, depth - other.depth }; }
    Extent3D operator*(const Extent3D& other) const { return { width * other.width, height * other.height, depth * other.depth }; }
    Extent3D operator/(const Extent3D& other) const { return { width / other.width, height / other.height, depth / other.depth }; }
    Extent3D operator>>(const Extent3D& other) const { return { width >> other.width, height >> other.height, depth >> other.depth }; }
    Extent3D operator<<(const Extent3D& other) const { return { width << other.width, height << other.height, depth << other.depth }; }
    Extent3D operator+(uint32_t val) const { return *this + Extent3D{ val, val, val }; }
    Extent3D operator-(uint32_t val) const { return *this - Extent3D{ val, val, val }; }
    Extent3D operator*(uint32_t val) const { return *this * Extent3D{ val, val, val }; }
    Extent3D operator/(uint32_t val) const { return *this / Extent3D{ val, val, val }; }
    Extent3D operator>>(uint32_t val) const { return *this >> Extent3D{ val, val, val }; }
    Extent3D operator<<(uint32_t val) const { return *this << Extent3D{ val, val, val }; }
  };

  inline Extent3D operator+(uint32_t val, Extent3D ext) { return ext + val; }
  inline Extent3D operator-(uint32_t val, Extent3D ext) { return ext - val; }
  inline Extent3D operator*(uint32_t val, Extent3D ext) { return ext * val; }
  inline Extent3D operator/(uint32_t val, Extent3D ext) { return ext / val; }
  inline Extent3D operator>>(uint32_t val, Extent3D ext) { return ext >> val; }
  inline Extent3D operator<<(uint32_t val, Extent3D ext) { return ext << val; }

  struct Offset2D
  {
    uint32_t x{};
    uint32_t y{};
    
#ifdef VK_HEADER_VERSION
    operator VkOffset2D() const { return {int32_t(x), int32_t(y)}; }
#endif
    bool operator==(const Offset2D&) const noexcept = default;
    Offset2D operator+(const Offset2D & other) const { return { x + other.x, y + other.y }; }
    Offset2D operator-(const Offset2D & other) const { return { x - other.x, y - other.y }; }
    Offset2D operator*(const Offset2D & other) const { return { x * other.x, y * other.y }; }
    Offset2D operator/(const Offset2D & other) const { return { x / other.x, y / other.y }; }
    Offset2D operator>>(const Offset2D & other) const { return { x >> other.x, y >> other.y }; }
    Offset2D operator<<(const Offset2D & other) const { return { x << other.x, y << other.y }; }
    Offset2D operator+(uint32_t val) const { return *this + Offset2D{ val, val }; }
    Offset2D operator-(uint32_t val) const { return *this - Offset2D{ val, val }; }
    Offset2D operator*(uint32_t val) const { return *this * Offset2D{ val, val }; }
    Offset2D operator/(uint32_t val) const { return *this / Offset2D{ val, val }; }
    Offset2D operator>>(uint32_t val) const { return *this >> Offset2D{ val, val }; }
    Offset2D operator<<(uint32_t val) const { return *this << Offset2D{ val, val }; }
  };

  inline Offset2D operator+(uint32_t val, Offset2D ext) { return ext + val; }
  inline Offset2D operator-(uint32_t val, Offset2D ext) { return ext - val; }
  inline Offset2D operator*(uint32_t val, Offset2D ext) { return ext * val; }
  inline Offset2D operator/(uint32_t val, Offset2D ext) { return ext / val; }
  inline Offset2D operator>>(uint32_t val, Offset2D ext) { return ext >> val; }
  inline Offset2D operator<<(uint32_t val, Offset2D ext) { return ext << val; }

  struct Offset3D
  {
    uint32_t x{};
    uint32_t y{};
    uint32_t z{};
    
#ifdef VK_HEADER_VERSION
    operator VkOffset3D() const { return {int32_t(x), int32_t(y), int32_t(z)}; }
#endif
    operator Offset2D() const { return { x, y }; }
    bool operator==(const Offset3D&) const noexcept = default;
    Offset3D operator+(const Offset3D& other) const { return { x + other.x, y + other.y, z + other.z }; }
    Offset3D operator-(const Offset3D& other) const { return { x - other.x, y - other.y, z - other.z }; }
    Offset3D operator*(const Offset3D& other) const { return { x * other.x, y * other.y, z * other.z }; }
    Offset3D operator/(const Offset3D& other) const { return { x / other.x, y / other.y, z / other.z }; }
    Offset3D operator>>(const Offset3D& other) const { return { x >> other.x, y >> other.y, z >> other.z }; }
    Offset3D operator<<(const Offset3D& other) const { return { x << other.x, y << other.y, z << other.z }; }
    Offset3D operator+(uint32_t val) const { return *this + Offset3D{ val, val, val }; }
    Offset3D operator-(uint32_t val) const { return *this - Offset3D{ val, val, val }; }
    Offset3D operator*(uint32_t val) const { return *this * Offset3D{ val, val, val }; }
    Offset3D operator/(uint32_t val) const { return *this / Offset3D{ val, val, val }; }
    Offset3D operator>>(uint32_t val) const { return *this >> Offset3D{ val, val, val }; }
    Offset3D operator<<(uint32_t val) const { return *this << Offset3D{ val, val, val }; }
  };

  inline Offset3D operator+(uint32_t val, Offset3D ext) { return ext + val; }
  inline Offset3D operator-(uint32_t val, Offset3D ext) { return ext - val; }
  inline Offset3D operator*(uint32_t val, Offset3D ext) { return ext * val; }
  inline Offset3D operator/(uint32_t val, Offset3D ext) { return ext / val; }
  inline Offset3D operator>>(uint32_t val, Offset3D ext) { return ext >> val; }
  inline Offset3D operator<<(uint32_t val, Offset3D ext) { return ext << val; }

  struct Rect2D
  {
    Offset2D offset;
    Extent2D extent;
    
#ifdef VK_HEADER_VERSION
    operator VkRect2D() const { return {VkOffset2D(offset), VkExtent2D(extent)}; }
#endif
    bool operator==(const Rect2D&) const noexcept = default;
  };

  enum class ImageType : uint32_t
  {
    TEX_1D,
    TEX_2D,
    TEX_3D,
    TEX_1D_ARRAY,
    TEX_2D_ARRAY,
    TEX_CUBEMAP,
    TEX_CUBEMAP_ARRAY,
    TEX_2D_MULTISAMPLE,
    TEX_2D_MULTISAMPLE_ARRAY,
  };
  
  /// @brief Specifies how a component is swizzled
  enum class ComponentSwizzle : uint32_t
  {
    ZERO,
    ONE,
    R,
    G,
    B,
    A
  };

  enum class Format : uint32_t
  {
    UNDEFINED,

    // Color formats
    R8_UNORM,
    R8_SNORM,
    R16_UNORM,
    R16_SNORM,
    R8G8_UNORM,
    R8G8_SNORM,
    R16G16_UNORM,
    R16G16_SNORM,
    R4G4B4A4_UNORM,
    R5G5B5A1_UNORM,
    R8G8B8A8_UNORM,
    B8G8R8A8_UNORM,
    R8G8B8A8_SNORM,
    A2R10G10B10_UNORM,
    A2R10G10B10_UINT,
    R16G16B16A16_UNORM,
    R16G16B16A16_SNORM,
    R8G8B8A8_SRGB,
    B8G8R8A8_SRGB,
    R16_SFLOAT,
    R16G16_SFLOAT,
    R16G16B16A16_SFLOAT,
    R32_SFLOAT,
    R32G32_SFLOAT,
    R32G32B32A32_SFLOAT,
    B10G11R11_UFLOAT,
    E5B9G9R9_UFLOAT,
    R8_SINT,
    R8_UINT,
    R16_SINT,
    R16_UINT,
    R32_SINT,
    R32_UINT,
    R8G8_SINT,
    R8G8_UINT,
    R16G16_SINT,
    R16G16_UINT,
    R32G32_SINT,
    R32G32_UINT,
    R8G8B8A8_SINT,
    R8G8B8A8_UINT,
    R16G16B16A16_SINT,
    R16G16B16A16_UINT,
    R32G32B32A32_SINT,
    R32G32B32A32_UINT,

    // Depth & stencil formats
    D32_SFLOAT,
    X8_D24_UNORM,
    D16_UNORM,
    D32_SFLOAT_S8_UINT,
    D24_UNORM_S8_UINT,

    // Compressed formats
    // DXT
    BC1_RGB_UNORM,
    BC1_RGB_SRGB,
    BC1_RGBA_UNORM,
    BC1_RGBA_SRGB,
    BC2_RGBA_UNORM,
    BC2_RGBA_SRGB,
    BC3_RGBA_UNORM,
    BC3_RGBA_SRGB,
    // RGTC
    BC4_R_UNORM,
    BC4_R_SNORM,
    BC5_RG_UNORM,
    BC5_RG_SNORM,
    // BPTC
    BC6H_RGB_UFLOAT,
    BC6H_RGB_SFLOAT,
    BC7_RGBA_UNORM,
    BC7_RGBA_SRGB,

    // TODO: 64-bits-per-component formats
  };

  // multisampling and anisotropy
  enum class SampleCount : uint32_t
  {
    SAMPLES_1  = 1,
    SAMPLES_2  = 2,
    SAMPLES_4  = 4,
    SAMPLES_8  = 8,
    SAMPLES_16 = 16,
    SAMPLES_32 = 32,
  };

  /// @brief Convenience constant to allow binding the whole buffer in Cmd::BindUniformBuffer and Cmd::BindStorageBuffer
  ///
  /// If an offset is provided with this constant, then the range [offset, buffer.Size()) will be bound.
  constexpr inline uint64_t WHOLE_BUFFER = static_cast<uint64_t>(-1);

  enum class Filter : uint32_t
  {
    NONE,
    NEAREST,
    LINEAR,
  };

  enum class AddressMode : uint32_t
  {
    REPEAT,
    MIRRORED_REPEAT,
    CLAMP_TO_EDGE,
    CLAMP_TO_BORDER,
    MIRROR_CLAMP_TO_EDGE,
  };

  enum class BorderColor : uint32_t
  {
    FLOAT_TRANSPARENT_BLACK,
    INT_TRANSPARENT_BLACK,
    FLOAT_OPAQUE_BLACK,
    INT_OPAQUE_BLACK,
    FLOAT_OPAQUE_WHITE,
    INT_OPAQUE_WHITE,
  };

  enum class AspectMaskBit : uint32_t
  {
    COLOR_BUFFER_BIT    = 1 << 0,
    DEPTH_BUFFER_BIT    = 1 << 1,
    STENCIL_BUFFER_BIT  = 1 << 2,
  };
  FVOG_DECLARE_FLAG_TYPE(AspectMask, AspectMaskBit, uint32_t)

  enum class PrimitiveTopology : uint32_t
  {
    POINT_LIST,
    LINE_LIST,
    LINE_STRIP,
    TRIANGLE_LIST,
    TRIANGLE_STRIP,
    TRIANGLE_FAN,
  };

  enum class PolygonMode : uint32_t
  {
    FILL,
    LINE,
    POINT,
  };

  enum class CullMode : uint32_t
  {
    NONE = 0b00,
    FRONT = 0b01,
    BACK = 0b10,
    FRONT_AND_BACK = 0b11,
  };

  enum class FrontFace : uint32_t
  {
    CLOCKWISE,
    COUNTERCLOCKWISE,
  };

  enum class CompareOp : uint32_t
  {
    NEVER,
    LESS,
    EQUAL,
    LESS_OR_EQUAL,
    GREATER,
    NOT_EQUAL,
    GREATER_OR_EQUAL,
    ALWAYS,
  };

  enum class LogicOp : uint32_t
  {
    CLEAR,
    SET,
    COPY,
    COPY_INVERTED,
    NO_OP,
    INVERT,
    AND,
    NAND,
    OR,
    NOR,
    XOR,
    EQUIVALENT,
    AND_REVERSE,
    OR_REVERSE,
    AND_INVERTED,
    OR_INVERTED,
  };

  enum class BlendFactor : uint32_t
  {
    ZERO,
    ONE,
    SRC_COLOR,
    ONE_MINUS_SRC_COLOR,
    DST_COLOR,
    ONE_MINUS_DST_COLOR,
    SRC_ALPHA,
    ONE_MINUS_SRC_ALPHA,
    DST_ALPHA,
    ONE_MINUS_DST_ALPHA,
    CONSTANT_COLOR,
    ONE_MINUS_CONSTANT_COLOR,
    CONSTANT_ALPHA,
    ONE_MINUS_CONSTANT_ALPHA,
    SRC_ALPHA_SATURATE,
    SRC1_COLOR,
    ONE_MINUS_SRC1_COLOR,
    SRC1_ALPHA,
    ONE_MINUS_SRC1_ALPHA,
  };

  enum class BlendOp : uint32_t
  {
    ADD,
    SUBTRACT,
    REVERSE_SUBTRACT,
    MIN,
    MAX,
  };

  enum class ColorComponentFlag : uint32_t
  {
    NONE,
    R_BIT = 0b0001,
    G_BIT = 0b0010,
    B_BIT = 0b0100,
    A_BIT = 0b1000,
    RGBA_BITS = 0b1111,
  };
  FVOG_DECLARE_FLAG_TYPE(ColorComponentFlags, ColorComponentFlag, uint32_t)

  enum class IndexType : uint32_t
  {
    UNSIGNED_BYTE,
    UNSIGNED_SHORT,
    UNSIGNED_INT,
  };

  enum class StencilOp : uint32_t
  {
    KEEP                = 0,
    ZERO                = 1,
    REPLACE             = 2,
    INCREMENT_AND_CLAMP = 3,
    DECREMENT_AND_CLAMP = 4,
    INVERT              = 5,
    INCREMENT_AND_WRAP  = 6,
    DECREMENT_AND_WRAP  = 7,
  };

  struct DrawIndirectCommand
  {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
  };

  struct DrawIndexedIndirectCommand
  {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;
  };

  struct DispatchIndirectCommand
  {
    uint32_t groupCountX;
    uint32_t groupCountY;
    uint32_t groupCountZ;
  };
  // clang-format on
} // namespace Fvog