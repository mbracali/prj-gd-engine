// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/scripttest.h"
#include <cctype>
#include "script/pc_strings.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <functional>
#include <vector>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qb.h"
#include "data/qbkey.h"
#include "script/pc_strings.h"
#include "script/bytecode.h"
#include "script/globals.h"
#include "script/natives_core.h"
#include "script/vm.h"

namespace {

// "Xbox" as a word (not xboxlive_ in a debug name), gamer profiles, storage
// devices, achievement unlocking.
bool consoleWording(const std::string& text) {
    std::string low = text;
    for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (size_t at = low.find("xbox"); at != std::string::npos; at = low.find("xbox", at + 1)) {
        const size_t e = at + 4;
        if (e >= low.size() || !(std::isalnum(static_cast<unsigned char>(low[e])) || low[e] == '_')) return true;
    }
    return low.find("gamer profile") != std::string::npos || low.find("storage device") != std::string::npos ||
           low.find("achievement unlocking") != std::string::npos;
}

}  // namespace

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
                        const auto toks = qs::tokenize(s.value.script->code.data(), s.value.script->code.size());
                        tokens += toks.size();
                        // PC wording (GD04-039): no text the scripts show names the console
                        // (qb.pak: English, the one language the port loads).
                        const bool english = path.find("/qb.pak.xen") != std::string::npos;
                        std::function<void(const qb::Struct&)> structStrings = [&](const qb::Struct& st) {
                            for (const qb::Item& it : st.items) {
                                const qb::Value& v = it.value;
                                if ((v.type == qb::Type::String || v.type == qb::Type::WString) && consoleWording(v.str) && ++failures <= 20)
                                    std::printf("FAIL console wording in %s: \"%s\"\n", qbkey::name(s.name).c_str(), v.str.c_str());
                                if (v.st) structStrings(*v.st);
                            }
                        };
                        for (const auto& t : toks)
                            if (english && t.st) structStrings(*t.st);
                        for (const auto& t : toks)
                            if (english)
                            if ((t.op == qs::kString || t.op == qs::kWideString || t.op == qs::kLocalString) && consoleWording(pcstrings::pcText(t.str))) {
                                if (++failures <= 20) std::printf("FAIL console wording in %s: \"%.80s\"\n", qbkey::name(s.name).c_str(), t.str.c_str());
                            }
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
