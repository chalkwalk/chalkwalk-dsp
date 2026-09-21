// SPDX-License-Identifier: MIT
// Part of chalkwalk-dsp. See LICENSE.
#pragma once

// HOW THIS CATALOGUE RUNS OUT OF VOLTS, in one place.
//
// ---- WHY IT IS A SHARED FUNCTION AND NOT THREE COPIES ----
//
// One authority per quantity, and the SHAPE of an
// amplifier running out is a quantity. It had three copies: the deck's line
// amplifier (a deck's line amplifier), the desk's buses and input stages
// (a desk's buses), and -- by omission -- the monitor mixer, which had none at
// all and handed a sum straight to the converter. They were the same
// arithmetic, so this is not a behaviour change for the two that had it; it is
// the removal of a way for them to drift apart.
//
// ---- THE SHAPE, AND WHY NOT A TANH ----
//
// A sixth-order soft saturator: linear to a thousandth of a decibel at 15 dB
// below the ceiling, and rounding only in the last few. A real line amplifier
// is LINEAR until it runs out and then stops; it does not compress from silence
// upwards. A tanh starts bending immediately -- at 15 dB down it is already
// rounding by 0.3 % -- which made INPUT monitoring, which should be a wire,
// measurably not one.
//
// SOFT RATHER THAN HARD ON PURPOSE. A hard corner generates harmonics without
// limit, and these stages run at the session rate where there is no
// oversampling left to catch them: a rail here would alias, and the aliasing
// would be blamed on the hysteresis.
//
// IT ASYMPTOTES TO THE CEILING FROM BELOW and never reaches it, which is what
// makes it safe to assert against.
//
// DECLARED as a shape. No specification in `SOURCES` gives an amplifier's knee;
// what IS sourced is where each ceiling SITS -- +24 dB over operating for a
// line amplifier (`§27`, `§50`), +8 dB for a portastudio's PGM bus (`§44`).
//
// ---- AND THE DECK'S FOURTH COPY IS GONE TOO ----
//
// `TapeDeck.h`'s line amplifier had these six lines inline, with a comment
// explaining that sharing them would make the tape library depend on the dsp
// one. That objection rested on TapeDeck being bound for chalkwalk-tape. It is
// not: it is the assembled machine, it stays in the plugin that owns it, and a
// plugin may depend on whatever it likes. The reasoning was sound and its
// premise stopped being true.
//
// JUCE-free by design. Part of chalkwalk-dsp.

#include <cmath>

namespace chalkwalk::dsp
{
    [[nodiscard]] inline double softCeiling(double x, double ceiling) noexcept
    {
        const double t = x / ceiling;
        const double t2 = t * t;
        const double t6 = t2 * t2 * t2;
        return x / std::pow(1.0 + t6, 1.0 / 6.0);
    }
}
