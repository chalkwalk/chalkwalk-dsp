# chalkwalk-dsp -- audio DSP primitives and measurement, JUCE-free.
#
# Written to work BOTH nested inside a parent project and standalone, so that
# `cmake -B build && ctest` works from this directory with nothing else on the
# machine. That is not a convenience: a library that only builds inside its
# parent has not actually been extracted, and the standalone build is the test
# of the boundary.

cmake_minimum_required(VERSION 3.22)
project(chalkwalk_dsp VERSION 0.1.0 LANGUAGES CXX)

# INTERFACE while everything here is header-only. This becomes a STATIC library
# the moment the first .cpp arrives; nothing about the consumer-facing usage
# (`chalkwalk::dsp`) changes when it does.
add_library(chalkwalk_dsp INTERFACE)
add_library(chalkwalk::dsp ALIAS chalkwalk_dsp)

target_include_directories(chalkwalk_dsp
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(chalkwalk_dsp INTERFACE cxx_std_17)

# ---------------------------------------------------------------------------
# chalkwalk::dsp::measure -- loudness, and only loudness.
#
# A SEPARATE TARGET, because it is the one part of this library with a
# dependency. `chalkwalk::dsp` stays header-only and dependency-free, so a
# consumer that wants a filter never links a loudness meter; a consumer that
# wants BS.1770 opts in by naming this target instead.
#
# The instruments themselves -- peak, rms, crest, brightness, pitch -- are in
# `Measure.h` and need only `chalkwalk::dsp`. They used to live behind this
# target too, because `<ebur128.h>` sat at the top of the header they share, so
# asking "is this signal silent or clipping" cost a loudness meter. Splitting
# `Loudness.h` out is what makes the cheap half reachable from a library that
# has no business linking libebur128.
#
# It is a target rather than a suggestion that everyone write their own
# because the alternative was measured: peak and rms existed three times over
# across these repositories, and `fundamentalHz` twice -- two independent pitch
# detectors, one of which reported 294.7 Hz for a 440 Hz tone and drove a fix
# all the way through before anyone suspected the instrument. A detector used
# only by the suite that defines it is a detector nobody has calibrated.
# ---------------------------------------------------------------------------
# ---- AND THE SPLIT HAS TO BE REAL AT CONFIGURE TIME, NOT ONLY AT LINK TIME ----
#
# This block used to be an unconditional FATAL_ERROR, which made the split above
# a fiction: `chalkwalk::dsp` is header-only and dependency-free, but you could
# not CONFIGURE it without libebur128 checked out. A consumer that wanted a
# filter still paid for the loudness meter -- in clone bytes, in a submodule it
# had no reason to initialise, and in a configure that failed outright telling
# it to fetch something it would never compile.
#
# Found when chalkwalk-tape took this library as a dependency for four DSP
# primitives and could not configure, because `git submodule add` does not
# recurse and the nested libebur128 was empty. The dependency the split exists
# to contain had escaped through the build system.
#
# So: present means measure is built, absent means it is not, and a consumer
# that asked for `chalkwalk::dsp::measure` gets told exactly that and how to fix
# it. The standalone build initialises submodules recursively and is therefore
# always the complete library, which is what its own suite tests.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/libebur128/ebur128/ebur128.c")
    set(CHALKWALK_DSP_MEASURE_AVAILABLE ON)
else()
    set(CHALKWALK_DSP_MEASURE_AVAILABLE OFF)
    message(STATUS
        "chalkwalk-dsp: modules/libebur128 is not checked out, so "
        "chalkwalk::dsp::measure is NOT defined. The header-only "
        "chalkwalk::dsp is unaffected. If something needs BS.1770 loudness: "
        "git submodule update --init --recursive")
endif()

if(CHALKWALK_DSP_MEASURE_AVAILABLE)

# libebur128 (MIT) -- ITU-R BS.1770 loudness.
#
# Adopted rather than maintained, on this library's standing rule: take the
# dependency when the thing has a SPECIFICATION you could fail to meet. Our own
# K-weighting and gating agreed with ffmpeg to inside 0.05 LU, so this is not a
# bug fix -- it is refusing to own a standard whose next revision, or whose
# short-term and range measures, we would have to track by hand.
#
# The target is declared here rather than by add_subdirectory: the vendored
# CMakeLists declares cmake_minimum_required(VERSION 2.8.12), which CMake 4
# refuses outright, and it also builds a shared library, tests and pkg-config
# files that a static consumer has no use for. The library itself is one C
# file.
#
# The bundled queue/ is used on every platform rather than only where
# sys/queue.h is missing, which is what upstream's try_compile decides. One
# copy on all three platforms is one behaviour to reason about, and Windows
# has no system sys/queue.h at all -- so the conditional could only ever
# produce a difference nobody wanted.
#
# Guarded: a parent that already vendors libebur128 keeps its own target rather
# than getting a second, colliding one.
if(NOT TARGET ebur128)
    enable_language(C)
    add_library(ebur128 STATIC modules/libebur128/ebur128/ebur128.c)
    target_include_directories(ebur128
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/libebur128/ebur128
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/libebur128/ebur128/queue)
    set_target_properties(ebur128 PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_definitions(ebur128 PRIVATE _USE_MATH_DEFINES)
    endif()
    find_library(MATH_LIBRARY m)
    if(MATH_LIBRARY)
        target_link_libraries(ebur128 PRIVATE ${MATH_LIBRARY})
    endif()
endif()

add_library(chalkwalk_dsp_measure INTERFACE)
add_library(chalkwalk::dsp::measure ALIAS chalkwalk_dsp_measure)
target_link_libraries(chalkwalk_dsp_measure INTERFACE chalkwalk::dsp ebur128)
endif()

# Tests only when built on its own, or when a parent explicitly asks. A parent
# that pulls this in for the headers should not have to build Catch2.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CHALKWALK_DSP_TESTS_DEFAULT ON)
else()
    set(CHALKWALK_DSP_TESTS_DEFAULT OFF)
endif()
option(CHALKWALK_DSP_TESTS "Build chalkwalk-dsp tests"
       ${CHALKWALK_DSP_TESTS_DEFAULT})

# The suite exercises `measure`, so it needs libebur128. A tree without it is a
# partial checkout rather than a configuration, and silently running a subset
# would be the worse outcome -- a green suite that did not test the half with a
# dependency.
if(CHALKWALK_DSP_TESTS AND NOT CHALKWALK_DSP_MEASURE_AVAILABLE)
    message(FATAL_ERROR
        "chalkwalk-dsp tests were requested but modules/libebur128 is not "
        "checked out, so half the library cannot be built. Run: "
        "git submodule update --init --recursive")
endif()

if(CHALKWALK_DSP_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
