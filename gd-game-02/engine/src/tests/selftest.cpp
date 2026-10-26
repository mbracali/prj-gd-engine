// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/selftest.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <vector>

#include "audio/vorbis_stream.h"
#include "data/dta.h"
#include "data/game_data.h"
#include "game/autoplay.h"
#include "game/song_db.h"
#include "game/song_setup.h"

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

// The mogg header and Vorbis headers, and songs.dtb's per-channel lists.
bool checkAudio(const GameData& data, const SongAudio& a, const std::string& id) {
    try {
        std::vector<uint8_t> head(size_t(std::min<uint64_t>(data.fileSize(a.moggPath), 256 * 1024)));
        data.readRange(a.moggPath, 0, head.data(), head.size());
        int channels = 0, rate = 0;
        probeMogg(head.data(), head.size(), channels, rate);
        size_t n = size_t(channels);
        if (a.pans.size() != n || a.vols.size() != n || a.cores.size() != n) {
            std::fprintf(stderr, "  %s: %s has %d channels, songs.dtb lists %zu pans / %zu vols / %zu cores\n", id.c_str(),
                         a.moggPath.c_str(), channels, a.pans.size(), a.vols.size(), a.cores.size());
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
        std::fprintf(stderr, "  %s: %s: %s\n", id.c_str(), a.moggPath.c_str(), ex.what());
        return false;
    }
}

struct PartTotals {
    int charts = 0, gems = 0, failed = 0;
};

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

        int audioFailures = 0, chartFailures = 0, hopos = 0;
        PartTotals totals[kPartCount];
        for (const SongInfo& info : songs) {
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
                        s.takeEvents();
                    }
                    const int total = static_cast<int>(p.chart().gems.size());
                    const bool ok = p.notesHit() == total && !p.failed() && s.notesHit() == total && !s.failed();
                    PartTotals& pt2 = totals[part];
                    ++pt2.charts, pt2.gems += total, pt2.failed += !ok;
                    chartFailures += !ok;
                    if (part == int(Part::Guitar))
                        std::printf("  %c%4d/%-4d %7d%s", difficultyName(diff)[0], p.notesHit(), total, p.score(), ok ? "" : "!");
                    if (ok) continue;
                    if (s.notesHit() != total) std::fprintf(stderr, "  %s %s %s strummed only: %d/%d\n", info.id.c_str(),
                                                            partTrackName(pt), difficultyName(diff), s.notesHit(), total);
                    const auto& gems = p.chart().gems;
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
        std::printf("%d gems hit as hammer-ons / pull-offs\n", hopos);
        std::printf("%d song audio file(s) with problems, %d script(s) not decoded\n", audioFailures, scriptFailures);
        std::printf(chartFailures ? "%d chart(s) not fully hit\n" : "all charts fully hit\n", chartFailures);
        return chartFailures || audioFailures || scriptFailures ? 1 : 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "self-test failed: %s\n", ex.what());
        return 2;
    }
}
