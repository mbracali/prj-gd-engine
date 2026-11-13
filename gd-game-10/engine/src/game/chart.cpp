// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/chart.h"

#include <algorithm>
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
    const qb::Array* a = array(f, name);
    // gh4 (Metallica): flat (time, packed) pairs, packed = velocity << 24 |
    // note << 16 | length [C data: every *_notes array; the velocity is 100
    // nearly everywhere].
    if (a && !a->values.empty() && a->values[0].type == qb::Type::Int) {
        if (a->values.size() % 2) throw std::runtime_error(name + ": (time, packed) pairs were expected");
        for (size_t i = 0; i + 1 < a->values.size(); i += 2) {
            const uint32_t p = uint32_t(intOf(a->values[i + 1]));
            out.push_back(ChartEvent{intOf(a->values[i]), int32_t((p >> 16) & 0xFF), int32_t(p & 0xFFFF), int32_t(p >> 24)});
        }
        return out;
    }
    for (auto& r : rows(a, 3, name, 4)) out.push_back(ChartEvent{r[0], r[1], r[2], r.size() > 3 ? r[3] : -1});
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
    const auto pakShared = Pak::shared(disc, "DATA/COMPRESSED/SONGS/" + song + "_song.pak.xen");
    const Pak& pak = *pakShared;
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
                auto& out = c.parts[p].notes[size_t(d)];
                // gh4 (Metallica): (time, packed) pairs, packed = flags << 16 |
                // length (ms); flags: frets 0x1f (green 0x01 .. orange 0x10),
                // 0x20 an open note [C 0x8214EE70], 0x40 a hammer-on (the chart's
                // own decision, D9) [C 0x8214EE70], 0x0f80 per-lane bits (all set
                // on guitar) and 0x2000 (drums) not used here yet.
                const bool gh4 = a->values.size() % 2 == 0 && a->values.size() >= 2 && (uint32_t(intOf(a->values[1])) >> 16) != 0;
                if (gh4) {
                    out.reserve(a->values.size() / 2);
                    for (size_t i = 0; i + 1 < a->values.size(); i += 2) {
                        ChartNote n;
                        n.time = intOf(a->values[i]);
                        const uint32_t packed = uint32_t(intOf(a->values[i + 1]));
                        n.length = int32_t(packed & 0xFFFF);
                        const uint32_t flags = packed >> 16;
                        n.frets = uint8_t(flags & 0x1f);
                        n.open = (flags & 0x20) != 0;
                        n.gh4 = true;
                        n.hopo = (flags & 0x40) != 0;
                        if (!n.frets && !n.open) continue;  // no lane at all [I]
                        // Two entries at one time are one chord whose frets
                        // sustain for different lengths (the gem expansion keeps a
                        // length per fret, as GH3's input_array) [C data: Master of
                        // Puppets hard, 99369 ms; I: the merge]; the longer one kept.
                        if (!out.empty() && out.back().time == n.time) {
                            ChartNote& b = out.back();
                            b.frets = uint8_t(b.frets | n.frets);
                            b.open = b.open || n.open;
                            b.length = std::max(b.length, n.length);
                            b.hopo = false;  // a chord is never a hammer-on [C 0x8214EE70]
                            continue;
                        }
                        out.push_back(n);
                    }
                } else {
                    if (a->values.size() % 3) throw std::runtime_error(notesName + ": not (time, length, frets) triples");
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
    if (const qb::Value* v = f.find(qbkey::key(song + "_scripts")); v && v->type == qb::Type::Array && v->arr)
        for (const qb::Value& e : v->arr->values) {
            if (e.type != qb::Type::Struct || !e.st) continue;
            Chart::TimedScript ts;
            if (const qb::Value* t = e.st->find(qbkey::key("time"))) ts.time = intOf(*t);
            if (const qb::Value* t = e.st->find(qbkey::key("scr"))) ts.script = t->key;
            c.scripts.push_back(ts);
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
