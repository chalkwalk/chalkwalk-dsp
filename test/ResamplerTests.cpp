// SPDX-License-Identifier: MIT
#include <chalkwalk/dsp/Pi.h>
#include <chalkwalk/dsp/Resampler.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using chalkwalk::dsp::kPi;
using chalkwalk::dsp::Resampler;

namespace {

std::vector<float> sine(double rate, double hz, double seconds, double amp = 0.5) {
  std::vector<float> v((size_t)(rate * seconds));
  for (size_t i = 0; i < v.size(); ++i)
    v[i] = (float)(amp * std::sin(2.0 * kPi * hz * (double)i / rate));
  return v;
}

double rms(const std::vector<float> &v, size_t from, size_t to) {
  double s = 0.0;
  for (size_t i = from; i < to; ++i)
    s += (double)v[i] * v[i];
  return std::sqrt(s / (double)(to - from));
}

std::vector<float> convert(const std::vector<float> &in, double from, double to,
                           size_t chunk) {
  Resampler r(from, to);
  std::vector<float> out;
  for (size_t i = 0; i < in.size(); i += chunk)
    r.process(in.data() + i, (int)std::min(chunk, in.size() - i), out);
  return out;
}

}  // namespace

TEST_CASE("a tone keeps its level through every common ratio", "[resampler]") {
  for (double from : {44100.0, 48000.0, 88200.0, 96000.0, 22050.0}) {
    auto in = sine(from, 1000.0, 1.0);
    auto out = convert(in, from, 48000.0, 4096);
    REQUIRE(out.size() > 40000);
    // Away from the start-up edge, a 0.5-amplitude sine has rms 0.5/sqrt 2.
    const double level = rms(out, 4800, out.size() - 4800);
    CHECK(20.0 * std::log10(level / (0.5 / std::sqrt(2.0))) ==
          Catch::Approx(0.0).margin(0.01));
  }
}

TEST_CASE("the output is where the input was: the first sample is not delayed",
          "[resampler]") {
  auto in = sine(44100.0, 1000.0, 0.5);
  auto out = convert(in, 44100.0, 48000.0, 512);
  // Compare against the ideal 48 kHz sine; phase-aligned output has small error.
  auto ideal = sine(48000.0, 1000.0, 0.5);
  double err = 0.0;
  for (size_t i = 2000; i < 20000; ++i)
    err = std::max(err, (double)std::fabs(out[i] - ideal[i]));
  CHECK(err < 1e-3);
}

TEST_CASE("downsampling removes what would alias", "[resampler]") {
  // 30 kHz is fine at 88.2 kHz and impossible at 48 kHz: it must vanish, not
  // fold back to 18 kHz.
  auto in = sine(88200.0, 30000.0, 1.0);
  auto out = convert(in, 88200.0, 48000.0, 4096);
  const double level = rms(out, 4800, out.size() - 4800);
  CHECK(20.0 * std::log10(level / (0.5 / std::sqrt(2.0))) < -80.0);
}

TEST_CASE("chunking does not change the result", "[resampler]") {
  auto in = sine(88200.0, 1234.0, 0.5);
  auto a = convert(in, 88200.0, 48000.0, 64);
  auto b = convert(in, 88200.0, 48000.0, 100000);
  REQUIRE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i)
    REQUIRE(a[i] == b[i]);
}

TEST_CASE("the output length follows the ratio", "[resampler]") {
  std::vector<float> in(88200, 0.0f);
  auto out = convert(in, 88200.0, 48000.0, 1000);
  const Resampler r(88200.0, 48000.0);
  const double expected = 48000.0 - r.latencyInputSamples() * 48000.0 / 88200.0;
  CHECK(std::fabs((double)out.size() - expected) <= 2.0);
}
