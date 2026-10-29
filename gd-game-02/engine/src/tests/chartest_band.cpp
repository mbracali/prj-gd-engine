// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/chartest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "char/character.h"
#include "char/midi_parser.h"
#include "data/game_data.h"
#include "data/midi.h"
#include "game/autoplay.h"
#include "game/band.h"
#include "game/camera_director.h"
#include "game/light_presets.h"
#include "game/player.h"
#include "game/rules.h"
#include "game/song_db.h"
#include "game/song_setup.h"
#include "game/venue.h"
#include "game/world.h"
#include "game/world_driver.h"
#include "render/band_view.h"
#include "render/scene_gpu.h"
#include "scene/milo.h"
#include "tests/miditest.h"
#include "ui/char_select.h"

namespace {

// What a character draws at each LOD (render/band_view.h meshesToDraw, the
// world's showing from a fresh pose: the saved flags): something at every
// LOD, the guitar when it has one, never a shadow mesh. Returns the problems.
// A blended mesh that writes depth (src_alpha / add with z_mode normal: a
// guitar's strings) drawn before an opaque one of a lower draw order cuts
// it out where its texture is clear (wave B #14: the guitar select drew
// flyingv_v2's strings, order 0.1, before guitar.mesh, order 0, in file
// order): the first such pair, or "". Equal orders keep the data's order
// (van_yellow's lod0.grp lists its strings first, as GH2 has them [I]).
std::string earlyBlended(const std::vector<const scene::Mesh*>& list) {
    std::vector<const scene::Mesh*> blended;
    for (const scene::Mesh* m : list) {
        const scene::Mat* mat = m->dir ? m->dir->resolve<scene::Mat>(m->mat) : nullptr;
        if (!mat) continue;
        const bool writes = mat->zMode == 1 || mat->zMode == 3 || mat->zMode == 4;
        if (mat->blend == 1) {
            for (const scene::Mesh* b : blended)
                if (b->draw.order > m->draw.order) return b->name + " before " + m->name;
        } else if (mat->blend != 0 && writes && mat->name.find("shadow") == std::string::npos) {
            blended.push_back(m);
        }
    }
    return {};
}

int drawListChecks(chr::Character& c, const std::string& what, bool wantGuitar) {
    const gfx::ScenePose pose;
    int problems = 0;
    const int lods = std::max(1, int(c.own().lods.size()));
    for (int k = 0; k < lods; ++k) {
        c.minLod = k;
        c.updateLod(1e30f);
        const std::vector<const scene::Mesh*> list = BandView::meshesToDraw(c, &pose);
        bool guitar = false, shadow = false;
        // A shadow mesh only where the data puts one in the LOD group itself
        // (sheraton's lod0.grp / lod1.grp list shadow_guitar.mesh).
        const scene::Object* lodObj = k < int(c.own().lods.size()) ? c.dir().get(c.own().lods[size_t(k)].group) : nullptr;
        const scene::Group* lodGroup = lodObj ? lodObj->as<scene::Group>() : nullptr;
        for (const scene::Mesh* m : list) {
            guitar |= m->name.rfind("guitar", 0) == 0;
            const bool listed = lodGroup && std::find(lodGroup->objects.begin(), lodGroup->objects.end(), m->name) != lodGroup->objects.end();
            shadow |= m->mat.find("shadow") != std::string::npos && !listed;
        }
        // (The merged LOD groups keep GH2's append order, 0x821DC870: the
        // outfit's meshes after the character's whatever their draw order.)
        const std::string early;
        if (list.empty() || (wantGuitar && !guitar) || shadow) {
            std::printf("chartest: %s LOD %d draws %zu meshes%s%s%s%s\n", what.c_str(), k, list.size(), wantGuitar && !guitar ? ", no guitar" : "",
                        shadow ? ", a shadow mesh" : "", early.empty() ? "" : ", blended first: ", early.c_str());
            ++problems;
        }
    }
    c.minLod = 0;
    c.updateLod(1e30f);
    return problems;
}


// The band's golden run: hangar18 in arena (the director golden's song),
// the band's main clips per (beat, slot) and the drum kit's triggers.
// A change in the drivers, the band scripts, the parsers or the director
// moves them; check the log (GD02_BAND_LOG=<file>) and update both.
// The band draws from its own RandomGen (GH2's generator, seeded by
// Band::Config::seed), so the world's, the crowd's and other scripts'
// random numbers do not move it. M9.6 / M9.8: walks, interacts, the Star
// Power task and the expression picks are in the run.
constexpr const char* kSong = "hangar18";
constexpr const char* kVenue = "arena";
constexpr int kGoldenLines = 607;
constexpr uint32_t kGoldenHash = 0x342907f5u;

uint32_t fnv(const std::string& s, uint32_t h) {
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

bool finite(const scene::Xfm& x) {
    for (float v : x.rot)
        if (!std::isfinite(v)) return false;
    for (float v : x.pos)
        if (!std::isfinite(v)) return false;
    return true;
}

// Every character milo, every guitar on alterna1, every kit on the drummer:
// they load and every skinned mesh's bones resolve.
int loadChecks(const GameData& data) {
    int failures = 0;
    chr::ClipLibrary clips(data);
    std::vector<std::string> chars;
    for (const std::string& p : data.paths()) {
        if (p.rfind("char/", 0) != 0 || p.find("/ng/gen/") == std::string::npos) continue;
        if (p.find("_ui") != std::string::npos || p.rfind("char/ng/", 0) == 0 || p.rfind("char/shared/", 0) == 0) continue;
        if (p.find("lighter_flame") != std::string::npos) continue;  // the crowd's lighter (a Character v9 effect, not a character)
        if (p.size() < 10 || p.compare(p.size() - 10, 10, ".milo_xbox") != 0) continue;
        chars.push_back(p);
    }
    int loaded = 0, bones = 0, drivers = 0, faces = 0, faceBad = 0;
    for (const std::string& p : chars) {
        try {
            chr::Character c(data, p, clips);
            ++loaded;
            // The lip servo (M9.8): its viseme set has `visemes` and
            // `neutral`, every face channel moves a bone, the eye entries
            // resolve, the servo's weight is 1 (PoseMeshes ignores it).
            if (const scene::FaceFxLipSyncServo* f = c.faceServo()) {
                ++faces;
                chr::ClipSet* set = c.faceClips();
                int unresolved = 0;
                for (const auto& ch : c.faceBones().channels()) {
                    const std::string b = scene::CharBonesSamples::boneOf(ch.name);
                    if (c.skeleton().find(b + ".trans") < 0 && c.skeleton().find(b + ".mesh") < 0) ++unresolved;
                }
                for (const auto& e : f->entries)
                    if (c.skeleton().find(e.object) < 0) ++unresolved;
                if (!set || !set->clip("visemes") || !set->clip("neutral") || unresolved || f->weightable.weight != 1.0f ||
                    c.faceBones().channels().empty()) {
                    std::printf("chartest: %s: lip.servo set %s, %zu face channels, %d unresolved, weight %g\n", p.c_str(), f->visemeSet.c_str(),
                                c.faceBones().channels().size(), unresolved, double(f->weightable.weight));
                    ++faceBad;
                }
            }
            for (const std::string& b : c.unresolvedBones()) {
                if (bones < 10) std::printf("chartest: %s: bone %s does not resolve\n", p.c_str(), b.c_str());
                ++bones;
            }
            for (const auto& d : c.drivers())
                if (!d->clipSet()) {
                    std::printf("chartest: %s: %s has no clip set\n", p.c_str(), d->name().c_str());
                    ++failures;
                }
            drivers += int(c.drivers().size());
        } catch (const std::exception& e) {
            std::printf("chartest: %s: %s\n", p.c_str(), e.what());
            ++failures;
        }
    }
    std::printf("chartest: characters: %d of %zu load, %d drivers, %d unresolved bones, %zu clip sets, %d faces (%d bad)\n", loaded,
                chars.size(), drivers, bones, clips.sets().size(), faces, faceBad);
    failures += bones + faceBad;
    // Guitars and kits.
    int guitars = 0, kits = 0, outfitFail = 0;
    for (const std::string& p : data.paths()) {
        const bool guitar = p.rfind("char/ng/guitars/gen/", 0) == 0, kit = p.rfind("char/ng/drums/gen/dw_", 0) == 0 && p.find("_drums.") != std::string::npos;
        if (!guitar && !kit) continue;
        try {
            chr::Character c(data, guitar ? "char/alterna1/ng/gen/alterna1.milo_xbox" : "char/metal_drummer/ng/gen/metal_drummer.milo_xbox", clips,
                             {{guitar ? "guitar.outfit" : "drums.outfit", p}});
            if (guitar) outfitFail += drawListChecks(c, "outfit " + p, p.find("air_guitar") == std::string::npos);
            // The guitar on its own (GuitarDisplayPanel, the store): the
            // same rules (ui/char_select.h displayMeshes).
            if (guitar) {
                scene::Milo milo(data.read(p), p);
                const std::vector<const scene::Mesh*> list = ui::chars::displayMeshes(milo.root());
                bool body = false, shadow = false;
                for (const scene::Mesh* m : list) {
                    body |= m->name.rfind("guitar", 0) == 0 || m->name.rfind("bass", 0) == 0;
                    // Only where the data lists a shadow mesh in a LOD group (sheraton).
                    bool listed = false;
                    for (const auto& o : milo.root().objects)
                        if (const auto* g = o->as<scene::Group>(); g && o->name.rfind("lod", 0) == 0)
                            listed |= std::find(g->objects.begin(), g->objects.end(), m->name) != g->objects.end();
                    shadow |= m->mat.find("shadow") != std::string::npos && !listed;
                }
                const std::string early = earlyBlended(list);
                const bool air = p.find("air_guitar") != std::string::npos;
                if (list.empty() != air || (!air && !body) || shadow || !early.empty()) {
                    std::printf("chartest: display %s draws %zu meshes%s%s%s%s\n", p.c_str(), list.size(), !air && !body ? ", no guitar" : "",
                                shadow ? ", a shadow mesh" : "", early.empty() ? "" : ", blended first: ", early.c_str());
                    ++outfitFail;
                }
            }
            if (c.outfitObjects() == 0 || !c.unresolvedBones().empty()) {
                std::printf("chartest: outfit %s: %d objects, %zu unresolved bones\n", p.c_str(), c.outfitObjects(), c.unresolvedBones().size());
                ++outfitFail;
            }
            (guitar ? guitars : kits)++;
        } catch (const std::exception& e) {
            std::printf("chartest: outfit %s: %s\n", p.c_str(), e.what());
            ++outfitFail;
        }
    }
    std::printf("chartest: outfits: %d guitars, %d kits merged, %d failed\n", guitars, kits, outfitFail);
    return failures + outfitFail;
}

// A scripted driver sequence (M9.2): alterna1's main.drv plays its idle
// group (kPlayLast | kPlayGraphLoop) for 32 beats at 120 beats a minute,
// then normal (kPlayNow) for 16; the (beat, clip, frame, weight, delay) log of every
// node each beat must match its golden line count and hash.
constexpr int kDriverLines = 82;
constexpr uint32_t kDriverHash = 0x0989cfaeu;
int driverRun(const GameData& data) {
    chr::ClipLibrary clips(data);
    chr::Character c(data, "char/alterna1/ng/gen/alterna1.milo_xbox", clips);
    chr::ClipDriver* d = c.mainDriver();
    if (!d || !d->clipSet()) {
        std::printf("chartest: driver: no main.drv clip set\n");
        return 1;
    }
    chr::Random rng(7);
    d->rng = &rng;
    std::vector<std::string> log;
    d->playGroup("idle", chr::kPlayLast | chr::kPlayGraphLoop);
    const float dt = 1.0f / 60.0f;
    double beat = 0;
    int lastWhole = -1;
    for (int f = 0; f < 48 * 30; ++f) {
        if (f == 32 * 30) d->playGroup("normal", chr::kPlayNow);
        chr::RigContext ctx;
        beat += dt * 2.0;
        ctx.beat = beat, ctx.dBeats = dt * 2.0f, ctx.dSeconds = dt, ctx.seconds = beat / 2;
        c.poll(ctx);
        if (int(beat) != lastWhole) {
            lastWhole = int(beat);
            for (const auto& n : d->chain()) {
                char b[160];
                std::snprintf(b, sizeof b, "%3d %s %.3f %.3f %.3f", lastWhole, n.clip->name.c_str(), double(n.frame), double(n.weight), double(n.delay));
                log.push_back(b);
            }
        }
    }
    uint32_t h = 2166136261u;
    for (const std::string& l : log) h = fnv(l + "\n", h);
    if (const char* out = SDL_getenv("GD02_DRIVER_LOG"))
        if (FILE* f = std::fopen(out, "w")) {
            for (const std::string& l : log) std::fprintf(f, "%s\n", l.c_str());
            std::fclose(f);
        }
    const bool ok = int(log.size()) == kDriverLines && h == kDriverHash;
    std::printf("chartest: driver golden: alterna1 idle / normal, log %d lines, hash %08x (expected %d, %08x)%s\n", int(log.size()), unsigned(h),
                kDriverLines, unsigned(kDriverHash), ok ? "" : " MISMATCH");
    return ok ? 0 : 1;
}

// The song through the world, director, lights and the band, autoplayed.
int bandRun(const GameData& data) {
    const std::vector<SongInfo> songs = loadSongs(data);
    const SongInfo* info = nullptr;
    for (const SongInfo& s : songs)
        if (s.id == kSong) info = &s;
    if (!info) {
        std::printf("chartest: band: no song %s\n", kSong);
        return 1;
    }
    const Rules rules = loadRules(data);
    const Song song = loadChart(data, *info, rules);
    const MidiFile midi = parseMidi(data.read(info->audio.midiPath));
    Venue venue(data, kVenue, VenueVariant::Single);
    World world(venue);
    world.interp().seed(0x60d1e5u);
    LightPresets lights(world, 0x4c504d31u);
    CameraDirector director(world, 0x43414d31u);
    director.provideGameObjects(false, "single");
    Band band(world, data, *info, Band::Config{});
    int failures = 0;
    for (const std::string& p : band.problems()) {
        std::printf("chartest: band: %s\n", p.c_str());
        ++failures;
    }
    std::vector<std::string> log;
    std::map<std::string, int> perSlot;
    band.onClip = [&](double beat, const std::string& slot, const std::string& clip) {
        char b[32];
        std::snprintf(b, sizeof b, "%8.2f ", beat);
        log.push_back(b + slot + " " + clip);
        ++perSlot[slot];
    };
    int shots = 0, bandShots = 0;
    director.onShot = [&](const std::string& s) {
        ++shots;
        if (s.find("guitarist") != std::string::npos || s.find("SOLO") != std::string::npos) ++bandShots;
    };
    std::map<std::string, int> triggers;
    Player p(song, Part::Guitar, Difficulty::Expert, rules, ControllerMode::Guitar, false);
    band.rating = [&](int) { return p.rockMeter(); };
    Autoplay bot(p.chart());
    WorldDriver driver(true);
    band.excitement = [&] { return driver.excitementLevel(); };
    const float lead = -3000;
    driver.reset(lead);
    world.setClock(lead / 1000.0, song.tempo.msToTick(lead) / 480.0);
    band.loadSong(midi, &song);
    world.start(false);
    band.enter();
    int nan = 0, spFrames = 0, walkFrames = 0;
    // Faces (M9.8): poses per member, the singer's visemes, the guitarist's
    // blinks (EyesClosed, frame 9) and expressions.
    std::map<std::string, int> facePoses;
    int singerVisemeFrames = 0, blinkFrames = 0, hiddenPoses = 0;
    std::set<std::string> expressions;
    bool spWas = false;
    std::set<std::string> handClips, spots;
    float maxDrift = 0;
    chr::Character* g0 = band.character("guitarist0");
    const scene::Xfm start = g0 ? g0->xfm() : scene::Xfm{};
    for (float t = lead; t < song.endMs + 6000; t += 1000.0f / 60.0f) {
        bot.feed(p, t);
        p.update(t);
        std::vector<PlayerEvent> events = p.takeEvents();
        // Star Power as soon as it is ready (the bot does not tilt).
        for (const PlayerEvent& e : events)
            if (e.kind == PlayerEvent::Kind::StarPowerReady) p.starPowerButton(t);
        if (p.starPowerActive() != spWas) {
            spWas = p.starPowerActive();
            for (const PlayerEvent& e : p.takeEvents()) events.push_back(e);
        }
        driver.update(world, song, t, &p, events, nullptr, {});
        if (t >= song.endMs) driver.gameWon(world);
        world.update();
        lights.update();
        director.setStarPower(p.starPowerActive());
        director.update();
        band.update(world.seconds(), world.beats());
        for (const auto& m : band.members()) {
            chr::Character& c = *m->character;
            if (!finite(c.xfm())) ++nan;
            for (int i = 0; i < c.skeleton().size(); i += 7)
                if (!finite(c.skeleton().world(i))) ++nan;
            if (m->slot == "guitarist0") {
                if (auto* d = c.driver("left_hand.drv"); d && d->newest()) handClips.insert(d->newest()->name);
                const auto a = c.xfm().pos;
                maxDrift = std::max(maxDrift, std::hypot(a[0] - start.pos[0], a[1] - start.pos[1]));
            }
        }
        if (band.playingStarpower("guitarist0")) ++spFrames;
        if (band.actuallyWalking("guitarist0")) ++walkFrames;
        for (const auto& m : band.members()) {
            chr::Character& c = *m->character;
            const int before = facePoses[m->slot];
            facePoses[m->slot] = c.facePoses();
            if (c.facePoses() == before) continue;
            if (!c.visible) ++hiddenPoses;
            const facefx::FacePose& f = c.lastFace();
            if (m->slot == "singer") {
                for (const auto& fr : f.frames)
                    if (fr.frame > 0 && fr.weight > 0.05f) {
                        ++singerVisemeFrames;
                        break;
                    }
            } else if (m->slot == "guitarist0") {
                for (const auto& fr : f.frames)
                    if (fr.frame == 9 && fr.weight > 0.5f) ++blinkFrames;
                if (m->lip && !m->lip->currentExpression().empty()) expressions.insert(m->lip->currentExpression());
            }
        }
    }
    uint32_t h = 2166136261u;
    for (const std::string& l : log) h = fnv(l + "\n", h);
    if (const char* out = SDL_getenv("GD02_BAND_LOG"))
        if (FILE* f = std::fopen(out, "w")) {
            for (const std::string& l : log) std::fprintf(f, "%s\n", l.c_str());
            std::fclose(f);
        }
    std::string slots;
    for (const auto& [s, n] : perSlot) slots += " " + s + " " + std::to_string(n);
    std::string trig;
    for (const auto& [e, n] : band.triggerCounts()) trig += " " + e + " " + std::to_string(n);
    std::printf("chartest: band triggers:%s\n", trig.c_str());
    std::printf("chartest: band %s in %s: %zu members, clips:%s; %zu hand clips, guitarist0 moved %.1f, %d SP frames, %d shots\n", kSong,
                kVenue, band.members().size(), slots.c_str(), handClips.size(), double(maxDrift), spFrames, shots);
    std::string fp;
    for (const auto& [slot, n] : facePoses) fp += " " + slot + " " + std::to_string(n);
    std::string ex;
    for (const std::string& e : expressions) ex += " " + e;
    std::printf("chartest: band faces:%s; singer visemes %d frames, guitarist0 blinks %d frames, expressions:%s\n", fp.c_str(),
                singerVisemeFrames, blinkFrames, ex.c_str());
    // hangar18 has a .voc (the singer sings), the drummer and bassist have no
    // lip servo; the guitarist blinks (_blinktrack) and changes expression.
    if (facePoses["singer"] == 0 || singerVisemeFrames < 1000 || facePoses["guitarist0"] == 0 || blinkFrames == 0 || expressions.size() < 3 ||
        facePoses["drummer"] != 0 || hiddenPoses) {
        std::printf("chartest: band: faces wrong (%d posed while hidden)\n", hiddenPoses);
        ++failures;
    }
    // Walks and interacts (M9.6): random walks at Okay and Great, the solo
    // walk, an interact; each walk ends near its waypoint (the plan moves
    // the target by up to its radius along the last leg, the servo then
    // regulates the rest).
    float worst = 0;
    for (float d : band.walkArrivals()) worst = std::max(worst, d);
    std::printf("chartest: band walks: %d started, %zu arrived (worst %.1f off), %d walking frames, %d interacts\n", band.walksStarted(),
                band.walkArrivals().size(), double(worst), walkFrames, band.interactsStarted());
    if (band.walksStarted() < 4 || band.walkArrivals().size() + 1 < size_t(band.walksStarted()) || worst > 60 || walkFrames < 600 ||
        band.interactsStarted() < 1) {
        std::printf("chartest: band: walks wrong\n");
        ++failures;
    }
    if (nan) {
        std::printf("chartest: band: %d non-finite transforms\n", nan);
        ++failures;
    }
    for (const auto& m : band.members())
        if (m->character) failures += drawListChecks(*m->character, "band " + m->slot, m->type == "guitarist" || m->type == "bassist");
    if (band.members().size() != 4) {
        std::printf("chartest: band: expected guitarist0, singer, bassist, drummer\n");
        ++failures;
    }
    const bool golden = int(log.size()) == kGoldenLines && h == kGoldenHash;
    std::printf("chartest: band golden: log %d lines, hash %08x (expected %d, %08x)%s\n", int(log.size()), unsigned(h), kGoldenLines,
                unsigned(kGoldenHash), golden ? "" : " MISMATCH");
    if (!golden) ++failures;
    return failures;
}

}  // namespace

int chartestBandChecks(const GameData& data) {
    int failures = loadChecks(data);
    failures += driverRun(data);
    failures += runMidiParserTest(data);
    failures += bandRun(data);
    return failures;
}
