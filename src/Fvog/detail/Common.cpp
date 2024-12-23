#include "Common.h"
#include <stdexcept>
#include <string>

namespace Fvog::detail
{
  void CheckVkResult(VkResult result)
  {
    // TODO: don't throw for certain non-success codes (since they aren't always errors)
    if (result != VK_SUCCESS)
    {
      throw std::runtime_error("vkResult was not VK_SUCCESS. Code: " + std::to_string(result));
    }
  }
} // namespace Fvog::detail
