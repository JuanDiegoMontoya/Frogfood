#pragma once

#include "BasicTypes2.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

namespace Fvog
{
  class Texture;
  class Buffer;

  // Minimal reference wrapper type. Didn't want to pull in <functional> just for this
  template<class T>
    requires std::is_object_v<T>
  class ReferenceWrapper
  {
  public:
    using type = T;

    template<class U>
    constexpr ReferenceWrapper(U&& val) noexcept
    {
      T& ref = static_cast<U&&>(val);
      ptr = std::addressof(ref);
    }

    constexpr operator T&() const noexcept
    {
      return *ptr;
    }

    [[nodiscard]] constexpr T& get() const noexcept
    {
      return *ptr;
    }

  private:
    T* ptr{};
  };

  struct ClearColorValue
  {
    ClearColorValue() = default;

    template<typename... Args>
      requires(sizeof...(Args) <= 4)
    ClearColorValue(const Args&... args) : data(std::array<std::common_type_t<std::remove_cvref_t<Args>...>, 4>{args...})
    {
    }

    std::variant<std::array<float, 4>, std::array<uint32_t, 4>, std::array<int32_t, 4>> data;
  };

  struct ClearDepthStencilValue
  {
    float depth{};
    uint32_t stencil{};
  };

  struct RenderColorAttachment
  {
    ReferenceWrapper<const Texture> texture;
    VkImageLayout layout{};
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ClearColorValue clearValue{};
  };

  struct RenderDepthStencilAttachment
  {
    ReferenceWrapper<const Texture> texture;
    VkImageLayout layout{};
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ClearDepthStencilValue clearValue{};
  };

  struct RenderInfo
  {
    /// @brief An optional name to demarcate the pass in a graphics debugger
    std::string_view name;

    /// @brief An optional viewport
    ///
    /// If empty, the viewport size will be the minimum the render targets' size and the offset will be 0.
    std::optional<VkViewport> viewport = std::nullopt;
    std::span<const RenderColorAttachment> colorAttachments;
    std::optional<RenderDepthStencilAttachment> depthAttachment = std::nullopt;
    std::optional<RenderDepthStencilAttachment> stencilAttachment = std::nullopt;
  };

  class Context
  {
  public:
    Context(VkCommandBuffer commandBuffer);

    void BeginRendering(const RenderInfo& renderInfo) const;
    void EndRendering() const;
    void ImageBarrier(const Texture& texture, VkImageLayout oldLayout, VkImageLayout newLayout) const;
    void ImageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask) const;
    void BufferBarrier(const Buffer& buffer) const;
    void BufferBarrier(VkBuffer buffer) const;

  private:
    VkCommandBuffer commandBuffer_;
  };
}
