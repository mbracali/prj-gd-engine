// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd02 as a module of the Guitar Dream Universal Launcher (GD02_MODULE,
// gd-engine-launcher/README.md): the launcher loads gd-game-02.so / .dll into
// its own process and calls gd_game_run with its window. gd02's own main runs
// unchanged; gd_module_shim.h (force-included in the module build) points its
// SDL_Init / SDL_CreateWindow / SDL_Quit at the launcher's SDL and window.
#include <string>
#include <vector>

#include "gd_host.h"
#include "gd_module_shim.h"

int gd02Main(int argc, char** argv);

extern "C" GD_MODULE_EXPORT int gd_game_run(const GdHost* host) {
    if (!host || host->abi_version != GD_HOST_ABI_VERSION || !host->window) return 3;
    gdshim_begin(host);
    // The launcher has its own application-menu entry: none for gd02 here.
    gdshim_setenv("GD02_NO_DESKTOP_ENTRY", "1");
    std::vector<std::string> args(host->argv, host->argv + host->argc);
    if (args.empty()) args.emplace_back("gd02");
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    if (host->log) host->log(host->log_user, "gd02 " GD02_VERSION " started");
    const int rc = gd02Main(int(args.size()), argv.data());
    gdshim_end();
    return rc;
}
