// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

#include <chalkwalk/dsp/Denormal.h>
#include <chalkwalk/dsp/OnePole.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace chalkwalk::dsp {

// A small, dark room: a feedback delay network for putting separate sources in
// one shared space, not for an effect anyone should notice as reverb.
//
// Eight delay lines of 17 to 42 ms, mixed through an 8x8 Hadamard matrix (an
// orthogonal mix, so the loop loses energy only where it is told to). Each
// line's feedback gain is set from its length so every line decays at the same
// rate, `decaySeconds` to -60 dB; a one-pole low-pass in each line makes the
// highs die sooner, which is what "dark" means here. The input is band-limited
// before it enters (high-pass and low-pass), so bass does not muddy the room
// and the room adds no fizz. Left feeds the even lines and right the odd, and
// each side reads back its own lines, so the two outputs are decorrelated.
//
// Wet only: the caller mixes it in, at whatever level makes it "quiet".
// Allocates in `prepare`, never in `process`.
class SmallRoom {
public:
  struct Settings {
    double decaySeconds = 0.5;   // RT60 at low frequencies
    double dampingHz = 2500.0;   // the in-loop low-pass
    double inputHighPassHz = 250.0;
    double inputLowPassHz = 4000.0;
    double preDelayMs = 8.0;
  };

  void prepare(double sampleRate, const Settings &s) {
    static constexpr double kLengthsMs[kLines] = {17.3, 19.7, 23.1, 26.3, 29.9, 33.7, 37.3, 41.9};
    for (int i = 0; i < kLines; ++i) {
      const int n = nextPrime((int)std::lround(kLengthsMs[i] * sampleRate / 1000.0));
      lines_[(size_t)i].assign((size_t)n, 0.0f);
      pos_[(size_t)i] = 0;
      // -60 dB over decaySeconds: each pass through a line of n samples
      // loses 60 * n / (sr * T) dB.
      gain_[(size_t)i] = (float)std::pow(10.0, -3.0 * n / (sampleRate * std::max(0.05, s.decaySeconds)));
      damp_[(size_t)i].set(s.dampingHz, sampleRate);
    }
    for (int c = 0; c < 2; ++c) {
      inHp_[c].set(s.inputHighPassHz, sampleRate);
      inLp_[c].set(s.inputLowPassHz, sampleRate);
      pre_[c].assign((size_t)std::max(1L, std::lround(s.preDelayMs * sampleRate / 1000.0)), 0.0f);
    }
    prePos_ = 0;
  }

  // Reads `inL`/`inR` and writes the room alone to `outL`/`outR` (which may be
  // other buffers, or the same ones).
  void process(const float *inL, const float *inR, float *outL, float *outR,
               int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i) {
      float in[2] = {inL[i], inR[i]};
      for (int c = 0; c < 2; ++c) {
        const float shaped = inLp_[c].lowPass(inHp_[c].highPass(in[c]));
        float &slot = pre_[c][(size_t)prePos_];
        in[c] = slot;
        slot = shaped;
      }
      prePos_ = (prePos_ + 1) % (int)pre_[0].size();

      std::array<float, kLines> x;
      for (int k = 0; k < kLines; ++k)
        x[(size_t)k] = lines_[(size_t)k][(size_t)pos_[(size_t)k]];

      outL[i] = 0.5f * (x[0] - x[2] + x[4] - x[6]);
      outR[i] = 0.5f * (x[1] - x[3] + x[5] - x[7]);

      hadamard(x);
      for (int k = 0; k < kLines; ++k) {
        float v = damp_[(size_t)k].lowPass(x[(size_t)k] * gain_[(size_t)k]);
        v += in[k & 1] * 0.5f;
        auto &line = lines_[(size_t)k];
        line[(size_t)pos_[(size_t)k]] = flush(v);
        if (++pos_[(size_t)k] == (int)line.size())
          pos_[(size_t)k] = 0;
      }
    }
  }

  void reset() {
    for (auto &l : lines_)
      std::fill(l.begin(), l.end(), 0.0f);
    for (auto &p : pre_)
      std::fill(p.begin(), p.end(), 0.0f);
    for (auto &d : damp_)
      d.reset();
    for (int c = 0; c < 2; ++c) {
      inHp_[c].reset();
      inLp_[c].reset();
    }
  }

private:
  static constexpr int kLines = 8;

  // In place, normalised: orthogonal, so it keeps energy.
  static void hadamard(std::array<float, kLines> &x) noexcept {
    for (int h = 1; h < kLines; h *= 2)
      for (int i = 0; i < kLines; i += 2 * h)
        for (int j = i; j < i + h; ++j) {
          const float a = x[(size_t)j], b = x[(size_t)(j + h)];
          x[(size_t)j] = a + b;
          x[(size_t)(j + h)] = a - b;
        }
    const float norm = 0.35355339f;  // 1 / sqrt(8)
    for (auto &v : x)
      v *= norm;
  }

  static int nextPrime(int n) {
    auto prime = [](int v) {
      if (v < 2)
        return false;
      for (int d = 2; d * d <= v; ++d)
        if (v % d == 0)
          return false;
      return true;
    };
    while (!prime(n))
      ++n;
    return n;
  }

  std::array<std::vector<float>, kLines> lines_;
  std::array<int, kLines> pos_{};
  std::array<float, kLines> gain_{};
  std::array<OnePole, kLines> damp_{};
  OnePole inHp_[2], inLp_[2];
  std::vector<float> pre_[2];
  int prePos_ = 0;
};

}  // namespace chalkwalk::dsp
