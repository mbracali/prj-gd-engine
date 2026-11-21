// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// World Tour's band logo: a CAP ("create a pattern") of layered decals, the
// game's composite texture drawn into the band logo's texture
// (CAS_Band_Logo_Details.textureasset, Global_Blank_Logo.dds) that the band
// name screen and the logo editor show (docs/FORMATS.md "Band logo").
//
// A layer [C 0x823AB4D0, the layer reader; defaults the globals
// default_cap_*]: texture (a CAG decal in cas_captextures.pak, by its short
// name), x_trans / y_trans (percent of the logo, default 0), x_scale /
// y_scale (percent, default 100), rot (milliradians, x 0.001), x_skew /
// y_skew (percent), a (alpha percent, x 2.55), color (a color_lookup_table
// entry's diffuse; default 250 250 250), flags: 1 | 2 kept, 4 the texture's
// alpha only (shader 2D_kCol_TexA_TexTransform), 8 / 16 mirror x / y,
// 32 kept. The layer is the decal over the logo transformed by these; the
// order of the transform's parts and the y direction are [I].
// Text layers (text / font) are not drawn yet.

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "data/pak.h"
#include "data/qbkey.h"
#include "data/texture.h"
#include "render/texture_codec.h"
#include "script/vm.h"
#include "ui/ui_system.h"

namespace ui {
namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

void sig(const qb::Value& v, std::string& out) {
    out += char('0' + int(v.type));
    out += std::to_string(v.i) + "," + std::to_string(v.f[0]) + "," + std::to_string(v.key) + v.str;
    if (v.st)
        for (const qb::Item& it : v.st->items) out += "{" + std::to_string(it.name) + ":", sig(it.value, out), out += "}";
    if (v.arr)
        for (const qb::Value& e : v.arr->values) out += "[", sig(e, out), out += "]";
}

}  // namespace

const UiSystem::CapDecal* UiSystem::capDecal(uint32_t texKey) {
    if (auto it = capDecals_.find(texKey); it != capDecals_.end()) return it->second.w ? &it->second : nullptr;
    CapDecal& d = capDecals_[texKey];
    // The layer names the decal by its path (tex/models/.../CAG_Icon036.img);
    // the pak holds it by its short name (cag_icon036) [C data].
    std::string n = qbkey::name(texKey);
    if (const size_t s = n.find_last_of("/\\"); s != std::string::npos) n = n.substr(s + 1);
    if (const size_t dot = n.find('.'); dot != std::string::npos) n = n.substr(0, dot);
    for (char& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    try {
        const auto pak = Pak::shared(disc_, "DATA/COMPRESSED/PAK/cas_captextures.pak.xen");
        const PakEntry* e = pak->findType(qbkey::key(n), K(".img"));
        if (!e) {
            SDL_Log("band logo: no decal %s", n.c_str());
            return nullptr;
        }
        auto [p, sz] = pak->data(*e);
        const gh3tex::Texture t = gh3tex::decodeImg(p, sz);
        if (t.levels.empty() || !gfx::decodeToRgba8(t.format, t.levels[0].data(), t.levels[0].size(), t.width, t.height, d.rgba)) {
            SDL_Log("band logo: decal %s (%s) not decoded", n.c_str(), t.sourceFormat.c_str());
            return nullptr;
        }
        d.w = t.width, d.h = t.height;
    } catch (const std::exception& ex) {
        SDL_Log("band logo: decal %s: %s", n.c_str(), ex.what());
        return nullptr;
    }
    return &d;
}

gfx::TextureId UiSystem::capLogoTexture() {
    static const bool legacy = SDL_getenv("GD07_CAP_LOGO_LEGACY") != nullptr;
    if (legacy) return {};
    const Globals& g = vm_.globals();
    // The logo being edited: cas_current_appearance.CAS_Band_Logo.cap
    // [C scripts SetCASAppearanceCAP, randomize_band_logo].
    const qb::Value* app = g.find(K("cas_current_appearance"));
    const qb::Value* part = app && app->st ? g.field(*app->st, K("CAS_Band_Logo")) : nullptr;
    const qb::Value* cap = part && part->st ? g.field(*part->st, K("cap")) : nullptr;
    if (!cap || !cap->arr || cap->arr->values.empty() || !cap->arr->values[0].st) return capTex_;
    std::string s;
    sig(*cap, s);
    if (s == capSig_ && capTex_) return capTex_;
    capSig_ = s;
    constexpr int N = 256;
    std::vector<float> img(size_t(N) * N * 4, 0.0f);  // premultiplied-free straight RGBA 0..1
    const qb::Struct& c0 = *cap->arr->values[0].st;
    const qb::Value* layers = g.field(c0, K("layers"));
    auto num = [&](const qb::Struct& l, const char* name, uint32_t def) {
        if (const qb::Value* v = g.field(l, K(name)); v && (v->type == qb::Type::Int || v->type == qb::Type::Float)) return v->number();
        const qb::Value* d = g.find(def);
        return d ? d->number() : 0.0f;
    };
    const qb::Value* table = g.find(K("color_lookup_table"));
    int drawn = 0, text = 0;
    for (const qb::Value& lv : layers && layers->arr ? layers->arr->values : std::vector<qb::Value>{}) {
        if (!lv.st) continue;
        const qb::Struct& l = *lv.st;
        const qb::Value* tv = g.field(l, K("texture"));
        if (!tv || !tv->key) {
            text += g.field(l, K("text")) != nullptr;
            continue;
        }
        const CapDecal* d = capDecal(tv->key);
        if (!d) continue;
        const float tx = num(l, "x_trans", K("default_cap_trans")) * 0.01f, ty = num(l, "y_trans", K("default_cap_trans")) * 0.01f;
        float sx = num(l, "x_scale", K("default_cap_scale")) * 0.01f, sy = num(l, "y_scale", K("default_cap_scale")) * 0.01f;
        const float rot = num(l, "rot", K("default_cap_rot")) * 0.001f;
        const float kx = num(l, "x_skew", K("default_cap_skew")) * 0.01f, ky = num(l, "y_skew", K("default_cap_skew")) * 0.01f;
        const float a = std::clamp(num(l, "a", K("default_cap_alpha")) * 2.55f / 255.0f, 0.0f, 1.0f);
        const qb::Value* fv = g.field(l, K("flags"));
        const int flags = fv ? int(fv->number()) : 0;
        if (flags & 8) sx = -sx;
        if (flags & 16) sy = -sy;
        float col[3] = {num(l, "", K("default_cap_red")), num(l, "", K("default_cap_green")), num(l, "", K("default_cap_blue"))};
        if (const qb::Value* cv = g.field(l, K("color")); cv && cv->key && table && table->st)
            if (const qb::Value* ce = g.field(*table->st, cv->key); ce && ce->st)
                if (const qb::Value* df = g.field(*ce->st, K("diffuse")); df && df->arr && df->arr->values.size() >= 3)
                    for (int i = 0; i < 3; ++i) col[i] = df->arr->values[i].number();
        for (float& v : col) v = std::clamp(v / 255.0f, 0.0f, 1.0f);
        if (std::fabs(sx) < 1e-4f || std::fabs(sy) < 1e-4f) continue;
        const float cr = std::cos(rot), sr = std::sin(rot);
        const float det = 1.0f - kx * ky;
        if (std::fabs(det) < 1e-4f) continue;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                // Logo point, centred, y down.
                const float px = (x + 0.5f) / N - 0.5f - tx, py = (y + 0.5f) / N - 0.5f - ty;
                // Undo the rotation, then the skew, then the scale.
                const float rx = cr * px + sr * py, ry = -sr * px + cr * py;
                const float qx = (rx - kx * ry) / det, qy = (ry - ky * rx) / det;
                const float u = qx / sx + 0.5f, v = qy / sy + 0.5f;
                if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
                // Bilinear sample of the decal.
                const float fx = u * d->w - 0.5f, fy = v * d->h - 0.5f;
                const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
                const float wx = fx - x0, wy = fy - y0;
                float t[4] = {0, 0, 0, 0};
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        const int xx = std::clamp(x0 + i, 0, d->w - 1), yy = std::clamp(y0 + j, 0, d->h - 1);
                        const float w = (i ? wx : 1 - wx) * (j ? wy : 1 - wy);
                        const uint8_t* s4 = &d->rgba[(size_t(yy) * d->w + xx) * 4];
                        for (int k = 0; k < 4; ++k) t[k] += w * s4[k] / 255.0f;
                    }
                float src[4];
                if (flags & 4) src[0] = col[0], src[1] = col[1], src[2] = col[2];
                else src[0] = col[0] * t[0], src[1] = col[1] * t[1], src[2] = col[2] * t[2];
                src[3] = a * t[3];
                float* dst = &img[(size_t(y) * N + x) * 4];
                const float oa = src[3] + dst[3] * (1 - src[3]);
                if (oa <= 0) continue;
                for (int k = 0; k < 3; ++k) dst[k] = (src[k] * src[3] + dst[k] * dst[3] * (1 - src[3])) / oa;
                dst[3] = oa;
            }
        ++drawn;
    }
    std::vector<uint8_t> out(size_t(N) * N * 4);
    for (size_t i = 0; i < out.size(); ++i) out[i] = uint8_t(std::lround(std::clamp(img[i], 0.0f, 1.0f) * 255.0f));
    if (capTex_) dev_.destroyTexture(capTex_);
    std::string err;
    const gfx::TexLevel lvl{out.data(), out.size()};
    capTex_ = dev_.createTexture({N, N, 1, gfx::TexFormat::RGBA8, gfx::TexKind::Tex2D}, &lvl, err);
    SDL_Log("band logo: %d layers composited%s", drawn, text ? (", " + std::to_string(text) + " text layers not drawn").c_str() : "");
    return capTex_;
}

}  // namespace ui
