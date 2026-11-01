// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app.h"
#include "platform/gpu_select.h"
#include "platform/paths.h"

namespace {

void usage() {
    std::printf(
        "usage: gd01 [options] [path-to-iso-or-game-folder]\n"
        "  --song <id>          start a song directly (e.g. smokeonthewater)\n"
        "  --difficulty <0-3>   0 Easy, 1 Medium, 2 Hard, 3 Expert (default 3)\n"
        "  --av-offset <ms>     audio/video latency compensation\n"
        "  --no-fail            the rock meter cannot fail you\n"
        "  --autoplay           play the chart automatically\n"
        "  --kb2p               two players on one keyboard (P2: H J K L ; frets, N M strum, B Star Power, O Start)\n"
        "  --no-venue           plain background instead of the song's venue\n"
        "  --selftest           auto-play every chart headlessly and report\n"
        "  --shadertest         compile every shader on this GPU and report (no game data)\n");
}

SDL_LogOutputFunction gDefaultLog = nullptr;
void* gDefaultLogData = nullptr;
std::FILE* gLogFile = nullptr;

void logToFile(void*, int category, SDL_LogPriority priority, const char* message) {
    if (gLogFile) {
        std::fprintf(gLogFile, "%s\n", message);
        std::fflush(gLogFile);
    }
    if (gDefaultLog) gDefaultLog(gDefaultLogData, category, priority, message);
}

#ifdef _WIN32
LPTOP_LEVEL_EXCEPTION_FILTER gPrevFilter = nullptr;

// A crash on Windows: its code and address (as an offset into gd01.exe or the
// module) in the log before the program ends, for bug reports.
LONG WINAPI logCrash(EXCEPTION_POINTERS* e) {
    const void* at = e && e->ExceptionRecord ? e->ExceptionRecord->ExceptionAddress : nullptr;
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (at && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 static_cast<LPCSTR>(at), &mod))
        GetModuleFileNameA(mod, name, MAX_PATH);
    SDL_Log("crash: exception 0x%08lx at %s+0x%llx", e && e->ExceptionRecord ? e->ExceptionRecord->ExceptionCode : 0ul, name,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(at) - reinterpret_cast<uintptr_t>(mod)));
    if (gLogFile) std::fflush(gLogFile);
    return gPrevFilter ? gPrevFilter(e) : EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Windows has no console for it: without GD01_LOG the log goes to gd01.log
// in the save folder, for bug reports.
void openLogFile() {
    const char* env = std::getenv("GD01_LOG");
    std::string path = env ? env : "";
#ifdef _WIN32
    if (!env) path = prefDir().empty() ? "" : prefDir() + "gd01.log";
    gPrevFilter = SetUnhandledExceptionFilter(logCrash);
#endif
    if (path.empty() || gLogFile) return;
    gLogFile = std::fopen(path.c_str(), "w");
    if (!gLogFile) return;
    SDL_GetLogOutputFunction(&gDefaultLog, &gDefaultLogData);
    SDL_SetLogOutputFunction(logToFile, nullptr);
}

// The log back to SDL's own, the file closed (the launcher module returns
// to the launcher, which keeps logging).
void closeLogFile() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(gPrevFilter);  // the launcher may unload this module
#endif
    if (!gLogFile) return;
    SDL_SetLogOutputFunction(gDefaultLog, gDefaultLogData);
    std::fclose(gLogFile);
    gLogFile = nullptr;
}

// Open for the whole of main, on every way out.
struct LogFile {
    LogFile() { openLogFile(); }
    ~LogFile() { closeLogFile(); }
};

}  // namespace

int main(int argc, char** argv) {
    aliasLegacyEnv();
    LogFile logFile;
    gpu::setArgs(argc, argv);
    Options opt;
    bool selftest = false, scenetest = false, shadertest = false;
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
        else if (a == "--av-offset") opt.avOffsetMs = std::atof(value().c_str());
        else if (a == "--no-fail") opt.noFail = true;
        else if (a == "--autoplay") opt.autoplay = true;
        else if (a == "--kb2p") opt.splitKeyboard = true;
        else if (a == "--no-venue") opt.venue = false;
        else if (a == "--venue") opt.venue = true;
        else if (a == "--selftest") selftest = true;
        else if (a == "--scenetest") scenetest = true;
        else if (a == "--shadertest") shadertest = true;
        else if (a == "--view") opt.viewScene = value();
        else if (a == "--panel") opt.panel = value();
        else if (a == "--character") opt.character = value();
        else if (a == "--at") opt.venueName = value();
        else if (a == "--guitar") opt.guitar = value();
        else if (a == "--skip-boot") opt.skipBoot = true;
        else if (a == "--root") opt.viewRoot = value();
        else if (a == "--cam") opt.viewCam = value();
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-' && a.rfind("-psn_", 0) != 0) { usage(); return 2; }  // -psn_: macOS Finder
        else if (a.rfind("-psn_", 0) != 0) opt.dataPath = a;
    }
    if (shadertest) return runShaderTest();
    if (scenetest) return runSceneTest(opt);
    return selftest ? runSelfTest(opt) : runApp(opt);
}
