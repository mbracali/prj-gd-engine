// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/highwaytest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "data/psp_gfx.h"
#include "data/str_file.h"
#include "data/umd.h"

namespace {

// COMMON.STR's chunk load addresses [C: chunk 0 by 65 name pointers; chunk 1
// by the track plane's sub-mesh pointers, which land on its blocks].
constexpr uint32_t kCommonBase[2] = {0x08F36600, 0x091C32E0};

}  // namespace

int highwayTest(const gd24::Umd& umd, const std::string& sheet) {
    std::vector<uint8_t> file;
    gd24::StrFile f;
    std::string error;
    const char* path = "PSP_GAME/USRDIR/STREAMS/COMMON.STR";
    if (!umd.read(path, file) || !f.parse(file.data(), file.size(), file.size(), &error)) {
        std::printf("highwaytest: FAIL %s: %s\n", path, error.c_str());
        return 1;
    }
    std::vector<gd24::PspTexture> textures;
    std::vector<gd24::PspMesh> meshes;
    for (size_t i = 0; i < 2 && i < f.chunks.size(); ++i) {
        gd24::HeapChunk c{file.data() + f.chunks[i].offset, f.chunks[i].size, kCommonBase[i]};
        for (auto& t : gd24::findTextures(c)) textures.push_back(std::move(t));
        for (auto& m : gd24::findMeshes(c)) meshes.push_back(std::move(m));
    }
    int failures = 0;
    std::printf("highwaytest: %zu textures, %zu meshes\n", textures.size(), meshes.size());
    static const char* const kPieces[] = {"track_plane_A", "plate_4a", "plate_4b", "plate_4c", "plate_4d", "beat_bar_a", "beat_bar2_a",
                                          "gem_4a", "gem_4b", "gem_4c", "gem_4d", "gem_4od", "gem_4a_crash", "OD_glow_ray_A"};
    for (const char* want : kPieces) {
        const gd24::PspMesh* m = nullptr;
        for (const auto& x : meshes)
            if (x.name == want) m = &x;
        if (!m) {
            std::printf("highwaytest: FAIL mesh %s not found\n", want), ++failures;
            continue;
        }
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        for (const auto& v : m->vertices) {
            const float p[3] = {v.x, v.y, v.z};
            for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], p[k]), hi[k] = std::max(hi[k], p[k]);
        }
        std::printf("highwaytest: ok   mesh %-14s 0x%08x vtype %06x, %zu prims, %3zu vertices, box (%.2f %.2f %.2f)..(%.2f %.2f %.2f)\n", want,
                    m->address, m->vtype, m->prims.size(), m->vertices.size(), double(lo[0]), double(lo[1]), double(lo[2]),
                    double(hi[0]), double(hi[1]), double(hi[2]));
    }
    // The gem textures: the most saturated colour of each.
    static const struct {
        const char* name;
        const char* colour;
        int r, g, b;
    } kGems[] = {{"PBT_Gem4A", "red", 1, 0, 0}, {"PBT_Gem4B", "yellow", 1, 1, 0}, {"PBT_Gem4C", "green", 0, 1, 0}, {"PBT_Gem4D", "blue", 0, 0, 1}};
    for (const auto& g : kGems) {
        const gd24::PspTexture* t = nullptr;
        for (const auto& x : textures)
            if (x.name == g.name) t = &x;
        if (!t) {
            std::printf("highwaytest: FAIL texture %s not found\n", g.name), ++failures;
            continue;
        }
        double sum[3] = {0, 0, 0};
        int n = 0;
        for (size_t i = 0; i + 3 < t->rgba.size(); i += 4) {
            const int r = t->rgba[i], gg = t->rgba[i + 1], b = t->rgba[i + 2];
            const int mx = std::max({r, gg, b}), mn = std::min({r, gg, b});
            if (t->rgba[i + 3] < 128 || mx < 80 || mx - mn < 80) continue;  // saturated, opaque pixels only
            sum[0] += r, sum[1] += gg, sum[2] += b, ++n;
        }
        const double r = n ? sum[0] / n : 0, gg = n ? sum[1] / n : 0, b = n ? sum[2] / n : 0;
        const double mx = std::max({r, gg, b});
        const bool ok = n > 50 && (r > mx * 0.6) == bool(g.r) && (gg > mx * 0.6) == bool(g.g) && (b > mx * 0.6) == bool(g.b);
        std::printf("highwaytest: %s texture %-10s %dx%d %d bpp: %d saturated pixels, mean (%.0f %.0f %.0f): %s\n", ok ? "ok  " : "FAIL",
                    g.name, t->width, t->height, t->bpp, n, r, gg, b, g.colour);
        failures += !ok;
    }
    if (!sheet.empty()) {
        int w = 0, h = 0;
        for (const auto& t : textures) w += t.width + 4, h = std::max(h, t.height);
        SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
        SDL_FillSurfaceRect(s, nullptr, SDL_MapSurfaceRGBA(s, 48, 64, 48, 255));
        int x = 0;
        for (const auto& t : textures) {
            SDL_Surface* ts = SDL_CreateSurfaceFrom(t.width, t.height, SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(t.rgba.data()), t.width * 4);
            SDL_Rect r{x, 0, t.width, t.height};
            SDL_BlitSurface(ts, nullptr, s, &r);
            SDL_DestroySurface(ts);
            x += t.width + 4;
        }
        std::printf("highwaytest: sheet %s: %s\n", sheet.c_str(), SDL_SaveBMP(s, sheet.c_str()) ? "saved" : SDL_GetError());
        SDL_DestroySurface(s);
    }
    std::printf("highwaytest: %d failures\n", failures);
    return failures ? 1 : 0;
}
