// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/shadertest.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

#include "render/renderer.h"

int runShaderTest() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "shader test: cannot start video: %s\n", SDL_GetError());
        return 2;
    }
    // A hidden window: only the context matters.
    SDL_Window* window = SDL_CreateWindow("gd02 shader test", 320, 180, gfx::prepareWindow() | SDL_WINDOW_HIDDEN);
    gfx::Renderer renderer;
    std::string error;
    if (!window || !renderer.init(window, error)) {
        std::fprintf(stderr, "shader test: no OpenGL%s context: %s\n", SDL_getenv("GD02_GLES") ? " ES" : "",
                     window ? error.c_str() : SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    const uint64_t start = SDL_GetTicksNS();
    int programs = 0;
    const std::vector<std::string> failed = renderer.compileAll(programs);
    const gfx::DeviceInfo& info = renderer.device().info();
    std::printf("%s on %s\n", info.version.c_str(), info.gpu.c_str());
    for (const std::string& name : failed) std::printf("  FAILED: %s (the compiler's message is above)\n", name.c_str());
    if (failed.empty())
        std::printf("all %d shader programs compiled (%.1f s)\n", programs, double(SDL_GetTicksNS() - start) / 1e9);
    else
        std::printf("%zu of %d shader programs failed\n", failed.size(), programs);
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failed.empty() ? 0 : 1;
}
