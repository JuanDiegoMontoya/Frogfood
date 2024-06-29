#pragma once

#include <vulkan/vulkan_core.h>
#include "BasicTypes2.h"
#include "TriviallyCopyableByteSpan.h"

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

namespace Fvog
{
  class Texture;
  class TextureView;
  class Buffer;
  class GraphicsPipeline;
  class ComputePipeline;
  class Device;
  struct TextureUpdateInfo;

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

    ReferenceWrapper(const ReferenceWrapper&) = default;
    ReferenceWrapper& operator=(const ReferenceWrapper&) = default;

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

  template<class T>
  ReferenceWrapper(T&) -> ReferenceWrapper<T>;

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
    //std::reference_wrapper<const TextureView> texture;
    ReferenceWrapper<const TextureView> texture;
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ClearColorValue clearValue{};
  };

  struct RenderDepthStencilAttachment
  {
    //std::reference_wrapper<const TextureView> texture;
    ReferenceWrapper<const TextureView> texture;
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ClearDepthStencilValue clearValue{};
  };

  struct RenderInfo
  {
    /// @brief An optional name to demarcate the pass in a graphics debugger
    const char* name;

    /// @brief An optional viewport
    ///
    /// If empty, the viewport size will be the minimum the render targets' size and the offset will be 0.
    std::optional<VkViewport> viewport = std::nullopt;
    std::span<const RenderColorAttachment> colorAttachments;
    std::optional<RenderDepthStencilAttachment> depthAttachment = std::nullopt;
    std::optional<RenderDepthStencilAttachment> stencilAttachment = std::nullopt;
  };

  struct TextureClearInfo
  {
    ClearColorValue color{};
    uint32_t baseMipLevel   = 0;
    uint32_t levelCount     = VK_REMAINING_MIP_LEVELS;
    uint32_t baseArrayLayer = 0;
    uint32_t layerCount     = VK_REMAINING_ARRAY_LAYERS;
  };

  class [[nodiscard]] ScopedDebugMarker
  {
  public:
    ScopedDebugMarker(VkCommandBuffer commandBuffer, const char* message, std::array<float, 4> color = {1, 1, 1, 1});
    ~ScopedDebugMarker();

    ScopedDebugMarker(const ScopedDebugMarker&) = delete;
    ScopedDebugMarker(ScopedDebugMarker&&) noexcept = delete;
    ScopedDebugMarker& operator=(const ScopedDebugMarker&) = delete;
    ScopedDebugMarker& operator=(ScopedDebugMarker&&) noexcept = delete;

  private:
    VkCommandBuffer commandBuffer_;
  };

  struct GlobalBarrier
  {
    VkPipelineStageFlags2 srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    VkPipelineStageFlags2 dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
  };

  struct ImageBarrier
  {
    VkPipelineStageFlags2 srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    VkPipelineStageFlags2 dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkAccessFlags2 dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    std::optional<VkImageLayout> oldLayout = std::nullopt;
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_GENERAL;
    std::variant<VkImage, Texture*> image;
  };

  struct CopyBufferInfo
  {
    VkDeviceSize srcOffset = 0;
    VkDeviceSize dstOffset = 0;
    VkDeviceSize size = 0;
  };

  class Context
  {
  public:
    Context(Device& device, VkCommandBuffer commandBuffer);

    void BeginRendering(const RenderInfo& renderInfo) const;
    void EndRendering() const;
    void ImageBarrier(const Texture& texture, VkImageLayout newLayout) const;
    void ImageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT) const;
    void Barriers(std::span<const std::variant<GlobalBarrier, struct ImageBarrier>> barriers);
    // Image barrier from UNDEFINED (discard image contents)
    void ImageBarrierDiscard(const Texture& texture, VkImageLayout newLayout) const;
    void BufferBarrier(const Buffer& buffer) const;
    void BufferBarrier(VkBuffer buffer) const;
    // Everything->everything barrier
    void Barrier() const;

    void ClearTexture(const Texture& texture, const TextureClearInfo& clearInfo) const;

    // Texture layout must be TRANSFER_DST_OPTIMAL or GENERAL
    void CopyBufferToTexture(const Buffer& src, Texture& dst, const TextureUpdateInfo& info);
    void CopyBuffer(const Buffer& src, Buffer& dst, const CopyBufferInfo& copyInfo);

    template<typename T>
      requires std::is_trivially_copyable_v<T>
    void TeenyBufferUpdate(Buffer& buffer, const T& data, size_t offset = 0) const
    {
      TeenyBufferUpdate(buffer, TriviallyCopyableByteSpan(data), offset);
    }

    void TeenyBufferUpdate(Buffer& buffer, TriviallyCopyableByteSpan data, size_t offset = 0) const;

    void BindGraphicsPipeline(const GraphicsPipeline& pipeline) const;
    void BindComputePipeline(const ComputePipeline& pipeline) const;

    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) const;
    void Dispatch(Extent3D groupCount) const;
    void DispatchInvocations(uint32_t invocationCountX, uint32_t invocationCountY, uint32_t invocationCountZ) const;
    void DispatchInvocations(Extent3D invocationCount) const;
    void DispatchIndirect(const Fvog::Buffer& buffer, VkDeviceSize offset = 0) const;

    void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) const;
    void DrawIndirect(const Fvog::Buffer& buffer, VkDeviceSize bufferOffset, uint32_t drawCount, uint32_t stride) const;
    void DrawIndexedIndirect(const Fvog::Buffer& buffer, VkDeviceSize bufferOffset, uint32_t drawCount, uint32_t stride) const;

    void BindIndexBuffer(const Buffer& buffer, VkDeviceSize offset, VkIndexType indexType) const;

    template<typename T>
      requires std::is_trivially_copyable_v<T>
    void SetPushConstants(const T& data, uint32_t offset = 0) const
    {
      SetPushConstants(TriviallyCopyableByteSpan(data), offset);
    }

    void SetPushConstants(TriviallyCopyableByteSpan values, uint32_t offset = 0) const;

    ScopedDebugMarker MakeScopedDebugMarker(const char* message, std::array<float, 4> color = {1, 1, 1, 1}) const;

  private:
    Fvog::Device* device_{};
    VkCommandBuffer commandBuffer_{};
    mutable const ComputePipeline* boundComputePipeline_{};
  };
}
