// SPDX-License-Identifier: MIT
#pragma once

#include <chalkwalk/dsp/Loudness.h>

#include <ebur128.h>

#include <cmath>
#include <vector>

// Integrated loudness over a MOVING window: BS.1770's gated measurement, kept
// current.
//
// `integratedLufs` answers "how loud was this buffer". A mix that rides gain
// across a two-hour session needs "how loud has this been lately": an
// integrated measurement accumulated from the start stops responding late in
// the evening, because an hour of history outvotes the last minute. This is
// the same measurement over the last `windowSeconds` instead.
//
// HOW, and why it is exact rather than an approximation. BS.1770's integrated
// loudness is the gated mean of 400 ms blocks overlapping by 75 %, i.e. one
// block every 100 ms. libebur128's MOMENTARY loudness is the loudness of the
// last 400 ms. So reading the momentary value every 100 ms, on the hop, yields
// exactly the standard's blocks; keeping the last window of them and applying
// the standard's two gates (absolute -70 LUFS, then relative -10 LU below the
// absolutely-gated mean) is the integrated measurement over that window. The
// K-weighting stays libebur128's, on this library's rule.
//
// Blocks below the absolute gate (silence, a player who is not playing) do not
// count, so a channel that rests for a minute keeps reading what it was when it
// played, not -70. `gatedSeconds` says how much playing the reading rests on.
//
// Allocates in the constructor only. Not for an audio thread all the same:
// libebur128 does its own bookkeeping, and nothing here is promised lock-free.

namespace chalkwalk::dsp::measure {

class SlidingLoudness {
public:
  // `channels` is 1 or 2.
  SlidingLoudness(int channels, double sampleRate, double windowSeconds)
      : channels_(channels < 2 ? 1 : 2),
        hop_(std::max(1, (int)std::lround(sampleRate * 0.1))),
        blocksPerWindow_(std::max(1, (int)std::lround(windowSeconds * 10.0))),
        blocks_((size_t)blocksPerWindow_, kSilenceLufs),
        energies_((size_t)blocksPerWindow_, 0.0),
        interleaved_((size_t)hop_ * (size_t)channels_) {
    st_ = ebur128_init((unsigned)channels_, (unsigned long)std::lround(sampleRate),
                       EBUR128_MODE_M);
    if (st_ != nullptr && channels_ == 1)
      ebur128_set_channel(st_, 0, EBUR128_CENTER);
  }

  ~SlidingLoudness() {
    if (st_ != nullptr)
      ebur128_destroy(&st_);
  }

  SlidingLoudness(const SlidingLoudness &) = delete;
  SlidingLoudness &operator=(const SlidingLoudness &) = delete;

  // Planar samples; `right` is ignored for a mono meter and may be null.
  void add(const float *left, const float *right, int numSamples) {
    if (st_ == nullptr)
      return;
    int i = 0;
    while (i < numSamples) {
      int n = std::min(numSamples - i, hop_ - filled_);
      for (int k = 0; k < n; ++k) {
        if (channels_ == 1) {
          interleaved_[(size_t)(filled_ + k)] = left[i + k];
        } else {
          interleaved_[(size_t)(filled_ + k) * 2] = left[i + k];
          interleaved_[(size_t)(filled_ + k) * 2 + 1] =
              right != nullptr ? right[i + k] : left[i + k];
        }
      }
      ebur128_add_frames_float(st_, interleaved_.data() + (size_t)filled_ * (size_t)channels_,
                               (size_t)n);
      filled_ += n;
      i += n;
      if (filled_ == hop_) {
        filled_ = 0;
        onHop();
      }
    }
  }

  // Gated loudness of the window in LUFS, or `kSilenceLufs` when no block in
  // it is above the absolute gate.
  double lufs() const { return lufsOfBlocks(blocksPerWindow_); }

  // The same measurement over only the most recent `seconds` of the window
  // (clamped to it). ONE METER, SEVERAL WINDOWS: a gain rider that wants a
  // long view and a short one reads both from the same blocks, instead of
  // running the K-weighting twice. It is exact for the same reason the whole
  // window is: the last N blocks are the standard's blocks for those N.
  double lufsOver(double seconds) const { return lufsOfBlocks(blocksIn(seconds)); }

  // How much of the window is above the absolute gate, in seconds of blocks.
  double gatedSeconds() const { return gatedSecondsOfBlocks(blocksPerWindow_); }
  double gatedSecondsOver(double seconds) const { return gatedSecondsOfBlocks(blocksIn(seconds)); }

  // The last 400 ms, as libebur128 measures it: the momentary loudness.
  double momentaryLufs() const { return last_; }

private:
  static constexpr double kAbsoluteGate = -70.0;
  static double energy(double lufs) { return std::pow(10.0, (lufs + 0.691) / 10.0); }
  static double loudness(double e) { return -0.691 + 10.0 * std::log10(e); }

  int blocksIn(double seconds) const {
    const int n = (int)std::lround(seconds * 10.0);
    return n < 1 ? 1 : (n > blocksPerWindow_ ? blocksPerWindow_ : n);
  }

  // The newest `count` blocks, newest first: f(lufs, energy).
  template <class F> void forRecent(int count, F f) const {
    int at = next_;
    for (int i = 0; i < count; ++i) {
      at = at == 0 ? blocksPerWindow_ - 1 : at - 1;
      f(blocks_[(size_t)at], energies_[(size_t)at]);
    }
  }

  // Energies are kept beside the blocks, so a reading costs additions, not a
  // pow() per block: a five-minute window is 3000 blocks, read every few tens
  // of milliseconds per channel.
  double lufsOfBlocks(int count) const {
    double sum = 0.0;
    int n = 0;
    forRecent(count, [&](double l, double e) {
      if (l > kAbsoluteGate) {
        sum += e;
        ++n;
      }
    });
    if (n == 0)
      return kSilenceLufs;
    const double relative = loudness(sum / n) - 10.0;
    double gatedSum = 0.0;
    int gated = 0;
    forRecent(count, [&](double l, double e) {
      if (l > kAbsoluteGate && l > relative) {
        gatedSum += e;
        ++gated;
      }
    });
    return gated == 0 ? kSilenceLufs : loudness(gatedSum / gated);
  }

  double gatedSecondsOfBlocks(int count) const {
    int n = 0;
    forRecent(count, [&](double l, double) {
      if (l > kAbsoluteGate)
        ++n;
    });
    return n * 0.1;
  }

  void onHop() {
    ++hops_;
    if (hops_ < 4)
      return;  // the first block needs 400 ms of signal behind it
    double m = kSilenceLufs;
    if (ebur128_loudness_momentary(st_, &m) != EBUR128_SUCCESS || !std::isfinite(m))
      m = kSilenceLufs;
    last_ = m;
    blocks_[(size_t)next_] = m;
    energies_[(size_t)next_] = m > kAbsoluteGate ? energy(m) : 0.0;
    next_ = (next_ + 1) % blocksPerWindow_;
  }

  int channels_;
  int hop_;
  int blocksPerWindow_;
  std::vector<double> blocks_;
  std::vector<double> energies_;
  std::vector<float> interleaved_;
  ebur128_state *st_ = nullptr;
  int filled_ = 0;
  long hops_ = 0;
  int next_ = 0;
  double last_ = kSilenceLufs;
};

}  // namespace chalkwalk::dsp::measure
