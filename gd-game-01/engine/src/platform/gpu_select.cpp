// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "platform/gpu_select.h"
#include "platform/paths.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
#include <unistd.h>
#endif

#ifdef _WIN32
// Read by the NVIDIA (Optimus) and AMD (PowerXpress / Switchable Graphics)
// drivers when the process starts: run on the dedicated GPU.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace gpu {

#if defined(__linux__) && !defined(__ANDROID__)

namespace {

std::vector<char*> gArgs;
bool gPreferred = false;  // this launch asked for the dedicated GPU

// Variables this module sets; cleared again for the fallback restart.
const char* const kVars[] = {"__NV_PRIME_RENDER_OFFLOAD", "__GLX_VENDOR_LIBRARY_NAME", "__VK_LAYER_NV_optimus",
                             "__EGL_VENDOR_LIBRARY_FILENAMES", "DRI_PRIME"};

std::string prefFile(const char* name) {
    return prefDir().empty() ? std::string(name) : prefDir() + name;
}

std::string readLine(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    return s;
}

struct Card {
    unsigned vendor = 0;
    bool bootVga = false;      // the GPU the firmware (and the desktop) starts on
    unsigned long long vram = 0;  // amdgpu only
    std::string pci;           // "0000:01:00.0"
};

std::vector<Card> cards() {
    std::vector<Card> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
        std::string n = e.path().filename().string();
        if (n.rfind("card", 0) != 0 || n.find('-') != std::string::npos) continue;  // skip connectors (card1-eDP-1)
        auto dev = e.path() / "device";
        Card c;
        c.vendor = unsigned(std::strtoul(readLine(dev / "vendor").c_str(), nullptr, 16));
        if (!c.vendor) continue;
        c.bootVga = readLine(dev / "boot_vga") == "1";
        c.vram = std::strtoull(readLine(dev / "mem_info_vram_total").c_str(), nullptr, 10);
        c.pci = std::filesystem::canonical(dev, ec).filename().string();
        out.push_back(c);
    }
    return out;
}

// NVIDIA cards are dedicated; AMD ones when they have real VRAM (APUs carve
// out 512 MB - 2 GB); Intel ones are taken as integrated.
bool dedicated(const Card& c) {
    if (c.vendor == 0x10DE) return true;
    if (c.vendor == 0x1002) return c.vram > (2ull << 30);
    return false;
}

// Driver + kernel: a remembered failure is retried after either changes.
std::string fingerprint() {
    return readLine("/proc/driver/nvidia/version") + "|" + readLine("/proc/sys/kernel/osrelease");
}

}  // namespace

void setArgs(int argc, char** argv) { gArgs.assign(argv, argv + argc), gArgs.push_back(nullptr); }

void preferDedicated() {
    if (const char* v = SDL_getenv("GD01_GPU"); v && std::string(v) == "system") return;
    for (const char* v : kVars)
        if (SDL_getenv(v)) return;  // the user (or a launcher) chose already
    std::error_code ec;
    if (std::filesystem::exists(prefFile("gpu_system"), ec) && readLine(prefFile("gpu_system")) != fingerprint())
        std::filesystem::remove(prefFile("gpu_system"), ec);  // drivers changed since: try again
    if (std::filesystem::exists(prefFile("gpu_system"), ec)) {
        SDL_Log("GPU: using the system's choice (the dedicated GPU failed before; delete %s to try it again)",
                prefFile("gpu_system").c_str());
        return;
    }
    if (std::filesystem::exists(prefFile("gpu_probe"), ec)) {
        // The last launch asked for the dedicated GPU and never got its graphics started.
        std::filesystem::rename(prefFile("gpu_probe"), prefFile("gpu_system"), ec);
        SDL_Log("GPU: the dedicated GPU did not start last time; using the system's choice");
        return;
    }

    if (const char* v = SDL_getenv("GD01_GPU"); v && std::string(v) == "testfail") {
        // Development aid: a dedicated GPU that cannot start, to exercise the fallback.
        setenv("__GLX_VENDOR_LIBRARY_NAME", "gd01_none", 1);
        setenv("__EGL_VENDOR_LIBRARY_FILENAMES", "/nonexistent/gd01_none.json", 1);
        gPreferred = true;
        std::ofstream(prefFile("gpu_probe")) << fingerprint() << "\n";
        return;
    }

    std::vector<Card> all = cards();
    if (all.size() < 2) return;
    const Card* boot = nullptr;
    const Card* best = nullptr;
    for (const Card& c : all) {
        if (c.bootVga) boot = &c;
        if (dedicated(c) && (!best || c.vram > best->vram)) best = &c;
    }
    if (!best || best == boot || (boot && dedicated(*boot))) return;  // already on a dedicated GPU

    if (best->vendor == 0x10DE && std::ifstream("/proc/driver/nvidia/version").good()) {
        // NVIDIA's own driver: PRIME render offload through glvnd.
        setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
        setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
        setenv("__VK_LAYER_NV_optimus", "NVIDIA_only", 1);
        for (const char* json : {"/usr/share/glvnd/egl_vendor.d/10_nvidia.json", "/etc/glvnd/egl_vendor.d/10_nvidia.json"})
            if (std::ifstream(json).good()) {
                setenv("__EGL_VENDOR_LIBRARY_FILENAMES", json, 1);
                break;
            }
    } else {
        // Mesa (amdgpu, nouveau, ...): DRI_PRIME=pci-0000_01_00_0.
        std::string tag = "pci-" + best->pci;
        for (char& ch : tag)
            if (ch == ':' || ch == '.') ch = '_';
        setenv("DRI_PRIME", tag.c_str(), 1);
    }
    gPreferred = true;
    std::ofstream(prefFile("gpu_probe")) << fingerprint() << "\n";
    SDL_Log("GPU: asking for the dedicated GPU (%s)", best->pci.c_str());
}

void started(bool ok, const std::string& glRenderer) {
    if (!gPreferred) return;
    std::error_code ec;
    bool software = glRenderer.find("llvmpipe") != std::string::npos || glRenderer.find("softpipe") != std::string::npos;
    if (ok && !software) {
        std::filesystem::remove(prefFile("gpu_probe"), ec);
        return;
    }
    SDL_Log("GPU: the dedicated GPU did not start (%s); restarting on the system's GPU",
            ok ? glRenderer.c_str() : SDL_GetError());
    std::filesystem::rename(prefFile("gpu_probe"), prefFile("gpu_system"), ec);
    for (const char* v : kVars) unsetenv(v);
    setenv("GD01_GPU", "system", 1);
    if (!gArgs.empty()) execv("/proc/self/exe", gArgs.data());
    SDL_Log("GPU: restart failed");
}

#else

void setArgs(int, char**) {}
void preferDedicated() {}
void started(bool, const std::string&) {}

#endif

}  // namespace gpu
