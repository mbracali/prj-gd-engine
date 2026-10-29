// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/audittest.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <map>
#include <set>
#include <vector>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qb.h"
#include "data/qbkey.h"
#include "script/bytecode.h"
#include "script/globals.h"
#include "script/natives_core.h"
#include "script/vm.h"
#include "tests/format_checks.h"
#include "render/renderer.h"
#include "render/texture_cache.h"
#include "ui/front_end.h"

#include <SDL3/SDL.h>
#include <memory>

int runAuditTest(const std::string& discPath, const std::string& outPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::fprintf(stderr, "audittest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    Globals g;
    // Every QB file of every pak is part of the namespace for the audit.
    std::map<uint32_t, size_t> calls;  // name called at a line start -> count
    std::map<std::string, size_t> typesSeen;
    FormatStats stats;
    for (const std::string& path : disc->list()) {
        if (path.size() < 8 || path.compare(path.size() - 8, 8, ".pak.xen") != 0) continue;
        Pak pak = Pak::load(*disc, path);
        for (const PakEntry& e : pak.entries()) {
            ++typesSeen[qbkey::name(e.type)];
            std::string what;
            checkPakFile(pak, e, stats, what);
            if (e.type != qbkey::key(".qb")) continue;
            auto span = pak.data(e);
            qb::File f = qb::decode(span.first, span.second);
            g.add(f);
            for (const qb::Section& s : f.sections) {
                if (s.value.type != qb::Type::Script) continue;
                auto toks = qs::tokenize(s.value.script->code.data(), s.value.script->code.size());
                bool lineStart = true;
                int depth = 0;  // inside a multi-line struct / array literal: flags and values, not calls
                for (size_t i = 0; i < toks.size(); ++i) {
                    const qs::Op op = toks[i].op;
                    if (op == qs::kStartStruct || op == qs::kStartArray) ++depth;
                    else if ((op == qs::kEndStruct || op == qs::kEndArray) && depth > 0) --depth;
                    if (op == qs::kEndOfLine || op == qs::kEndOfLineNumber) {
                        lineStart = true;
                        continue;
                    }
                    if (op == qs::kNop) continue;
                    // A call: a name at the start of a line (not `x = ...`), or
                    // right after if / elseif / not.
                    // (A member call `x : Name` is the object's, not a native.)
                    // (`Bassist : obj_x` names the object, not a native.)
                    const bool member = (i > 0 && toks[i - 1].op == qs::kColon) || (i + 1 < toks.size() && toks[i + 1].op == qs::kColon);
                    if (op == qs::kName && lineStart && depth == 0 && !member && !(i + 1 < toks.size() && toks[i + 1].op == qs::kEquals))
                        ++calls[toks[i].u];
                    lineStart = (op == qs::kFastIf || op == qs::kElseIf || op == qs::kNot);
                }
            }
        }
    }
    // The natives as the game registers them: a front end on a hidden window
    // (the screen elements, flow, game and sound natives); a bare VM's core
    // natives if no renderer can start.
    qs::Vm bare(g);
    qs::addCoreNatives(bare);
    std::unique_ptr<FrontEnd> fe;
    std::unique_ptr<TextureCache> tex;
    gfx::Renderer renderer;
    SDL_Window* window = nullptr;
    if (SDL_Init(SDL_INIT_VIDEO)) {
        window = SDL_CreateWindow("gd04 audit", 1280, 720, gfx::prepareWindow() | SDL_WINDOW_HIDDEN);
        std::string rerr;
        if (window && renderer.init(window, rerr)) {
            tex = std::make_unique<TextureCache>(renderer.device());
            fe = std::make_unique<FrontEnd>(*disc, g, *tex, renderer.device());
        } else {
            std::printf("audittest: no renderer (%s); counting the core natives only\n", rerr.c_str());
        }
    }
    qs::Vm& vm = fe ? fe->vm() : bare;
    std::vector<std::pair<size_t, std::string>> missing;
    size_t scripts = 0, natives = 0;
    for (auto& [k, n] : calls) {
        const qb::Value* v = g.find(k);
        if (v && v->type == qb::Type::Script) ++scripts;
        else if (vm.hasNative(k)) ++natives;
        else missing.emplace_back(n, qbkey::name(k));
    }
    std::sort(missing.rbegin(), missing.rend());
    std::FILE* out = outPath.empty() ? stdout : std::fopen(outPath.c_str(), "w");
    if (!out) {
        std::fprintf(stderr, "audittest: cannot write %s\n", outPath.c_str());
        return 2;
    }
    std::fprintf(out, "# gd-game-04 asset and reference audit\n\nMade by `gd04 --audittest <disc> docs/audit.md`; re-run at every gate.\n\n");
    std::fprintf(out, "## Names the scripts call\n\n%zu distinct names: %zu are scripts, %zu are natives the engine has, **%zu are natives it does not have yet** (below, most-called first). Each is a backlog item until implemented or shown unused by the original's flow.\n\n",
                 calls.size(), scripts, natives, missing.size());
    std::fprintf(out, "| Calls | Name |\n|---:|---|\n");
    for (auto& [n, name] : missing) std::fprintf(out, "| %zu | %s |\n", n, name.c_str());
    std::fprintf(out, "\n## File types in the paks\n\n| Type | Files | Decoded by the engine |\n|---|---:|---|\n");
    for (auto& [t, n] : typesSeen)
        std::fprintf(out, "| %s | %zu | %s |\n", t.c_str(), n, stats.decoded.count(t) ? "yes" : "**no**");
    std::fprintf(out, "\nWhat the engine draws, plays or runs of each decoded type is audited per milestone (M4-M7).\n");
    if (out != stdout) std::fclose(out);
    std::printf("audittest: %zu called names, %zu scripts, %zu natives, %zu natives missing\n", calls.size(), scripts, natives,
                missing.size());
    fe.reset();
    tex.reset();
    renderer.shutdown();
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
