// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <deque>
#include <memory>

#include "game/chart.h"
#include "game/player.h"
#include "render/scene_renderer.h"

class GameData;

// The note highway, drawn from the game's track/gen/track.rnd_ps2 the way the
// original places it (see FINDINGS.md "Highway").
class TrackRenderer {
public:
    ~TrackRenderer();

public:
    // Throws std::runtime_error if the track scene cannot be loaded.
    TrackRenderer(Renderer& renderer, const GameData& data);

    // `trackSpeed` is the track_speed value for the difficulty; `slopMs` the hit window.
    // `hitFlash` holds a 0..1 flash level per fret (1 = just hit).
    // extend_track.tnm on the track camera: frame 0 = rolled in (camera 127
    // units down the track), 1920 = out (rest); 2000..2025 = the Star Power shake.
    void setExtendFrame(float frame);
    // track_panel messages: pop_smasher i, set_smasher_glowing i B (i = -1: all), reset, explode.
    void popSmasher(int fret);
    void setSmasherGlowing(int fret, bool on);
    void reset();
    void explode();
    // Multiplayer face-off: player 0 (left) or 1 (right).
    void setFaceOff(int player);
    // Lefty Flip (Options > Game Settings, per player): the lanes mirrored, green
    // on the right. FUN_00163828: a lane's x uses 4 - lane when lefty, and the
    // hit effects' fan angle is negated.
    void setLefty(bool on) { lefty_ = on; }
    bool lefty() const { return lefty_; }
    // The player's character's highway surface (characters.dta track_surface,
    // put on track_surface.tex as FUN_0015c540 does); false when not found.
    bool setSurface(const GameData& data, const std::string& character);
    // The win explosion is playing (it outlives the song).
    bool exploding() const;
    // Where lane `fret` is on screen `along` units up the highway (0 = the
    // strike line), in menu units, as last drawn; false before the first draw.
    bool laneScreen(int fret, float along, float& mx, float& my) const;
    void draw(const Player& player, float nowMs, float trackSpeed, float slopMs, const Song& song,
              const float* hitFlash);
    // Hit effects (FUN_00164988, docs/hit_fx.md): a gem with `frets` was hit at
    // `nowMs`. spState is the Star Power phrase state (0 none, 2 intact phrase,
    // 3 this hit captured it); `sustain` starts the burn on that gem.
    void hitGem(float nowMs, int gem, uint32_t frets, bool bonus, int spState, bool sustain);
    // A miss (FUN_00164ae8) or the burning sustain's end / release (FUN_00164c50): every effect stops.
    void stopHitEffects();

private:
    bool lefty_ = false;
    float laneX(int fret) const;
    Renderer& renderer_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<SceneRenderer> sr_;
    std::string surface_;  // the character whose track_surface is on
    std::unique_ptr<class AnimPlayer> anims_;
    int tailStrip(float x, float y0, float y1, bool played, float& maxS);
    int player_ = -1, rows_ = 30;  // face-off player (-1: single player)
    Mat4 screen_;                  // clip-space placement of this highway
    Mat4 lastVp_;                  // the last draw's view-projection (touch lanes)
    bool drawn_ = false;
    std::vector<MeshVertex> profile_;  // tail02.mesh's cross-section at y = 0, by x
    std::deque<float> whammy_;  // sustain whammy history (h = 3 * bar), newest first
    int whammyGem_ = -1;
    float whammyClockMs_ = 0;
    float lineW_ = 1, lineClockMs_ = 0;  // the sustain glow line's eased width
    float bonusStartMs_ = -1;
    // Smashers (GemSmasher): pop / hit start times (wall clock), script glow, last flash level.
    float popAt_[5] = {-1e9f, -1e9f, -1e9f, -1e9f, -1e9f}, hitAt_[5] = {-1e9f, -1e9f, -1e9f, -1e9f, -1e9f};
    bool glow_[5] = {};
    float lastFlash_[5] = {};
    float explodeAt_ = -1;
    int missesSeen_ = 0;  // Player::missCount() already popped
    // The one smash effect per track, replayed on every fret of the latest hit;
    // the phrase-capture burst + lightning; the sustain burn.
    const char* smashView_ = nullptr;
    uint32_t smashMask_ = 0, comboMask_ = 0;
    float smashT0_ = 0, comboT0_ = -1e9f;
    int burnGem_ = -1;
    // Star-tail whammy particles: the ramp g, the emitter's own rate, the tail's frets.
    float whammyRamp_ = 0, lastWhammyMs_ = 0;
    bool burnWhammy_ = false;  // the burn shows its whammy (blue) state
    bool tailPartBase_ = false;
    std::array<float, 2> tailEmitBase_{};
    uint32_t tailFrets_ = 0;
};
