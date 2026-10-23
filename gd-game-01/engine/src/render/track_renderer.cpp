// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/track_renderer.h"

#include <SDL3/SDL.h>

#include "scene/anim.h"

#include <algorithm>
#include <cmath>

#include "data/bitmap.h"
#include "data/dta.h"
#include "data/game_data.h"

namespace {

// Track layout recovered from the game and its track scene:
// - lane centers: the lane-line mesh divides the 20-unit-wide surface at
//   +-1.96 and +-5.9, so lanes are 3.93 wide (surface vertices at +-7.87)
// - scroll speed: track_speed * 0.06 units per ms (FUN_00163780)
// - notes enter at horizon_y 110 and leave at remove_y -10 (FUN_0015a4b8)
// - the star gem meshes are authored at (7.85, 6.55); gem meshes at the origin
constexpr float kLaneX[5] = {-7.87f, -3.935f, 0.0f, 3.935f, 7.87f};
constexpr float kUnitsPerMsPerSpeed = 0.06f;
constexpr float kHorizonY = 110, kRemoveY = -10;
constexpr float kStarOffsetX = 7.85f, kStarOffsetY = 6.527f;  // gem_star.view, the authoring parent
constexpr float kSurfaceVPerUnit = 1.2985f / 129.18f;  // track_surface.mesh uv span / length
const char* const kColors[5] = {"green", "red", "yellow", "blue", "orange"};

Mat4 translate(float x, float y, float z) {
    Mat4 m;
    m.at(0, 3) = x, m.at(1, 3) = y, m.at(2, 3) = z;
    return m;
}

}  // namespace

// The sustain tail (FUN_001609b0 / FUN_001633d8, docs/render_gems_dof.md
// §2.3): tail02.mesh's 6-vertex cross-section swept along y in N = 30 rows of
// 4 units anchored to the far end (the first N - k rows collapse onto the
// start), each row widened by its whammy sample when the tail is played, the
// last two rows a 1-unit pointed tip.
int TrackRenderer::tailStrip(float x, float y0, float y1, bool played, float& maxS) {
    if (profile_.empty())
        if (const SceneObject* o = scene_->find("tail02.mesh"); o && o->mesh) {
            for (const MeshVertex& v : o->mesh->verts)
                if (std::fabs(v.pos[1]) < 0.01f) profile_.push_back(v);
            std::sort(profile_.begin(), profile_.end(), [](const MeshVertex& a, const MeshVertex& b) { return a.pos[0] < b.pos[0]; });
        }
    const int kRows = rows_;
    const size_t np = profile_.size();
    float len = y1 - y0;
    int k = std::clamp(int(std::floor(len / 4.0f)), 1, kRows);
    float r = len - 4.0f * float(k);
    auto sample = [&](float d) {
        if (!played || whammy_.empty()) return 0.0f;
        return whammy_[std::min(whammy_.size() - 1, size_t(std::max(0.0f, d) / 2.0f))];
    };
    std::vector<Vertex> v;
    v.reserve(size_t(kRows + 2) * np);
    maxS = 1;
    auto row = [&](float d, float s) {
        for (const MeshVertex& p : profile_)
            v.push_back({x + p.pos[0] * s, y0 + d, p.pos[2], p.uv[0], p.uv[1], 255, 255, 255,
                         uint8_t(std::lround(std::clamp(p.extra[3], 0.0f, 1.0f) * 255)), p.normal[0], p.normal[1], p.normal[2]});
    };
    // Rows at r, r + 4, ..., L - 4 (collapsed ones at 0), then L - 1 and the tip at L.
    for (int i = 0; i < kRows - k; ++i) row(0, 1 + sample(0));
    for (int i = 0; i < k; ++i) {
        float d = r + 4.0f * float(i);
        float s = 1 + sample(d);
        maxS = std::max(maxS, s);
        row(d, s);
    }
    float h = sample(len - 1);
    row(len - 1, h * 0.5f + 1);
    row(len, 0);
    std::vector<uint32_t> idx;
    size_t rows = v.size() / np;
    for (size_t j = 0; j + 1 < rows; ++j)
        for (size_t q = 0; q + 1 < np; ++q) {
            uint32_t a = uint32_t(j * np + q), b = a + 1, c2 = uint32_t((j + 1) * np + q), d2 = c2 + 1;
            idx.insert(idx.end(), {a, c2, b, b, c2, d2});
        }
    return sr_->dynamicMesh(v, idx);
}

// Face-off dual perspective (FUN_001592e8): the single-player framing scaled
// by 0.76 (track_scale) and centred in the player's half of the screen; 20
// tail rows (FUN_001633d8) and only the player's own gems.
void TrackRenderer::setFaceOff(int player) {
    player_ = player;
    rows_ = player < 0 ? 30 : 20;
    screen_ = Mat4{};
    if (player < 0) return;
    screen_.at(0, 0) = screen_.at(1, 1) = 0.76f;
    screen_.at(0, 3) = player == 0 ? -0.5f : 0.5f;  // NDC: the centre of the left / right half
}

bool TrackRenderer::setSurface(const GameData& data, const std::string& character) {
    if (character == surface_) return true;
    try {
        // (metal (track_surface "track/surfaces/metal.bmp")) -> track/surfaces/gen/metal.bmp_ps2
        DataNode chars = parseDtb(data.read(dtbPath("config/characters.dta")));
        for (const DataNode& c : chars.children) {
            if (!c.isList() || c.children.empty() || c.children[0].s != character) continue;
            const DataNode* ts = c.find("track_surface");
            if (!ts || ts->children.size() < 2) return false;
            std::string path = ts->textAt(1);
            size_t slash = path.rfind('/');
            path = path.substr(0, slash + 1) + "gen/" + path.substr(slash + 1) + "_ps2";
            if (!data.exists(path)) return false;
            Bytes b = data.read(path);
            sr_->replaceTexture("track_surface.tex", decodeBitmap(b));
            surface_ = character;
            return true;
        }
    } catch (const std::exception& ex) {
        SDL_Log("track surface %s: %s", character.c_str(), ex.what());
    }
    return false;
}

TrackRenderer::TrackRenderer(Renderer& renderer, const GameData& data)
    : renderer_(renderer),
      scene_(std::make_unique<Scene>(data.read("track/gen/track.rnd_ps2"))),
      sr_(std::make_unique<SceneRenderer>(renderer, *scene_)),
      anims_(std::make_unique<AnimPlayer>(std::vector<Scene*>{scene_.get()})) {
    // View instances (FUN_00164f70): shown, and their particle systems forced
    // to relative motion so they follow whichever lane draws them.
    for (const char* v : {"smash_normal.view", "smash_star.view", "smash_bonus.view", "smash_combo.view",
                          "smash_combo_lightning.view", "burn_tail.view"}) {
        SceneObject* o = scene_->find(v);
        if (!o || !o->view) continue;
        o->view->draw.showing = true;
        for (const std::string& d : o->view->draw.draws)
            if (SceneObject* p = scene_->find(d); p && p->particles) p->particles->relativeMotion = 1;
    }
    // burn_tail_state.view is frozen at its state frame (FUN_00166940), not run with the burn.
    anims_->unhook("burn_tail_state.view");
}

void TrackRenderer::hitGem(float nowMs, int gem, uint32_t frets, bool bonus, int spState, bool sustain) {
    bool star = spState == 2 || spState == 3;
    smashView_ = bonus ? "smash_bonus.view" : star ? "smash_star.view" : "smash_normal.view";
    smashMask_ = frets;
    smashT0_ = nowMs;
    burnGem_ = sustain ? gem : -1;
    if (spState == 3) comboMask_ = frets, comboT0_ = nowMs;
}

void TrackRenderer::stopHitEffects() {
    smashView_ = nullptr;
    smashMask_ = 0;
    burnGem_ = -1;
}

TrackRenderer::~TrackRenderer() = default;

void TrackRenderer::setExtendFrame(float frame) {
    anims_->setFrame("extend_track.tnm", frame);
    anims_->updateTransforms();
}

void TrackRenderer::popSmasher(int fret) {
    for (int f = 0; f < 5; ++f)
        if (fret < 0) popAt_[f] = -1e9f;  // pop_smasher -1 stops them all
        else if (f == fret) popAt_[f] = float(SDL_GetTicks());
}

void TrackRenderer::setSmasherGlowing(int fret, bool on) {
    for (int f = 0; f < 5; ++f)
        if (fret < 0 || f == fret) glow_[f] = on;
}

void TrackRenderer::reset() {
    for (int f = 0; f < 5; ++f) glow_[f] = false, popAt_[f] = hitAt_[f] = -1e9f;
    explodeAt_ = -1;
    stopHitEffects();
    comboMask_ = 0;
}

// FUN_001598c8: the track graphics go; track_explode.view plays from frame 0.
void TrackRenderer::explode() {
    explodeAt_ = float(SDL_GetTicks());
    if (SceneObject* v = scene_->find("track_explode.view"); v && v->view) v->view->draw.showing = true;
}

bool TrackRenderer::laneScreen(int fret, float along, float& mx, float& my) const {
    if (!drawn_ || fret < 0 || fret > 4) return false;
    // The smashers stand at gem_smasher.mesh's own place, offset along x by the lane.
    std::array<float, 3> base{0, 0, 0};
    if (const SceneObject* o = scene_->find("gem_smasher.mesh"); o && o->transformable()) base = o->transformable()->world.pos;
    const auto& m = lastVp_.m;
    const float x = base[0] + kLaneX[fret], y = base[1] + along, z = base[2];
    float cx = m[0] * x + m[4] * y + m[8] * z + m[12], cy = m[1] * x + m[5] * y + m[9] * z + m[13];
    float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
    if (cw <= 1e-6f) return false;
    mx = (cx / cw + 1) * 0.5f * kScreenW;
    my = (1 - cy / cw) * 0.5f * kScreenH;
    return true;
}

bool TrackRenderer::exploding() const { return explodeAt_ >= 0 && float(SDL_GetTicks()) - explodeAt_ < 4000; }

void TrackRenderer::draw(const Player& player, float nowMs, float trackSpeed, float slopMs, const Song& song,
                         const float* hitFlash) {
    Mat4 vp;
    if (!sr_->camera("track.cam", kScreenW / kScreenH, vp)) return;
    vp = screen_ * vp;  // face-off: this player's half of the screen
    lastVp_ = vp, drawn_ = true;
    const float wall = float(SDL_GetTicks());
    // Billboards and particles face the track camera (explosion included).
    if (const SceneObject* cam = scene_->find("track.cam"); cam && cam->cam) sr_->setCamera(cam->cam->trans.world);
    if (explodeAt_ >= 0) {
        // The pieces carry the track surface; ScaleOffset x2.5 on the view: ~1.33 s.
        anims_->setFrame("track_explode.view", (wall - explodeAt_) * 0.03f);
        anims_->updateTransforms();
        sr_->draw("track_explode.view", vp);
        return;
    }
    const float speed = trackSpeed * kUnitsPerMsPerSpeed;
    auto yAt = [&](float ms) { return (ms - nowMs) * speed; };
    using Opt = SceneRenderer::MeshOptions;
    const Mat4 identity;

    // FUN_00158838 / FUN_00159590: track_fade.tex, 128 x 32 PS2 pixels with its
    // top-left at (256 - 64, 216) of the 512 x 448 frame (track_graphics fade
    // normal), taken before the highway is drawn. In NDC of the 4:3 track
    // framing in our 16:9 screen: x +-0.1875, y +0.0357 .. -0.1071, then this
    // player's placement (screen_).
    {
        auto ndc = [&](float x, float y) {
            float sx = screen_.at(0, 0) * x + screen_.at(0, 3), sy = screen_.at(1, 1) * y + screen_.at(1, 3);
            return std::array<float, 2>{sx, sy};
        };
        auto a = ndc(-0.1875f, 0.035714f), b = ndc(0.1875f, -0.107143f);
        renderer_.grabNdc(player_ < 0 ? 0 : player_, a[0], a[1], b[0], b[1]);
    }

    // Surface (scrolling), rails, lane lines.
    Opt surface;
    // The surface's V runs from 1 at the strike line to -0.3 at the horizon,
    // so the texture scrolls toward the player as V decreases.
    surface.uvOffset[1] = -nowMs * speed * kSurfaceVPerUnit;
    surface.repeatTexture = true;
    sr_->drawMesh("track_surface.mesh", vp, identity, surface);
    // FUN_001657d8: the rails pulse red in the warning zone and blue/white
    // when Star Power is ready (side_rails_*.mnm on track_side_rails.mat).
    {
        bool ready = player.starMeter() >= 0.5f && !player.starPowerActive();
        bool warning = player.rockMeter() < player.warningLevel();
        static const char* kRails[] = {"side_rails_none.mnm", "side_rails_warning.mnm", "side_rails_star.mnm",
                                       "side_rails_warning_star.mnm"};
        anims_->setFrame(kRails[(ready ? 2 : 0) + (warning ? 1 : 0)], std::fmod(nowMs * 0.03f, 15.0f));
    }
    sr_->drawMesh("track_side_rails.mesh", vp, identity, {});
    sr_->drawMesh("track_lane_lines.mesh", vp, identity, {});

    // Beat lines.
    Opt local;
    local.useWorld = false;
    for (const BeatLine& b : song.beats) {
        float y = yAt(b.ms);
        if (y < kRemoveY || y > kHorizonY) continue;
        sr_->drawMesh("bar_line.mesh", vp, translate(0, y, 0), local);
    }

    // Notes, far to near.
    const auto& gems = player.chart().gems;
    const float lookBehindMs = -kRemoveY / speed, lookAheadMs = kHorizonY / speed;
    auto first = std::lower_bound(gems.begin(), gems.end(), nowMs - lookBehindMs - 30000.0f,
                                  [](const Gem& g, float v) { return g.ms < v; });
    std::vector<int> visible;
    for (auto it = first; it != gems.end() && it->ms <= nowMs + lookAheadMs; ++it) {
        if (player_ >= 0 && !it->ownedBy(player_)) continue;  // FUN_0015d840: the opponent's gems draw nothing
        float endMs = it->ms + (it->noSustain ? 0 : it->durationMs);
        if (endMs >= nowMs - lookBehindMs) visible.push_back(int(it - gems.begin()));
    }

    // Star Power: gem_bonus.mnm / gem_bonus2.mnm spin, driven by track_anims.view at now * 0.045.
    const bool bonus = player.starPowerActive();
    anims_->setFrame("track_anims.view", nowMs * 0.045f);
    sr_->resetDynamic();
    // Sustain whammy (FUN_001633d8): while a sustain plays, every Poll (60 Hz)
    // pushes h = 3 * bar (0..3) into a 100-sample history; a tail row at
    // distance y uses the sample floor(y / 2) polls ago and widens by 1 + h.
    // When the sustain stops the shape freezes; a new tail starts zeroed.
    if (player.sustainGem() >= 0) {
        if (player.sustainGem() != whammyGem_) {
            whammy_.assign(100, 0.0f);
            whammyGem_ = player.sustainGem();
            whammyClockMs_ = nowMs;
        }
        float h = 3.0f * std::clamp(player.whammyValue(), 0.0f, 1.0f);
        for (int n = 0; nowMs - whammyClockMs_ >= 1000.0f / 60.0f && n < 100; ++n) {
            whammyClockMs_ += 1000.0f / 60.0f;
            whammy_.push_front(h);
            whammy_.pop_back();
        }
        if (nowMs - whammyClockMs_ >= 1000.0f / 60.0f) whammyClockMs_ = nowMs;
    }
    // Star tail whammy ramp g (FUN_001609b0): +-0.1 a frame toward "whammy active"
    // on a held star-phrase sustain (not during Star Power).
    {
        int sg = player.sustainGem();
        bool starWhammy = sg >= 0 && !bonus && gems[size_t(sg)].phrase >= 0 && player.phraseIntact(gems[size_t(sg)].phrase) &&
                          player.whammying();
        // The burn's whammy state (player+0xc0) is this star whammy: a plain
        // sustain whammied keeps the orange burn; during Star Power it whammies blue.
        burnWhammy_ = starWhammy || (bonus && player.whammying());
        float step = std::max(0.0f, nowMs - lastWhammyMs_) * 0.006f;  // 0.1 per 60 Hz poll
        lastWhammyMs_ = nowMs;
        whammyRamp_ = starWhammy ? std::min(1.0f, whammyRamp_ + step) : std::max(0.0f, whammyRamp_ - step);
    }

    // Star gems spin (FUN_001609b0): star.tnm at now * 0.03, MinMaxLoop 0..100 (3.33 s a turn).
    anims_->setFrame("star.tnm", nowMs * 0.03f);
    anims_->updateTransforms();
    for (auto vi = visible.rbegin(); vi != visible.rend(); ++vi) {
        int i = *vi;
        const Gem& g = gems[i];
        bool hit = player.gemHit(i);
        bool sustaining = player.sustainGem() == i;
        bool missed = !hit && g.ms < nowMs - slopMs;
        // Star look (FUN_0015ff98): every gem of a phrase; when the phrase breaks
        // (on_miss_phrase, FUN_0015e778) only its not-yet-played gems revert, so the
        // gem that broke it keeps the star look (with star_miss).
        // HOPO look (FUN_0015d840, slti s2,2 at 0x15d960): a HOPO-able gem from the
        // third on whose previous gem is this track's own; the first two never show it.
        const bool hopoLook = g.hopo && i >= 2 && (player_ < 0 || gems[size_t(i - 1)].ownedBy(player_));
        bool star = g.phrase >= 0 && player.phraseIntact(g.phrase);
        if (g.phrase >= 0 && !star) {
            star = true;
            for (int j = i - 1; j >= 0 && gems[size_t(j)].phrase == g.phrase; --j)
                if (!player.gemHit(j) && gems[size_t(j)].ms < nowMs - slopMs) star = false;
            if (!missed) star = false;
        }
        float y = yAt(g.ms);
        for (int f = 0; f < 5; ++f) {
            if (!(g.frets & (1u << f))) continue;
            std::string c = kColors[f];

            // Tails: coloured (also for star phrases), bonus during Star Power, and grey
            // once missed or released early (the rest stays on the track).
            if (!g.noSustain) {
                float endY = yAt(g.ms + g.durationMs);
                float y0 = std::max(sustaining ? 1.5f : y, kRemoveY);
                float y1 = std::min(endY, kHorizonY);
                bool released = hit && !sustaining;
                if (released) y0 = std::max(y, 0.0f);
                if (y1 > y0 && !(released && endY <= 0)) {
                    Opt tail;
                    tail.useWorld = false;
                    tail.material = missed || released ? "tail_miss.mat" : bonus ? "tail_bonus.mat" : "tail_" + c + ".mat";
                    // The tail builder (FUN_001633d8) writes the mat colour into the vertex
                    // colours, which these vert_amb mats then use; a whammied star
                    // sustain lerps it toward tail_star's blue by the ramp g.
                    float col[4] = {1, 1, 1, 1};
                    if (const SceneObject* tm = scene_->find(tail.material); tm && tm->mat)
                        std::copy_n(tm->mat->color.data(), 4, col);
                    bool starSustain = sustaining && star && !bonus;
                    if (starSustain && whammyRamp_ > 0)
                        if (const SceneObject* sm = scene_->find("tail_star.mat"); sm && sm->mat)
                            for (int k = 0; k < 3; ++k) col[k] += (sm->mat->color[size_t(k)] - col[k]) * whammyRamp_;
                    std::copy_n(col, 4, tail.tint);
                    float maxS = 1;
                    tail.geometry = tailStrip(kLaneX[f], y0, y1, sustaining, maxS);
                    sr_->drawMesh("tail02.mesh", vp, Mat4{}, tail);
                    // The glow line (RndLine at z 0.16, FUN_001609b0): a tight cyan core on
                    // unplayed star tails; while sustaining, 1.5 w wide in the fret / star /
                    // bonus glow colour, w easing 0.2 a poll toward (max s - 1) / 2 + 1.
                    if (sustaining) {
                        float target = (maxS - 1) * 0.5f + 1, k = std::min(1.0f, std::max(0.0f, nowMs - lineClockMs_) * 0.012f);
                        lineW_ += (target - lineW_) * k;
                        lineClockMs_ = nowMs;
                    }
                    if (!missed && !released && (sustaining || (star && !hit))) {
                        std::string lineMat = !sustaining ? "tail_glow_tight.mat"
                                            : bonus       ? "tail_glow_bonus.mat"
                                            : star        ? "tail_glow_star.mat"
                                                          : "tail_glow_" + c + ".mat";
                        float w = sustaining ? 1.5f * lineW_ : 0.7f;
                        float lc[4];
                        const float* lineCol = nullptr;
                        const SceneObject* lm = scene_->find(lineMat);
                        const SceneObject* wm = scene_->find("tail_glow_whammy.mat");
                        if (starSustain && whammyRamp_ > 0 && lm && lm->mat && wm && wm->mat) {
                            for (int k = 0; k < 4; ++k)
                                lc[k] = lm->mat->color[size_t(k)] + (wm->mat->color[size_t(k)] - lm->mat->color[size_t(k)]) * whammyRamp_;
                            lineCol = lc;
                        }
                        sr_->drawLine(lineMat, vp, {kLaneX[f], y0, 0.16f}, {kLaneX[f], y1, 0.16f}, w, lineCol);
                    }
                }
            }
            if (hit || y < kRemoveY || y > kHorizonY) continue;

            // Gems (FUN_001602a8): the body by state, the top by kind; a missed gem keeps its top.
            Mat4 at = translate(kLaneX[f], y, 0);
            Mat4 starAt = translate(kLaneX[f] - kStarOffsetX, y - kStarOffsetY, 0);
            // Layers (docs/render_gems_dof.md §2.1): top, body, then the spec slot,
            // an additive texgen-5 env-map highlight (gem2; gem_bonus2's moving streak
            // in Star Power) that stays on a missed gem.
            if (bonus && !missed) {
                sr_->drawMesh(hopoLook ? "top_blue.mesh" : "top_black.mesh", vp, at, {});
                sr_->drawMesh("gem_bonus.mesh", vp, at, {});
                sr_->drawMesh("gem_bonus2.mesh", vp, at, {});
            } else if (star) {
                // Layer order: top, star_base, star_<c> (stargem.tex atlas cell), star2
                // highlight; star_<c> and star2 are star_base's children and spin with it.
                sr_->drawMesh(hopoLook ? "top_star_" + c + ".mesh" : "top_star_black.mesh", vp, starAt, {});
                sr_->drawMesh("star_base.mesh", vp, starAt, {});
                sr_->drawMesh(missed ? "star_miss.mesh" : "star_" + c + ".mesh", vp, starAt, {});
                sr_->drawMesh("star2.mesh", vp, starAt, {});
            } else {
                sr_->drawMesh(hopoLook ? "top_" + c + ".mesh" : "top_black.mesh", vp, at, {});
                sr_->drawMesh(missed ? "gem_miss.mesh" : "gem_" + c + ".mesh", vp, at, {});
                sr_->drawMesh(bonus ? "gem_bonus2.mesh" : "gem2.mesh", vp, at, {});
            }
        }
    }

    // Star Power deploy (FUN_0015ec28): gem_bonus_spark1/2 on every visible
    // unplayed gem, gem_bonus_spark1.tnm / .mnm and spark2.mnm over ~18 frames.
    if (bonus) {
        if (bonusStartMs_ < 0) bonusStartMs_ = nowMs;
        float sf = (nowMs - bonusStartMs_) * 0.03f;
        if (sf < 20) {
            for (const char* a : {"gem_bonus_spark1.tnm", "gem_bonus_spark1.mnm", "gem_bonus_spark2.mnm"}) anims_->setFrame(a, sf);
            anims_->updateTransforms();
            for (int i : visible) {
                const Gem& g = gems[size_t(i)];
                float y = yAt(g.ms);
                if (player.gemHit(i) || y < 0 || y > kHorizonY) continue;
                for (int f = 0; f < 5; ++f)
                    if (g.frets & (1u << f)) {
                        sr_->drawMesh("gem_bonus_spark2.mesh", vp, translate(kLaneX[f], y, 0), {});
                        sr_->drawMesh("gem_bonus_spark1.mesh", vp, translate(kLaneX[f], y, 0), {});
                    }
            }
        }
    }
    // Star sustain whammy (FUN_001609b0): tail_bonus.part emits along the held
    // star tail at 2g its rate (g: the ramp above).
    {
        int sg = player.sustainGem();
        if (SceneObject* tp = scene_->find("tail_bonus.part"); tp && tp->particles) {
            ParticleSysObj& p = *tp->particles;
            if (!tailPartBase_) tailPartBase_ = true, tailEmitBase_ = p.emitRate, p.relativeMotion = 1, p.draw.showing = true;
            p.emitRate = {tailEmitBase_[0] * 2 * whammyRamp_, tailEmitBase_[1] * 2 * whammyRamp_};
            if (sg >= 0) {
                const Gem& g = gems[size_t(sg)];
                // Emit only along what is left of the tail (past its end they die).
                p.posHigh[1] = std::clamp(yAt(g.ms + g.durationMs), 0.0f, 100.0f);
                tailFrets_ = g.frets;
            }
            anims_->setFrame("tail_bonus.part", nowMs * 0.03f);
            if (!p.live.empty())
                for (int f = 0; f < 5; ++f)
                    if (tailFrets_ & (1u << f)) sr_->draw("tail_bonus.part", vp, translate(kLaneX[f], 0, 0));
        }
    }

    // FUN_00159590: the venue strip saved before the highway goes back over
    // its far end, alpha 1 at the top to 0 at the bottom (track_fade.mat),
    // then track_add.mat (+0.02). Everything above fades in; the smashers,
    // hit effects and lightning below draw over it.
    renderer_.drawGrab(player_ < 0 ? 0 : player_, 0.02f);

    // Fret buttons ("smashers"): a pop (gem_smasher.tnm, intro and misses)
    // or hit (gem_smasher_hit.tnm) jump at 30 fps; lit while held or by the script.
    // A miss with a fret (FUN_0015e2f8 -> FUN_00164ae8) pops that smasher with the
    // big gem_smasher.tnm jump (z 0.71).
    if (player.missCount() != missesSeen_) {
        missesSeen_ = player.missCount();
        if (int mf = player.lastMissFret(); mf >= 0 && mf < 5) popAt_[mf] = wall;
    }
    for (int f = 0; f < 5; ++f) {
        if (hitFlash && hitFlash[f] > lastFlash_[f] + 0.01f) hitAt_[f] = wall;
        lastFlash_[f] = hitFlash ? hitFlash[f] : 0;
    }
    for (int f = 0; f < 5; ++f) {
        bool held = (player.heldFrets() & (1u << f)) || glow_[f];
        float popFrame = (wall - popAt_[f]) * 0.03f, hitFrame = (wall - hitAt_[f]) * 0.03f;
        if (popFrame < 6) anims_->setFrame("gem_smasher.tnm", popFrame);
        else if (hitFrame < 6) anims_->setFrame("gem_smasher_hit.tnm", hitFrame);
        else anims_->setFrame("gem_smasher.tnm", 5);
        anims_->updateTransforms();
        Mat4 at = translate(kLaneX[f], 0, 0);
        sr_->drawMesh("smasher shadow.mesh", vp, at, {});
        Opt smasher;
        smasher.material = held ? std::string("gem_smasher_") + kColors[f] + ".mat" : "gem_smasher.mat";
        sr_->drawMesh("gem_smasher.mesh", vp, at, smasher);
        Opt ring;
        ring.material = std::string("now_ring_") + kColors[f] + ".mat";  // GemSmasher ctor: always the slot's ring
        sr_->drawMesh("smasher_rim.mesh", vp, at, ring);
    }

    // Hit effects (FUN_00164870 poll): the lane proxies run their source at
    // 0.03 * (now - hit) frames and draw it at the lane (docs/hit_fx.md §2).
    // The burning sustain ended or was released: everything stops (FUN_00164c50).
    if (burnGem_ >= 0 && player.sustainGem() != burnGem_) stopHitEffects();
    auto lanes = [&](const char* view, uint32_t mask, float t0, bool fan) {
        anims_->setFrame(view, (nowMs - t0) * 0.03f);
        anims_->updateTransforms();
        for (int f = 0; f < 5; ++f) {
            if (!(mask & (1u << f))) continue;
            Mat4 at = translate(kLaneX[f], 0, 0);
            if (fan) {  // set [10]: rotated about the track's Y by (lane - 2) * 12 degrees (single player)
                float a = float(f - 2) * 12.0f * 3.14159265f / 180.0f;
                Xfm r;
                r.rot = {std::cos(a), 0, -std::sin(a), 0, 1, 0, std::sin(a), 0, std::cos(a)};
                at = at * Mat4::fromRowXfm(r.rot, r.pos);
            }
            sr_->draw(view, vp, at);
        }
    };
    if (smashView_) {
        if (burnGem_ >= 0) {
            // burn_tail_state: normal 0 / whammy 10 / bonus 20 (whammy wins).
            anims_->setFrame("burn_tail_state.view", burnWhammy_ ? 10.0f : bonus ? 20.0f : 0.0f);
            lanes("burn_tail.view", smashMask_, smashT0_, false);
        }
        lanes(smashView_, smashMask_, smashT0_, false);
    }
    if (comboMask_ && nowMs - comboT0_ < 3000) {
        lanes("smash_combo.view", comboMask_, comboT0_, false);
        lanes("smash_combo_lightning.view", comboMask_, comboT0_, true);
    }

    // set_bonus_gems: the lightning flipbook runs from the deploy time.
    if (player.starPowerActive()) {
        if (bonusStartMs_ < 0) bonusStartMs_ = nowMs;
        anims_->setFrame("lightning_track.view", (nowMs - bonusStartMs_) * 0.03f);
        if (SceneObject* v = scene_->find("lightning_track.view"); v && v->view) v->view->draw.showing = true;
        sr_->draw("lightning_track.view", vp);
    } else {
        bonusStartMs_ = -1;
    }
}
