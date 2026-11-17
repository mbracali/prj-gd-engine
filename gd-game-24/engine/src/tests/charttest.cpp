// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/charttest.h"

#include <cmath>
#include <cstdio>
#include <map>

#include "data/beatmatch.h"
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
