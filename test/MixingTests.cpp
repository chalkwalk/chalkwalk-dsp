// SPDX-License-Identifier: MIT
// The mixing primitives: one-pole split, tilt, compressor, small room. Each is
// measured against an answer known in advance, not against itself.
#include <chalkwalk/dsp/Compressor.h>
#include <chalkwalk/dsp/OnePole.h>
#include <chalkwalk/dsp/Pi.h>
#include <chalkwalk/dsp/SmallRoom.h>
#include <chalkwalk/dsp/TiltEq.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace chalkwalk::dsp;
using Catch::Approx;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> sine(double hz, double amplitude, double seconds) {
  std::vector<float> v((size_t)(seconds * kRate));
  for (size_t i = 0; i < v.size(); ++i)
    v[i] = (float)(amplitude * std::sin(2.0 * kPi * hz * (double)i / kRate));
  return v;
}

double rmsDb(const std::vector<float> &v, size_t from = 0) {
  double s = 0.0;
  for (size_t i = from; i < v.size(); ++i)
    s += (double)v[i] * v[i];
  return 10.0 * std::log10(s / (double)(v.size() - from) + 1e-30);
}

// Gain in dB of `f` (in place, mono) on a steady sine, over its last half.
template <class F> double gainAt(double hz, F f) {
  auto in = sine(hz, 0.25, 1.0);
  auto out = in;
  f(out.data(), (int)out.size());
  return rmsDb(out, out.size() / 2) - rmsDb(in, in.size() / 2);
}

}  // namespace

TEST_CASE("One-pole: low and high are complementary, and -3 dB at the cutoff", "[mixing]") {
  OnePole p;
  p.set(100.0, kRate);
  const auto in = sine(37.0, 0.5, 0.2);
  // To float rounding (two roundings: the subtraction and the sum), not to a
  // filter's tolerance.
  for (float x : in) {
    const float lo = p.lowPass(x);
    const float hi = x - lo;
    CHECK(std::fabs((lo + hi) - x) <=
          2.5e-7f * std::max({std::fabs(lo), std::fabs(hi), std::fabs(x)}) + 1e-12f);
  }
  auto hp = [](double fc, double hz) {
    OnePole f;
    f.set(fc, kRate);
    return gainAt(hz, [&](float *b, int n) {
      for (int i = 0; i < n; ++i)
        b[i] = f.highPass(b[i]);
    });
  };
  CHECK(hp(100.0, 100.0) == Approx(-3.0).margin(0.15));
  CHECK(hp(100.0, 50.0) == Approx(-7.0).margin(0.2));   // 6 dB per octave below
  CHECK(hp(100.0, 25.0) == Approx(-12.3).margin(0.3));
  // The pass band sits a hair low (1 - a/2 at Nyquist): under 0.1 dB here.
  CHECK(hp(100.0, 2000.0) == Approx(0.0).margin(0.1));
}

TEST_CASE("Tilt: zero is a wire, and a tilt moves the ends by half each", "[mixing]") {
  auto tilted = [](double tiltDb, double hz) {
    TiltEq t;
    t.prepare(kRate, 700.0);
    t.setTiltDb(tiltDb);
    t.reset();  // start at the target, not gliding toward it
    return gainAt(hz, [&](float *b, int n) { t.process(b, nullptr, n); });
  };
  CHECK(tilted(0.0, 60.0) == Approx(0.0).margin(1e-4));
  CHECK(tilted(0.0, 9000.0) == Approx(0.0).margin(1e-4));
  // Positive brightens: far lows down, far highs up, by half the tilt each.
  CHECK(tilted(3.0, 40.0) == Approx(-1.5).margin(0.15));
  CHECK(tilted(3.0, 15000.0) == Approx(1.5).margin(0.15));
  CHECK(tilted(-3.0, 40.0) == Approx(1.5).margin(0.15));
  // Near the pivot the shelves meet: little change either way.
  CHECK(std::fabs(tilted(3.0, 700.0)) < 0.5);
}

TEST_CASE("Tilt: a change glides instead of stepping", "[mixing]") {
  TiltEq t;
  t.prepare(kRate, 700.0, 20.0);
  t.setTiltDb(0.0);
  t.reset();
  t.setTiltDb(6.0);
  auto b = sine(10000.0, 0.25, 0.005);  // 5 ms: a quarter of the glide
  t.process(b.data(), nullptr, (int)b.size());
  const double early = rmsDb(b, b.size() / 2) - 20.0 * std::log10(0.25 / std::sqrt(2.0));
  CHECK(early > 0.1);
  CHECK(early < 2.0);  // well short of the +3 dB it is heading for
}

TEST_CASE("Compressor: the static curve, with a knee that never boosts", "[mixing]") {
  Compressor c;
  Compressor::Settings s;
  s.thresholdDb = -20.0;
  s.ratio = 4.0;
  s.kneeDb = 6.0;
  c.prepare(kRate, s);
  CHECK(c.curveDb(-40.0) == 0.0);
  CHECK(c.curveDb(-23.0) == Approx(0.0).margin(1e-12));          // knee start
  CHECK(c.curveDb(-10.0) == Approx(-7.5));                        // 10 over at 4:1
  CHECK(c.curveDb(-17.0) == Approx(-0.75 * 3.0).margin(1e-9));    // knee end
  // Continuous and never above unity anywhere through the knee.
  double prev = c.curveDb(-30.0);
  for (double x = -30.0; x <= 0.0; x += 0.01) {
    const double g = c.curveDb(x);
    CHECK(g <= 0.0);
    CHECK(std::fabs(g - prev) < 0.01);
    prev = g;
  }
}

TEST_CASE("Compressor: a steady tone settles where the curve says", "[mixing]") {
  Compressor c;
  Compressor::Settings s;
  s.thresholdDb = -30.0;
  s.ratio = 3.0;
  s.kneeDb = 0.0;
  s.attackMs = 5.0;
  s.releaseMs = 50.0;
  s.detectorMs = 20.0;
  c.prepare(kRate, s);
  // A sine of amplitude 0.25 has RMS -15.05 dBFS: 15.05 over, 2/3 of it off.
  const double g = gainAt(1000.0, [&](float *b, int n) { c.process(b, nullptr, n); });
  CHECK(g == Approx(-(15.05 * 2.0 / 3.0)).margin(0.25));
  // Below the threshold it is a wire, exactly.
  Compressor quiet;
  quiet.prepare(kRate, s);
  auto in = sine(1000.0, 0.01, 0.2);
  auto out = in;
  quiet.process(out.data(), nullptr, (int)out.size());
  CHECK(out == in);
}

TEST_CASE("Compressor: both sides decide, and both get the same gain", "[mixing]") {
  Compressor::Settings s;
  s.thresholdDb = -30.0;
  s.ratio = 4.0;
  s.detectorMs = 10.0;
  s.sidechainHighPassHz = 60.0;
  // Loud on the right only: the left must come down too (linked), which the
  // left-only detector this replaces would not have done.
  Compressor c;
  c.prepare(kRate, s);
  auto l = sine(1000.0, 0.001, 0.5);
  auto r = sine(1000.0, 0.5, 0.5);
  const auto l0 = l;
  c.process(l.data(), r.data(), (int)l.size());
  CHECK(c.gainReductionDb() < -10.0);
  const double ratioEnd = (double)l.back() / (double)l0.back();
  CHECK(20.0 * std::log10(std::fabs(ratioEnd)) == Approx(c.gainReductionDb()).margin(0.5));
}

TEST_CASE("Compressor: the sidechain high-pass lets the bass through", "[mixing]") {
  Compressor::Settings s;
  s.thresholdDb = -30.0;
  s.ratio = 4.0;
  s.detectorMs = 20.0;
  s.sidechainHighPassHz = 150.0;
  auto reduction = [&](double hz) {
    Compressor c;
    c.prepare(kRate, s);
    auto b = sine(hz, 0.25, 0.5);
    c.process(b.data(), nullptr, (int)b.size());
    return c.gainReductionDb();
  };
  // 40 Hz is two octaves under the corner: about 12 dB quieter to the
  // detector, so about 9 dB less reduction at 4:1.
  CHECK(reduction(40.0) > reduction(2000.0) + 6.0);
}

TEST_CASE("Compressor: attack is faster than release", "[mixing]") {
  Compressor::Settings s;
  s.thresholdDb = -30.0;
  s.ratio = 10.0;
  s.attackMs = 2.0;
  s.releaseMs = 300.0;
  s.detectorMs = 0.0;  // peak
  Compressor c;
  c.prepare(kRate, s);
  auto loud = sine(1000.0, 0.5, 0.05);
  c.process(loud.data(), nullptr, (int)loud.size());
  const double down = c.gainReductionDb();
  CHECK(down < -15.0);
  auto quiet = sine(1000.0, 0.001, 0.05);
  c.process(quiet.data(), nullptr, (int)quiet.size());
  // 50 ms of a 300 ms release: about 15 % of the way home.
  CHECK(c.gainReductionDb() < down * 0.75);
  CHECK(c.gainReductionDb() > down * 0.95);
}

namespace {
// The room's response to a click, `seconds` long.
void roomImpulse(const SmallRoom::Settings &s, double seconds, std::vector<float> &l,
                 std::vector<float> &r) {
  SmallRoom room;
  room.prepare(kRate, s);
  const size_t n = (size_t)(seconds * kRate);
  std::vector<float> inL(n, 0.0f), inR(n, 0.0f);
  inL[0] = 1.0f;
  inR[0] = 1.0f;
  l.assign(n, 0.0f);
  r.assign(n, 0.0f);
  room.process(inL.data(), inR.data(), l.data(), r.data(), (int)n);
}

double energyDb(const std::vector<float> &v, double from, double to) {
  double e = 0.0;
  for (size_t i = (size_t)(from * kRate); i < (size_t)(to * kRate) && i < v.size(); ++i)
    e += (double)v[i] * v[i];
  return 10.0 * std::log10(e + 1e-30);
}
}  // namespace

TEST_CASE("Small room: it decays at the rate asked, and stays decaying", "[mixing]") {
  SmallRoom::Settings s;
  s.decaySeconds = 0.5;
  s.dampingHz = 20000.0;  // undamped, so the broadband rate is the set one
  s.inputLowPassHz = 20000.0;
  std::vector<float> l, r;
  roomImpulse(s, 2.0, l, r);
  // Energy in 100 ms slices: 60 dB per 0.5 s is 12 dB per 100 ms.
  const double a = energyDb(l, 0.1, 0.2), b = energyDb(l, 0.3, 0.4);
  CHECK((a - b) / 2.0 == Approx(12.0).margin(2.5));
  // A long decay must still decay: the matrix keeps energy, the gains lose it.
  s.decaySeconds = 5.0;
  roomImpulse(s, 6.0, l, r);
  CHECK(energyDb(l, 5.0, 6.0) < energyDb(l, 0.5, 1.5) - 40.0);
}

TEST_CASE("Small room: dark, and the two sides differ", "[mixing]") {
  SmallRoom::Settings s;
  std::vector<float> l, r;
  roomImpulse(s, 1.0, l, r);
  // Highs die sooner: compare a high band's drop with a low band's.
  auto band = [&](double hz, double from) {
    OnePole a, b;
    a.set(hz / 1.4, kRate);
    b.set(hz * 1.4, kRate);
    std::vector<float> v(l.size());
    for (size_t i = 0; i < l.size(); ++i)
      v[i] = b.lowPass(a.highPass(l[i]));
    return energyDb(v, from, from + 0.05);
  };
  const double lowDrop = band(500.0, 0.05) - band(500.0, 0.25);
  const double highDrop = band(5000.0, 0.05) - band(5000.0, 0.25);
  CHECK(highDrop > lowDrop + 3.0);
  // Decorrelated sides.
  double lr = 0.0, ll = 0.0, rr = 0.0;
  for (size_t i = 0; i < l.size(); ++i) {
    lr += (double)l[i] * r[i];
    ll += (double)l[i] * l[i];
    rr += (double)r[i] * r[i];
  }
  CHECK(std::fabs(lr / std::sqrt(ll * rr)) < 0.3);
}
