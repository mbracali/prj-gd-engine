// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// Enhanced > Cheats & Enhancers > Song Speed and Track Speed (GDALL-053,
// GDALL-054, GDALL-051): the same keys, values and rules in every game, so the
// launcher can share the choice later. enhanced.json:
//   "song_speed":  "50" .. "200" (percent, steps of 10), default "100"
//   "track_speed": "0.5" .. "3.5" (x the difficulty's own track speed, steps of 0.25), default "1"
// GH2 Deluxe's rules (docs/spikes/study_gh2dx.md): the song plays at the song
// speed (here with its pitch kept, time_stretch.h), the track speed is divided
// by the song speed so the gems keep their speed on screen, and above 100 %
// the hit window (slop) is multiplied by the song speed (at or below 100 % it
// stays). A song not at 100 % saves no record (as practice). [I] Enhancements.

#include <cmath>
#include <cstdlib>
#include <string>

namespace gd::core::speed {

// The values in menu order (the dropdowns' rows), and the defaults.
inline constexpr const char* kSongSpeeds[] = {"50", "60", "70", "80", "90", "100", "110", "120",
                                               "130", "140", "150", "160", "170", "180", "190", "200"};
inline constexpr const char* kTrackSpeeds[] = {"0.5", "0.75", "1", "1.25", "1.5", "1.75", "2",
                                                "2.25", "2.5", "2.75", "3", "3.25", "3.5"};
inline constexpr const char* kSongSpeedDefault = "100";
inline constexpr const char* kTrackSpeedDefault = "1";

inline bool validSongSpeed(const std::string& v) {
    for (const char* s : kSongSpeeds)
        if (v == s) return true;
    return false;
}
inline bool validTrackSpeed(const std::string& v) {
    for (const char* s : kTrackSpeeds)
        if (v == s) return true;
    return false;
}
// "150" -> 1.5; anything else 1.
inline double songSpeed(const std::string& v) { return validSongSpeed(v) ? std::atof(v.c_str()) / 100.0 : 1.0; }
// "1.25" -> 1.25; anything else 1.
inline double trackSpeedFactor(const std::string& v) { return validTrackSpeed(v) ? std::atof(v.c_str()) : 1.0; }
inline bool isNormalSpeed(double songSpeed) { return std::fabs(songSpeed - 1.0) < 1e-6; }

// The highway's track_speed: the difficulty's x the Track Speed / the Song Speed.
inline float scrollSpeed(float difficultySpeed, double trackFactor, double songSpeed) {
    return float(double(difficultySpeed) * trackFactor / (songSpeed > 0 ? songSpeed : 1.0));
}
// The hit window (song ms): x the song speed above 100 %.
inline float slopMs(float slop, double songSpeed) { return songSpeed > 1.0 ? float(double(slop) * songSpeed) : slop; }
// A run saves records only at 100 % (a slower or faster song is practice).
inline bool savesRecords(double songSpeed) { return isNormalSpeed(songSpeed); }

}  // namespace gd::core::speed
