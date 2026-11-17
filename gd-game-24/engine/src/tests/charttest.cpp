// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/charttest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "data/beatmatch.h"
#include "data/song_config.h"
#include "game/song_play.h"
#include "data/song_list.h"
#include "data/umd.h"

int chartTest(const gd24::Umd& umd, const std::string& which, bool notes) {
    static const char* const kDiff[] = {"Easy", "Medium", "Hard", "Expert"};
    int failures = 0, songs = 0;
    size_t allGems = 0;
    const bool one = !which.empty() && which != "all";
    for (const gd24::SongEntry& s : gd24::readSongList(umd)) {
        if (!(one ? gd24::upperPath(which) == s.folder : !s.tutorial)) continue;
        ++songs;
        std::vector<uint8_t> bytes;
        gd24::Beatmatch bm;
        std::string error;
        if (!umd.read(s.beatmatchPath(), bytes) || !bm.parse(std::move(bytes), &error)) {
            std::printf("charttest: FAIL %s: %s\n", s.folder.c_str(), error.empty() ? "cannot read" : error.c_str());
            ++failures;
            continue;
        }
        std::string why;
        for (size_t i = 0; i < bm.images.size(); ++i)
            if (bm.images[i].difficulty != i)
                why += " entry " + std::to_string(bm.images[i].entry) + " has difficulty word " + std::to_string(bm.images[i].difficulty);
        const gd24::BeatmatchImage& im = bm.images.back();
        int pointed = 0, total = 0;
        bm.kPadSymbolsAt(im, pointed, total);
        if (!total || pointed != total) why += " base: " + std::to_string(pointed) + "/" + std::to_string(total) + " kPad symbols";
        std::string terr;
        const auto tracks = bm.readTracks(im, &terr);
        if (!terr.empty()) why += " " + terr;
        if (tracks.size() != 4) why += " " + std::to_string(tracks.size()) + " tracks";
        // Checks: one tick has one time in every list (the shared tempo
        // map); the four images give the same gems.
        std::map<uint32_t, float> timeOf;
        int conflicts = 0;
        std::string counts;
        size_t gems = 0;
        int twoLaneEasy = 0;
        for (const auto& t : tracks) {
            counts += " " + t.name.substr(5) + " ";
            for (size_t d = 0; d < t.difficulty.size(); ++d) {
                const gd24::GemList& l = t.difficulty[d];
                counts += (d ? "/" : "") + std::to_string(l.gems.size());
                gems += l.gems.size();
                for (const gd24::Gem& g : l.gems) {
                    auto [it, fresh] = timeOf.emplace(g.tick, g.ms);
                    if (!fresh && std::fabs(it->second - g.ms) > 1.0f) ++conflicts;
                }
            }
            if (!t.difficulty.empty() && t.difficulty[0].twoLane()) ++twoLaneEasy;
        }
        std::string e0;
        const auto easyImage = bm.readTracks(bm.images.front(), &e0);
        size_t gems0 = 0;
        for (const auto& t : easyImage)
            for (const auto& l : t.difficulty) gems0 += l.gems.size();
        if (gems0 != gems) why += " the Easy image has " + std::to_string(gems0) + " gems, the Expert one " + std::to_string(gems);
        if (conflicts) why += " " + std::to_string(conflicts) + " ticks with two times";
        allGems += gems;
        std::printf("charttest: %s %-18s%s (E/M/H/X), %zu gems, %zu ticks, %d two-lane Easy\n", why.empty() ? "ok  " : "FAIL",
                    s.folder.c_str(), counts.c_str(), gems, timeOf.size(), twoLaneEasy);
        if (!why.empty()) std::printf("charttest:   %s:%s\n", s.folder.c_str(), why.c_str()), ++failures;
        if (!one) continue;
        for (const auto& t : tracks)
            for (size_t d = 0; d < t.difficulty.size() && d < 4; ++d) {
                const gd24::GemList& l = t.difficulty[d];
                std::printf("  %s %s: %zu gems (%d chords), %.0f..%.0f ms, list 0x%08x\n", t.name.c_str(), kDiff[d], l.gems.size(),
                            l.chords(), double(l.gems.front().ms), double(l.gems.back().ms), l.address);
                if (!notes) continue;
                for (const gd24::Gem& g : l.gems) {
                    char lanes[5] = "....";
                    for (int k = 0; k < 4; ++k)
                        if (g.lanes & (1 << k)) lanes[k] = char('1' + k);
                    std::printf("    %9.2f ms  tick %6u  %s  id %3u  %5u %5u  flags %02x %08x\n", double(g.ms), g.tick, lanes, g.id, g.a,
                                g.b, g.flags, g.flags2);
                }
            }
    }
    std::printf("charttest: %d songs, %zu gems; %d failures\n", songs, allGems, failures);
    return failures || !songs ? 1 : 0;
}

namespace {

struct RulesRun {
    gd24::SongPlay play;
    double endMs = 0;
};

// Replays the score events with the rule alone; returns the mismatches.
int replayScore(const gd24::SongPlay& p, std::string& why) {
    const gd24::Rules& r = p.rules();
    int m = 1, bad = 0;
    bool od = false;
    double grace = -1e300;
    long sum = 0;
    for (const auto& e : p.events()) {
        auto shown = [&] { return od ? (m == 5 ? 11 : 2 * m) : m; };
        if (e.what == 'o') od = e.value == 1;
        if (e.what == 'c') m = std::min(r.maxMultiplier, m + 1), grace = e.ms + r.captureGraceMs;
        if (e.what == 'm' && e.ms > grace) m = 1;
        if (e.multiplier != shown()) {
            if (!bad++) why = "event '" + std::string(1, e.what) + "' at " + std::to_string(int(e.ms)) + " ms: multiplier " +
                              std::to_string(e.multiplier) + ", the rule gives " + std::to_string(shown());
        }
        if (e.what == 'h') sum += e.value, bad += e.value != r.gemPoints * shown();
        if (e.what == 'c') sum += e.value, bad += e.value != r.capturePoints * shown();
    }
    if (sum != p.score()) {
        if (!bad) why = "score " + std::to_string(p.score()) + " != the events' " + std::to_string(sum);
        ++bad;
    }
    return bad;
}

// The idle player's fail time from the chart alone.
double predictIdleFail(const gd24::SongPlay& p, double startMs) {
    const gd24::Rules& r = p.rules();
    const int d = p.difficulty();
    struct Hit { double ms; int track; float amount; };
    std::vector<Hit> hits;
    const double slop = p.slop();
    for (int t = 0; t < gd24::SongPlay::kTracks; ++t) {
        const auto& tr = p.tracks()[size_t(t)];
        if (t != p.active()) {
            for (const auto& ph : tr.phrases)
                if (tr.gems[ph.first].ms >= startMs - slop)
                    hits.push_back({tr.gems[ph.first].ms + slop, t, r.healthMiss[d] * r.inactiveFactor});
            continue;
        }
        // The active track: the first gem of each phrase the indicator takes.
        double from = startMs;
        for (const auto& ph : tr.phrases) {
            if (ph.startMs < from) continue;
            hits.push_back({ph.startMs + slop, t, r.healthMiss[d]});
            from = ph.startMs + slop + slop;  // the next phrase after the miss is seen (one frame late at most)
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.ms < b.ms; });
    float h[4] = {r.healthInit, r.healthInit, r.healthInit, r.healthInit};
    bool failed[4] = {};
    for (const Hit& x : hits) {
        if (failed[x.track]) continue;
        h[x.track] -= x.amount;
        const double frac = x.ms / p.chartEndMs();
        if ((d == 0 && frac < 0.5) || (d == 1 && frac < 0.3)) h[x.track] = std::max(h[x.track], 2 * r.healthMiss[d]);
        if (h[x.track] <= 0) h[x.track] = 0, failed[x.track] = true;
        float sum = 0;
        int nf = 0;
        for (int t = 0; t < 4; ++t) sum += std::clamp(h[t], 0.0f, 1.0f), nf += failed[t];
        if (sum * 0.25f - float(nf) * r.crowdFailedPenalty <= 0) return x.ms;
    }
    return -1;
}

}  // namespace

int rulesTest(const gd24::Umd& umd, const std::string& which, int difficulty) {
    const int d = difficulty >= 0 ? std::clamp(difficulty, 0, 3) : 3;
    const bool one = !which.empty() && which != "all";
    int failures = 0, songs = 0;
    const double frame = 1000.0 / 60.0;
    for (const gd24::SongEntry& s : gd24::readSongList(umd)) {
        if (!(one ? gd24::upperPath(which) == s.folder : !s.tutorial)) continue;
        std::vector<uint8_t> bytes;
        gd24::Beatmatch bm;
        std::string error;
        if (!umd.read(s.beatmatchPath(), bytes) || !bm.parse(std::move(bytes), &error)) {
            std::printf("rulestest: FAIL %s: %s\n", s.folder.c_str(), error.empty() ? "cannot read" : error.c_str());
            ++failures;
            continue;
        }
        ++songs;
        const gd24::BeatmatchImage* im = &bm.images[0];
        for (const auto& x : bm.images)
            if (int(x.difficulty) == d) im = &x;
        const auto tracks = bm.readTracks(*im, &error);
        const auto sigs = bm.readTimeSigs(*im);
        const gd24::SongConfig cfg = gd24::readSongConfig(bm, *im, s.folder);
        const int initial = cfg.initialTrack >= 0 ? cfg.initialTrack : 2;
        std::string why;
        // 1. Perfect autoplay over the whole chart.
        gd24::SongPlay p;
        p.start(tracks, sigs, d, initial, cfg.slopMs);
        const double end = p.chartEndMs() + 2000;
        for (double ms = 0; ms < end && !p.failed(); ms += frame) gd24::autoplayStep(p, ms), p.update(ms);
        int captures = 0, phraseMisses = 0, attempted = 0, at5 = -1, releases = 0;
        for (const auto& e : p.events()) {
            captures += e.what == 'c', phraseMisses += e.what == 'm', releases += e.what == 'r';
            if (e.what == 'c' && at5 < 0 && (e.multiplier == 5 || e.multiplier == 11)) at5 = captures;
        }
        for (const auto& t : p.tracks()) attempted += t.attempted;
        if (p.failed()) why += " perfect play failed the song at " + std::to_string(int(p.stats().failMs)) + " ms;";
        if (phraseMisses) why += " perfect play missed " + std::to_string(phraseMisses) + " phrases;";
        if (at5 >= 0 && at5 != 4) why += " multiplier 5 at capture " + std::to_string(at5) + " (expected 4);";
        std::string rw;
        if (replayScore(p, rw)) why += " " + rw + ";";
        const auto& st = p.stats();
        std::printf("rulestest: %-18s %s perfect: score %d (gems %d + captures %d), %d / %d phrases, x%d max, x5 at capture %d, "
                    "Overdrive %d (%d runs), %d releases\n",
                    s.folder.c_str(), gd24::SongPlay::kTracks ? "" : "", st.score, st.gemScore, st.captureScore, captures, attempted,
                    st.maxMultiplier, at5, st.odActivations, st.odRunsCompleted, releases);
        // 2. An idle player.
        gd24::SongPlay idle;
        idle.start(tracks, sigs, d, initial, cfg.slopMs);
        double t = 0;
        for (; t < end && !idle.failed(); t += frame) idle.update(t);
        const double predicted = predictIdleFail(idle, 0);
        const double got = idle.failed() ? idle.stats().failMs : -1;
        const bool idleOk = predicted < 0 ? got < 0 : (got >= predicted && got <= predicted + frame + 0.01);
        if (!idleOk) why += " idle fail at " + std::to_string(int(got)) + " ms, predicted " + std::to_string(int(predicted)) + ";";
        // 3. One gem left out after five captures.
        gd24::SongPlay m;
        m.start(tracks, sigs, d, initial, cfg.slopMs);
        int skipTrack = -1;
        size_t skipGem = 0;
        double skipAt = -1;
        for (double ms = 0; ms < end && !m.failed(); ms += frame) {
            int caps = 0;
            for (const auto& e : m.events()) caps += e.what == 'c';
            const auto& tr = m.tracks()[size_t(m.active())];
            if (skipTrack < 0 && caps >= 5 && tr.indicator >= 0) {
                const auto& ph = tr.phrases[size_t(tr.indicator)];
                if (ph.startMs > ms + 200 && ph.startMs > m.events().back().ms + m.rules().captureGraceMs + 100)
                    skipTrack = m.active(), skipGem = ph.first, skipAt = ph.startMs;
            }
            if (skipTrack == m.active() && ms <= skipAt + m.slop()) {  // stay on the track, leave the gem out
                // press everything except the skipped gem
                const auto& t2 = m.tracks()[size_t(m.active())];
                for (size_t i = t2.passed; i < t2.gems.size() && t2.gems[i].ms <= ms; ++i)
                    if (i != skipGem && t2.state[i] == gd24::SongPlay::State::Pending && t2.gems[i].ms > ms - m.slop())
                        for (int l = 0; l < 4; ++l)
                            if ((t2.gems[i].lanes & (1 << l)) && !(t2.pressed[i] & (1 << l))) m.press(l, ms);
            } else {
                gd24::autoplayStep(m, ms, false);
            }
            m.update(ms);
            if (skipAt >= 0 && ms > skipAt + 500) break;
        }
        int multAfter = -1;
        bool sawMiss = false;
        for (const auto& e : m.events())
            if (e.what == 'm' && e.ms >= skipAt) sawMiss = true, multAfter = e.multiplier;
        if (skipAt < 0) why += " no phrase to leave a gem out of;";
        else if (!sawMiss || multAfter != 1 || m.streak() != 0) why += " a missed gem: multiplier " + std::to_string(multAfter) + ", streak " + std::to_string(m.streak()) + ";";
        std::printf("rulestest: %-18s idle: song failed at %.0f ms (predicted %.0f); a gem left out at %.0f ms: multiplier %d, streak %d%s\n",
                    s.folder.c_str(), got, predicted, skipAt, multAfter, m.streak(), why.empty() ? "" : "");
        if (!why.empty()) std::printf("rulestest: FAIL %s:%s\n", s.folder.c_str(), why.c_str()), ++failures;
    }
    std::printf("rulestest: %d songs (%s), %d failures\n", songs, d == 3 ? "Expert" : d == 2 ? "Hard" : d == 1 ? "Medium" : "Easy", failures);
    return failures ? 1 : 0;
}
