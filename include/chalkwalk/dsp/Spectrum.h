// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

// Spectral measurement: a transform, a window, and the two instruments built
// from them.
//
// ---- WHY AN FFT IS HERE AT ALL, GIVEN THE STANDING RULE ----
//
// This ecosystem adopts rather than writes anything with a specification it
// could fail to meet -- that is why libebur128 supplies BS.1770 and signalsmith
// supplies the Kaiser window. An FFT has a specification, a correctness proof
// and a hundred person-years of optimisation behind it, so a library needing a
// transform in its SIGNAL PATH should take PFFFT or KISS and not this.
//
// Forty lines of radix-2 used to MEASURE is the other half of the same rule:
// small enough to test exhaustively, and tested here against a naive DFT, which
// shares none of its machinery. The question was put again -- should this be
// PFFFT? -- and answered by measurement rather than by the rule. This transform
// agrees with a naive DFT to 2.1e-12 of the spectral peak at the sizes these
// instruments use, while the assertions they support live at 40 to 70 dB, which
// is 1e-2 to 3e-4. Nine orders of margin: there is no accuracy to buy, and the
// form of PFFFT everyone vendors is single-precision.
//
// ---- IT IS HERE BECAUSE IT WAS IN TWO PLACES ----
//
// Remanence had this and chalkwalk-tape's suite had it, and they were the same
// transform line for line -- identical bit-reversal, identical butterfly, the
// same -2*pi/len, and the same six-bin exclusion skirt derived the same way
// from the same window. Two repositories, two suites, one algorithm, and
// nothing keeping the two honest about each other.
//
// ---- THE WINDOW IS THE INSTRUMENT'S NOISE FLOOR ----
//
// Easy to get wrong and expensive when it is. A Hann window's first sidelobe is
// at only -31 dB, so a tone that does not sit exactly on a bin -- the normal
// case -- leaks about -40 dB of itself into every other bin. Measured with
// Hann, a PURE SINE reads as 40 dB of aliasing, and every number a bench
// printed would have been that floor rather than the thing under test.
//
// A four-term Blackman-Harris has sidelobes at -92 dB, below anything worth
// measuring. It costs a wider main lobe -- eight bins rather than four -- which
// is where `kSpectralSkirt` comes from.
//
// FOR MEASUREMENT, NOT FOR THE AUDIO PATH: it allocates.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace chalkwalk::dsp::spectrum
{
    // Not M_PI. That is POSIX rather than standard C++ and is absent on MSVC
    // unless _USE_MATH_DEFINES is defined before <cmath> -- which no header can
    // rely on a consumer having done. Both copies this file replaces used it,
    // and both would have failed on the first Windows build.
    inline constexpr double kPi = 3.14159265358979323846;

    // Bins an exclusion band must span to clear a windowed peak's main lobe.
    // Blackman-Harris spreads about four bins either side; six leaves room for
    // a tone sitting between bins.
    inline constexpr int kSpectralSkirt = 6;

    // In-place iterative radix-2 Cooley-Tukey. `v.size()` must be a power of two.
    inline void fft(std::vector<std::complex<double>>& v) noexcept
    {
        const std::size_t n = v.size();
        for (std::size_t i = 1, j = 0; i < n; ++i)
        {
            std::size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(v[i], v[j]);
        }
        for (std::size_t len = 2; len <= n; len <<= 1)
        {
            // Negative angle: the FORWARD transform. Flipping this sign
            // conjugates the spectrum and leaves every magnitude untouched, so
            // a magnitude-only suite passes with the transform running
            // backwards. The tests pin it by checking a cosine's bin is real
            // and positive and a sine's is negative-imaginary.
            const double angle = -2.0 * kPi / static_cast<double>(len);
            const std::complex<double> step(std::cos(angle), std::sin(angle));
            for (std::size_t i = 0; i < n; i += len)
            {
                std::complex<double> w(1.0, 0.0);
                for (std::size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = v[i + k];
                    const auto t = v[i + k + len / 2] * w;
                    v[i + k] = u + t;
                    v[i + k + len / 2] = u - t;
                    w *= step;
                }
            }
        }
    }

    // One four-term Blackman-Harris coefficient at `i` of `n`.
    [[nodiscard]] inline double blackmanHarris(std::size_t i, double denom) noexcept
    {
        const double t = 2.0 * kPi * static_cast<double>(i) / denom;
        return 0.35875 - 0.48829 * std::cos(t)
             + 0.14128 * std::cos(2.0 * t)
             - 0.01168 * std::cos(3.0 * t);
    }

    // Forward transform of real input, TRUNCATED to the largest power of two it
    // contains rather than zero-padded. Padding a measurement changes its
    // spectrum, and a bench that padded would report a resolution it does not
    // have. Returns empty for anything shorter than two samples.
    [[nodiscard]] inline std::vector<std::complex<double>> forwardFft(
        const std::vector<double>& input)
    {
        std::size_t n = 1;
        while (n * 2 <= input.size())
            n *= 2;
        if (n < 2)
            return {};

        std::vector<std::complex<double>> data(n);
        for (std::size_t i = 0; i < n; ++i)
            data[i] = input[i];
        fft(data);
        return data;
    }

    // Magnitude spectrum, Blackman-Harris windowed and zero-padded to
    // 2^fftOrder. Padding IS right here: this is a peak-finding instrument
    // rather than an energy one, and the interpolation a longer transform buys
    // is what puts a between-bin tone where it belongs.
    [[nodiscard]] inline std::vector<double> magnitudes(const std::vector<float>& x,
                                                        int fftOrder = 15)
    {
        const std::size_t fftSize = std::size_t{ 1 } << fftOrder;
        std::vector<std::complex<double>> buf(fftSize, { 0.0, 0.0 });

        const std::size_t n = std::min(fftSize, x.size());
        const double denom = n > 1 ? static_cast<double>(n - 1) : 1.0;
        for (std::size_t i = 0; i < n; ++i)
            buf[i] = { static_cast<double>(x[i]) * blackmanHarris(i, denom), 0.0 };
        fft(buf);

        std::vector<double> mags(fftSize / 2);
        for (std::size_t i = 0; i < fftSize / 2; ++i)
            mags[i] = std::abs(buf[i]);
        return mags;
    }

    // Energy at the fundamental relative to everything else, in dB. Higher is
    // purer: a clean sine reads well above 70; imaging or aliasing pulls it
    // down. The interpolation instrument.
    [[nodiscard]] inline float sinePurityDb(const std::vector<float>& x, double f,
                                            double sr, int fftOrder = 15)
    {
        const auto mags = magnitudes(x, fftOrder);
        const double binHz = sr / static_cast<double>(std::size_t{ 1 } << fftOrder);
        const int fundBin = static_cast<int>(std::lround(f / binHz));

        double fund = 0.0, rest = 0.0;
        for (int i = 1; i < static_cast<int>(mags.size()); ++i)
        {
            const double m = mags[static_cast<std::size_t>(i)];
            const double e = m * m;
            if (std::abs(i - fundBin) <= kSpectralSkirt)
                fund += e;
            else
                rest += e;
        }
        if (rest < 1e-20)
            return 120.0f;
        return static_cast<float>(10.0 * std::log10(fund / rest));
    }

    inline constexpr double kNonHarmonicFloorDb = -88.6;

    // Energy that is NOT at a harmonic of `fundamentalHz`, in dB relative to
    // the total. The ALIASING instrument.
    //
    // Harmonics are excluded deliberately and it is the whole point:
    // saturation legitimately generates them, and that is the product rather
    // than a defect. An instrument counting them would report a well-behaved
    // saturator as badly aliased, and an oversampling rate chosen from that
    // number would be chosen from noise.
    [[nodiscard]] inline double nonHarmonicEnergyDb(const std::vector<double>& signal,
                                                    double fundamentalHz,
                                                    double sampleRate)
    {
        std::size_t n = 1;
        while (n * 2 <= signal.size())
            n *= 2;
        if (n < 64 || fundamentalHz <= 0.0 || sampleRate <= 0.0)
            return 0.0;

        std::vector<double> windowed(n);
        for (std::size_t i = 0; i < n; ++i)
            windowed[i] = signal[i] * blackmanHarris(i, static_cast<double>(n));

        const auto sp = forwardFft(windowed);
        const double binHz = sampleRate / static_cast<double>(n);

        double harmonic = 0.0;
        double other = 0.0;
        for (std::size_t k = 1; k < n / 2; ++k)
        {
            const double power = std::norm(sp[k]);
            const double hz = static_cast<double>(k) * binHz;
            const double nearest = std::round(hz / fundamentalHz);
            const bool isHarmonic =
                nearest >= 1.0
                && std::abs(hz - nearest * fundamentalHz) <= kSpectralSkirt * binHz;

            if (isHarmonic)
                harmonic += power;
            else
                other += power;
        }

        const double total = harmonic + other;
        if (total <= 0.0 || other <= 0.0)
            return -400.0;
        return 10.0 * std::log10(other / total);
    }
}
