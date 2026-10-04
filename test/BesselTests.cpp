// I0 and J0, against three references that do not depend on each other.
//
// 1. PUBLISHED VALUES (Abramowitz & Stegun, Tables 9.1, 9.5 and 9.8), which run
//    everywhere -- including macOS, where there is no standard-library version
//    to compare with, and which is the platform these functions exist for.
// 2. THE STANDARD LIBRARY, where it has them (libstdc++ and MSVC). Swept densely,
//    because a table checks a handful of points and a sweep checks the shape.
// 3. THE LARGE-ARGUMENT ASYMPTOTIC FORM of J0 (A&S 9.2.5-9.2.10), the regime
//    the trapezoid's panel count exists for and which neither of the others
//    reaches.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chalkwalk/dsp/Bessel.h>

#include <cmath>

using Catch::Approx;
namespace dsp = chalkwalk::dsp;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
}

TEST_CASE("I0 matches the published table", "[bessel]")
{
    REQUIRE(dsp::besselI0(0.0) == 1.0);
    REQUIRE(dsp::besselI0(1.0) == Approx(1.266065877752008).epsilon(1e-14));
    REQUIRE(dsp::besselI0(2.0) == Approx(2.279585302336067).epsilon(1e-14));
    REQUIRE(dsp::besselI0(5.0) == Approx(27.23987182360445).epsilon(1e-14));
    REQUIRE(dsp::besselI0(10.0) == Approx(2815.716628466254).epsilon(1e-14));
    // Even, as every term is a square.
    REQUIRE(dsp::besselI0(-3.5) == dsp::besselI0(3.5));
}

TEST_CASE("J0 matches the published table, and its zeros are zeros", "[bessel]")
{
    REQUIRE(dsp::besselJ0(0.0) == 1.0);
    REQUIRE(dsp::besselJ0(1.0) == Approx(0.7651976865579666).epsilon(1e-14));
    REQUIRE(dsp::besselJ0(2.0) == Approx(0.2238907791412357).epsilon(1e-14));
    REQUIRE(dsp::besselJ0(5.0) == Approx(-0.1775967713143383).epsilon(1e-14));
    REQUIRE(dsp::besselJ0(10.0) == Approx(-0.2459357644513483).epsilon(1e-14));
    REQUIRE(dsp::besselJ0(-10.0) == dsp::besselJ0(10.0));

    // The first three zeros (A&S Table 9.5). An absolute bound, since a
    // relative one means nothing at zero.
    for (const double z : {2.404825557695773, 5.520078110286311, 8.653727912911012})
        REQUIRE(std::abs(dsp::besselJ0(z)) < 1e-14);
}

TEST_CASE("J0 holds its accuracy far past where a series would fail", "[bessel]")
{
    // A&S 9.2.5, with P and Q from 9.2.9 and 9.2.10 at order zero:
    //     J0(x) ~ sqrt(2/(pi x)) [P cos(chi) - Q sin(chi)],  chi = x - pi/4
    //     P = 1 - 9/(128 x^2),   Q = -1/(8x) + 75/(1024 x^3)
    // The first term left out is 11025/(98304 x^4) in P -- below 1e-12 at
    // x = 300 once scaled -- so 1e-10 is a bound the series can promise and a
    // short panel count cannot meet.
    //
    // Carried to this order because the first-order form is NOT enough: at
    // x = 300 its error is 3.6e-8, which is how the first version of this test
    // failed a correct function.
    for (const double x : {300.0, 1000.0, 2500.5})
    {
        const double chi = x - 0.25 * kPi;
        const double p = 1.0 - 9.0 / (128.0 * x * x);
        const double q = -1.0 / (8.0 * x) + 75.0 / (1024.0 * x * x * x);
        const double expected = std::sqrt(2.0 / (kPi * x))
                              * (p * std::cos(chi) - q * std::sin(chi));
        REQUIRE(std::abs(dsp::besselJ0(x) - expected) < 1e-10);
    }
}

#if defined(__cpp_lib_math_special_functions)
TEST_CASE("I0 and J0 agree with the standard library where it has them", "[bessel]")
{
    for (int i = 0; i <= 3000; ++i)
    {
        const double x = 0.01 * static_cast<double>(i);   // 0 .. 30
        REQUIRE(dsp::besselI0(x) == Approx(std::cyl_bessel_i(0.0, x)).epsilon(1e-14));
    }
    for (int i = 0; i <= 20000; ++i)
    {
        const double x = 0.01 * static_cast<double>(i);   // 0 .. 200
        REQUIRE(std::abs(dsp::besselJ0(x) - std::cyl_bessel_j(0.0, x)) < 1e-13);
    }
}
#endif
