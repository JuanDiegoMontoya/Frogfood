#pragma once

#include <volk.h>

#include <memory>

namespace Fvog::detail
{
  template<class T>
  [[nodiscard]] T* Address(T&& v)
  {
    return std::addressof(v);
  }

  constexpr size_t AlignUp(size_t value, size_t alignment)
  {
    return (value + alignment - 1) & ~(alignment - 1);
  }

  void CheckVkResult(VkResult result);
}