// SPDX-License-Identifier: MIT
#include <chalkwalk/dsp/Loudness.h>
#include <chalkwalk/dsp/PeakLimiter.h>
#include <chalkwalk/dsp/Pi.h>
#include <chalkwalk/dsp/SlidingLoudness.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <vector>

using chalkwalk::dsp::PeakLimiter;
using chalkwalk::dsp::measure::integratedLufs;
using chalkwalk::dsp::measure::kSilenceLufs;
using chalkwalk::dsp::measure::SlidingLoudness;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> tone(double seconds, double amplitude, double hz = 997.0) {
  std::vector<float> v((size_t)(seconds * kRate));
  for (size_t i = 0; i < v.size(); ++i)
    v[i] = (float)(amplitude * std::sin(2.0 * chalkwalk::dsp::kPi * hz * (double)i / kRate));
  return v;
}

void feed(SlidingLoudness &m, const std::vector<float> &v) {
  // In uneven pieces, so the hop bookkeeping is exercised.
  size_t i = 0, step = 997;
  while (i < v.size()) {
    size_t n = std::min(step, v.size() - i);
    m.add(v.data() + i, v.data() + i, (int)n);
    i += n;
    step = step == 997 ? 4801 : 997;
  }
}

}  // namespace

TEST_CASE("over a window longer than the signal, it is integratedLufs",
          "[sliding-loudness]") {
  auto a = tone(5.0, 0.5);
  auto b = tone(5.0, 0.05, 440.0);
  std::vector<float> both(a);
  both.insert(both.end(), b.begin(), b.end());

  SlidingLoudness m(2, kRate, 60.0);
  feed(m, both);
  const double reference = integratedLufs(both.data(), both.data(), (int)both.size(), kRate);
  CHECK(m.lufs() == Catch::Approx(reference).margin(0.1));
}

TEST_CASE("it forgets what has left the window", "[sliding-loudness]") {
  SlidingLoudness m(2, kRate, 10.0);
  feed(m, tone(20.0, 0.5));
  const double loud = m.lufs();
  feed(m, tone(12.0, 0.05));
  const double quiet = m.lufs();
  CHECK(loud - quiet == Catch::Approx(20.0).margin(0.2));
}

TEST_CASE("silence is gated out, and says how much playing it rests on",
          "[sliding-loudness]") {
  SlidingLoudness m(2, kRate, 60.0);
  feed(m, std::vector<float>((size_t)(5 * kRate), 0.0f));
  CHECK(m.lufs() == kSilenceLufs);
  CHECK(m.gatedSeconds() == 0.0);

  auto t = tone(5.0, 0.5);
  feed(m, t);
  const double playing = m.lufs();
  CHECK(m.gatedSeconds() == Catch::Approx(5.0).margin(0.5));

  feed(m, std::vector<float>((size_t)(20 * kRate), 0.0f));
  // A rest does not drag the reading down: the silent blocks do not count.
  CHECK(m.lufs() == Catch::Approx(playing).margin(0.3));
}

TEST_CASE("a mono meter reads a mono signal", "[sliding-loudness]") {
  auto t = tone(5.0, 0.5);
  SlidingLoudness m(1, kRate, 60.0);
  m.add(t.data(), nullptr, (int)t.size());
  CHECK(m.lufs() == Catch::Approx(integratedLufs(t.data(), (int)t.size(), kRate)).margin(0.1));
}

TEST_CASE("the limiter never lets a sample past its ceiling", "[peak-limiter]") {
  std::mt19937 rng(7);
  std::normal_distribution<float> noise(0.0f, 0.4f);
  std::uniform_real_distribution<float> spike(-4.0f, 4.0f);
  std::vector<float> l((size_t)kRate * 10), r(l.size());
  for (size_t i = 0; i < l.size(); ++i) {
    l[i] = noise(rng);
    r[i] = noise(rng);
    if (i % 1009 == 0)
      l[i] = spike(rng);  // isolated peaks, the hardest case for a limiter
    if (i % 2003 == 0)
      r[i] = spike(rng);
  }
  PeakLimiter lim;
  lim.prepare(kRate, 5.0, 50.0, -1.0);
  const float ceiling = (float)std::pow(10.0, -1.0 / 20.0);
  float worst = 0.0f;
  for (size_t i = 0; i < l.size(); i += 480) {
    lim.process(l.data() + i, r.data() + i, 480);
  }
  for (size_t i = 0; i < l.size(); ++i)
    worst = std::max(worst, std::max(std::fabs(l[i]), std::fabs(r[i])));
  CHECK(worst <= ceiling);
  CHECK(worst > ceiling * 0.9f);  // and it does not pull far below it
}

TEST_CASE("below the ceiling the limiter is a pure delay", "[peak-limiter]") {
  auto t = tone(1.0, 0.5);
  std::vector<float> l(t), r(t);
  PeakLimiter lim;
  lim.prepare(kRate, 5.0, 50.0, -1.0);
  lim.process(l.data(), r.data(), (int)l.size());
  const int d = lim.latencySamples();
  CHECK(d == 240);
  for (size_t i = (size_t)d; i < l.size(); ++i)
    REQUIRE(l[i] == t[i - (size_t)d]);
  CHECK(lim.gainDb() == 0.0);
}

TEST_CASE("the limiter lets go after the peak", "[peak-limiter]") {
  PeakLimiter lim;
  lim.prepare(kRate, 5.0, 50.0, -1.0);
  std::vector<float> l((size_t)kRate, 0.1f), r(l);
  l[100] = 2.0f;
  lim.process(l.data(), r.data(), 2000);
  CHECK(lim.gainDb() < -1.0);
  lim.process(l.data() + 2000, r.data() + 2000, (int)l.size() - 2000);
  CHECK(lim.gainDb() > -0.01);
}

TEST_CASE("A shorter window read from a long meter is that window exactly", "[sliding]") {
  // One meter, two readings: the long view and the last 30 s. The 30 s
  // reading must equal a 30 s meter fed the same signal, block for block.
  SlidingLoudness longMeter(1, kRate, 120.0), shortMeter(1, kRate, 30.0);
  auto loud = tone(50.0, 0.3), quiet = tone(40.0, 0.03), silent = tone(10.0, 0.0);
  for (auto *m : {&longMeter, &shortMeter}) {
    feed(*m, loud);
    feed(*m, quiet);
  }
  CHECK(longMeter.lufsOver(30.0) == Catch::Approx(shortMeter.lufs()).margin(1e-9));
  // The last 30 s are all quiet; the long view still hears the loud part.
  CHECK(longMeter.lufs() > longMeter.lufsOver(30.0) + 15.0);
  CHECK(longMeter.gatedSecondsOver(30.0) == Catch::Approx(shortMeter.gatedSeconds()).margin(1e-9));
  for (auto *m : {&longMeter, &shortMeter})
    feed(*m, silent);
  CHECK(longMeter.lufsOver(30.0) == Catch::Approx(shortMeter.lufs()).margin(1e-9));
  CHECK(longMeter.gatedSecondsOver(5.0) == 0.0);  // a resting player: nothing to go on
  // Asking for more than the window is the window.
  CHECK(longMeter.lufsOver(1000.0) == longMeter.lufs());
}
