// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The Guitar Dream Universal Launcher (gdul): see ../README.md.
#include <SDL3/SDL_main.h>

#include "launcher.h"

int main(int, char**) {
    Launcher launcher;
    return launcher.run();
}
