#pragma once

// pi, once, for the headers in `chalkwalk::dsp` that need it.
//
// NOT `M_PI`. That is POSIX rather than standard C++, and MSVC defines it only
// when `_USE_MATH_DEFINES` precedes the first `<cmath>` -- which no header can
// rely on a consumer having done. Four headers here used it, and every Windows
// build failed on them. `Spectrum.h` already said so, and kept its own copy.
//
// Not `std::numbers::pi` either: this library's floor is C++17.
//
// One definition rather than one per header, because two headers in the same
// namespace each defining `kPi` is a redefinition in any file that includes
// both. Nested namespaces that already have their own (`spectrum`, `measure`)
// keep them; the value is the same and the inner one is found first.

namespace chalkwalk::dsp
{
    inline constexpr double kPi = 3.14159265358979323846;
}
