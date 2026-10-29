// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/chartest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "data/band.h"
#include "data/disc.h"
#include "data/qbkey.h"
#include "data/venue.h"
#include "game/chart.h"
#include "render/band.h"
#include "render/band_anim.h"
#include "render/band_view.h"
#include "render/renderer.h"
#include "render/venue_stage.h"
#include "script/globals.h"

namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }
constexpr double kStepMs = 1000.0 / 60.0;
constexpr double kLeadInMs = 3000.0;
constexpr double kCheckMs = 250.0;

float healthAt(double stepMs) { return 1.0f + 0.95f * float(std::sin(stepMs / 1000.0 * 2 * 3.14159265358979 / 47.0)); }

bool finite(const bandanim::Mat34& m) {
    for (float v : m.m)
        if (!std::isfinite(v)) return false;
    return true;
}

std::string nameOf(uint32_t k) { return qbkey::names().get(k); }

std::string label(const band::Member& m) {
    std::string s = m.profileName + " " + nameOf(m.body);
    if (m.instrument) s += " + " + nameOf(m.instrument);
    return s;
}

// The bind pose's world matrices times the inverse binds: the identity [C data: docs/formats.md].
float bindError(const bandanim::Rig& rig) {
    std::vector<bandanim::Mat34> world(rig.size());
    float worst = 0;
    for (size_t b = 0; b < rig.size(); ++b) {
        const bandanim::Mat34 l = bandanim::matOf(rig.bind[b]);
        world[b] = rig.parent[b] >= 0 ? bandanim::mmul(world[size_t(rig.parent[b])], l) : l;
        const bandanim::Mat34 id = bandanim::mmul(world[b], rig.invBind[b]);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 4; ++j) worst = std::max(worst, std::fabs(id.m[i * 4 + j] - (i == j ? 1.0f : 0.0f)));
    }
    return worst;
}

// Skins every vertex of `m` with `skin`: false on a NaN; the bounds.
bool skinBounds(const band::Model& m, const std::vector<bandanim::Mat34>& skin, float lo[3], float hi[3]) {
    for (int i = 0; i < 3; ++i) lo[i] = 1e30f, hi[i] = -1e30f;
    for (const band::Part& p : m.parts)
        for (const scene::Mesh& mesh : p.scene.meshes)
            for (size_t k = 0; k < mesh.positions.size(); ++k) {
                bandanim::Vec out{};
                const auto& bi = mesh.blendIndices[k];
                const auto& bw = mesh.blendWeights[k];
                for (int j = 0; j < 3; ++j) {
                    if (bw[size_t(j)] <= 0 || bi[size_t(j)] >= skin.size()) continue;
                    const bandanim::Vec v = bandanim::xform(skin[bi[size_t(j)]], {mesh.positions[k][0], mesh.positions[k][1], mesh.positions[k][2]});
                    out.x += v.x * bw[size_t(j)], out.y += v.y * bw[size_t(j)], out.z += v.z * bw[size_t(j)];
                }
                const float c[3] = {out.x, out.y, out.z};
                for (int i = 0; i < 3; ++i) {
                    if (!std::isfinite(c[i])) return false;
                    lo[i] = std::min(lo[i], c[i]), hi[i] = std::max(hi[i], c[i]);
                }
            }
    return true;
}

// A grid of screenshots (6 x 2 cells of 320 x 360, the middle of each frame).
struct Grid {
    Grid(std::string d, std::string p) : dir(std::move(d)), prefix(std::move(p)) {}
    Grid(const Grid&) = delete;
    Grid& operator=(const Grid&) = delete;
    std::string dir, prefix;
    SDL_Surface* surf = nullptr;
    int cells = 0, files = 0;
    std::string legend;
    void add(SDL_Surface* frame, const std::string& name) {
        if (dir.empty() || !frame) return;
        if (!surf) surf = SDL_CreateSurface(1920, 720, SDL_PIXELFORMAT_ARGB8888);
        if (!surf) return;
        const int cw = 320, ch = 360;
        SDL_Rect src{frame->w / 4, 0, frame->w / 2, frame->h};
        SDL_Rect dst{(cells % 6) * cw, (cells / 6) * ch, cw, ch};
        SDL_BlitSurfaceScaled(frame, &src, surf, &dst, SDL_SCALEMODE_LINEAR);
        legend += "  " + std::to_string(cells) + ": " + name + "\n";
        if (++cells == 12) flush();
    }
    void flush() {
        if (!surf || !cells) return;
        char path[600];
        std::snprintf(path, sizeof path, "%s/%s_%02d.bmp", dir.c_str(), prefix.c_str(), files++);
        SDL_SaveBMP(surf, path);
        std::printf("chartest grid %s:\n%s", path, legend.c_str());
        legend.clear();
        SDL_FillSurfaceRect(surf, nullptr, 0);
        cells = 0;
    }
    ~Grid() {
        flush();
        if (surf) SDL_DestroySurface(surf);
    }
};

// One frame of a lone model through a camera 3.4 m in front of it (it faces +z).
SDL_Surface* portrait(gfx::Renderer& r, const band::Model& model, const std::vector<bandanim::Mat34>& skin, float height) {
    BandView view;
    std::string err;
    const int id = view.add(r.device(), model, err);
    if (id < 0) return nullptr;
    venue::Venue empty;
    venue::LightShow show;
    std::vector<venue::LightState> none;
    BandView::Instance in;
    in.member = id;
    in.skin = &skin;
    venue::CamPose cam;
    const float h = std::max(0.6f, height);
    cam.pos = {0, h * 0.5f, 2.2f * h + 0.6f};
    cam.rot = {0, 1, 0, 0};  // looking down -z
    cam.fov = 40;
    if (const char* z = SDL_getenv("GD04_CHARTEST_ZOOM")) {  // development aid: a closer look at the hands
        cam.fov = 40.0f / std::max(1.0f, float(std::atof(z)));
        cam.pos[1] = h * 0.62f;
        if (const char* yaw = SDL_getenv("GD04_CHARTEST_YAW")) {
            const float a = float(std::atof(yaw)) * 3.14159265f / 180.0f, d = 2.2f * h + 0.6f;
            cam.pos[0] = d * std::sin(a), cam.pos[2] = d * std::cos(a);
            cam.rot = {0, std::cos(a * 0.5f), 0, -std::sin(a * 0.5f)};
        }
    }
    const float grey[4] = {0.18f, 0.18f, 0.2f, 1};
    r.beginFrame(grey);
    view.draw(cam, {in}, empty, none, show);
    SDL_Surface* px = r.readPixels();
    r.endFrame();
    view.shutdown();
    return px;
}

struct BodyResult {
    bool ok = true;
    std::string why;
};

// Loads, checks, animates (and draws) one member.
BodyResult testMember(const Disc& disc, const Globals& g, band::Library& lib, const Chart& chart, const band::Member& m, int frames,
                      gfx::Renderer* r, Grid* grid, int& missingTex, int& missingClips, size_t& verts) {
    BodyResult res;
    std::string err;
    auto model = band::loadModel(disc, g, lib, m, chart.song, err);
    if (!model) return {false, err};
    const size_t bones = model->skeleton->bones.size();
    for (const band::Part& p : model->parts) {
        for (const scene::Mesh& mesh : p.scene.meshes) {
            verts += mesh.positions.size();
            if (mesh.blendIndices.size() != mesh.positions.size()) return {false, p.pak + ": a mesh without bone indices"};
            for (size_t k = 0; k < mesh.positions.size(); ++k)
                for (int j = 0; j < 3; ++j)
                    if (mesh.blendWeights[k][size_t(j)] > 0 && mesh.blendIndices[k][size_t(j)] >= bones)
                        return {false, p.pak + ": bone " + std::to_string(mesh.blendIndices[k][size_t(j)]) + " is not in the skeleton (" +
                                           std::to_string(bones) + " bones)"};
        }
        for (const scene::Material& mat : p.scene.materials)
            for (uint32_t t : mat.textures)
                if (!model->textures.count(t)) {
                    ++missingTex;
                    if (std::getenv("GD04_CHARTEST_VERBOSE"))
                        std::printf("  %s: material %08x texture %s missing\n", label(m).c_str(), mat.key, nameOf(t).c_str());
                }
    }
    bandanim::Animator a;
    bandanim::Animator::Setup s;
    s.model = model.get();
    s.g = &g;
    s.chart = &chart;
    s.seed = m.body ^ m.instrument;
    s.startMs = -kLeadInMs;
    if (!a.init(s, err)) return {false, err};
    const float be = bindError(a.rig());
    if (be > 1e-3f) return {false, "bind pose x inverse bind is off by " + std::to_string(be)};
    std::vector<bandanim::Mat34> world, skin;
    const double t0 = 20000.0;
    float lo[3], hi[3];
    for (int f = 0; f < frames; ++f) {
        const double t = t0 + double(f) * kStepMs;
        a.advance(t);
        a.evaluate(t, world, skin);
        for (const auto& mt : skin)
            if (!finite(mt)) return {false, "a NaN in the skinning matrices at " + std::to_string(int(t)) + " ms"};
        if (f % 30 == 0 && !skinBounds(*model, skin, lo, hi)) return {false, "a NaN in the skinned vertices"};
    }
    if (!skinBounds(*model, skin, lo, hi)) return {false, "a NaN in the skinned vertices"};
    const float height = hi[1] - lo[1];
    if (a.stats().bodyAnims == 0) res = {false, "no body animation played"};
    // A body stands between 0.4 and 3.5 m tall (Lou's wings, the drum kit) [I].
    if (!(height > 0.4f && height < 3.5f)) res = {false, "skinned height " + std::to_string(height) + " m"};
    missingClips += int(a.missing().size());
    if (std::getenv("GD04_CHARTEST_VERBOSE"))
        for (uint32_t k : a.missing()) std::printf("  %s: clip %s missing\n", label(m).c_str(), nameOf(k).c_str());
    if (r && grid) {
        if (SDL_Surface* px = portrait(*r, *model, skin, hi[1])) {
            grid->add(px, label(m) + (res.ok ? "" : "  [FAIL " + res.why + "]"));
            SDL_DestroySurface(px);
        }
    }
    return res;
}

// The band's bone positions (world) at the stage's last evaluate, for comparisons.
std::vector<float> bandPose(VenueStage& st, double t) {
    std::vector<float> out;
    Band* b = st.band();
    if (!b) return out;
    b->evaluate(t);
    for (const Band::Entry& e : b->members()) {
        for (const auto& m : e.world) {
            const bandanim::Vec p = bandanim::xform(e.root, {m.m[3], m.m[7], m.m[11]});
            out.push_back(p.x), out.push_back(p.y), out.push_back(p.z);
        }
    }
    return out;
}

struct Sample {
    double t = 0;
    std::vector<float> pose;
    venue::CamPose cam;
};

// The stage played from the lead-in to `endMs` at `fps` (as ui/song_screen.cpp does), sampled every kCheckMs.
std::vector<Sample> simulate(VenueStage& st, double endMs, int fps, int& offScreen, int& framed, std::string& offWhat) {
    std::vector<Sample> out;
    const double t0 = -kLeadInMs;
    const int perCheck = int(std::lround(kCheckMs * fps / 1000.0));
    long n = 0;
    for (long f = 0;; ++f) {
        const bool check = f % perCheck == 0;
        const double t = check ? t0 + double(f / perCheck) * kCheckMs : t0 + double(f) * 1000.0 / fps;
        if (t > endMs) break;
        while (t0 + double(n + 1) * kStepMs <= t) {
            ++n;
            const double step = t0 + double(n) * kStepMs;
            st.update(step, healthAt(step));
        }
        st.advance(t);
        if (!check) continue;
        Sample s;
        s.t = t;
        s.cam = st.camera(t);
        s.pose = bandPose(st, t);
        // The member the shot frames must be on screen.
        const uint32_t who = st.cameras().lookatMember(), bone = st.cameras().lookatBone();
        if (who && t >= 0 && st.band()) {
            ++framed;
            venue::Vec3 p;
            bool on = st.band()->bonePos(who, bone ? bone : K("bone_chest"), p);
            if (on) {
                const venue::CamPose& c = s.cam;
                const venue::Vec3 fwd = venue::rotate(c.rot, {0, 0, 1}), up = venue::rotate(c.rot, {0, 1, 0}),
                                  right = venue::rotate(c.rot, {-1, 0, 0});
                const float d[3] = {p[0] - c.pos[0], p[1] - c.pos[1], p[2] - c.pos[2]};
                const float z = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
                const float x = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
                const float y = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
                const float th = std::tan(c.fov * 0.5f * 3.14159265f / 180.0f), tv = th / (16.0f / 9.0f);
                on = z > 0.05f && std::fabs(x / z / th) <= 1.0f && std::fabs(y / z / tv) <= 1.0f;
            }
            if (!on) {
                if (!offScreen) offWhat = nameOf(who) + " at " + std::to_string(int(t)) + " ms (" + nameOf(st.cameras().shot().name) + ")";
                ++offScreen;
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

uint64_t hashPose(const std::vector<float>& p) {
    uint64_t h = 1469598103934665603ull;
    for (float v : p) {
        const int64_t q = std::llround(double(v) * 1000.0);
        for (int i = 0; i < 8; ++i) h = (h ^ uint64_t((q >> (8 * i)) & 0xFF)) * 1099511628211ull;
    }
    return h;
}

}  // namespace

int runCharTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::printf("chartest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    Globals g;
    try {
        g.load(*disc);
    } catch (const std::exception& e) {
        std::printf("chartest: globals: %s\n", e.what());
        return 2;
    }
    const char* songEnv = SDL_getenv("GD04_CHARTEST_SONG");
    const std::string song = songEnv ? songEnv : "slowride";
    Chart chart;
    try {
        chart = loadChart(*disc, song);
    } catch (const std::exception& e) {
        std::printf("chartest: %s: %s\n", song.c_str(), e.what());
        return 2;
    }
    const int frames = SDL_getenv("GD04_CHARTEST_FRAMES") ? std::max(1, std::atoi(SDL_getenv("GD04_CHARTEST_FRAMES"))) : 120;
    const char* shots = SDL_getenv("GD04_CHARTEST_SHOTS");
    const bool only = SDL_getenv("GD04_CHARTEST_ONLY") != nullptr;  // development aid: bodies whose label contains it
    const std::string onlyText = only ? SDL_getenv("GD04_CHARTEST_ONLY") : "";

    // The portraits have no venue lights: a brighter fill (render/band_view.cpp).
    SDL_setenv_unsafe("GD04_BAND_FILL", "0.85", 0);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("chartest: cannot start video: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow("gd04 character test", 1280, 720, gfx::prepareWindow() | SDL_WINDOW_HIDDEN);
    gfx::Renderer renderer;
    if (!window || !renderer.init(window, error)) {
        std::printf("chartest: no renderer: %s\n", window ? error.c_str() : SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    int failures = 0;
    auto fail = [&](const std::string& what) {
        std::printf("chartest: FAIL %s\n", what.c_str());
        ++failures;
    };

    // 1. Every body and instrument.
    band::Library lib;
    try {
        lib.load(*disc);
    } catch (const std::exception& e) {
        std::printf("chartest: %s\n", e.what());
        return 2;
    }
    int loaded = 0, notOnDisc = 0, missingTex = 0, missingClips = 0;
    size_t verts = 0;
    {
        Grid bodies{shots ? shots : "", "bodies"};
        const std::vector<band::Member> all = band::everyBody(g);
        for (const band::Member& m : all) {
            if (only && label(m).find(onlyText) == std::string::npos) continue;
            const BodyResult r = testMember(*disc, g, lib, chart, m, frames, &renderer, &bodies, missingTex, missingClips, verts);
            if (!r.ok && r.why.find("is not on the disc") != std::string::npos) {
                // A desc whose pak the disc does not carry (debug-only profiles) [C data].
                std::printf("chartest: %s: %s\n", label(m).c_str(), r.why.c_str());
                ++notOnDisc;
                continue;
            }
            // The debug_only profiles (BoneMan_*) borrow anim sets whose clips their paks lack [C data].
            const qb::Value* dbg = m.profile ? m.profile->find(K("debug_only")) : nullptr;
            if (!r.ok && dbg && dbg->number() == 1 && r.why == "no body animation played") {
                std::printf("chartest: %s (debug_only): %s\n", label(m).c_str(), r.why.c_str());
                ++loaded;
                continue;
            }
            if (!r.ok) fail(label(m) + ": " + r.why);
            else ++loaded;
        }
        std::printf("chartest: %zu bodies: %d loaded, skinned and animated, %d not on the disc\n", all.size(), loaded, notOnDisc);
    }
    int instLoaded = 0, instMissing = 0;
    {
        Grid inst{shots ? shots : "", "instruments"};
        const std::vector<band::Member> all = band::everyInstrument(g);
        for (const band::Member& m : all) {
            if (only && label(m).find(onlyText) == std::string::npos) continue;
            const BodyResult r = testMember(*disc, g, lib, chart, m, std::min(frames, 30), &renderer, &inst, missingTex, missingClips, verts);
            if (!r.ok && r.why.find("is not on the disc") != std::string::npos) {
                std::printf("chartest: %s: %s\n", label(m).c_str(), r.why.c_str());
                ++instMissing;
                continue;
            }
            if (!r.ok) fail(label(m) + ": " + r.why);
            else ++instLoaded;
        }
        std::printf("chartest: %zu instruments: %d loaded, skinned and animated, %d not on the disc\n", all.size(), instLoaded, instMissing);
    }
    if (missingTex) fail(std::to_string(missingTex) + " material textures missing");
    std::printf("chartest: %zu vertices skinned; %d clip names asked for and absent (GD04_CHARTEST_VERBOSE=1 lists them)\n", verts, missingClips);

    // 2. The band of every venue: 60 vs 240 fps, the framed member on screen; 3. golden poses.
    std::vector<std::string> zones;
    if (const char* z = SDL_getenv("GD04_VENUE")) zones.push_back(z);
    else if (!only) zones = venue::venueZones(g);
    double songEnd = 0;
    for (const ChartEvent& e : chart.cameras) songEnd = std::max(songEnd, double(e.time + e.length));
    songEnd += 2000.0;
    const uint32_t songKey = qbkey::key(song);
    const char* goldenPath = SDL_getenv("GD04_CHARTEST_GOLDEN");
    std::map<std::string, std::string> golden, goldenNow;
    bool haveGolden = false;
    if (goldenPath) {
        std::ifstream in(goldenPath);
        std::string zone, hash;
        while (in >> zone >> hash) golden[zone] = hash, haveGolden = true;
    }
    int bandVenues = 0, goldenMatched = 0;
    for (const std::string& zone : zones) {
        VenueStage a, b;
        if (!a.start(*disc, g, zone, chart, songKey, kLeadInMs, nullptr, error) ||
            !b.start(*disc, g, zone, chart, songKey, kLeadInMs, nullptr, error)) {
            fail(zone + ": " + error);
            continue;
        }
        if (!a.band() || !b.band()) {
            fail(zone + ": no band");
            continue;
        }
        size_t placed = 0;
        for (const Band::Entry& e : a.band()->members()) placed += e.placed;
        if (placed != a.band()->members().size()) fail(zone + ": " + std::to_string(a.band()->members().size() - placed) + " members without a waypoint");
        int offA = 0, framedA = 0, offB = 0, framedB = 0;
        std::string offWhat, offWhatB;
        const std::vector<Sample> sa = simulate(a, songEnd, 60, offA, framedA, offWhat);
        const std::vector<Sample> sb = simulate(b, songEnd, 240, offB, framedB, offWhatB);
        size_t agree = 0;
        bool differ = sa.size() != sb.size();
        for (size_t i = 0; !differ && i < sa.size(); ++i) {
            const Sample &x = sa[i], &y = sb[i];
            bool same = x.t == y.t && x.pose.size() == y.pose.size();
            for (size_t k = 0; same && k < x.pose.size(); ++k) same = std::fabs(x.pose[k] - y.pose[k]) <= 1e-4f;
            if (!same) {
                fail(zone + ": the band's pose at " + std::to_string(int(x.t)) + " ms differs between 60 and 240 fps");
                differ = true;
                break;
            }
            ++agree;
        }
        if (offA) fail(zone + ": " + std::to_string(offA) + " of " + std::to_string(framedA) + " framed checkpoints lose their member, first " + offWhat);
        // Golden: the poses at 5, 30 and 60 s of song time.
        std::vector<float> all;
        for (const Sample& s : sa)
            if (s.t == 5000 || s.t == 30000 || s.t == 60000) all.insert(all.end(), s.pose.begin(), s.pose.end());
        char hash[32];
        std::snprintf(hash, sizeof hash, "%016llx", static_cast<unsigned long long>(hashPose(all)));
        goldenNow[zone] = hash;
        if (haveGolden) {
            auto it = golden.find(zone);
            if (it == golden.end()) fail(zone + ": not in the golden file");
            else if (it->second != hash) fail(zone + ": golden poses differ (" + it->second + " -> " + hash + ")");
            else ++goldenMatched;
        }
        std::string members;
        for (const Band::Entry& e : a.band()->members())
            members += std::string(members.empty() ? "" : ", ") + band::roleName(e.model->member.role) + " " + e.model->member.profileName;
        const auto& st = a.band()->members().front().anim.stats();
        std::printf("chartest %s: %s; 60/240 fps: %zu checkpoints identical; %d framed checkpoints, %d off screen; guitarist: %d body anims, "
                    "%d strums, %d frets, %d chords\n",
                    zone.c_str(), members.c_str(), agree, framedA, offA, st.bodyAnims, st.strums, st.frets, st.chords);
        ++bandVenues;

        // 4. Screenshots: the band in the middle of the first shots.
        if (shots) {
            VenueStage s;
            if (s.start(*disc, g, zone, chart, songKey, kLeadInMs, &renderer.device(), error)) {
                double step = -kLeadInMs;
                std::vector<double> times;
                if (const char* at = SDL_getenv("GD04_CHARTEST_AT")) {  // development aid: "<ms>,<ms>,..."
                    for (const char* p = at; *p;) {
                        char* end = nullptr;
                        const double v = std::strtod(p, &end);
                        if (end == p) break;
                        times.push_back(v);
                        p = *end == ',' ? end + 1 : end;
                    }
                } else {
                    for (const auto& h : a.cameras().history())
                        if (h.start >= 0 && times.size() < 6) times.push_back(h.start + std::min(h.end - h.start, 6000.0) * 0.5);
                }
                Grid grid{shots, "band_" + zone};
                for (double t : times) {
                    while (step + kStepMs <= t) {
                        step += kStepMs;
                        s.update(step, healthAt(step));
                    }
                    s.advance(t);
                    const float black[4] = {0, 0, 0, 1};
                    renderer.beginFrame(black);
                    s.draw(t);
                    SDL_Surface* px = renderer.readPixels();
                    renderer.endFrame();
                    if (!px) continue;
                    char name[600];
                    std::snprintf(name, sizeof name, "%s/band_%s_%06d.bmp", shots, zone.c_str(), int(t));
                    SDL_SaveBMP(px, name);
                    SDL_DestroySurface(px);
                }
            }
        }
    }
    if (goldenPath && !haveGolden) {
        std::ofstream out(goldenPath);
        for (const auto& [z, h] : goldenNow) out << z << " " << h << "\n";
        std::printf("chartest: golden poses written to %s\n", goldenPath);
    } else if (haveGolden) {
        std::printf("chartest: golden poses: %d of %zu venues match\n", goldenMatched, zones.size());
    }
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (failures) {
        std::printf("chartest: %d failures\n", failures);
        return 1;
    }
    std::printf("chartest: %d bodies and %d instruments skinned and animated; the band in %d venues frame-rate independent and on screen\n",
                loaded, instLoaded, bandVenues);
    return 0;
}
