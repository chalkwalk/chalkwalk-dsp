// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

#include <chalkwalk/dsp/OnePole.h>

#include <cmath>

namespace chalkwalk::dsp {

// A tilt EQ: the lows go one way and the highs the other, about a pivot.
//
// The signal is split by a one-pole at the pivot into its low part and the
// rest, and the two are scaled and summed. Because the split is complementary
// (`OnePole`), a tilt of 0 dB is the input exactly, and any tilt is a pair of
// first-order shelves meeting at the pivot: gentle by construction, which is
// what a tilt is for. `tiltDb` is the difference between the far highs and the
// far lows; each side moves by half of it, so the pivot itself barely moves and
// the overall level stays close.
//
// The idea is lockstep's (seq_play) tilt effect, written again here without
// JUCE, with the gains smoothed in place of per-sample dB conversions, and with
// the sign stated once: positive brightens.
//
// Stereo, one split per side. Changes to the tilt glide over `glideMs`, so a
// slowly moving automatic tilt never steps.
class TiltEq {
public:
  void prepare(double sampleRate, double pivotHz = 700.0, double glideMs = 20.0) {
    for (auto &s : split_)
      s.set(pivotHz, sampleRate);
    glide_ = (float)(1.0 - std::exp(-1.0 / std::max(1.0, sampleRate * glideMs / 1000.0)));
    reset();
  }

  void setTiltDb(double tiltDb) {
    tiltDb_ = tiltDb;
    lowTarget_ = (float)std::pow(10.0, -tiltDb / 40.0);
    highTarget_ = (float)std::pow(10.0, tiltDb / 40.0);
  }
  double tiltDb() const { return tiltDb_; }

  // In place; `right` may be null for mono.
  void process(float *left, float *right, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i) {
      low_ += glide_ * (lowTarget_ - low_);
      high_ += glide_ * (highTarget_ - high_);
      const float lo = split_[0].lowPass(left[i]);
      left[i] = low_ * lo + high_ * (left[i] - lo);
      if (right != nullptr) {
        const float lr = split_[1].lowPass(right[i]);
        right[i] = low_ * lr + high_ * (right[i] - lr);
      }
    }
  }

  void reset() noexcept {
    for (auto &s : split_)
      s.reset();
    low_ = lowTarget_;
    high_ = highTarget_;
  }

private:
  OnePole split_[2];
  float glide_ = 1.0f;
  double tiltDb_ = 0.0;
  float lowTarget_ = 1.0f, highTarget_ = 1.0f;
  float low_ = 1.0f, high_ = 1.0f;
};

}  // namespace chalkwalk::dsp
