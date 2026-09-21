// The measuring instrument, calibrated before anything relies on it.
//
// A detector nobody has calibrated is how a measurement error gets mistaken for
// a bug, so this is checked against closed-form answers BEFORE the bench relies
// on it -- including the check that catches a transform running backwards, which
// a magnitude-only test cannot see.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/dsp/Spectrum.h>

#include <cmath>
#include <complex>
#include <vector>

using Catch::Approx;
namespace dsp = chalkwalk::dsp::spectrum;

TEST_CASE("the transform puts DC in bin zero", "[spectrum]")
{
    std::vector<double> signal(1024, 1.0);
    const auto spectrum = dsp::forwardFft(signal);

    REQUIRE(std::abs(spectrum[0]) == Approx(1024.0).epsilon(1.0e-12));
    for (std::size_t k = 1; k < 16; ++k)
        REQUIRE(std::abs(spectrum[k]) == Approx(0.0).margin(1.0e-9));
}

TEST_CASE("a bin-k sine lands in bins k and n-k at height n/2", "[spectrum]")
{
    constexpr int n = 1024;
    for (const int k : {1, 7, 64, 300})
    {
        std::vector<double> signal(n);
        for (int i = 0; i < n; ++i)
            signal[static_cast<std::size_t>(i)] = std::sin(2.0 * M_PI * k * i / n);

        const auto spectrum = dsp::forwardFft(signal);
        REQUIRE(std::abs(spectrum[static_cast<std::size_t>(k)])
                == Approx(n / 2.0).epsilon(1.0e-9));
        REQUIRE(std::abs(spectrum[static_cast<std::size_t>(n - k)])
                == Approx(n / 2.0).epsilon(1.0e-9));
    }
}

TEST_CASE("the transform runs in the right direction", "[spectrum]")
{
    // THE check a magnitude-only test cannot make. Flipping the sign of the
    // twiddle angle conjugates the spectrum and leaves every magnitude
    // untouched, so a suite that only compares magnitudes passes with the
    // transform running backwards. A sibling project shipped exactly that.
    //
    // A cosine minus a sine has a known complex value at its bin, not merely a
    // known magnitude.
    constexpr int n = 256;
    constexpr int k = 5;
    std::vector<double> signal(n);
    for (int i = 0; i < n; ++i)
        signal[static_cast<std::size_t>(i)] = std::cos(2.0 * M_PI * k * i / n);

    const auto spectrum = dsp::forwardFft(signal);

    // For a real cosine the bin is real and positive with this sign convention;
    // the imaginary part must be zero rather than merely small in magnitude.
    REQUIRE(spectrum[k].real() == Approx(n / 2.0).epsilon(1.0e-9));
    REQUIRE(spectrum[k].imag() == Approx(0.0).margin(1.0e-9));

    // And a sine is purely negative-imaginary, which distinguishes the two
    // directions.
    std::vector<double> sine(n);
    for (int i = 0; i < n; ++i)
        sine[static_cast<std::size_t>(i)] = std::sin(2.0 * M_PI * k * i / n);
    const auto sineSpectrum = dsp::forwardFft(sine);
    REQUIRE(sineSpectrum[k].imag() < -1.0);
}

TEST_CASE("the transform agrees with a naive DFT", "[spectrum]")
{
    // An independent algorithm sharing none of the fast transform's machinery.
    constexpr int n = 128;
    std::vector<double> signal(n);
    for (int i = 0; i < n; ++i)
        signal[static_cast<std::size_t>(i)] =
            std::sin(2.0 * M_PI * 11.0 * i / n) + 0.4 * std::cos(2.0 * M_PI * 37.0 * i / n);

    const auto fast = dsp::forwardFft(signal);

    double worst = 0.0;
    for (int k = 0; k < n; ++k)
    {
        std::complex<double> sum{};
        for (int i = 0; i < n; ++i)
        {
            const double angle = -2.0 * M_PI * k * i / n;
            sum += signal[static_cast<std::size_t>(i)]
                 * std::complex<double>(std::cos(angle), std::sin(angle));
        }
        worst = std::max(worst, std::abs(fast[static_cast<std::size_t>(k)] - sum));
    }
    REQUIRE(worst < 1.0e-10);
}

TEST_CASE("a pure sine has no non-harmonic energy", "[spectrum][aliasing]")
{
    // The aliasing instrument. A clean tone must read as clean, or every number
    // the bench prints is offset by whatever this reads on silence.
    constexpr double sampleRate = 48000.0;
    constexpr double hz = 1000.0;
    constexpr int n = 16384;

    std::vector<double> signal(n);
    for (int i = 0; i < n; ++i)
        signal[static_cast<std::size_t>(i)] = std::sin(2.0 * M_PI * hz * i / sampleRate);

    // MEASURED FLOOR: -88.6 dB, which is the window's own sidelobe level and
    // not a property of the signal. Asserted just below it rather than at some
    // rounder number, so that a change of window shows up here as a moved floor
    // instead of passing silently and shifting every bench reading.
    //
    // dsp::kNonHarmonicFloorDb carries the same number for the bench to print,
    // because a reading of -85 dB means "at the floor", not "very clean".
    const double reading = dsp::nonHarmonicEnergyDb(signal, hz, sampleRate);
    INFO("reading " << reading << " dB, floor " << dsp::kNonHarmonicFloorDb);
    REQUIRE(reading < -85.0);
    REQUIRE(reading == Approx(dsp::kNonHarmonicFloorDb).margin(4.0));
}

TEST_CASE("harmonic distortion is not counted as aliasing", "[spectrum][aliasing]")
{
    // Saturation legitimately makes harmonics; that is the product, not a
    // defect. The instrument must ignore them or it will report a well-behaved
    // tape as badly aliased and the oversampling choice will be made on noise.
    constexpr double sampleRate = 96000.0;
    constexpr double hz = 1000.0;
    constexpr int n = 16384;

    std::vector<double> signal(n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sampleRate;
        signal[static_cast<std::size_t>(i)] = std::tanh(3.0 * std::sin(2.0 * M_PI * hz * t));
    }

    REQUIRE(dsp::nonHarmonicEnergyDb(signal, hz, sampleRate) < -80.0);
}

TEST_CASE("a deliberately aliased tone is detected", "[spectrum][aliasing]")
{
    // Teeth: an inharmonic partial must register, or the instrument reads
    // everything as clean and is useless.
    constexpr double sampleRate = 48000.0;
    constexpr double hz = 1000.0;
    constexpr int n = 16384;

    std::vector<double> signal(n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sampleRate;
        signal[static_cast<std::size_t>(i)] =
            std::sin(2.0 * M_PI * hz * t) + 0.01 * std::sin(2.0 * M_PI * 3456.0 * t);
    }

    const double reading = dsp::nonHarmonicEnergyDb(signal, hz, sampleRate);
    INFO("reading " << reading << " dB");
    REQUIRE(reading > -45.0);
    REQUIRE(reading < -35.0);
}
