// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd24 as a module of the Guitar Dream Universal Launcher (GD24_MODULE,
// gd-engine-launcher/README.md "Games as modules"): the launcher loads
// gd-game-24.so / .dll into its own process and calls gd_game_run with its
// window; gd_module_shim.h (force-included) points gd24's SDL_Init /
// SDL_CreateWindow / SDL_Quit at the launcher's.
#include <string>
#include <vector>

#include "gd_host.h"
#include "gd_module_shim.h"

int gd24Main(int argc, char** argv);

extern "C" GD_MODULE_EXPORT int gd_game_run(const GdHost* host) {
    if (!host || host->abi_version != GD_HOST_ABI_VERSION || !host->window) return 3;
    gdshim_begin(host);
    gdshim_setenv("GD24_HANDHELD", (GD_HOST_FLAGS(host) & GD_HOST_HANDHELD) ? "1" : "0");
    gdshim_setenv("GD24_FULLSCREEN", (GD_HOST_FLAGS(host) & GD_HOST_FULLSCREEN) ? "1" : "0");
    std::vector<std::string> args(host->argv, host->argv + host->argc);
    if (args.empty()) args.emplace_back("gd24");
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    if (host->log) host->log(host->log_user, "gd24 " GD24_VERSION " started");
    const int rc = gd24Main(int(args.size()), argv.data());
    gdshim_end();
    return rc;
}
