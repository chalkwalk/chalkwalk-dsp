// SPDX-License-Identifier: MIT
#pragma once

#include <chalkwalk/dsp/Bessel.h>
#include <chalkwalk/dsp/FirDesign.h>
#include <chalkwalk/dsp/Pi.h>

#include <algorithm>
#include <cmath>
#include <vector>

// Sample-rate conversion at any ratio, streaming: a windowed-sinc resampler.
//
// For audio that arrives at somebody else's rate -- a remote player's interval
// at 44.1, 48, 88.2 or 96 kHz -- and has to be mixed at ours. Stateful across
// calls, so a stream cut into intervals resamples as one stream: no seam at the
// cut, which per-chunk conversion would put there.
//
// WHY NOT HERMITE OR LAGRANGE. A four-point interpolator is fine for reading a
// buffer at a fractional position when the rate barely changes. DOWNsampling
// is different: 88.2 kHz to 48 kHz must remove everything between 24 and
// 44.1 kHz first, or it folds back into the audible band as inharmonic tones.
// A polynomial interpolator does not filter; this does. Cutoff at 97 % of the
// lower Nyquist, Kaiser-windowed, stopband from Kaiser's relation.
//
// HOW. Each output sample is the input convolved with a lowpass sinc centred
// on its fractional input position. The kernel is tabulated once, at 512
// points per input sample, and read with linear interpolation, which keeps
// the table error far under the stopband. Downsampling widens the kernel by
// the ratio (it is a lowpass at the OUTPUT's Nyquist, in input samples).
//
// Mono; use one per channel. Allocates in the constructor and when the caller's
// output vector grows -- not for an audio thread.

namespace chalkwalk::dsp {

class Resampler {
public:
  Resampler(double inRate, double outRate, int halfTaps = 16,
            double stopbandDb = 96.0)
      : step_(inRate / outRate),
        fc_(std::min(1.0, outRate / inRate) * 0.97),
        width_((int)std::ceil(halfTaps / fc_)) {
    const double beta = fir::kaiserBeta(stopbandDb);
    const double norm = besselI0(beta);
    table_.resize((size_t)width_ * kPhases + 2);
    for (size_t i = 0; i < table_.size(); ++i) {
      const double x = (double)i / kPhases;  // distance in input samples
      if (x >= width_) {
        table_[i] = 0.0;
        continue;
      }
      const double r = x / width_;
      const double window = besselI0(beta * std::sqrt(1.0 - r * r)) / norm;
      const double a = kPi * fc_ * x;
      const double sinc = x == 0.0 ? 1.0 : std::sin(a) / a;
      table_[i] = (float)(fc_ * sinc * window);
    }
    reset();
  }

  void reset() {
    // Start with `width_` zeros of history, so the first output sample sits
    // on the first input sample rather than `width_` samples into it.
    hist_.assign((size_t)width_, 0.0f);
    base_ = -width_;
    pos_ = 0.0;
  }

  // Feed `n` input samples; appends every output sample that can now be made.
  void process(const float *in, int n, std::vector<float> &out) {
    hist_.insert(hist_.end(), in, in + n);
    const long last = base_ + (long)hist_.size() - 1;  // newest input index
    while (true) {
      const long centre = (long)std::floor(pos_);
      if (centre + width_ > last)
        break;
      out.push_back(sampleAt(centre));
      pos_ += step_;
    }
    // Drop history no future output can reach.
    const long keepFrom = (long)std::floor(pos_) - width_ + 1;
    const long drop = keepFrom - base_;
    if (drop > 4096) {
      hist_.erase(hist_.begin(), hist_.begin() + drop);
      base_ += drop;
    }
  }

  // How far behind its input the output runs, in input samples.
  int latencyInputSamples() const { return width_; }

private:
  static constexpr int kPhases = 512;

  float kernel(double distance) const {
    const double x = std::fabs(distance) * kPhases;
    const size_t i = (size_t)x;
    if (i + 1 >= table_.size())
      return 0.0f;
    const double f = x - (double)i;
    return (float)(table_[i] + (table_[i + 1] - table_[i]) * f);
  }

  float sampleAt(long centre) const {
    double acc = 0.0;
    for (long k = centre - width_ + 1; k <= centre + width_; ++k)
      acc += (double)hist_[(size_t)(k - base_)] * kernel(pos_ - (double)k);
    return (float)acc;
  }

  double step_;
  double fc_;
  int width_;
  std::vector<float> table_;
  std::vector<float> hist_;
  long base_ = 0;
  double pos_ = 0.0;
};

}  // namespace chalkwalk::dsp
