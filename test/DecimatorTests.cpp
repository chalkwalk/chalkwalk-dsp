// The record chain's final decimation, and the upsampling that feeds it.
//
// These were written in Remanence, where the ladder runs 48 -> 768 -> 128 kHz,
// and moved here unchanged when the pair moved. The constants still name that
// ladder because that is where the numbers were measured; nothing in the code
// under test knows about it -- `design()` takes every rate as an argument.
//
// RECORD SIDE (PRINCIPLES section 2): whatever this stage aliases is written
// onto the medium permanently, so its stopband is not a quality setting.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/dsp/Decimator.h>
#include <chalkwalk/dsp/Interpolator.h>

#include <cmath>
#include <vector>

using Catch::Approx;
namespace tape = chalkwalk::dsp;

namespace
{
    constexpr double kIn = 768000.0;    // engine 48k x16
    constexpr double kOut = 128000.0;   // the medium (DESIGN 4.2's ladder)
    constexpr double k15ips = 0.381;
    constexpr double kLambdaCut = 6.78e-6;   // DESIGN 6.0's table, Capstan
    const double kPass = k15ips / kLambdaCut;  // 56.2 kHz

    double composite(const tape::Decimator& d, double hz)
    {
        return d.stageA().magnitudeAt(hz, kIn)
             * d.stageB().magnitudeAt(hz, kIn / 3.0);
    }

    // The worst leak at any frequency that folds back into the passband.
    double worstAlias(const tape::Decimator& d)
    {
        double worst = 0.0;
        for (double hz = kOut - kPass; hz < 0.5 * kIn; hz += 5.0)
        {
            const double image = std::abs(hz - kOut * std::round(hz / kOut));
            if (image <= kPass)
                worst = std::max(worst, composite(d, hz));
        }
        return 20.0 * std::log10(worst);
    }
}

TEST_CASE("the design meets its stopband instead of estimating it",
          "[decimator]")
{
    // KAISER'S TAP FORMULA IS AN ESTIMATE, AND ESTIMATING WAS NOT GOOD ENOUGH.
    //
    // beta and the tap count pull against each other: raising the target raises
    // beta, which WIDENS the transition, so if the rounded tap count does not
    // also rise the filter comes out worse. Measured before the design verified
    // itself, asking for 96 dB realised 83 where asking for 93 realised 93 -- a
    // design that got worse as it was asked for more.
    //
    // That is invisible to any test that only checks one design point, which is
    // why this sweeps: what must hold is that MORE IS NEVER WORSE.
    double previous = 0.0;
    for (const double target : {89.0, 93.0, 96.0, 100.0, 105.0})
    {
        tape::Decimator d;
        d.design(kIn, kOut, kPass, target);
        const double realised = worstAlias(d);
        INFO("asked for " << target << " dB, realised " << realised);
        REQUIRE(realised <= -target);
        REQUIRE(realised <= previous + 1.0e-9);  // monotone in the target
        previous = realised;
    }
}

TEST_CASE("the alias floor clears the instrument's own floor", "[decimator]")
{
    // -89 dB is where RemanenceBench's aliasing measurement bottoms out
    // (DESIGN 4.2), so a decimator leaking above it would be the thing being
    // measured rather than the thing under test.
    tape::Decimator d;
    d.design(kIn, kOut, kPass, 89.0);
    const double floorDb = worstAlias(d);
    INFO("worst alias " << floorDb << " dB");
    REQUIRE(floorDb <= -89.0);
}

TEST_CASE("the passband is flat to lambda_cut, not to the audio band",
          "[decimator]")
{
    // THE MISTAKE THIS PINS. The engine is 48 kHz, so band-limiting here to
    // 24 kHz looks obviously right and is wrong: DESIGN 6.0 chooses R so that
    // spatial Nyquist lands PAST the head's own cutoff, precisely so the top
    // end is set by gap loss rather than by sampling. Content at 56 kHz on the
    // medium arrives at 28 kHz when the tape is played at half speed.
    //
    // A decimator that stopped at 24 kHz would put a brick wall exactly where
    // section 6.0 went to trouble to avoid one, and it would only be audible
    // under varispeed -- long after it was written.
    tape::Decimator d;
    d.design(kIn, kOut, kPass, 89.0);

    double low = 1.0e30, high = 0.0;
    for (double hz = 0.0; hz <= kPass; hz += 100.0)
    {
        const double m = composite(d, hz);
        low = std::min(low, m);
        high = std::max(high, m);
    }
    const double rippleDb = 20.0 * std::log10(high / low);
    INFO("passband ripple " << rippleDb << " dB to " << kPass << " Hz");
    REQUIRE(rippleDb < 0.1);

    // And it really does still pass well above the audio band.
    REQUIRE(composite(d, 40000.0) > 0.99);
}

TEST_CASE("the last stage is a halfband, which the ladder made legal",
          "[decimator]")
{
    // ROADMAP.md said the last stage may NOT be a halfband, and that was true
    // of the ladder it was written for: decimating to the ENGINE rate, the
    // final stage transitions around 24 kHz with hysteresis harmonics reaching
    // right up to it, so the fold lands in the music.
    //
    // Decimating to the MEDIUM rate, stage B runs at 256 kHz where a halfband
    // is symmetric about 64 kHz -- and the passband edge (56.2) and the fold
    // point (71.8) straddle it evenly. The margin that rounding R up bought is
    // what makes the cheap structure legal.
    //
    // Asserted because it is worth about a third of the total cost, and because
    // a change to the rate ladder would silently take it away.
    tape::Decimator d;
    d.design(kIn, kOut, kPass, 89.0);

    const double fraction = static_cast<double>(d.stageB().multiplies())
                          / static_cast<double>(d.stageB().length());
    INFO("stage B: " << d.stageB().multiplies() << " multiplies of "
         << d.stageB().length() << " taps");
    REQUIRE(fraction < 0.55);   // about half the taps are structurally zero
    REQUIRE(fraction > 0.45);
}

TEST_CASE("two stages cost far less than one", "[decimator]")
{
    // The reason for 3:1 then 2:1 rather than a single 6:1. Decimating first at
    // the highest rate leaves the widest transition relative to the rate, so
    // the short filter runs often and the sharp one runs at a sixth.
    tape::Decimator d;
    d.design(kIn, kOut, kPass, 89.0);

    // The bench's instrument for the same job: 64 taps per factor, 385 at 6:1,
    // evaluated at the output rate.
    const double instrument = (2.0 * 32.0 * 6.0 + 1.0) / 6.0;
    INFO("decimator " << d.multipliesPerInput()
         << " multiplies per input, instrument " << instrument);
    REQUIRE(d.multipliesPerInput() < instrument / 3.0);
}

// ---------------------------------------------------------------------------
// The other half of the rate conversion: 48 -> 768 kHz.
// ---------------------------------------------------------------------------

TEST_CASE("the upsampler suppresses its images", "[interpolator]")
{
    // THE TEST THAT CATCHES THE BUG THIS CLASS WAS WRITTEN WITH.
    //
    // Each stage is called several times per input -- stage s runs 2^s times --
    // and those calls are consecutive samples at that stage's rate, so they
    // must arrive in TIME ORDER. Expanding the buffer in place needs a
    // descending loop to avoid clobbering, which feeds every stage its samples
    // backwards. That was the first version, and the first image came back
    // 2 dB ABOVE the fundamental.
    //
    // A filter fed time-reversed input is still a filter: nothing crashes,
    // nothing returns NaN, and the output is a plausible-looking signal. Only
    // the spectrum shows it.
    constexpr double kIn48 = 48000.0;
    const double passband = 0.45 * kIn48;

    tape::Interpolator up;
    up.design(kIn48, passband, 89.0);

    const double probe = 10000.0;
    const double outRate = kIn48 * tape::Interpolator::kFactor;

    std::vector<double> y;
    y.reserve(1u << 20);
    double buffer[tape::Interpolator::kFactor] = {};
    for (int n = 0; n < 32768; ++n)
    {
        up.push(std::sin(2.0 * M_PI * probe * n / kIn48), buffer);
        for (int i = 0; i < tape::Interpolator::kFactor; ++i)
            y.push_back(buffer[i]);
    }

    const auto magnitudeAt = [&y, outRate](double hz)
    {
        const std::size_t from = y.size() / 4;   // past the filter's own settling
        double re = 0.0, im = 0.0;
        for (std::size_t i = from; i < y.size(); ++i)
        {
            const double w = 2.0 * M_PI * hz * static_cast<double>(i - from) / outRate;
            re += y[i] * std::cos(w);
            im += y[i] * std::sin(w);
        }
        return 2.0 * std::sqrt(re * re + im * im)
             / static_cast<double>(y.size() - from);
    };

    const double fundamental = magnitudeAt(probe);
    INFO("fundamental " << fundamental);
    REQUIRE(fundamental == Approx(1.0).margin(0.01));   // unity gain, not 1/16

    for (const double image : {kIn48 - probe, kIn48 + probe,
                               2 * kIn48 - probe, 2 * kIn48 + probe})
    {
        const double db = 20.0 * std::log10(magnitudeAt(image) / fundamental);
        INFO("image at " << image << " Hz: " << db << " dB");
        REQUIRE(db < -89.0);
    }
}

TEST_CASE("the upsampler's work is at the TOP of the ladder", "[interpolator]")
{
    // Worth pinning because it is the opposite of what the tap counts suggest.
    // A stage's filter gets shorter as the ladder climbs -- the transition it
    // must resolve is fixed in hertz while its rate doubles -- but it also runs
    // twice as often, and the second effect wins. So the 19-tap stage at the
    // top costs more than the 123-tap stage at the bottom, and any attempt to
    // economise should start where the filter looks cheapest.
    tape::Interpolator up;
    up.design(48000.0, 0.45 * 48000.0, 89.0);

    const double bottom = static_cast<double>(up.stage(0).multiplies());
    const double top = static_cast<double>(up.stage(3).multiplies()) * 8.0;

    INFO("stage 0 " << up.stage(0).length() << " taps costing " << bottom
         << "; stage 3 " << up.stage(3).length() << " taps costing " << top);
    REQUIRE(up.stage(3).length() < up.stage(0).length());   // shorter filter
    REQUIRE(top > bottom);                                   // and more work
}

TEST_CASE("the upsampler costs far less than the instrument", "[interpolator]")
{
    tape::Interpolator up;
    up.design(48000.0, 0.45 * 48000.0, 89.0);

    // The bench's instrument for the same job: 64 taps per factor, 1025 at 16x,
    // and its inner loop touches every one per input sample.
    const double instrument = 2.0 * 32.0 * 16.0 + 1.0;
    INFO("upsampler " << up.multipliesPerInput() << " vs instrument " << instrument);
    REQUIRE(up.multipliesPerInput() < instrument / 3.0);
}

TEST_CASE("the decimator does the factor it was asked for", "[decimator][teeth]")
{
    // IT DID NOT. `Decimator::design` took an input and an output rate but
    // hardcoded its stages at 3 then 2, so ANY ratio asked for produced 6:1.
    // That went unnoticed until the ladder became per machine and Splice asked
    // for 9 -- its tape was then written half again too fast, permanently.
    //
    // AND TWO TESTS FAILED TO CATCH IT, both for the same reason: they measured
    // something downstream that saturates. One compared a write rate against a
    // read rate, both computed from the decimation the DECK had asked for, so
    // neither knew what the decimator did. The other counted engine samples that
    // produced at least one deposit, which is 1.0 for any ladder emitting more
    // than one per sample.
    //
    // Counting the decimator's own outputs is the only measurement that sees it.
    for (int factor : { 6, 9, 12 })
    {
        tape::Decimator decimator;
        decimator.design(768000.0, 768000.0 / factor, 48000.0);

        constexpr int kInputs = 60000;
        int outputs = 0;
        for (int i = 0; i < kInputs; ++i)
        {
            double out = 0.0;
            if (decimator.push(std::sin(0.01 * i), out))
                ++outputs;
        }

        const double measured = double(kInputs) / double(outputs);
        INFO("asked for /" << factor << ", got /" << measured);
        REQUIRE(measured == Approx(double(factor)).epsilon(0.01));
    }
}
