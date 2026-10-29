// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/venuetest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "audio/synth.h"
#include "data/game_data.h"
#include "game/venue.h"
#include "game/camera_director.h"
#include "game/light_presets.h"
#include "game/world.h"
#include "tests/director_golden.h"
#include "video/ipu.h"
#include "script/script.h"
#include "scene/milo.h"
#include "scene/ui_styles.h"
#include "scene/synth_objects.h"
#include "scene/world_objects.h"

namespace {

using namespace scene;

// Classes the port does not decode, expected in venue milos: the banks'
// reverb send (not played, audio/synth.h). The band slots (Character /
// BandCharacter) decode since M9 (char/char_objects.h).
const std::set<std::string> kRawClasses = {"FxSendReverb"};

// World milos no venue loads [C data: no script or milo of this disc names
// them]: work files left on the disc (the "attempt" and "export" scenes, a
// test scene, a shadow variant) and the shared light can (its objects are
// copied into fest_lighting).
const std::set<std::string> kUnusedMilos = {
    "world/arena/og/gen/arena_attempt.milo_ps2",     "world/fest/og/gen/fest_attempt.milo_ps2",
    "world/fest/og/gen/zombie_export.milo_ps2",      "world/shared/lightcan/gen/lightcan.milo_ps2",
    "world/small1/og/gen/test.milo_ps2",             "world/theatre/og/gen/randall_export.milo_ps2",
    "world/theatre/og/gen/rock_ball_export.milo_ps2", "world/theatre/og/gen/theatre_geom_shadow_town.milo_ps2",
};

// A reference from one object to another, by name.
struct Ref {
    const char* field;
    std::string name;
};

std::vector<Ref> refsOf(const Object& o) {
    std::vector<Ref> r;
    auto add = [&](const char* f, const std::string& n) {
        if (!n.empty()) r.push_back({f, n});
    };
    auto addAll = [&](const char* f, const std::vector<std::string>& v) {
        for (const auto& n : v) add(f, n);
    };
    auto trans = [&](const Trans& t) {
        add("parent", t.parent);
        add("target", t.target);
    };
    if (auto* m = o.as<Mesh>()) {
        trans(m->trans);
        add("mat", m->mat);
        if (m->verts.empty()) add("geom_owner", m->geomOwner);
        for (const auto& b : m->bones) add("bone", b.name);
    } else if (auto* m = o.as<Mat>()) {
        for (const auto* n : {&m->diffuseTex, &m->nextPass, &m->normalMap, &m->emissiveMap, &m->specularMap, &m->customDiffuseMap,
                              &m->environMap})
            add("texture", *n);
    } else if (auto* g = o.as<Group>()) {
        trans(g->trans);
        addAll("objects", g->objects);
        add("environ", g->environment);
        add("lod", g->lod);
    } else if (auto* t = o.as<TransObj>()) {
        trans(t->trans);
    } else if (auto* a = o.as<TransAnim>()) {
        add("trans", a->trans);
        add("keys_owner", a->keysOwner);
    } else if (auto* a = o.as<MatAnim>()) {
        add("mat", a->mat);
        add("keys_owner", a->keysOwner);
        for (const auto& k : a->texKeys) add("tex_keys", k.tex);
    } else if (auto* a = o.as<AnimFilter>()) {
        add("anim", a->anim);
    } else if (auto* p = o.as<ParticleSys>()) {
        trans(p->trans);
        add("mat", p->mat);
        add("mesh", p->mesh);
        add("relative_parent", p->relativeParent);
    } else if (auto* a = o.as<ParticleSysAnim>()) {
        add("part", a->partSys);
        add("keys_owner", a->keysOwner);
    } else if (auto* a = o.as<MeshAnim>()) {
        add("mesh", a->mesh);
        add("keys_owner", a->keysOwner);
    } else if (auto* e = o.as<Environ>()) {
        addAll("lights", e->lights);
    } else if (auto* l = o.as<Light>()) {
        trans(l->trans);
        add("texture", l->texture);
        addAll("shadowcasters", l->shadowcasters);
    } else if (auto* c = o.as<Cam>()) {
        trans(c->trans);
        add("target_tex", c->targetTex);
    } else if (auto* c = o.as<CamShot>()) {
        add("path", c->path);
        add("crowd", c->crowd);
        add("glow_spot", c->glowSpot);
        addAll("hide_list", c->hideList);
        add("postprocess", c->flag("postprocess"));
        add("next_shot", c->flag("next_shot"));
        for (const auto& k : c->keyframes) {
            for (const auto& t : k.targets) add("target", t.object);
            add("focal_target", k.focalTarget.object);
            add("parent", k.parent.object);
        }
    } else if (auto* p = o.as<LightPreset>()) {
        addAll("spotlights", p->spotlights);
        addAll("environs", p->environs);
        addAll("lights", p->lights);
        for (const auto& k : p->keyframes)
            for (const auto& s : k.spots) add("spot_target", s.target);
    } else if (auto* s = o.as<Spotlight>()) {
        trans(s->trans);
        add("beam_material", s->beam.material);
        addAll("cutouts", s->beam.cutouts);
        add("light_can", s->lightCan);
        add("target", s->target);
        add("spot_material", s->spotMaterial);
        add("flare_material", s->flareMaterial);
        add("lens_material", s->lensMaterial);
        addAll("additional_objects", s->additionalObjects);
    } else if (auto* x = o.as<Sfx>()) {
        for (const auto& m : x->maps) add("sample", m.sample);
        add("send", x->send);
    } else if (auto* g = o.as<GroupSeq>()) {
        addAll("children", g->children);
    } else if (auto* q = o.as<SfxSeq>()) {
        add("child", q->child);
    } else if (auto* e = o.as<EventTrigger>()) {
        for (const auto& a : e->anims) add("anims", a.anim);
        addAll("sounds", e->sounds);
        addAll("shows", e->shows);
        for (const auto& h : e->hideDelays) add("hides", h.first);
        add("next_random", e->nextRandom);
        for (const auto& p : e->proxyCalls) add("proxy", p.first);
    } else if (auto* f = o.as<Flare>()) {
        trans(f->trans);
        add("mat", f->mat);
    } else if (auto* a = o.as<LightAnim>()) {
        add("light", a->light);
        add("keys_owner", a->keysOwner);
    } else if (auto* a = o.as<EnvAnim>()) {
        add("environ", a->environName);
        add("keys_owner", a->keysOwner);
    } else if (auto* s = o.as<SetObj>()) {
        addAll("objects", s->objects);
    } else if (auto* c = o.as<WorldCrowd>()) {
        add("placement_mesh", c->placementMesh);
    } else if (auto* w = o.as<Waypoint>()) {
        trans(w->trans);
        addAll("connections", w->connections);
    } else if (auto* p = o.as<PostProc>()) {
        add("luminance_map", p->luminanceMap);
        add("noise_map", p->noiseMap);
    } else if (auto* d = o.as<DirObj>()) {
        if (d->hasRnd) {
            trans(d->rnd.trans);
            add("environ", d->rnd.environment);
        }
    }
    return r;
}

struct VariantResult {
    int parts = 0, proxies = 0, objects = 0, decoded = 0, undecoded = 0, refs = 0, unresolved = 0;
    std::map<std::string, int> raw;                        // untyped classes
    std::map<std::string, std::vector<std::string>> missing;  // class.field -> names
    std::vector<std::string> problems;
};

VariantResult checkVariant(const Venue& v, std::set<std::string>& loaded) {
    VariantResult r;
    r.parts = int(v.parts().size());
    r.proxies = int(v.proxies().size());
    r.problems = v.problems();
    std::set<std::string> dirNames;
    for (const auto& p : v.parts()) {
        loaded.insert(p.path);
        dirNames.insert(p.milo->root().name);
    }
    for (const auto& p : v.proxies()) {
        loaded.insert(p.path);
        dirNames.insert(p.milo->root().name);
        dirNames.insert(p.object->name);
    }
    v.forEach([&](Object& o) {
        ++r.objects;
        if (!o.typed) {
            ++r.raw[o.cls];
            return;
        }
        if (!o.decoded) {
            ++r.undecoded;
            r.problems.push_back(o.cls + " " + o.name + ": " + o.error);
            return;
        }
        ++r.decoded;
        for (const Ref& ref : refsOf(o)) {
            ++r.refs;
            if (dirNames.count(ref.name)) continue;  // the directory itself
            if ((o.dir && o.dir->resolve(ref.name)) || v.find(ref.name)) continue;
            ++r.unresolved;
            auto& m = r.missing[o.cls + "." + ref.field];
            if (std::find(m.begin(), m.end(), ref.name) == m.end()) m.push_back(ref.name);
        }
    });
    return r;
}

// The venue's script and triggers through a scripted mini-song on the
// world's own interpreter: enter, start, the intro, beats with the band's
// and the player's events, every excitement, the sections and lighting
// texts, a won song. Returns the problems (unknown names, script errors,
// unhandled messages); the stubs (the next phase's methods) are listed.
int scriptRun(Venue& v, std::string& summary) {
    // The sound effects play on a Synth without a device (counted).
    Synth synth;
    World w(v);
    w.setSynth(&synth);
    // The camera director and the lighting presets (game/camera_director.h, game/light_presets.h).
    LightPresets lights(w);
    CameraDirector director(w);
    Interp& in = w.interp();
    in.resetReport();
    double s = 0;
    auto step = [&](double dt) {
        s += dt;
        w.setClock(s, s * 2);
        w.update();
        lights.update();
        director.update();
    };
    w.start(v.variant() != VenueVariant::Single);
    w.message("intro_start_msg", {Value::integer(0), Value::integer(0)});
    step(0.5);
    w.message("intro_end");
    w.message("music_start");
    for (int beat = 0; beat < 64; ++beat) {
        in.var("beat") = Value::integer(beat);
        w.message("beat");
        if (beat % 4 == 0) w.message("downbeat");
        w.message("kick_drum");
        w.message("bass_hit");
        w.message("hit_p0_fret" + std::to_string(beat % 5 + 1));
        if (beat % 16 == 8) {
            in.var("upcoming_section") = Value::symbol(beat % 32 == 8 ? "verse" : "chorus");
            w.message("one_bar_to");
        }
        if (beat == 12) w.message("chorus");
        if (beat == 20) w.message("lighting_change", {Value::list({Value::symbol("strobe")})});
        if (beat == 24) w.message("do_lighting_next_keyframe");
        if (beat == 28) w.message("venue_effect");
        if (beat == 30) w.message("peak_on");
        if (beat == 34) w.message("peak_off");
        if (beat == 36) w.message("build_streak");
        if (beat % 12 == 0) w.message("excitement", {Value::integer((beat / 12) % 4)});
        step(0.5);
    }
    w.message("game_won_msg", {Value::integer(0)});
    for (int i = 0; i < 20; ++i) step(0.25);
    const World::Stats& st = w.stats();
    int problems = int(in.unknownCounts().size() + in.errors().size() + in.unhandledCounts().size());
    summary = std::to_string(st.eventsFired) + " events, " + std::to_string(st.triggersFired) + " triggers, " +
              std::to_string(st.animsStarted) + " anims, " + std::to_string(st.shows) + " shows, " + std::to_string(st.hides) +
              " hides, " + std::to_string(st.proxyCalls) + " proxy calls, " + std::to_string(st.sounds) + " sounds (" +
              std::to_string(synth.stats().voices) + " voices), " +
              std::to_string(director.shotsStarted()) + " shots, " + std::to_string(lights.switches()) + " presets; stubs:";
    for (const auto& [k, n] : in.stubCounts())
        if (k.rfind("world ", 0) == 0) summary += " " + k.substr(6);
    for (const auto& [k, n] : in.unknownCounts()) summary += "\n    UNKNOWN " + k;
    for (const auto& [k, n] : in.unhandledCounts()) summary += "\n    UNHANDLED " + k;
    for (const auto& e : in.errors()) summary += "\n    ERROR " + e;
    // Every sound the triggers and the crowd play is in the bank and decodes.
    const Synth::Stats& ss = synth.stats();
    problems += ss.decodeFailures + ss.unresolved;
    if (ss.decodeFailures || ss.unresolved)
        summary += "\n    SOUNDS " + std::to_string(ss.decodeFailures) + " decode failures, " + std::to_string(ss.unresolved) + " unresolved";
    for (const auto& [k, n] : in.stubCounts())
        if (k.rfind("world_sfx ", 0) == 0) ++problems, summary += "\n    SOUND " + k.substr(10);
    return problems;
}

// Objects that could show an IPU movie: a Movie (RndMovie), a VideoScreen
// (its default / blend movies) or a Tex made from a .ipu file.
int movieObjects(const Venue& v) {
    int n = 0;
    v.forEach([&](Object& o) {
        if (o.cls == "Movie" || o.cls == "TexMovie" || o.cls == "VideoScreen") ++n;
        else if (auto* t = o.as<Tex>(); t && t->file.size() > 4 && t->file.compare(t->file.size() - 4, 4, ".ipu") == 0) ++n;
    });
    return n;
}

// The venue type list (song_movie fest_1) names in fest.dta, recursively.
const ::DataNode* findKey(const ::DataNode& n, const std::string& key) {
    if (!n.isList()) return nullptr;
    if (n.children.size() >= 2 && n.children[0].isText() && n.children[0].s == key) return &n;
    for (const auto& c : n.children)
        if (const ::DataNode* r = findKey(c, key)) return r;
    return nullptr;
}

// fest_1.ipu (docs/venues.md "fest_1.ipu"): fest.dta's (song_movie fest_1)
// names it, but SLUS_215.86 never reads song_movie and no scene has a Movie,
// so the original never shows it [C]. Checks that it stays that way (no
// object of the venues could draw it: drawn 0), that every frame decodes
// and the pictures change, the decode against tools/ipu.py's hash, and the
// PS2 Movie's forward-only SetFrame on a 30 fps song clock.
int ipuRun(const GameData& data, int venueMovieObjects, std::string& report) {
    int problems = 0;
    std::string name;
    try {
        const ::DataNode fest = scene::loadPlainDta(data, "world/fest/fest.dta");
        if (const ::DataNode* k = findKey(fest, "song_movie")) name = k->children[1].s;
    } catch (const std::exception& e) {
        report = std::string("fest.dta: ") + e.what();
        return 1;
    }
    if (name != "fest_1") ++problems;
    const std::string path = "world/shared/" + name + ".ipu";
    if (!data.exists(path)) {
        report = "song_movie '" + name + "': no " + path;
        return problems + 1;
    }
    try {
        IpuMovie movie(data.read(path));
        std::vector<uint8_t> rgba;
        uint64_t all = ipuHash(nullptr, 0), prev = 0;
        int same = 0;
        for (int i = 0; i < movie.frameCount(); ++i) {
            movie.decodeRgba(i, rgba);
            all = ipuHash(rgba.data(), rgba.size(), all);
            const uint64_t h = ipuHash(rgba.data(), rgba.size());
            if (i && h == prev) ++same;
            prev = h;
        }
        // tools/ipu.py extracted/ark/world/shared/fest_1.ipu: "all frames rgba fnv".
        constexpr uint64_t kGolden = 0xa98f1be53bddb31dull;
        const bool golden = all == kGolden;
        // GH1's Movies are driven by the song in ms through a 0.03 ScaleOffset
        // filter (30 fps, gd-game-01 docs/video.md 4.3) [I for this disc: no
        // Movie exists]; 3.2 s at 60 Hz, looping.
        IpuPlayer player(movie, true);
        const int steps = 192;
        for (int k = 0; k <= steps; ++k) player.setFrame(float(k * 1000.0 / 60.0 * 0.03));
        const int expectFrame = int(steps * 1000.0 / 60.0 * 0.03) % movie.frameCount();
        const bool advanced = player.current() == expectFrame && player.decodes() >= 90 && player.rewinds() == 3;  // the first call (current = n) and two wraps
        char buf[320];
        std::snprintf(buf, sizeof buf,
                      "%s %dx%d, %d frames decoded (flags 0x%02x, %zu..%zu bytes), %d unchanged, fnv %016llx%s; "
                      "player %llu decodes, %llu rewinds, at %d%s; venue movie objects %d, drawn 0",
                      path.c_str(), movie.width(), movie.height(), movie.frameCount(), movie.frameFlags(0), movie.frameBytes(0),
                      movie.frameBytes(movie.frameCount() - 1), same, static_cast<unsigned long long>(all),
                      golden ? "" : " (NOT the tools/ipu.py hash)", static_cast<unsigned long long>(player.decodes()),
                      static_cast<unsigned long long>(player.rewinds()), player.current(), advanced ? "" : " (WRONG)",
                      venueMovieObjects);
        report = buf;
        problems += (movie.frameCount() != 45) + (movie.width() != 256) + (movie.height() != 256) + same + !golden + !advanced +
                    venueMovieObjects;
    } catch (const std::exception& e) {
        report = path + ": " + e.what();
        ++problems;
    }
    return problems;
}

}  // namespace

int runVenueTest(const std::string& dataPath) {
    std::unique_ptr<GameData> data;
    try {
        data = std::make_unique<GameData>(dataPath);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "venue test: %s\n", e.what());
        return 2;
    }
    int failures = 0;
    std::set<std::string> loaded;
    int venueMovieObjects = 0;
    const VenueVariant variants[] = {VenueVariant::Single, VenueVariant::Mp, VenueVariant::Coop};
    for (const std::string& name : venues::names()) {
        for (VenueVariant var : variants) {
            const uint64_t t0 = SDL_GetTicksNS();
            std::unique_ptr<Venue> v;
            try {
                v = std::make_unique<Venue>(*data, name, var);
            } catch (const std::exception& e) {
                std::printf("%-8s %-4s FAILED to load: %s\n", name.c_str(), venues::variantName(var), e.what());
                ++failures;
                continue;
            }
            const double ms = double(SDL_GetTicksNS() - t0) / 1e6;
            VariantResult r = checkVariant(*v, loaded);
            venueMovieObjects += movieObjects(*v);
            std::string raw;
            int rawUnexpected = 0;
            for (const auto& [cls, n] : r.raw) {
                raw += " " + cls + " " + std::to_string(n);
                if (!kRawClasses.count(cls)) ++rawUnexpected;
            }
            std::printf("%-8s %-4s %d milos + %d proxies, %d objects (%d decoded, %d undecoded), %d references, %d unresolved, %.0f ms; raw:%s\n",
                        name.c_str(), venues::variantName(var), r.parts, r.proxies, r.objects, r.decoded, r.undecoded, r.refs, r.unresolved,
                        ms, raw.c_str());
            for (const auto& p : r.problems) std::printf("    problem: %s\n", p.c_str());
            for (const auto& [key, names] : r.missing) {
                std::string list;
                for (size_t i = 0; i < names.size() && i < 8; ++i) list += (i ? ", " : "") + names[i];
                if (names.size() > 8) list += ", ...";
                std::printf("    unresolved %s (%zu): %s\n", key.c_str(), names.size(), list.c_str());
            }
            if (rawUnexpected) std::printf("    unexpected undecoded classes\n");
            std::string script;
            const int scriptProblems = scriptRun(*v, script);
            std::printf("    script: %s\n", script.c_str());
            std::string crowdReport;
            const int crowdProblems = crowdRun(*v, crowdReport);
            std::printf("    crowd: %s\n", crowdReport.c_str());
            failures += crowdProblems;
            std::string cameraReport;
            const int cameraProblems = cameraRun(*v, cameraReport);
            std::printf("    camera: %s\n", cameraReport.c_str());
            failures += cameraProblems;
            failures += int(r.problems.size()) + r.undecoded + rawUnexpected + scriptProblems;
        }
        // Every language's star message (stone's geom names ../eng/starmessage.milo).
        for (const char* lang : {"fre", "deu", "ita", "esl"}) {
            const std::string p = "world/" + name + "/og/" + lang + "/gen/starmessage.milo_ps2";
            if (data->exists(p)) loaded.insert(p);
        }
    }
    // World milos nothing loads.
    std::vector<std::string> unused;
    for (const std::string& p : data->paths())
        if (p.rfind("world/", 0) == 0 && p.size() > 9 && p.compare(p.size() - 9, 9, ".milo_ps2") == 0 && !loaded.count(p))
            unused.push_back(p);
    std::sort(unused.begin(), unused.end());
    int unexpectedUnused = 0;
    for (const auto& p : unused) {
        const bool expected = kUnusedMilos.count(p) != 0;
        if (!expected) ++unexpectedUnused;
        std::printf("unused milo: %s%s\n", p.c_str(), expected ? "" : "  (UNEXPECTED)");
    }
    for (const auto& p : kUnusedMilos)
        if (std::find(unused.begin(), unused.end(), p) == unused.end()) {
            std::printf("expected unused milo is loaded now: %s\n", p.c_str());
            ++unexpectedUnused;
        }
    failures += unexpectedUnused;
    // The fest venue's song_movie (video/ipu.h).
    std::string ipuReport;
    const int ipuProblems = ipuRun(*data, venueMovieObjects, ipuReport);
    std::printf("ipu movie: %s\n", ipuReport.c_str());
    failures += ipuProblems;
    // The director's and presets' golden log (tests/director_golden.h).
    failures += runDirectorGolden(*data);
    failures += runCameraRateCheck(*data);
    std::printf("\nvenue test %s: %d failures, %zu world milos loaded, %zu unused\n", failures ? "FAILED" : "passed", failures,
                loaded.size(), unused.size());
    return failures ? 1 : 0;
}
