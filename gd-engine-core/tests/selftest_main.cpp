// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "selftest.h"

namespace gd::core::selftest {

namespace {
std::vector<std::pair<std::string, TestFn>>& registry() {
    static std::vector<std::pair<std::string, TestFn>> r;
    return r;
}
}  // namespace

Register::Register(const char* name, TestFn fn) { registry().emplace_back(name, fn); }

void Context::check(bool ok, const char* expr, const char* file, int line, const std::string& msg) {
    if (ok) return;
    ++failures;
    std::printf("    FAIL %s:%d: %s%s%s\n", file, line, expr, msg.empty() ? "" : " -- ", msg.c_str());
}

void Context::note(const std::string& line) { std::printf("    %s\n", line.c_str()); }

}  // namespace gd::core::selftest

int main(int argc, char** argv) {
    using namespace gd::core::selftest;
    std::vector<std::string> filters, extra;
    bool list = false, afterDashes = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (afterDashes) extra.push_back(a);
        else if (a == "--") afterDashes = true;
        else if (a == "--list") list = true;
        else filters.push_back(a);
    }
    auto& tests = registry();
    std::sort(tests.begin(), tests.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (list) {
        for (const auto& t : tests) std::printf("%s\n", t.first.c_str());
        return 0;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path scratch = fs::temp_directory_path(ec) / ("gdcore_selftest_" + std::to_string(SDL_GetTicksNS()));
    fs::create_directories(scratch, ec);
    if (ec) {
        std::printf("gdcore_selftest: cannot make a scratch folder: %s\n", ec.message().c_str());
        return 2;
    }

    int run = 0, failed = 0;
    for (const auto& [name, fn] : tests) {
        if (!filters.empty() && std::none_of(filters.begin(), filters.end(),
                                             [&](const std::string& f) { return name.find(f) != std::string::npos; }))
            continue;
        const fs::path dir = scratch / name;
        fs::create_directories(dir, ec);
        Context ctx;
        ctx.scratch = dir.string() + "/";
        ctx.args = extra;
        std::printf("[ RUN  ] %s\n", name.c_str());
        std::fflush(stdout);
        fn(ctx);
        ++run;
        if (ctx.failures) ++failed;
        std::printf("[ %s ] %s\n", ctx.failures ? "FAIL" : " OK ", name.c_str());
        std::fflush(stdout);
    }
    fs::remove_all(scratch, ec);
    std::printf("gdcore_selftest: %d test(s), %d failed\n", run, failed);
    SDL_Quit();
    return failed == 0 && run > 0 ? 0 : 1;
}
