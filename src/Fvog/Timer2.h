#pragma once
#include <vulkan/vulkan_core.h>

#include <utility>
#include <optional>

namespace Fvog
{
  class Device;

  /// @brief Async N-buffered timer query that does not induce pipeline stalls
  ///
  /// Useful for measuring performance of passes every frame without causing stalls.
  /// However, the results returned may be from multiple frames ago,
  /// and results are not guaranteed to be available.
  /// In practice, setting N to 5 should allow at least one query to be available every frame.
  class TimerQueryAsync
  {
  public:
    TimerQueryAsync(Device& device, uint32_t N);
    ~TimerQueryAsync();

    TimerQueryAsync(const TimerQueryAsync&)            = delete;
    TimerQueryAsync& operator=(const TimerQueryAsync&) = delete;

    TimerQueryAsync(TimerQueryAsync&& old) noexcept
      : device_(std::exchange(old.device_, nullptr)),
        queryPool_(std::exchange(old.queryPool_, nullptr)),
        start_(std::exchange(old.start_, 0)),
        count_(std::exchange(old.count_, 0)),
        capacity_(std::exchange(old.capacity_, 0))
    {
    }

    TimerQueryAsync& operator=(TimerQueryAsync&& old) noexcept
    {
      if (&old == this)
        return *this;
      this->~TimerQueryAsync();
      return *new (this) TimerQueryAsync(std::move(old));
    }

    /// @brief Begins a query zone
    ///
    /// @note EndZone must be called before another zone can begin
    void BeginZone(VkCommandBuffer commandBuffer);

    /// @brief Ends a query zone
    ///
    /// @note BeginZone must be called before a zone can end
    void EndZone(VkCommandBuffer commandBuffer);

    /// @brief Gets the latest available query
    /// @return The latest query, if available. Otherwise, std::nullopt is returned
    [[nodiscard]] std::optional<uint64_t> PopTimestamp();

  private:
    Device* device_;
    VkQueryPool queryPool_;
    uint32_t start_{}; // next timer to be used for measurement
    uint32_t count_{}; // number of timers 'buffered', ie measurement was started by result not read yet
    uint32_t capacity_{};
  };

  /// @brief RAII wrapper for TimerQueryAsync
  template<typename T>
  class TimerScoped
  {
  public:
    TimerScoped(T& zone, VkCommandBuffer commandBuffer)
      : zone_(zone),
        commandBuffer_(commandBuffer)
    {
      zone_.BeginZone(commandBuffer_);
    }

    ~TimerScoped()
    {
      zone_.EndZone(commandBuffer_);
    }

    TimerScoped(const TimerScoped&) = delete;
    TimerScoped(TimerScoped&&) noexcept = delete;
    TimerScoped& operator=(const TimerScoped&) = delete;
    TimerScoped& operator=(TimerScoped&&) noexcept = delete;

  private:
    T& zone_;
    VkCommandBuffer commandBuffer_;
  };
} // namespace Fvog