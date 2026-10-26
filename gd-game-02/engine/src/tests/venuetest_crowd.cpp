// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/venuetest.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/crowd.h"
#include "game/venue.h"
#include "game/world.h"
#include "script/script.h"

// --venuetest's crowd, crowd audio and two-player / encore part
// (docs/venues.md "Crowd", "Crowd audio", "Two players, encores and the
// venues' extras"). Each check that fails adds a line to `report` and counts.

namespace {

struct Checker {
    std::string& report;
    int failures = 0;
    void check(bool ok, const std::string& what) {
        if (ok) return;
        ++failures;
        report += "\n    FAIL " + what;
    }
};

// A chain directory's script object ({<dir> get key}) by its milo's root name.
ScriptObject* dirScript(Interp& in, const std::string& name) { return in.resolver ? in.resolver(name) : nullptr; }

std::vector<std::string> names(Interp& in, ScriptObject* o, const std::string& key) {
    std::vector<std::string> r;
    if (!o) return r;
    const Value v = o->getProp(in, key);
    if (v.type == Value::Type::Array && v.array)
        for (const Value& e : *v.array) {
            const Value x = in.evalValue(e);
            r.push_back(x.type == Value::Type::Object && x.object ? x.object->name() : x.str());
        }
    return r;
}

int expectedShown(const Crowd::State& s, float f) {
    int n = 0;
    for (const auto& a : s.archetypes) n += std::min(int(a.cards.size()), int(float(a.cards.size()) * f)) + int(a.threeD.size());
    return n;
}

}  // namespace

int crowdRun(Venue& v, std::string& report) {
    Checker c{report};
    World w(v);
    Interp& in = w.interp();
    Crowd& crowd = w.crowd();
    const bool mp = v.variant() != VenueVariant::Single;
    double t = 0;
    auto step = [&](double dt) {
        t += dt;
        w.setClock(t, t * 2);
        w.update();
    };
    w.start(mp);

    // ---- the crowd
    int total = 0, shown = 0;
    for (const Crowd::State& s : crowd.crowds()) {
        total += s.total();
        for (const auto& a : s.archetypes) c.check(a.actor != nullptr, s.crowd->name + ": archetype " + a.name + " does not load");
    }
    // crowd.dta's {exists game} branch (the game's): a bare `game` for the test.
    ScriptObject game("game", "Game");
    in.registerObject(&game);
    // intro_start_msg through the world's sinks: set_fullness 0.5 0.5.
    w.message("intro_start_msg", {Value::integer(0), Value::integer(0)});
    for (const Crowd::State& s : crowd.crowds()) c.check(s.full2d == 0.5f && s.full3d == 0.5f, s.crowd->name + ": intro fullness");
    const float kFull[] = {0.1f, 0.25f, 0.5f, 1.0f};
    // Groups per level (crowd.dta animate): Boot bad; Bad 0.8 bad 0.2 ok; Okay 0.8 ok 0.2 great; Great great.
    for (int level = 0; level < 4; ++level) {
        w.message("excitement", {Value::integer(level)});
        for (const Crowd::State& s : crowd.crowds()) {
            in.send(s.script, "crowd_update", {}, false);
            c.check(s.full2d == kFull[level], s.crowd->name + ": fullness at excitement " + std::to_string(level));
            c.check(s.shownCount() == expectedShown(s, kFull[level]), s.crowd->name + ": shown placements at excitement " + std::to_string(level));
            std::map<std::string, int> groups;
            int n = 0;
            for (const auto& a : s.archetypes)
                if (a.actor) ++groups[a.actor->group], ++n;
            auto frac = [&](float f) { return int(float(n) * f + 0.5f - 1e-4f); };  // the accumulator's rounding
            if (level == 0) c.check(groups["bad"] == n, s.crowd->name + ": Boot plays bad");
            if (level == 1) c.check(groups["bad"] == frac(0.8f) && groups["ok"] == n - frac(0.8f), s.crowd->name + ": Bad mix");
            if (level == 2) c.check(groups["ok"] == frac(0.8f) && groups["great"] == n - frac(0.8f), s.crowd->name + ": Okay mix");
            if (level == 3) c.check(groups["great"] == n, s.crowd->name + ": Great plays great");
        }
        if (level == 1)
            for (const Crowd::State& s : crowd.crowds()) shown += s.shownCount();
    }
    w.message("crowd_half_tempo");
    for (const Crowd::State& s : crowd.crowds())
        for (const auto& a : s.archetypes) c.check(!a.actor || a.actor->beatScale == 0.5f, s.crowd->name + ": half tempo");
    w.message("crowd_normal_tempo");
    // A shot's crowd3d pairs.
    int threeD = 0;
    for (const scene::CamShot* shot : w.shots()) {
        if (shot->crowd3d.empty() || !crowd.find(shot->crowd)) continue;
        crowd.startShot(shot, true);
        const Crowd::State& s = *crowd.find(shot->crowd);
        int n3 = 0, cards = 0;
        for (const auto& a : s.archetypes) n3 += int(a.threeD.size()), cards += int(a.cards.size());
        c.check(n3 > 0 && n3 <= int(shot->crowd3d.size()) && n3 + cards == s.total(), shot->name + ": crowd3d moves placements");
        c.check(s.rotate == (shot->flag("crowd_face_camera") == "1"), shot->name + ": rotate follows crowd_face_camera");
        threeD += n3;
        w.message("crowd_hide");
        c.check(s.shownCount() == int(float(n3) * s.full3d) || s.shownCount() <= n3, shot->name + ": crowd_hide leaves the 3D crowd");
        break;
    }
    in.unregisterObject(&game);

    // ---- the crowd audio
    CrowdAudio& au = crowd.audio();
    const int decoded = au.decodedStreams();
    for (int s = 0; s < CrowdAudio::kStreams; ++s)
        c.check(!au.entry(CrowdAudio::Stream(s)).file.empty(), "crowd audio: no stream " + std::to_string(s));
    c.check(decoded == CrowdAudio::kStreams, "crowd audio: " + std::to_string(decoded) + " streams decoded");
    c.check(au.entry(CrowdAudio::kEncoreOutro).loopEndMs > au.entry(CrowdAudio::kEncoreOutro).loopStartMs, "crowd audio: encore loop_ms");
    au.introStart(false, false);
    c.check(au.playing() == CrowdAudio::kIntro, "crowd audio: intro");
    au.musicStart(3, t);
    au.update(t + 0.5);
    c.check(au.playing() == CrowdAudio::kIntro, "crowd audio: the level waits crowd_audio_delay");
    au.update(t + 1.0);
    c.check(au.playing() == CrowdAudio::kGreat, "crowd audio: the Great loop");
    au.introStart(false, true);
    c.check(au.playing() == CrowdAudio::kEncoreIntro, "crowd audio: encore intro");
    w.message("encore_outro");
    c.check(au.playing() == CrowdAudio::kEncoreOutro, "crowd audio: encore outro");

    // ---- two players: the hides and the amps (the chain's RndDir types)
    int hidden = 0, hideErrors = 0, amps = 0;
    for (const auto& p : v.parts()) {
        ScriptObject* d = dirScript(in, p.milo->root().name);
        if (!d) continue;
        for (const std::string& n : names(in, d, "multiplayer_hide_objects"))
            if (const scene::Object* o = v.find(n)) {
                ++hidden;
                if (w.pose().showing(o) == mp) ++hideErrors;
            }
        for (const std::string& n : names(in, d, "singleplayer_hide_objects"))
            if (const scene::Object* o = v.find(n)) {
                ++hidden;
                if (w.pose().showing(o) != mp) ++hideErrors;
            }
        // set_player1_bass: the bass amp shows, the guitar amp hides.
        const auto bass = names(in, d, "player1_bassamp_objects"), guitar = names(in, d, "player1_guitaramp_objects");
        if (bass.empty() && guitar.empty()) continue;
        w.message("set_player1_bass");
        for (const std::string& n : bass)
            if (const scene::Object* o = v.find(n)) amps += 1, c.check(w.pose().showing(o), n + " shows for a bass player");
        for (const std::string& n : guitar)
            if (const scene::Object* o = v.find(n)) amps += 1, c.check(!w.pose().showing(o), n + " hides for a bass player");
        w.message("set_player1_guitar");
    }
    c.check(hideErrors == 0, std::to_string(hideErrors) + " of " + std::to_string(hidden) + " (single|multi)player_hide_objects wrong");

    // ---- encores, the drummer explosion, the venues' extras
    const World::Stats before = w.stats();
    w.message("play_encore_fx");
    const int encoreTriggers = w.stats().triggersFired - before.triggersFired;
    const World::Stats b2 = w.stats();
    w.message("drummer_explode");
    const int explodeCalls = w.stats().proxyCalls - b2.proxyCalls;
    // The drummer explosion is in the one-player worlds only [C data: no proxy in _mp / _coop].
    if ((v.name() == "battle" || v.name() == "small1") && !mp) c.check(explodeCalls > 0, "drummer_explode calls its proxy");
    std::string extras;
    if (v.name() == "arena") {
        w.message("peak_on");
        w.message("chorus");
        step(0.1);
        const scene::Object* sparks = v.find("sparks.filt");
        c.check(sparks && w.animating(sparks), "arena: chorus x peak_on animates sparks.filt");
        extras = " sparks";
    }
    if (v.name() == "small2") {
        // A lighting preset's trigger (neon_all_on.trig) -> the neon controller's all_on: nine EnvAnims.
        auto* trig = v.find<scene::EventTrigger>("neon_all_on.trig");
        c.check(trig != nullptr, "small2: neon_all_on.trig");
        if (trig) w.trigger(trig);
        int lit = 0;
        for (int k = 0; k < 12; ++k) step(0.25);
        for (const scene::EnvAnim* e : v.all<scene::EnvAnim>())
            if (w.animating(e) || w.currentFrame(e) > 0) ++lit;
        c.check(lit >= 9, "small2: the neon controller drives its EnvAnims (" + std::to_string(lit) + ")");
        extras = " neon " + std::to_string(lit);
    }
    if (v.name() == "small1") {
        // The bottle throws: their `start` (bottle_throw type) shows them within 10 s.
        int bottles = 0, shownBottles = 0;
        for (const auto& p : v.proxies())
            if (p.path.find("bottle_throw") != std::string::npos) {
                ++bottles;
                w.interp().send(w.scriptFor(const_cast<scene::DirObj*>(p.object)), "start", {}, false);
            }
        for (int k = 0; k < 44; ++k) step(0.25);
        for (const auto& p : v.proxies())
            if (p.path.find("bottle_throw") != std::string::npos && w.pose().showing(p.object)) ++shownBottles;
        // Each proxy keeps its own throw_task (ScriptObject::nameScope).
        c.check(bottles == 4 && shownBottles == 4, "small1: " + std::to_string(shownBottles) + " of " + std::to_string(bottles) + " bottles thrown");
        extras = " bottles " + std::to_string(shownBottles);
    }
    report = std::to_string(crowd.crowds().size()) + " crowds, " + std::to_string(total) + " placements (" + std::to_string(shown) +
             " at Bad), 3D " + std::to_string(threeD) + "; audio " + std::to_string(decoded) + " streams; hides " + std::to_string(hidden) +
             ", amps " + std::to_string(amps) + "; encore fx " + std::to_string(encoreTriggers) + " triggers, explode " +
             std::to_string(explodeCalls) + " calls" + extras + report;
    return c.failures;
}
