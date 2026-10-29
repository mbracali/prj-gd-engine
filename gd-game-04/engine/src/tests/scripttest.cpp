// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/scripttest.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <vector>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qb.h"
#include "data/qbkey.h"
#include "script/bytecode.h"
#include "script/globals.h"
#include "script/natives_core.h"
#include "script/vm.h"

int runScriptTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::fprintf(stderr, "scripttest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    int failures = 0;
    // 1. Every script of every pak tokenizes.
    size_t scripts = 0, tokens = 0;
    for (const std::string& path : disc->list()) {
        if (path.size() < 8 || path.compare(path.size() - 8, 8, ".pak.xen") != 0) continue;
        try {
            Pak pak = Pak::load(*disc, path);
            for (const PakEntry& e : pak.entries()) {
                if (e.type != qbkey::key(".qb")) continue;
                auto span = pak.data(e);
                qb::File f = qb::decode(span.first, span.second);
                for (const qb::Section& s : f.sections) {
                    if (s.value.type != qb::Type::Script) continue;
                    ++scripts;
                    try {
                        tokens += qs::tokenize(s.value.script->code.data(), s.value.script->code.size()).size();
                    } catch (const std::exception& ex) {
                        if (++failures <= 20) std::printf("FAIL %s %s: %s\n", path.c_str(), qbkey::name(s.name).c_str(), ex.what());
                    }
                }
            }
        } catch (const std::exception& ex) {
            if (++failures <= 20) std::printf("FAIL %s: %s\n", path.c_str(), ex.what());
        }
    }
    std::printf("scripts: %zu tokenized, %zu tokens\n", scripts, tokens);

    // 2. The boot script against the natives there are.
    Globals g;
    g.load(*disc);
    qs::Vm vm(g);
    qs::addCoreNatives(vm);
    if (!vm.run(qbkey::key("guitar_startup"), qb::Struct{})) {
        std::printf("FAIL guitar_startup is not a script\n");
        ++failures;
    }
    for (int frame = 0; frame < 600; ++frame) vm.update(1000.0 / 60.0);
    const qs::VmStats& st = vm.stats();
    std::printf("boot: %llu lines, %llu calls (%llu native, %llu script), %zu scripts still running\n",
                static_cast<unsigned long long>(st.lines), static_cast<unsigned long long>(st.calls),
                static_cast<unsigned long long>(st.nativeCalls), static_cast<unsigned long long>(st.scriptCalls), vm.runs().size());
    for (auto& r : vm.runs()) {
        std::printf("still running: %s", qbkey::name(r->name).c_str());
        for (auto& f : r->frames) std::printf(" > %s@%zu", qbkey::name(f.code->name).c_str(), f.pc);
        std::printf(" (wait frames %d, until %.0f ms)\n", r->waitFrames, r->waitUntilMs);
    }
    std::vector<std::string> names;
    for (uint32_t k : vm.unhandled()) names.push_back(qbkey::name(k));
    std::sort(names.begin(), names.end());
    std::printf("unhandled calls (%zu):", names.size());
    for (const std::string& n : names) std::printf(" %s", n.c_str());
    std::printf("\n");
    if (failures) {
        std::printf("scripttest: %d FAILED\n", failures);
        return 1;
    }
    std::printf("scripttest: every script tokenizes; boot ran (%zu natives to write)\n", names.size());
    return 0;
}
