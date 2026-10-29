// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/song_db.h"

#include "data/dta.h"
#include "data/game_data.h"

namespace {

// The values of (key v0 v1 ...) or (key (v0 v1 ...)).
const DataNode* valueList(const DataNode* entry) {
    if (!entry || entry->children.size() < 2) return nullptr;
    return entry->children[1].isList() ? &entry->children[1] : nullptr;
}

std::vector<float> floats(const DataNode* entry) {
    std::vector<float> out;
    if (const DataNode* v = valueList(entry))
        for (size_t i = 0; i < v->children.size(); ++i) out.push_back(v->floatAt(i));
    return out;
}

std::vector<int> ints(const DataNode& v) {
    std::vector<int> out;
    if (v.isList()) {
        for (size_t i = 0; i < v.children.size(); ++i) out.push_back(v.intAt(i));
    } else if (v.type == DataNode::Type::Int) {
        out.push_back(v.i);
    }
    return out;
}

std::optional<SongAudio> readAudio(const DataNode* block) {
    if (!block) return std::nullopt;
    const DataNode* name = block->find("name");
    const DataNode* midi = block->find("midi_file");
    if (!name || !midi) return std::nullopt;
    SongAudio a;
    a.audioPath = name->textAt(1) + ".vgs";
    a.midiPath = midi->textAt(1);
    if (const DataNode* tracks = valueList(block->find("tracks"))) {
        for (const DataNode& t : tracks->children)
            if (t.isList() && t.children.size() >= 2 && t.children[0].isText())
                a.tracks[t.children[0].s] = ints(t.children[1]);
    }
    a.pans = floats(block->find("pans"));
    a.vols = floats(block->find("vols"));
    if (const DataNode* cores = valueList(block->find("cores"))) a.cores = ints(*cores);
    if (const DataNode* h = block->find("hopo_threshold")) a.hopoThresholdTicks = h->intAt(1);
    return a;
}

}  // namespace

std::vector<SongInfo> loadSongs(const GameData& data) {
    DataNode root = parseDtb(data.read("config/gen/songs.dtb"));
    resolveConditionals(root, {"HX_XBOX", "_SHIP"});
    std::vector<SongInfo> songs;
    for (const DataNode& entry : root.children) {
        if (!entry.isList() || entry.children.empty() || !entry.children[0].isText()) continue;
        std::optional<SongAudio> audio = readAudio(entry.find("song"));
        if (!audio) continue;

        SongInfo s;
        s.id = entry.children[0].s;
        if (const DataNode* n = entry.find("name")) s.name = n->textAt(1);
        if (const DataNode* a = entry.find("artist")) s.artist = a->textAt(1);
        s.audio = std::move(*audio);
        s.coopAudio = readAudio(entry.find("song_coop"));
        if (const DataNode* sp = valueList(entry.find("practice_speeds")))
            for (size_t i = 0; i < sp->children.size(); ++i) s.practiceSpeeds.push_back(sp->intAt(i));
        for (const char* b : {"song_practice_1", "song_practice_2", "song_practice_3"})
            if (std::optional<SongAudio> pa = readAudio(entry.find(b)); pa && data.exists(pa->audioPath)) s.blocks[b] = std::move(*pa);
        if (const DataNode* p = entry.find("preview"); p && p->children.size() >= 3) {
            s.previewStartMs = p->floatAt(1);
            s.previewEndMs = p->floatAt(2);
        }
        if (const DataNode* q = entry.find("quickplay"))
            if (const DataNode* v = q->find("venue")) s.venue = v->textAt(1);
        if (const DataNode* t = entry.find("anim_tempo"); t && t->children.size() > 1) {
            const DataNode& v = t->children[1];
            if (v.type == DataNode::Type::Int) s.animTempo = v.i;
            else if (v.isText()) s.animTempo = v.s == "kTempoFast" ? 2 : 1;  // macros.dta
        }
        if (s.name.empty()) s.name = s.id;
        if (data.exists(s.audio.audioPath) && data.exists(s.audio.midiPath)) songs.push_back(std::move(s));
    }
    return songs;
}
