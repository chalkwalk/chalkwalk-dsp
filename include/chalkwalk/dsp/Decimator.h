#pragma once

// The record chain's final decimation, 768 -> 128 kHz (DESIGN.md section 4.2).
//
// RECORD SIDE (PRINCIPLES section 2): what this stage passes is written onto
// the medium permanently, and what it aliases is written there too.
//
// NOT THE BENCH'S `decimate`, WHICH IS AN INSTRUMENT. That one holds its cutoff
// at 0.45 of the BASE rate whatever the oversampling factor, so its tap count
// grows as 64x the factor and its leakage sits far below whatever is being
// measured. That is the right design for a ruler and the wrong one for a
// product: 385 taps against the 31 to 93 here.
//
// WHAT THE STAGE HAS TO PRESERVE, which is not obvious and sets everything
// else. It is tempting to keep only the audio band, since the engine is 48 kHz.
// That would be wrong. DESIGN.md section 6.0 chooses `R` so that spatial
// Nyquist lands PAST the head's own cutoff wavelength, precisely so the top end
// is set by gap loss and not by sampling -- and the reason that matters is
// varispeed. Content at 56 kHz on the medium arrives at 28 kHz when the tape is
// played at half speed. Band-limiting to 24 kHz here would put a brick wall
// exactly where section 6.0 went to trouble to avoid one.
//
// So the passband edge is `lambda_cut`: 56.2 kHz for Capstan at 15 ips.
//
// THE TRANSITION BAND IS THE MARGIN THE RATE LADDER ALREADY BOUGHT. Aliasing
// folds about the output rate, so the first fold into the passband comes from
// 128 - 56.2 = 71.8 kHz. Passband 56.2, stopband 71.8, and the medium's Nyquist
// at 64.0 sitting neatly between them -- that gap exists only because section
// 4.2 rounded `R` up until the ladder was integer. The same rounding that made
// 768/6 whole is what makes this filter affordable.
//
// TWO STAGES, 3:1 THEN 2:1, AND THE ORDER IS MEASURED NOT ASSUMED. Decimating
// first at the highest rate leaves the widest transition band relative to the
// rate, so the first filter is short and the sharp one runs at a sixth of the
// work. Estimated at -89 dB: single-stage 6:1 needs ~278 taps and 46 MACs per
// input sample; 3:1 then 2:1 needs ~30 and ~93 for 18.
//
// AND THE LAST STAGE IS A HALFBAND, WHICH ROADMAP.md SAID IT COULD NOT BE.
// That claim was true of the ladder it was written for. Decimating to the
// ENGINE rate, the final stage transitions around 24 kHz with hysteresis
// harmonics reaching right up to it, so there is no margin and the fold lands
// in the music. Decimating to the MEDIUM rate, stage B runs at 256 kHz where a
// halfband is symmetric about 64 kHz -- and 56.2 and 71.8 straddle it evenly.
// The margin makes the cheap structure legal.
//
// AND THEN THE BIAS CARRIER MADE IT A REAL LOWPASS. Everything above sizes the
// stopband from the FOLD -- the anti-aliasing requirement and nothing else --
// which leaves the passband reaching 57.6 kHz with the 55 kHz carrier inside it
// by 2.6 kHz. Measured, that put 0.5429 of the medium's range into bias alone.
// `RecordChain` now passes an explicit stopband below the carrier, which costs
// stage B its halfband symmetry and takes it from 123 taps to 247. The
// varispeed argument above survives intact: the passband is still set by what a
// 2:1 speed range can bring into the audio band, and everything measured
// between the audio band and the carrier lives below 50 kHz.
//
// JUCE-free by design. Promotion target: chalkwalk-dsp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace chalkwalk::dsp
{
    // One decimating FIR stage, evaluated only where an output is wanted.
    class DecimatorStage
    {
    public:
        // Kaiser-windowed sinc. The window is chosen for a stated stopband
        // attenuation rather than by taste, which is the whole reason to use
        // Kaiser here: `beta` follows from `stopbandDb` by Kaiser's own
        // formula, so the filter is specified by what it must achieve.
        void design(double sampleRate, int factor, double passbandHz,
                    double stopbandHz, double stopbandDb)
        {
            factor_ = factor < 1 ? 1 : factor;

            const double transition = (stopbandHz - passbandHz) / sampleRate;
            int taps = (transition > 0.0)
                ? static_cast<int>(std::ceil((stopbandDb - 8.0)
                                             / (2.285 * 2.0 * M_PI * transition)))
                : 8;
            if (taps < 8) taps = 8;
            taps |= 1;  // odd, so there is a centre tap and a whole-sample delay

            const double beta = kaiserBeta(stopbandDb);

            // KAISER'S TAP FORMULA IS AN ESTIMATE, SO THE DESIGN VERIFIES
            // ITSELF RATHER THAN TRUSTING IT.
            //
            // The estimate rounds, and beta and the tap count pull against each
            // other: raising the target raises beta, which WIDENS the
            // transition, and if the rounded tap count does not also rise the
            // filter comes out worse. Measured, asking for 96 dB gave 83 where
            // asking for 93 gave 93 -- a design that got worse as it was asked
            // for more, which is exactly the kind of result a formula hides and
            // a measurement cannot.
            //
            // So `build` designs at a tap count and the loop below grows it
            // until the realised stopband actually meets the specification. It
            // runs at design time, on the message thread, and terminates: each
            // pass adds taps, and attenuation is monotonic in taps at fixed
            // beta.
            for (int attempt = 0; attempt < 64; ++attempt)
            {
                build(taps, beta, passbandHz, stopbandHz, sampleRate);
                if (worstStopband(stopbandHz, sampleRate) <= stopbandDb * -1.0)
                    break;
                taps += 2;
            }
            finish();
        }

    private:
        void build(int taps, double beta, double passbandHz, double stopbandHz,
                   double sampleRate)
        {
            const double cutoff = 0.5 * (passbandHz + stopbandHz) / sampleRate;
            const int half = taps / 2;
            (void) sampleRate;

            coefficients_.assign(static_cast<std::size_t>(taps), 0.0);
            double sum = 0.0;
            for (int n = -half; n <= half; ++n)
            {
                const double x = static_cast<double>(n);
                const double sinc = (n == 0) ? 2.0 * cutoff
                                             : std::sin(2.0 * M_PI * cutoff * x)
                                               / (M_PI * x);
                const double r = x / static_cast<double>(half);
                const double w = std::cyl_bessel_i(0.0, beta * std::sqrt(1.0 - r * r))
                               / std::cyl_bessel_i(0.0, beta);
                coefficients_[static_cast<std::size_t>(n + half)] = sinc * w;
                sum += sinc * w;
            }
            // Unity at DC. A decimator that changed the level would read as the
            // tape being quiet rather than as the filter being wrong.
            for (auto& c : coefficients_)
                c /= sum;

            // Skip the structurally zero taps. A halfband -- which stage B is,
            // when the passband and stopband straddle the quarter rate evenly --
            // has every other one exactly zero, and the point of choosing that
            // structure is not to multiply by them.
        }

        // The realised stopband, in dB (negative). Sampled finely enough to
        // land on the ripple peaks rather than between them.
        [[nodiscard]] double worstStopband(double stopbandHz, double sampleRate) const noexcept
        {
            double worst = 0.0;
            const double step = sampleRate / 20000.0;
            for (double hz = stopbandHz; hz <= 0.5 * sampleRate; hz += step)
                worst = std::max(worst, magnitudeAt(hz, sampleRate));
            return 20.0 * std::log10(std::max(worst, 1.0e-30));
        }

        void finish()
        {
            //
            // RELATIVE TO THE PEAK, NOT ABSOLUTE, and the difference decides
            // whether the halfband is one. A halfband's even taps are
            // sin(k*pi)/(k*pi), which is exactly zero in algebra and about
            // 1e-17 in floating point. An absolute threshold of 1e-18 keeps
            // every one of them, so the structure is a halfband and the
            // arithmetic is not: measured, that left 81 of 93 multiplies
            // instead of 47.
            double peak = 0.0;
            for (const double c : coefficients_)
                peak = std::max(peak, std::abs(c));

            live_.clear();
            for (std::size_t i = 0; i < coefficients_.size(); ++i)
                if (std::abs(coefficients_[i]) > 1.0e-12 * peak)
                    live_.push_back({i, coefficients_[i]});

            // ---- SPARSE OR DENSE, WHICHEVER IS ACTUALLY CHEAPER ----
            //
            // The sparse list exists for the HALFBAND, where every second tap
            // is a real zero and skipping them halves the work. On a filter
            // that is NOT a halfband it costs more than it saves: the loop
            // walks `{index, value}` pairs and reaches history through
            // `p[n - 1 - t.index]`, which is an indirection and a backwards
            // walk, and neither vectorises.
            //
            // Measured (`RemanenceBench perf-detail`): the two stages together
            // did 12.8 multiplies a step and took 15.23 ns, which is 1.17 ns a
            // multiply against the interpolator's 0.64 on the same machine.
            // Stage A keeps 25 taps of 35 -- barely sparse -- and pays the
            // indirection on every one of them.
            //
            // AND THE DENSE FORM IS A PLAIN DOT PRODUCT, because the kernel is
            // SYMMETRIC. `coefficients_[i] == coefficients_[n - 1 - i]`, so
            // reversing the coefficients is a no-op and `sum c[i] * p[i]` over
            // contiguous memory is the same number the reversed sparse walk
            // produced. That is a form the compiler can vectorise.
            dense_ = live_.size() * 5 > coefficients_.size() * 3;   // over 60 %

            history_.assign(coefficients_.size() * 2, 0.0);
            cursor_ = 0;
            phase_ = 0;
        }

    public:
        void reset() noexcept
        {
            for (auto& v : history_) v = 0.0;
            cursor_ = 0;
            phase_ = 0;
        }

        // Feed one input. Returns true and sets `out` on the samples where an
        // output falls; the convolution is evaluated only then, which is where
        // the factor's saving actually comes from.
        bool push(double x, double& out) noexcept
        {
            const std::size_t n = coefficients_.size();
            if (n == 0) { out = x; return true; }

            history_[cursor_] = x;
            history_[cursor_ + n] = x;
            cursor_ = (cursor_ + 1 == n) ? 0 : cursor_ + 1;

            if (++phase_ < factor_)
                return false;
            phase_ = 0;

            const double* p = history_.data() + cursor_;
            double acc = 0.0;
            if (dense_)
            {
                const double* c = coefficients_.data();
                for (std::size_t i = 0; i < n; ++i)
                    acc += c[i] * p[i];
            }
            else
            {
                for (const auto& t : live_)
                    acc += t.value * p[n - 1 - t.index];
            }
            out = acc;
            return true;
        }

        [[nodiscard]] std::size_t length() const noexcept { return coefficients_.size(); }
        [[nodiscard]] std::size_t multiplies() const noexcept { return live_.size(); }

        [[nodiscard]] double magnitudeAt(double hz, double sampleRate) const noexcept
        {
            double re = 0.0, im = 0.0;
            for (std::size_t i = 0; i < coefficients_.size(); ++i)
            {
                const double a = -2.0 * M_PI * hz * static_cast<double>(i) / sampleRate;
                re += coefficients_[i] * std::cos(a);
                im += coefficients_[i] * std::sin(a);
            }
            return std::sqrt(re * re + im * im);
        }

    private:
        struct Tap { std::size_t index; double value; };

        // Kaiser's own relation between stopband attenuation and beta.
        static double kaiserBeta(double db) noexcept
        {
            if (db > 50.0) return 0.1102 * (db - 8.7);
            if (db >= 21.0) return 0.5842 * std::pow(db - 21.0, 0.4)
                                 + 0.07886 * (db - 21.0);
            return 0.0;
        }

        std::vector<double> coefficients_;
        std::vector<Tap> live_;
        std::vector<double> history_;
        bool dense_ = false;
        std::size_t cursor_ = 0;
        int factor_ = 1;
        int phase_ = 0;
    };

    // The record chain's 6:1, as 3:1 then 2:1.
    class Decimator
    {
    public:
        // `stopbandHz` of zero means "derive it from the fold", which is the
        // anti-aliasing requirement and nothing more: content above
        // `outputRate - passbandHz` is what lands back in the passband.
        //
        // AN EXPLICIT STOPBAND IS FOR REJECTING SOMETHING THAT IS NOT AN ALIAS.
        // The record chain has exactly one such thing -- the bias carrier, which
        // is inside the fold-derived passband and would otherwise be written to
        // the medium at full amplitude, where it costs 6.8 dB of the store's
        // range and buys nothing that is ever reproduced. Passing a stopband
        // BELOW the fold makes this a real lowpass rather than only a guard, and
        // it is not free: the transition narrows and stage B stops being a
        // halfband. `RecordChain` states what it is buying with that.
        // THE TOTAL FACTOR IS `inputRate / outputRate`, and it is no longer
        // assumed to be six. It was: `mid` was `inputRate / 3` and the stages
        // were hardcoded 3 then 2, so asking for any other ratio silently got
        // 6:1 anyway -- which is what happened when the ladder became per
        // machine and Splice asked for 9. Its tape was written half again too
        // fast, and the test that should have caught it compared two numbers
        // both derived from the ratio it had ASKED for rather than from what the
        // decimator did.
        //
        // Stage A stays at 3 because it is the cheap one and wants the widest
        // transition; stage B takes the rest.
        void design(double inputRate, double outputRate, double passbandHz,
                    double stopbandDb = 89.0, double stopbandHz = 0.0)
        {
            const int total = static_cast<int>(std::lround(inputRate / outputRate));
            const int stageA = 3;
            const int stageB = (total >= stageA && total % stageA == 0)
                             ? total / stageA : 2;
            const double mid = inputRate / stageA;
            const double fold = outputRate - passbandHz;
            const double edge = (stopbandHz > passbandHz && stopbandHz < fold)
                              ? stopbandHz : fold;
            // Stage A only has to keep clean whatever stage B will fold into
            // the passband; its own stopband may start much higher, which is
            // what makes it short. The carrier is well inside stage A's
            // passband and stays there -- stage B is what removes it.
            first_.design(inputRate, stageA, passbandHz, mid - passbandHz, stopbandDb);
            // Stage B is the sharp one, and runs at a third of the rate.
            second_.design(mid, stageB, passbandHz, edge, stopbandDb);
        }

        void reset() noexcept { first_.reset(); second_.reset(); }

        // How many INPUT samples it takes for both stages to be running on real
        // history rather than on the zeros they were reset to. Stage B sees one
        // sample per `stageA` inputs, so its length costs that much more.
        [[nodiscard]] std::size_t settlingInputs() const noexcept
        {
            return first_.length() + second_.length() * 3 + 4;
        }

        bool push(double x, double& out) noexcept
        {
            double mid = 0.0;
            if (! first_.push(x, mid))
                return false;
            return second_.push(mid, out);
        }

        [[nodiscard]] const DecimatorStage& stageA() const noexcept { return first_; }
        [[nodiscard]] const DecimatorStage& stageB() const noexcept { return second_; }

        // Multiplies per INPUT sample, which is the figure that compares
        // against anything else in the chain.
        [[nodiscard]] double multipliesPerInput() const noexcept
        {
            return static_cast<double>(first_.multiplies()) / 3.0
                 + static_cast<double>(second_.multiplies()) / 6.0;
        }

    private:
        DecimatorStage first_, second_;
    };
}
