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
#include "render/scene_view.h"
#include "render/shadertest.h"
#include "scene/scenetest.h"
#include "tests/scripttest.h"
#include "tests/selftest.h"
#include "tests/uitest.h"
#include "tests/sfxtest.h"
#include "tests/facefxtest.h"
#include "tests/venuetest.h"
#include "tests/chartest.h"
#include "video/wmv_player.h"

namespace {

void usage() {
    std::printf(
        "%s %s: Guitar Hero II (Xbox 360) reimplementation (first skeleton)\n"
        "usage: %s [options] [disc.iso | game folder]\n"
        "  --song <id>          play one song directly (e.g. yyz), then quit\n"
        "  --difficulty <0-3>   0 Easy, 1 Medium, 2 Hard, 3 Expert (default 3)\n"
        "  --autoplay           play the chart automatically\n"
        "  --no-fail            the rock meter cannot fail you\n"
        "  --selftest           check the data, auto-play every chart and check the rules headlessly\n"
        "  --scenetest          parse every .milo_ps2 scene and report decoded objects per class\n"
        "  --scripttest         load every script, run the boot scripts with stub objects and report (GD03_UILOG=1 logs\n"
        "                       UNHANDLED / STUB / UNKNOWN script messages once each, also in the game)\n"
        "  --sfxtest            decode every sound effect (XMA SynthSamples) and check the banks and the sounds the\n"
        "                       scripts play (GD03_SFX_DUMP=<dir> writes them as WAVs)\n"
        "  --facefxtest         parse every FaceFX .voc / .fac and check the lip servo's face graph against reference values\n"
        "  --shadertest         compile every shader on this GPU and report (no game data; GD03_GLES=1: OpenGL ES)\n"
        "  --view <milo>        draw one scene, e.g. world/stone/gen/stone.milo_ps2 (development viewer); a UI\n"
        "                       panel (ui/gen/main.milo_ps2) draws with its components and texts (GD03_VIEW_LANG=\n"
        "                       eng|fre|deu|ita|esl, GD03_VIEW_FOCUS=<button>, GD03_VIEW_UI=0 plain scene)\n"
        "  --playmovie <name>   play one of the disc's videos (intro.wmv, hmx.wmv, wg.wmv), then quit\n"
        "  --uitest             run GH2's menu scripts headlessly: every screen and panel, their milos and components;\n"
        "                       GH2's career rules on a scratch band\n"
        "  --chartest           decode every character class on the disc (clips, rigs, band slots), check the counts\n"
        "                       and golden poses\n"
        "  --venuetest          load the 8 venues (one player, face-off, co-op): their milos, classes, references and\n"
        "                       scripts, headlessly\n"
        "  --walktest <walk>    play through the menus with scripted buttons and check them: quickplay, lose, pause,\n"
        "                       attract, career, career_full, career_song, back, enhanced, cheats, coop, faceoff, proface (headless: SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy)\n"
        "Environment: GD03_KEYS=\"<ms>:<key>,...,s<song ms>:<key>,...\" scripted input: menu keys at wall-clock ms\n"
        "  (confirm back up down left right start select green red yellow blue orange; +key / -key hold / release),\n"
        "  song keys at song times (+green/-green .. orange, strum, starpower, whammy=<0..1>); menu items k<key name>\n"
        "  (kTab, kReturn) are real keys and j<button> (ja, jstart) GD03_VIRTUAL_PAD's buttons; GD03_SKIP_BOOT=1 skips\n"
        "  the splashes and the intro, GD03_SCREEN=<screen> opens a screen; GD03_DEBUG_MENU=1 shows the debug song list\n"
        "  instead of GH2's menus; GD03_UILOG=1 logs UNHANDLED / STUB / UNKNOWN script messages; GD03_SHOTS=<dir> saves\n"
        "  every screen reached; GD03_SCREENSHOT=<file.bmp> with GD03_SCREENSHOT_AT=<ms>[,<ms>...] (with the menus:\n"
        "  wall-clock ms, s<ms> song ms) saves frames; GD03_AUTOPLAY=1 plays the menus' songs automatically;\n"
        "  GD03_VIRTUAL_PAD=<vendor>:<product>[:guitar] (hex) plugs in a virtual\n"
        "  controller (e.g. 1430:4748:guitar, 045e:028e). GD03_DEBUG_HIGHWAY=1 draws the debug highway\n"
        "  instead of GH2's track and HUD; GD03_HUD=0 hides the HUD, GD03_TRACK=0 the track. GD03_MODE=faceoff|proface|coop|practice\n"
        "  plays that mode (without the menus) with a second autoplaying player (GD03_DIFF2=0..3 its difficulty,\n"
        "  GD03_PARTS=guitar,bass the co-op parts); GD03_LEFTY=1|2|12 lefty flip;\n"
        "  GD03_FAIL_AT=<ms> fails the song then, GD03_END_AT=<ms> ends (wins) it then; GD03_STATS=1 logs fps; GD03_SFXLOG=1 logs every sound played.\n"
        "  GD03_LOG=<file> also writes the log to that file (Windows: gd03.log in the save folder); GD03_PERF=1 logs a frame-time breakdown every 2 s.\n"
        "  GD03_FACE_CAM=<member>[:<distance>] (guitarist0, singer, ...) frames that band member's head.\n"
        "  GD03_POST=0 draws the venue without GH2's post-process; GD03_POST_DEBUG=bloom|dof|lum|doflow shows its\n"
        "  buffers (--view: GD03_VIEW_POST=<milo>[:<PostProc>], GD03_VIEW_DOF=<focus>,<blur_depth>,<max_blur>).\n"
        "  GD03_SKIP_MOVIES=1 skips the videos; GD03_MOVIE_DUMP=<file.yuv> makes --playmovie only decode\n"
        "  GD03_MOVIE_FRAMES (default 120) pictures into that file, headless.\n"
        "The game folder is the disc's files (gen/main.hdr, gen/main_0.ark, ...). Without one, the\n"
        "disc image picked last time is used (or an .iso next to the program), else the first\n"
        "screen asks for it.\n",
        app::kProgram, app::kVersion, app::kProgram);
}

// GD03_LOG=<file>: every SDL_Log line also goes to that file (the Windows
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

// A crash on Windows: its code and address (as an offset into gd03.exe or the
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

// Windows has no console for it: without GD03_LOG the log goes to gd03.log
// in the save folder, for bug reports.
void openLogFile() {
    const char* env = std::getenv("GD03_LOG");
    std::string path = env ? env : "";
#ifdef _WIN32
    if (!env) path = prefDir().empty() ? "" : prefDir() + "gd03.log";
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
int gd03Main(int argc, char** argv) {
    openLogFile();
    const int rc = run(argc, argv);
    closeLogFile();
    return rc;
}

namespace {

int run(int argc, char** argv) {
    Options opt;
    bool selftest = false, scenetest = false, shadertest = false, scripttest = false, uitest = false, venuetest = false, sfxtest = false, facefxtest = false, chartest = false;
    std::string viewPath, movie;
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
        else if (a == "--scenetest") scenetest = true;
        else if (a == "--shadertest") shadertest = true;
        else if (a == "--scripttest") scripttest = true;
        else if (a == "--uitest") uitest = true;
        else if (a == "--venuetest") venuetest = true;
        else if (a == "--chartest") chartest = true;
        else if (a == "--sfxtest") sfxtest = true;
        else if (a == "--facefxtest") facefxtest = true;
        else if (a == "--walktest") opt.walk = value();
        else if (a == "--view") viewPath = value();
        else if (a == "--playmovie") movie = value();
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-' && a.rfind("-psn_", 0) != 0) { usage(); return 2; }  // -psn_: macOS Finder
        else if (a.rfind("-psn_", 0) != 0) opt.dataPath = a;
    }
    if (shadertest) return runShaderTest();
    if ((selftest || scenetest || scripttest || uitest || venuetest || sfxtest || facefxtest || chartest || !opt.walk.empty() || !viewPath.empty() || !movie.empty()) && opt.dataPath.empty()) {
        usage();
        return 2;
    }
    if (scenetest) return runSceneTest(opt.dataPath);
    if (scripttest) return runScriptTest(opt.dataPath);
    if (uitest) return runUiTest(opt.dataPath);
    if (venuetest) return runVenueTest(opt.dataPath);
    if (chartest) return runCharTest(opt.dataPath);
    if (sfxtest) return runSfxTest(opt.dataPath);
    if (facefxtest) return runFaceFxTest(opt.dataPath);
    if (!viewPath.empty()) return runSceneView(opt.dataPath, viewPath);
    if (!movie.empty()) return runPlayMovie(opt.dataPath, movie);
    return selftest ? runSelfTest(opt.dataPath) : runApp(opt);
}

}  // namespace

#ifndef GD03_MODULE
int main(int argc, char** argv) { return gd03Main(argc, argv); }
#endif
