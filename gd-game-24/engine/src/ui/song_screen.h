// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// A song on the original highway (GD24-002, the first boot): the four
// instrument tracks side by side, built from COMMON.STR's models
// (data/highway_assets), the chart of one difficulty scrolling in time with
// the song's preview (the encrypted .ATM audio is out of scope, DECISIONS
// D6), beat lines from the chart's ticks, the player's lanes judged
// (game/song_play), track switching.
//
// Placement (docs/FORMATS.md "The highway's placement"):
//   [C data] model units, x across, y along, z up; the track plane 3.21 x
//            12.92 (x -1.60..1.61, y -6.56..6.36), its surface ("base")
//            2.93 wide, the beat bars 2.90, the gems 0.69, the lane
//            plates 0.75 wide;
//   [C data] the lanes and the strike line: the track plane's four lane
//            marks (red / yellow / green / blue atlas row) at x -1.10
//            -0.36 0.38 1.13, y -4.49; the plates (0.75 wide) sit on them;
//   [I]      the tracks side by side, one track width + 0.1 apart; the
//            camera chosen to frame the track (not found in the code or the
//            data yet: GD24_CAMERA="eye x,y,z,target x,y,z,fov" overrides);
//   [C data, I use] the speed: (game (track (y_per_second 7.5 8.5 9.5 11.5)))
//            units per second for Easy..Expert;
//   [C data + I] the preview AT3 starts at the song's (preview <start ms>
//            ...), corrected by the measured onset lag (game/preview_sync.h).
#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "audio/at3_preview.h"
#include "data/beatmatch.h"
#include "data/highway_assets.h"
#include "data/song_config.h"
#include "game/song_play.h"
#include "input/input.h"
#include "render/highway_renderer.h"

class AudioOut;
class Canvas;

namespace gd24 {
class Umd;
struct SongEntry;
}  // namespace gd24

class SongScreen {
public:
    static const char* difficultyName(int d);

    ~SongScreen();
    // Loads the chart and starts decoding the preview; false + `error`.
    bool start(const gd24::Umd& umd, const gd24::SongEntry& song, int difficulty, int track, std::string& error);
    void stop(AudioOut& audio);
    void release();  // the GPU objects, before the device goes
    // Every frame before drawing.
    void update(AudioOut& audio);
    // A gameplay action (input.h's gameplayOf applied); false: leave the song.
    bool action(Action a, bool down, AudioOut& audio);
    // The HUD on the (transparent) canvas, then the 3D picture under it.
    void drawOverlay(Canvas& c);
    gd::core::render::Texture draw3d(gd::core::render::Device& dev, gd::core::render::Texture overlay);

    double songMs() const { return songMs_; }
    const gd24::SongPlay& play() const { return play_; }
    const gd24::SongConfig& config() const { return cfg_; }
    bool ready() const { return audioStarted_; }

private:
    float yPerSecond() const;
    float trackX(int t) const;

    gd24::HighwayAssets assets_;
    bool assetsLoaded_ = false;
    std::string assetsError_;
    gd24::HighwayRenderer renderer_;
    gd24::SongPlay play_;
    gd24::SongConfig cfg_;
    std::string title_;
    std::future<std::shared_ptr<PcmClip>> decoding_;
    bool audioStarted_ = false;
    double songMs_ = 0, lastPos_ = -1;
    int lagMs_ = 0, measuredLag_ = 0;  // the preview's measured place (game/preview_sync.h)
    float camTrack_ = 2;  // the camera's track position (eases to the active track)
    uint64_t lastNs_ = 0;
    bool lanesDown_[4] = {};
    bool autoplay_ = false;
    float laneX_[4] = {-1.11f, -0.37f, 0.37f, 1.11f}, strikeY_ = -4.49f;  // from the track plane (laneMarks)
    double laneFlash_[4] = {-1e9, -1e9, -1e9, -1e9};
};
