#include "game/guitar_walk.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

#include "game/chart.h"
#include "scene/character.h"

namespace {

constexpr float kPi = 3.14159265f;
// charbase.dta / CharWalk defaults.
constexpr float kBeatsPerInch = 0.069f, kEndBeats = 3, kMaxWait = 6;

using V3 = std::array<float, 3>;

float wrap(float a) {
    a = std::fmod(a + kPi, 2 * kPi);
    return a < 0 ? a + kPi : a - kPi;
}
float dist(const V3& a, const V3& b) { return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]); }
float len2(const V3& a) { return std::hypot(a[0], a[1]); }

// FUN_001883d8: integrates `clip`'s bone_facing motion from f0 to f1 (f1 < f0
// runs backward) onto a world pose: pos += rotZ(yaw) * delta, yaw += dTheta.
void integrate(const Character& c, int clip, float f0, float f1, V3& pos, float& yaw) {
    if (f0 == f1) return;
    V3 d;
    float dy;
    c.rootDelta(clip, f0, f1, d, dy);
    float cs = std::cos(yaw), sn = std::sin(yaw);
    pos[0] += d[0] * cs - d[1] * sn;
    pos[1] += d[0] * sn + d[1] * cs;
    pos[2] += d[2];
    yaw += dy;
}
// The length of a clip's motion from f0 to f1.
float travel(const Character& c, int clip, float f0, float f1) {
    V3 d;
    float dy;
    c.rootDelta(clip, f0, f1, d, dy);
    return len2(d);
}
// The game's heading of a direction: 0 along +Y, growing counter-clockwise.
float heading(float dx, float dy) { return -std::atan2(dx, dy); }

// FUN_00196588: the angle in a clip name after `key`: forward 0, backward
// 180, left_N +N, right_N -N degrees.
bool nameAngle(const std::string& name, const std::string& key, float& out) {
    size_t at = name.find(key);
    if (at == std::string::npos) return false;
    std::string rest = name.substr(at + key.size());
    if (rest.rfind("forward", 0) == 0) out = 0;
    else if (rest.rfind("backward", 0) == 0) out = kPi;
    else if (rest.rfind("left_", 0) == 0) out = float(std::atoi(rest.c_str() + 5)) * kPi / 180;
    else if (rest.rfind("right_", 0) == 0) out = -float(std::atoi(rest.c_str() + 6)) * kPi / 180;
    else if (rest.rfind("left", 0) == 0) out = kPi / 2;
    else if (rest.rfind("right", 0) == 0) out = -kPi / 2;
    else return false;
    return true;
}

// Turns every row of `x` counter-clockwise about world Z.
void rotateZ(Xfm& x, float a) {
    float c = std::cos(a), s = std::sin(a);
    for (int r = 0; r < 3; ++r) {
        float& vx = x.rot[size_t(r * 3)];
        float& vy = x.rot[size_t(r * 3 + 1)];
        float nx = vx * c - vy * s, ny = vx * s + vy * c;
        vx = nx, vy = ny;
    }
}

}  // namespace

GuitarWalk::GuitarWalk(std::vector<Xfm> spots, const Config& config, const std::vector<SongEvent>& events)
    : spots_(std::move(spots)), config_(config) {
    if (spots_.size() < 2) config_.allow = false;
    hub_ = std::max(0, int(spots_.size()) - 1);
    if (config_.soloPoint >= hub_) config_.soloPoint = -1;
    if (SDL_getenv("GH1RE_LOG_SHOTS"))
        for (size_t i = 0; i < spots_.size(); ++i)
            SDL_Log("walk: spot %d at %.0f %.0f %.0f facing %.0f deg%s", int(i), spots_[i].pos[0], spots_[i].pos[1], spots_[i].pos[2],
                    yawOf(spots_[i]) * 180 / kPi, int(i) == hub_ ? " (hub)" : "");
    for (float& r : rolled_) r = 0;
    // [I] The hold starts on the start spot, so the stand clips' small
    // bone_facing deltas are absorbed before the first walk too (and in
    // venues without walks, where the guitarist never leaves it).
    if (!spots_.empty()) hold_ = {true, true, spots_[size_t(start_)].pos, yawOf(spots_[size_t(start_)])};
    if (!config_.allow) return;

    // FUN_00281fb8: block random walks before solos and the song's end; in
    // venues with a solo point, walk there before the solo.
    auto leg = [&](int i) { return dist(spots_[size_t(i)].pos, spots_[size_t(hub_)].pos); };
    float longest = 0;
    for (int i = 0; i < hub_; ++i)
        for (int j = 0; j < hub_; ++j)
            if (i != j) longest = std::max(longest, leg(i) + leg(j));
    const float wMax = longest * kBeatsPerInch + kEndBeats;
    float wSolo = 0;
    if (config_.soloPoint >= 0) {
        float far = 0;
        for (int i = 0; i < hub_; ++i)
            if (i != config_.soloPoint) far = std::max(far, leg(i));
        wSolo = (leg(config_.soloPoint) + far) * kBeatsPerInch + kEndBeats + kMaxWait;
    }
    float lastTick = 0;
    for (const SongEvent& e : events) {
        lastTick = std::max(lastTick, float(e.tick));
        if (e.text != "solo_on") continue;
        if (config_.soloPoint < 0) preSolo2_.push_back(float(e.tick) - wMax * 480);
        else {
            preSolo1_.push_back(float(e.tick) - (wSolo + wMax) * 480);
            preSolo2_.push_back(float(e.tick) - wSolo * 480);
        }
    }
    preSolo2_.push_back(lastTick - wMax * 480);
    std::sort(preSolo1_.begin(), preSolo1_.end());
    std::sort(preSolo2_.begin(), preSolo2_.end());
    // FUN_00281cd8: a solo in the first 8 beats starts the guitarist on the solo point.
    if (config_.soloPoint >= 0 && !preSolo2_.empty() && preSolo2_.front() < 3840) start_ = config_.soloPoint;
    hold_ = {true, true, spots_[size_t(start_)].pos, yawOf(spots_[size_t(start_)])};  // the solo point may have moved the start
}

float GuitarWalk::yawOf(const Xfm& x) const { return heading(x.rot[3], x.rot[4]); }

int GuitarWalk::nearestSpot(const V3& pos, bool withHub) const {
    int best = 0;
    float bestD = 1e30f;
    for (int i = 0; i < int(spots_.size()); ++i) {
        if (!withHub && i == hub_) continue;
        float d = dist(pos, spots_[size_t(i)].pos);
        if (d < bestD) bestD = d, best = i;
    }
    return best;
}

bool GuitarWalk::walking(const Character& c) const {
    if (state_ < 2 || chain_.empty()) return false;
    int i = std::min(k_ - 2, int(chain_.size()) - 1);
    return i >= 0 && c.currentClip() == chain_[size_t(i)].clip;
}

void GuitarWalk::roll(float now) {
    lastReset_ = now;
    for (size_t i = 0; i < rolled_.size(); ++i) {
        const auto& d = config_.delays[i];
        rolled_[i] = d[0] < 0 ? 0 : std::uniform_real_distribution<float>(d[0], std::max(d[0], d[1]))(rng_);
    }
}

bool GuitarWalk::update(Character& c, float beat, float ms, float ticks, int state, int excitement,
                        const std::vector<std::string>& events) {
    (void)beat;
    now_ = ms * 0.001f;
    if (lastReset_ == 0 && rolled_[2] == 0) roll(now_);

    // PRE_SOLO1 / PRE_SOLO2 (song-setup events).
    for (; nextPre1_ < preSolo1_.size() && preSolo1_[nextPre1_] <= ticks; ++nextPre1_)
        if (++pre1_ == 1 && state_ == 2 && !walking(c)) cancel(c);
    for (; nextPre2_ < preSolo2_.size() && preSolo2_[nextPre2_] <= ticks; ++nextPre2_)
        if (++pre2_ == 1) {
            if (state_ == 2 && !walking(c)) cancel(c);
            if (config_.soloPoint >= 0) startWalkTo(c, config_.soloPoint, excitement, Character::kAnimFirst);
        }
    for (const std::string& e : events)
        if (e == "solo_off") {
            pre1_ = std::max(0, pre1_ - 1);
            pre2_ = std::max(0, pre2_ - 1);
            noWalkUntil_ = ticks + 1920;
        }

    if (state == 4) {
        cancel(c);
        gameOver_ = true;  // the win / lose clips move as they were made to
        return false;
    }
    // GuitarCharacter_v13: a random walk once the rolled delay for this
    // excitement has passed (only while no clip change is in progress).
    if (!c.blending() && (state_ == 0 || state_ == 3)) {
        int e = std::clamp(excitement, 0, 4);
        // Development aid: GH1RE_FORCE_WALKS=1 walks about every 6 s whatever the excitement.
        static const bool force = SDL_getenv("GH1RE_FORCE_WALKS") != nullptr;
        if (force) e = 3, excitement = 3, rolled_[3] = std::min(rolled_[3], 6.0f);
        int n = int(spots_.size());
        if (config_.allow && n > 2 && state == 1 && pre2_ < 1 && config_.delays[size_t(e)][0] >= 0 && noWalkUntil_ < ticks &&
            rolled_[size_t(e)] < now_ - lastReset_) {
            int cur = nearestSpot(c.placement().pos);
            // FUN_0024a1d0(1, n - 1): [1, n - 1), the upper bound excluded.
            int r = 1 + int(rng_() % uint32_t(n - 2));
            int dest = ((cur + r) % (n - 1) + (n - 1)) % (n - 1);
            if (config_.soloPoint >= 0 && pre1_ > 0) dest = config_.soloPoint;
            if (!startWalkTo(c, dest, excitement, Character::kAnimLongest)) roll(now_);
        }
    }
    return state_ == 1 || state_ == 2;
}

// StartWalkTo (FUN_002848c0) and CharWalk::Start (FUN_00196c88).
bool GuitarWalk::startWalkTo(Character& c, int dest, int excitement, int mode) {
    const Xfm& root = c.placement();
    if (!config_.allow || (state_ != 0 && state_ != 3) || dest == nearestSpot(root.pos)) return false;
    const Xfm& D = spots_[size_t(dest)];
    const V3& hub = spots_[size_t(hub_)].pos;
    V3 d{hub[0] - root.pos[0], hub[1] - root.pos[1], hub[2] - root.pos[2]};
    float x = d[0] * D.rot[0] + d[1] * D.rot[1] + d[2] * D.rot[2];
    float y = d[0] * D.rot[3] + d[1] * D.rot[4] + d[2] * D.rot[5];
    using C = Character;
    uint32_t turnF, walkF, stopF;
    if (std::fabs(y) < std::fabs(2 * x)) {
        uint32_t side = x > 0 ? C::kWalkRight : C::kWalkLeft;
        turnF = side | C::kWalkTurn;
        walkF = std::uniform_real_distribution<float>(0, 1)(rng_) < 0.25f ? C::kWalkForward | C::kWalkWalk : side | C::kWalkWalk;
        stopF = C::kWalkStop;
    } else if (y > 0) {
        turnF = C::kWalkTurn, walkF = C::kWalkForward | C::kWalkWalk, stopF = C::kWalkForward | C::kWalkStop;
    } else {
        turnF = C::kWalkTurn | C::kWalkBackward, walkF = C::kWalkBackward | C::kWalkWalk, stopF = C::kWalkBackward | C::kWalkStop;
    }
    walkF |= excitement < 3 ? C::kWalkNormal : C::kWalkExtreme;

    if (SDL_getenv("GH1RE_LOG_SHOTS")) {
        int s = nearestSpot(root.pos, false);
        SDL_Log("walk: starting %.1f units from spot %d, facing off by %.1f deg", dist(root.pos, spots_[size_t(s)].pos), s,
                wrap(yawOf(spots_[size_t(s)]) - yawOf(root)) * 180 / kPi);
    }
    std::vector<V3> path = {root.pos, hub, D.pos};
    const float destYaw = yawOf(D);
    const C::DriverInfo cur = c.newestStarted();
    if (cur.clip < 0) return false;
    std::vector<WalkClip> chain;
    // Turn (FUN_00197c50): the one whose end facing is closest (< 45 deg) to the
    // heading toward the hub, from the pose predicted at its transition.
    {
        int best = -1;
        float bestErr = kPi / 4;
        C::Transition bestT;
        V3 bestP{};
        for (int k : c.clipsWith(turnF, false)) {
            float a;
            if (!nameAngle(c.clipNameOf(k), "turn_", a)) continue;
            C::Transition t = c.transition(cur.clip, cur.frame, k, mode);
            if (!t.found) continue;
            V3 P = root.pos;
            float th = yawOf(root);
            if (!(hold_.hasPos && hold_.hasYaw)) integrate(c, cur.clip, cur.frame, t.src, P, th);
            if (hold_.hasPos) P = hold_.pos;
            if (hold_.hasYaw) th = hold_.yaw;
            float h = heading(path[1][0] - P[0], path[1][1] - P[1]);
            float err = std::fabs(wrap(wrap(th + a) - h));
            if (err < bestErr) bestErr = err, best = k, bestT = t, bestP = P;
        }
        if (best < 0) {
            if (SDL_getenv("GH1RE_LOG_SHOTS")) SDL_Log("walk: no turn to spot %d (flags %x)", dest, turnF);
            return false;
        }
        chain.push_back({best, bestT.dest, bestT.src});
        path[0] = bestP;
    }
    // Walk (FUN_001981c0): the least recently used of the song's tempo with a graph edge from the turn.
    {
        int best = -1;
        C::Transition bestT;
        for (int k : c.clipsWith(walkF, true)) {
            C::Transition t = c.explicitEdge(chain[0].clip, k);
            if (!t.found) continue;
            if (best < 0 || c.clipUsed(k) < c.clipUsed(best)) best = k, bestT = t;
        }
        if (best < 0) {
            // [I] No .acg edge: the computed transition at the turn's end.
            for (int k : c.clipsWith(walkF, true))
                if (best < 0 || c.clipUsed(k) < c.clipUsed(best)) best = k;
            if (best >= 0) bestT = c.transition(chain[0].clip, chain[0].start, best, C::kAnimLongest);
        }
        if (best < 0) {
            if (SDL_getenv("GH1RE_LOG_SHOTS")) SDL_Log("walk: no walk clip to spot %d (flags %x)", dest, walkF);
            return false;
        }
        c.touchClip(best);
        chain.push_back({best, bestT.dest, bestT.src});
    }
    // Stop (FUN_00198570): the one ending closest to the spot's facing. [I]
    // The original compares destYaw - heading with the angle in the clip's
    // name, but the shipped stop clips' bone_facing turns do not follow their
    // names (stop_left_90 turns right; measured from the entry frame), so the
    // turn each stop actually makes from its entry frame is used, from the
    // body's heading during the walk (travel heading minus the walk clip's own
    // direction: sideways and backward walks).
    {
        const V3& a = path[path.size() - 2];
        const V3& b = path.back();
        V3 wd;
        float wr;
        c.rootDelta(chain[1].clip, c.clipStart(chain[1].clip), c.clipEnd(chain[1].clip), wd, wr);
        float body = heading(b[0] - a[0], b[1] - a[1]) - (len2(wd) > 1e-3f ? heading(wd[0], wd[1]) : 0.0f);
        const float need = wrap(destYaw - body);
        int best = -1;
        float bestErr = 1e9f;
        for (int k : c.clipsWith(stopF, false)) {
            if (c.clipNameOf(k).find("stop_") == std::string::npos) continue;
            V3 sd;
            float turn;
            c.rootDelta(k, std::floor(c.clipEnd(k) - 2.2f - c.clipBlendWidth(k) * 0.5f), c.clipEnd(k), sd, turn);
            float err = std::fabs(wrap(need - turn));
            if (err < bestErr) bestErr = err, best = k;
        }
        if (best < 0) {
            if (SDL_getenv("GH1RE_LOG_SHOTS")) SDL_Log("walk: no stop to spot %d (flags %x)", dest, stopF);
            return false;
        }
        chain.push_back({best, std::floor(c.clipEnd(best) - 2.2f - c.clipBlendWidth(best) * 0.5f), 0});
    }
    chain_ = std::move(chain);
    path_ = std::move(path);
    destYaw_ = destYaw;
    planClip_ = c.currentClip();
    state_ = 1;
    if (!layout(c)) return false;
    if (SDL_getenv("GH1RE_LOG_SHOTS")) {
        std::string names;
        for (const WalkClip& w : chain_) names += " " + c.clipNameOf(w.clip);
        SDL_Log("walk: to spot %d via%s (corr %.3f)", dest, names.c_str(), corr_);
    }
    return true;
}

void GuitarWalk::setSegment(int seg) {
    seg_ = seg;
    if (seg_ < 1 || seg_ >= int(path_.size())) return;
    V3 d{path_[size_t(seg_)][0] - path_[size_t(seg_ - 1)][0], path_[size_t(seg_)][1] - path_[size_t(seg_ - 1)][1], 0};
    float l = len2(d);
    dir_ = l > 0 ? V3{d[0] / l, d[1] / l, 0} : V3{0, 1, 0};
}

// Layout (FUN_001989c8): simulates the chain in 2-beat steps until the walked
// distance reaches the stop clip's start point, adds walk loops as needed,
// picks the step that ends closest (within the 18-unit undershoot), and turns
// the rest into a speed correction.
bool GuitarWalk::layout(Character& c) {
    constexpr float kOver = 0, kUnder = -18;
    const Character::DriverInfo cur = c.newestStarted();
    if (cur.clip < 0 || cur.clip != planClip_ || cur.frame > chain_[0].src || chain_.size() < 3) {
        cancel(c);
        return false;
    }
    for (const WalkClip& w : chain_)
        if (!c.clipLoaded(w.clip)) {
            cancel(c);
            return false;
        }
    const int walk = chain_[1].clip;
    Character::Transition loop = c.transition(walk, chain_[1].start, walk, Character::kAnimLongest);
    const float loopSrc = loop.src, loopDest = loop.dest;
    setSegment(1);
    hold_ = {};
    const size_t n = path_.size();
    float L = 0;
    for (size_t j = 0; j + 2 < n; ++j) L += len2({path_[j + 1][0] - path_[j][0], path_[j + 1][1] - path_[j][1], 0});
    V3 S = path_[n - 1];
    float sy = destYaw_;
    const int stop = chain_.back().clip;
    integrate(c, stop, c.clipEnd(stop), chain_.back().start, S, sy);
    L += len2({path_[n - 2][0] - S[0], path_[n - 2][1] - S[1], 0});

    auto parity = [](float v) { return int(std::lround(v)) & 1; };
    float f0 = chain_[0].start, t = std::ceil(f0);
    if (parity(t) != parity(chain_.back().start)) t += 1;
    size_t i = 0;
    float next = chain_[1].src, rem = L, prevRem = L, stepStart = f0, end = t;
    size_t stepClip = 0;
    for (int guard = 0; guard < 400; ++guard) {
        prevRem = rem, stepClip = i, stepStart = f0, end = t;
        bool done = false;
        if (next <= end) {
            rem -= travel(c, chain_[i].clip, f0, next);
            if (rem <= -kUnder) break;
            ++i;
            if (i == chain_.size() - 1) chain_.insert(chain_.end() - 1, {walk, loopDest, loopSrc});
            float over = end - next;
            f0 = chain_[i].start;
            end = f0 + over;
            next = loopSrc;
            rem -= travel(c, chain_[i].clip, f0, end);
        } else {
            rem -= travel(c, chain_[i].clip, f0, end);
        }
        if (rem <= -kUnder) done = true;
        if (done) break;
        f0 = end;
        t = end + 2;
    }
    float err = -rem - kOver;
    if (-rem > -kUnder) {
        float errPrev = prevRem + kUnder;
        if (errPrev < err) i = stepClip, end = stepStart, rem = prevRem, err = errPrev;
    }
    while (chain_.size() > i + 2) chain_.erase(chain_.begin() + long(i) + 1);
    chain_.back().src = end;
    corr_ = 0;
    state_ = 2;
    k_ = 0;
    float residual = -rem;
    if (err > 0) {
        float total = 0;
        for (size_t j = 0; j < chain_.size(); ++j) {
            float exit = j + 1 < chain_.size() ? chain_[j + 1].src : c.clipEnd(chain_[j].clip);
            total += exit - chain_[j].start;
        }
        float signedErr = residual > 0 ? err : -err;
        if (total > 0) corr_ = -signedErr / total;
        residual -= signedErr;
    }
    // [I] The last point moves along the last segment by the residual.
    V3 seg{path_[n - 1][0] - path_[n - 2][0], path_[n - 1][1] - path_[n - 2][1], 0};
    if (float l = len2(seg); l > 0)
        for (int k = 0; k < 2; ++k) path_[n - 1][size_t(k)] += seg[size_t(k)] / l * residual;
    return true;
}

void GuitarWalk::poll(Character& c, float beat) {
    float dBeat = polled_ ? beat - lastBeat_ : 0;
    lastBeat_ = beat;
    polled_ = true;
    if (dBeat < 0 || dBeat > 1) dBeat = 0;
    // [I] Watchdog (port only): the longest walk takes ~20 beats; one still
    // running after 80 is stuck and is cancelled, so the standing guard takes over.
    busyBeats_ = state_ == 0 ? 0 : busyBeats_ + dBeat;
    if (busyBeats_ > 80 && !gameOver_) {
        SDL_Log("walk: stuck in state %d for %.0f beats, cancelled", state_, busyBeats_);
        cancel(c);
        busyBeats_ = 0;
    }
    switch (state_) {
    case 1:
        if (!layout(c)) break;
        [[fallthrough]];
    case 2: {
        const Character::DriverInfo cur = c.newestStarted();
        if (!c.pendingAfterNewest(nullptr, nullptr) || k_ == 0) {
            if (k_ < int(chain_.size())) {
                const WalkClip& e = chain_[size_t(k_)];
                c.queueClip(e.clip, e.start, e.src - cur.frame);
            } else {
                state_ = 3;
            }
            ++k_;
        }
        steer(c, dBeat);
        break;
    }
    case 3:
        if (c.currentClip() != chain_.back().clip) {  // a mood clip has taken over from the stop
            if (SDL_getenv("GH1RE_LOG_SHOTS") && !path_.empty()) {
                const V3& p = c.placement().pos;
                int s = nearestSpot(p, false);
                SDL_Log("walk: ended %.1f units from spot %d, facing off by %.1f deg", dist(p, spots_[size_t(s)].pos), s,
                        wrap(destYaw_ - yawOf(c.placement())) * 180 / kPi);
            }
            // [I] The original holds where the walk ended; the hold targets
            // the spot itself so walks never leave him off it (the shipped
            // stop clips end up to ~25 units and some degrees away).
            V3 spotPos = spots_[size_t(nearestSpot(c.placement().pos, false))].pos;
            cancel(c);
            hold_ = {true, true, spotPos, destYaw_};
        } else {
            steer(c, dBeat);
        }
        break;
    default: break;
    }
    // Development aid: GH1RE_FACELOG=1 logs the facing against the nearest spot every 2 beats.
    if (static const bool faceLog = SDL_getenv("GH1RE_FACELOG") != nullptr; faceLog && std::floor(beat / 2) != std::floor((beat - dBeat) / 2)) {
        const Xfm& root = c.placement();
        int s = nearestSpot(root.pos, false);
        float pel = 0;
        if (const SceneObject* p = c.find("bone_pelvis.mesh"); p && p->transformable()) pel = yawOf(p->transformable()->world);
        SDL_Log("FACE beat %.0f state %d clip %s dist %.1f root %.0f pelvis %.0f (spot %.0f)", beat, state_, c.clipName().c_str(),
                dist(root.pos, spots_[size_t(s)].pos), wrap(yawOf(root) - yawOf(spots_[size_t(s)])) * 180 / kPi,
                wrap(pel - yawOf(spots_[size_t(s)])) * 180 / kPi, yawOf(spots_[size_t(s)]) * 180 / kPi);
    }
    // ResetTimer (FUN_00284618) when a walk ends.
    if ((prevState_ == 1 || prevState_ == 2) && (state_ == 0 || state_ == 3)) roll(now_);
    prevState_ = state_;
    hold(c, dBeat);
    guard(c, dBeat);
}

// [I] The port's standing guard (not in the original): between walks the
// mood, Star Power and win / lose clips swing the root through their
// bone_facing deltas, and a clip cut short by the next one leaves part of its
// swing behind (a special move into a stand clip turned the guitarist 108
// degrees). Off the walk the root eases back to the hold (the spot it stands
// on) every frame, and may never be more than 60 degrees or 15 units away.
void GuitarWalk::guard(Character& c, float dBeat) {
    if (state_ != 0 || gameOver_ || !hold_.hasPos || !hold_.hasYaw) return;
    standGuard(c, hold_.pos, hold_.yaw, dBeat);
}

// [I] Off camera, the guitarist is put back exactly on his spot (the user's
// request: fix him up where nobody sees it). Standing only.
bool GuitarWalk::snapHome(Character& c) {
    if (state_ != 0 || gameOver_ || !hold_.hasPos || !hold_.hasYaw) return false;
    Xfm root = c.placement();
    float err = wrap(hold_.yaw - yawOf(root));
    if (std::fabs(err) < 2 * kPi / 180 && dist(root.pos, hold_.pos) < 1) return false;
    rotateZ(root, err);
    root.pos = hold_.pos;
    c.setPlacement(root);
    return true;
}

void standGuard(Character& c, const std::array<float, 3>& pos, float yaw, float dBeat) {
    if (dBeat <= 0) return;
    constexpr float kMaxYaw = 60 * kPi / 180, kMaxDist = 15;
    Xfm root = c.placement();
    float k = std::min(1.0f, dBeat * 0.6f);
    float err = wrap(yaw - heading(root.rot[3], root.rot[4]));
    float turn = err * k;
    float after = err - turn;
    if (std::fabs(after) > kMaxYaw) turn = err - std::copysign(kMaxYaw, after);
    rotateZ(root, turn);
    V3 d{root.pos[0] - pos[0], root.pos[1] - pos[1], root.pos[2] - pos[2]};
    for (int i = 0; i < 3; ++i) d[size_t(i)] *= 1 - k;
    float l = std::hypot(d[0], d[1], d[2]);
    if (l > kMaxDist)
        for (float& x : d) x *= kMaxDist / l;
    root.pos = {pos[0] + d[0], pos[1] + d[1], pos[2] + d[2]};
    c.setPlacement(root);
}

// FUN_001975f0: steers the root toward the path, predicting the pose one beat ahead.
void GuitarWalk::steer(Character& c, float dBeat) {
    if (dBeat == 0 || !walking(c)) return;
    hold_ = {};
    Xfm root = c.placement();
    V3 P = root.pos;
    const Character::DriverInfo cur = c.newestStarted();
    const int last = int(chain_.size()) - 1;
    int i = std::min(k_ - 2, last);
    float f = cur.frame, tgt = f + 1, frac = 1;
    V3 pred = P;
    float pth = yawOf(root);
    while (i < last && chain_[size_t(i + 1)].src < tgt) {
        integrate(c, chain_[size_t(i)].clip, f, chain_[size_t(i + 1)].src, pred, pth);
        tgt = chain_[size_t(i + 1)].start + (tgt - chain_[size_t(i + 1)].src);
        f = chain_[size_t(i + 1)].start;
        ++i;
    }
    if (i == last && c.clipEnd(chain_[size_t(i)].clip) < tgt) {
        frac -= tgt - c.clipEnd(chain_[size_t(i)].clip);
        tgt = c.clipEnd(chain_[size_t(i)].clip);
    }
    integrate(c, chain_[size_t(i)].clip, f, tgt, pred, pth);
    const int npts = int(path_.size());
    V3 T;
    if (seg_ >= npts - 1) {  // the last leg: the ideal pose a beat from now, integrated back from the end
        T = path_.back();
        float tth = destYaw_;
        float e = c.clipEnd(chain_.back().clip);
        for (int j = last; j > i; --j) {
            integrate(c, chain_[size_t(j)].clip, e, chain_[size_t(j)].start, T, tth);
            e = chain_[size_t(j)].src;
        }
        integrate(c, chain_[size_t(i)].clip, e, tgt, T, tth);
    } else {
        T = path_[size_t(seg_)];
        if ((pred[0] - T[0]) * dir_[0] + (pred[1] - T[1]) * dir_[1] >= 0) setSegment(seg_ + 1);
    }
    // The corridor: at most slop (12) off the segment line.
    if (seg_ >= 1 && seg_ < npts) {
        const V3& a = path_[size_t(seg_ - 1)];
        float along = (T[0] - a[0]) * dir_[0] + (T[1] - a[1]) * dir_[1];
        V3 foot{a[0] + dir_[0] * along, a[1] + dir_[1] * along, 0};
        V3 e{T[0] - foot[0], T[1] - foot[1], 0};
        float el = len2(e);
        if (el > 12) {
            foot[0] += dir_[0] * (el - 12), foot[1] += dir_[1] * (el - 12);
            T[0] = foot[0] + e[0] * 12 / el, T[1] = foot[1] + e[1] * 12 / el;
        }
    }
    // FUN_00196690: the angle between the target and the predicted direction.
    float tx = T[0] - P[0], ty = T[1] - P[1], px = pred[0] - P[0], py = pred[1] - P[1];
    float tl = std::max(std::hypot(tx, ty), 5.0f), pl = std::max(std::hypot(px, py), 5.0f);
    float alpha = std::asin(std::clamp((tx * py - ty * px) / (tl * pl), -1.0f, 1.0f));
    if (float d = std::hypot(px, py); corr_ != 0 && d > 0) {
        root.pos[0] += px * (dBeat * corr_ / d);
        root.pos[1] += py * (dBeat * corr_ / d);
    }
    rotateZ(root, (-alpha / std::max(frac, 0.25f)) * dBeat);
    c.setPlacement(root);
}

// FUN_001958b0: while a clip waits to start, eases the root so that, when it
// starts, the guitarist stands at the hold position facing the hold yaw.
void GuitarWalk::hold(Character& c, float dBeat) {
    if (!(hold_.hasPos || hold_.hasYaw) || !c.rootMotion() || dBeat == 0) return;
    Character::DriverInfo pend, before;
    if (!c.pendingAfterNewest(&pend, &before) || pend.delay < 0) return;
    Xfm root = c.placement();
    V3 pp = root.pos;
    float py = yawOf(root);
    integrate(c, before.clip, before.frame, before.frame + pend.delay, pp, py);
    float rate = dBeat * 1.1f / std::max({pend.delay, c.clipBlendWidth(pend.clip), 1e-3f});
    rate = std::min(rate, 1.0f);
    if (hold_.hasPos)
        for (int k = 0; k < 3; ++k) root.pos[size_t(k)] += (hold_.pos[size_t(k)] - pp[size_t(k)]) * rate;
    if (hold_.hasYaw) rotateZ(root, wrap(hold_.yaw - py) * rate);
    c.setPlacement(root);
}

void GuitarWalk::cancel(Character& c) {
    if (state_ != 0 && !chain_.empty()) {
        Character::DriverInfo pend;
        if (c.pendingAfterNewest(&pend, nullptr) && pend.blend == 0)
            for (const WalkClip& w : chain_)
                if (w.clip == pend.clip) {
                    c.popUnstarted();
                    break;
                }
    }
    chain_.clear();
    path_.clear();
    state_ = 0;
    k_ = 0;
    if (!hold_.hasPos && !spots_.empty()) {  // a cancelled walk keeps to the nearest spot
        const Xfm& s = spots_[size_t(nearestSpot(c.placement().pos, false))];
        hold_ = {true, true, s.pos, yawOf(s)};
    }
}

void GuitarWalk::loseTeleport(Character& c, const V3& singerPelvis) {
    if (!config_.loseTeleport) return;
    const Xfm& root = c.placement();
    if (std::hypot(root.pos[0] - singerPelvis[0], root.pos[1] - singerPelvis[1]) > 72) return;
    const Xfm& spot = spots_[size_t(nearestSpot(root.pos, false))];
    c.setPlacement(spot);
    hold_ = {true, true, spot.pos, yawOf(spot)};  // [I] the original only moves the root
}
