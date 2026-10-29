// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/selftest.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "audio/vgs_stream.h"
#include "data/dta.h"
#include "data/game_data.h"
#include "data/midi.h"
#include "game/autoplay.h"
#include "game/band_performer.h"
#include "game/key_script.h"
#include "game/song_db.h"
#include "game/song_setup.h"
#include "input/input_selftest.h"

namespace {

// Every .dtb in the archive decrypts and parses with every byte consumed.
int checkScripts(const GameData& data) {
    int ok = 0, bad = 0;
    for (const std::string& p : data.paths()) {
        if (p.size() < 4 || p.compare(p.size() - 4, 4, ".dtb") != 0) continue;
        try {
            parseDtb(data.read(p));
            ++ok;
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "  script %s: %s\n", p.c_str(), ex.what());
            ++bad;
        }
    }
    std::printf("%zu archive files; %d .dtb scripts decoded, %d failed\n", data.paths().size(), ok, bad);
    return bad;
}

// The .vgs header, and songs.dtb's per-channel lists.
bool checkAudio(const GameData& data, const SongAudio& a, const std::string& id) {
    try {
        std::vector<uint8_t> head(size_t(std::min<uint64_t>(data.fileSize(a.audioPath), 0x80)));
        data.readRange(a.audioPath, 0, head.data(), head.size());
        int channels = 0, rate = 0;
        probeVgs(head.data(), head.size(), channels, rate);
        size_t n = size_t(channels);
        if (a.pans.size() != n || a.vols.size() != n || a.cores.size() != n) {
            std::fprintf(stderr, "  %s: %s has %d channels, songs.dtb lists %zu pans / %zu vols / %zu cores\n", id.c_str(),
                         a.audioPath.c_str(), channels, a.pans.size(), a.vols.size(), a.cores.size());
            return false;
        }
        for (const auto& [instrument, list] : a.tracks)
            for (int c : list)
                if (c < 0 || c >= channels) {
                    std::fprintf(stderr, "  %s: %s channel %d out of range\n", id.c_str(), instrument.c_str(), c);
                    return false;
                }
        return true;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "  %s: %s: %s\n", id.c_str(), a.audioPath.c_str(), ex.what());
        return false;
    }
}

struct PartTotals {
    int charts = 0, gems = 0, failed = 0;
};


// ---------------------------------------------------------------- rule checks
// Focused checks of GH2's rules (docs/FINDINGS.md) on small synthetic charts
// at 125 BPM (480 ticks a beat: 1 tick = 1 ms, a 4/4 measure = 1920 ms),
// Expert, played with KeyScript input (GD03_KEYS's format).

struct NoteSpec {
    int tick;
    uint8_t frets;  // mask, bit 0 = green
    int len = 100;
};

[[maybe_unused]] constexpr uint8_t G = 1, R = 2, Y = 4, B = 8, O = 16;  // fret bits; O is unused so far

// Face-off sections: (start, end, player 0 or 1) on base+9 / base+10.
struct Section {
    int start, end, player;
};

Song makeSong(const std::vector<NoteSpec>& notes, const std::vector<std::pair<int, int>>& phrases = {}, int hopoTicks = 170,
              const std::vector<NoteSpec>& bass = {}, const std::vector<std::pair<int, int>>& bassPhrases = {},
              const std::vector<Section>& sections = {}) {
    MidiFile midi;
    midi.division = 480;
    MidiTrack tempo;
    MidiEvent t;
    t.status = 0xFF, t.data1 = 0x51, t.text = std::string("\x07\x53\x00", 3);  // 480000 us a beat
    tempo.events.push_back(t);
    midi.tracks.push_back(tempo);

    struct Raw { uint32_t tick; bool on; int note; };
    int last = 0;
    auto addPart = [&](const char* name, const std::vector<NoteSpec>& ns, const std::vector<std::pair<int, int>>& ps,
                       const std::vector<Section>& secs) {
        std::vector<Raw> raw;
        for (const NoteSpec& n : ns) {
            for (int f = 0; f < 5; ++f)
                if (n.frets & (1 << f)) raw.push_back({uint32_t(n.tick), true, 96 + f}), raw.push_back({uint32_t(n.tick + n.len), false, 96 + f});
            last = std::max(last, n.tick + n.len);
        }
        for (const auto& [a, b] : ps) raw.push_back({uint32_t(a), true, 103}), raw.push_back({uint32_t(b), false, 103});
        for (const Section& sec : secs)
            raw.push_back({uint32_t(sec.start), true, 105 + sec.player}), raw.push_back({uint32_t(sec.end), false, 105 + sec.player});
        // Note-offs first at a tick, then the face-off sections' note-ons, then the frets'.
        std::stable_sort(raw.begin(), raw.end(), [](const Raw& a, const Raw& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            if (a.on != b.on) return !a.on;
            return a.note > b.note;
        });
        MidiTrack part;
        part.name = name;
        for (const Raw& r : raw) {
            MidiEvent e;
            e.tick = r.tick, e.status = r.on ? 0x90 : 0x80, e.data1 = uint8_t(r.note), e.data2 = r.on ? 100 : 0;
            part.events.push_back(e);
        }
        midi.tracks.push_back(part);
    };
    addPart("PART GUITAR", notes, phrases, sections);
    if (!bass.empty()) addPart("PART BASS", bass, bassPhrases, {});
    MidiTrack events;
    events.name = "EVENTS";
    MidiEvent end;
    end.tick = uint32_t(last + 4000), end.status = 0xFF, end.data1 = 0x01, end.text = "[end]";
    events.events.push_back(end);
    midi.tracks.push_back(events);
    return buildSong(midi, hopoTicks);
}

struct Play {
    std::unique_ptr<Player> p;
    std::vector<PlayerEvent> ev;
    int count(PlayerEvent::Kind k, int gem = -2) const {
        return int(std::count_if(ev.begin(), ev.end(), [&](const PlayerEvent& e) { return e.kind == k && (gem == -2 || e.gem == gem); }));
    }
};

Play play(const Song& song, const Rules& rules, const std::string& script, float untilMs = 0,
          ControllerMode mode = ControllerMode::Guitar) {
    Play r;
    r.p = std::make_unique<Player>(song, Part::Guitar, Difficulty::Expert, rules, mode, false);
    KeyScript keys(script);
    if (!keys.valid()) throw std::runtime_error("bad key script: " + script);
    if (untilMs <= 0) untilMs = song.endMs;
    for (float t = 0; t <= untilMs; t += 1) {
        keys.feed(*r.p, t);
        r.p->update(t);
        for (const PlayerEvent& e : r.p->takeEvents()) r.ev.push_back(e);
    }
    return r;
}

using K = PlayerEvent::Kind;

// Two players on one synthetic song (docs/multiplayer.md), wired as the
// game does: co-op (guitar + bass, coop_config, the band, one Star Power
// meter), face-off (player_mask ownership) or pro face-off (both play
// every gem). Neither player fails on its own meter (app.cpp's noFail).
enum class TwoMode { Coop, FaceOff, ProFaceOff };
struct Play2 {
    std::unique_ptr<Player> p[2];
    std::unique_ptr<BandPerformer> band;
    StarMeter meter;
    std::vector<PlayerEvent> ev[2], bandEv;
    int failedAfter = -1;  // the players' penalties when the band failed
    int count(int who, PlayerEvent::Kind k) const {
        const auto& list = who < 0 ? bandEv : ev[who];
        return int(std::count_if(list.begin(), list.end(), [&](const PlayerEvent& e) { return e.kind == k; }));
    }
};

std::unique_ptr<Play2> play2(const Song& song, const Rules& rules, TwoMode mode, const std::string& keys0, const std::string& keys1,
                             float untilMs, Difficulty d0 = Difficulty::Expert, Difficulty d1 = Difficulty::Expert) {
    auto r = std::make_unique<Play2>();
    const bool coop = mode == TwoMode::Coop, faceOff = mode == TwoMode::FaceOff;
    const ScoreConfig* config = coop ? &rules.coop : &rules.solo;
    r->p[0] = std::make_unique<Player>(song, Part::Guitar, d0, rules, ControllerMode::Guitar, true, faceOff ? 0 : -1, config);
    r->p[1] = std::make_unique<Player>(song, coop ? Part::Bass : Part::Guitar, d1, rules, ControllerMode::Guitar, true,
                                       faceOff ? 1 : -1, config);
    r->p[1]->setNumber(1);
    normaliseScores(*r->p[0], *r->p[1]);
    if (coop) {
        r->band = std::make_unique<BandPerformer>(song, rules, d0, d1, "test");
        linkCoop(*r->p[0], *r->p[1], *r->band, r->meter);
    }
    KeyScript k0(keys0), k1(keys1);
    if (!k0.valid() || !k1.valid()) throw std::runtime_error("bad key script");
    for (float t = 0; t <= untilMs; t += 1) {
        k0.feed(*r->p[0], t);
        k1.feed(*r->p[1], t);
        r->p[0]->update(t);
        r->p[1]->update(t);
        if (r->band) r->band->poll(t);
        for (int i = 0; i < 2; ++i)
            for (const PlayerEvent& e : r->p[i]->takeEvents()) r->ev[i].push_back(e);
        if (r->band)
            for (const PlayerEvent& e : r->band->takeEvents()) {
                if (e.kind == K::Failed) r->failedAfter = r->p[0]->penalties() + r->p[1]->penalties();
                r->bandEv.push_back(e);
            }
    }
    return r;
}

// Strums at each tick with `fret` held from the start.
std::string strums(const std::vector<int>& ticks, const char* fret = "green") {
    std::string k = std::string("100:+") + fret;
    for (int t : ticks) k += "," + std::to_string(t) + ":strum";
    return k;
}

int runTwoPlayerChecks(const Rules& rules) {
    int failures = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
        failures += !ok;
    };
    {  // Co-op Star Power: one meter, both must engage, both drain it.
        Song s = makeSong({{1000, G}, {1300, G}, {12000, G}}, {{1000, 1400}}, 170, {{2000, G}, {2300, G}, {12500, G}}, {{2000, 2400}});
        const std::string k0 = strums({1000, 1300}) + ",3000:starpower";
        const std::string k1 = strums({2000, 2300});
        auto a = play2(s, rules, TwoMode::Coop, k0, k1, 3400);
        check("co-op: each player's phrase fills the one shared meter (0.25 + 0.25 = ready for both)",
              std::abs(a->meter.value - 0.5f) < 1e-6f && a->count(0, K::StarPowerReady) == 1 && a->count(1, K::StarPowerReady) == 1);
        check("co-op: one player engaging alone does not deploy (0x822C80F8)",
              a->count(0, K::StarPowerStart) == 0 && a->count(1, K::StarPowerStart) == 0);
        auto b = play2(s, rules, TwoMode::Coop, k0, k1 + ",3500:starpower", 9250);
        auto c = play2(s, rules, TwoMode::Coop, k0, k1 + ",3500:starpower", 9280);
        // Both deployed drain 2 × 0.0833 a measure: 0.5 lasts 3.0 measures = 5762 ms from 3500.
        std::printf("      (co-op Star Power: meter %.4f at 9250 ms)\n", double(b->meter.value));
        check("co-op: the second engage deploys both; both polls drain the meter (0.5 lasts 3 measures)",
              b->count(0, K::StarPowerStart) == 1 && b->count(1, K::StarPowerStart) == 1 && b->count(0, K::StarPowerEnd) == 0 &&
                  c->count(0, K::StarPowerEnd) == 1 && c->count(1, K::StarPowerEnd) == 1);
    }
    {  // Co-op band streak, multiplier and score.
        std::vector<NoteSpec> gtr, bass;
        std::vector<int> t0, t1;
        for (int i = 0; i < 6; ++i) gtr.push_back({1000 + 400 * i, G}), t0.push_back(1000 + 400 * i);
        for (int i = 0; i < 6; ++i) bass.push_back({1200 + 400 * i, G}), t1.push_back(1200 + 400 * i);
        bass.push_back({5000, G});
        Song s = makeSong(gtr, {}, 170, bass);
        auto r = play2(s, rules, TwoMode::Coop, strums(t0), strums(t1), 4000);
        // 12 hits: the band's streak 12 (x2 from its 10th hit); each player's own 6.
        check("co-op: every hit raises the band's streak (12 hits, x2), each player keeps its own (6)",
              r->band->streak() == 12 && r->band->multiplier() == 2 && r->p[0]->streak() == 6 && r->p[1]->streak() == 6);
        // Equal base scores: no normalisation; band points 9 x 50 + 3 x 100 = 750.
        check("co-op: the band scores every point at its own multiplier (9 x 50 + 3 x 100 = 750)", r->band->score() == 750);
        auto m = play2(s, rules, TwoMode::Coop, strums(t0), strums(t1), 6000);
        check("co-op: a pass by one player ends the band's streak; the other keeps its own",
              m->band->streak() == 0 && m->p[0]->streak() == 6 && m->band->longestStreak() == 12);
    }
    {  // Co-op fail: the band's meter ends the song, never a player's.
        std::vector<NoteSpec> gtr, bass;
        for (int i = 0; i < 40; ++i) gtr.push_back({1000 + 300 * i, G}), bass.push_back({1150 + 300 * i, G});
        Song s = makeSong(gtr, {}, 170, bass);
        auto r = play2(s, rules, TwoMode::Coop, "", "", 14000);
        const CrowdRules& c = rules.coop.crowd[3];
        int expect = 0;
        for (float raw = (c.rangeLow + c.rangeHigh) * 0.5f; expect < 200;) {
            raw = (1 - c.noteWeight) * raw;
            ++expect;
            if ((raw - c.rangeLow) / (c.rangeHigh - c.rangeLow) < c.loseLevel) break;
        }
        const int passes = r->failedAfter;
        std::printf("      (co-op fail: band failed after %d passes, expected %d)\n", passes, expect);
        check("co-op: the band fails on coop_config's meter (both players' passes); the players never fail",
              r->band->failed() && r->count(-1, K::Failed) == 1 && passes == expect && !r->p[0]->failed() && !r->p[1]->failed());
    }
    {  // The band's crowd and the easier player's mix (0x822DE800).
        Song s = makeSong({{1000, G}});
        BandPerformer hard(s, rules, Difficulty::Expert, Difficulty::Easy, "x"), tie(s, rules, Difficulty::Hard, Difficulty::Hard, "x");
        const BandPerformer::Mix m = hard.mix();
        const CrowdRules& e = rules.coop.crowd[0];
        const CrowdRules& x = rules.coop.crowd[3];
        const float lo = m.a + (m.b - m.a) * e.rangeLow, hi = m.a + (m.b - m.a) * e.rangeHigh;
        check("co-op: the band's crowd is the harder difficulty's; the easier player's range maps onto it, weight x min/max",
              hard.easierPlayer() == 1 && std::abs(lo - x.rangeLow) < 1e-5f && std::abs(hi - x.rangeHigh) < 1e-5f &&
                  std::abs(m.w - e.noteWeight / x.noteWeight) < 1e-5f && tie.easierPlayer() == 1 && std::abs(tie.mix().a) < 1e-6f &&
                  std::abs(tie.mix().b - 1) < 1e-6f);
    }
    {  // Normalisation: base 500 (lead) vs 250 (bass), r = 2.
        std::vector<NoteSpec> gtr, bass;
        for (int i = 0; i < 10; ++i) gtr.push_back({1000 + 300 * i, G});
        for (int i = 0; i < 5; ++i) bass.push_back({1000 + 600 * i, G});
        Song s = makeSong(gtr, {}, 170, bass);
        auto r = play2(s, rules, TwoMode::Coop, strums({1000}), strums({1000}), 1100);
        check("normalisation: r = 500 / 250, player 0's points x 0.75 (37), player 1's x 1.5 (75); the band 37.5 + 75 = 112",
              r->p[0]->score() == 37 && r->p[1]->score() == 75 && r->band->score() == 112);
    }
    {  // Face-off: the sections' player_mask decides who plays a gem.
        Song s = makeSong({{1000, G}, {2000, G}, {3000, G}}, {}, 170, {}, {}, {{900, 1200, 0}, {1900, 2200, 1}});
        const auto& g = s.chart(Part::Guitar, Difficulty::Expert).gems;
        auto r = play2(s, rules, TwoMode::FaceOff, "", "", 5000);
        check("face-off: section notes give player_mask 1 / 2, gems outside sections belong to both",
              g.size() == 3 && g[0].players == 1 && g[1].players == 2 && g[2].players == 0);
        check("face-off: each player is penalised only for its own gems and its base counts only those",
              r->p[0]->penalties() == 2 && r->p[1]->penalties() == 2 && r->p[0]->baseScore() == 100 && r->p[1]->baseScore() == 100);
        auto pro = play2(s, rules, TwoMode::ProFaceOff, "", "", 5000);
        check("pro face-off: both players play every gem", pro->p[0]->penalties() == 3 && pro->p[1]->penalties() == 3);
    }
    {  // Pro face-off / face-off: competitive, nobody fails.
        std::vector<NoteSpec> notes;
        for (int i = 0; i < 60; ++i) notes.push_back({1000 + 200 * i, G});
        Song s = makeSong(notes);
        auto r = play2(s, rules, TwoMode::ProFaceOff, "", "", 14000);
        check("pro face-off: a player whose meter empties does not fail (competitive, 0x822B37F8)",
              r->p[0]->rockMeter() == 0 && !r->p[0]->failed() && !r->p[1]->failed() && r->count(0, K::Failed) == 0);
    }
    {  // The band's stars (0x822CE5D8): 18andlife's max_scores (31933 43128 59640 66462) on this disc.
        check("co-op stars: 4 at 2.0 x max_scores[1], 5 at 2.0 x max_scores[3], a song without max_scores 5",
              rules.coopStars("18andlife", 86255) == 3 && rules.coopStars("18andlife", 86256) == 4 &&
                  rules.coopStars("18andlife", 132923) == 4 && rules.coopStars("18andlife", 132924) == 5 &&
                  rules.coopStars("nosuchsong", 0) == 5);
    }
    return failures;
}

int runRuleChecks(const Rules& rules) {
    int failures = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
        failures += !ok;
    };

    {  // Chart -> gem list
        Song s = makeSong({{1000, G}, {1010, R}, {2000, G}, {2011, R}});
        const auto& g = s.chart(Part::Guitar, Difficulty::Expert).gems;
        check("chart: note-ons 10 ticks apart are one chord, 11 apart are two gems",
              g.size() == 3 && g[0].frets == (G | R) && g[1].frets == G && g[2].frets == R);
        Song s2 = makeSong({{1000, Y, 160}, {2000, B, 161}});
        const auto& g2 = s2.chart(Part::Guitar, Difficulty::Expert).gems;
        check("chart: 160 ticks long has no sustain, 161 has one", g2[0].noSustain && !g2[1].noSustain);
        Song s3 = makeSong({{1000, G}, {1169, R}, {3000, G}, {3170, R}, {5000, G}, {5100, G | R}, {7000, Y}, {7100, B | Y}, {7200, G}});
        const auto& g3 = s3.chart(Part::Guitar, Difficulty::Expert).gems;
        check("chart: HOPO when single fret, no shared fret, gap < hopo_threshold (169 yes, 170 no, chords never)",
              g3.size() == 9 && g3[1].hopo && !g3[3].hopo && !g3[5].hopo && !g3[7].hopo && g3[8].hopo && !g3[0].hopo);
        Song s4 = makeSong({{1000, G}, {1170, R}, {3000, G}, {3249, R}, {5000, G}, {5250, R}}, {}, 250);
        const auto& g4 = s4.chart(Part::Guitar, Difficulty::Expert).gems;
        check("chart: a song's hopo_threshold 250 replaces 170 (170 and 249 yes, 250 no)", g4[1].hopo && g4[3].hopo && !g4[5].hopo);
    }

    const Song hopo = makeSong({{1000, G}, {1160, R}, {3000, Y, 100}});
    {
        Play r = play(hopo, rules, "999:+green,1000:strum,1070:+red", 2000);
        check("HOPO 90 ms early is accepted (GH1's window started at -40 ms)", r.p->gemHit(1) && r.count(K::Hopo, 1) == 1 && r.count(K::Miss) == 0);
        Play r2 = play(hopo, rules, "999:+green,1000:strum,1059:+red,1160:strum", 2000);
        check("HOPO 101 ms early is rejected (and does not lock: strummed later)", r2.count(K::Hopo) == 0 && r2.p->gemHit(1) && r2.p->penalties() == 0);
        Play r3 = play(hopo, rules, "999:+green,1000:strum,1100:-green,1320:+red", 2000);
        check("HOPO 160 ms late is accepted, after letting go of every fret without strumming",
              r3.count(K::Hopo, 1) == 1 && r3.p->penalties() == 0);
        Play r4 = play(hopo, rules, "999:+green,1000:strum,1100:-green,1321:+red", 2000);
        check("HOPO 161 ms late is rejected; the sweep passes it at slop + 60", r4.count(K::Hopo) == 0 && r4.count(K::Pass, 1) == 1);
    }
    {
        Song s = makeSong({{1000, G}, {1100, R}, {3000, Y}});
        Play r = play(s, rules, "999:+green,1000:strum,1050:-green,1100:+red", 2000);
        check("hammer-on with no strum since all frets were released is accepted (no GH1 strum gate)",
              r.count(K::Hopo, 1) == 1 && r.p->penalties() == 0);
    }
    {
        Song s = makeSong({{1000, Y}, {1100, R}, {3000, G}});
        Play r = play(s, rules, "999:+yellow,1000:strum,1020:+green,1090:-yellow,1100:+red", 2000);
        check("a wrong pull-off does not lock the gem (the hammer-on after it hits)", r.count(K::Hopo, 1) == 1 && r.p->penalties() == 0);
        Song s2 = makeSong({{1000, G}, {1100, Y}, {3000, R}});
        Play r2 = play(s2, rules, "999:+green,1000:strum,1090:+blue,1095:-blue,1100:+yellow,1110:strum", 2000);
        check("a wrong hammer-on locks the gem for HOPOs (hit by the strum instead)",
              r2.count(K::Hopo) == 0 && r2.p->gemHit(1) && r2.p->penalties() == 0);
        Play r3 = play(hopo, rules, "999:+green,1000:strum,1100:+red,1130:strum", 2000);
        check("strumming right after a hammer-on is forgiven (swingAtHopo)",
              r3.count(K::Hopo, 1) == 1 && r3.count(K::SwingAtHopo, 1) == 1 && r3.p->penalties() == 0);
    }
    {
        Song s = makeSong({{1000, G}, {4000, G}});
        Play r = play(s, rules, "990:+red,1000:strum,1030:+green", 2000);
        check("wrong strum: pressing the gem's fret within 60 ms hits", r.p->gemHit(0) && r.p->penalties() == 0);
        Play r2 = play(s, rules, "990:+green,990:+red,1000:strum,1030:-red", 2000);
        check("wrong strum: releasing to the gem's mask within 60 ms hits", r2.p->gemHit(0) && r2.p->penalties() == 0);
        Play r3 = play(s, rules, "990:+red,1000:strum,1020:+yellow", 2000);
        bool missAt1020 = false;
        for (const PlayerEvent& e : r3.ev) missAt1020 |= e.kind == K::Miss && e.gem == 0;
        check("wrong strum: a non-matching press misses at once; the pass is not penalised again",
              missAt1020 && !r3.p->gemHit(0) && r3.p->penalties() == 1 && r3.p->passes() == 1);
        Play r4 = play(s, rules, "990:+red,1000:strum", 1059);
        Play r5 = play(s, rules, "990:+red,1000:strum", 1061);
        check("wrong strum: the miss comes when the 60 ms run out", r4.count(K::Miss) == 0 && r5.count(K::Miss, 0) == 1);
        Play r6 = play(s, rules, "999:+green,1000:strum,1040:strum,1100:strum", 2000);
        check("a strum <= 52 ms after the last one is dropped", r6.p->gemHit(0) && r6.count(K::Miss) == 1);
        Song tie = makeSong({{1000, G}, {1200, R}});
        Play r7 = play(tie, rules, "1090:+red,1100:strum", 1300);
        check("nearest gem: a tie goes to the earlier gem", r7.count(K::Miss, 0) == 1 && r7.count(K::Miss, 1) == 0);
        Song hold = makeSong({{1000, R}, {4000, G | R}});
        Play r8 = play(hold, rules, "990:+green,990:+red,1000:strum,3990:+yellow,4000:strum", 4200);
        check("anchoring: a lower fret may be held on a single note; a chord needs the exact frets",
              r8.p->gemHit(0) && !r8.p->gemHit(1) && r8.count(K::Miss, 1) == 1);
    }
    {
        // Gem 1 is hammered on; the strum at 1140 is nearest the chord (gem 2)
        // with the wrong frets while gem 1 is still the HOPO-hit gem.
        Song s2 = makeSong({{1000, G}, {1100, R, 40}, {1150, G | Y}, {4000, Y}});
        Play r2 = play(s2, rules, "999:+green,1000:strum,1050:-green,1100:+red,1140:strum", 2000);
        check("a wrong strum right after a HOPO hit expires silently (only the pass penalises)",
              r2.count(K::Hopo, 1) == 1 && r2.count(K::Miss) == 0 && r2.count(K::Pass, 2) == 1 && r2.p->penalties() == 1);
    }
    {
        Song s = makeSong({{1000, G}, {5000, G}});
        Play r = play(s, rules, "1000:+green,1001:-green,2000:+red,2001:-red,2020:+yellow,2021:-yellow", 2100, ControllerMode::Joypad);
        Play r2 = play(s, rules, "1000:+green,1001:-green,2000:+red,2001:-red,2060:+yellow,2061:-yellow", 2100, ControllerMode::Joypad);
        check("at most one overstrum per 50 ms (joypad presses 20 ms / 60 ms apart)",
              r.p->gemHit(0) && r.count(K::Miss) == 1 && r2.count(K::Miss) == 2);
        Song c = makeSong({{1000, G | R}, {5000, G}});
        Play r3 = play(c, rules, "1000:+green,1040:+red", 1200, ControllerMode::Joypad);
        Play r4 = play(c, rules, "1000:+green,1010:+yellow", 1200, ControllerMode::Joypad);
        check("joypad: a chord's frets within 50 ms hit; a wrong fret misses", r3.p->gemHit(0) && !r4.p->gemHit(0) && r4.count(K::Miss, 0) == 1);
    }
    {
        std::vector<NoteSpec> ten;
        for (int i = 0; i < 10; ++i) ten.push_back({1000 + 480 * i, uint8_t(i % 2 ? R : G)});
        Song s = makeSong(ten);
        std::string keys;
        for (int i = 0; i < 10; ++i) {
            const char* f = i % 2 ? "red" : "green";
            int t = 1000 + 480 * i;
            keys += std::to_string(t - 5) + ":+" + f + "," + std::to_string(t) + ":strum," + std::to_string(t + 200) + ":-" + f + ",";
        }
        Play r = play(s, rules, keys);
        check("the 10th note in a row already scores at 2x (9 x 50 + 100 = 550)", r.p->score() == 550 && r.p->notesHit() == 10);
        check("base score and stars: base 500, 550 = 3 stars (thresholds 2.0 / 2.8 from scoring.dtb)",
              r.p->baseScore() == 500 && r.p->stars() == 3 && rules.starThresholds[3].first == 2.0f &&
                  std::abs(rules.starThresholds[3].second - 2.8f) < 1e-5f);
    }
    {
        Song s = makeSong({{1000, G, 960}, {5000, R}});
        Play r = play(s, rules, "999:+green,1000:strum,2500:-green", 3000);
        Play r2 = play(s, rules, "999:+green,1000:strum,1480:-green", 3000);
        check("sustain: 2 beats = 50 tail points held fully, 25 when let go halfway", r.p->score() == 100 && r2.p->score() == 75);
        check("sustain: base score counts the tail (50 + 960 x 25 / 480 = 100 for the first gem)", r.p->baseScore() == 100 + 50);
        // 0x823500c8 [C]: a fret pressed below a sustained single note (an anchor) keeps it, one above ends it.
        Song y = makeSong({{1000, Y, 960}, {5000, R}});
        Play r3 = play(y, rules, "999:+yellow,1000:strum,1200:+green,2500:-yellow", 3000);
        Play r4 = play(y, rules, "999:+yellow,1000:strum,1480:+blue,2500:-yellow", 3000);
        check("sustain: a lower fret pressed during a single sustain keeps it; a higher one ends it",
              r3.p->score() == 100 && r4.p->score() == 75);
        // On a chord any press ends it; letting go of any of its frets too (0x8234f110).
        Song c = makeSong({{1000, G | R, 960}, {5000, R}});
        Play r5 = play(c, rules, "998:+green,999:+red,1000:strum,1480:+yellow,2500:-green,2500:-red", 3000);
        Play r6 = play(c, rules, "998:+green,999:+red,1000:strum,1480:-green,2500:-red", 3000);
        check("sustain: on a chord any extra fret or any released fret ends it", r5.p->score() == 150 && r6.p->score() == 150);
    }
    {
        // GH2 lets lower frets be held on a single note (strum and HOPO: 0x8234eb60 compares the gem's fret with
        // the highest held one; 0x8234f210 needs no held fret above it); a chord needs exactly its frets [C].
        Song s = makeSong({{1000, Y}, {3000, Y}, {5000, G | Y}, {7000, G | Y}});
        Play r = play(s, rules, "990:+green,995:+yellow,1000:strum,1100:-green,1100:-yellow,2990:+yellow,2995:+blue,3000:strum,3100:-blue,"
                                "3100:-yellow,4990:+green,4992:+red,4995:+yellow,5000:strum,5100:-green,5100:-red,5100:-yellow,"
                                "6990:+yellow,7000:strum",
                       8000);
        check("anchoring: green + yellow held hits a single yellow; yellow + blue does not",
              r.p->gemHit(0) && !r.p->gemHit(1) && r.count(K::Miss, 1) == 1);
        check("chords are exact: an extra lower fret or a missing fret misses",
              !r.p->gemHit(2) && !r.p->gemHit(3) && r.count(K::Miss, 2) == 1 && r.count(K::Miss, 3) == 1);
        Song h = makeSong({{1000, R}, {1100, Y}, {1200, R}, {3000, G}});
        Play r2 = play(h, rules, "990:+green,995:+red,1000:strum,1100:+yellow,1200:-yellow", 2000);
        check("HOPOs with an anchor: hammer-on to yellow and pull-off to red with green held",
              r2.count(K::Hopo, 1) == 1 && r2.count(K::Hopo, 2) == 1 && r2.p->penalties() == 0);
    }
    {
        Song s = makeSong({{1000, G}, {1300, G}, {2000, G}, {2300, G}, {4000, G}, {5000, G}, {5300, G}, {14000, G}},
                          {{1000, 1400}, {2000, 2400}, {5000, 5400}});
        std::string keys = "500:+green";
        for (int t : {1000, 1300, 2000, 2300, 4000, 5000, 5300, 14000}) keys += "," + std::to_string(t) + ":strum";
        keys += ",3000:starpower";
        Play r = play(s, rules, keys);
        const int captured = r.count(K::PhraseCaptured);
        const bool ready = r.count(K::StarPowerReady) == 1;
        // Score: 4 x 50 before Star Power, then 3 gems at 2x (100 each), then the last at 1x.
        check("Star Power: two phrases (0.25 each) make it ready; a phrase during Star Power is not captured",
              captured == 2 && ready && r.count(K::StarPowerStart) == 1);
        check("Star Power: x2 score while deployed (4 x 50 + 3 x 100 + 50 = 550)", r.p->score() == 550);
        // Drain: 0.5 at 0.125 a measure = 4 measures = 7680 ms after 3000.
        Play r2 = play(s, rules, keys, 10670);
        Play r3 = play(s, rules, keys, 10700);
        check("Star Power: drains 0.125 a measure (0.5 lasts 4 measures)",
              r2.count(K::StarPowerEnd) == 0 && r3.count(K::StarPowerEnd) == 1);
    }
    {
        Song s = makeSong({{1000, G, 1920}, {5000, R}}, {{1000, 1100}});
        std::string fast = "999:+green,1000:strum,4000:-green", slow = fast;
        for (int t = 1100, k = 0; t <= 2900; t += 20, ++k) {
            fast += "," + std::to_string(t) + ":whammy=" + (k % 2 ? "0.5" : "0");
            slow += "," + std::to_string(t) + ":whammy=" + (k % 2 ? "0.005" : "0");
        }
        Play r = play(s, rules, fast, 3500);
        Play r2 = play(s, rules, slow, 3500);
        const float m = r.p->starMeter(), m2 = r2.p->starMeter();
        std::printf("      (whammy: meter %.4f with a moving bar, %.4f with a slow one)\n", m, m2);
        // 0.25 + 0.034 × (2920 - 1120) / 480 = 0.3775: from the first movement to the sustain's end.
        check("whammy fills 0.034 a beat on a captured phrase's sustain; a bar slower than 0.0005/ms does not",
              m > 0.37f && m < 0.385f && std::abs(m2 - 0.25f) < 1e-6f && r.count(K::WhammyStart) == 1);
    }
    {
        std::vector<NoteSpec> notes;
        for (int i = 0; i < 30; ++i) notes.push_back({1000 + 300 * i, G});
        Song s = makeSong(notes);
        Play r = play(s, rules, "", 1300);
        const CrowdRules& c = rules.solo.crowd[3];
        const float raw0 = (c.rangeLow + c.rangeHigh) * 0.5f;
        const float raw1 = (1 - c.noteWeight) * raw0;
        const float want = (raw1 - c.rangeLow) / (c.rangeHigh - c.rangeLow);
        check("rock meter: one pass on Expert moves raw by note_weight toward 0 (scoring.dtb values)",
              r.p->penalties() == 1 && std::abs(r.p->rockMeter() - want) < 1e-4f);
        Play r2 = play(s, rules, "");
        int expect = 0;
        for (float raw = raw0; expect < 100;) {
            raw = (1 - c.noteWeight) * raw;
            ++expect;
            if ((raw - c.rangeLow) / (c.rangeHigh - c.rangeLow) < c.loseLevel) break;
        }
        std::printf("      (rock meter: failed after %d passes, expected %d)\n", r2.p->penalties(), expect);
        check("rock meter: the song fails once the rating drops below lose_level", r2.p->failed() && r2.p->penalties() == expect);
    }
    return failures;
}

}  // namespace

int runSelfTest(const std::string& dataPath) {
    try {
        GameData data(dataPath);
        std::printf("%s\n", data.description().c_str());
        int scriptFailures = checkScripts(data);
        Rules rules = loadRules(data);
        std::vector<SongInfo> songs = loadSongs(data);
        std::printf("%zu songs; slop %.0f ms, head %d, tail %d, hopo_threshold %d ticks\n", songs.size(), rules.slopMs,
                    rules.headPoints, rules.tailPoints, rules.hopoThresholdTicks);

        int audioFailures = 0, chartFailures = 0, hopos = 0, strumHopos = 0, ownThreshold = 0;
        PartTotals totals[kPartCount];
        for (const SongInfo& info : songs) {
            ownThreshold += info.audio.hopoThresholdTicks > 0;
            audioFailures += !checkAudio(data, info.audio, info.id);
            if (info.coopAudio) audioFailures += !checkAudio(data, *info.coopAudio, info.id + " (co-op)");
            Song song;
            try {
                song = loadChart(data, info, rules);
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "  %s: chart: %s\n", info.id.c_str(), ex.what());
                ++chartFailures;
                continue;
            }
            std::printf("%-20s", info.id.c_str());
            for (int part = 0; part < kPartCount; ++part) {
                if (!song.hasPart[size_t(part)]) continue;
                for (int d = 0; d < kDifficultyCount; ++d) {
                    // Every gem strummed, then HOPOs hammered on / pulled off (gd01).
                    const Part pt = static_cast<Part>(part);
                    const Difficulty diff = static_cast<Difficulty>(d);
                    Player s(song, pt, diff, rules, ControllerMode::Guitar, false);
                    Player p(song, pt, diff, rules, ControllerMode::Guitar, false);
                    Autoplay strummer(s.chart(), -1, false), bot(p.chart());
                    for (float t = -1000; t < song.endMs + 5000; t += 4) {
                        strummer.feed(s, t), bot.feed(p, t);
                        s.update(t), p.update(t);
                        for (const PlayerEvent& e : p.takeEvents()) hopos += e.kind == PlayerEvent::Kind::Hopo;
                        for (const PlayerEvent& e : s.takeEvents()) strumHopos += e.kind == PlayerEvent::Kind::Hopo;
                    }
                    const int total = static_cast<int>(p.chart().gems.size());
                    // The strum-only run may drop gems that follow the previous
                    // one by 52 ms or less: the original drops such strums
                    // (0x002356f8, "last + 52 < now") [C], so only hammer-ons
                    // reach them (ballroomblitz's expert run at 48.7 ms).
                    bool strumOk = !s.failed();
                    for (int i = 0; i < total && strumOk; ++i)
                        if (!s.gemHit(i) && !(i > 0 && p.chart().gems[size_t(i)].ms - p.chart().gems[size_t(i) - 1].ms <= 52.5f))
                            strumOk = false;
                    const bool ok = p.notesHit() == total && !p.failed() && strumOk;
                    PartTotals& pt2 = totals[part];
                    ++pt2.charts, pt2.gems += total, pt2.failed += !ok;
                    chartFailures += !ok;
                    if (part == int(Part::Guitar))
                        std::printf("  %c%4d/%-4d %7d%s", difficultyName(diff)[0], p.notesHit(), total, p.score(), ok ? "" : "!");
                    if (ok) continue;
                    if (s.notesHit() != total) std::fprintf(stderr, "  %s %s %s strummed only: %d/%d\n", info.id.c_str(),
                                                            partTrackName(pt), difficultyName(diff), s.notesHit(), total);
                    const auto& gems = p.chart().gems;
                    for (int i = 0, shown = 0; i < total && shown < 6; ++i)
                        if (!s.gemHit(i))
                            ++shown, std::fprintf(stderr, "    strum missed gem %d at %.1f ms, frets %02x%s, gap %.1f ms\n", i,
                                                  gems[size_t(i)].ms, gems[size_t(i)].frets, gems[size_t(i)].hopo ? " hopo" : "",
                                                  i ? gems[size_t(i)].ms - gems[size_t(i) - 1].ms : 0.0f);
                    for (int i = 0; i < total; ++i) {
                        if (p.gemHit(i)) continue;
                        std::fprintf(stderr, "  missed %s %s %s gem %d at %.1f ms, frets %02x%s, gap %.1f ms\n",
                                     info.id.c_str(), partTrackName(pt), difficultyName(diff), i, gems[size_t(i)].ms,
                                     gems[size_t(i)].frets, gems[size_t(i)].hopo ? " hopo" : "",
                                     i ? gems[size_t(i)].ms - gems[size_t(i) - 1].ms : 0.0f);
                    }
                }
            }
            std::printf("\n");
        }
        for (int part = 0; part < kPartCount; ++part)
            std::printf("%-17s %3d charts, %6d gems, %d not fully hit\n", partTrackName(static_cast<Part>(part)),
                        totals[part].charts, totals[part].gems, totals[part].failed);
        std::printf("%d gems hit as hammer-ons / pull-offs (%d in the strummed run: a fret press hits an eligible HOPO\n"
                    "  before the strum, which is then forgiven, as in GH2); %d songs with their own hopo_threshold\n",
                    hopos, strumHopos, ownThreshold);
        std::printf("rule checks:\n");
        int ruleFailures = runRuleChecks(rules);
        std::printf("two-player rule checks (docs/multiplayer.md):\n");
        ruleFailures += runTwoPlayerChecks(rules);
        std::printf(ruleFailures ? "%d rule check(s) failed\n" : "all rule checks passed\n", ruleFailures);
        std::printf("input checks (virtual controllers):\n");
        const int inputFailures = runInputChecks();
        std::printf(inputFailures ? "%d input check(s) failed\n" : "all input checks passed\n", inputFailures);
        std::printf("%d song audio file(s) with problems, %d script(s) not decoded\n", audioFailures, scriptFailures);
        std::printf(chartFailures ? "%d chart(s) not fully hit\n" : "all charts fully hit\n", chartFailures);
        return chartFailures || audioFailures || scriptFailures || ruleFailures || inputFailures ? 1 : 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "self-test failed: %s\n", ex.what());
        return 2;
    }
}
