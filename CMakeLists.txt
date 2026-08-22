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

target_compile_features(chalkwalk_dsp INTERFACE cxx_std_20)

# ---------------------------------------------------------------------------
# chalkwalk::dsp::measure -- peak, rms, crest, brightness, pitch and loudness.
#
# A SEPARATE TARGET, because it is the one part of this library with a
# dependency. `chalkwalk::dsp` stays header-only and dependency-free, so a
# consumer that wants a filter never links a loudness meter; a consumer that
# wants a number opts in by naming this target instead.
#
# It is a target rather than a suggestion that everyone write their own
# because the alternative was measured: peak and rms existed three times over
# across these repositories, and `fundamentalHz` twice -- two independent pitch
# detectors, one of which reported 294.7 Hz for a 440 Hz tone and drove a fix
# all the way through before anyone suspected the instrument. A detector used
# only by the suite that defines it is a detector nobody has calibrated.
# ---------------------------------------------------------------------------
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/modules/libebur128/ebur128/ebur128.c)
    message(FATAL_ERROR
        "modules/libebur128 is empty. Run: git submodule update --init --recursive")
endif()

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

# Tests only when built on its own, or when a parent explicitly asks. A parent
# that pulls this in for the headers should not have to build Catch2.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CHALKWALK_DSP_TESTS_DEFAULT ON)
else()
    set(CHALKWALK_DSP_TESTS_DEFAULT OFF)
endif()
option(CHALKWALK_DSP_TESTS "Build chalkwalk-dsp tests"
       ${CHALKWALK_DSP_TESTS_DEFAULT})

if(CHALKWALK_DSP_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
