// SPDX-License-Identifier: MIT
#include "LegacyCheck.h"

#include <chalkwalk/dsp/Svf.h>

#include <cmath>
#include <string>
#include <vector>

using namespace chalkwalk::dsp;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Steady-state amplitude response at one frequency: drive a sine in, measure
// what comes out after the transient has passed.
double gainAt(Svf::Mode mode, double cutoffHz, double q, double testHz) {
  Svf f;
  f.set(cutoffHz, q, kSr);
  const int settle = 8192, measure = 8192;
  double peak = 0.0;
  for (int i = 0; i < settle + measure; ++i) {
    const auto x = static_cast<float>(std::sin(2.0 * kPi * testHz * i / kSr));
    const float y = f.process(x, mode);
    if (i >= settle)
      peak = std::max(peak, std::abs(static_cast<double>(y)));
  }
  return peak;
}

}  // namespace

TEST_CASE("a lowpass passes what is below it and stops what is above") {
  CHECK_MSG(gainAt(Svf::LowPass, 1000.0, 0.707, 100.0) > 0.9, "100 Hz should pass");
  CHECK_MSG(gainAt(Svf::LowPass, 1000.0, 0.707, 10000.0) < 0.1, "10 kHz should not");
}

TEST_CASE("a highpass does the opposite") {
  CHECK_MSG(gainAt(Svf::HighPass, 1000.0, 0.707, 10000.0) > 0.9, "10 kHz should pass");
  CHECK_MSG(gainAt(Svf::HighPass, 1000.0, 0.707, 100.0) < 0.1, "100 Hz should not");
}

TEST_CASE("a bandpass peaks at its cutoff") {
  const double at = gainAt(Svf::BandPass, 1000.0, 4.0, 1000.0);
  CHECK_MSG(at > gainAt(Svf::BandPass, 1000.0, 4.0, 250.0), "should beat an octave below");
  CHECK_MSG(at > gainAt(Svf::BandPass, 1000.0, 4.0, 4000.0), "and one above");
}

TEST_CASE("a notch removes its cutoff and passes the rest") {
  CHECK_MSG(gainAt(Svf::Notch, 1000.0, 4.0, 1000.0) < 0.2, "the notch should bite");
  CHECK_MSG(gainAt(Svf::Notch, 1000.0, 4.0, 100.0) > 0.8, "well below should pass");
  CHECK_MSG(gainAt(Svf::Notch, 1000.0, 4.0, 10000.0) > 0.8, "well above should pass");
}

TEST_CASE("the cutoff is where it says it is") {
  // -3 dB at the corner for a Butterworth Q, within a tolerance that allows
  // for the discretisation.
  const double at = gainAt(Svf::LowPass, 1000.0, 0.707, 1000.0);
  CHECK_MSG(at > 0.60 && at < 0.80,
            "expected about -3 dB at the corner, got " + std::to_string(at));
}

TEST_CASE("resonance lifts the corner") {
  CHECK_MSG(gainAt(Svf::LowPass, 1000.0, 8.0, 1000.0) >
                gainAt(Svf::LowPass, 1000.0, 0.707, 1000.0),
            "a high Q should peak at the corner");
}

// ===========================================================================
// The guards, which one project had and the other did not.
// ===========================================================================

TEST_CASE("a cutoff at or past Nyquist does not blow up") {
  for (double hz : {24000.0, 30000.0, 1.0e9}) {
    Svf f;
    f.set(hz, 0.707, kSr);
    float acc = 0.0f;
    for (int i = 0; i < 4096; ++i)
      acc += f.process(static_cast<float>(std::sin(i * 0.1)), Svf::LowPass);
    CHECK_MSG(std::isfinite(acc), "cutoff " + std::to_string(hz) + " produced " +
                                      std::to_string(acc));
  }
}

TEST_CASE("a cutoff of zero does not make the filter a wire") {
  // The bug this guard exists for: g == 0 gives a1 == 1 and a lowpass that
  // passes everything, which is the opposite of a closed filter and only shows
  // up at the very end of a sweep.
  CHECK_MSG(gainAt(Svf::LowPass, 0.0, 0.707, 1000.0) < 0.1,
            "a closed lowpass must not pass 1 kHz");
  CHECK_MSG(gainAt(Svf::LowPass, -50.0, 0.707, 1000.0) < 0.1,
            "and neither must a negative one");
}

TEST_CASE("a Q of zero does not divide by it") {
  Svf f;
  f.set(1000.0, 0.0, kSr);
  CHECK_MSG(std::isfinite(f.k), "k should be finite");
  float acc = 0.0f;
  for (int i = 0; i < 1024; ++i)
    acc += f.process(0.5f, Svf::LowPass);
  CHECK_MSG(std::isfinite(acc), "output should be finite");
}

TEST_CASE("a sample rate of zero is survivable") {
  Svf f;
  const float before = f.a1;
  f.set(1000.0, 0.707, 0.0);
  CHECK_MSG(f.a1 == before, "an impossible rate should leave the filter alone");
}

// ===========================================================================
// Denormal flushing: the half one project was missing, on every track.
// ===========================================================================

TEST_CASE("the state reaches exactly zero after silence") {
  // Without the flush this decays toward zero forever and lands in denormal
  // range, where the arithmetic gets tens to hundreds of times slower. The
  // audible symptom is a dropout on an IDLE track, which is the opposite of
  // where anyone looks for one.
  Svf f;
  f.set(1000.0, 0.707, kSr);
  for (int i = 0; i < 64; ++i)
    (void)f.process(1.0f, Svf::LowPass);
  for (int i = 0; i < 200000; ++i)
    (void)f.process(0.0f, Svf::LowPass);

  CHECK_MSG(f.ic1eq == 0.0f, "state 1 never reached zero: " + std::to_string(f.ic1eq));
  CHECK_MSG(f.ic2eq == 0.0f, "state 2 never reached zero: " + std::to_string(f.ic2eq));
}

TEST_CASE("flushing does not disturb an audible signal") {
  // The flush level is far below the noise floor of any converter, so it must
  // be inaudible where it matters: a real signal through the filter must be
  // unchanged by its presence.
  CHECK_MSG(gainAt(Svf::LowPass, 1000.0, 0.707, 100.0) > 0.9,
            "the flush should not touch a normal signal");
}

// ===========================================================================
// Both entry points, which is the merge.
// ===========================================================================

TEST_CASE("setCoeffs and set agree when given the same filter") {
  const double cutoff = 1200.0, q = 1.5;
  Svf viaHz;
  viaHz.set(cutoff, q, kSr);

  Svf viaRaw;
  viaRaw.setCoeffs(static_cast<float>(std::tan(kPi * cutoff / kSr)),
                   static_cast<float>(1.0 / q));

  CHECK_MSG(viaHz.a1 == viaRaw.a1, "a1 differs");
  CHECK_MSG(viaHz.a2 == viaRaw.a2, "a2 differs");
  CHECK_MSG(viaHz.a3 == viaRaw.a3, "a3 differs");
  CHECK_MSG(viaHz.k == viaRaw.k, "k differs");
}

TEST_CASE("reset clears the state") {
  Svf f;
  f.set(1000.0, 0.707, kSr);
  for (int i = 0; i < 100; ++i)
    (void)f.process(1.0f, Svf::LowPass);
  f.reset();
  CHECK_MSG(f.ic1eq == 0.0f && f.ic2eq == 0.0f, "reset left state behind");
}
