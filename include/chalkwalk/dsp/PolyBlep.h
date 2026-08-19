// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

// Band-limited step correction for naive oscillators.
//
// A naive saw or pulse is a sampled discontinuity, and a discontinuity has
// energy above Nyquist that folds back as aliasing -- inharmonic tones that
// move the wrong way when you play a scale. polyBLEP splices a short polynomial
// across each discontinuity, spanning one sample either side, which removes
// most of that fold-back for two multiplies and a compare.
//
// THE SIGN IS THE WHOLE THING. The correction is SUBTRACTED from a rising
// discontinuity and ADDED to a falling one. Getting it backwards does not
// sound obviously broken -- it sounds like a slightly different oscillator --
// and it makes aliasing measurably WORSE than doing nothing at all, because
// the correction now reinforces the discontinuity instead of cancelling it.
// One of these projects shipped that inversion for its entire life, with a
// test that passed BECAUSE of it. The tests here measure aliasing against an
// oversampled reference rather than asserting shapes, for that reason.

namespace chalkwalk::dsp {

// The correction itself. `t` is phase in [0, 1); `dt` is the phase increment
// per sample, which is frequency / sampleRate.
[[nodiscard]] inline float polyBlep(double t, double dt) noexcept {
  if (dt <= 0.0)
    return 0.0f;
  if (t < dt) {
    const double x = t / dt;
    return static_cast<float>(x + x - x * x - 1.0);
  }
  if (t > 1.0 - dt) {
    const double x = (t - 1.0) / dt;
    return static_cast<float>(x * x + x + x + 1.0);
  }
  return 0.0f;
}

[[nodiscard]] inline float polyBlepSaw(double phase, double increment) noexcept {
  return static_cast<float>(2.0 * phase - 1.0) - polyBlep(phase, increment);
}

// How narrow a pulse may get before the method stops working, as a MULTIPLE OF
// THE PHASE INCREMENT rather than as a fixed width.
//
// A pulse has two discontinuities and each correction spans one sample either
// side of its own. If the two are closer together than that, the corrections
// overlap and neither is right -- so the limit is a property of the SAMPLE
// RATE relative to the frequency, not a number of cycles.
//
// One project clamped width to [0.05, 0.95] and the other did not clamp at all.
// Measured against an oversampled reference, sweeping width as a multiple of
// the increment, aliasing collapses below about 2x and plateaus above it. Which
// makes the fixed clamp wrong at both ends: at 110 Hz, 0.05 is twenty-one
// increments, so it forbids narrow pulses that would have been perfectly
// clean; at 3520 Hz it is two THIRDS of an increment, so it does not protect
// at all -- the case it exists for is exactly the case it misses.
inline constexpr double kMinPulseWidthInIncrements = 2.0;

// A band-limited pulse of the given duty cycle.
//
// `width` is clamped to what the method can actually band-limit at this
// increment. A caller asking for a narrower pulse than the sample rate can
// carry gets the narrowest clean one rather than a dirty version of what it
// asked for.
[[nodiscard]] inline float polyBlepPulse(double phase, double increment,
                                         double width) noexcept {
  const double margin = kMinPulseWidthInIncrements * increment;
  if (margin * 2.0 >= 1.0) {
    // Above roughly a quarter of the sample rate there is no room for a duty
    // cycle at all; a square is the only pulse left that means anything.
    width = 0.5;
  } else {
    if (width < margin)
      width = margin;
    if (width > 1.0 - margin)
      width = 1.0 - margin;
  }

  float s = phase < width ? 1.0f : -1.0f;
  s += polyBlep(phase, increment);          // up at the start of the cycle
  double atWidth = phase - width;
  if (atWidth < 0.0)
    atWidth += 1.0;
  s -= polyBlep(atWidth, increment);        // down at the width
  return s;
}

}  // namespace chalkwalk::dsp
