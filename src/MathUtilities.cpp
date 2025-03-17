#include "MathUtilities.h"
#include <cmath>

#define PI (3.14159265f)

float Math::Ease(float t, Easing easing)
{
  switch (easing)
  {
  case Easing::LINEAR: return t;
  case Easing::EASE_IN_SINE: return EaseInSine(t);
  case Easing::EASE_OUT_SINE: return EaseOutSine(t);
  case Easing::EASE_IN_OUT_BACK: return EaseInOutBack(t);
  case Easing::EASE_IN_CUBIC: return EaseInCubic(t);
  case Easing::EASE_OUT_CUBIC: return EaseOutCubic(t);
  default: assert(false); return t;
  }
}

float Math::EaseInSine(float t)
{
  return 1 - std::cos(t * PI / 2);
}

float Math::EaseOutSine(float t)
{
  return std::sin(t * PI / 2);
}

float Math::EaseInOutBack(float t)
{
  constexpr auto c1 = 1.70158f;
  constexpr auto c2 = c1 * 1.525f;

  return t < 0.5 ? 
    (std::pow(2 * t, 2.0f) * ((c2 + 1) * 2 * t - c2)) / 2 :
    (std::pow(2 * t - 2, 2.0f) * ((c2 + 1) * (t * 2 - 2) + c2) + 2) / 2;
}

float Math::EaseInCubic(float t)
{
  return t * t * t;
}

float Math::EaseOutCubic(float t)
{
  const float t1 = 1.0f - t;
  return 1.0f - t1 * t1 * t1;
}
