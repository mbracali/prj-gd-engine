// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/chartest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "char/char_objects.h"
#include "data/game_data.h"
#include "scene/milo.h"

namespace {

using namespace scene;

// The character classes on the disc, by class (records; directory records
// counted apart) [C data: every milo of the extracted disc].
const std::pair<const char*, int> kCounts[] = {
    {"CharClipSamples", 1994}, {"CharClipSet (dir)", 82}, {"CharClipGroup", 330}, {"CharClipFilter", 82},
    {"CharBone", 5797},        {"Character (dir)", 107},  {"Character", 85},      {"BandCharacter (dir)", 19},
    {"BandCharacter", 16},     {"CharDriver", 58},        {"CharDriverMidi", 38}, {"CharIKHand", 38},
    {"CharIKMidi", 19},        {"CharHair", 92},          {"CharLookAt", 36},     {"CharEyes", 18},
    {"CharWeightSetter", 38},  {"CharUpperTwist", 88},    {"CharForeTwist", 79},  {"CharServoBone", 57},
    {"CharWalk", 19},          {"CharPosConstraint", 4},  {"CharIKRod", 4},       {"OutfitLoader", 20},
    {"FaceFxLipSyncServo", 22},
};

// Golden poses: m9 reference parser (clips.py --pose <set> <clip> <frame>),
// a few channels per case.
struct GoldenChannel {
    const char* milo;
    const char* clip;
    float frame;
    const char* channel;
    float v[4];
    int n;
};
const GoldenChannel kGolden[] = {
    {"char/alterna1/anims/gen/alterna1_main.milo_ps2", "idle_fast_01", 3.37f, "bone_facing.pos", {2.19538156f, -0.135224358f, 0.0f}, 3},
    {"char/alterna1/anims/gen/alterna1_main.milo_ps2", "idle_fast_01", 3.37f, "bone_pelvis.pos", {3.5227689f, -0.217338049f, 35.7133089f}, 3},
    {"char/alterna1/anims/gen/alterna1_main.milo_ps2", "idle_fast_01", 3.37f, "bone_pos_guitar.pos", {-1.61074554f, 7.47117259f, -5.39210634f}, 3},
    {"char/alterna1/anims/gen/alterna1_main.milo_ps2", "idle_fast_01", 3.37f, "bone_L-ankle.quat", {0.015015702f, -0.110443565f, 0.129505965f, 0.985294348f}, 4},
    {"char/alterna1/anims/gen/alterna1_main.milo_ps2", "idle_fast_01", 3.37f, "bone_L-clavicle.quat", {0.681468617f, 0.0895289422f, 0.721346441f, 0.0851140606f}, 4},
    {"char/alterna1/anims/gen/alterna1_main.milo_ps2", "idle_fast_01", 3.37f, "bone_L-foreArm.rotz", {-1.56074057f}, 1},
    {"char/alterna1/anims/gen/alterna1_main.milo_ps2", "idle_fast_01", 3.37f, "bone_L-hand.quat", {-0.953576014f, 0.102393154f, -0.0425097953f, 0.280002403f}, 4},
    {"char/metal_drummer/anims/gen/drummer_main.milo_ps2", "drummer_active_medium_allbeat", 1.5f, "bone_L-finger0.rotx", {-0.000610351562f}, 1},
    {"char/metal_drummer/anims/gen/drummer_main.milo_ps2", "drummer_active_medium_allbeat", 1.5f, "bone_R-finger0.rotx", {0.000610351562f}, 1},
    {"char/metal_drummer/anims/gen/drummer_main.milo_ps2", "drummer_active_medium_allbeat", 1.5f, "bone_pelvis.pos", {0.381778933f, 0.144283785f, 23.7745961f}, 3},
    {"char/metal_drummer/anims/gen/drummer_main.milo_ps2", "drummer_active_medium_allbeat", 1.5f, "bone_L-ankle.quat", {0.0204726428f, -0.000488575672f, 0.135176739f, 0.990609853f}, 4},
    {"char/metal_drummer/anims/gen/drummer_main.milo_ps2", "drummer_active_medium_allbeat", 1.5f, "bone_L-clavicle.quat", {0.756599184f, 0.0538870999f, 0.648556422f, 0.0634698564f}, 4},
    {"char/metal_drummer/anims/gen/drummer_main.milo_ps2", "drummer_active_medium_allbeat", 1.5f, "bone_L-foreArm.rotz", {-0.979367018f}, 1},
    {"char/metal_drummer/anims/gen/drummer_main.milo_ps2", "drummer_active_medium_allbeat", 1.5f, "bone_L-hand.quat", {-0.819201669f, 0.0529122519f, -0.10215441f, 0.561848196f}, 4},
    {"char/alterna1/anims/gen/alterna1_fret.milo_ps2", "finger_vibrato_index", 0.1f, "bone_fret_hand.pos", {-4.34516479f, 1.77606667f, 1.5075443f}, 3},
    {"char/alterna1/anims/gen/alterna1_fret.milo_ps2", "finger_vibrato_index", 0.1f, "bone_L-index01.quat", {-0.198401324f, 0.140049776f, 0.0725425999f, 0.96734717f}, 4},
    {"char/alterna1/anims/gen/alterna1_fret.milo_ps2", "finger_vibrato_index", 0.1f, "bone_L-index02.rotz", {1.43127444f}, 1},
    {"char/alterna1/anims/gen/alterna1_fret.milo_ps2", "finger_vibrato_index", 0.1f, "bone_L-index03.rotz", {0.795898516f}, 1},
    {"char/alterna1/anims/gen/alterna1_fret.milo_ps2", "finger_vibrato_index", 0.1f, "bone_L-middlefinger01.quat", {0.00387580241f, 0.0136110856f, 0.0158694273f, 0.999773913f}, 4},
    {"char/alterna1/anims/gen/alterna1_fret.milo_ps2", "finger_vibrato_index", 0.1f, "bone_L-middlefinger02.rotz", {0.484619141f}, 1},
    {"char/alterna1/anims/gen/alterna1_fret.milo_ps2", "finger_vibrato_index", 0.1f, "bone_L-middlefinger03.rotz", {0.524291992f}, 1},
    {"char/crowd/anims/gen/female_main.milo_ps2", "female01_idle_01", 4.0f, "bone_pelvis.pos", {3.52314625f, 2.40917733f, 33.8921089f}, 3},
    {"char/crowd/anims/gen/female_main.milo_ps2", "female01_idle_01", 4.0f, "bone_L-ankle.quat", {0.00886570712f, -0.0602593333f, 0.141408741f, 0.988075797f}, 4},
    {"char/crowd/anims/gen/female_main.milo_ps2", "female01_idle_01", 4.0f, "bone_L-clavicle.quat", {0.580141382f, 0.0607318109f, 0.81105959f, 0.0439313781f}, 4},
    {"char/crowd/anims/gen/female_main.milo_ps2", "female01_idle_01", 4.0f, "bone_L-forearm.rotz", {-0.0933836986f}, 1},
    {"char/crowd/anims/gen/female_main.milo_ps2", "female01_idle_01", 4.0f, "bone_L-hand.quat", {-0.688889231f, -0.115409967f, -0.309530194f, 0.645215643f}, 4},
    {"char/crowd/anims/gen/female_main.milo_ps2", "female01_idle_01", 4.0f, "bone_L-knee.rotz", {-0.641479492f}, 1},
    {"char/crowd/anims/gen/female_main.milo_ps2", "female01_idle_01", 4.0f, "bone_L-thigh.quat", {-0.156682641f, -0.971926777f, -0.152135359f, 0.0875426898f}, 4},
};

struct Stat {
    int decoded = 0, failed = 0;
    std::vector<std::string> failures;
};

}  // namespace

int runCharTest(const std::string& dataPath) {
    int failures = 0;
    try {
        GameData data(dataPath);
        std::vector<std::string> paths;
        for (const auto& p : data.paths())
            if (p.size() > 9 && p.compare(p.size() - 9, 9, ".milo_ps2") == 0) paths.push_back(p);
        std::sort(paths.begin(), paths.end());
        std::set<std::string> charClasses;
        for (const auto& [c, n] : kCounts) charClasses.insert(c);
        std::map<std::string, Stat> stats;
        auto note = [&](const std::string& key, bool ok, const std::string& where) {
            Stat& s = stats[key];
            if (ok) ++s.decoded;
            else {
                ++s.failed;
                if (s.failures.size() < 5) s.failures.push_back(where);
            }
        };
        int files = 0, fileFailures = 0, quats = 0, badQuats = 0, entryMismatch = 0;
        std::map<std::string, int> types;  // clip set type -> sets
        for (const auto& p : paths) {
            std::unique_ptr<Milo> milo;
            try {
                milo = std::make_unique<Milo>(data.read(p), p);
            } catch (const std::exception& e) {
                std::printf("  %s: %s\n", p.c_str(), e.what());
                ++fileFailures;
                continue;
            }
            ++files;
            std::vector<const Directory*> stack{&milo->root()};
            while (!stack.empty()) {
                const Directory* d = stack.back();
                stack.pop_back();
                for (const auto& [owner, sub] : d->subdirs) stack.push_back(sub.get());
                if (d->self && (d->self->cls == "Character" || d->self->cls == "BandCharacter")) {
                    std::string err;
                    try {
                        decodeCharacterDir(*d->self);
                    } catch (const std::exception& e) {
                        err = e.what();
                    }
                    note(d->self->cls + " (dir)", err.empty(), p + ": " + err);
                }
                if (d->self && d->self->cls == "CharClipSet") {
                    std::vector<const CharClipSamples*> clips;
                    for (const auto& o : d->objects)
                        if (o->cls == "CharClipSamples") clips.push_back(o->as<CharClipSamples>());
                    const CharClipSetData s = decodeClipSetDir(*d->self, clips.size());
                    note("CharClipSet (dir)", s.decoded, p + ": " + s.error);
                    if (s.decoded) {
                        ++types[s.type + " v" + std::to_string(s.typeVersion)];
                        for (const auto& e : s.clips) {
                            const auto* c = d->get(e.name) ? d->get(e.name)->as<CharClipSamples>() : nullptr;
                            if (!c || c->flags != e.flags) {
                                if (entryMismatch < 5) std::printf("  %s: clip entry %s disagrees with its clip\n", p.c_str(), e.name.c_str());
                                ++entryMismatch;
                            }
                        }
                    }
                }
                for (const auto& o : d->objects) {
                    if (!charClasses.count(o->cls)) continue;
                    note(o->cls, o->decoded, p + ": " + o->name + ": " + o->error);
                    if (const auto* c = o->as<CharClipSamples>(); c && c->decoded) {
                        const auto& h = c->full;
                        for (uint32_t k = 0; k < h.count; ++k)
                            for (size_t b = 0; b < h.bones.size(); ++b) {
                                if (h.bones[b].type != CharBonesSamples::kQuat) continue;
                                const float* q = h.sample(k) + h.offset[b];
                                const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
                                ++quats;
                                if (std::fabs(l - 1) > 0.01f) ++badQuats;
                            }
                    }
                }
            }
        }
        std::printf("chartest: %d milo files parsed, %d failed\n", files, fileFailures);
        failures += fileFailures;
        std::printf("class                    decoded  failed  expected\n");
        for (const auto& [cls, want] : kCounts) {
            const Stat& s = stats[cls];
            const bool ok = s.failed == 0 && s.decoded == want;
            std::printf("  %-22s %7d  %6d  %8d%s\n", cls, s.decoded, s.failed, want, ok ? "" : "  MISMATCH");
            for (const auto& f : s.failures) std::printf("      %s\n", f.c_str());
            if (!ok) ++failures;
        }
        std::printf("clip set types:");
        for (const auto& [t, n] : types) std::printf(" '%s' %d", t.c_str(), n);
        std::printf("\nclip quats: %d checked, %d not unit (>1%%)\n", quats, badQuats);
        std::printf("clip set entries disagreeing with their clips: %d\n", entryMismatch);
        if (badQuats) ++failures;
        if (entryMismatch) ++failures;

        // Golden poses.
        int goldenOk = 0, goldenBad = 0;
        MiloLibrary lib(data);
        for (const GoldenChannel& g : kGolden) {
            const CharClipSamples* c = nullptr;
            try {
                c = lib.load(g.milo).find<CharClipSamples>(g.clip);
            } catch (const std::exception&) {
            }
            bool ok = c != nullptr;
            if (c) {
                const auto pose = referencePose(*c, g.frame);
                auto it = pose.find(g.channel);
                ok = it != pose.end() && int(it->second.size()) == g.n;
                for (int i = 0; ok && i < g.n; ++i) ok = std::fabs(it->second[size_t(i)] - g.v[i]) <= 1e-4f;
            }
            if (ok) ++goldenOk;
            else {
                ++goldenBad;
                std::printf("  golden pose %s %s @ %.2f %s: MISMATCH\n", g.milo, g.clip, g.frame, g.channel);
            }
        }
        std::printf("golden poses: %d channels match the reference parser, %d do not\n", goldenOk, goldenBad);
        if (goldenBad) ++failures;

        failures += chartestBandChecks(data);
        failures += chartestCrowdChecks(data);
    } catch (const std::exception& e) {
        std::printf("chartest: %s\n", e.what());
        ++failures;
    }
    std::printf("chartest: %d failures\n", failures);
    return failures ? 1 : 0;
}
