// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/song_db.h"

#include <cctype>

#include "data/qbkey.h"
#include "script/globals.h"

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string str(const Globals& g, const qb::Struct& st, const char* field) {
    const qb::Value* v = g.field(st, qbkey::key(field));
    if (!v) return "";
    if (v->type == qb::Type::KeyPtr) v = g.resolve(*v);
    if (v && (v->type == qb::Type::String || v->type == qb::Type::WString)) return v->str;
    return "";
}

float num(const Globals& g, const qb::Struct& st, const char* field, float fallback) {
    const qb::Value* v = g.field(st, qbkey::key(field));
    if (!v) return fallback;
    return v->type == qb::Type::Float || v->type == qb::Type::Int ? v->number() : fallback;
}

uint32_t key(const Globals& g, const qb::Struct& st, const char* field) {
    const qb::Value* v = g.field(st, qbkey::key(field));
    return v && v->type == qb::Type::Key ? v->key : 0;
}

}  // namespace

bool SongDb::load(const Globals& g) {
    songs_.clear();
    const qb::Value* list = g.find(qbkey::key("gh3_songlist"));
    const qb::Value* props = g.find(qbkey::key("gh3_songlist_props"));
    if (!list || list->type != qb::Type::Array || !props || props->type != qb::Type::Struct) return false;
    for (const qb::Value& k : list->arr->values) {
        if (k.type != qb::Type::Key) continue;
        const qb::Value* p = g.field(*props->st, k.key);
        if (!p || p->type != qb::Type::Struct) continue;
        SongInfo s;
        s.checksum = k.key;
        s.name = str(g, *p->st, "name");
        s.title = str(g, *p->st, "title");
        s.artist = str(g, *p->st, "artist");
        s.year = str(g, *p->st, "year");
        s.countoff = str(g, *p->st, "countoff");
        s.originalArtist = num(g, *p->st, "original_artist", 0) != 0;
        s.rhythmTrack = num(g, *p->st, "rhythm_track", 0) != 0;
        s.gemOffset = int(num(g, *p->st, "gem_offset", 0));
        s.inputOffset = int(num(g, *p->st, "input_offset", 0));
        s.bandVolume = num(g, *p->st, "band_playback_volume", 0);
        s.guitarVolume = num(g, *p->st, "guitar_playback_volume", 0);
        s.singer = key(g, *p->st, "singer");
        s.keyboard = key(g, *p->st, "keyboard") == qbkey::key("true");
        s.hammerOnMeasureScale = num(g, *p->st, "hammer_on_measure_scale", 0);
        for (const qb::Item& it : p->st->items)
            if (it.name == 0 && it.value.type == qb::Type::Key && it.value.key == qbkey::key("use_coop_notetracks")) s.useCoopNotetracks = true;
        if (s.name.empty()) continue;
        songs_.push_back(std::move(s));
    }
    return !songs_.empty();
}

const SongInfo* SongDb::find(const std::string& name) const {
    const std::string n = lower(name);
    for (const SongInfo& s : songs_)
        if (lower(s.name) == n) return &s;
    return nullptr;
}
