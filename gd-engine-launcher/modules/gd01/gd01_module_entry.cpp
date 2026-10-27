// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd01 (gd-game-01, Guitar Hero PS2) as a module of the Guitar Dream
// Universal Launcher, built from gd-game-01's sources without changing them
// (modules/gd01/CMakeLists.txt): its main() is compiled as gd01_main, and
// gd_module_shim.h, force-included in every file, points its SDL_Init /
// SDL_CreateWindow / SDL_Quit at the launcher's SDL and window.
#include <string>
#include <vector>

#include "gd_host.h"
#include "gd_module_shim.h"

int gd01_main(int argc, char** argv);

extern "C" GD_MODULE_EXPORT int gd_game_run(const GdHost* host) {
    if (!host || host->abi_version != GD_HOST_ABI_VERSION || !host->window) return 3;
    gdshim_begin(host);
    // The launcher has its own application-menu entry: none for gd01 here.
    gdshim_setenv("GD01_NO_DESKTOP_ENTRY", "1");
    // gd01 picks the dedicated GPU of a hybrid laptop by setting the driver's
    // variables before SDL starts, and restarts itself (execv) when that GPU
    // fails. In the launcher, SDL and OpenGL started long before: keep the
    // launcher's GPU, never restart the launcher's process.
    gdshim_setenv("GD01_GPU", "system");
    std::vector<std::string> args(host->argv, host->argv + host->argc);
    if (args.empty()) args.emplace_back("gd01");
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    if (host->log) host->log(host->log_user, "gd01 " GD01_VERSION " started");
    const int rc = gd01_main(int(args.size()), argv.data());
    gdshim_end();
    return rc;
}
