#pragma once
#include <span>
#include <type_traits>
#include <cstddef>

namespace Fvog
{
  /// @brief Used to constrain the types accpeted by Buffer
  class TriviallyCopyableByteSpan : public std::span<const std::byte>
  {
  public:
    template<typename T>
      requires std::is_trivially_copyable_v<T>
    TriviallyCopyableByteSpan(const T& t) : std::span<const std::byte>(std::as_bytes(std::span{&t, static_cast<size_t>(1)}))
    {
    }

    template<typename T>
      requires std::is_trivially_copyable_v<T>
    TriviallyCopyableByteSpan(std::span<const T> t) : std::span<const std::byte>(std::as_bytes(t))
    {
    }

    template<typename T>
      requires std::is_trivially_copyable_v<T>
    TriviallyCopyableByteSpan(std::span<T> t) : std::span<const std::byte>(std::as_bytes(t))
    {
    }
  };
} // namespace Fvog