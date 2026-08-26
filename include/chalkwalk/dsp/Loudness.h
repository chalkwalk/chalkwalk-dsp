// SPDX-License-Identifier: MIT
#pragma once

#include <ebur128.h>

#include <algorithm>
#include <cmath>
#include <vector>

// Loudness, split out from `Measure.h` because it is the one measurement with
// a dependency.
//
// Everything in `Measure.h` is arithmetic over a buffer and compiles anywhere.
// This file needs libebur128, and a consumer that wants to know whether a
// signal is silent, faint or clipping has no use for a loudness meter -- it
// wants peak and rms, and it should not link a BS.1770 implementation to get
// them. Before the split there was no way to ask: the include sat at the top
// of the header and every consumer paid for it.
//
// Same namespace as `Measure.h` on purpose. The argument for that header is
// that peak, rms, crest, brightness, pitch and loudness come from ONE place,
// and two files in one namespace is still one place -- what changes is what
// you have to link, not what you have to spell.
namespace chalkwalk::dsp::measure {

// Loudness, as ITU-R BS.1770 / EBU R128 hears it.
//
// RMS is not loudness, and the difference matters most for exactly the
// comparison this gets used for: a kick and a hi-hat at the same RMS are
// nowhere near the same loudness, because the ear is far less sensitive at
// 50 Hz than at 8 kHz. Balancing a band by RMS therefore flatters whatever is
// lowest, and the drums were the thing being balanced.
//
// MEASURED BY libebur128 (MIT), not here. There was a K-weighting pair and a
// two-stage gate in this file, and they were correct -- validated against
// ffmpeg's ebur128 to inside 0.05 LU on every case the suite covers, which is
// why the swap could be checked rather than trusted. They were deleted
// anyway, on the standing rule: take the dependency when the thing has a
// SPECIFICATION you could fail to meet.
//
// The failure being avoided is not today's. It is the momentary and
// short-term measures, the loudness range, the true peak, and whatever the
// next revision of BS.1770 says -- each of which is a further piece of a
// standard to track by hand, each correct only until it silently is not.
// Being right once is not the same as staying right, and a reimplementation
// gives you no way to tell the difference.
//
// What is kept is the interface. `integratedLufs` still takes two channel
// pointers and a sample rate and returns LUFS, so that peak, rms, crest,
// brightness, pitch and loudness continue to come from ONE place -- which is
// the whole argument for these headers, and the one shim the dependency rule
// defends by name.

inline constexpr double kSilenceLufs = -70.0;

// Integrated loudness in LUFS. `right` may be null for a single channel.
//
// Needs at least one 400 ms block; anything shorter returns the silence floor,
// because the standard has nothing to say about a shorter measurement and
// inventing an answer would be worse than admitting there is not one.
inline double integratedLufs(const float *left, const float *right,
                             int numSamples, double sampleRate) {
  if (left == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return kSilenceLufs;

  // Shorter than one gating block is refused here rather than deeper down.
  // libebur128 answers -HUGE_VAL, which is indistinguishable from silence;
  // "there was not enough audio to measure" and "the audio was silent" are
  // different facts, and only one of them is about the signal.
  const int blockSamples = (int)(0.4 * sampleRate);
  if (blockSamples <= 0 || numSamples < blockSamples)
    return kSilenceLufs;

  const unsigned channels = right != nullptr ? 2u : 1u;

  ebur128_state *st =
      ebur128_init(channels, (unsigned long)sampleRate, EBUR128_MODE_I);
  if (st == nullptr)
    return kSilenceLufs;

  // libebur128 takes interleaved frames, and this header takes a pointer per
  // channel, so one copy is unavoidable. It is a measurement path -- offline,
  // over whole takes -- so the copy costs nothing that matters.
  std::vector<float> interleaved((size_t)numSamples * channels);
  if (channels == 2) {
    for (int i = 0; i < numSamples; ++i) {
      interleaved[(size_t)i * 2] = left[i];
      interleaved[(size_t)i * 2 + 1] = right[i];
    }
  } else {
    std::copy(left, left + numSamples, interleaved.begin());
  }

  double lufs = kSilenceLufs;
  if (ebur128_add_frames_float(st, interleaved.data(), (size_t)numSamples) ==
      EBUR128_SUCCESS) {
    double measured = 0.0;
    if (ebur128_loudness_global(st, &measured) == EBUR128_SUCCESS &&
        measured > kSilenceLufs)
      lufs = measured;
  }

  ebur128_destroy(&st);
  return lufs;
}

inline double integratedLufs(const float *data, int numSamples,
                             double sampleRate) {
  return integratedLufs(data, nullptr, numSamples, sampleRate);
}

// The gain that moves a measured loudness onto a target one.
inline double gainForLufs(double measuredLufs, double targetLufs) {
  if (measuredLufs <= kSilenceLufs)
    return 1.0;
  return std::pow(10.0, (targetLufs - measuredLufs) / 20.0);
}

}  // namespace chalkwalk::dsp::measure
