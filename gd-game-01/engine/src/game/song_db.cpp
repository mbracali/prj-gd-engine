// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/song_db.h"

#include "data/dta.h"
#include "data/game_data.h"

namespace {

std::vector<float> floats(const DataNode* list) {
    std::vector<float> out;
    if (!list || list->children.size() < 2 || !list->children[1].isList()) return out;
    const DataNode& values = list->children[1];
    for (size_t i = 0; i < values.children.size(); ++i) out.push_back(values.floatAt(i));
    return out;
}

}  // namespace

std::vector<SongInfo> loadSongs(const GameData& data) {
    DataNode root = parseDtb(data.read(dtbPath("config/songs.dta")));
    std::vector<SongInfo> songs;
    for (const DataNode& entry : root.children) {
        if (!entry.isList() || entry.children.empty() || !entry.children[0].isText()) continue;
        const DataNode* song = entry.find("song");
        const DataNode* midi = entry.find("midi_file");
        if (!song || !midi) continue;
        const DataNode* songName = song->find("name");
        if (!songName) continue;

        SongInfo s;
        s.id = entry.children[0].s;
        if (const DataNode* n = entry.find("name")) s.name = n->textAt(1);
        if (const DataNode* a = entry.find("artist")) s.artist = a->textAt(1);
        s.audioPath = songName->textAt(1) + ".vgs";
        s.midiPath = midi->textAt(1);
        s.pans = floats(song->find("pans"));
        s.vols = floats(song->find("vols"));
        if (const DataNode* cores = song->find("cores"); cores && cores->children.size() > 1) {
            for (size_t i = 0; i < cores->children[1].children.size(); ++i) s.cores.push_back(cores->children[1].intAt(i));
        }
        if (const DataNode* p = entry.find("preview"); p && p->children.size() >= 3) {
            s.previewStartMs = p->floatAt(1);
            s.previewEndMs = p->floatAt(2);
        }
        if (const DataNode* q = entry.find("quickplay")) {
            if (const DataNode* v = q->find("venue")) s.venue = v->textAt(1);
            if (const DataNode* c = q->find("character")) s.character = c->textAt(1);
            if (const DataNode* g = q->find("guitar")) s.guitar = g->textAt(1);
        }
        if (const DataNode* t = entry.find("anim_tempo"); t && t->children.size() > 1) {
            const DataNode& v = t->children[1];
            if (v.type == DataNode::Type::Int) s.animTempo = v.i;
            else if (v.isText()) s.animTempo = v.s == "kTempoFast" ? 2 : v.s == "kTempoMediumFast" ? 3 : 1;  // macros.dta
        }
        if (const DataNode* b = entry.find("band"))
            for (size_t i = 1; i < b->children.size(); ++i)
                if (b->children[i].isText()) s.band.push_back(b->children[i].s);
        if (s.name.empty()) s.name = s.id;
        if (data.exists(s.audioPath) && data.exists(s.midiPath)) songs.push_back(std::move(s));
    }
    return songs;
}
