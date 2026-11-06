// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// gdcore_selftest: one headless program for every area's checks. An area adds
// its test files with gdcore_add_selftest(SOURCES ...) in its CMakeLists.txt
// and writes:
//
//   #include "selftest.h"
//   GDCORE_TEST(image_png_roundtrip) {
//       GDCORE_CHECK(1 + 1 == 2);
//       GDCORE_CHECK_MSG(ok, "decode: " + error);
//   }
//
// Usage: gdcore_selftest [--list] [name-substring ...] [-- <args for the tests>]
// Tests never use game data; they make what they need at run time in
// ctx.scratch (a fresh temporary folder, removed afterwards). Tests may read
// optional extra files given after "--" (ctx.args), e.g. a disc image, and
// must pass without them.
#pragma once

#include <string>
#include <vector>

namespace gd::core::selftest {

struct Context {
    std::string scratch;            // a fresh folder for this run, with a trailing '/'
    std::vector<std::string> args;  // what followed "--" on the command line
    int failures = 0;
    void check(bool ok, const char* expr, const char* file, int line, const std::string& msg = {});
    void note(const std::string& line);  // printed under the test's name
};

using TestFn = void (*)(Context&);
struct Register {
    Register(const char* name, TestFn fn);
};

}  // namespace gd::core::selftest

#define GDCORE_TEST(name)                                                         \
    static void gdcore_test_##name(::gd::core::selftest::Context& ctx);           \
    static const ::gd::core::selftest::Register gdcore_test_reg_##name(#name, &gdcore_test_##name); \
    static void gdcore_test_##name([[maybe_unused]] ::gd::core::selftest::Context& ctx)

#define GDCORE_CHECK(cond) ctx.check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define GDCORE_CHECK_MSG(cond, msg) ctx.check(static_cast<bool>(cond), #cond, __FILE__, __LINE__, (msg))
