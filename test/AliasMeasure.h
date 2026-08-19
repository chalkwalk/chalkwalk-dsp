// SPDX-License-Identifier: MIT
#pragma once

// Measuring aliasing, rather than asserting a shape.
//
// The bug this library exists partly to prevent -- an inverted polyBLEP sign --
// produces a waveform that LOOKS plausible and passes any test written about
// its shape. It shipped for years behind a test that passed because of it. So
// the oscillator tests here render a tone, sort the spectrum into bins that are
// harmonics of the fundamental and bins that are not, and assert on the ratio.
// A correction with the wrong sign makes that ratio worse than no correction at
// all, which is a fact no shape assertion can express.

#include <cmath>
#include <vector>

namespace measure {

// Energy in non-harmonic bins relative to harmonic bins, in dB. Lower is
// cleaner. Uses Goertzel per bin -- slower than an FFT and one fewer dependency
// for a library that has none.
[[nodiscard]] inline double aliasDb(const std::vector<double> &x, double f0,
                                    double sampleRate) {
  const int n = static_cast<int>(x.size());
  if (n < 8 || f0 <= 0.0 || sampleRate <= 0.0)
    return 0.0;

  const double binHz = sampleRate / n;
  double harmonic = 0.0, other = 0.0;

  for (int b = 1; b < n / 2; ++b) {
    const double w = 2.0 * 3.14159265358979323846 * b / n;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) {
      const double s0 = x[static_cast<std::size_t>(i)] + c * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    const double mag = s1 * s1 + s2 * s2 - c * s1 * s2;

    const double ratio = (b * binHz) / f0;
    const double nearest = std::round(ratio);
    const bool isHarmonic = nearest >= 1.0 &&
                            std::abs(ratio - nearest) < 2.0 * binHz / f0;
    if (isHarmonic)
      harmonic += mag;
    else
      other += mag;
  }
  return 10.0 * std::log10((other + 1e-30) / (harmonic + 1e-30));
}

// Render `n` samples of a generator that takes (phase, increment).
template <typename Gen>
[[nodiscard]] inline std::vector<double> render(Gen gen, double f0,
                                                double sampleRate, int n) {
  std::vector<double> out(static_cast<std::size_t>(n), 0.0);
  const double inc = f0 / sampleRate;
  double phase = 0.0;
  for (int i = 0; i < n; ++i) {
    out[static_cast<std::size_t>(i)] = gen(phase, inc);
    phase += inc;
    if (phase >= 1.0)
      phase -= 1.0;
  }
  return out;
}

}  // namespace measure
