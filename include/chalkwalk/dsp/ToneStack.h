#pragma once

// A Baxandall-style bass and treble pair, IN THE RECORD PATH.
//
// RECORD SIDE, AND THAT IS THE WHOLE POINT (`PRINCIPLES §2`). Slipback's tone
// controls affect the echo and not the dry or the reverb, which is attested;
// WHERE on the wet path they sit is not in Roland's manual (`SOURCES §52`), and
// this project puts them before the record head. So the tone you set is
// MAGNETISED ONTO THE TAPE: darken the treble and the repeats already
// circulating stay bright while new ones arrive dull, and the change washes
// through the loop over several repeats instead of applying to all of them at
// once. That behaviour is available to a machine whose medium stores and to no
// other kind of echo.
//
// AND IT IS WHY THE FEEDBACK RISK IS SMALLER HERE THAN IT LOOKS. A boost inside
// the loop raises the loop gain in its band either way -- but on the record side
// the tape is downstream of it, and the record chain already pre-emphasises so
// that short wavelengths saturate first (`SOURCES §29`). A bright, hot loop
// therefore saturates the top of the band rather than growing without bound.
// The same control on the PLAYBACK side would boost after the tape with nothing
// to stop it.
//
// FIRST ORDER, because a 1974 tone stack is. Two shelves, far enough apart
// (100 Hz and 5 kHz) that neither reaches the other's corner.
//
// JUCE-free by design. Part of chalkwalk-dsp.

#include <algorithm>
#include <cmath>

namespace chalkwalk::dsp
{
    class ToneStack
    {
    public:
        // Roland's own reference data for the RE-201: "Treble ... +10dB at
        // 5KHz, Bass .... +10dB at 100Hz" (`SOURCES §52`). The BOOST is what the
        // manual states; the cut is not given, and symmetry is the assumption.
        static constexpr double kBassHz = 100.0;
        static constexpr double kTrebleHz = 5000.0;
        static constexpr double kRangeDb = 10.0;

        void prepare(double sampleRateHz) noexcept
        {
            const double fs = sampleRateHz > 0.0 ? sampleRateHz : 48000.0;
            bassCoeff_ = onePole(kBassHz, fs);
            trebleCoeff_ = onePole(kTrebleHz, fs);
            reset();
        }

        void reset() noexcept { bassState_ = 0.0; trebleState_ = 0.0; }

        // Decibels, as the panel reads them.
        void setBassDb(double db) noexcept
        {
            bassGain_ = std::pow(10.0, std::clamp(db, -kRangeDb, kRangeDb) / 20.0);
        }
        void setTrebleDb(double db) noexcept
        {
            trebleGain_ = std::pow(10.0, std::clamp(db, -kRangeDb, kRangeDb) / 20.0);
        }

        [[nodiscard]] double bassGain() const noexcept { return bassGain_; }
        [[nodiscard]] double trebleGain() const noexcept { return trebleGain_; }

        // ---- THE MOST THE STACK CAN ADD AT ANY FREQUENCY ----
        //
        // What the runaway calculation needs, and the reason this is not simply
        // the product of the two: the shelves are fifty times apart, so at DC
        // the treble shelf is at unity and at the top the bass shelf is, and
        // neither ever multiplies the other. The peak is whichever shelf is
        // boosting most, or unity if both are cutting.
        //
        // A CEILING RATHER THAN A CURVE, deliberately. The loop finds whichever
        // frequency has the most gain, so what the marker needs is the worst
        // case and not the response.
        [[nodiscard]] double peakGain() const noexcept
        {
            return std::max(1.0, std::max(bassGain_, trebleGain_));
        }

        [[nodiscard]] bool flat() const noexcept
        {
            return bassGain_ == 1.0 && trebleGain_ == 1.0;
        }

        [[nodiscard]] double process(double x) noexcept
        {
            // Low shelf: the signal plus what the lowpass has, scaled by how
            // much more or less of it is wanted.
            bassState_ += bassCoeff_ * (x - bassState_);
            const double afterBass = x + (bassGain_ - 1.0) * bassState_;

            // High shelf, the same trick with the complement.
            trebleState_ += trebleCoeff_ * (afterBass - trebleState_);
            return afterBass + (trebleGain_ - 1.0) * (afterBass - trebleState_);
        }

    private:
        [[nodiscard]] static double onePole(double hz, double fs) noexcept
        {
            const double a = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * hz / fs);
            return std::clamp(a, 0.0, 1.0);
        }

        double bassCoeff_ = 0.0, trebleCoeff_ = 0.0;
        double bassState_ = 0.0, trebleState_ = 0.0;
        double bassGain_ = 1.0, trebleGain_ = 1.0;
    };
}
