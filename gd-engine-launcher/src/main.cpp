// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The Guitar Dream Universal Launcher (gdul): see ../README.md. The command
// line (--help) is read here; the launcher itself is launcher.cpp.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <string>
#include <vector>

#include "catalog.h"
#include "launcher.h"
#include "module_host.h"
#include "platform.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

const char* const kHelp =
    "Usage: \"Guitar Dream Universal Launcher\" [options] [-- <game arguments>]\n"
    "\n"
    "With no option the launcher opens its list of games. Options:\n"
    "  --play <game>      start that game at once (gd-game-02, gd02 or 02)\n"
    "  --explore <game>   open that game's Explore models screen at once\n"
    "  --iso <file>       the disc image to use for this run (not saved; without\n"
    "                     it, the game's saved or found disc)\n"
    "  --exit-after       quit when the game (or the explorer) ends, with the\n"
    "                     game's exit code; without it the list comes back\n"
    "  --list             print the games: version, ready, module, disc found\n"
    "  --version          print the launcher's version\n"
    "  --help             print this help\n"
    "  -- <arguments>     everything after -- goes to the game's own command\n"
    "                     line (before the disc image)\n"
    "\n"
    "Exit codes: 0 fine, 1 the game did not start (or ended with 1), 2 a wrong\n"
    "command line; with --play --exit-after, the game's own exit code.\n";

// gd-game-02, gd02, 02 or 2: "gd-game-02"; anything else as it is.
std::string gameId(const std::string& s) {
    std::string n = s;
    if (n.rfind("gd-game-", 0) == 0) return n;
    if (n.rfind("gd", 0) == 0) n = n.substr(2);
    if (!n.empty() && n.size() <= 2 && n.find_first_not_of("0123456789") == std::string::npos)
        return "gd-game-" + std::string(n.size() == 1 ? "0" : "") + n;
    return s;
}

// Windows: the launcher is a window program; started from a terminal, its
// output goes to that terminal (nothing changes when started from Explorer).
void attachConsole() {
#if defined(_WIN32)
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        std::printf("\n");  // past the prompt cmd.exe already printed
    }
#endif
}

int list() {
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_ERROR);  // the catalog's notes are not for this table
    Catalog catalog;
    catalog.load();
    std::printf("Guitar Dream Universal Launcher %s (%s-%s%s)\n", catalog.launcherVersion().c_str(), platform::kOs,
                platform::kArch, platform::isSteamDeck() ? ", Steam Deck" : "");
    std::printf("%-12s %-36s %-8s %-6s %-7s %s\n", "GAME", "NAME", "VERSION", "READY", "MODULE", "DISC");
    for (const Game& g : catalog.games()) {
        const std::string disc = g.iso.empty() ? "not found" : g.iso;
        std::printf("%-12s %-36s %-8s %-6s %-7s %s\n", g.id.c_str(), g.name.c_str(), g.version.c_str(), g.ready ? "yes" : "no",
                    g.hasModule ? "yes" : g.moduleMissing ? "MISSING" : "-", disc.c_str());
    }
    std::printf("Discs are looked for in:\n");
    for (const Catalog::DiscDir& d : catalog.discDirInfo())
        std::printf("  %s (%s)%s\n", d.path.c_str(), d.what.c_str(), d.problem.empty() ? "" : (": " + d.problem).c_str());
    return 0;
}

int usage(const std::string& why) {
    std::fprintf(stderr, "Guitar Dream Universal Launcher: %s\n\n%s", why.c_str(), kHelp);
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    CommandLine cmd;
    std::vector<std::string> gameArgs;
    bool listOnly = false, version = false, help = false;
    std::string bad;
    for (int i = 1; i < argc && bad.empty(); ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) bad = a + " needs a value";
            else out = argv[++i];
        };
        if (a == "--") {
            for (++i; i < argc; ++i) gameArgs.emplace_back(argv[i]);
        } else if (a == "--play") {
            value(cmd.play);
        } else if (a == "--explore") {
            value(cmd.explore);
        } else if (a == "--iso") {
            value(cmd.iso);
        } else if (a == "--exit-after") {
            cmd.exitAfter = true;
        } else if (a == "--list") {
            listOnly = true;
        } else if (a == "--version" || a == "-v") {
            version = true;
        } else if (a == "--help" || a == "-h" || a == "/?") {
            help = true;
        } else if (a.rfind("-psn_", 0) == 0) {
            // macOS: Finder's process serial number on older systems; ignored
        } else {
            bad = "unknown option " + a;
        }
    }
    if (argc > 1) attachConsole();
    if (!bad.empty()) return usage(bad);
    if (!cmd.play.empty() && !cmd.explore.empty()) return usage("--play and --explore: one at a time");
    if (help) {
        std::printf("%s", kHelp);
        return 0;
    }
    if (version) {
        std::printf("Guitar Dream Universal Launcher %s (%s-%s)\n", GDUL_VERSION, platform::kOs, platform::kArch);
        return 0;
    }
    if (listOnly) return list();
    cmd.play = cmd.play.empty() ? "" : gameId(cmd.play);
    cmd.explore = cmd.explore.empty() ? "" : gameId(cmd.explore);
    setExtraGameArgs(gameArgs);
    Launcher launcher;
    launcher.setCommandLine(cmd);
    const int rc = launcher.run();
    std::fflush(stdout);
    return rc;
}
