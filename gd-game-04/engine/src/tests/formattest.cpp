// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/formattest.h"

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <exception>
#include <map>
#include <string>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qb.h"
#include "data/qbkey.h"
#include "tests/format_checks.h"

// --formattest <disc>: every archive on the disc and every file in them
// parses to its end (the M1 gate, kickoff section 6). Prints one line per
// format and "formattest: all files parse" when nothing failed.
int runFormatTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::fprintf(stderr, "formattest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    std::printf("formattest: %s, %zu names\n", discPath.c_str(), qbkey::names().size());

    int failures = 0;
    size_t paks = 0, files = 0;
    std::map<uint32_t, size_t> perType;
    FormatStats stats;
    for (const std::string& path : disc->list()) {
        if (path.size() < 8 || path.compare(path.size() - 8, 8, ".pak.xen") != 0) continue;
        ++paks;
        try {
            Pak pak = Pak::load(*disc, path);
            for (const PakEntry& e : pak.entries()) {
                ++files;
                ++perType[e.type];
                std::string what;
                if (!checkPakFile(pak, e, stats, what)) {
                    if (++failures <= 30)
                        std::printf("FAIL %s %s: %s\n", path.c_str(), qbkey::name(e.fullName).c_str(), what.c_str());
                }
            }
        } catch (const std::exception& ex) {
            if (++failures <= 30) std::printf("FAIL %s: %s\n", path.c_str(), ex.what());
        }
    }
    std::printf("paks: %zu, files: %zu\n", paks, files);
    for (auto& [type, n] : perType) std::printf("  %-8s %6zu\n", qbkey::name(type).c_str(), n);
    printFormatStats(stats);
    // Unresolved names the scripts use (keys with no name): a measure of the
    // debug table's coverage, not a failure.
    std::printf("qb: %zu files, %zu sections, %zu scripts, %zu unused bytes\n", stats.qbFiles, stats.qbSections,
                stats.qbScripts, stats.qbUnused);
    if (failures) {
        std::printf("formattest: %d FAILED\n", failures);
        return 1;
    }
    std::printf("formattest: all files parse\n");
    return 0;
}
