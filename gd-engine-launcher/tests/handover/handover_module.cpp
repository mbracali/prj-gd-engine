// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// gd-test-handover: a test module for the launcher's window hand-over
// (sdk/gd_host.h request_window, gd-engine-core/docs/render.md section 5).
// Built only with GDUL_BUILD_TESTS (tests/handover/CMakeLists.txt), never
// shipped. Like a game on gd-engine-core, it asks for its window through
// gd::core::render::prepareWindow + SDL_CreateWindow (the module shim does
// the asking), for each API in turn, checks what it gets, uses it (an SDL_GPU
// device claims and presents it, a Vulkan surface is made on it, the core's
// OpenGL3 device draws on it) and returns with the window still made for
// SDL_GPU: the launcher must take its OpenGL window back by itself.
//
// gd_game_run returns the number of failed checks (0: all passed). The lines
// "handover: PASS ..." / "FAIL ..." / "SKIP ..." go to the log.
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <string>

#include "gd/core/render/device.h"
#include "gd_host.h"
#include "gd_module_shim.h"

namespace gr = gd::core::render;

namespace {

int g_failed = 0;

void check(bool ok, const std::string& what) {
    if (!ok) ++g_failed;
    SDL_Log("handover: %s %s", ok ? "PASS" : "FAIL", what.c_str());
}
void skip(const std::string& what) { SDL_Log("handover: SKIP %s", what.c_str()); }

std::string windowApiProperty() {
    const char* p = SDL_GetStringProperty(SDL_GetGlobalProperties(), gr::kWindowApiProperty, "");
    return p ? p : "";
}

// The window for `b`, as a game on the core makes it.
SDL_Window* windowFor(SDL_Window* old, gr::Backend b) {
    if (old) SDL_DestroyWindow(old);  // the shim: nothing for the launcher's window
    gr::prepareWindow(b, false);
    return SDL_CreateWindow("gd-test-handover", 640, 360, SDL_WindowFlags(gr::windowFlags(b)) | SDL_WINDOW_RESIZABLE);
}

// SDL_GPU: a device that claims the window, clears a frame and presents it.
void useSdlGpu(SDL_Window* w) {
    SDL_GPUDevice* dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    if (!dev) {
        skip(std::string("SDL_GPU device (no driver for SPIR-V here: ") + SDL_GetError() + ")");
        return;
    }
    const bool claimed = SDL_ClaimWindowForGPUDevice(dev, w);
    const std::string what = std::string("SDL_GPU (") + SDL_GetGPUDeviceDriver(dev) + ") claims the handed-over window";
    if (!claimed && std::strcmp(SDL_GetCurrentVideoDriver(), "offscreen") == 0) {
        // [C] 2026-11-06, NVIDIA 615.71: the offscreen driver's surface
        // (VK_EXT_headless_surface) cannot present, so SDL_GPU's swapchain
        // cannot be made on it. The hand-over itself is checked above; run
        // the test under x11 / wayland for this part.
        skip(what + " (" + SDL_GetError() + ": the offscreen video driver cannot present)");
        SDL_DestroyGPUDevice(dev);
        return;
    }
    check(claimed, what + (claimed ? "" : std::string(": ") + SDL_GetError()));
    if (claimed) {
        bool presented = false;
        for (int frame = 0; frame < 3; ++frame) {
            SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(dev);
            SDL_GPUTexture* swap = nullptr;
            if (cb && SDL_WaitAndAcquireGPUSwapchainTexture(cb, w, &swap, nullptr, nullptr) && swap) {
                SDL_GPUColorTargetInfo t{};
                t.texture = swap;
                t.clear_color = {0.1f, 0.6f, 0.2f, 1.0f};
                t.load_op = SDL_GPU_LOADOP_CLEAR;
                t.store_op = SDL_GPU_STOREOP_STORE;
                SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cb, &t, 1, nullptr));
                presented = true;
            }
            if (cb) SDL_SubmitGPUCommandBuffer(cb);
        }
        check(presented, "SDL_GPU acquires the swapchain and presents");
        SDL_WaitForGPUIdle(dev);
        SDL_ReleaseWindowFromGPUDevice(dev, w);
    }
    SDL_DestroyGPUDevice(dev);
}

// Vulkan: an instance and a surface on the window (what the Vulkan backend
// starts with), through SDL's loader.
void useVulkan(SDL_Window* w) {
    auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    Uint32 n = 0;
    const char* const* ext = SDL_Vulkan_GetInstanceExtensions(&n);
    if (!getProc || !ext) {
        skip(std::string("Vulkan surface (no Vulkan loader: ") + SDL_GetError() + ")");
        return;
    }
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(getProc(nullptr, "vkCreateInstance"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = n;
    ci.ppEnabledExtensionNames = ext;
    VkInstance inst = VK_NULL_HANDLE;
    if (!createInstance || createInstance(&ci, nullptr, &inst) != VK_SUCCESS) {
        skip("Vulkan surface (vkCreateInstance failed)");
        return;
    }
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const bool ok = SDL_Vulkan_CreateSurface(w, inst, nullptr, &surface);
    check(ok, std::string("a Vulkan surface on the handed-over window") + (ok ? "" : std::string(": ") + SDL_GetError()));
    if (surface) SDL_Vulkan_DestroySurface(inst, surface, nullptr);
    auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(getProc(inst, "vkDestroyInstance"));
    if (destroyInstance) destroyInstance(inst, nullptr);
}

// OpenGL: the core's OpenGL3 device with this module's (shimmed) context calls, as gd01 makes it.
void useOpenGl(SDL_Window* w) {
    gr::DeviceCreateInfo info;
    info.backend = gr::Backend::OpenGL3;
    info.window = w;
    info.gl.createContext = [](SDL_Window* win) -> void* { return SDL_GL_CreateContext(win); };
    info.gl.destroyContext = [](void* c) { SDL_GL_DestroyContext(static_cast<SDL_GLContext>(c)); };
    info.gl.swapWindow = [](SDL_Window* win) { return SDL_GL_SwapWindow(win); };
    std::string error;
    std::unique_ptr<gr::Device> dev = gr::createDevice(info, error);
    check(dev != nullptr, "the core's OpenGL3 device on the OpenGL window again" + (dev ? std::string() : ": " + error));
    if (!dev) return;
    gr::TextureDesc td;
    td.width = td.height = 4, td.usage = gr::kSampledTexture | gr::kColorTarget;
    gr::Texture t = dev->createTexture(td);
    dev->beginFrame();
    gr::PassDesc p;
    p.colorCount = 1;
    p.colors[0].texture = t;
    p.colors[0].clear[0] = 1, p.colors[0].clear[3] = 1;
    dev->beginPass(p);
    dev->endPass();
    uint8_t px[4] = {};
    gr::Readback rb = dev->readTexture(t, {0, 0, 1, 1});
    check(rb && dev->readbackData(rb, px, 4) && px[0] == 255 && px[1] == 0, "the OpenGL3 device draws (a cleared target read back)");
    dev->present(t, {0, 0, 4, 4}, gr::Filter::Nearest);
    dev->endFrame();
}

}  // namespace

extern "C" GD_MODULE_EXPORT int gd_game_run(const GdHost* host) {
    if (!host || host->abi_version != GD_HOST_ABI_VERSION || !host->window) return 100;
    gdshim_begin(host);
    if (!SDL_Init(SDL_INIT_VIDEO)) return 101;
    SDL_Log("handover: launcher %s, video driver %s", host->launcher_version, SDL_GetCurrentVideoDriver());
    check(GD_HOST_HAS(host, request_window), "the launcher offers request_window");

    // 1. A game before gd-engine-core: an OpenGL window, nothing asked.
    SDL_Window* launcherGl = static_cast<SDL_Window*>(host->window);
    SDL_Window* w = SDL_CreateWindow("gd-test-handover", 640, 360, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    check(w == launcherGl && (SDL_GetWindowFlags(w) & SDL_WINDOW_OPENGL) && host->gl_context,
          "an OpenGL window, as every module had it: the launcher's own");
    check(windowApiProperty() == "opengl3", "the shim says the window is for opengl3");
    int w0 = 0, h0 = 0;
    SDL_GetWindowSize(w, &w0, &h0);

    // 2. SDL_GPU.
    w = windowFor(w, gr::Backend::SdlGpu);
    int w1 = 0, h1 = 0;
    if (w) SDL_GetWindowSize(w, &w1, &h1);
    check(w && w == host->window && !(SDL_GetWindowFlags(w) & SDL_WINDOW_OPENGL) && !host->gl_context &&
              gdshim_window_api() == GD_WINDOW_API_SDL_GPU,
          "SDL_GPU: a window without OpenGL, the launcher's context released");
    check(w1 == w0 && h1 == h0, "the window keeps its size (" + std::to_string(w1) + "x" + std::to_string(h1) + ")");
    check(windowApiProperty() == "sdl3", "the shim says the window is for sdl3");
    {
        gr::DeviceCreateInfo info;
        info.backend = gr::Backend::OpenGL3;
        info.window = w;
        std::string error;
        auto dev = gr::createDevice(info, error);
        check(!dev && error.find("made for sdl3") != std::string::npos, "createDevice refuses OpenGL3 on it: " + error);
    }
    const bool noContext = SDL_GL_CreateContext(w) == nullptr;
    check(noContext, std::string("no GL context on it: ") + SDL_GetError());
    {
        // The game's own hidden window (a shader test, say) is not the
        // launcher's: the check leaves it alone.
        gr::prepareWindow(gr::Backend::OpenGL3, false);
        SDL_Window* own = SDL_CreateWindow("gd-test-handover hidden", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        gr::DeviceCreateInfo info;
        info.backend = gr::Backend::OpenGL3;
        info.window = own;
        std::string error;
        auto dev = own ? gr::createDevice(info, error) : nullptr;
        check(own && own != host->window && dev && gdshim_window_api() == GD_WINDOW_API_SDL_GPU,
              "an OpenGL3 device on the game's own hidden window meanwhile" + (dev ? std::string() : ": " + error));
        dev.reset();
        if (own) SDL_DestroyWindow(own);
    }
    if (w) useSdlGpu(w);

    // 3. Vulkan.
    w = windowFor(w, gr::Backend::Vulkan);
    const bool vk = w && (SDL_GetWindowFlags(w) & SDL_WINDOW_VULKAN);
    if (vk) {
        check(w == host->window && !host->gl_context && windowApiProperty() == "vulkan", "Vulkan: a window made with SDL_WINDOW_VULKAN");
        useVulkan(w);
    } else {
        // No Vulkan loader: the launcher keeps the window as it was.
        check(w == host->window && windowApiProperty() == "sdl3", "no Vulkan window here: the window stays as it was");
        skip(std::string("Vulkan window (") + SDL_GetError() + ")");
    }

    // 4. Back to OpenGL (a game whose backend failed falls back so).
    w = windowFor(w, gr::Backend::OpenGL3);
    check(w && w == host->window && (SDL_GetWindowFlags(w) & SDL_WINDOW_OPENGL) && host->gl_context &&
              windowApiProperty() == "opengl3" && gdshim_window_api() == GD_WINDOW_API_OPENGL,
          "OpenGL again: an OpenGL window with the launcher's context");
    if (w) useOpenGl(w);

    // 5. Leave it made for SDL_GPU: the launcher takes it back after we return.
    w = windowFor(w, gr::Backend::SdlGpu);
    check(w && !(SDL_GetWindowFlags(w) & SDL_WINDOW_OPENGL), "SDL_GPU again, left so for the launcher to take back");

    // 6. Restart Now (GDUL-028): the module knows it runs in the launcher, asks
    // for a restart once and is run again as a new Play.
    const char* hosted = SDL_getenv("GD_LAUNCHER_HOSTED");
    check(hosted && hosted[0] == '1', "GD_LAUNCHER_HOSTED is 1 inside the launcher");
    const bool again = SDL_getenv("GDUL_TEST_RESTARTED") != nullptr;
    if (again) SDL_Log("handover: run again after Restart Now");
    if (!again && SDL_getenv("GDUL_TEST_RESTART_ONCE") && g_failed == 0) {
        gdshim_setenv("GDUL_TEST_RESTARTED", "1");
        SDL_Log("handover: asking for a restart");
        gdshim_end();
        return GD_GAME_EXIT_RESTART;
    }

    SDL_Log("handover: %d failed", g_failed);
    if (host->log) host->log(host->log_user, ("hand-over test: " + std::to_string(g_failed) + " failed").c_str());
    gdshim_end();
    return g_failed;
}
