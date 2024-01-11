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

  void CheckVkResult(VkResult result);
}