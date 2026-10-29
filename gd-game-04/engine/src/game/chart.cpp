// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/chart.h"

#include <stdexcept>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qb.h"
#include "data/qbkey.h"

namespace {

const char* const kDiff[kDifficulties] = {"Easy", "Medium", "Hard", "Expert"};
// The part infixes of the section names [C data]: notes "<song>_song<infix>_<Diff>",
// phrases "<song><infix>_<Diff>_Star" / "_StarBattleMode".
const char* const kPartInfix[size_t(Part::Count)] = {"", "_rhythm", "_guitarcoop", "_rhythmcoop"};

const qb::Array* array(const qb::File& f, const std::string& name) {
    const qb::Value* v = f.find(qbkey::key(name));
    if (!v) return nullptr;
    if (v->type != qb::Type::Array || !v->arr) throw std::runtime_error(name + " is not an array");
    return v->arr.get();
}

int32_t intOf(const qb::Value& v) {
    if (v.type == qb::Type::Int) return v.i;
    if (v.type == qb::Type::Float) return int32_t(v.f[0]);
    throw std::runtime_error("chart: a number was expected");
}

// Arrays of int arrays: [[a, b, c], ...]
std::vector<std::vector<int32_t>> rows(const qb::Array* a, size_t width, const std::string& name, size_t maxWidth = 0) {
    std::vector<std::vector<int32_t>> out;
    if (!a) return out;
    if (!maxWidth) maxWidth = width;
    for (const qb::Value& r : a->values) {
        if (r.type != qb::Type::Array || !r.arr || r.arr->values.size() < width || r.arr->values.size() > maxWidth)
            throw std::runtime_error(name + ": a row of " + std::to_string(width) + " numbers was expected");
        std::vector<int32_t> row;
        for (const qb::Value& x : r.arr->values) row.push_back(intOf(x));
        out.push_back(std::move(row));
    }
    return out;
}

std::vector<ChartPhrase> phrases(const qb::File& f, const std::string& name, bool withNotes) {
    std::vector<ChartPhrase> out;
    for (auto& r : rows(array(f, name), withNotes ? 3 : 2, name)) out.push_back(ChartPhrase{r[0], r[1], withNotes ? r[2] : 0});
    return out;
}

std::vector<ChartEvent> events(const qb::File& f, const std::string& name) {
    std::vector<ChartEvent> out;
    // (time, note, length) or (time, note, length, value) [C data: 4-wide
    // rows in some drums / performance / lightshow / anim / crowd arrays].
    for (auto& r : rows(array(f, name), 3, name, 4)) out.push_back(ChartEvent{r[0], r[1], r[2], r.size() > 3 ? r[3] : -1});
    return out;
}

}  // namespace

const char* partName(Part p) {
    switch (p) {
    case Part::Guitar: return "guitar";
    case Part::Rhythm: return "rhythm";
    case Part::GuitarCoop: return "guitarcoop";
    case Part::RhythmCoop: return "rhythmcoop";
    default: return "?";
    }
}

const char* difficultyName(int d) { return d >= 0 && d < kDifficulties ? kDiff[d] : "?"; }

Chart loadChart(const Disc& disc, const std::string& song) {
    Pak pak = Pak::load(disc, "DATA/COMPRESSED/SONGS/" + song + "_song.pak.xen");
    const PakEntry* e = pak.findType(qbkey::key("songs\\" + song + ".mid.qb"), qbkey::key(".qb"));
    if (!e) throw std::runtime_error("no songs\\" + song + ".mid.qb in its pak");
    auto span = pak.data(*e);
    qb::File f = qb::decode(span.first, span.second);

    Chart c;
    c.song = song;
    for (size_t p = 0; p < size_t(Part::Count); ++p) {
        for (int d = 0; d < kDifficulties; ++d) {
            const std::string notesName = song + "_song" + kPartInfix[p] + "_" + kDiff[d];
            if (const qb::Array* a = array(f, notesName)) {
                if (a->values.size() % 3) throw std::runtime_error(notesName + ": not (time, length, frets) triples");
                auto& out = c.parts[p].notes[size_t(d)];
                out.reserve(a->values.size() / 3);
                for (size_t i = 0; i < a->values.size(); i += 3) {
                    ChartNote n;
                    n.time = intOf(a->values[i]);
                    n.length = intOf(a->values[i + 1]);
                    const int32_t mask = intOf(a->values[i + 2]);
                    if (mask & ~0x3f) throw std::runtime_error(notesName + ": unknown fret mask bits");
                    n.frets = uint8_t(mask & 0x1f);
                    n.flag20 = (mask & 0x20) != 0;
                    out.push_back(n);
                }
            }
            const std::string base = song + kPartInfix[p] + "_" + kDiff[d];
            c.parts[p].star[size_t(d)] = phrases(f, base + "_Star", true);
            c.parts[p].starBattle[size_t(d)] = phrases(f, base + "_StarBattleMode", true);
        }
    }
    c.faceOffP1 = phrases(f, song + "_FaceOffP1", false);
    c.faceOffP2 = phrases(f, song + "_FaceOffP2", false);
    c.bossP1 = phrases(f, song + "_BossBattleP1", false);
    c.bossP2 = phrases(f, song + "_BossBattleP2", false);
    for (auto& r : rows(array(f, song + "_timesig"), 3, "timesig")) c.timesig.push_back(TimeSig{r[0], r[1], r[2]});
    if (const qb::Array* a = array(f, song + "_fretbars"))
        for (const qb::Value& v : a->values) c.fretbars.push_back(intOf(v));
    if (const qb::Array* a = array(f, song + "_markers")) {
        for (const qb::Value& v : a->values) {
            if (v.type != qb::Type::Struct || !v.st) continue;
            ChartMarker m;
            if (const qb::Value* t = v.st->find(qbkey::key("time"))) m.time = intOf(*t);
            if (const qb::Value* t = v.st->find(qbkey::key("marker"))) m.text = t->key;
            c.markers.push_back(m);
        }
    }
    c.cameras = events(f, song + "_cameras_notes");
    c.lightshow = events(f, song + "_lightshow_notes");
    c.crowd = events(f, song + "_crowd_notes");
    c.drums = events(f, song + "_drums_notes");
    c.anim = events(f, song + "_anim_notes");
    c.scriptsNotes = events(f, song + "_scripts_notes");
    c.triggers = events(f, song + "_triggers_notes");
    c.performanceNotes = events(f, song + "_performance_notes");
    return c;
}
