// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "render/device.h"

class Disc;

// GPU textures of GH3's paks by name key: every texture of every .tex
// dictionary in a pak (keyed by the texture's key) and every .img (keyed by
// the pak entry's short name), uploaded once (L7: at load, not first draw).
class TextureCache {
public:
    explicit TextureCache(gfx::RenderDevice& dev) : dev_(dev) {}
    ~TextureCache();
    // Uploads the textures of a pak; returns how many, -1 on error (logged).
    int loadPak(const Disc& disc, const std::string& pakPath);
    gfx::TextureId find(uint32_t key) const {
        auto it = textures_.find(key);
        return it == textures_.end() ? gfx::TextureId{} : it->second.id;
    }
    // Size of a texture (0 x 0 if unknown).
    void size(uint32_t key, int& w, int& h) const;
    size_t count() const { return textures_.size(); }

private:
    struct Entry {
        gfx::TextureId id;
        int w = 0, h = 0;
    };
    gfx::RenderDevice& dev_;
    std::unordered_map<uint32_t, Entry> textures_;
};
