// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

#include <chalkwalk/dsp/Denormal.h>
#include <chalkwalk/dsp/Pi.h>

#include <cmath>

namespace chalkwalk::dsp {

// A one-pole low-pass, and the high-pass that is what it leaves behind.
//
// 6 dB per octave either way. The high-pass is `x - lowPass(x)`, which makes
// the pair COMPLEMENTARY: low + high is the input, sample for sample and to
// float rounding, whatever the cutoff. That is the property a tilt EQ or a band
// split is built on: set both gains to one and the signal passes untouched, not
// merely "flat within a filter's tolerance".
//
// The coefficient is the matched-z one, 1 - exp(-2 pi fc / sr): the -3 dB
// point is at `fc` while fc is well below Nyquist, which is everywhere this is
// meant to be used (rumble filters, tilt pivots, detector smoothing). The
// high-pass's pass band sits slightly low, by a/2 at Nyquist: under 0.1 dB for
// a corner of 100 Hz at 48 kHz.
struct OnePole {
  float a = 1.0f;
  float z = 0.0f;

  void set(double cutoffHz, double sampleRate) noexcept {
    if (sampleRate <= 0.0)
      return;
    const double fc = cutoffHz < 0.1 ? 0.1 : (cutoffHz > 0.45 * sampleRate ? 0.45 * sampleRate : cutoffHz);
    a = (float)(1.0 - std::exp(-2.0 * kPi * fc / sampleRate));
  }

  [[nodiscard]] float lowPass(float x) noexcept {
    z = flush(z + a * (x - z));
    return z;
  }

  [[nodiscard]] float highPass(float x) noexcept { return x - lowPass(x); }

  void reset() noexcept { z = 0.0f; }
};

}  // namespace chalkwalk::dsp
