// SPDX-License-Identifier: MIT
#include "LegacyCheck.h"

#include <chalkwalk/dsp/SoftClip.h>

#include <cmath>
#include <string>

using namespace chalkwalk::dsp;

TEST_CASE("below the knee it is EXACTLY the identity") {
  // Not "close to". A mix staged below the knee must come out bit-identical,
  // which is the entire difference between this and a tanh.
  for (float x = -kMasterKnee; x <= kMasterKnee; x += 0.001f)
    CHECK_MSG(softClip(x) == x,
              "altered a signal below the knee at " + std::to_string(x));
}

TEST_CASE("it never exceeds the ceiling, however hard it is hit") {
  for (float x : {1.0f, 2.0f, 10.0f, 1000.0f, 1.0e9f}) {
    CHECK_MSG(softClip(x) <= kMasterCeiling, "overshot at +" + std::to_string(x));
    CHECK_MSG(softClip(-x) >= -kMasterCeiling, "overshot at -" + std::to_string(x));
  }
}

TEST_CASE("it is odd, so it adds no DC") {
  // An asymmetric clipper puts a DC offset on the output, which eats headroom
  // in exactly the situation where there is none to spare.
  for (float x = 0.0f; x < 4.0f; x += 0.01f)
    CHECK_MSG(softClip(x) == -softClip(-x), "asymmetry at " + std::to_string(x));
}

TEST_CASE("it is monotonic, so it does not fold") {
  float previous = softClip(-4.0f);
  for (float x = -4.0f; x <= 4.0f; x += 0.005f) {
    const float y = softClip(x);
    CHECK_MSG(y >= previous - 1e-6f, "folded back at " + std::to_string(x));
    previous = y;
  }
}

TEST_CASE("the curve is continuous at the knee") {
  // C1 by construction: slope exactly 1 at the knee. A break here is audible
  // as a rasp on peaks that just cross it.
  const float e = 1.0e-4f;
  const float below = softClip(kMasterKnee - e);
  const float above = softClip(kMasterKnee + e);
  CHECK_MSG(std::abs(above - below) < 1.0e-3f, "a step at the knee");

  const float slope = (above - below) / (2.0f * e);
  CHECK_MSG(slope > 0.9f && slope < 1.1f,
            "slope at the knee should be 1, got " + std::to_string(slope));
}

TEST_CASE("the knee and ceiling are arguments, and they work") {
  // The reason they are arguments at all: two projects baked in different
  // constants and both called it bare, so "the default" silently differed.
  CHECK_MSG(softClip(10.0f, 0.70f, 0.95f) <= 0.95f, "a custom ceiling should hold");
  CHECK_MSG(softClip(0.60f, 0.70f, 0.95f) == 0.60f, "a custom knee should be transparent");
  CHECK_MSG(softClip(0.80f, 0.70f, 0.95f) != 0.80f, "and should engage above itself");
}

TEST_CASE("a degenerate range is passed through rather than dividing by zero") {
  CHECK_MSG(softClip(0.5f, 0.9f, 0.9f) == 0.5f, "equal knee and ceiling");
  CHECK_MSG(softClip(0.5f, 0.9f, 0.1f) == 0.5f, "ceiling below the knee");
}

TEST_CASE("zero stays zero") {
  CHECK_MSG(softClip(0.0f) == 0.0f, "silence must stay silent");
}
