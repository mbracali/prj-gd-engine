// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/venue_renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "data/game_data.h"

VenueRenderer::VenueRenderer(Renderer& renderer, const GameData& data, const std::string& venue, const Band& band) {
    data_ = &data;
    renderer_ = &renderer;
    std::string dir = "venues/" + venue;
    script_ = std::make_unique<VenueScript>(data, venue);
    script_->getSpot = [this](const std::string&) { return walkSpotNow(); };
    script_->cameraShot = [this] { return camera_ ? camera_->shotName() : std::string(); };
    // FUN_001f50e0 set_steps: steps = max(n, 1); a full flare stays full, else count *= (int)(n / steps).
    script_->setFlareSteps = [this](const std::string& name, int n) {
        SceneObject* o = anims_->find(name);
        if (!o || !o->flare) return;
        FlareObj& f = *o->flare;
        n = std::max(n, 1);
        if (f.count == f.steps) f.count = n;
        else f.count *= n / f.steps;
        f.steps = n;
    };
    std::vector<Scene*> scenes;
    for (const VenueScript::Section& s : script_->sections()) {
        std::string path = dir + "/gen/" + s.file + ".rnd_ps2";
        if (!data.exists(path)) {
            SDL_Log("venue %s: no section file %s", venue.c_str(), path.c_str());
            continue;
        }
        Section sec;
        sec.name = s.name;
        sec.scene = std::make_unique<Scene>(data.read(path));
        scenes.push_back(sec.scene.get());
        sections_.push_back(std::move(sec));
    }
    if (sections_.empty()) throw std::runtime_error("venue has no scenes");
    // arena/gen/venue.dta: {arena load_section '' campaths}
    if (std::string cp = dir + "/gen/campaths.rnd_ps2"; data.exists(cp)) {
        campaths_ = std::make_unique<Scene>(data.read(cp));
        scenes.push_back(campaths_.get());
    }
    // FUN_00167fe0: the arena also loads system/run/arena/fx.rnd (shaky_cam1.tnm).
    if (std::string fx = "../../system/run/arena/gen/fx.rnd_ps2"; data.exists(fx)) {
        fx_ = std::make_unique<Scene>(data.read(fx));
        scenes.push_back(fx_.get());
    }
    // The Garden's ceiling rig (anim_beam01..08 and the floor pools that follow
    // them, orange/pink/red_spotNN) is driven only by lighting.view on song
    // ticks with the default 100-frame loop filter, i.e. a full sweep every
    // 0.2 beat. Every other short loop in the shipped venues carries its own
    // slow-down (fx_ray 0.05, speakers 0.5, chandeliers 0.02), so these play at
    // 0.05: one sweep per 4/4 bar. A deliberate deviation (the sweeps flailed).
    if (venue == "arena")
        for (Section& s : sections_) {
            if (s.name != "lighting") continue;
            for (SceneObject& o : s.scene->objects()) {
                if (!o.anim || o.anim->kind != AnimObj::Kind::Trans) continue;
                if (o.name.rfind("anim_beam", 0) != 0 && o.name.find("_spot") == std::string::npos) continue;
                auto& f = o.anim->anim.filters;
                if (f.size() == 2 && f[0].type == 0 && f[0].a == 1 && f[0].b == 0 && f[1].type == 1 && f[1].b == 100) f[0].a = 0.05f;
            }
        }
    anims_ = std::make_unique<AnimPlayer>(scenes);

    if (std::string cam = dtbPath(dir + "/camera.dta"); data.exists(cam)) {
        DataNode paths = parseDtb(data.read(dtbPath("arena/cam_paths.dta")));
        DataNode venues = parseDtb(data.read(dtbPath("arena/venues.dta")));
        const DataNode* durations = nullptr;
        unsigned introFlags = 0;
        for (const DataNode& v : venues.children)
            if (v.isList() && !v.children.empty() && v.children[0].isText() && v.children[0].s == venue) {
                durations = v.find("camera_durations");
                // (intro_camera_flags {| kCamNear kCamRight}): config/macros.dta values.
                if (const DataNode* n = v.find("intro_camera_flags"); n && n->children.size() > 1) {
                    static const std::map<std::string, unsigned> kFlags = {{"kCamLeft", 1},  {"kCamRight", 2}, {"kCamCloseup", 4},
                                                                           {"kCamNear", 8},  {"kCamFar", 16},  {"kCamBehind", 32}};
                    const DataNode& e = n->children[1];
                    auto add = [&](const DataNode& x) {
                        if (x.type == DataNode::Type::Int) introFlags |= unsigned(x.i);
                        else if (auto it = kFlags.find(x.s); it != kFlags.end()) introFlags |= it->second;
                    };
                    if (e.isList()) for (const DataNode& x : e.children) add(x);
                    else add(e);
                }
                // allow_walks / lose_teleport / solo_walk_point (single player only: no 2-player mode yet).
                auto flag = [&](const char* key) {
                    const DataNode* n = v.find(key);
                    return n && n->children.size() > 1 && (n->children[1].type == DataNode::Type::Int ? n->children[1].i != 0
                                                                                                   : n->children[1].s == "TRUE");
                };
                walkConfig_.allow = flag("allow_walks");
                walkConfig_.loseTeleport = flag("lose_teleport");
                if (const DataNode* n = v.find("solo_walk_point"); n && n->children.size() > 1) walkConfig_.soloPoint = n->intAt(1);
            }
        camera_ = std::make_unique<VenueCamera>(parseDtb(data.read(cam)), paths, durations);
        if (!camera_->hasShots()) camera_.reset();
        else camera_->setIntroFlags(introFlags);
        // lose01's pre-switch event: lose_teleport (arena_game.dta) before the focus snaps to the head.
        if (camera_)
            camera_->preSwitch = [this](const std::string& shot) {
                if (shot != "lose01" || !walk_ || !guitarist_ || !walkConfig_.loseTeleport) return;
                for (Member& m : band_)
                    if (m.role == "singer")
                        if (const SceneObject* p = m.character->find("bone_pelvis.mesh"); p && p->transformable())
                            walk_->loseTeleport(*guitarist_, p->transformable()->world.pos);
            };
    }

    guitaristName_ = band.guitarist;
    if (!band.guitarist.empty()) guitarist_ = makeGuitarist(renderer, data, band.guitarist, band.guitar, band.tempo);
    if (!band.guitarist2.empty()) {
        guitarist2_ = makeGuitarist(renderer, data, band.guitarist2, band.guitar2, band.tempo);
        walkConfig_.allow = false;  // MyCharSys_v10: no walks with two guitarists
        if (script_) script_->setMultiplayer(true);
        if (camera_) camera_->setFixedGroup("MULTIPLAYER");  // camera_multiplayer.dta intro
    }
    if (!band.guitarist.empty()) loadBand(renderer, data, band.tempo, band.lineup);
    // The band's shadows use the guitarist's charsys_shadow.mat (metal's when he has none).
    shadowMat_ = guitarist_ ? guitarist_->shadowMaterial() : nullptr;
    if (!shadowMat_ && data.exists("charsys/metal/gen/metal_shadow.rnd_ps2")) {
        shadowMatScene_ = std::make_unique<Scene>(data.read("charsys/metal/gen/metal_shadow.rnd_ps2"));
        if (const SceneObject* m = shadowMatScene_->find("charsys_shadow.mat"); m && m->mat) shadowMat_ = m->mat.get();
    }
    try {
        crowd_ = std::make_unique<CrowdRenderer>(renderer, data, *anims_, venue);
    } catch (const std::exception& ex) {
        SDL_Log("venue %s: no crowd: %s", venue.c_str(), ex.what());
    }
    if (crowd_ && guitarist_) guitarist_->eyeCrowdTarget = [this] { return crowd_->randomMemberTarget(); };
    if (camera_ && guitarist_)
        camera_->setCharacterLookup([this](const std::string& name) -> const SceneObject* {
            return name.rfind("guitarist0::", 0) == 0 ? guitarist_->find(name.substr(12)) : nullptr;
        });
    // FUN_00179108 / FUN_00168808: one spotlight per (arena (spot_lights ...)) entry,
    // floor_spot_height / floor_spot_scale (gh.dta overrides arena.dta's 1.5 with 1).
    spotLight_ = "spotlight01.lit";
    if (std::string cfg = "../../system/run/config/gen/arena.dtb"; data.exists(cfg)) {
        DataNode a = parseDtb(data.read(cfg));
        if (const DataNode* n = a.find("floor_spot_scale"); n && n->children.size() > 1) floorSpotScale_ = n->floatAt(1);
    }
    if (fx_ && anims_->find(spotLight_) && anims_->find("floorspot_glow.mat")) {
        fxRenderer_ = std::make_unique<SceneRenderer>(renderer, *fx_);
        fxRenderer_->setLookup([this](const std::string& name) -> const SceneObject* { return anims_->find(name); });
        fxRenderer_->preferLookupMaterials(true);
    }
    for (Section& s : sections_) {
        s.renderer = std::make_unique<SceneRenderer>(renderer, *s.scene);
        s.renderer->setLookup([this](const std::string& name) -> const SceneObject* { return anims_->find(name); });
        // FUN_00178db8: <s>_transparent.view leaves its parents' draw lists; it is drawn after the band.
        s.renderer->skipView(s.name + "_transparent.view");
    }
}

std::unique_ptr<Character> VenueRenderer::makeGuitarist(Renderer& renderer, const GameData& data, const std::string& archetype,
                                                        const std::string& guitar, int tempo) {
    try {
        Character::Spec spec = Character::guitaristSpec(data, archetype, guitar, tempo);
        auto g = std::make_unique<Character>(renderer, data, spec);
        g->setWorldLookup([this](const std::string& n) -> const SceneObject* { return anims_->find(n); });
        g->setRootMotion(true);  // charbase.dta: bone.servo use_delta
        // charsys.dta HERO_FACES (face_file "charsys/<c>/<c>_face.rnd"; hair_metal: hairmetal_face).
        std::string faceName = archetype + "_face";
        if (!data.exists("charsys/" + archetype + "/gen/" + faceName + ".rnd_ps2")) std::erase(faceName, '_'), faceName.insert(faceName.size() - 4, "_");
        g->loadFace(data, "charsys/" + archetype + "/" + faceName + ".rnd", false);
        g->enableEyes();
        // my_preload show_shadow "charsys/<c>/<c>_shadow.rnd" (classic: male_shadow; hair_metal: hairmetal_shadow).
        for (std::string n : {archetype + "_shadow", faceName.substr(0, faceName.size() - 5) + "_shadow", std::string("male_shadow")})
            if (data.exists("charsys/" + archetype + "/gen/" + n + ".rnd_ps2")) {
                g->loadShadow(data, "charsys/" + archetype + "/" + n + ".rnd");
                break;
            }
        return g;
    } catch (const std::exception& ex) {
        SDL_Log("guitarist %s unavailable: %s", archetype.c_str(), ex.what());
        return nullptr;
    }
}

void VenueRenderer::activePlayers(int mask) {
    activeMask_ = mask;
    if (camera_ && guitarist2_ && !gameOver_) camera_->activePlayers(mask);
}

void VenueRenderer::gameOverMulti(bool p1Won, bool p2Won) {
    won2_ = p2Won;
    gameOver(p1Won, false);
    if (guitarist2_) guitarist2_->play(p2Won ? Character::kGuitarWin : Character::kGuitarLose, p2Won ? Character::kAnimFirst : Character::kAnimReset);
}

void VenueRenderer::update(const VenueFrame& f) {
    ms_ = f.ms, ticks_ = f.ticks;
    if (!started_) {
        script_->start(*anims_, f.excitement);
        if (camera_) camera_->start(*anims_, f.ms, f.ticks);
        excitement_ = f.excitement;
        started_ = true;
    }
    script_->update(f.ms, f.ticks, f.excitement, f.events, f.downbeat);
    const float beat = f.ticks / 480.0f;
    if (guitarist_ && guitarState_ == 4) {
        // GuitarCharacter_v10 runs every frame, game over included: bone_fret keeps
        // following the neck (else the fret hand stays pinned where the song ended).
        if (hands_) hands_->update(beat, false, *guitarist_);
        guitarist_->poll(beat, f.ms);
    } else if (guitarist_) {
        // guitarist.dta / GuitarCharacter: the mood is Bad while doing badly,
        // else by state; upcoming_gtr / wail / solo events start the new mood's
        // clip so it has blended in on the event beat (FUN_002856a0).
        auto mood = [&] {
            if (f.excitement <= VenueScript::Bad && guitarState_ != 0) return Character::kGuitarBad;
            static const uint32_t kMoods[] = {Character::kGuitarIdle, Character::kGuitarNormal, Character::kGuitarExtreme,
                                              Character::kGuitarSolo};
            return kMoods[guitarState_];
        };
        if (!placed_) {  // FUN_00281cd8: single player stands on walk_spot_01
            std::vector<Xfm> spots = walkSpots();
            if (guitarist2_) {  // two guitarists: player k on walk spot k % 2 (P1 walk_spot_02, P2 walk_spot_01)
                if (spots.size() >= 2) guitarist_->setPlacement(spots[1]), guitarist2_->setPlacement(spots[0]);
                spots2_ = spots;
            } else {
                if (!walk_ && !spots.empty()) walk_ = std::make_unique<GuitarWalk>(spots, walkConfig_, std::vector<SongEvent>{});
                if (walk_) guitarist_->setPlacement(walk_->spot(walk_->startSpot()));
            }
            placed_ = true;
        }
        const bool walkBusy = walk_ && walk_->update(*guitarist_, beat, f.ms, f.ticks, guitarState_, f.excitement, f.events);
        if (!introPlayed_) {  // play_intro: kGuitarIntro kAnimReset, then idle
            guitarist_->play(Character::kGuitarIntro, Character::kAnimReset);
            introPlayed_ = true;
        }
        for (const auto& [e, at] : f.soon) {
            int s = guitarState_;
            if (e == "gtr_on") s = 1;
            else if (e == "gtr_off") s = 0;
            else if (e == "wail_on") s = 2;
            else if (e == "wail_off" || e == "solo_off") s = guitarState_ ? 1 : 0;
            else if (e == "solo_on") s = 3;
            else continue;
            guitarState_ = s;
            if (!walkBusy && !(guitarist_->clipFlags() & Character::kGuitarSpecial)) guitarist_->playAt(mood(), at);
        }
        // Every frame: once a clip has blended in, queue the next of the mood.
        if (!walkBusy && !guitarist_->blending()) guitarist_->play(mood(), Character::kAnimLongest);
        // FUN_00286298: a face-off player plays only in its own sections.
        auto active = [&](int p) { return activeMask_ == 0 || (activeMask_ >> p & 1); };
        if (hands_) hands_->update(beat, f.hitting && active(0), *guitarist_);
        guitarist_->poll(beat, f.ms);
        if (walk_) walk_->poll(*guitarist_, beat);
        // Face-off guitarists have no walk: the standing guard keeps them on their spots.
        if (guitarist2_ && spots2_.size() >= 2 && lastGuardBeat_ > -1e29f) {
            float dBeat = std::clamp(beat - lastGuardBeat_, 0.0f, 1.0f);
            auto yaw = [](const Xfm& x) { return -std::atan2(x.rot[3], x.rot[4]); };
            standGuard(*guitarist_, spots2_[1].pos, yaw(spots2_[1]), dBeat);
            standGuard(*guitarist2_, spots2_[0].pos, yaw(spots2_[0]), dBeat);
        }
        lastGuardBeat_ = beat;
        if (static const bool rootLog = SDL_getenv("GD01_ROOTLOG") != nullptr; rootLog && placed_ && std::floor(beat / 2) != std::floor(lastRootLogBeat_ / 2)) {
            const Xfm& r = guitarist_->placement();
            if (!rootLogInit_) rootLogInit_ = true, rootLog0_ = r;
            auto yaw = [](const Xfm& x) { return -std::atan2(x.rot[3], x.rot[4]); };
            float dy = yaw(r) - yaw(rootLog0_);
            while (dy > 3.14159265f) dy -= 6.2831853f;
            while (dy < -3.14159265f) dy += 6.2831853f;
            float py = 0;
            if (const SceneObject* p = guitarist_->find("bone_pelvis.mesh"); p && p->transformable()) py = yaw(p->transformable()->world) - yaw(rootLog0_);
            while (py > 3.14159265f) py -= 6.2831853f;
            while (py < -3.14159265f) py += 6.2831853f;
            SDL_Log("ROOT beat %.0f clip %s walk %d yaw %.0f pelvis %.0f dist %.1f", beat, guitarist_->clipName().c_str(), walk_ ? walk_->state() : -1, dy * 57.2958f,
                    py * 57.2958f, std::hypot(r.pos[0] - rootLog0_.pos[0], r.pos[1] - rootLog0_.pos[1]));
        }
        lastRootLogBeat_ = beat;
        if (guitarist2_) {
            if (guitarist2_->clipName().empty()) guitarist2_->play(Character::kGuitarIntro, Character::kAnimReset);
            if (!guitarist2_->blending()) guitarist2_->play(mood(), Character::kAnimLongest);
            if (hands2_) hands2_->update(beat, f.hitting2 && active(1), *guitarist2_);
            guitarist2_->poll(beat, f.ms);
        }
    } else if (guitarist2_) {
        guitarist2_->poll(beat, f.ms);
    }
    if (guitarist2_ && guitarState_ == 4) {
        if (hands2_) hands2_->update(beat, false, *guitarist2_);
        guitarist2_->poll(beat, f.ms);
    }
    for (Member& m : band_) {
        // theband.dta: upcoming_<part>_on/off and the drummer's patterns play
        // with kAnimNow|kAnimGraphLoop; each downbeat an active member changes
        // clip with probability 0.2.
        std::string part = m.role == "singer" ? "sing" : m.role == "drummer" ? "drum" : m.role == "keyboard" ? "keys" : m.role;
        constexpr int kNowLoop = Character::kAnimNow | Character::kAnimGraphLoop;
        if (gameOver_) {
            m.character->poll(beat, f.ms, m.frameScale);
            continue;
        }
        if (m.character->clipName().empty()) m.character->play(Character::kBandIdle, Character::kAnimReset | Character::kAnimGraphLoop);
        for (const auto& [e, at] : f.soon) {
            if (e == part + "_on" || (m.role == "drummer" && e == "drum_normal")) m.character->play(Character::kBandActive, kNowLoop);
            else if (e == part + "_off") m.character->play(Character::kBandIdle, kNowLoop);
            else if (m.role == "drummer" && e == "drum_allbeat") m.character->play(Character::kBandAllbeat, kNowLoop);
            else if (m.role == "drummer" && e == "drum_double") m.character->play(Character::kBandDouble, kNowLoop);
            else if (m.role == "drummer" && e == "drum_half") m.character->play(Character::kBandHalf, kNowLoop);
        }
        for (const std::string& e : f.events) {  // set_frame_scale
            if (e == part + "_half_tempo") m.frameScale = 0.5f;
            else if (e == part + "_double_tempo") m.frameScale = 2;
            else if (e == part + "_normal_tempo") m.frameScale = 1;
        }
        if (f.downbeat && (m.character->clipFlags() & Character::kBandActive) &&
            std::uniform_real_distribution<float>(0, 1)(rng_) < 0.2f)
            m.character->play(Character::kBandActive, Character::kAnimFirst | Character::kAnimGraphLoop);
        char spot[32];
        std::snprintf(spot, sizeof spot, "stage_spot_%02d.mesh", m.spot + 1);
        if (Xfm place; spotPlacement(spot, place)) m.character->setPlacement(place);
        m.character->poll(beat, f.ms, m.frameScale);
    }
    // Faces follow the global venue excitement (DAT_00363780).
    if (guitarist_) {
        guitarist_->setFaceExcitement(f.excitement);
        // Each switch_cam sends the shot's (eyes N) to the guitarist (Character_v5).
        if (camera_) {
            guitarist_->setEyeMode(camera_->eyesMode());
            guitarist_->setEyeCamera(camera_->transform().pos);
        }
    }
    updateSpotLight();
    if (crowd_) {
        for (const std::string& e : f.events) crowd_->tempoEvent(e);
        std::string shot = camera_ ? camera_->shotName() : "";
        crowd_->update(f.ticks / 480.0f, f.ms, f.excitement, shot != lastShot_ || !camera_, camera_ ? camera_->crowdRegion() : -1,
                       camera_ ? camera_->transform() : Xfm{}, camera_ ? camera_->projX() : 1.0f);
        lastShot_ = shot;
    }
    if (camera_) {
        VenueCamera::State cs;
        cs.starMode = f.starMode;
        cs.specialClip = guitarist_ && (guitarist_->clipFlags() & Character::kGuitarSpecial);
        cs.excitement = f.excitement;
        cs.walking = walkingNow();
        cs.walkSpot = walkSpotNow();
        camera_->setState(cs);
        // Message order (BeatMatch_v1): beat, downbeat, then one_bar_to.
        if (f.excitement != excitement_) camera_->excitementChanged();
        for (const auto& [left, number] : f.beatLines) camera_->beat(left, number, f.excitement);
        if (f.downbeat) camera_->downbeat(f.excitement);
        for (const std::string& s : f.upcoming) camera_->upcomingSection(s, f.excitement);
    }
    excitement_ = f.excitement;
    // VenueCam switch_cam (FUN_0016f3b8) sends the venue post_switch_cam after
    // every cut: the scripts hide what spoils a shot (the arena's stadium
    // spotlight in the far floor shots, the fest's solo beams, big_club's
    // smoke and beams ...).
    if (camera_ && camera_->shotName() != postSwitchShot_) {
        postSwitchShot_ = camera_->shotName();
        script_->message("post_switch_cam");
    }

    // The shot moves "6 foot camera.cam", the venue camera the scripts
    // attach the key light and colour plane to (add_trans).
    if (camera_) {
        vp_ = camera_->viewProj(f.ms, f.ticks, aspect_);
        if (SceneObject* cam = anims_->find("6 foot camera.cam"); cam && cam->cam)
            cam->cam->trans.local = cam->cam->trans.world = camera_->transform();
        anims_->updateAttached();
        // [I] At a camera cut, or while out of shot, a standing guitarist is
        // put back exactly on his spot: the cut hides the jump.
        if (walk_ && guitarist_ && !gameOver_) {
            const std::string& shot = camera_->shotName();
            const bool cut = shot != snapShot_;
            snapShot_ = shot;
            if ((cut || !onScreen(*guitarist_)) && walk_->snapHome(*guitarist_) && SDL_getenv("GD01_LOG_SHOTS"))
                SDL_Log("walk: guitarist reset on his spot (%s %s)", cut ? "cut to" : "out of", shot.c_str());
        }
    } else {
        // No shots: an audience viewpoint (venues are Z-up, the crowd toward -Y).
        constexpr float kFovX = 45.0f * 3.14159265f / 180.0f;
        float fovY = 2.0f * std::atan(std::tan(kFovX * 0.5f) / (4.0f / 3.0f));
        vp_ = Mat4::perspective(fovY, aspect_, 5.0f, 5000.0f) * Mat4::lookAt({0, -280, 95}, {0, 0, 55});
    }
}

void VenueRenderer::draw() {
    // Development aid: GD01_GUITARIST_CAM=1 frames the guitarist up close.
    if (SDL_getenv("GD01_GUITARIST_CAM"))
        if (const SceneObject* pelvis = guitarist_ ? guitarist_->find("bone_pelvis.mesh") : nullptr) {
            const auto& p = pelvis->transformable()->world.pos;
            float fovY = 2.0f * std::atan(std::tan(0.35f) / (4.0f / 3.0f));
            vp_ = Mat4::perspective(fovY, aspect_, 5.0f, 5000.0f) *
                  Mat4::lookAt({p[0] + 20, p[1] - 120, p[2] + 25}, {p[0], p[1], p[2] + 10});
        }
    if (still_) vp_ = stillVp_;
    const Mat4& vp = vp_;
    const bool showCrowd = crowd_ && !(camera_ && camera_->hideCrowd());
    const Xfm camXfm = still_ ? stillCam_ : camera_ ? camera_->transform() : Xfm{};
    if (showCrowd) crowd_->renderImpostors(camXfm);
    // ArenaPanel draw (FUN_001696d8): each section's view, the characters, the
    // crowd last, then the sections' transparent views (beams, glows, flares).
    for (Section& s : sections_) {
        if (camera_ || still_) s.renderer->setCamera(camXfm);
        s.renderer->draw(s.name + ".view", vp);
    }
    const SceneObject* envObj = script_->singerEnv().empty() ? nullptr : anims_->find(script_->singerEnv());
    const EnvironObj* env = envObj && envObj->environment ? envObj->environment.get() : nullptr;
    // [I] More Lights: a little fill on the band (the environment's ambient raised).
    EnvironObj filled;
    if (moreLights_ > 0 && env) {
        filled = *env;
        const float gain = moreLights_ == 2 ? 1.3f : 1.15f, lift = moreLights_ == 2 ? 0.07f : 0.035f;
        for (int k = 0; k < 3; ++k) filled.ambient[size_t(k)] = std::min(1.0f, env->ambient[size_t(k)] * gain + lift);
        env = &filled;
    }
    if (guitarist_) {
        guitarist_->draw(vp, env);
        // The shadow falls away from the spotlight that follows the guitarist.
        if (const SceneObject* light = anims_->find(spotLight_); light && light->transformable()) {
            const auto& L = light->transformable()->world.pos;
            const auto& t = guitarist_->placement().pos;
            std::array<float, 3> d{t[0] - L[0], t[1] - L[1], t[2] - L[2]};
            float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len > 0) guitarist_->drawShadow(vp, {d[0] / len, d[1] / len, d[2] / len});
        }
    }
    if (guitarist2_) guitarist2_->draw(vp, env);
    for (Member& m : band_) m.character->draw(vp, env);
    const std::vector<LitChar> lit = bandShadows_ || moreLights_ > 0 ? litCharacters() : std::vector<LitChar>{};
    // [I] All Band Mates Cast Shadows: each silhouette (depth only) before the glows it cuts.
    if (bandShadows_)
        for (const LitChar& l : lit) {
            if (l.main) continue;
            const auto& t = l.character->placement().pos;
            std::array<float, 3> d{t[0] - l.light[0], t[1] - l.light[1], t[2] - l.light[2]};
            float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len <= 0) continue;
            d = {d[0] / len, d[1] / len, d[2] / len};
            if (l.character == guitarist2_.get()) l.character->drawShadow(vp, d);
            else l.character->drawBodyShadow(vp, d, shadowMat_);
        }
    if (showCrowd) crowd_->draw(vp);
    // 0x169818: the floor spots, then the transparent views.
    if (fxRenderer_ && floorSpotOn_) {
        SceneRenderer::MeshOptions opt;
        opt.material = "floorspot_glow.mat";
        opt.useWorld = false;
        fxRenderer_->drawMesh("floorspot_char.mesh", vp, Mat4::fromRowXfm(floorSpot_.rot, floorSpot_.pos), opt);
    }
    if (!lit.empty()) drawEnhancedLights(vp, camXfm, lit);
    for (Section& s : sections_) s.renderer->draw(s.name + "_transparent.view", vp);
    for (Section& s : sections_) s.renderer->resolveFlares();
}

// FUN_00179420: aim the light along +Y at the guitarist's root and lay the
// floor ellipse under it, stretched away from the light by 1/cos.
void VenueRenderer::updateSpotLight() {
    floorSpotOn_ = false;
    SceneObject* light = anims_->find(spotLight_);
    if (!guitarist_ || !light || !light->transformable()) return;
    Transformable& lt = *light->transformable();
    const std::array<float, 3> t = guitarist_->placement().pos, L = lt.world.pos;
    std::array<float, 3> d{t[0] - L[0], t[1] - L[1], t[2] - L[2]};
    float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-3f) return;
    std::array<float, 3> Y{d[0] / len, d[1] / len, d[2] / len};
    std::array<float, 3> X{Y[1], -Y[0], 0};  // Y x (0,0,1)
    float xl = std::sqrt(X[0] * X[0] + X[1] * X[1]);
    if (xl < 1e-5f) X = {1, 0, 0};
    else X = {X[0] / xl, X[1] / xl, 0};
    std::array<float, 3> Z{X[1] * Y[2] - X[2] * Y[1], X[2] * Y[0] - X[0] * Y[2], X[0] * Y[1] - X[1] * Y[0]};
    lt.local.rot = {X[0], X[1], X[2], Y[0], Y[1], Y[2], Z[0], Z[1], Z[2]};
    lt.world.rot = lt.local.rot;
    anims_->markMoved(light->name);
    anims_->updateTransforms();
    // Floor: rows Xh * S, Yh * S * |d| / |dz|, (0,0,1); centre pushed 0.25 Ls away from the light.
    float h = std::sqrt(d[0] * d[0] + d[1] * d[1]);
    if (std::fabs(d[2]) < 1e-3f) return;
    std::array<float, 3> Yh = h > 1e-5f ? std::array<float, 3>{d[0] / h, d[1] / h, 0} : std::array<float, 3>{0, 1, 0};
    std::array<float, 3> Xh{Yh[1], -Yh[0], 0};
    float S = floorSpotScale_, Ls = S * len / std::fabs(d[2]);
    floorSpot_.rot = {Xh[0] * S, Xh[1] * S, 0, Yh[0] * Ls, Yh[1] * Ls, 0, 0, 0, 1};
    floorSpot_.pos = {t[0] + 0.25f * Ls * Yh[0], t[1] + 0.25f * Ls * Yh[1], t[2] + floorSpotHeight_};
    floorSpotOn_ = true;
}

bool VenueRenderer::floorSpotFor(const std::array<float, 3>& L, const std::array<float, 3>& t, Xfm& out) const {
    std::array<float, 3> d{t[0] - L[0], t[1] - L[1], t[2] - L[2]};
    float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-3f || std::fabs(d[2]) < 1e-3f) return false;
    float h = std::sqrt(d[0] * d[0] + d[1] * d[1]);
    std::array<float, 3> Yh = h > 1e-5f ? std::array<float, 3>{d[0] / h, d[1] / h, 0} : std::array<float, 3>{0, 1, 0};
    std::array<float, 3> Xh{Yh[1], -Yh[0], 0};
    float S = floorSpotScale_, Ls = S * len / std::fabs(d[2]);
    out.rot = {Xh[0] * S, Xh[1] * S, 0, Yh[0] * Ls, Yh[1] * Ls, 0, 0, 0, 1};
    out.pos = {t[0] + 0.25f * Ls * Yh[0], t[1] + 0.25f * Ls * Yh[1], t[2] + floorSpotHeight_};
    return true;
}

void VenueRenderer::setMoreLights(int level) {
    moreLights_ = std::clamp(level, 0, 2);
    for (Section& s : sections_) s.renderer->setAdditiveGain(moreLights_ == 2 ? 1.45f : moreLights_ == 1 ? 1.2f : 1.0f);
}

// [I] The guitarist under the venue's spotlight; every other band member under
// a light of his own straight above and a little toward the audience (-Y).
std::vector<VenueRenderer::LitChar> VenueRenderer::litCharacters() {
    std::vector<LitChar> out;
    auto above = [](const Character& c) {
        const auto& p = c.placement().pos;
        return std::array<float, 3>{p[0] + 10, p[1] - 70, p[2] + 210};
    };
    if (guitarist_) {
        const SceneObject* light = anims_->find(spotLight_);
        if (light && light->transformable()) out.push_back({guitarist_.get(), light->transformable()->world.pos, true});
    }
    if (guitarist2_) out.push_back({guitarist2_.get(), above(*guitarist2_), false});
    for (Member& m : band_) out.push_back({m.character.get(), above(*m.character), false});
    return out;
}

std::vector<std::array<float, 9>> VenueRenderer::beamLights() {
    std::array<float, 3> colour{1, 0.93f, 0.8f};
    if (const SceneObject* l = anims_ ? anims_->find(spotLight_) : nullptr; l && l->light) {
        float peak = std::max({l->light->color[0], l->light->color[1], l->light->color[2]});
        if (peak >= 0.25f)
            for (int k = 0; k < 3; ++k) colour[size_t(k)] = std::clamp(l->light->color[size_t(k)] / peak, 0.0f, 1.0f);
    }
    std::vector<std::array<float, 9>> out;
    if (!anims_) return out;
    for (const LitChar& l : litCharacters()) {
        const auto& p = l.character->placement().pos;
        out.push_back({l.light[0], l.light[1], l.light[2], p[0], p[1], p[2], colour[0], colour[1], colour[2]});
    }
    return out;
}

// The band members' floor glows (with shadows or More Lights) and, with More
// Lights, a soft beam from each light to its pool and dust in the beams, all
// in the stage spotlight's (animated) colour, as one additive mesh.
void VenueRenderer::drawEnhancedLights(const Mat4& vp, const Xfm& cam, const std::vector<LitChar>& lit) {
    std::array<float, 3> colour{1, 0.93f, 0.8f};
    if (const SceneObject* l = anims_->find(spotLight_); l && l->light)
        for (int k = 0; k < 3; ++k) colour[size_t(k)] = std::clamp(l->light->color[size_t(k)], 0.0f, 1.0f);
    float peak = std::max({colour[0], colour[1], colour[2]});
    if (peak < 0.25f) colour = {1, 0.93f, 0.8f};  // a light turned down: keep the beams warm white
    else
        for (float& c : colour) c /= peak;
    // Floor glows: the band's own; the guitarist's is the venue's.
    if (fxRenderer_)
        for (const LitChar& l : lit) {
            Xfm pool;
            if (l.main || !floorSpotFor(l.light, l.character->placement().pos, pool)) continue;
            SceneRenderer::MeshOptions opt;
            opt.material = "floorspot_glow.mat";
            opt.useWorld = false;
            float a = bandShadows_ ? 1.0f : moreLights_ == 2 ? 0.8f : 0.5f;
            opt.tint[0] = opt.tint[1] = opt.tint[2] = a;
            fxRenderer_->drawMesh("floorspot_char.mesh", vp, Mat4::fromRowXfm(pool.rot, pool.pos), opt);
        }
    if (moreLights_ == 0 || !renderer_) return;
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    auto rgba = [&](float a) {
        return std::array<uint8_t, 4>{uint8_t(colour[0] * 255), uint8_t(colour[1] * 255), uint8_t(colour[2] * 255),
                                      uint8_t(std::clamp(a, 0.0f, 1.0f) * 255)};
    };
    auto push = [&](const std::array<float, 3>& p, float a) {
        auto c = rgba(a);
        v.push_back({p[0], p[1], p[2], 0, 0, c[0], c[1], c[2], c[3]});
        return uint32_t(v.size() - 1);
    };
    auto sub = [](const std::array<float, 3>& a, const std::array<float, 3>& b) { return std::array<float, 3>{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto norm = [](std::array<float, 3> a) {
        float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        return l > 1e-6f ? std::array<float, 3>{a[0] / l, a[1] / l, a[2] / l} : std::array<float, 3>{0, 0, 1};
    };
    auto cross = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return std::array<float, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    const float beamA = moreLights_ == 2 ? 0.32f : 0.16f;
    const int motes = lightDust_ ? (moreLights_ == 2 ? 70 : 35) : 0;
    const std::array<float, 3> camRight{cam.rot[0], cam.rot[1], cam.rot[2]}, camUp{cam.rot[6], cam.rot[7], cam.rot[8]};
    for (size_t li = 0; li < lit.size(); ++li) {
        const LitChar& l = lit[li];
        const auto& t = l.character->placement().pos;
        // From 25 % of the way down (the lamp itself is out of shot) to the floor.
        std::array<float, 3> A{l.light[0] + (t[0] - l.light[0]) * 0.25f, l.light[1] + (t[1] - l.light[1]) * 0.25f,
                               l.light[2] + (t[2] - l.light[2]) * 0.25f};
        std::array<float, 3> B{t[0], t[1], t[2] + 1};
        std::array<float, 3> axis = sub(B, A), mid{(A[0] + B[0]) / 2, (A[1] + B[1]) / 2, (A[2] + B[2]) / 2};
        std::array<float, 3> side = norm(cross(axis, sub(cam.pos, mid)));
        const float rTop = 10, rBot = 46;
        // Two columns of quads: transparent edges, bright core, fading toward the floor.
        uint32_t base = uint32_t(v.size());
        for (int row = 0; row < 2; ++row) {
            const auto& P = row == 0 ? A : B;
            float r = row == 0 ? rTop : rBot, a = row == 0 ? beamA : beamA * 0.35f;
            push({P[0] - side[0] * r, P[1] - side[1] * r, P[2] - side[2] * r}, 0);
            push(P, a);
            push({P[0] + side[0] * r, P[1] + side[1] * r, P[2] + side[2] * r}, 0);
        }
        for (uint32_t c = 0; c < 2; ++c)
            idx.insert(idx.end(), {base + c, base + c + 1, base + 3 + c, base + c + 1, base + 4 + c, base + 3 + c});
        // Dust: each mote drifts on its own slow orbit inside the beam.
        for (int k = 0; k < motes; ++k) {
            float h1 = std::fmod(std::sin(float(k * 37 + int(li) * 101) * 12.9898f) * 43758.5453f, 1.0f);
            float h2 = std::fmod(std::sin(float(k * 53 + int(li) * 71) * 78.233f) * 12345.678f, 1.0f);
            h1 = std::fabs(h1), h2 = std::fabs(h2);
            float time = ms_ * 0.001f;
            float u = std::fmod(h1 + time * (0.01f + 0.02f * h2), 1.0f);  // slowly sinking
            float ang = h2 * 6.2831853f + time * 0.3f * (h1 - 0.5f);
            float r = (rTop + (rBot - rTop) * u) * 0.8f * std::sqrt(std::fabs(std::sin(h1 * 91.7f)));
            std::array<float, 3> P{A[0] + axis[0] * u + side[0] * r * std::cos(ang), A[1] + axis[1] * u + side[1] * r * std::cos(ang),
                                   A[2] + axis[2] * u + r * 0.5f * std::sin(ang)};
            float twinkle = 0.5f + 0.5f * std::sin(time * (1.5f + 3 * h2) + h1 * 20);
            // Motes right in front of the lens would be big blobs: they fade out near the camera.
            std::array<float, 3> toCam = sub(P, cam.pos);
            float dist = std::sqrt(toCam[0] * toCam[0] + toCam[1] * toCam[1] + toCam[2] * toCam[2]);
            float a = (moreLights_ == 2 ? 0.6f : 0.4f) * twinkle * (1 - u * 0.6f) * std::clamp((dist - 40) / 80, 0.0f, 1.0f);
            if (a <= 0.01f) continue;
            // A soft diamond: bright centre, transparent tips.
            const float sz = 0.5f;
            uint32_t b = push(P, a);
            for (int corner = 0; corner < 4; ++corner) {
                float sx = corner == 0 ? sz : corner == 2 ? -sz : 0, sy = corner == 1 ? sz : corner == 3 ? -sz : 0;
                push({P[0] + camRight[0] * sx + camUp[0] * sy, P[1] + camRight[1] * sx + camUp[1] * sy, P[2] + camRight[2] * sx + camUp[2] * sy}, 0);
            }
            idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3, b, b + 3, b + 4, b, b + 4, b + 1});
        }
    }
    if (idx.empty()) return;
    // A small ring of meshes, so this frame never rewrites one the GPU may still read.
    int& mesh = fxMeshes_[fxMeshNext_];
    fxMeshNext_ = (fxMeshNext_ + 1) % 3;
    if (mesh) renderer_->updateMesh(mesh, v, idx);
    else mesh = renderer_->createMesh(v, idx);
    Renderer::RenderState st;
    st.blend = Blend::AddAlpha;
    st.depthTest = true, st.depthWrite = false;
    renderer_->drawMesh(mesh, vp.m.data(), st);
}

bool VenueRenderer::spotPlacement(const std::string& name, Xfm& out) {
    SceneObject* spot = anims_->find(name);
    if (!spot || !spot->transformable()) return false;
    out = spot->transformable()->world;
    for (int r = 0; r < 3; ++r) {
        float* row = &out.rot[r * 3];
        float len = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
        if (len > 0)
            for (int c = 0; c < 3; ++c) row[c] /= len;
    }
    return true;
}

namespace {

// The first symbol named *_ANIMS anywhere below `n`.
std::string findAnimsMacro(const DataNode& n) {
    if (n.isText() && n.s.size() > 6 && n.s.compare(n.s.size() - 6, 6, "_ANIMS") == 0) return n.s;
    for (const DataNode& c : n.children)
        if (std::string m = findAnimsMacro(c); !m.empty()) return m;
    return {};
}

}  // namespace

void VenueRenderer::loadBand(Renderer& renderer, const GameData& data, int tempo, std::vector<std::string> lineup) {
    // The song's (band ...) in songs.dta (female singer, keyboard player, or no
    // singer at all), else charsys.dta's default (band SINGER_MALE_METAL BASS_METAL DRUMMER_METAL);
    // (band_spots (bass 1) (drummer 2) (keyboard 0) (singer 0)); each member
    // is a band_chars.dta macro ((<role> (outfit (directory "charsys/<model>")) ...)).
    DataNode charsys = parseDtb(data.read(dtbPath("charsys/charsys.dta")));
    DataNode chars = parseDtb(data.read(dtbPath("charsys/band_chars.dta")));
    const DataNode* spots = charsys.find("band_spots");
    if (const char* dev = SDL_getenv("GD01_BAND")) {  // dev aid: "SINGER_FEMALE_METAL BASS_METAL DRUMMER_METAL"
        lineup.clear();
        std::istringstream in(dev);
        for (std::string w; in >> w;) lineup.push_back(w);
    }
    if (lineup.empty())
        if (const DataNode* def = charsys.find("band"))
            for (size_t i = 1; i < def->children.size(); ++i) lineup.push_back(def->children[i].s);
    for (const std::string& macro : lineup) {
        const DataNode* def = nullptr;
        for (size_t k = 0; k + 1 < chars.children.size(); ++k)
            if (chars.children[k].type == DataNode::Type::Define && chars.children[k].s == macro) def = &chars.children[k + 1];
        if (!def || def->children.empty() || !def->children[0].isList()) continue;
        const DataNode& body = def->children[0];
        if (body.children.empty() || !body.children[0].isText()) continue;
        Member m;
        m.role = body.children[0].s;
        const DataNode* outfit = body.find("outfit");
        const DataNode* dir = outfit ? outfit->find("directory") : nullptr;
        if (!dir) continue;
        std::string folder = dir->textAt(1);
        Character::Spec spec{folder.substr(folder.rfind('/') + 1), "", "charsys/band_anims.dta", findAnimsMacro(body), tempo, 0, "", 1};
        if (spots)
            if (const DataNode* s = spots->find(m.role); s && s->children.size() > 1) m.spot = s->intAt(1);
        try {
            m.character = std::make_unique<Character>(renderer, data, spec);
            m.character->setWorldLookup([this](const std::string& n) -> const SceneObject* { return anims_->find(n); });
            // (main.drv (realign 1)): drummer and keyboard (band_chars.dta).
            if (const DataNode* drv = body.find("main.drv"); drv && drv->find("realign"))
                m.character->setRealign(drv->find("realign")->intAt(1) != 0);
            if (const DataNode* ff = body.find("face_file"); ff && ff->children.size() > 1)  // SINGER_FACES
                m.character->loadFace(data, ff->textAt(1), true);
            band_.push_back(std::move(m));
        } catch (const std::exception& ex) {
            SDL_Log("band member %s unavailable: %s", spec.model.c_str(), ex.what());
        }
    }
}

bool VenueRenderer::singerMicPos(std::array<float, 3>& out) const { return singerBonePos("bone_pos_mic.mesh", out); }

bool VenueRenderer::singerBonePos(const std::string& bone, std::array<float, 3>& out) const {
    for (const Member& m : band_)
        if (m.role == "singer")
            if (const SceneObject* b = m.character->find(bone); b && b->transformable()) {
                out = b->transformable()->world.pos;
                return true;
            }
    return false;
}

SceneObject* VenueRenderer::findObject(const std::string& name) { return anims_ ? anims_->find(name) : nullptr; }

void VenueRenderer::gameOver(bool won, bool finalWin) {
    gameOver_ = true;
    wonGame_ = won;
    finalWin = finalWin && won;
    if (guitarist_) {
        guitarState_ = 4;
        // set_game_over (FUN_00284fd8): won waits for a transition (kAnimFirst),
        // lost cuts in (kAnimReset); losing mid-walk plays kGuitarLoseWalk; the
        // career's final song plays kGuitarWinFinal.
        if (!won && walkingNow()) guitarist_->play(kGuitarLoseWalk, Character::kAnimReset);
        else if (finalWin) guitarist_->play(kGuitarWinFinal, Character::kAnimFirst);
        else guitarist_->play(won ? Character::kGuitarWin : Character::kGuitarLose, won ? Character::kAnimFirst : Character::kAnimReset);
        if (walk_) walk_->cancel(*guitarist_);
    }
    for (Member& m : band_)
        m.character->play(won ? Character::kBandWin : Character::kBandLose, won ? Character::kAnimFirst : Character::kAnimNow);
    // camera_multiplayer.dta has no game_over handler: the face-off shot stays.
    if (camera_ && !guitarist2_) camera_->gameOver(won, finalWin, guitaristName_ == "grim");
}

bool VenueRenderer::outroDone() const {
    if (!guitarist_) return true;
    // The win / lose / final-win / lose-walk clip must be the one ending (won waits for a transition).
    return (guitarist_->clipFlags() & 0xC240000u) && !guitarist_->blending() && guitarist_->atEnd();
}

void VenueRenderer::setSong(const Song& song, const Chart& chart) {
    for (Member& m : band_)
        if (m.role == "singer") {
            std::vector<Character::FaceEvent> ev;
            for (const auto& e : song.singerEvents) ev.push_back({e.first, e.second});
            m.character->setFaceEvents(std::move(ev));
        }
    if (!guitarist_) return;
    if (std::vector<Xfm> spots = walkSpots(); !spots.empty() && !guitarist2_) walk_ = std::make_unique<GuitarWalk>(spots, walkConfig_, song.events);
    try {
        DataNode charsys = parseDtb(data_->read(dtbPath("charsys/charsys.dta")));
        if (guitarist_->hasHands()) hands_ = std::make_unique<GuitarHands>(song, chart, charsys);
        if (guitarist2_ && guitarist2_->hasHands()) hands2_ = std::make_unique<GuitarHands>(song, chart, charsys);
    } catch (const std::exception& ex) {
        SDL_Log("guitarist hands: %s", ex.what());
    }
}

// Whether any of the character's pelvis, head or feet projects inside the view.
bool VenueRenderer::onScreen(const Character& c) const {
    std::vector<std::array<float, 3>> pts;
    for (const char* b : {"bone_pelvis.mesh", "bone_head.mesh", "bone_L-foot.mesh", "bone_R-foot.mesh"})
        if (const SceneObject* o = c.find(b); o && o->transformable()) pts.push_back(o->transformable()->world.pos);
    if (pts.empty()) pts.push_back(c.placement().pos);
    for (const auto& p : pts) {
        float x = vp_.at(0, 0) * p[0] + vp_.at(0, 1) * p[1] + vp_.at(0, 2) * p[2] + vp_.at(0, 3);
        float y = vp_.at(1, 0) * p[0] + vp_.at(1, 1) * p[1] + vp_.at(1, 2) * p[2] + vp_.at(1, 3);
        float w = vp_.at(3, 0) * p[0] + vp_.at(3, 1) * p[1] + vp_.at(3, 2) * p[2] + vp_.at(3, 3);
        if (w > 0 && std::fabs(x) < w * 1.15f && std::fabs(y) < w * 1.15f) return true;
    }
    return false;
}

void VenueRenderer::startStarPower() {
    // FUN_002858c8: not while walking, already special, or after game over.
    if (!guitarist_ || guitarState_ == 4 || walkingNow() || (guitarist_->clipFlags() & Character::kGuitarSpecial)) return;
    guitarist_->play(Character::kGuitarSpecial, Character::kAnimFirst);
}

float VenueRenderer::dofDepth() const { return camera_ ? camera_->dofDepth() : 0.0f; }

void VenueRenderer::setDepthOfField(bool on) {
    if (camera_) camera_->setDepthOfField(on);
}

// FUN_001682f0: walk_spot_01, 02, ... until the first missing one.
std::vector<Xfm> VenueRenderer::walkSpots() {
    std::vector<Xfm> spots;
    for (int i = 1;; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "walk_spot_%02d.mesh", i);
        Xfm x;
        if (!spotPlacement(name, x)) break;
        spots.push_back(x);
    }
    return spots;
}
