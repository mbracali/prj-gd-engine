// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/shadertest.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "data/disc.h"
#include "data/qbkey.h"
#include "render/gh3_shader.h"
#include "render/renderer.h"
#include "render/xenos_glsl.h"

int runShaderTest(const std::string& discPath) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "shader test: cannot start video: %s\n", SDL_GetError());
        return 2;
    }
    // A hidden window: only the context matters.
    SDL_Window* window = SDL_CreateWindow("gd04 shader test", 320, 180, gfx::prepareWindow() | SDL_WINDOW_HIDDEN);
    gfx::Renderer renderer;
    std::string error;
    if (!window || !renderer.init(window, error)) {
        std::fprintf(stderr, "shader test: no OpenGL%s context: %s\n", SDL_getenv("GD04_GLES") ? " ES" : "",
                     window ? error.c_str() : SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    const uint64_t start = SDL_GetTicksNS();
    int programs = 0;
    std::vector<std::string> failed = renderer.compileAll(programs);
    const gfx::DeviceInfo& info = renderer.device().info();
    if (!discPath.empty()) {
        std::unique_ptr<Disc> disc = Disc::open(discPath, error);
        std::shared_ptr<const matlib::Library> lib = disc ? gh3shader::library(*disc) : nullptr;
        if (!lib) {
            std::printf("shader test: no material library on %s%s\n", discPath.c_str(), disc ? "" : (": " + error).c_str());
            failed.push_back("material library");
        } else {
            int untranslated = 0;
            for (const matlib::Template& t : lib->templates()) {
                if (t.vs[matlib::Default] < 0 || t.ps[matlib::Default] < 0) continue;
                const xenos::Program x = xenos::translate(lib->shader(t.vs[matlib::Default]), lib->shader(t.ps[matlib::Default]));
                if (!x.ok) {
                    ++untranslated;
                    if (SDL_getenv("GD04_SHADERTEST_VERBOSE")) std::printf("  not translated: %s (%s)\n", t.name.c_str(), x.error.c_str());
                    continue;
                }
                ++programs;
                std::string err;
                const gfx::ProgramId p = renderer.device().createProgram(gh3shader::programDesc(x, t.name, info.clipDepthZeroToOne), err);
                if (p) {
                    renderer.device().destroyProgram(p);
                } else {
                    SDL_Log("shader gh3:%s: %s", t.name.c_str(), err.c_str());
                    if (SDL_getenv("GD04_SHADERTEST_VERBOSE")) std::printf("%s\n--\n%s\n", x.vertex.c_str(), x.fragment.c_str());
                    failed.push_back("gh3:" + t.name);
                }
            }
            std::printf("material templates: %d not translated (control flow not supported)\n", untranslated);
            // The screen effects' passes (render/screen_fx.cpp): named techniques of the filter templates.
            const std::pair<const char*, const char*> fx[] = {
                {"ImmediateMode", "_2D_Cutoff"}, {"ImmediateMode", "_2D_DepthOfFieldNear"}, {"ImmediateMode", "_2D_Tex_NoViewport"},
                {"ImmediateModeAlt", "_2D_Tex_BS_NoViewport"}, {"ImmediateModeAlt", "_2D_ComposeBloom0"},
                {"ImmediateModeAlt", "_2D_ComposeBloom01"}, {"ImmediateModeFilter", "_2D_DownSample4x"},
                {"ImmediateModeFilter", "_2D_GaussianBlur7x7"}, {"ImmediateModeFilter", "_2D_GaussianBlur7x7H"},
                {"ImmediateModeFilter", "_2D_GaussianBlur7x7V"}};
            int fxCount = 0;
            for (const auto& [tn, tech] : fx) {
                const matlib::Template* t = lib->find(qbkey::key(tn));
                const matlib::TechniqueRef* r = t ? t->technique(tech) : nullptr;
                const std::string name = std::string(tn) + "." + tech;
                if (!r || r->vs[0] < 0 || r->ps[0] < 0) {
                    failed.push_back("gh3:" + name + " (missing)");
                    continue;
                }
                const xenos::Program x = xenos::translate(lib->shader(r->vs[0]), lib->shader(r->ps[0]));
                ++programs, ++fxCount;
                std::string err;
                const gfx::ProgramId p = x.ok ? renderer.device().createProgram(gh3shader::programDesc(x, name, info.clipDepthZeroToOne), err) : gfx::ProgramId{};
                if (p) renderer.device().destroyProgram(p);
                else SDL_Log("shader gh3:%s: %s", name.c_str(), x.ok ? err.c_str() : x.error.c_str()), failed.push_back("gh3:" + name);
            }
            std::printf("screen effects: %d filter techniques\n", fxCount);
        }
    }
    std::printf("%s on %s\n", info.version.c_str(), info.gpu.c_str());
    for (const std::string& name : failed) std::printf("  FAILED: %s (the compiler's message is above)\n", name.c_str());
    if (failed.empty())
        std::printf("all %d shader programs compiled (%.1f s)\n", programs, double(SDL_GetTicksNS() - start) / 1e9);
    else
        std::printf("%llu of %d shader programs failed\n", (unsigned long long)failed.size(), programs);
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failed.empty() ? 0 : 1;
}
