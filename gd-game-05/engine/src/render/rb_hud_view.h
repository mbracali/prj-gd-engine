// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "render/rb_track_view.h"
#include "render/scene_gpu.h"
#include "scene/ui_styles.h"

class GameData;
class TempoMap;
namespace scene {
class Milo;
class MiloLibrary;
class Object;
class Directory;
class Environ;
class EventTrigger;
class Text;
}  // namespace scene
namespace gfx {
class Renderer;
class MaterialRenderer;
class SceneCache;
class TextDrawer;
struct TextLayout;
}  // namespace gfx

// Rock Band's in-song HUD (GD05-058), drawn from the game's own track
// panel: ui/track_panel.dta's coop_track_panel is track/trackpanel.milo, a
// TrackPanelDir whose draw_order.grp holds, besides the gem tracks
// (RbTrackView), the HUD's inline RndDirs [C data]:
//   star_power_0N   band_power_meter.milo: a track's Overdrive meter
//   streak_meter_0N streak_meter.milo: the multiplier dial (multiplier.txt,
//                   the wipe filling toward the next multiplier)
//   scoreboard      the band's score (score.txt) and stars
//   crowd_meter     BandCrowdMeterDir: the band (rock) meter, an icon per
//                   instrument
//   star_meter_multiplier  the band's Overdrive multiplier (not shown with
//                   one participant)
//   player_feedback_0N  solo percentage / rating pop-ups, bass super streak
//   failed_feedback_0N  FAILED / SAVED
//   ending_bonus    the Big Rock Ending
// Their places per player count are the panel's track_configuration objects
// (1_player_wide, 2_player_wide, 3_player_wide, vocal_only_wide: objects,
// visibles, xfms; track_paneldir.dta update) [C data], seen through the
// panel's Cam.cam (track_paneldir.dta reset: at the origin in widescreen)
// [C data]. Each element is driven as its RndDir type's script does
// (track/gen/<type>.dta: the triggers, the frames, the texts) from the
// players' state, which the TrackPanel's poll sends (RB 0x82803750 [C]:
// each scoreboard's set_score / set_num_stars from the band, the panel's
// set_multiplier); the per-track senders are not traced [I].
class RbHudView {
public:
    // One gem track's player.
    struct Track {
        std::string part;  // guitar, bass, drum
        std::string name;  // the player's name (player_intro, player_feedback)
        float introAtMs = -1e30f;  // the track's play_intro (gem_track.dta)
        int streak = 0;
        int streakMultiplier = 1;  // the streak's own (1..4, bass 6)
        int totalMultiplier = 1;   // with the band's Overdrive multiplier
        int maxMultiplier = 4;
        float energy = 0;          // Overdrive 0..1
        bool deploying = false;
        float rating = 0.5f;       // the player's crowd rating 0..1
        bool failed = false;
        bool soloActive = false;
        int soloPercent = 0;
    };
    struct State {
        std::vector<Track> tracks;  // in the panel's gem track order
        bool vocals = false;
        int bandScore = 0;
        float stars = 0;
        float crowd = 0.5f;         // the band's rating 0..1
        int excitement = 2;         // 0..4 (kExcitementPeak 4)
        int bandMultiplier = 1;
        int participants = 1;
        bool won = false, lost = false;
        bool coda = false;
        int codaScore = 0;
        // The song's tempo: the triggers' anims at k1_fpb / k480_fpb count
        // beats (render/anim_rate.h); none: 120 beats a minute.
        const TempoMap* tempo = nullptr;
    };
    // An element's box on the 1280 x 720 picture: from the milo data
    // (its shown meshes through the panel camera).
    struct Box {
        std::string name;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool valid = false;
        // An element's own meshes (strict: the pixels must match), or one
        // of its nested directories ("crowd_meter/guitar": an instrument's
        // arrow, a scoreboard star), whose textures leave clear edges, so
        // only its pixels inside the box are checked.
        bool strict = true;
    };

    RbHudView();
    ~RbHudView();
    // Throws std::runtime_error.
    void load(const GameData& data, scene::MiloLibrary& library);
    bool loaded() const { return panel_ != nullptr; }
    // A new song: the panel's reset (reset_all.trig, every element's reset).
    // `introAtMs`: when the panel's intro (on_extend: intro.trig) plays.
    // `tempo`: the new song's (the reset runs the panel's reset anims at
    // their beat rates; the view keeps no pointer into an older song, whose
    // chart a retry frees: GD05-086).
    void reset(float introAtMs, const TempoMap* tempo);
    float introAt() const { return introAtMs_; }
    // Draws the HUD over the picture (its own camera; depth cleared).
    void draw(gfx::Renderer& r, gfx::SceneCache& cache, const State& state, float nowMs);
    // Draws only element `name` without its nested directories, or only
    // the nested "element/sub" (the walks' pixel probe), state unchanged.
    void drawOnly(gfx::Renderer& r, gfx::SceneCache& cache, const std::string& name);
    // The elements a solo / band player sees now and their boxes from data.
    std::vector<Box> expectedBoxes() const;
    const std::string& configuration() const { return config_; }
    // Where each gem track draws in the current configuration (x, y, w, h
    // of the picture): the one-player track's picture scaled and moved so
    // its now bar sits on that track's Overdrive meter (star_power_0N's
    // place in the configuration) [I: RB draws the tracks through one
    // camera with x_offset / side_angle; the TrackDir's camera code is not
    // read]. Empty with one track.
    const std::vector<std::array<float, 4>>& trackRects() const { return trackRects_; }
    // The one-player configuration's track camera and speed (1_player_wide:
    // pitch, fov, y_pos, z_pos, fade_start, fade_length; the panel's
    // view_time_easy / _expert), the type's defaults for what it leaves out
    // [C data: track_paneldir.dta]. The band's tracks are this picture moved
    // onto their meters (trackRects).
    RbTrackView::Config trackConfig() const;
    // The last draw's directories (the elements and their nested ones) and
    // the pairs in them drawn out of draw order (render/draw_order.h): the
    // walks' check, 0 as RB's RndDir draws (GD05-070).
    int drawOrderInversions() const { return orderInversions_; }
    int drawOrderDirs() const { return orderDirs_; }
    // What the elements show (the walks compare these with the players).
    struct Shown {
        std::string score, multiplier;  // score.txt, streak_meter_01's multiplier.txt ("" hidden)
        float energyFrame = -1, crowdFrame = -1, wipeFrame = -1;
    };
    Shown shown(int track = 0) const;

private:
    struct Elem {
        std::string name;
        const scene::Object* owner = nullptr;  // the panel's RndDir object
        scene::Directory* dir = nullptr;       // its inline directory
    };
    const TempoMap* tempo_ = nullptr;  // the song's, for the beat rates
    bool countOrder_ = false;
    int orderInversions_ = 0, orderDirs_ = 0;
    struct Task {
        const scene::Object* anim = nullptr;
        float from = 0, to = 0, scale = 1;
        float t0 = 0;
        int type = 0;  // 0 range, 1 loop, 2 a hide later
        int rate = 0;  // the entry's RndAnimatable rate (render/anim_rate.h)
    };
    const scene::Object* panelObj(const std::string& name) const;
    scene::Directory* subdir(const std::string& name) const;
    const scene::Object* in(const std::string& elem, const std::string& name) const;
    void applyConfiguration(const std::string& name);
    bool trigger(const scene::Directory* d, const std::string& name, float nowMs);
    void trigger(const scene::EventTrigger& t, float nowMs);
    void animate(const scene::Object* anim, float from, float to, int type, float scale, float startMs, int rate = 0);
    void setFrame(const scene::Object* anim, float frame);
    bool applyPropAnim(const scene::Object& anim, float frame);
    void runTasks(float nowMs);
    void setText(const std::string& elem, const std::string& text, const std::string& value);
    void drive(const State& s, float nowMs);
    void setEnv(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env);
    void drawElem(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Object* o);
    void drawDir(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Directory& d, const scene::Environ* env, int depth);
    void drawObject(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Object* o, const scene::Environ* env, int depth);
    void begin(gfx::Renderer& r, gfx::SceneCache& cache);
    void setCamera();

    scene::Milo* panel_ = nullptr;
    const scene::Object* cam_ = nullptr;
    std::string config_;
    std::vector<std::string> configured_;  // the objects the configuration placed
    gfx::ScenePose pose_;
    gfx::Camera camera_;
    const scene::Environ* currentEnv_ = nullptr;
    bool frameSet_ = false;
    int viewW_ = 1280, viewH_ = 720;
    std::vector<Task> tasks_;
    std::map<const scene::Object*, float> animFrame_;
    std::map<const scene::Object*, std::string> texts_;  // set_text overrides
    std::unique_ptr<gfx::TextDrawer> textDrawer_;
    gfx::SceneCache* cache_ = nullptr;
    std::vector<std::string> hiddenElems_;  // not drawn here (the tracks, vocals ...)
    float lastMs_ = -1e30f, introAtMs_ = -1e30f;
    bool introPlayed_ = false;
    // What each element was last sent (its script's own fields).
    struct TrackSeen {
        int mult = 1, total = 0;
        int state = -1;  // 0 none (filling), 1 ready, 2 deploying
        float energy = -1;
        bool failed = false, solo = false, intro = false;
        bool deploying = false, iconFailed = false;  // crowd_meter's icon deploy / stop_deploy, state sent
    };
    std::array<TrackSeen, 3> seen_{};
    int crowdDeployed_ = 0;  // crowd_meter.dta [deployed]: the members deploying
    int fxCrowdTriggers_ = 0;  // crowd meter deploy / state triggers fired (the walks)

public:
    int crowdMeterTriggers() const { return fxCrowdTriggers_; }

private:
    std::string scoreShown_;
    int bandMultShown_ = 0;
    bool wonSeen_ = false, lostSeen_ = false, codaSeen_ = false;
    int excitementSeen_ = -1;
    int starsSeen_ = -1;
    std::vector<std::array<float, 4>> trackRects_;
    std::array<float, 3> refMeter_{640, 653, 516};  // 1_player_wide's meter: centre x, y, width (px)
    bool meterBox(int index, float& cx, float& cy, float& w) const;
    // BandLabels: their type's template text (ui/ui_objects.dta) and the locale.
    const GameData* data_ = nullptr;
    scene::MiloLibrary* lib_ = nullptr;
    scene::UiStyles styles_;
    scene::Locale locale_;
    std::map<std::string, scene::Milo*> resources_;
    std::array<std::string, 3> introSet_{};
    // Each text's quads as last laid out (world space), for expectedBoxes.
    std::map<const scene::Object*, std::vector<std::array<float, 3>>> textQuads_;
    void keepQuads(const scene::Object* o, const gfx::TextLayout& layout, const scene::Xfm& world);
    void drawLabel(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Object& o, const scene::Environ* env);
    std::string onlyElem_, onlySub_;  // drawOnly
    bool suppress_ = false;
};
