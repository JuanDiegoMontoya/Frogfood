#pragma once

#define NO_COPY(T)                 \
  T(const T&)            = delete; \
  T& operator=(const T&) = delete

#define NO_MOVE(T)                     \
  T(T&&) noexcept            = delete; \
  T& operator=(T&&) noexcept = delete

#define NO_COPY_NO_MOVE(T) \
  NO_COPY(T);              \
  NO_MOVE(T)

#define DEFAULT_MOVE(T)                 \
  T(T&&) noexcept            = default; \
  T& operator=(T&&) noexcept = default
