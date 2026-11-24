// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/careertest.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "data/disc.h"
#include "data/qbkey.h"
#include "game/career.h"
#include "game/save.h"
#include "game/shop.h"
#include "script/globals.h"

// Proves (data-derived, no eyeballing) on the real disc:
//  * the five careers load from GH4_Career_<Part>_Songs with 20 tiers each, and
//    the band unlock-order + rewards parse;
//  * starting a band career unlocks the free gigs (1,2,3) and not gig 4;
//  * autoplaying every song of gig 1 to 5 stars awards exactly the cash the real
//    cash_milestones table says, into band_info.cash (the store's currency);
//  * the gig then reads complete, its venue unlocks, the next gig (4) and its
//    tier's songs unlock, and the reward (progression_band_rewards[0]) grants the
//    career-gated CAS pieces as band_info `unlocked` flags — which makes a $locked
//    store item buyable (closing the Step 7.4 loop);
//  * all of it (cash + gig flags + the reward unlock) survives a save round-trip;
//  * the ten career cutscenes are present in the Bonus_Videos movie inventory.
namespace {

std::string nm(uint32_t k) {
    const std::string* n = qbkey::names().find(k);
    return n ? *n : ("0x" + std::to_string(k));
}

}  // namespace

int runCareerTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::printf("careertest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    Globals g;
    try {
        g.load(*disc);
    } catch (const std::exception& e) {
        std::printf("careertest: globals: %s\n", e.what());
        return 2;
    }

    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) {
            std::printf("careertest: FAIL %s\n", what.c_str());
            ++failures;
        }
        return ok;
    };

    // (a) The five careers load.
    std::vector<career::Career> careers = career::load(g);
    check(careers.size() == career::kNumParts,
          "expected 5 careers, got " + std::to_string(careers.size()));
    for (const career::Career& c : careers) {
        int tiersWithSongs = 0, totalSongs = 0;
        for (int i = 1; i <= c.numTiers; ++i)
            if (const career::Tier* t = c.tier(i)) {
                if (!t->songs.empty()) ++tiersWithSongs;
                totalSongs += int(t->songs.size());
            }
        std::printf("careertest: career %-6s num_tiers=%d tiers-with-songs=%d songs=%d unlocksets=%zu rewards=%zu intro=%s outro=%s\n",
                    career::partName(c.part), c.numTiers, tiersWithSongs, totalSongs,
                    c.unlockSets.size(), c.rewards.size(), c.initialMovie.c_str(), c.endMovie.c_str());
        check(c.numTiers == 20, std::string(career::partName(c.part)) + " num_tiers != 20");
        check(tiersWithSongs >= 13, std::string(career::partName(c.part)) + " too few tiers with songs");
        check(!c.unlockSets.empty(), std::string(career::partName(c.part)) + " no unlocksets");
        check(!c.rewards.empty(), std::string(career::partName(c.part)) + " no rewards");
    }

    const career::Career* band = career::find(careers, career::Part::Band);
    if (!check(band != nullptr, "no band career")) return failures;

    // (b) Movie inventory: the ten career cutscenes are present.
    std::vector<career::Movie> movies = career::movies(g);
    const char* wanted[] = {"band_intro", "band_outro", "guitarist_intro", "guitarist_outro",
                            "drummer_intro", "drummer_outro", "bass_intro", "bass_outro",
                            "singer_intro", "singer_outro"};
    int foundCutscenes = 0;
    for (const char* w : wanted)
        for (const career::Movie& m : movies)
            if (m.name == w) { ++foundCutscenes; break; }
    std::printf("careertest: Bonus_Videos movies=%zu; career cutscenes present=%d/10\n",
                movies.size(), foundCutscenes);
    check(foundCutscenes == 10, "missing career cutscenes in Bonus_Videos");

    // (c) Start the band career: free gigs 1,2,3 unlocked; gig 4 locked.
    career::TagStore st;
    qb::Struct bandInfo;  // a fresh band_info (cash defaults to 0)
    career::startCareer(st, bandInfo, *band);
    check(career::gigUnlocked(st, *band, 1), "gig 1 not unlocked at start");
    check(career::gigUnlocked(st, *band, 2), "gig 2 not unlocked at start");
    check(career::gigUnlocked(st, *band, 3), "gig 3 not unlocked at start");
    check(!career::gigUnlocked(st, *band, 4), "gig 4 should be locked at start");
    check(shop::cash(bandInfo) == 0, "fresh band cash != 0");
    check(!career::gigCompleted(st, *band, 1), "gig 1 already complete at start");

    // (d) Autoplay gig 1 to 5 stars: expected cash per song from the real table.
    const career::Tier* t1 = band->tier(1);
    if (!check(t1 && !t1->songs.empty(), "band tier 1 has no songs")) return failures;
    career::Performance perf;      // expert (index 4), 5 stars, 100% (perfect), score beats 0
    perf.difficultyIndex = 4;
    perf.stars = 5;
    perf.perfect = true;
    perf.scoreImproved = true;
    perf.score = 500000;
    const int expectedPerSong = career::cashForSong(g, perf);
    std::printf("careertest: cash per 5-star perfect expert song = $%d (stars_5 + perfect + score, from cash_milestones)\n",
                expectedPerSong);
    check(expectedPerSong > 0, "cash_milestones table gave $0 for a 5-star song");

    const int cashBefore = shop::cash(bandInfo);
    int awarded = 0;
    for (uint32_t song : t1->songs) {
        career::SongWonResult r = career::recordSongResult(st, bandInfo, g, *band, song, perf);
        awarded += r.cashAwarded;
        check(r.cashAwarded == expectedPerSong,
              std::string("song ") + nm(song) + " awarded $" + std::to_string(r.cashAwarded) +
                  " expected $" + std::to_string(expectedPerSong));
        check(r.newStars == 5, std::string("song ") + nm(song) + " stars != 5");
    }
    const int cashAfter = shop::cash(bandInfo);
    const int expectedTotal = expectedPerSong * int(t1->songs.size());
    std::printf("careertest: gig1 (%zu songs) awarded $%d; band cash %d -> %d\n",
                t1->songs.size(), awarded, cashBefore, cashAfter);
    check(awarded == expectedTotal, "total cash awarded wrong");
    check(cashAfter == cashBefore + expectedTotal, "band_info.cash not incremented by the award");
    check(career::isGigComplete(st, *band, 1), "gig 1 not complete after 5-starring every song");

    // (e) Complete gig 1: venue + next gig + reward.
    career::GigCompleteResult gc = career::completeGig(st, bandInfo, g, *band, 1);
    check(gc.gigNewlyCompleted, "gig 1 did not newly complete");
    check(career::gigCompleted(st, *band, 1), "gig 1 flag not set complete");
    check(gc.venueFlag != 0 && career::gigStarted(st, *band, 1), "venue/started not set");
    const bool gig4now = career::gigUnlocked(st, *band, 4);
    check(gig4now, "gig 4 not unlocked after completing gig 1");
    check(std::find(gc.unlockedGigs.begin(), gc.unlockedGigs.end(), 4) != gc.unlockedGigs.end(),
          "gig 4 not in the unlocked list");
    check(!gc.unlockedCarPieces.empty(), "gig 1 granted no reward car pieces");
    std::printf("careertest: completed gig1 -> venue %s unlocked, gigs {", nm(gc.venueFlag).c_str());
    for (int n : gc.unlockedGigs) std::printf(" %d", n);
    std::printf(" } unlocked, %zu CAS reward pieces granted, %zu reward chars\n",
                gc.unlockedCarPieces.size(), gc.unlockedCharacters.size());

    // (f) Close the 7.4 loop: a reward CAS piece is a $locked store item that is
    // NotUnlocked before the gig and buyable (unlocked) after.
    {
        std::vector<shop::Item> cat = shop::catalog(g);
        const shop::Item* rewardItem = nullptr;
        for (const auto& [part, desc] : gc.unlockedCarPieces) {
            for (const shop::Item& it : cat)
                if (it.part == part && it.descId == desc && it.locked) { rewardItem = &it; break; }
            if (rewardItem) break;
        }
        if (check(rewardItem != nullptr, "no reward piece is a $locked store item")) {
            // A fresh band that has NOT done the career cannot buy it.
            qb::Struct fresh;
            career::TagStore st2;
            career::startCareer(st2, fresh, *band);
            shop::Item buyCopy = *rewardItem;
            buyCopy.priced = true;  // force the purchase path so we exercise the unlock gate
            buyCopy.price = std::max(1, rewardItem->price);
            // ensure some cash to isolate the gate
            for (qb::Item& x : fresh.items)
                if (x.name == qbkey::key("cash")) { x.value.type = qb::Type::Int; x.value.i = 100000; }
            const shop::Buy before = shop::purchase(fresh, buyCopy);
            check(before == shop::Buy::NotUnlocked,
                  std::string("locked reward not gated before career: ") + shop::buyName(before));
            // The band that completed gig 1 now has the unlock flag.
            check(shop::partUnlockedFlag(bandInfo, rewardItem->part, rewardItem->descId),
                  "reward piece not unlocked in the career band_info");
            check(shop::partUnlocked(bandInfo, *rewardItem), "store still sees the reward piece locked");
            std::printf("careertest: reward %s/%s was NotUnlocked pre-career, unlocked post-gig1 (7.4 loop closed)\n",
                        nm(rewardItem->part).c_str(), nm(rewardItem->descId).c_str());
        }
    }

    // (g) Persist: the TagStore + band_info survive a save write/read round-trip.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "gd07_careertest";
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        save::setFolder(dir.string() + "/");
        save::Tags tags = st;  // the career tags
        tags[qbkey::key("band_info")] = bandInfo;
        std::string err;
        check(save::write(tags, save::Sections{}, err), "save write failed: " + err);
        save::Tags readTags;
        save::Sections readSec;
        const auto& reward0 = gc.unlockedCarPieces.front();
        if (check(save::read(readTags, readSec, err), "save read failed: " + err)) {
            career::TagStore rst(readTags.begin(), readTags.end());
            auto bi = readTags.find(qbkey::key("band_info"));
            if (check(bi != readTags.end(), "band_info missing after reload")) {
                check(shop::cash(bi->second) == cashAfter, "cash wrong after reload");
                check(shop::partUnlockedFlag(bi->second, reward0.first, reward0.second),
                      "reward unlock lost after reload");
            }
            check(career::gigCompleted(rst, *band, 1), "gig 1 completion lost after reload");
            check(career::gigUnlocked(rst, *band, 4), "gig 4 unlock lost after reload");
        }
        save::setFolder("");
        fs::remove_all(dir, ec);
    }

    std::printf("careertest: %s (%d checks failed)\n", failures ? "FAIL" : "all career checks passed", failures);
    return failures ? 1 : 0;
}
