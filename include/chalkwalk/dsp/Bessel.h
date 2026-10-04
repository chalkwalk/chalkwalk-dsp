#pragma once

// Bessel functions of order zero: I0 for the Kaiser window, J0 for a thin-gap
// head's response.
//
// ---- WHY NOT `std::cyl_bessel_i` AND `std::cyl_bessel_j` ----
//
// They are C++17, and Apple's standard library does not ship them -- libc++ has
// never implemented the special mathematical functions. Both were used here and
// in chalkwalk-tape, and both libraries compiled on Linux and failed on macOS
// from the moment they were promoted, which is how a header-only library finds
// out what "portable" means. Two short functions written from their definitions
// cost less than a platform split, and they are the same on every platform.
//
// ---- I0: THE POWER SERIES (Abramowitz & Stegun 9.6.12) ----
//
//     I0(x) = sum_k ((x/2)^k / k!)^2
//
// Every term is positive, so there is no cancellation and the sum is accurate
// to a few ulp at any argument it can represent. The Kaiser window evaluates it
// at beta <= ~15 (beta = 0.1102 (A - 8.7) is 12.3 at 120 dB), where it takes
// about forty terms.
//
// ---- J0: BESSEL'S INTEGRAL, BY THE TRAPEZOID RULE (A&S 9.1.18) ----
//
//     J0(x) = (1/pi) integral_0^pi cos(x sin t) dt
//
// The integrand is smooth and periodic, which is the case where the trapezoid
// rule converges EXPONENTIALLY rather than as a power of the step (Trefethen and
// Weideman, "The exponentially convergent trapezoidal rule", SIAM Review 56(3),
// 2014). Here the error is not merely small but known exactly: the Jacobi-Anger
// expansion (A&S 9.1.42)
//
//     cos(x sin t) = J0(x) + 2 sum_k J_2k(x) cos(2k t)
//
// integrates term by term, and M equal panels over [0, pi] sum cos(2k t) to
// zero unless k is a multiple of M. So the rule returns
//
//     J0(x) + 2 J_2M(x) + 2 J_4M(x) + ...
//
// and J_n(x) collapses super-exponentially once n passes x. `panelsFor` takes
// 2M >= max(2|x|, |x| + 60), where the first neglected term is below 1e-17 --
// under double precision at every argument, without a table of coefficients and
// without the series' cancellation, which costs digits for x past about ten.
//
// The cost is O(|x|) cosines. Both callers are design-time -- a window, a
// response curve -- and neither is on the audio thread.
//
// JUCE-free, header-only, C++17.

#include <chalkwalk/dsp/Pi.h>

#include <cmath>

namespace chalkwalk::dsp
{
    [[nodiscard]] inline double besselI0(double x) noexcept
    {
        const double q = 0.25 * x * x;   // (x/2)^2
        double term = 1.0;
        double sum = 1.0;
        // The ratio of successive terms is q / k^2, so once k^2 exceeds q the
        // terms fall and the stop is reached in a handful more. The cap only
        // guards an argument too large to represent the result anyway.
        for (int k = 1; k < 1000; ++k)
        {
            term *= q / (static_cast<double>(k) * static_cast<double>(k));
            sum += term;
            if (term < sum * 1.0e-17)
                break;
        }
        return sum;
    }

    namespace bessel_detail
    {
        // M, the panel count, from 2M >= max(2|x|, |x| + 60). See the header.
        [[nodiscard]] inline int panelsFor(double ax) noexcept
        {
            const double twoM = std::fmax(2.0 * ax, ax + 60.0);
            return static_cast<int>(std::ceil(0.5 * twoM)) + 1;
        }
    }

    [[nodiscard]] inline double besselJ0(double x) noexcept
    {
        const double ax = std::fabs(x);
        const int m = bessel_detail::panelsFor(ax);
        const double h = kPi / static_cast<double>(m);

        // Endpoints at half weight. sin(0) = sin(pi) = 0, so both are cos(0).
        double sum = 1.0;
        for (int j = 1; j < m; ++j)
            sum += std::cos(ax * std::sin(h * static_cast<double>(j)));
        return sum / static_cast<double>(m);
    }
}
