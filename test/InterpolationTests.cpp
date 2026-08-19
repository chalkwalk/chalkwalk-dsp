// SPDX-License-Identifier: MIT
#include "LegacyCheck.h"

#include <chalkwalk/dsp/Interpolation.h>

#include <cmath>
#include <string>

using namespace chalkwalk::dsp;

TEST_CASE("it passes through the samples it interpolates between") {
  // t=0 must be y0 and t=1 must be y1 exactly, or a read at an integer
  // position does not return the sample that is there.
  const float a = -0.3f, b = 0.5f, c = 0.9f, d = -0.1f;
  CHECK_MSG(std::abs(hermite4(a, b, c, d, 0.0f) - b) < 1e-6f, "t=0 is not y0");
  CHECK_MSG(std::abs(hermite4(a, b, c, d, 1.0f) - c) < 1e-6f, "t=1 is not y1");
}

TEST_CASE("a constant signal interpolates to that constant") {
  for (float v : {-1.0f, 0.0f, 0.25f, 1.0f})
    for (float t = 0.0f; t <= 1.0f; t += 0.05f)
      CHECK_MSG(std::abs(hermite4(v, v, v, v, t) - v) < 1e-6f,
                "a constant wobbled at t=" + std::to_string(t));
}

TEST_CASE("a straight line interpolates to that line") {
  // Hermite reproduces polynomials up to its order, so a ramp must come back
  // as the ramp: any error here is a nonlinearity in every varispeed read.
  for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
    const float got = hermite4(-1.0f, 0.0f, 1.0f, 2.0f, t);
    CHECK_MSG(std::abs(got - t) < 1e-5f,
              "a ramp bent at t=" + std::to_string(t) + ": " + std::to_string(got));
  }
}

TEST_CASE("it beats linear interpolation on a sine") {
  // The reason it exists. Linear interpolation is a lowpass whose corner moves
  // with the fraction, so a varispeed read using it goes dull at some speeds
  // and not others.
  double hermiteErr = 0.0, linearErr = 0.0;
  const double freq = 0.11;   // cycles per sample
  for (int i = 4; i < 2000; ++i)
    for (double t = 0.05; t < 1.0; t += 0.1) {
      auto s = [&](int k) { return (float)std::sin(2.0 * 3.14159265358979 * freq * k); };
      const double truth = std::sin(2.0 * 3.14159265358979 * freq * (i + t));
      hermiteErr += std::abs(hermite4(s(i - 1), s(i), s(i + 1), s(i + 2), (float)t) - truth);
      linearErr += std::abs((1.0 - t) * s(i) + t * s(i + 1) - truth);
    }
  CHECK_MSG(hermiteErr < linearErr * 0.25,
            "hermite should be several times better than linear: " +
                std::to_string(hermiteErr) + " against " + std::to_string(linearErr));
}

TEST_CASE("it stays bounded on bounded input") {
  // Hermite can overshoot -- it is not shape-preserving -- but it must not run
  // away, or a resampled transient clips where the original did not.
  for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
    const float got = hermite4(-1.0f, 1.0f, -1.0f, 1.0f, t);
    CHECK_MSG(got > -2.0f && got < 2.0f,
              "overshoot beyond reason at t=" + std::to_string(t) + ": " +
                  std::to_string(got));
  }
}
