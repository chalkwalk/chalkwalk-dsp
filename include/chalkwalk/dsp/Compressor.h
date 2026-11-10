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
    configure(sampleRate, s);
    reset();
  }

  // New settings, keeping the state: for a host whose parameters move while it
  // plays. A few exp()s and pow()s; call it when something changed, not every
  // block regardless.
  void configure(double sampleRate, const Settings &s) {
    s_ = s;
    auto coef = [&](double ms) {
      return (float)(ms <= 0.0 ? 1.0 : 1.0 - std::exp(-1.0 / std::max(1.0, sampleRate * ms / 1000.0)));
    };
    attack_ = coef(s.attackMs);
    release_ = coef(s.releaseMs);
    detector_ = s.detectorMs > 0.0 ? coef(s.detectorMs) : 1.0f;
    for (auto &h : hp_)
      h.set(s.sidechainHighPassHz, sampleRate);
    const double kneeStart = s.thresholdDb - s.kneeDb / 2.0;
    // The detector's level at which the knee begins, in its own units (power
    // for RMS, magnitude for peak), so the common case needs no log.
    quietBelow_ = (float)(s.detectorMs > 0.0 ? std::pow(10.0, kneeStart / 10.0)
                                             : std::pow(10.0, kneeStart / 20.0));
    slope_ = (float)(1.0 - 1.0 / std::max(1.0, s.ratio));
    makeup_ = (float)std::pow(10.0, s.makeupDb / 20.0);
    threshold_ = (float)s.thresholdDb;
    knee_ = (float)std::max(0.0, s.kneeDb);
  }

  // In place; `right` may be null for mono.
  //
  // In float, and in base 2: log2 and exp2 of a float are several times
  // cheaper than log10 and pow of a double, and a gain computer needs nothing
  // like double's precision. Measured against lockstep's float compressors
  // it was 1.6 times their cost in double; this is what closed it.
  void process(float *left, float *right, int numSamples) noexcept {
    const bool rms = s_.detectorMs > 0.0;
    const bool hp = s_.sidechainHighPassHz > 0.0;
    // dB = k * log2(level): 10 log10 for power, 20 log10 for magnitude.
    const float toDb = rms ? 3.0103000f : 6.0206000f;
    constexpr float kDbToLog2 = 0.16609640f;  // log2(10) / 20
    for (int i = 0; i < numSamples; ++i) {
      float l = left[i], r = right != nullptr ? right[i] : l;
      if (hp) {
        l = hp_[0].highPass(l);
        r = right != nullptr ? hp_[1].highPass(r) : l;
      }
      float level;
      if (rms) {
        env_ += detector_ * (0.5f * (l * l + r * r) - env_);
        if (env_ < 1e-30f)
          env_ = 0.0f;
        level = env_;
      } else {
        level = std::max(std::fabs(l), std::fabs(r));
      }

      float target = 0.0f;
      if (level > quietBelow_)
        target = curve(toDb * std::log2(level));
      else if (gainDb_ > -1e-4f) {
        // Below the knee and home: unity, without a log or an exp.
        gainDb_ = 0.0f;
        left[i] *= makeup_;
        if (right != nullptr)
          right[i] *= makeup_;
        continue;
      }
      float rel = release_;
      if (s_.autoRelease && level <= quietBelow_)
        rel = std::min(1.0f, rel * 4.0f);
      gainDb_ += (target < gainDb_ ? attack_ : rel) * (target - gainDb_);
      const float g = std::exp2(gainDb_ * kDbToLog2) * makeup_;
      left[i] *= g;
      if (right != nullptr)
        right[i] *= g;
    }
  }

  // The static curve: gain in dB (never above 0) for a detector level in dB.
  double curveDb(double levelDb) const noexcept { return curve((float)levelDb); }

  // How far the gain is down now, in dB (0 or negative), before makeup.
  double gainReductionDb() const noexcept { return gainDb_; }

  void reset() noexcept {
    for (auto &h : hp_)
      h.reset();
    env_ = 0.0f;
    gainDb_ = 0.0f;
  }

private:
  float curve(float levelDb) const noexcept {
    const float over = levelDb - threshold_;
    const float w = knee_;
    if (2.0f * over <= -w)
      return 0.0f;
    if (w > 0.0f && 2.0f * std::fabs(over) < w) {
      const float x = over + w / 2.0f;
      return -slope_ * x * x / (2.0f * w);
    }
    return -slope_ * over;
  }

  Settings s_;
  float attack_ = 1.0f, release_ = 1.0f, detector_ = 1.0f;
  float quietBelow_ = 0.0f, slope_ = 0.5f, makeup_ = 1.0f;
  float threshold_ = -18.0f, knee_ = 6.0f;
  OnePole hp_[2];
  float env_ = 0.0f;
  float gainDb_ = 0.0f;
};

}  // namespace chalkwalk::dsp
