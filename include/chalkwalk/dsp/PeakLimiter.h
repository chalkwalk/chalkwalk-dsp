// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// A lookahead peak limiter that never lets a sample past its ceiling.
//
// For a final stage -- a mix bus feeding an encoder -- where the promise is
// "never clips" rather than a sound. Stereo-linked, so the image does not move.
//
// HOW THE GUARANTEE IS MADE, so it can be checked rather than trusted:
//
//  1. For every input sample, the gain that would bring it to the ceiling,
//     g_req = min(1, ceiling / peak across channels).
//  2. HOLD: the minimum g_req over the last L+1 samples (L = lookahead).
//  3. RELEASE: a state that drops to the held value instantly and rises back
//     toward it exponentially, so it is never above the held value.
//  4. SMOOTH: the mean of the last L+1 states.
//  5. The audio is delayed by L samples, and the smoothed gain applied.
//
// The sample leaving the delay at time n entered at n-L. Every state averaged
// at n is a hold window that contains n-L, so every one is at most
// g_req[n-L], and so is their mean: the output is at most the ceiling. The
// mean is also why the gain ramps down over the lookahead instead of
// stepping, which is what keeps it from clicking.
//
// SAMPLE PEAK, not true peak. An encoder downstream can overshoot by a
// fraction of a decibel; leave that much below 0 dBFS.
//
// Allocates in `prepare` only.

namespace chalkwalk::dsp {

class PeakLimiter {
public:
  void prepare(double sampleRate, double lookaheadMs, double releaseMs,
               double ceilingDb) {
    lookahead_ = std::max(1, (int)std::lround(sampleRate * lookaheadMs / 1000.0));
    ceiling_ = std::pow(10.0, ceilingDb / 20.0);
    release_ = 1.0 - std::exp(-1.0 / std::max(1.0, sampleRate * releaseMs / 1000.0));
    const size_t span = (size_t)lookahead_ + 1;
    delayL_.assign((size_t)lookahead_, 0.0f);
    delayR_.assign((size_t)lookahead_, 0.0f);
    states_.assign(span, 1.0);
    heldIndex_.assign(span + 1, 0);
    heldValue_.assign(span + 1, 1.0);
    head_ = tail_ = 0;
    sum_ = (double)span;
    pos_ = 0;
    statePos_ = 0;
    index_ = 0;
    state_ = 1.0;
    gain_ = 1.0;
  }

  // The delay the limiter adds, in samples.
  int latencySamples() const { return lookahead_; }

  // The gain applied to the last sample out, in dB (0 when not limiting).
  double gainDb() const { return 20.0 * std::log10(std::max(gain_, 1e-12)); }

  // In place. `right` may be null for mono.
  void process(float *left, float *right, int numSamples) {
    for (int i = 0; i < numSamples; ++i) {
      const float l = left[i];
      const float r = right != nullptr ? right[i] : 0.0f;
      const double peak = std::max(std::fabs((double)l), std::fabs((double)r));
      const double req = peak > ceiling_ ? ceiling_ / peak : 1.0;

      // HOLD: a monotonic queue (in a fixed ring, so nothing allocates) gives
      // the window minimum in O(1) amortised.
      const size_t cap = heldIndex_.size();
      while (head_ != tail_ && heldValue_[(tail_ + cap - 1) % cap] >= req)
        tail_ = (tail_ + cap - 1) % cap;
      heldIndex_[tail_] = index_;
      heldValue_[tail_] = req;
      tail_ = (tail_ + 1) % cap;
      while (heldIndex_[head_] <= index_ - (long)(lookahead_ + 1))
        head_ = (head_ + 1) % cap;
      const double hold = heldValue_[head_];

      // RELEASE: never above the held value.
      if (hold < state_)
        state_ = hold;
      else
        state_ += (hold - state_) * release_;

      // SMOOTH: running mean of the last L+1 states, re-summed now and then
      // so rounding cannot accumulate over a long session.
      sum_ += state_ - states_[(size_t)statePos_];
      states_[(size_t)statePos_] = state_;
      statePos_ = (statePos_ + 1) % (lookahead_ + 1);
      if (statePos_ == 0) {
        sum_ = 0.0;
        for (double s : states_)
          sum_ += s;
      }
      gain_ = std::min(1.0, sum_ / (double)(lookahead_ + 1));

      // DELAY, then apply.
      const float outL = delayL_[(size_t)pos_];
      const float outR = delayR_[(size_t)pos_];
      delayL_[(size_t)pos_] = l;
      delayR_[(size_t)pos_] = r;
      pos_ = (pos_ + 1) % lookahead_;

      left[i] = clampToCeiling((float)(outL * gain_));
      if (right != nullptr)
        right[i] = clampToCeiling((float)(outR * gain_));
      ++index_;
    }
  }

private:
  // The arithmetic above already guarantees this; float rounding of the
  // product can land one ulp over, and the promise is "never".
  float clampToCeiling(float x) const {
    const float c = (float)ceiling_;
    return x > c ? c : (x < -c ? -c : x);
  }

  int lookahead_ = 1;
  double ceiling_ = 1.0;
  double release_ = 0.001;
  std::vector<float> delayL_, delayR_;
  std::vector<double> states_;
  std::vector<long> heldIndex_;
  std::vector<double> heldValue_;
  size_t head_ = 0, tail_ = 0;
  double sum_ = 0.0;
  int pos_ = 0;
  int statePos_ = 0;
  long index_ = 0;
  double state_ = 1.0;
  double gain_ = 1.0;
};

}  // namespace chalkwalk::dsp
