// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

namespace gd24 {
class Umd;
}

// gd24 --charttest <disc> [<song> | all] [--notes]: reads each song's
// BEATMATCH.STR (data/beatmatch.h): the four difficulty images and their
// difficulty words, the image base (kPad_* symbols), the four tracks (PART
// DRUMS / BASS / GUITAR / VOCALS) with their Easy / Medium / Hard / Expert
// gem counts, and checks: one tick has one time in every list (the shared
// tempo map), the Easy image gives the same gems. With
// one song: each list's range; --notes prints every gem. Exit code 1 on a
// failed check.
int chartTest(const gd24::Umd& umd, const std::string& which, bool notes);

// gd24 --rulestest <disc> [<song> | all] [--difficulty 0..3]: the gameplay
// rules (game/song_play.h) played headless over each song's whole chart:
//  1. perfect autoplay: the song passes, no phrase is missed, and every
//     score event agrees with the rule replayed from the event log alone
//     (multiplier = captures + 1 up to 5, reset by a phrase miss after the
//     capture timer, doubled in Overdrive with 5 -> 11; points = gem / capture
//     value x multiplier; the score = their sum);
//  2. an idle player: the song fails at the time predicted from the chart
//     alone (each gem nobody plays, each missed phrase start: the health
//     amounts; the crowd = mean health - failed tracks x penalty);
//  3. one gem left out after five captures: the multiplier goes back to 1,
//     the phrase is not captured.
// Exit code 1 on a failed check.
int rulesTest(const gd24::Umd& umd, const std::string& which, int difficulty);
