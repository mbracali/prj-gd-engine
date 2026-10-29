// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "script/script.h"

class World;
namespace scene {
class LightPreset;
}

// GH2's lighting preset manager (WorldDir + 0x14c; Handle 0x82265010, Poll
// 0x82263070; docs/venues.md "Lighting") and LightPreset playback, on the
// world's clock. It answers the WorldDir's lighting methods (World::addNatives):
//
//   set_lighting cats [adjectives [delayed]] (0x82264EB8): a symbol or a
//     non-empty list replaces the categories (an empty list keeps them); a
//     list replaces the adjectives (anything else keeps them); the candidates
//     are rebuilt and the best one starts, delayed (default) so that its
//     fade_in ends 4 beats from now (now + 4 - fade_in / 480 beats; at once
//     when that is <= 0) or at once (FALSE, only when it is another preset);
//   set_excitement ex (0x82264F70): rebuild, switch at once if the best changed;
//   lighting / cycle_lighting / next_lighting / current_lighting;
//   lighting_next / prev / first_keyframe (0x822613A0 / 0x82261400 /
//     0x82261460 -> 0x8223FC58...): a manual preset's step, queued 4 beats
//     ahead (the TRIGGERS note's time);
//   toggle_lighting_events / ignore_lighting_events (+0x85): while set,
//     set_lighting, set_excitement and the steps are ignored.
//
// Candidates (0x82264D38 / 0x82264770): for each category in order, its
// presets with min_excitement <= ex <= max_excitement whose adjectives
// intersect the requested ones (or either list is empty); scored
// (0x82261738): category i of n -> n - i, each requested adjective j of m
// the preset has -> (m - j) * n, each preset adjective not requested -> -1;
// the top-scored ones shuffled (0x82261D38), the first wins.
//
// Playback (Poll): the current preset at weight 1 and, while its fade_in
// runs, the previous one over it at 1 - t. A preset's frame (SetFrame
// 0x82242560): automatic presets hold keyframe i for its duration and fade
// to the next over its fade_out (looping wraps to the first, else the last
// holds); manual presets step on the queued commands, starting the fade at
// the command's time - fade_out / 480 beats so that it ends on the note.
// Apply (0x82239BB8): spotlights (animate_from_preset; intensity, colour,
// rotation slerp; target and flare when the weight reaches 1, 0x82232698),
// environs (animate_from_preset: ambient, fog colour and range, 0x82238920)
// and lights (colour / range and position by their flags, 0x822387E8).
class LightPresets {
public:
    explicit LightPresets(World& world, uint32_t seed = 0x4c504d31u);
    ~LightPresets();
    LightPresets(const LightPresets&) = delete;
    LightPresets& operator=(const LightPresets&) = delete;

    // Once a frame, after World::update: due switches and steps, then the
    // presets applied to the venue's lights.
    void update();

    const scene::LightPreset* current() const { return current_; }
    int excitement() const { return excitement_; }
    // set_lighting from C++ (the same rules as the script's).
    void setLighting(const std::vector<std::string>& categories, const std::vector<std::string>* adjectives, bool delayed);
    void setExcitement(int ex);
    // ignore_lighting_events from C++ (the Enhanced Options' backdrop picks its own).
    void ignoreEvents(bool on) { ignoreEvents_ = on; }
    // Switches now (no delay), e.g. the venue viewer's preset key.
    void start(const scene::LightPreset* p);

    // Each switch, as "preset <name> (<category>)" (the golden log, GD03_WORLDLOG).
    std::function<void(const std::string&)> onSwitch;
    int switches() const { return switches_; }

private:
    struct Manual {
        int index = 0, prev = -1;
        float blendStart = 0, blendLen = 0;  // frames
        double startBeat = 0;
    };
    struct Step {
        int cmd;      // 0 next, 1 prev, 2 first
        double beat;  // when the step's fade ends
    };
    struct Pending {
        const scene::LightPreset* preset;
        double beat;
    };

    bool native(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r);
    void rebuild();
    int score(const scene::LightPreset& p) const;
    void choose(bool delayed);
    void switchTo(const scene::LightPreset* p);
    double now(const scene::LightPreset& p) const;
    float frameOf(const scene::LightPreset& p, double start) const;
    // The blended keyframe of `p` at `frame` (`kfA` -> `kfB` by `t`).
    void keyframeAt(const scene::LightPreset& p, float frame, int& a, int& b, float& t);
    void apply(const scene::LightPreset& p, int a, int b, float t, float weight);
    void queueStep(int cmd);
    uint32_t random();

    World& world_;
    std::map<std::string, std::vector<const scene::LightPreset*>> byCategory_;
    std::vector<const scene::LightPreset*> all_;
    std::vector<std::string> categories_, adjectives_;
    std::vector<const scene::LightPreset*> candidates_;
    int excitement_ = 2;  // kExcitementOkay (WORLD_OBJECT_BASE's start)
    bool ignoreEvents_ = false;
    const scene::LightPreset *current_ = nullptr, *previous_ = nullptr;
    double currentStart_ = 0, previousStart_ = 0;  // in each preset's units
    double currentStartSeconds_ = 0;
    std::deque<Pending> pending_;
    std::deque<Step> steps_;
    std::unordered_map<const scene::LightPreset*, Manual> manual_;
    size_t cycle_ = 0;
    uint32_t rng_;
    int switches_ = 0;
};

namespace lighting {
// [lighting (x y)] -> adjectives; tolerant of GH2's chart typos
// ("lighting(blackout)", "lighting (strobe) ", missing spaces / parentheses).
bool parseEvent(const std::string& text, std::vector<std::string>& adjectives);
}  // namespace lighting
