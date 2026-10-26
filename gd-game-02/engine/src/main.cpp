// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "app.h"
#include "app_ids.h"
#include "tests/selftest.h"

namespace {

void usage() {
    std::printf(
        "%s %s: Guitar Hero II (Xbox 360) reimplementation (first skeleton)\n"
        "usage: %s [options] <disc.iso | game folder>\n"
        "  --song <id>          play one song directly (e.g. yyz), then quit\n"
        "  --difficulty <0-3>   0 Easy, 1 Medium, 2 Hard, 3 Expert (default 3)\n"
        "  --autoplay           play the chart automatically\n"
        "  --no-fail            the rock meter cannot fail you\n"
        "  --selftest           check the data, auto-play every chart and check the rules headlessly\n"
        "Environment: GD02_KEYS=\"<song ms>:<key>,...\" scripted input (+green/-green .. orange, strum,\n"
        "  starpower, whammy=<0..1>).\n"
        "The game folder is the disc's files (gen/main.hdr, gen/main_0.ark, ...).\n",
        app::kProgram, app::kVersion, app::kProgram);
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--song") opt.song = value();
        else if (a == "--difficulty") opt.difficulty = std::atoi(value().c_str());
        else if (a == "--autoplay") opt.autoplay = true;
        else if (a == "--no-fail") opt.noFail = true;
        else if (a == "--selftest") selftest = true;
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-' && a.rfind("-psn_", 0) != 0) { usage(); return 2; }  // -psn_: macOS Finder
        else if (a.rfind("-psn_", 0) != 0) opt.dataPath = a;
    }
    if (opt.dataPath.empty()) {
        usage();
        return 2;
    }
    return selftest ? runSelfTest(opt.dataPath) : runApp(opt);
}
