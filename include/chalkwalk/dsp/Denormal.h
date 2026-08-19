// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

// Denormals, and why a filter that never gets an input can still cost you the
// audio thread.
//
// A recursive filter's state decays toward zero and never reaches it. Below
// about 1e-38 a float stops being normalised and becomes a DENORMAL -- a
// gradual-underflow representation that many CPUs handle in microcode, at a
// cost of tens to hundreds of cycles per operation. So a filter fed silence
// gets SLOWER the longer it is silent, which is the opposite of what anyone
// expects and shows up as a dropout on an idle track rather than a busy one.
//
// The fix is to snap the state to zero once it is inaudible. 1e-9 is around
// -180 dBFS: far below the noise floor of any converter, and far above the
// denormal threshold, so the state passes through it long before the hardware
// notices.
//
// This is done at the point where state is STORED rather than by setting a
// flush-to-zero CPU mode, because the CPU mode is a global the host owns and a
// library has no business changing it -- and because a plugin cannot rely on
// the host having set it.

namespace chalkwalk::dsp {

inline constexpr float kFlushLevel = 1.0e-9f;

[[nodiscard]] inline float flush(float x) noexcept {
  return (x > kFlushLevel || x < -kFlushLevel) ? x : 0.0f;
}

}  // namespace chalkwalk::dsp
