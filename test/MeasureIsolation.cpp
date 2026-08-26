// SPDX-License-Identifier: MIT

// The split, asserted at compile time.
//
// `Measure.h` is supposed to need nothing but a compiler, and `Loudness.h` is
// supposed to be the only file in this library that needs libebur128. Neither
// claim is checked by the suite: `chalkwalk_dsp_tests` links BOTH targets, so
// an `#include <ebur128.h>` drifting back to the top of `Measure.h` would
// compile there and go on compiling until some consumer that linked only
// `chalkwalk::dsp` tried to build -- which, since the point of the split is a
// JUCE-free library that never wanted a loudness meter, would be somebody
// else's repository and not this one.
//
// So this file includes `Measure.h` and NOTHING else, and its target links
// `chalkwalk::dsp` and NOTHING else. It has no test cases and is never run;
// the build succeeding is the whole assertion. Deleting the include from
// `Loudness.h`'s side of the wall is what it is here to notice.
#include <chalkwalk/dsp/Measure.h>

namespace {

// Odr-used so the header is instantiated rather than merely parsed, and taken
// through one function from each group -- level, dB, brightness and pitch --
// so that a dependency reaching any of them is caught here.
float measureWithoutLoudness(const float *data, int numSamples,
                             double sampleRate) {
  namespace m = chalkwalk::dsp::measure;
  const float level = m::peak(data, numSamples) + m::rms(data, numSamples) +
                      m::crest(data, numSamples);
  const double spectral =
      m::toDb(1.0) + m::brightnessHz(data, numSamples, sampleRate) +
      m::crossingRateHz(data, numSamples, sampleRate) +
      m::fundamentalHz(data, numSamples, sampleRate) +
      m::firstNoteHz(data, numSamples, sampleRate, numSamples / 4) +
      m::midiForHz(440.0) + (double)m::pitchClassForHz(440.0);
  return level + (float)spectral;
}

} // namespace

// Referenced so no compiler decides the whole file is dead and skips it.
float (*chalkwalkDspMeasureIsolationProbe)(const float *, int,
                                           double) = &measureWithoutLoudness;
