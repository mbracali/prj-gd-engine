// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/camera.h"
#include "render/particles.h"
#include "render/scene_gpu.h"
#include "render/spotlight_view.h"

class World;
class CrowdView;
class BandView;
namespace gfx {
class Renderer;
}

// Draws a venue (game/world.h) the way GH2's RndDir::DrawShowing does
// [I: GH2's draw list, from later Harmonix code: the directory's drawables
// and its sub-directories', less those a Group draws, sorted by draw_order,
// each Group drawing its members in order with its Environ selected while it
// draws]: meshes with their Mats (MatAnim / pose state, next passes, skinned
// ones from their bones, MeshAnim vertices), particle systems (render/
// particles.h, run on the world clock), proxy directories at their
// transforms, and the Environs' ambient, lights and fog as EnvAnim /
// LightAnim / the lighting preset leave them (ScenePose). Spotlight beams,
// flares, the crowd and the band are the next phases'.
//
// The camera is a CamShot keyframe (the director picks them in the next
// phase): its transform (the shot's parent ignored for now), field of view
// (y, radians), near and far.
class VenueView {
public:
    VenueView(World& world, gfx::SceneCache& cache);
    ~VenueView();

    // The camera from `shot`'s keyframe `k`.
    void setShot(const scene::CamShot* shot, size_t k = 0);
    const scene::CamShot* shot() const { return shot_; }
    void setCamera(const gfx::Camera& c) { camera_ = c; }
    const gfx::Camera& camera() const { return camera_; }

    // Runs the particle systems to the world clock (call once a frame, after
    // World::update).
    void update();
    // Draws into the bound target: `viewW` x `viewH` pixels (the camera's
    // aspect follows it); `hdr` when drawing into the post-process's HDR
    // buffer (render/post_fx.h).
    void draw(gfx::Renderer& r, int viewW, int viewH, bool hdr);

    int meshesDrawn() const { return meshes_; }
    // The spotlights, flares and their beams (render/spotlight_view.h), drawn after the list.
    const gfx::SpotlightView& spotlights() const { return *spotlights_; }
    int particlesDrawn() const { return particles_; }
    size_t roots() const { return roots_.size(); }
    const CrowdView& crowdView() const { return *crowdView_; }
    BandView& bandView() { return *bandView_; }

private:
    struct Root {
        const scene::Object* object;
        const scene::Environ* env;  // the directory's environ
    };
    void build();
    void drawObject(gfx::MaterialRenderer& mr, const scene::Object* o, const scene::Environ* env, const scene::Xfm* base, int depth);
    void drawMesh(gfx::MaterialRenderer& mr, const scene::Mesh& m, const scene::Xfm* base);
    void selectEnv(gfx::MaterialRenderer& mr, const scene::Environ* env);
    scene::Xfm worldOf(const scene::Object* o, const scene::Xfm* base) const;

    World& world_;
    gfx::SceneCache& cache_;
    gfx::Camera camera_;
    const scene::CamShot* shot_ = nullptr;
    std::vector<Root> roots_;
    // A proxy's milo: its own draw list (drawn at the proxy's transform).
    std::unordered_map<const scene::Object*, std::vector<Root>> proxyRoots_;
    gfx::ParticleSet particleSet_;
    std::unordered_map<const scene::ParticleSys*, size_t> systemIndex_;
    std::unique_ptr<gfx::ParticleRenderer> particleRenderer_;
    std::unique_ptr<gfx::DynamicMesh> dynamic_;
    std::unique_ptr<gfx::SpotlightView> spotlights_;
    std::unique_ptr<CrowdView> crowdView_;
    std::unique_ptr<BandView> bandView_;
    const scene::Environ* currentEnv_ = nullptr;
    bool envSet_ = false;
    bool hdr_ = false;
    int viewW_ = 1280, viewH_ = 720;
    int meshes_ = 0, particles_ = 0;
};

// Where a band slot stands before the band exists (M9): guitarist0 /
// guitarist1 / singer / keyboard / bassist / drummer -> the venue's
// waypoint with that slot's start flag (kStartGuitarist0 1, kStartGuitarist1Mp
// 2, kStartSinger 4, kStartKeyboardist 8, kStartBassist 16, kStartDrummer 32,
// kStartGuitarist0Mp 512; macros.dta [C data]), as {waypoint_find} places
// them (chars set_singleplayer: guitarist0 start_at); other objects: their
// world transform. False when nothing is found.
bool venueObjectXfm(const World& world, const std::string& name, bool multiplayer, scene::Xfm& out);

// A CamShot keyframe as a static camera (the director's full rules are the
// next phase's, PLAN.md section 3): the keyframe transform under its parent
// (translation only unless use_parent_rotation) and its path TransAnim at
// `frame`; with targets, turned to look at their mean (a bone subpart aims
// 40 units above the slot) [I: no screen offset, no filter, no shake].
gfx::Camera shotCamera(const World& world, const scene::CamShot& shot, size_t keyframe, float frame = 0);

// gd02 --view world/<v>/gen/<v>.milo_xbox (or --view <venue>): the venue
// viewer. The chain loads as in the game, the world starts (start,
// single_player, intro_start) and its clock runs at 120 beats a minute.
// Left / Right: the previous / next camera shot (Up / Down its keyframes);
// P: the next lighting preset; 1-5: hit_p0_fret1..5; K kick_drum, B
// bass_hit, E venue_effect, G excitement_great, O excitement_okay, X
// excitement_bad. GD02_VIEW_CAM=<n> starts at shot n, GD02_VIEW_SHOT=<name>
// at that shot, GD02_VIEW_CATEGORY=<category> at its first shot;
// GD02_VIEW_PRESET=<name>|none the first lighting preset (default
// INTRO's); GD02_VIEW_TIME=<s> runs the clock that far first;
// GD02_SCREENSHOT=<file.bmp> saves a frame and quits.
int runVenueView(const std::string& dataPath, const std::string& venueOrMilo);
// True when `path` names a venue (a venue name or a world milo).
bool isVenuePath(const std::string& path);
