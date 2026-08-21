// SPDX-License-Identifier: MIT
#pragma once

// juce::UnitTest's three verbs, on top of Catch2.
//
// These tests came from plugins that use juce::UnitTest, and a JUCE-free
// library cannot link it. The obvious port is to restructure each file into
// TEST_CASEs -- and restructuring is exactly where a port quietly drops an
// assertion, which for a wire protocol is the worst possible thing to drop
// silently.
//
// So the bodies are untouched and the verbs are shimmed instead. `beginTest`
// names the group in any failure message that follows it, which is what it did
// before; `expect` and `expectEquals` become Catch2 checks. The assertion count
// is therefore comparable across the move, and it was: 261 either side.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>

namespace shim {

// The name of the group currently running, so a failure says which one.
inline std::string currentGroup;

inline void setGroup(const std::string &name) { currentGroup = name; }

template <typename T>
std::string show(const T &v) {
  std::ostringstream os;
  os << v;
  return os.str();
}

// A base with the shape juce::UnitTest had, so a ported file's class
// declaration does not have to change either.
class UnitTest {
public:
  explicit UnitTest(std::string name, std::string = {}) : name_(std::move(name)) {}
  virtual ~UnitTest() = default;
  virtual void runTest() = 0;

protected:
  void beginTest(const std::string &n) { setGroup(name_ + " / " + n); }

private:
  std::string name_;
};

}  // namespace shim

// Argument-count dispatch: expect(cond) and expect(cond, msg) both exist in
// juce::UnitTest. This needs a CONFORMING preprocessor -- see the MSVC note in
// test/CMakeLists.txt, where it silently picked the wrong arm.
#define expect(...) SHIM_EXPECT_PICK(__VA_ARGS__, SHIM_EXPECT2, SHIM_EXPECT1)(__VA_ARGS__)
#define SHIM_EXPECT_PICK(_1, _2, NAME, ...) NAME
#define SHIM_EXPECT1(cond)                                                     \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    CHECK((cond));                                                             \
  } while (false)
#define SHIM_EXPECT2(cond, msg)                                                \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    INFO(msg);                                                                 \
    CHECK((cond));                                                             \
  } while (false)

#define expectEquals(...)                                                      \
  SHIM_EQ_PICK(__VA_ARGS__, SHIM_EQ3, SHIM_EQ2)(__VA_ARGS__)
#define SHIM_EQ_PICK(_1, _2, _3, NAME, ...) NAME
#define SHIM_EQ2(a, b)                                                         \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    CHECK((a) == (b));                                                         \
  } while (false)
#define SHIM_EQ3(a, b, msg)                                                    \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    INFO(msg);                                                                 \
    CHECK((a) == (b));                                                         \
  } while (false)

// juce::UnitTest's fourth argument is an optional failure message, and both
// arities appear in the ported suites.
#define expectWithinAbsoluteError(...)                                         \
  SHIM_NEAR_PICK(__VA_ARGS__, SHIM_NEAR4, SHIM_NEAR3)(__VA_ARGS__)
#define SHIM_NEAR_PICK(_1, _2, _3, _4, NAME, ...) NAME
#define SHIM_NEAR3(actual, expected, tolerance)                                \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    INFO("expected " << (expected) << " +/- " << (tolerance) << ", got "       \
                     << (actual));                                             \
    CHECK(std::abs((actual) - (expected)) <= (tolerance));                     \
  } while (false)
#define SHIM_NEAR4(actual, expected, tolerance, msg)                           \
  do {                                                                         \
    INFO(::shim::currentGroup);                                                \
    INFO(msg);                                                                 \
    INFO("expected " << (expected) << " +/- " << (tolerance) << ", got "       \
                     << (actual));                                             \
    CHECK(std::abs((actual) - (expected)) <= (tolerance));                     \
  } while (false)

namespace shim {

// juce::Random, as much of it as these tests use.
//
// Seeded and deterministic, which is the only property the tests depend on --
// a fuzz over random payloads has to replay identically when it finds
// something. std::mt19937 rather than JUCE's LCG, so the sequences differ from
// the originals; nothing here asserts a particular sequence.
class Random {
public:
  explicit Random(std::uint32_t seed) : engine_(seed) {}

  double nextDouble() {
    return std::uniform_real_distribution<double>(0.0, 1.0)(engine_);
  }
  int nextInt(int upperExclusive) {
    if (upperExclusive <= 0)
      return 0;
    return std::uniform_int_distribution<int>(0, upperExclusive - 1)(engine_);
  }

private:
  std::mt19937 engine_;
};

}  // namespace shim

namespace juce {

// juce::String, as much of it as a failure message needs.
//
// Ported suites build their messages by concatenation -- `"read " +
// juce::String(hz) + " Hz"` -- and there are hundreds of them. Shimming the
// type keeps those lines byte-identical, which is the same argument as the
// verbs above: the diff of a move should be the harness and nothing else.
//
// The two-argument form is juce's "this many decimal places". Default
// formatting differs from juce's in the last digit or two for some values;
// nothing asserts on a formatted string, only reads one after a failure.
class String {
public:
  String() = default;
  String(const char *s) : text_(s == nullptr ? "" : s) {}
  String(const std::string &s) : text_(s) {}

  template <typename T,
            typename = std::enable_if_t<std::is_arithmetic_v<T>>>
  explicit String(T v) {
    std::ostringstream os;
    os << v;
    text_ = os.str();
  }

  String(double v, int decimalPlaces) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(decimalPlaces) << v;
    text_ = os.str();
  }

  const std::string &toStdString() const { return text_; }

  String &operator+=(const String &o) {
    text_ += o.text_;
    return *this;
  }
  friend String operator+(String a, const String &b) { return a += b; }

  bool contains(const String &o) const {
    return text_.find(o.text_) != std::string::npos;
  }

private:
  std::string text_;
};

inline std::ostream &operator<<(std::ostream &os, const String &s) {
  return os << s.toStdString();
}

}  // namespace juce
