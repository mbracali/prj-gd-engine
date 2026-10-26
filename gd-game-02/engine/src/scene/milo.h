// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/bytes.h"
#include "scene/objects.h"

class GameData;

// GH2's .milo_xbox scene files (docs/milo_and_textures.md):
//   container: u32 0xCBBEDEAF, u32 header size, u32 block count, u32 largest
//   block, u32 compressed size per block; blocks are raw deflate streams that
//   concatenate to the directory stream.
//   directory (version 25): class, name, two string-table hints, the object
//   table (class, name), then the directory's own record and one record per
//   object, each closed by 0xADDEADDE.
// An object that is itself a directory (RndDir lighter_flame in the crowd
// milos) may carry a complete directory inline in its record; it becomes a
// sub-directory of the object's directory, named after the object.

namespace scene {

class Directory {
public:
    std::string cls, name;          // e.g. "PanelDir", "track"
    uint32_t stringCount = 0, stringSize = 0;  // allocation hints from the header
    std::unique_ptr<Object> self;   // the directory's own record (typed as raw)
    std::vector<std::unique_ptr<Object>> objects;  // table order
    // Inline sub-directories, keyed by the object that holds each.
    std::vector<std::pair<std::string, std::unique_ptr<Directory>>> subdirs;
    Directory* parent = nullptr;

    // This directory's object `name` (exact, case-sensitive), or nullptr.
    Object* get(const std::string& name) const;
    // Searches this directory, then its inline sub-directories (depth first).
    Object* find(const std::string& name) const;
    template <class T> T* find(const std::string& name) const {
        Object* o = find(name);
        return o ? o->as<T>() : nullptr;
    }
    // Resolves a reference made by an object of this directory: this
    // directory and its sub-directories first, then the parents'.
    Object* resolve(const std::string& name) const;
    template <class T> T* resolve(const std::string& name) const {
        Object* o = resolve(name);
        return o ? o->as<T>() : nullptr;
    }
    // Every object of class `cls` here and in the sub-directories.
    std::vector<Object*> byClass(const std::string& cls) const;
    template <class T> std::vector<T*> all() const {
        std::vector<T*> v;
        forEach([&](Object& o) {
            if (T* t = o.as<T>()) v.push_back(t);
        });
        return v;
    }
    // Calls fn for every object here and in the sub-directories (not the
    // directories' own records).
    template <class F> void forEach(F&& fn) const {
        for (const auto& o : objects) fn(*o);
        for (const auto& [owner, d] : subdirs) d->forEach(fn);
    }

private:
    mutable std::map<std::string, Object*> index_;
};

struct MiloInfo {
    uint32_t magic = 0, headerSize = 0, blocks = 0, largestBlock = 0;
    size_t streamSize = 0;
};

class Milo {
public:
    // Parses a .milo_xbox file (compressed or a bare directory stream).
    // Throws std::runtime_error when the container or the directory
    // structure is broken; objects whose typed decoder fails are kept with
    // their error (Object::decoded == false).
    explicit Milo(const Bytes& file, std::string path = {});

    const std::string& path() const { return path_; }
    const MiloInfo& info() const { return info_; }
    Directory& root() { return *root_; }
    const Directory& root() const { return *root_; }

    Object* find(const std::string& name) const { return root_->find(name); }
    template <class T> T* find(const std::string& name) const { return root_->find<T>(name); }

private:
    std::string path_;
    MiloInfo info_;
    std::unique_ptr<Directory> root_;
};

// Loaded milos by archive path, so proxies shared by several directories
// (track_panel_2p's two GHTrackDirs, the HUD's meter directories) load once.
class MiloLibrary {
public:
    explicit MiloLibrary(const GameData& data) : data_(data) {}

    // Loads (or returns the cached) milo; see loadMilo. Throws on failure.
    Milo& load(const std::string& path);
    // The milo a proxy directory object refers to (its ObjectDir proxy file,
    // relative to the milo that holds it), or nullptr when the object is not
    // a proxy or its directory was stored inline. Throws if the file is broken.
    Milo* proxy(const Milo& owner, const Object& dirObject);
    const GameData& data() const { return data_; }

private:
    const GameData& data_;
    std::map<std::string, std::unique_ptr<Milo>> milos_;
};

// A file path stored in a milo (relative to the folder of the file holding
// it, e.g. "../track_new.milo" inside track/gen/track_panel_1p.milo_xbox)
// -> the archive path ("track/gen/track_new.milo_xbox").
std::string resolveMiloRelative(const std::string& fromArkPath, const std::string& rel);
// Same, checked against the archive: GH2's data still names some shared
// files under "og/" (old-gen) folders that ship as "ng/"; the engine swaps
// the two (0x821c9480 holds both strings). Returns the path that exists, or
// the plain resolution when neither does.
std::string findMiloRelative(const GameData& data, const std::string& fromArkPath, const std::string& rel);

// Inflates the container; returns the directory stream (a stream without the
// container magic is returned as is).
Bytes decompressMilo(const Bytes& file, MiloInfo* info = nullptr);

// Loads "track/gen/track.milo_xbox" style archive paths; also accepts the
// source spelling ("track/track.milo" -> "track/gen/track.milo_xbox").
// Throws std::runtime_error if missing or broken.
std::unique_ptr<Milo> loadMilo(const GameData& data, const std::string& arkPath);
// The archive path a source-style milo path maps to.
std::string miloArkPath(const std::string& path);

}  // namespace scene
