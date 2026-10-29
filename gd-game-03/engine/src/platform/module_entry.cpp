// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd03 as a module of the Guitar Dream Universal Launcher (GD03_MODULE,
// gd-engine-launcher/README.md): the launcher loads gd-game-03.so / .dll into
// its own process and calls gd_game_run with its window. gd03's own main runs
// unchanged; gd_module_shim.h (force-included in the module build) points its
// SDL_Init / SDL_CreateWindow / SDL_Quit at the launcher's SDL and window.
#include <string>
#include <vector>

#include "gd_host.h"
#include "gd_module_shim.h"

int gd03Main(int argc, char** argv);

extern "C" GD_MODULE_EXPORT int gd_game_run(const GdHost* host) {
    if (!host || host->abi_version != GD_HOST_ABI_VERSION || !host->window) return 3;
    gdshim_begin(host);
    // The launcher has its own application-menu entry: none for gd03 here.
    gdshim_setenv("GD03_NO_DESKTOP_ENTRY", "1");
    // A Steam Deck (the launcher's handheld profile): the Steam Deck settings
    // are the Enhanced Options' defaults (App::enhancedValue).
    gdshim_setenv("GD03_HANDHELD", (GD_HOST_FLAGS(host) & GD_HOST_HANDHELD) ? "1" : "0");
    std::vector<std::string> args(host->argv, host->argv + host->argc);
    if (args.empty()) args.emplace_back("gd03");
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    if (host->log) host->log(host->log_user, "gd03 " GD03_VERSION " started");
    const int rc = gd03Main(int(args.size()), argv.data());
    gdshim_end();
    return rc;
}
