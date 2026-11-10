// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

#include <chalkwalk/dsp/OnePole.h>

#include <algorithm>
#include <cmath>

namespace chalkwalk::dsp {

// A feed-forward compressor: stereo-linked, soft knee, optional sidechain
// high-pass, RMS or peak detection, attack and release on the gain in dB.
//
// Written from lockstep's (seq_play) bus and track compressors, the same
// author's, and not moved: those are JUCE effects, and reviewing them for the
// move found two faults this one is built not to have:
//
// - THE SIDECHAIN WAS ONE CHANNEL. The bus compressor summed both sides for
//   its detector and then overwrote the sum with the left side's high-passed
//   sample, so it compressed on the left only. Here each side is high-passed
//   and the detector reads both (`linked`).
// - THE KNEE BOOSTED. Its knee scaled the overshoot by a fraction that is
//   still applied while the overshoot is negative, so just below threshold it
//   added gain (about 0.2 dB at 2:1). The knee here is the standard quadratic
//   one: its gain is never above unity, and it meets both straight parts with
//   matching slope.
//
// THE COST IS IN THE LOGS, so they are skipped when there is nothing to do:
// below the knee with the gain already home, a sample costs a compare and a
// multiply. A bus or a quiet channel is mostly there.
class Compressor {
public:
  struct Settings {
    double thresholdDb = -18.0;
    double ratio = 2.0;
    double kneeDb = 6.0;           // full width, centred on the threshold
    double attackMs = 10.0;
    double releaseMs = 200.0;
    bool autoRelease = false;      // release four times faster below the knee
    double detectorMs = 10.0;      // RMS averaging time; 0 = peak detection
    double sidechainHighPassHz = 0;// 0 = none
    double makeupDb = 0.0;
  };

  void prepare(double sampleRate, const Settings &s) {
    s_ = s;
    auto coef = [&](double ms) {
      return ms <= 0.0 ? 1.0 : 1.0 - std::exp(-1.0 / std::max(1.0, sampleRate * ms / 1000.0));
    };
    attack_ = coef(s.attackMs);
    release_ = coef(s.releaseMs);
    detector_ = s.detectorMs > 0.0 ? coef(s.detectorMs) : 1.0;
    for (auto &h : hp_)
      h.set(s.sidechainHighPassHz, sampleRate);
    const double kneeStart = s.thresholdDb - s.kneeDb / 2.0;
    // The detector's level at which the knee begins, in its own units (power
    // for RMS, magnitude for peak), so the common case needs no log.
    quietBelow_ = s.detectorMs > 0.0 ? std::pow(10.0, kneeStart / 10.0)
                                     : std::pow(10.0, kneeStart / 20.0);
    slope_ = 1.0 - 1.0 / std::max(1.0, s.ratio);
    makeup_ = std::pow(10.0, s.makeupDb / 20.0);
    reset();
  }

  // In place; `right` may be null for mono.
  void process(float *left, float *right, int numSamples) noexcept {
    const bool rms = s_.detectorMs > 0.0;
    const bool hp = s_.sidechainHighPassHz > 0.0;
    for (int i = 0; i < numSamples; ++i) {
      float l = left[i], r = right != nullptr ? right[i] : l;
      if (hp) {
        l = hp_[0].highPass(l);
        r = right != nullptr ? hp_[1].highPass(r) : l;
      }
      double level;
      if (rms) {
        env_ += detector_ * (0.5 * ((double)l * l + (double)r * r) - env_);
        if (env_ < 1e-30)
          env_ = 0.0;
        level = env_;
      } else {
        level = std::max(std::fabs((double)l), std::fabs((double)r));
      }

      double target = 0.0;
      if (level > quietBelow_)
        target = curveDb(rms ? 10.0 * std::log10(level) : 20.0 * std::log10(level));
      else if (gainDb_ > -1e-4) {
        // Below the knee and home: unity, without a log or a pow.
        gainDb_ = 0.0;
        left[i] = (float)(left[i] * makeup_);
        if (right != nullptr)
          right[i] = (float)(right[i] * makeup_);
        continue;
      }
      double rel = release_;
      if (s_.autoRelease && level <= quietBelow_)
        rel = std::min(1.0, rel * 4.0);
      gainDb_ += (target < gainDb_ ? attack_ : rel) * (target - gainDb_);
      const double g = std::pow(10.0, gainDb_ / 20.0) * makeup_;
      left[i] = (float)(left[i] * g);
      if (right != nullptr)
        right[i] = (float)(right[i] * g);
    }
  }

  // The static curve: gain in dB (never above 0) for a detector level in dB.
  double curveDb(double levelDb) const noexcept {
    const double over = levelDb - s_.thresholdDb;
    const double w = s_.kneeDb;
    if (2.0 * over <= -w)
      return 0.0;
    if (w > 0.0 && 2.0 * std::fabs(over) < w) {
      const double x = over + w / 2.0;
      return -slope_ * x * x / (2.0 * w);
    }
    return -slope_ * over;
  }

  // How far the gain is down now, in dB (0 or negative), before makeup.
  double gainReductionDb() const noexcept { return gainDb_; }

  void reset() noexcept {
    for (auto &h : hp_)
      h.reset();
    env_ = 0.0;
    gainDb_ = 0.0;
  }

private:
  Settings s_;
  double attack_ = 1.0, release_ = 1.0, detector_ = 1.0;
  double quietBelow_ = 0.0, slope_ = 0.5, makeup_ = 1.0;
  OnePole hp_[2];
  double env_ = 0.0;
  double gainDb_ = 0.0;
};

}  // namespace chalkwalk::dsp
