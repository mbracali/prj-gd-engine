// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "app.h"
#include "app_ids.h"
#include "render/scene_view.h"
#include "render/shadertest.h"
#include "scene/scenetest.h"
#include "tests/scripttest.h"
#include "tests/selftest.h"
#include "tests/uitest.h"
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
        "  --scenetest          parse every .milo_xbox scene and report decoded objects per class\n"
        "  --scripttest         load every script, run the boot scripts with stub objects and report (GD02_UILOG=1 logs\n"
        "                       UNHANDLED / STUB / UNKNOWN script messages once each, also in the game)\n"
        "  --shadertest         compile every shader on this GPU and report (no game data; GD02_GLES=1: OpenGL ES)\n"
        "  --view <milo>        draw one scene, e.g. world/stone/gen/stone.milo_xbox (development viewer); a UI\n"
        "                       panel (ui/gen/main.milo_xbox) draws with its components and texts (GD02_VIEW_LANG=\n"
        "                       eng|fre|deu|ita|esl, GD02_VIEW_FOCUS=<button>, GD02_VIEW_UI=0 plain scene)\n"
        "  --playmovie <name>   play one of the disc's videos (intro.wmv, hmx.wmv, wg.wmv), then quit\n"
        "  --uitest             run GH2's menu scripts headlessly: every screen and panel, their milos and components\n"
        "  --walktest <walk>    play through the menus with scripted buttons and check them: quickplay, lose, pause,\n"
        "                       attract (headless: SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy)\n"
        "Environment: GD02_KEYS=\"<ms>:<key>,...,s<song ms>:<key>,...\" scripted input: menu keys at wall-clock ms\n"
        "  (confirm back up down left right start select green red yellow blue orange; +key / -key hold / release),\n"
        "  song keys at song times (+green/-green .. orange, strum, starpower, whammy=<0..1>); GD02_SKIP_BOOT=1 skips\n"
        "  the splashes and the intro, GD02_SCREEN=<screen> opens a screen; GD02_DEBUG_MENU=1 shows the debug song list\n"
        "  instead of GH2's menus; GD02_UILOG=1 logs UNHANDLED / STUB / UNKNOWN script messages; GD02_SHOTS=<dir> saves\n"
        "  every screen reached; GD02_SCREENSHOT=<file.bmp> with GD02_SCREENSHOT_AT=<ms>[,<ms>...] (with the menus:\n"
        "  wall-clock ms, s<ms> song ms) saves frames; GD02_AUTOPLAY=1 plays the menus' songs automatically;\n"
        "  GD02_VIRTUAL_PAD=<vendor>:<product>[:guitar] (hex) plugs in a virtual\n"
        "  controller (e.g. 1430:4748:guitar, 045e:028e). GD02_DEBUG_HIGHWAY=1 draws the debug highway\n"
        "  instead of GH2's track and HUD; GD02_HUD=0 hides the HUD. GD02_MODE=vs|faceoff|coop|practice\n"
        "  shows that mode's tracks and HUD with a second autoplaying player; GD02_LEFTY=1|2|12 lefty flip;\n"
        "  GD02_FAIL_AT=<ms> fails the song then, GD02_END_AT=<ms> ends (wins) it then; GD02_STATS=1 logs fps.\n"
        "  GD02_SKIP_MOVIES=1 skips the videos; GD02_MOVIE_DUMP=<file.yuv> makes --playmovie only decode\n"
        "  GD02_MOVIE_FRAMES (default 120) pictures into that file, headless.\n"
        "The game folder is the disc's files (gen/main.hdr, gen/main_0.ark, ...). Without one, the\n"
        "disc image picked last time is used (or an .iso next to the program), else the first\n"
        "screen asks for it.\n",
        app::kProgram, app::kVersion, app::kProgram);
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    bool selftest = false, scenetest = false, shadertest = false, scripttest = false, uitest = false;
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
        else if (a == "--walktest") opt.walk = value();
        else if (a == "--view") viewPath = value();
        else if (a == "--playmovie") movie = value();
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-' && a.rfind("-psn_", 0) != 0) { usage(); return 2; }  // -psn_: macOS Finder
        else if (a.rfind("-psn_", 0) != 0) opt.dataPath = a;
    }
    if (shadertest) return runShaderTest();
    if ((selftest || scenetest || scripttest || uitest || !opt.walk.empty() || !viewPath.empty() || !movie.empty()) && opt.dataPath.empty()) {
        usage();
        return 2;
    }
    if (scenetest) return runSceneTest(opt.dataPath);
    if (scripttest) return runScriptTest(opt.dataPath);
    if (uitest) return runUiTest(opt.dataPath);
    if (!viewPath.empty()) return runSceneView(opt.dataPath, viewPath);
    if (!movie.empty()) return runPlayMovie(opt.dataPath, movie);
    return selftest ? runSelfTest(opt.dataPath) : runApp(opt);
}
