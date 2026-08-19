# chalkwalk-dsp

Audio DSP primitives. JUCE-free, dependency-free, header-only, MIT.

Four things, chosen because they are small enough to test exhaustively and
because a specification exists that you could fail to meet. Anything with a
real specification — loudness to ITU-R BS.1770, resampling, FFT — should be a
dependency instead, and is.

| | |
|---|---|
| `Svf.h` | Cytomic state-variable filter: LP/HP/BP/notch from one pole pair |
| `PolyBlep.h` | Band-limited step correction for saw and pulse oscillators |
| `SoftClip.h` | Soft-knee ceiling that is *exactly* unity below the knee |
| `Interpolation.h` | 4-point Hermite, for fractional buffer reads |
| `Denormal.h` | State flushing, so an idle filter does not get slower |

## Why this exists

These were written twice, in two plugins, and the copies disagreed in ways
that mattered:

- **The polyBLEP sign.** One project subtracted the correction and one added
  it. An inverted correction *reinforces* the discontinuity instead of
  cancelling it, so it is not merely worse — it is worse than doing nothing.
  It shipped for years behind a test that passed *because* of the bug. The
  tests here measure aliasing against non-harmonic spectral energy rather than
  asserting anything about shape, because shape assertions are what let it
  survive.

- **The pulse width limit.** A pulse has two discontinuities, and each
  correction spans one sample either side of its own; closer together than
  that and they overlap. One project clamped width to `[0.05, 0.95]` and the
  other did not clamp at all. Measured, the fixed clamp is wrong at both ends
  — at 110 Hz it forbids clean narrow pulses, and at 3520 Hz it is two thirds
  of a phase increment, so it fails to protect in exactly the case it exists
  for. The limit is a multiple of the increment.

- **Denormal flushing.** One had it; the other has a filter on every track and
  did not. A recursive filter fed silence decays into denormal range, where the
  arithmetic costs tens to hundreds of times more — so the dropout happens on
  an *idle* track, which is not where anyone looks.

- **Filter guards.** `tan()` runs away at Nyquist; a cutoff of zero makes a
  lowpass pass everything, which is a "closed" filter that is really a wire.

Each merge takes both halves rather than picking a winner.

## Use

Header-only. Add the include directory, or as a CMake subdirectory:

```cmake
add_subdirectory(libs/dsp)
target_link_libraries(your_target PRIVATE chalkwalk::dsp)
```

```cpp
#include <chalkwalk/dsp/Svf.h>

chalkwalk::dsp::Svf filter;
filter.set(1000.0, 0.707, 48000.0);          // Hz, Q, sample rate
const float out = filter.process(in, chalkwalk::dsp::Svf::LowPass);
```

`setCoeffs(g, k)` is there too, for callers that already smooth their own
coefficients per sample and do not want `tan()` called again inside the filter.

## Build and test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

Standalone with nothing else on the machine — that is the test of the
boundary, not a convenience. A library that only builds inside its parent has
not been extracted.

## Licence

MIT. See [LICENSE](LICENSE).

Part of the [chalkwalk](https://github.com/chalkwalk) plugin ecosystem,
alongside [chalkwalk-music](https://github.com/chalkwalk/chalkwalk-music).
