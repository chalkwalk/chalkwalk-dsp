// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

// A Cytomic-form state-variable filter: one 12 dB/octave pole pair, four
// outputs from the same state.
//
// Two projects had this and their process() bodies were identical to the line.
// What differed was everything AROUND it, and each had half of what the other
// needed:
//
//   RAW COEFFICIENTS. A sequencer whose filter cutoff is already a smoothed
//   per-sample parameter has computed g and k itself and does not want tan()
//   called again inside the filter. `setCoeffs`.
//
//   HERTZ AND Q. Everything else wants to say what it means, and wants the
//   edges handled once rather than at each call site: tan() runs away at
//   Nyquist, a cutoff of zero makes the filter a wire, and a Q of zero divides
//   by it. `set`.
//
//   DENORMAL FLUSHING. One had it and one did not, and the one that did not
//   has a filter on every track. See Denormal.h -- this is a real cost on an
//   idle track, not a theoretical one.
//
// So the merged version has both entry points and always flushes. Nothing is
// traded away: a caller that computed its own coefficients keeps doing that,
// and gains the flush it was missing.

#include <chalkwalk/dsp/Denormal.h>

#include <cmath>

namespace chalkwalk::dsp {

struct Svf {
  enum Mode { LowPass = 0, HighPass = 1, BandPass = 2, Notch = 3 };

  float ic1eq = 0.0f, ic2eq = 0.0f;
  float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 0.0f;

  // g is tan(pi * fc / sr); k is 1/Q. For callers that already have them.
  void setCoeffs(float g, float damping) noexcept {
    k = damping;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }

  // The same thing, said in the units a musician uses.
  //
  // The cutoff is clamped short of Nyquist because tan() goes to infinity
  // there, and away from zero because a g of zero makes the filter a wire --
  // a "closed" lowpass that passes everything is the kind of bug that survives
  // a listening test because it only happens at the end of a sweep.
  void set(double cutoffHz, double q, double sampleRate) noexcept {
    if (sampleRate <= 0.0)
      return;
    constexpr double kPi = 3.14159265358979323846;
    const double ceiling = 0.45 * sampleRate;
    const double fc = cutoffHz < 1.0 ? 1.0 : (cutoffHz > ceiling ? ceiling : cutoffHz);
    const double safeQ = q < 0.05 ? 0.05 : q;
    setCoeffs(static_cast<float>(std::tan(kPi * fc / sampleRate)),
              static_cast<float>(1.0 / safeQ));
  }

  [[nodiscard]] float process(float v, Mode mode) noexcept {
    const float v3 = v - ic2eq;
    const float v1 = a1 * ic1eq + a2 * v3;
    const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
    ic1eq = flush(2.0f * v1 - ic1eq);
    ic2eq = flush(2.0f * v2 - ic2eq);

    switch (mode) {
    case LowPass:  return v2;
    case HighPass: return v - k * v1 - v2;
    case BandPass: return v1;
    default:       return v - k * v1;
    }
  }

  void reset() noexcept { ic1eq = ic2eq = 0.0f; }
};

}  // namespace chalkwalk::dsp
