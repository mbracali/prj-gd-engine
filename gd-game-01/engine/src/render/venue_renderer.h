// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "game/guitar_anim.h"
#include "game/guitar_walk.h"
#include "game/venue_camera.h"
#include "game/venue_script.h"
#include "render/crowd_renderer.h"
#include "render/scene_renderer.h"
#include "scene/anim.h"
#include "scene/character.h"

class GameData;

// What happened in the song since the last venue update.
struct VenueFrame {
    float ms = 0, ticks = 0;
    int excitement = VenueScript::Okay;
    std::vector<std::string> events;    // EVENTS texts reached
    std::vector<std::string> upcoming;  // sections (verse/chorus/solo) now one bar away
    // EVENTS one beat away (FUN_002589b8: upcoming_<event>), with the event's beat.
    std::vector<std::pair<std::string, float>> soon;
    bool downbeat = false;              // a measure line was crossed
    // Beat lines crossed: (beats left to the next measure line, 0 on one; beat number).
    std::vector<std::array<int, 2>> beatLines;
    bool hitting = false;               // the player hit the last gem (hit_gem; cleared by miss / pass)
    bool hitting2 = false;              // face-off: player 2
    bool starMode = false;              // player 0's Star Power is active
};

// A venue (venues/<name>/gen/): its sections' scenes (the venue itself plus
// those its script loads, such as lighting), animated by the venue script and
// filmed by the venue camera.
class VenueRenderer {
public:
    // The guitarist: an archetype ("metal"), a guitar ("gibson_sg") and the
    // song's anim_tempo. An empty archetype leaves the stage empty.
    struct Band {
        std::string guitarist, guitar;
        int tempo = 1;
        // Face-off: player 2's guitarist (MyCharSys guitarist1); empty in single player.
        std::string guitarist2, guitar2;
        // The song's (band ...) lineup (SongInfo::band); empty = charsys.dta's (band ...).
        std::vector<std::string> lineup = {};
    };
    // Throws std::runtime_error if the venue cannot be loaded.
    VenueRenderer(Renderer& renderer, const GameData& data, const std::string& venue, const Band& band);

    void update(const VenueFrame& frame);
    // arena game_over: the guitarist plays kGuitarWin / kGuitarLose (set_game_over
    // FUN_00284fd8), the band kBandWin (kAnimFirst) / kBandLose (kAnimNow).
    // set_game_over; `finalWin`: the career's final song (kGuitarWinFinal, FINAL_WIN shot).
    void gameOver(bool won, bool finalWin = false);
    // A venue scene object, for the scripts (arena::<name> set_showing).
    SceneObject* findObject(const std::string& name);
    // Face-off: the section owners changed (camera_multiplayer.dta; FUN_00286298 hand activity).
    void activePlayers(int mask);
    // Face-off game over: each guitarist wins or loses (a draw: both win).
    void gameOverMulti(bool p1Won, bool p2Won);
    // Tutorial mode: the TUTORIAL camera shot and no crowd.
    void setTutorial() {
        if (camera_) camera_->setFixedGroup("TUTORIAL");
        tutorial_ = true;
    }
    // GuitarCharacter start_starpower: the Star Power move unless one is playing.
    void startStarPower();
    // A gem was hit; `slot` is its lowest fret ({arena hit_gem}).
    void hitGem(int slot) {
        if (script_) script_->hitGem(slot);
    }
    // The viewport's aspect (wider than 16:9 on ultrawide windows), from the next update on.
    void setAspect(float aspect) { aspect_ = aspect; }
    // The current shot's depth-of-field plane as a window depth, 0 when off.
    float dofDepth() const;
    // Enhanced Settings "Stronger Bokeh": focus nearer the subject, wider blur.
    void setStrongerBokeh(bool on) {
        strongerBokeh_ = on;
        if (camera_) camera_->setFocusScale(on ? 1.3f : 2.0f);
    }
    bool strongerBokeh() const { return strongerBokeh_; }
    // The shaky cam of the (shaky 1) shots (default on, as the original);
    // Enhanced Settings "Disable Shaky Camera" turns it off.
    void setShakyCamera(bool on) {
        shakyCamera_ = on;
        if (camera_) camera_->setShakyCamera(on);
    }
    void setDepthOfField(bool on);
    // [I] Enhanced Settings > All Band Mates Cast Shadows: every band mate gets
    // the guitarist's treatment, a spotlight overhead with its floor glow and the
    // flattened depth-only silhouette that cuts the glow.
    void setBandShadows(bool on) { bandShadows_ = on; }
    // [I] Enhanced Settings > More Lights on Venues: 0 off, 1 subtle, 2 strong.
    // Brighter light cones and glows, a soft beam and floor pool on each band
    // member in the stage spotlight's colour, a little fill on the band.
    void setMoreLights(int level);
    // [I] Enhanced Settings > Dust in Light Beams (with More Lights): motes drifting in the beams.
    void setLightDust(bool on) { lightDust_ = on; }
    // The guitarist's hand events for this song and chart (GuitarAnimInfo).
    void setSong(const Song& song, const Chart& chart);
    // game_outro: the guitarist's game-over clip reached its last frame.
    bool outroDone() const;
    // The singer's microphone (band_chars singer: bone_pos_mic.mesh, the bone
    // mic_stand.mesh hangs from) in world space; false when the song has no singer.
    bool singerMicPos(std::array<float, 3>& out) const;
    // A bone of the singer (e.g. "bone_head.mesh") in world space.
    bool singerBonePos(const std::string& bone, std::array<float, 3>& out) const;
    // [I] A fixed view replacing the shots (the Enhanced Settings backdrop).
    // `camera` (rows X, Y forward, Z up; at the eye) faces the billboards and the crowd.
    void setStillView(const Mat4& viewProj, const Xfm& camera) { stillVp_ = viewProj, stillCam_ = camera, still_ = true; }
    // For the post-processing (ground fog, TAA): the view-projection drawn
    // last, and the stage floor's height (the guitarist's root), false without one.
    const Mat4& viewProj() const { return vp_; }
    bool stageFloorZ(float& z) const {
        if (!guitarist_) return false;
        z = guitarist_->placement().pos[2];
        return true;
    }
    // [I] For Super Fog's light shafts: each lit band member's light (the
    // guitarist's is the venue's spotlight), as its position, the member's
    // feet and the stage spotlight's colour (0..1), 9 floats each.
    std::vector<std::array<float, 9>> beamLights();
    void draw();

private:
    struct Section {
        std::string name;
        std::unique_ptr<Scene> scene;
        std::unique_ptr<SceneRenderer> renderer;
    };
    std::vector<Section> sections_;
    std::unique_ptr<Scene> campaths_;  // camera path anims (not drawn)
    std::unique_ptr<Scene> fx_;
    std::string guitaristName_;
    // VenueSpotLight (docs/spotlight.md): the light aimed at guitarist0's feet
    // and the floor glow (fx.rnd floorspot_char.mesh with the venue's floorspot_glow.mat).
    std::string spotLight_;
    float floorSpotHeight_ = 1, floorSpotScale_ = 100;
    std::unique_ptr<SceneRenderer> fxRenderer_;
    Xfm floorSpot_;
    bool floorSpotOn_ = false;
    void updateSpotLight();
    // The floor ellipse under a light at L aimed at t (FUN_00179420's floor half).
    bool floorSpotFor(const std::array<float, 3>& L, const std::array<float, 3>& t, Xfm& out) const;
    // The enhanced lights: per lit character, its light, then shadows / pools / beams / dust.
    struct LitChar {
        Character* character;
        std::array<float, 3> light;
        bool main;  // guitarist0: the venue's own spotlight and shadow
    };
    std::vector<LitChar> litCharacters();
    void drawEnhancedLights(const Mat4& vp, const Xfm& cam, const std::vector<LitChar>& lit);
    bool bandShadows_ = false, lightDust_ = true;
    int moreLights_ = 0;
    std::unique_ptr<Scene> shadowMatScene_;  // charsys_shadow.mat when the guitarist has none
    const MatObj* shadowMat_ = nullptr;
    Renderer* renderer_ = nullptr;
    int fxMeshes_[3] = {0, 0, 0};
    int fxMeshNext_ = 0;
    static constexpr uint32_t kGuitarWinFinal = 0x4000000, kGuitarLoseWalk = 0x8000000;        // arena fx.rnd: shaky_cam1.tnm, floorspot_char.mesh
    std::unique_ptr<VenueScript> script_;
    std::unique_ptr<VenueCamera> camera_;
    std::unique_ptr<AnimPlayer> anims_;
    std::unique_ptr<Character> guitarist_;
    std::mt19937 rng_{1};
    std::unique_ptr<GuitarHands> hands_;
    // Face-off (docs/multiplayer_plan.md §9): guitarist1 on walk_spot_01, no walks.
    std::unique_ptr<Character> guitarist2_;
    std::unique_ptr<GuitarHands> hands2_;
    int activeMask_ = 0;
    bool onScreen(const Character& c) const;
    std::string snapShot_;
    bool shakyCamera_ = true;
    bool strongerBokeh_ = false;  // the shot of the last frame (the cut snap)
    std::vector<Xfm> spots2_;          // the walk spots, for the face-off standing guard
    float lastGuardBeat_ = -1e30f;
    float lastRootLogBeat_ = 0;  // GD01_ROOTLOG (development aid)
    bool rootLogInit_ = false;
    Xfm rootLog0_;
    bool won2_ = false;
    std::unique_ptr<Character> makeGuitarist(Renderer& renderer, const GameData& data, const std::string& archetype,
                                             const std::string& guitar, int tempo);
    std::unique_ptr<GuitarWalk> walk_;
    GuitarWalk::Config walkConfig_;
    bool placed_ = false, wonGame_ = false;
    std::vector<Xfm> walkSpots();
    const GameData* data_ = nullptr;
    int guitarState_ = 0;  // GuitarCharacter +0x108: 0 idle, 1 playing, 2 wail, 3 solo
    bool introPlayed_ = false, gameOver_ = false;
    std::string lastShot_ = "-";
    std::string postSwitchShot_;  // the shot post_switch_cam last ran for
    int lastBeat_ = -1;
    bool tutorial_ = false;
    float aspect_ = 16.0f / 9.0f;
    bool walkingNow() const { return walk_ && guitarist_ && walk_->walking(*guitarist_); }
    int walkSpotNow() const { return walk_ && guitarist_ ? walk_->nearestSpot(guitarist_->placement().pos) : 0; }
    // The band (charsys.dta (band ...) on the stage spots of band_spots).
    struct Member {
        std::string role;  // singer, bass, drummer, keyboard
        std::unique_ptr<Character> character;
        int spot = 0;      // stage_spot_<spot + 1>
        float frameScale = 1;  // <part>_half_tempo / _double_tempo / _normal_tempo
    };
    std::vector<Member> band_;
    std::unique_ptr<CrowdRenderer> crowd_;
    void loadBand(Renderer& renderer, const GameData& data, int tempo, std::vector<std::string> lineup);
    // A spot's position and facing, without the marker's scale.
    bool spotPlacement(const std::string& spot, Xfm& out);
    float ms_ = 0, ticks_ = 0;
    Mat4 vp_;  // the camera of the last update
    Mat4 stillVp_;
    Xfm stillCam_;
    bool still_ = false;
    int excitement_ = -1;
    bool started_ = false;
};
