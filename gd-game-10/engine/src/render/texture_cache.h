// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <future>
#include <list>
#include <memory>
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
    // Enhanced > Sharper Text & Sprites, read when the textures load (set by the app before).
    static inline bool sharp = false;
    int sharpened = 0;
    ~TextureCache();
    // Uploads the textures of a pak; returns how many, -1 on error (logged).
    int loadPak(const Disc& disc, const std::string& pakPath);
    // The same, the pak read and its textures decoded on a worker; poll()
    // uploads what is ready (the app calls it every frame; wait = true:
    // everything pending, now). A song's zone pak under the loading screen
    // (it had stopped the game for half a second).
    void loadPakAsync(const Disc& disc, const std::string& pakPath);
    void poll(bool wait = false);
    bool pending() const { return !pending_.empty() || !committing_.empty(); }
    // Uploads one loose image (an .img.xen, e.g. a loading screen) under
    // `key`; true if it is there (loaded now or before).
    bool loadImage(const Disc& disc, const std::string& path, uint32_t key);
    gfx::TextureId find(uint32_t key) const {
        auto it = textures_.find(key);
        return it == textures_.end() ? gfx::TextureId{} : it->second.id;
    }
    // The first texture of a material (scene files' materials, recorded by
    // loadPak), or 0.
    uint32_t materialTexture(uint32_t materialKey) const {
        auto it = materials_.find(materialKey);
        return it == materials_.end() ? 0 : it->second;
    }
    // Size of a texture (0 x 0 if unknown).
    void size(uint32_t key, int& w, int& h) const;
    // GH3's asset contexts (PushAssetContext context = <pak name key>): every
    // character body pak holds its own .img Mag_Photo, all under the same
    // key; the pak header's pak-name key (= the body's asset_context, e.g.
    // axel_1) tells them apart [C data: musician_body[].asset_context = the
    // .img entries' pak name; script create_newspaper_menu]. Uploads the
    // pak's .img entries (only: the results art, small) under `context`,
    // once; returns how many there are, -1 on error.
    int loadContextImages(const Disc& disc, const std::string& pakPath, uint32_t context);
    bool hasContext(uint32_t context) const { return contexts_.count(context) != 0; }
    // A texture under a context, or 0 (then the global one applies).
    gfx::TextureId find(uint32_t context, uint32_t key) const {
        if (!context) return find(key);
        auto it = ctxTextures_.find(ctxKey(context, key));
        return it == ctxTextures_.end() ? find(key) : it->second.id;
    }
    bool inContext(uint32_t context, uint32_t key) const { return context && ctxTextures_.count(ctxKey(context, key)); }
    void size(uint32_t context, uint32_t key, int& w, int& h) const;
    size_t count() const { return textures_.size(); }

private:
    struct Prepared;
    static std::shared_ptr<Prepared> prepare(const Disc& disc, const std::string& pakPath, bool sharpen);
    int commit(Prepared& pr, bool sliced = false);
    std::list<std::future<std::shared_ptr<Prepared>>> pending_;
    std::list<std::shared_ptr<Prepared>> committing_;  // ready, uploading a slice a frame
    struct Entry {
        gfx::TextureId id;
        int w = 0, h = 0;
        bool owner = true;  // false: a second key for a texture owned by another entry
    };
    gfx::RenderDevice& dev_;
    std::unordered_map<uint32_t, Entry> textures_;
    std::unordered_map<uint32_t, uint32_t> materials_;
    static uint64_t ctxKey(uint32_t context, uint32_t key) { return (uint64_t(context) << 32) | key; }
    std::unordered_map<uint64_t, Entry> ctxTextures_;
    std::unordered_map<uint32_t, int> contexts_;  // loaded contexts -> their image count
};
