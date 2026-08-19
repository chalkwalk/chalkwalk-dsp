// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

#include <cmath>

namespace chalkwalk::dsp {

// A soft-knee ceiling that is EXACTLY unity below the knee.
//
// Unlike a plain tanh, which starts compressing audibly from around -6 dBFS,
// this is the identity below `knee` and only engages above it, saturating
// smoothly toward `ceiling` and never exceeding it. A mix staged below the
// knee passes through bit-identical; only peaks that would approach full scale
// are caught.
//
// Construction: above the knee the excess is mapped through a tanh scaled so
// the curve is C1-continuous at the knee -- slope exactly 1 there -- and
// asymptotes to the ceiling. Branch-light: a sign, one compare, one tanh.
//
// THE KNEE AND CEILING ARE POLICY, NOT PRIMITIVE, which is why they are
// arguments. Two projects had this function with different constants baked in
// and both called it with no arguments, so "the default" was silently
// different in each. The defaults here are the transparent master-bus pair;
// anything shaping a voice rather than protecting an output wants its own, and
// should say so at the call site where the reason lives.
inline constexpr float kMasterKnee = 0.71f;     // -3 dBFS
inline constexpr float kMasterCeiling = 0.99f;  // -0.1 dBFS

[[nodiscard]] inline float softClip(float x, float knee = kMasterKnee,
                                    float ceiling = kMasterCeiling) noexcept {
  const float range = ceiling - knee;
  if (range <= 0.0f)
    return x;

  const float a = std::abs(x);
  if (a <= knee)
    return x;  // transparent

  const float shaped = knee + range * std::tanh((a - knee) / range);
  return std::copysign(shaped, x);
}

}  // namespace chalkwalk::dsp
