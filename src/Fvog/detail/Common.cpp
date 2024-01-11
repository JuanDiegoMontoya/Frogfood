#include "Common.h"
#include <stdexcept>

namespace Fvog::detail
{
  void CheckVkResult(VkResult result)
  {
    // TODO: don't throw for certain non-success codes (since they aren't always errors)
    if (result != VK_SUCCESS)
    {
      throw std::runtime_error("result was not VK_SUCCESS");
    }
  }
} // namespace Fvog::detail
