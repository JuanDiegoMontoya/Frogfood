#ifndef HASH_H
#define HASH_H

///////////////////////////////////////////////////////////////////////////////
// PCG family of hashes/pRNGs
///////////////////////////////////////////////////////////////////////////////

uint PCG_Hash(uint seed)
{
  uint state = seed * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

// Used to advance the PCG state.
uint PCG_RandU32(inout uint rng_state)
{
  uint state = rng_state;
  rng_state = rng_state * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

// Advances the prng state and returns the corresponding random float.
float PCG_RandFloat(inout uint state, float min_, float max_)
{
  state = PCG_RandU32(state);
  float f = float(state) * uintBitsToFloat(0x2f800004u);
  return f * (max_ - min_) + min_;
}

///////////////////////////////////////////////////////////////////////////////
// Miscellany
// mostly sin(fract(..)) stuff that I don't approve of but am too lazy to change
///////////////////////////////////////////////////////////////////////////////

float MM_Hash2(vec2 v)
{
  return fract(1e4 * sin(17.0 * v.x + 0.1 * v.y) * (0.1 + abs(sin(13.0 * v.y + v.x))));
}

float MM_Hash3(vec3 v)
{
  return MM_Hash2(vec2(MM_Hash2(v.xy), v.z));
}

#endif // HASH_H
