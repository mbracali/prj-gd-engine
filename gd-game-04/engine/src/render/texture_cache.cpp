// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/texture_cache.h"
#include "render/image_enhance.h"
#include "render/texture_codec.h"
#include <atomic>
#include <thread>
#include <unordered_map>

#include <SDL3/SDL_log.h>

#include <exception>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "data/scene.h"
#include "data/texture.h"

TextureCache::~TextureCache() {
    for (auto& [k, e] : textures_)
        if (e.owner) dev_.destroyTexture(e.id);
    for (auto& [k, e] : ctxTextures_)
        if (e.owner) dev_.destroyTexture(e.id);
}

void TextureCache::size(uint32_t context, uint32_t key, int& w, int& h) const {
    auto it = context ? ctxTextures_.find(ctxKey(context, key)) : ctxTextures_.end();
    if (it == ctxTextures_.end()) return size(key, w, h);
    w = it->second.w, h = it->second.h;
}

int TextureCache::loadContextImages(const Disc& disc, const std::string& pakPath, uint32_t context) {
    if (auto it = contexts_.find(context); it != contexts_.end()) return it->second;
    int n = 0;
    try {
        const auto pakShared = Pak::shared(disc, pakPath);
        for (const PakEntry& e : pakShared->entries()) {
            if (e.type != qbkey::key(".img")) continue;
            auto [p, size] = pakShared->data(e);
            const gh3tex::Texture t = gh3tex::decodeImg(p, size);
            if (!t.supported || t.levels.empty()) continue;
            std::string error;
            auto levels = t.texLevels();
            const gfx::TextureId id = dev_.createTexture(t.desc(), levels.data(), error);
            if (!id) {
                SDL_Log("textures: %s in %s: %s", qbkey::name(e.fullName).c_str(), pakPath.c_str(), error.c_str());
                continue;
            }
            // Scripts name an .img by its full name or its short one [C data].
            ctxTextures_[ctxKey(context, e.fullName)] = Entry{id, t.width, t.height, true};
            if (e.name && e.name != e.fullName) ctxTextures_[ctxKey(context, e.name)] = Entry{id, t.width, t.height, false};
            ++n;
        }
    } catch (const std::exception& ex) {
        SDL_Log("textures: %s: %s", pakPath.c_str(), ex.what());
        n = -1;
    }
    contexts_[context] = n;
    return n;
}

void TextureCache::size(uint32_t key, int& w, int& h) const {
    auto it = textures_.find(key);
    w = it == textures_.end() ? 0 : it->second.w;
    h = it == textures_.end() ? 0 : it->second.h;
}

bool TextureCache::loadImage(const Disc& disc, const std::string& path, uint32_t key) {
    if (textures_.count(key)) return true;
    try {
        const gh3tex::Texture t = gh3tex::decodeImg(disc.read(path));
        if (!t.supported || t.levels.empty()) return false;
        std::string error;
        auto levels = t.texLevels();
        gfx::TextureId id = dev_.createTexture(t.desc(), levels.data(), error);
        if (!id) {
            SDL_Log("textures: %s: %s", path.c_str(), error.c_str());
            return false;
        }
        textures_[key] = Entry{id, t.width, t.height, true};
        return true;
    } catch (const std::exception& ex) {
        SDL_Log("textures: %s: %s", path.c_str(), ex.what());
        return false;
    }
}

namespace {
struct Sharp {
    std::vector<uint8_t> rgba;
    std::vector<std::vector<uint8_t>> chain;
    int w = 0, h = 0;
    bool ok = false;
};
}  // namespace

int TextureCache::loadPak(const Disc& disc, const std::string& pakPath) {
    int n = 0;
    try {
        const auto pakShared = Pak::shared(disc, pakPath);
        const Pak& pak = *pakShared;
        // Enhanced > Sharper Text & Sprites (gd02's, from the next start): each
        // 2D texture up to 512 px decoded, upscaled (image_enhance) and
        // uploaded as RGBA8 with its own mips, worked out on every core first;
        // the size the menus use stays.
        std::unordered_map<uint32_t, Sharp> sharpDone;
        if (sharp) {
            std::vector<std::pair<uint32_t, gh3tex::Texture>> todo;
            auto want = [&](uint32_t key, gh3tex::Texture t) {
                if (t.supported && !t.levels.empty() && t.kind == gfx::TexKind::Tex2D && t.width <= 512 && t.height <= 512)
                    todo.emplace_back(key, std::move(t));
            };
            for (const PakEntry& e : pak.entries()) {
                auto [p, size] = pak.data(e);
                try {
                    if (e.type == qbkey::key(".tex"))
                        for (gh3tex::Texture& t : gh3tex::decodeTexDict(p, size)) want(t.key, std::move(t));
                    else if (e.type == qbkey::key(".img"))
                        want(e.fullName, gh3tex::decodeImg(p, size));
                } catch (const std::exception&) {
                }
            }
            std::vector<Sharp> out(todo.size());
            std::atomic<size_t> next{0};
            auto work = [&] {
                for (size_t i; (i = next++) < todo.size();) {
                    const gh3tex::Texture& t = todo[i].second;
                    Sharp& sh = out[i];
                    sh.w = t.width, sh.h = t.height;
                    const bool ok = t.format == gfx::TexFormat::RGBA8
                                        ? (sh.rgba.assign(t.levels[0].begin(), t.levels[0].end()), sh.rgba.size() >= size_t(sh.w) * size_t(sh.h) * 4)
                                        : gfx::decodeToRgba8(t.format, t.levels[0].data(), t.levels[0].size(), sh.w, sh.h, sh.rgba);
                    sh.ok = ok && gfx::enhanceImage(sh.rgba, sh.w, sh.h);
                    if (sh.ok) sh.chain = gfx::mipChain(sh.rgba, sh.w, sh.h);
                }
            };
            std::vector<std::thread> pool;
            const unsigned n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
            for (unsigned k = 1; k < n; ++k) pool.emplace_back(work);
            work();
            for (std::thread& th : pool) th.join();
            for (size_t i = 0; i < todo.size(); ++i) sharpDone.emplace(todo[i].first, std::move(out[i]));
        }
        auto upload = [&](uint32_t key, const gh3tex::Texture& t) {
            if (!t.supported || t.levels.empty() || textures_.count(key)) return;
            std::string error;
            auto levels = t.texLevels();
            gfx::TextureId id;
            // Enhanced > Sharper Text & Sprites (gd02's, from the next start):
            // a 2D texture up to 512 px decoded, upscaled (image_enhance) and
            // uploaded as RGBA8 with its own mips; the size the menus use stays.
            if (auto it = sharpDone.find(key); it != sharpDone.end() && it->second.ok) {
                const Sharp& sh = it->second;
                std::vector<gfx::TexLevel> lv{{sh.rgba.data(), sh.rgba.size()}};
                for (const auto& m : sh.chain) lv.push_back({m.data(), m.size()});
                id = dev_.createTexture(gfx::TextureDesc{sh.w, sh.h, int(lv.size()), gfx::TexFormat::RGBA8, gfx::TexKind::Tex2D}, lv.data(), error);
                if (id) ++sharpened;
            }
            if (!id) id = dev_.createTexture(t.desc(), levels.data(), error);
            if (!id) {
                SDL_Log("textures: %s in %s: %s", qbkey::name(key).c_str(), pakPath.c_str(), error.c_str());
                return;
            }
            textures_[key] = Entry{id, t.width, t.height, true};
            ++n;
        };
        for (const PakEntry& e : pak.entries()) {
            auto [p, size] = pak.data(e);
            if (e.type == qbkey::key(".tex")) {
                for (const gh3tex::Texture& t : gh3tex::decodeTexDict(p, size)) upload(t.key, t);
            } else if (e.type == qbkey::key(".img")) {
                upload(e.fullName, gh3tex::decodeImg(p, size));
                // Scripts name an .img by its full name or its short one [C data].
                if (e.name != e.fullName && textures_.count(e.fullName) && !textures_.count(e.name))
                    textures_[e.name] = Entry{textures_[e.fullName].id, textures_[e.fullName].w, textures_[e.fullName].h, false};
            } else if (e.type == qbkey::key(".scn") || e.type == qbkey::key(".skin") || e.type == qbkey::key(".mdl")) {
                const scene::Kind kind = e.type == qbkey::key(".skin") ? scene::Kind::Skin
                                         : e.type == qbkey::key(".mdl") ? scene::Kind::Mdl
                                                                        : scene::Kind::Scn;
                for (const scene::Material& m : scene::parse(p, size, kind).materials)
                    if (m.key && !m.textures.empty()) materials_.emplace(m.key, m.textures.front());
            }
        }
    } catch (const std::exception& ex) {
        SDL_Log("textures: %s: %s", pakPath.c_str(), ex.what());
        return -1;
    }
    return n;
}
