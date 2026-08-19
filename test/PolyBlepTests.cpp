// SPDX-License-Identifier: MIT
#include "AliasMeasure.h"
#include "LegacyCheck.h"

#include <chalkwalk/dsp/PolyBlep.h>

#include <string>

using namespace chalkwalk::dsp;

namespace {

constexpr double kSr = 48000.0;
constexpr int kN = 1 << 14;

double naiveSawAlias(double f0) {
  return measure::aliasDb(
      measure::render([](double p, double) { return 2.0 * p - 1.0; }, f0, kSr, kN),
      f0, kSr);
}
double blepSawAlias(double f0) {
  return measure::aliasDb(
      measure::render([](double p, double d) { return (double)polyBlepSaw(p, d); },
                      f0, kSr, kN),
      f0, kSr);
}
// The bug that shipped: the same code with the correction ADDED.
double invertedSawAlias(double f0) {
  return measure::aliasDb(
      measure::render(
          [](double p, double d) { return 2.0 * p - 1.0 + (double)polyBlep(p, d); },
          f0, kSr, kN),
      f0, kSr);
}

}  // namespace

// ===========================================================================
// THE SIGN. Measured, because it is invisible to any assertion about shape.
// ===========================================================================

TEST_CASE("the correction reduces aliasing, at every pitch") {
  for (double f0 : {110.0, 440.0, 1760.0, 3520.0}) {
    const double naive = naiveSawAlias(f0);
    const double blep = blepSawAlias(f0);
    CHECK_MSG(blep < naive, "polyBLEP made " + std::to_string((int)f0) +
                                " Hz worse: " + std::to_string(blep) +
                                " dB against a naive " + std::to_string(naive));
  }
}

TEST_CASE("an inverted correction is worse than no correction at all") {
  // This is the whole reason the tests measure rather than assert shapes. The
  // inverted version reinforces the discontinuity instead of cancelling it, so
  // it is not merely "less good" -- it is worse than leaving it alone, which
  // is what makes the bug survivable for years while sounding like a slightly
  // different oscillator.
  for (double f0 : {440.0, 1760.0}) {
    CHECK_MSG(invertedSawAlias(f0) > naiveSawAlias(f0),
              "the inverted sign should be WORSE than naive at " +
                  std::to_string((int)f0) + " Hz");
    CHECK_MSG(blepSawAlias(f0) < invertedSawAlias(f0),
              "the correct sign should beat the inverted one at " +
                  std::to_string((int)f0) + " Hz");
  }
}

// ===========================================================================
// The correction function itself.
// ===========================================================================

TEST_CASE("the correction is zero away from a discontinuity") {
  const double dt = 0.01;
  for (double t = 2.0 * dt; t < 1.0 - 2.0 * dt; t += 0.01)
    CHECK_MSG(polyBlep(t, dt) == 0.0f, "non-zero mid-cycle at t=" + std::to_string(t));
}

TEST_CASE("a zero or negative increment corrects nothing") {
  for (double t = 0.0; t < 1.0; t += 0.1) {
    CHECK_MSG(polyBlep(t, 0.0) == 0.0f, "zero increment");
    CHECK_MSG(polyBlep(t, -0.5) == 0.0f, "negative increment");
  }
}

TEST_CASE("the correction is bounded") {
  for (double dt = 0.001; dt < 0.4; dt *= 1.5)
    for (double t = 0.0; t < 1.0; t += 0.001) {
      const float c = polyBlep(t, dt);
      CHECK_MSG(c > -1.001f && c < 1.001f,
                "unbounded correction " + std::to_string(c));
    }
}

TEST_CASE("the saw stays in range") {
  for (double f0 : {55.0, 440.0, 5000.0})
    for (double v : measure::render(
             [](double p, double d) { return (double)polyBlepSaw(p, d); },
             f0, kSr, 4096))
      CHECK_MSG(v > -2.5 && v < 2.5, "saw left the rails: " + std::to_string(v));
}

// ===========================================================================
// The pulse width rule, which neither project had right.
// ===========================================================================

TEST_CASE("a pulse narrower than the method can carry is widened, not dirtied") {
  // The rule is a multiple of the INCREMENT, so it protects at every pitch.
  for (double f0 : {110.0, 880.0, 3520.0}) {
    const double inc = f0 / kSr;
    const double margin = kMinPulseWidthInIncrements * inc;

    const auto asked = measure::render(
        [](double p, double d) { return (double)polyBlepPulse(p, d, 0.0001); },
        f0, kSr, kN);
    const auto clamped = measure::render(
        [margin](double p, double d) { return (double)polyBlepPulse(p, d, margin); },
        f0, kSr, kN);

    // Asking for an impossibly narrow pulse must give the narrowest CLEAN one.
    for (std::size_t i = 0; i < asked.size(); ++i)
      CHECK_MSG(asked[i] == clamped[i],
                "an unusable width was not clamped to the usable one at " +
                    std::to_string((int)f0) + " Hz");
  }
}

TEST_CASE("the fixed clamp the other project used does not protect at pitch") {
  // 0.05 was one project's floor. At 3520 Hz it is two thirds of an increment,
  // so it is inside the region the clamp exists to keep out -- which is why the
  // rule is increment-relative. Asserted as a fact about the numbers rather
  // than about the audio, because it is arithmetic.
  const double inc = 3520.0 / kSr;
  CHECK_MSG(0.05 < kMinPulseWidthInIncrements * inc,
            "the fixed 0.05 floor should be BELOW the safe margin at 3520 Hz");
  const double lowInc = 110.0 / kSr;
  CHECK_MSG(0.05 > kMinPulseWidthInIncrements * lowInc * 4.0,
            "and far above it at 110 Hz, forbidding pulses that were clean");
}

TEST_CASE("a pulse is band-limited better than a naive one") {
  for (double f0 : {220.0, 880.0}) {
    const double naive = measure::aliasDb(
        measure::render([](double p, double) { return p < 0.5 ? 1.0 : -1.0; },
                        f0, kSr, kN),
        f0, kSr);
    const double blep = measure::aliasDb(
        measure::render(
            [](double p, double d) { return (double)polyBlepPulse(p, d, 0.5); },
            f0, kSr, kN),
        f0, kSr);
    CHECK_MSG(blep < naive, "the pulse correction made " +
                                std::to_string((int)f0) + " Hz worse");
  }
}

TEST_CASE("above a quarter of the sample rate a pulse becomes a square") {
  // There is no room for a duty cycle left, and pretending otherwise produces
  // a correction that overlaps itself.
  const double inc = 0.3;   // well past the limit
  for (double asked : {0.1, 0.25, 0.5, 0.75, 0.9})
    for (double p = 0.0; p < 1.0; p += 0.05)
      CHECK_MSG(polyBlepPulse(p, inc, asked) == polyBlepPulse(p, inc, 0.5),
                "a duty cycle survived past the limit");
}
