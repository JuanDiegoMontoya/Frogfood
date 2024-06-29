#include "Timer2.h"
#include "Device.h"
#include "detail/Common.h"

#include <volk.h>

#include <cassert>

namespace Fvog
{
  TimerQueryAsync::TimerQueryAsync(Device& device, uint32_t N)
    : device_(&device),
      capacity_(N)
  {
    assert(capacity_ > 0);

    vkCreateQueryPool(device_->device_,
      detail::Address(VkQueryPoolCreateInfo{
        .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType  = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = N * 2, // Each query needs a start and end, hence the count is doubled
      }),
      nullptr,
      &queryPool_);

    vkResetQueryPool(device_->device_, queryPool_, 0, N * 2);
  }

  TimerQueryAsync::~TimerQueryAsync()
  {
    if (device_ && queryPool_)
    {
      vkDestroyQueryPool(device_->device_, queryPool_, nullptr);
    }
  }

  void TimerQueryAsync::BeginZone(VkCommandBuffer commandBuffer)
  {
    // begin a query if there is at least one inactive
    if (count_ < capacity_)
    {
      //vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, queryPool_, start_);
      vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, queryPool_, start_);
    }
  }

  void TimerQueryAsync::EndZone(VkCommandBuffer commandBuffer)
  {
    // end a query if there is at least one inactive
    if (count_ < capacity_)
    {
      //vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, queryPool_, start_ + capacity_);
      vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, queryPool_, start_ + capacity_);
      start_ = (start_ + 1) % capacity_; // wrap
      count_++;
    }
  }

  std::optional<uint64_t> TimerQueryAsync::PopTimestamp()
  {
    // Return nothing if there is no active query
    if (count_ == 0)
    {
      return std::nullopt;
    }

    // Get the index of the oldest query
    uint32_t index = (start_ + capacity_ - count_) % capacity_;

    // Getting the start result is a sanity check
    struct
    {
      uint64_t timestamp;
      uint64_t availability;
    } startResult{}, endResult{};

    constexpr auto flags = VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
    vkGetQueryPoolResults(device_->device_, queryPool_, index, 1, sizeof(startResult), &startResult, 0, flags);
    vkGetQueryPoolResults(device_->device_, queryPool_, index + capacity_, 1, sizeof(endResult), &endResult, 0, flags);
    
    // The oldest queries' results are not available, abandon ship!
    if (startResult.availability == 0 || endResult.availability == 0)
    {
      return std::nullopt;
    }

    // pop oldest timing and retrieve result
    count_--;
    vkResetQueryPool(device_->device_, queryPool_, index, 1);
    vkResetQueryPool(device_->device_, queryPool_, index + capacity_, 1);
    const auto period = (uint64_t)device_->device_.physical_device.properties.limits.timestampPeriod;
    if (endResult.timestamp > startResult.timestamp)
      return (endResult.timestamp - startResult.timestamp) * period;
    //return startResult.timestamp - endResult.timestamp;
    //__debugbreak();
    return std::nullopt;
  }
} // namespace Fvog