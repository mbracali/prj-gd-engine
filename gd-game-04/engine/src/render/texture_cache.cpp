// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/texture_cache.h"

#include <SDL3/SDL_log.h>

#include <exception>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "data/scene.h"
#include "data/texture.h"

TextureCache::~TextureCache() {
    for (auto& [k, e] : textures_) dev_.destroyTexture(e.id);
}

void TextureCache::size(uint32_t key, int& w, int& h) const {
    auto it = textures_.find(key);
    w = it == textures_.end() ? 0 : it->second.w;
    h = it == textures_.end() ? 0 : it->second.h;
}

int TextureCache::loadPak(const Disc& disc, const std::string& pakPath) {
    int n = 0;
    try {
        Pak pak = Pak::load(disc, pakPath);
        auto upload = [&](uint32_t key, const gh3tex::Texture& t) {
            if (!t.supported || t.levels.empty() || textures_.count(key)) return;
            std::string error;
            auto levels = t.texLevels();
            gfx::TextureId id = dev_.createTexture(t.desc(), levels.data(), error);
            if (!id) {
                SDL_Log("textures: %s in %s: %s", qbkey::name(key).c_str(), pakPath.c_str(), error.c_str());
                return;
            }
            textures_[key] = Entry{id, t.width, t.height};
            ++n;
        };
        for (const PakEntry& e : pak.entries()) {
            auto [p, size] = pak.data(e);
            if (e.type == qbkey::key(".tex")) {
                for (const gh3tex::Texture& t : gh3tex::decodeTexDict(p, size)) upload(t.key, t);
            } else if (e.type == qbkey::key(".img")) {
                upload(e.name, gh3tex::decodeImg(p, size));
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
