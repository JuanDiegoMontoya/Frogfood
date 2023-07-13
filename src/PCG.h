#pragma once
#include <cstdint>
#include <bit>

namespace PCG
{
  constexpr std::uint32_t Hash(std::uint32_t seed)
  {
    std::uint32_t state = seed * 747796405u + 2891336453u;
    std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
  }

  // Used to advance the PCG state.
  constexpr std::uint32_t RandU32(std::uint32_t& rng_state)
  {
    std::uint32_t state = rng_state;
    rng_state = rng_state * 747796405u + 2891336453u;
    std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
  }

  // Advances the prng state and returns the corresponding random float.
  constexpr float RandFloat(std::uint32_t& state, float min = 0, float max = 1)
  {
    state = RandU32(state);
    float f = float(state) * std::bit_cast<float>(0x2f800004u);
    return f * (max - min) + min;
  }
}