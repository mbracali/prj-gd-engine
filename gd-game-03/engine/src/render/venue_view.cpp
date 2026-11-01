// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/venue_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "app_ids.h"
#include "char/char_objects.h"
#include "data/game_data.h"
#include "devaids/screenshot.h"
#include "game/band.h"
#include "game/camera_director.h"
#include "game/crowd.h"
#include "game/world.h"
#include "render/band_view.h"
#include "render/canvas.h"
#include "render/crowd_view.h"
#include "render/renderer.h"

using gfx::Mat4;

namespace {

bool drawable(const scene::Object* o) {
    return o->as<scene::Mesh>() || o->as<scene::Group>() || o->as<scene::ParticleSys>() || o->as<scene::WorldCrowd>() ||
           o->as<scene::CharacterObj>() ||  // a band slot (render/band_view.h)
           (o->as<scene::DirObj>() && !o->as<scene::DirObj>()->own);
}

const scene::Environ* dirEnv(const scene::Directory& d) {
    const auto* own = d.self ? d.self->as<scene::DirObj>() : nullptr;
    if (!own || !own->hasRnd || own->rnd.environment.empty()) return nullptr;
    return d.resolve<scene::Environ>(own->rnd.environment);
}

// A directory's draw list: its drawables less those its Groups draw, by
// draw_order (stable: table order within one order).
std::vector<const scene::Object*> drawList(const std::vector<const scene::Directory*>& dirs) {
    std::set<const scene::Object*> children;
    std::vector<const scene::Object*> all;
    for (const scene::Directory* d : dirs)
        d->forEach([&](scene::Object& o) {
            if (!o.decoded) return;
            if (auto* g = o.as<scene::Group>())
                for (const std::string& n : g->objects)
                    if (const scene::Object* c = g->dir ? g->dir->resolve(n) : nullptr; c && c != g) children.insert(c);
            if (drawable(&o)) all.push_back(&o);
        });
    std::vector<const scene::Object*> roots;
    for (const scene::Object* o : all)
        if (!children.count(o)) roots.push_back(o);
    std::stable_sort(roots.begin(), roots.end(), [](const scene::Object* a, const scene::Object* b) {
        const scene::Draw* da = gfx::drawOf(a);
        const scene::Draw* db = gfx::drawOf(b);
        return (da ? da->order : 0) < (db ? db->order : 0);
    });
    return roots;
}

}  // namespace

VenueView::VenueView(World& world, gfx::SceneCache& cache) : world_(world), cache_(cache) {
    particleRenderer_ = std::make_unique<gfx::ParticleRenderer>(cache.device());
    dynamic_ = std::make_unique<gfx::DynamicMesh>(cache.device());
    spotlights_ = std::make_unique<gfx::SpotlightView>(world, cache);
    crowdView_ = std::make_unique<CrowdView>(world, cache);
    bandView_ = std::make_unique<BandView>(world, cache);
    build();
    setShot(world_.firstShot());
}

VenueView::~VenueView() = default;

void VenueView::build() {
    Venue& v = world_.venue();
    // The chain: one draw list over every directory (FindObject's view).
    std::vector<const scene::Directory*> dirs;
    for (const auto& p : v.parts()) dirs.push_back(&p.milo->root());
    for (const scene::Object* o : drawList(dirs)) {
        const scene::Environ* env = o->dir ? dirEnv(*o->dir) : nullptr;
        roots_.push_back({o, env});
    }
    for (const auto& p : v.proxies()) {
        auto& list = proxyRoots_[p.object];
        for (const scene::Object* o : drawList({&p.milo->root()})) list.push_back({o, dirEnv(p.milo->root())});
    }
    // Every particle system of the venue as one running copy.
    v.forEach([&](scene::Object& o) {
        if (o.as<scene::ParticleSys>() || o.as<scene::ParticleSysAnim>()) particleSet_.add(&o, uint32_t(particleSet_.systems().size() + 7));
    });
    for (size_t i = 0; i < particleSet_.systems().size(); ++i) systemIndex_[&particleSet_.systems()[i].def()] = i;
}

bool venueObjectXfm(const World& world, const std::string& name, bool multiplayer, scene::Xfm& out) {
    static const std::pair<const char*, uint32_t> kSlots[] = {{"guitarist0", 1},  {"guitarist1", 2}, {"singer", 4},
                                                              {"keyboard", 8},    {"bassist", 16},   {"drummer", 32}};
    const Venue& v = world.venue();
    // The band, when loaded: the member itself (game/band.h).
    if (world.band && world.band->objectXfm(name, "", out)) return true;
    for (const auto& [slot, flag] : kSlots) {
        if (name != slot) continue;
        const uint32_t want = multiplayer && flag == 1 ? 512u : flag;
        for (const scene::Waypoint* w : v.all<scene::Waypoint>())
            if (w->flags & want) {
                out = world.pose().world(w);
                return true;
            }
        return false;
    }
    const scene::Object* o = v.find(name);
    if (!o || !gfx::transOf(o)) return false;
    out = world.pose().world(o);
    return true;
}

// Keyframe k of the shot (from its start, plus `frame`), as the camera
// director frames it (CameraDirector::evaluate: GH2's look-at, screen
// offset, path and parent, without the filter and the shake).
gfx::Camera shotCamera(const World& world, const scene::CamShot& shot, size_t k, float frame) {
    if (shot.keyframes.empty()) return {};
    float start = 0;
    for (size_t i = 0; i < std::min(k, shot.keyframes.size() - 1); ++i) start += shot.keyframes[i].duration + shot.keyframes[i].blend;
    return CameraDirector::evaluate(world, shot, start + frame);
}

void VenueView::setShot(const scene::CamShot* shot, size_t k) {
    shot_ = shot;
    if (!shot || shot->keyframes.empty()) {
        camera_ = gfx::Camera::lookAt({0, -400, 120}, {0, 0, 60});
        camera_.nearPlane = 10, camera_.farPlane = 20000;
        return;
    }
    camera_ = shotCamera(world_, *shot, k);
}

void VenueView::update() {
    const gfx::ScenePose& pose = world_.pose();
    particleSet_.sync(pose);
    particleSet_.update(pose, scene::Xfm{}, float(world_.seconds() * 30.0));
}

scene::Xfm VenueView::worldOf(const scene::Object* o, const scene::Xfm* base) const {
    const scene::Xfm w = world_.pose().world(o);
    return base ? scene::compose(w, *base) : w;
}

void VenueView::selectEnv(gfx::MaterialRenderer& mr, const scene::Environ* env) {
    if (envSet_ && env == currentEnv_) return;
    envSet_ = true;
    currentEnv_ = env;
    gfx::LightEnv le;
    if (env) le = cache_.lightEnv(*env, &world_.pose());
    if (filling_) {
        static constexpr float kGain[4] = {1, 1.15f, 1.3f, 1.45f}, kLift[4] = {0, 0.035f, 0.07f, 0.1f};  // none, 1.5x, 2x, 2.5x
        const size_t ml = size_t(std::clamp(look_.moreLights, 0, 3));
        const float gain = kGain[ml], lift = kLift[ml];
        for (float* c : {&le.envAmbient.x, &le.envAmbient.y, &le.envAmbient.z}) *c = std::min(1.0f, *c * gain + lift);
        currentEnv_ = nullptr;  // the next unfilled draw selects it again
    }
    mr.setFrame(camera_, le, viewW_, viewH_);
}

void VenueView::draw(gfx::Renderer& r, int viewW, int viewH, bool hdr) {
    viewW_ = std::max(1, viewW), viewH_ = std::max(1, viewH);
    camera_.aspect = float(viewW_) / float(viewH_);
    // TAA moves the picture by a sub-pixel each frame (Enhanced Options).
    const std::array<float, 2> j = hdr ? r.post().jitter() : std::array<float, 2>{0, 0};
    camera_.jitterX = j[0], camera_.jitterY = j[1];
    hdr_ = hdr;
    envSet_ = false;
    meshes_ = particles_ = 0;
    particleRenderer_->resetStats();
    crowdView_->resetStats();
    bandView_->resetStats();
    particleRenderer_->setHdr(hdr);
    gfx::MaterialRenderer& mr = r.materials();
    for (const Root& root : roots_) drawObject(mr, root.object, root.env, nullptr, 0);
    particles_ = particleRenderer_->drawnParticles();
    // The Enhanced Options' shadows, pools, beams and dust (after the list, before GH2's beams).
    if (look_.any()) {
        if (!lights_) lights_ = std::make_unique<gfx::EnhancedLights>(cache_.device());
        lights_->draw(look_, feet_, camera_, hdr, timeSec_);
    } else if (lights_) {
        lights_->draw(look_, {}, camera_, hdr, timeSec_);  // clears its counts
    }
    // Spotlights (cans, floor spots, beams, lenses) and flares after the list.
    spotlights_->draw(mr, camera_, viewW_, viewH_, hdr, hdr ? r.post().worldTarget() : gfx::TargetId{},
                      hdr ? r.post().worldDepth() : gfx::TextureId{});
    envSet_ = false;
}

void VenueView::drawObject(gfx::MaterialRenderer& mr, const scene::Object* o, const scene::Environ* env, const scene::Xfm* base,
                           int depth) {
    const gfx::ScenePose& pose = world_.pose();
    if (!o || depth > 24 || !pose.showing(o)) return;
    if (auto* m = o->as<scene::Mesh>()) {
        selectEnv(mr, env);
        drawMesh(mr, *m, base);
    } else if (auto* g = o->as<scene::Group>()) {
        // RndGroup::DrawShowing: its Environ while its members draw.
        if (!g->environment.empty() && g->dir)
            if (auto* e = g->dir->resolve<scene::Environ>(g->environment)) env = e;
        for (const std::string& n : g->objects)
            if (const scene::Object* c = g->dir ? g->dir->resolve(n) : nullptr; c && c != g) drawObject(mr, c, env, base, depth + 1);
    } else if (o->as<scene::CharacterObj>()) {
        // A band slot: its member, when the band is loaded.
        // More Lights on Venues: a fill on the band (its Environ's ambient raised, gd01's).
        filling_ = look_.moreLights > 0;
        if (filling_) envSet_ = false;
        bandView_->draw(mr, o, env, camera_, hdr_, [&](const scene::Environ* e) { selectEnv(mr, e); });
        if (filling_) envSet_ = false;
        filling_ = false;
    } else if (auto* c = o->as<scene::WorldCrowd>()) {
        // The audience (render/crowd_view.h); GD03_CROWD=0 leaves it out.
        static const bool noCrowd = [] {
            const char* e = SDL_getenv("GD03_CROWD");
            return e && *e == '0';
        }();
        selectEnv(mr, env);
        if (!noCrowd)
            crowdView_->draw(mr, *c, env, camera_, hdr_, [&](const scene::Environ* e) {
                envSet_ = false;  // the card pass set its own camera and lights
                selectEnv(mr, e);
            });
    } else if (auto* p = o->as<scene::ParticleSys>()) {
        auto it = systemIndex_.find(p);
        if (it == systemIndex_.end()) return;
        const gfx::ParticleSystem& s = particleSet_.systems()[it->second];
        if (s.count() == 0 && p->preserved.empty()) return;
        selectEnv(mr, env);
        particleRenderer_->draw(mr, cache_, s, camera_, base ? gfx::toMat4(*base) : Mat4::identity(), &pose);
    } else if (auto* d = o->as<scene::DirObj>()) {
        // A proxy directory: its milo's draw list at the proxy's transform.
        auto it = proxyRoots_.find(d);
        if (it == proxyRoots_.end()) return;
        const scene::Xfm b = worldOf(d, base);
        for (const Root& root : it->second) drawObject(mr, root.object, root.env ? root.env : env, &b, depth + 1);
    }
}

void VenueView::drawMesh(gfx::MaterialRenderer& mr, const scene::Mesh& m, const scene::Xfm* base) {
    const gfx::ScenePose& pose = world_.pose();
    const gfx::GpuMesh* cached = cache_.mesh(&m);
    if (!cached || !m.dir) return;
    gfx::GpuMesh g = *cached;
    const scene::Mesh* geom = &m;
    if (m.verts.empty() && !m.geomOwner.empty())
        if (auto* owner = m.dir->resolve<scene::Mesh>(m.geomOwner)) geom = owner;
    // MeshAnim vertices.
    if (const gfx::ScenePose::MeshAnimState* ma = pose.meshAnim(&m); ma && ma->anim) {
        const std::vector<float> v = gfx::meshAnimVertices(*geom, *ma->anim, ma->frame);
        std::vector<uint16_t> idx;
        idx.reserve(geom->faces.size() * 3);
        for (const auto& f : geom->faces) idx.insert(idx.end(), {f[0], f[1], f[2]});
        g = dynamic_->upload(v, idx);
        g.skinned = !geom->bones.empty();
    }
    Mat4 bones[4];
    const Mat4 world = gfx::toMat4(worldOf(&m, base));
    bones[0] = world;
    int boneCount = 1;
    if (g.skinned && !geom->bones.empty()) {
        boneCount = int(std::min<size_t>(geom->bones.size(), 4));
        for (int i = 0; i < boneCount; ++i) {
            const auto& bone = geom->bones[size_t(i)];
            const scene::Object* bo = m.dir->resolve(bone.name);
            bones[i] = bo && gfx::transOf(bo) ? gfx::toMat4(scene::compose(bone.offset, worldOf(bo, base))) : world;
        }
    } else {
        g.skinned = false;
    }
    gfx::DrawContext ctx;
    ctx.bones = boneCount;
    ctx.hdr = hdr_;
    const scene::Mat* mat = m.dir->resolve<scene::Mat>(m.mat);
    if (!mat) {
        mr.draw(g, bones, gfx::Material{}, ctx);
        ++meshes_;
        return;
    }
    // The material and its next passes (at most four, a loop guard).
    for (int pass = 0; mat && pass < 4; ++pass) {
        mr.draw(g, bones, cache_.material(*mat, &pose), ctx);
        if (mat->nextPass.empty() || !mat->dir) break;
        const scene::Mat* next = mat->dir->resolve<scene::Mat>(mat->nextPass);
        mat = next != mat ? next : nullptr;
    }
    ++meshes_;
}

// ---- the venue viewer --------------------------------------------------------

namespace {

// "arena", "world/arena/gen/arena_mp.milo_ps2" -> venue and variant.
bool parseVenue(const std::string& path, std::string& venue, VenueVariant& variant) {
    if (venues::known(path)) {
        venue = path, variant = VenueVariant::Single;
        return true;
    }
    for (const std::string& v : venues::names())
        for (VenueVariant var : {VenueVariant::Single, VenueVariant::Mp, VenueVariant::Coop})
            if (scene::miloArkPath(path) == venues::worldMilo(v, var)) {
                venue = v, variant = var;
                return true;
            }
    return false;
}

}  // namespace

bool isVenuePath(const std::string& path) {
    std::string v;
    VenueVariant var;
    return parseVenue(path, v, var);
}

int runVenueView(const std::string& dataPath, const std::string& venueOrMilo) {
    std::string name;
    VenueVariant variant = VenueVariant::Single;
    if (!parseVenue(venueOrMilo, name, variant)) {
        std::fprintf(stderr, "%s --view: %s is not a venue\n", app::kProgram, venueOrMilo.c_str());
        return 2;
    }
    std::unique_ptr<GameData> data;
    std::unique_ptr<Venue> venue;
    try {
        data = std::make_unique<GameData>(dataPath);
        venue = std::make_unique<Venue>(*data, name, variant);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s --view: %s\n", app::kProgram, e.what());
        return 2;
    }
    for (const auto& p : venue->problems()) SDL_Log("venue: %s", p.c_str());
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "%s --view: cannot start video: %s\n", app::kProgram, SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), SDL_WINDOW_RESIZABLE | gfx::prepareWindow());
    Canvas canvas;
    std::string error;
    if (!window || !canvas.init(window, error)) {
        std::fprintf(stderr, "%s --view: cannot open the window: %s\n", app::kProgram, window ? error.c_str() : SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    int code = 0;
    {
        auto cache = std::make_unique<gfx::SceneCache>(canvas.renderer().device());
        World world(*venue);
        VenueView view(world, *cache);
        const std::vector<const scene::CamShot*> shots = world.shots();
        std::vector<const scene::LightPreset*> presets;
        for (const scene::LightPreset* p : venue->all<scene::LightPreset>())
            if (!p->keyframes.empty()) presets.push_back(p);
        size_t shot = 0, keyframe = 0, preset = 0;
        if (const scene::CamShot* first = world.firstShot())
            shot = size_t(std::find(shots.begin(), shots.end(), first) - shots.begin());
        if (const char* c = SDL_getenv("GD03_VIEW_CAM")) shot = size_t(std::max(0, std::atoi(c)));
        if (const char* c = SDL_getenv("GD03_VIEW_CATEGORY"))
            for (size_t i = shots.size(); i-- > 0;)
                if (shots[i]->category == c) shot = i;
        if (const char* s = SDL_getenv("GD03_VIEW_SHOT"))
            for (size_t i = 0; i < shots.size(); ++i)
                if (shots[i]->name == s) shot = i;
        if (const scene::LightPreset* p = world.firstPreset())
            preset = size_t(std::find(presets.begin(), presets.end(), p) - presets.begin());
        // GD03_VIEW_PRESET=<name> starts with that preset, "none" with none.
        bool noPreset = false;
        if (const char* pn = SDL_getenv("GD03_VIEW_PRESET")) {
            noPreset = std::string(pn) == "none";
            for (size_t i = 0; i < presets.size(); ++i)
                if (presets[i]->name == pn) preset = i;
        }
        if (preset < presets.size() && !noPreset) world.applyPreset(*presets[preset], 0);
        // The clock: 120 beats a minute.
        double t = 0;
        auto clock = [&](double s) {
            world.setClock(s, s * 2.0);
            world.update();
            view.update();
        };
        clock(0);
        world.start(variant != VenueVariant::Single);
        world.message("intro_start");
        // The crowd (game/crowd.h): GD03_VIEW_EXCITEMENT=<0..3> then each
        // shot's start (crowd_update); GD03_VIEW_ENCORE=1 the encore intro
        // (play_encore_fx), =2 also the encore outro.
        if (const char* e = SDL_getenv("GD03_VIEW_ENCORE")) {
            world.message("intro_start_msg", {Value::integer(0), Value::integer(1)});
            if (std::atoi(e) > 1) world.message("encore_outro");
        }
        if (const char* e = SDL_getenv("GD03_VIEW_EXCITEMENT")) world.message("excitement", {Value::integer(std::atoi(e))});
        size_t crowdShot = size_t(-1);
        if (const char* vt = SDL_getenv("GD03_VIEW_TIME"))
            for (double end = std::atof(vt); t < end; t += 1.0 / 30.0) clock(t);
        SDL_Log("view: venue %s %s: %d milos, %d proxies, %d draw roots, %d shots, %d presets", name.c_str(), venues::variantName(variant),
                int(venue->parts().size()), int(venue->proxies().size()), int(view.roots()), int(shots.size()), int(presets.size()));
        uint64_t last = SDL_GetTicksNS();
        bool running = true;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) running = false;
                if (e.type != SDL_EVENT_KEY_DOWN) continue;
                const SDL_Keycode k = e.key.key;
                if (k == SDLK_ESCAPE) running = false;
                if (!shots.empty() && k == SDLK_RIGHT) shot = (shot + 1) % shots.size(), keyframe = 0;
                if (!shots.empty() && k == SDLK_LEFT) shot = (shot + shots.size() - 1) % shots.size(), keyframe = 0;
                if (k == SDLK_UP) ++keyframe;
                if (k == SDLK_DOWN && keyframe > 0) --keyframe;
                if (k == SDLK_P && !presets.empty()) {
                    preset = (preset + 1) % presets.size();
                    world.applyPreset(*presets[preset], 0);
                    SDL_Log("view: preset %s (%s)", presets[preset]->name.c_str(), presets[preset]->category.c_str());
                }
                if (k >= SDLK_1 && k <= SDLK_5) world.message("hit_p0_fret" + std::to_string(int(k - SDLK_1) + 1));
                if (k == SDLK_K) world.message("kick_drum");
                if (k == SDLK_B) world.message("bass_hit");
                if (k == SDLK_E) world.message("venue_effect");
                if (k == SDLK_G) world.message("excitement", {Value::integer(3)}), crowdShot = size_t(-1);
                if (k == SDLK_O) world.message("excitement", {Value::integer(2)}), crowdShot = size_t(-1);
                if (k == SDLK_X) world.message("excitement", {Value::integer(1)}), crowdShot = size_t(-1);
            }
            const uint64_t now = SDL_GetTicksNS();
            t += std::min(0.1, double(now - last) / 1e9);
            last = now;
            clock(t);
            if (shot < shots.size()) view.setShot(shots[shot], std::min(keyframe, shots[shot]->keyframes.size() - 1));
            if (shot < shots.size() && shot != crowdShot) world.crowd().startShot(shots[shot], true), crowdShot = shot;
            canvas.begin({0, 0, 0, 255});
            gfx::Renderer& r = canvas.renderer();
            int vx, vy, vw, vh;
            r.pictureRect(vx, vy, vw, vh);
            // GH2's post-process with the shot's PostProc (GD03_POST=0: off).
            const scene::PostProc* pp = view.shot() ? venue->find<scene::PostProc>(view.shot()->flag("postprocess")) : nullptr;
            if (pp) r.post().select(gfx::postProcState(*pp, pp->luminanceMap.empty() ? gfx::TextureId{} : cache->texture(pp, pp->luminanceMap)));
            else r.post().reset();
            const float black[4] = {0, 0, 0, 1};
            if (r.post().begin(black)) {
                r.post().size(vw, vh);
                view.draw(r, vw, vh, true);
                r.post().end();
            } else {
                view.draw(r, vw, vh, false);
            }
            if (!SDL_getenv("GD03_VIEW_CLEAN")) {
                char line[256];
                std::snprintf(line, sizeof line, "%s %s  shot %s (%s, %zu/%zu)  preset %s  %d meshes  %d particles  %d anims  t %.1f s",
                              name.c_str(), venues::variantName(variant), view.shot() ? view.shot()->name.c_str() : "-",
                              view.shot() ? view.shot()->category.c_str() : "-", shots.empty() ? 0 : shot + 1, shots.size(),
                              preset < presets.size() ? presets[preset]->name.c_str() : "-", view.meshesDrawn(), view.particlesDrawn(),
                              world.runningAnims(), t);
                canvas.flush();
                canvas.text(12, 8, 1.5f, {235, 235, 240, 255}, line);
                std::string crowdLine;
                for (const Crowd::State& c : world.crowd().crowds())
                    crowdLine += c.crowd->name + " " + std::to_string(c.shownCount()) + "/" + std::to_string(c.total()) +
                                 (c.rotate ? " rot" : "") + (c.show3dOnly ? " 3d-only" : "") + "  ";
                if (!crowdLine.empty())
                    canvas.text(12, 30, 1.5f, {235, 235, 240, 255},
                                "crowd: " + crowdLine + " drawn " + std::to_string(view.crowdView().charactersDrawn()) + " in " +
                                    std::to_string(view.crowdView().drawCalls()) + " draws");
            }
            if (captureDebugScreenshot(canvas, false, 0)) running = false;
            canvas.end();
        }
    }
    canvas.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}
