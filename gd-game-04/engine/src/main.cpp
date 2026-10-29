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
#include <string>

#include "app.h"
#include "app_ids.h"
#include "platform/paths.h"
#include "render/shadertest.h"
#include "tests/audittest.h"
#include "tests/formattest.h"
#include "tests/scripttest.h"
#include "tests/selftest.h"
#include "video/movie_player.h"
#include "tests/sfxtest.h"

namespace {

void usage() {
    std::printf(
        "%s %s: Guitar Hero III: Legends of Rock (Xbox 360) reimplementation\n"
        "usage: %s [options] [disc.iso | disc folder]\n"
        "  --song <id>          play one song directly (e.g. slowride), then quit\n"
        "  --difficulty <0-3>   0 Easy, 1 Medium, 2 Hard, 3 Expert (default 3)\n"
        "  --autoplay           play the chart automatically\n"
        "  --no-fail            the rock meter cannot fail you\n"
        "  --formattest         parse every pak on the disc and every file in it to its end\n"
        "  --selftest           check the data (song list, every chart), auto-play every chart\n"
        "  --uitest             boot through the menus, then start every flow state's screen in turn (headless)\n"
        "  --walktest <walk>    play through GH3's own menus: quickplay, lose, back, boot (headless:\n"
        "                       SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy)\n"
        "  --audittest [--out f] the asset / reference audit: names the scripts call and pak types the engine lacks\n"
        "  --scripttest         tokenize every script; run the boot script against the natives (GD04_UILOG=1 logs\n"
        "                       every unhandled call once)\n"
        "  --sfxtest            decode every sound of every FMOD bank (GD04_SFXTEST_LIMIT=<n>: the first n)\n"
        "  --playmovie <name>   play one video (atvi, ns_logo, GH3_Intro, ...), then quit; GD04_MOVIE_DUMP=<f.yuv>\n"
        "                       decodes GD04_MOVIE_FRAMES (120) pictures headless; GD04_SKIP_MOVIES=1 skips videos\n"
        "  --shadertest         compile every shader on this GPU and report (no game data; GD04_GLES=1: OpenGL ES)\n"
        "Environment: GD04_KEYS=\"<ms>:<key>,...\" scripted menu keys; GD04_SCREENSHOT=<file.bmp> with\n"
        "  GD04_SCREENSHOT_AT=<ms>[,<ms>...] saves frames; GD04_LOG=<file> also writes the log to that file\n"
        "  (Windows: gd04.log in the save folder); GD04_STATS=1 logs fps; GD04_PERF=1 a frame-time breakdown.\n"
        "The disc folder is the disc's files (DATA/, default.xex). Without one, the disc image picked\n"
        "last time is used (or an .iso next to the program), else the first screen asks for it.\n",
        app::kProgram, app::kVersion, app::kProgram);
}

// GD04_LOG=<file>: every SDL_Log line also goes to that file (the Windows
// build is a GUI program, so its log otherwise only reaches a debugger).
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

// A crash on Windows: its code and address (as an offset into gd04.exe or the
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

// Windows has no console for it: without GD04_LOG the log goes to gd04.log
// in the save folder, for bug reports.
void openLogFile() {
    const char* env = std::getenv("GD04_LOG");
    std::string path = env ? env : "";
#ifdef _WIN32
    if (!env) path = prefDir().empty() ? "" : prefDir() + "gd04.log";
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

int run(int argc, char** argv);

}  // namespace

// The program: main() below, or the launcher module's gd_game_run
// (platform/module_entry.cpp) in the Guitar Dream Universal Launcher.
int gd04Main(int argc, char** argv) {
    openLogFile();
    const int rc = run(argc, argv);
    closeLogFile();
    return rc;
}

namespace {

int run(int argc, char** argv) {
    Options opt;
    std::string movie;
    bool audittest = false, formattest = false, shadertest = false, selftest = false, sfxtest = false, scripttest = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                return std::string();
            }
            return argv[++i];
        };
        if (a == "--song") opt.song = value();
        else if (a == "--difficulty") opt.difficulty = std::atoi(value().c_str());
        else if (a == "--autoplay") opt.autoplay = true;
        else if (a == "--no-fail") opt.noFail = true;
        else if (a == "--formattest") formattest = true;
        else if (a == "--selftest") selftest = true;
        else if (a == "--sfxtest") sfxtest = true;
        else if (a == "--scripttest") scripttest = true;
        else if (a == "--audittest") audittest = true;
        else if (a == "--out") opt.out = value();
        else if (a == "--shadertest") shadertest = true;
        else if (a == "--walktest") opt.walk = value();
        else if (a == "--uitest") opt.walk = "uitest";
        else if (a == "--playmovie") movie = value();
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-' && a.rfind("-psn_", 0) != 0) { usage(); return 2; }  // -psn_: macOS Finder
        else if (a.rfind("-psn_", 0) != 0) opt.dataPath = a;
    }
    if (shadertest) return runShaderTest();
    if ((!movie.empty() || audittest || formattest || selftest || sfxtest || scripttest || !opt.walk.empty()) && opt.dataPath.empty()) {
        usage();
        return 2;
    }
    if (!movie.empty()) return runPlayMovie(opt.dataPath, movie);
    if (formattest) return runFormatTest(opt.dataPath);
    if (selftest) return runSelfTest(opt.dataPath);
    if (sfxtest) return runSfxTest(opt.dataPath);
    if (scripttest) return runScriptTest(opt.dataPath);
    if (audittest) return runAuditTest(opt.dataPath, opt.out);
    return runApp(opt);
}

}  // namespace

#ifndef GD04_MODULE
int main(int argc, char** argv) { return gd04Main(argc, argv); }
#endif
