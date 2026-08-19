// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

namespace chalkwalk::dsp {

// 4-point, 3rd-order Hermite interpolation.
//
// For reading a buffer at a fractional position. `t` is in [0, 1) between `y0`
// and `y1`, with `ym1` and `y2` the neighbours either side -- so a caller needs
// one sample of history and one of lookahead, which is why this is not what a
// feedback loop should use.
//
// Chosen over linear because linear interpolation is a lowpass whose corner
// moves with the fractional position: a swept delay or a varispeed read using
// it loses treble in a way that changes as it sweeps, which is audible as the
// sound going dull at some speeds and not others. Hermite's error is far
// smaller and far flatter across the fraction.
[[nodiscard]] inline float hermite4(float ym1, float y0, float y1, float y2,
                                    float t) noexcept {
  const float c0 = y0;
  const float c1 = 0.5f * (y1 - ym1);
  const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
  const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
  return ((c3 * t + c2) * t + c1) * t + c0;
}

}  // namespace chalkwalk::dsp
