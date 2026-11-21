// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/song_config.h"

#include <cctype>

#include "data/beatmatch.h"
#include "data/dta.h"

namespace gd24 {

SongConfig readSongConfig(const Beatmatch& bm, const BeatmatchImage& im, const std::string& song) {
    SongConfig c;
    DtaImage dta(bm.image(im), im.size, Beatmatch::kBase);
    std::string name;
    for (char ch : song) name.push_back(char(std::tolower(static_cast<unsigned char>(ch))));
    for (const DtaNode& s : dta.arraysNamed(name, 3)) {
        const DtaNode* p = s.find("preview");
        if (!p || p->array.size() < 3) continue;  // the symbol in other places (midi names...)
        c.found = true;
        c.previewStartMs = int(p->array[1].number()), c.previewEndMs = int(p->array[2].number());
        if (const DtaNode* v = s.find("song_scroll_speed"); v && v->array.size() > 1) c.scrollSpeedMs = int(v->array[1].number());
        if (const DtaNode* v = s.find("initial_track"); v && v->array.size() > 1) c.initialTrack = int(v->array[1].number());
        break;
    }
    for (const DtaNode& y : dta.arraysNamed("y_per_second", 2))
        for (size_t k = 1; k < y.array.size(); ++k)
            if (y.array[k].type == DtaNode::Type::Array && y.array[k].array.size() == 2) {
                const int d = int(y.array[k].array[0].number());
                if (d >= 0 && d < 4) c.yPerSecond[d] = y.array[k].array[1].number();
            }
    for (const DtaNode& w : dta.arraysNamed("watcher", 2))
        if (const DtaNode* v = w.find("slop"); v && v->array.size() > 1) c.slopMs = int(v->array[1].number());
    return c;
}

}  // namespace gd24
