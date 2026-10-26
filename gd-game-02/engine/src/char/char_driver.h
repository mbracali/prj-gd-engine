// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "char/char_objects.h"
#include "char/clip.h"
#include "script/random_gen.h"

namespace scene {
class Milo;
}

// GH2's clip playback (docs/band.md "CharDriver") [C unless noted]:
// a CharClipSet at run time (ClipSet) and a CharDriver's chain of
// CharClipDriver nodes (ClipDriver), ported from the Xbox 360 code.
namespace chr {

// Flags (config/gen/macros.dta) [C data].
enum PlayFlags : uint32_t {
    kPlayNow = 1, kPlayNoBlend = 2, kPlayFirst = 3, kPlayLast = 4, kPlayDirty = 8,
    kPlayNoLoop = 0x10, kPlayLoop = 0x20, kPlayGraphLoop = 0x30, kPlayNodeLoop = 0x40,
    kPlayRealTime = 0x200, kPlayUserTime = 0x400,
    kPlayBeatAlign1 = 0x1000, kPlayBeatAlign2 = 0x2000, kPlayBeatAlign4 = 0x4000, kPlayBeatAlign8 = 0x8000,
};

// GH2's random numbers (RandomGen, script/random_gen.h) [C]. GH2 has one
// global generator; gd02 gives the band its own instance so that other
// systems' draws do not move the band's picks (docs/band.md).
using Random = ::RandomGen;

// A CharClipSet milo at run time: its clips (with the run-time flags the
// game ORs in), groups, the least-recently-used list (+0x8c) and the
// clips deleted by filter_clips.
class ClipSet {
public:
    explicit ClipSet(scene::Milo& milo);
    const std::string& path() const { return path_; }
    const scene::CharClipSetData& data() const { return data_; }
    scene::Milo& milo() { return milo_; }

    scene::CharClipSamples* clip(const std::string& name) const;
    scene::CharClipGroup* group(const std::string& name) const;
    bool groupHas(const scene::CharClipGroup& g, const scene::CharClipSamples* c) const;  // 0x82175F10
    const std::vector<scene::CharClipSamples*>& clips() const { return clips_; }
    const std::vector<scene::CharClipGroup*>& groups() const { return groups_; }
    bool alive(const scene::CharClipSamples* c) const { return c && !deleted_.count(c); }

    // 0x82165038: the first LRU clip with all of `flags`, moved to the back.
    scene::CharClipSamples* lruByFlags(uint32_t flags);
    // 0x8216C0A0 with matcher 0x8216A498 (play_if_safe).
    scene::CharClipSamples* lruSafe(uint32_t flags, uint32_t safeFlags, float length);
    // CharClipGroup::NextClip 0x82175AB8: round robin (index first incremented).
    scene::CharClipSamples* next(scene::CharClipGroup& g);
    // 0x82176550: from the clip after the current one, round, the first with
    // all of `flags`; it becomes the current one (null when none).
    scene::CharClipSamples* nextMatching(scene::CharClipGroup& g, uint32_t flags);
    // 0x82176238: every group shuffled (swap(i, RandomInt(i, n))).
    void shuffleGroups(Random& rng);
    // 0x82175C58: ORs `flags` into every clip of group `g`.
    void orGroupFlags(const std::string& g, uint32_t flags);
    // delete_domain_any (0x82165598): a clip is "in" when (flags & a) != 0 or
    // (flags & b) == 0; the clips whose "in" differs from `keep` are deleted.
    void deleteDomainAny(uint32_t a, uint32_t b, bool keep);
    void deleteClip(scene::CharClipSamples* c);
    void deleteGroupClips(const std::string& g);
    // Keeps `n` clips of a group (0x82175B20 / 0x8219C368: those marked keep, play flags
    // 0x10000, then the last others, which get the mark); 0 deletes them all.
    void trimGroup(const std::string& g, size_t n);
    // The group's live clips.
    std::vector<scene::CharClipSamples*> groupClips(const std::string& g) const;
    // The servo channels the set's drivers write (0x821674D0): its skeleton
    // (CharBone entries) or, without one, every clip channel, plus the root
    // channels (bone_facing.pos / .rotz, and with `delta` bone_facing_delta)
    // when its clips carry bone_facing.
    void listChannels(Bones& bones, bool delta) const;
    bool hasFacing() const { return hasFacing_; }

private:
    scene::Milo& milo_;
    std::string path_;
    scene::CharClipSetData data_;
    std::vector<scene::CharClipSamples*> clips_;
    std::vector<scene::CharClipGroup*> groups_;
    std::unordered_map<std::string, scene::CharClipSamples*> byName_;
    std::unordered_map<std::string, scene::CharClipGroup*> groupByName_;
    std::unordered_map<const scene::CharClipGroup*, int> groupIndex_;
    std::unordered_map<const scene::CharClipGroup*, std::vector<std::string>> groupOrder_;
    std::list<scene::CharClipSamples*> lru_;
    std::unordered_map<const scene::CharClipSamples*, bool> deleted_;
    bool hasFacing_ = false;
};

// What a play / play_group argument names (0x82165460 / 0x8219C8B0).
struct ClipSel {
    enum Kind { Flags, Name, List } kind = Flags;
    uint32_t flags = 0;
    std::string name;            // a clip or a group
    std::vector<ClipSel> list;   // any of
};

// A CharDriver (Poll 0x8216AF70, Handle 0x8216CAB0) with its chain of
// CharClipDriver nodes (ctor 0x821A4BA8, advance 0x821A4E30, blend
// 0x821A4988).
class ClipDriver {
public:
    struct Node {
        uint32_t flags = 0;       // [0] play flags (the clip's, the call's non-zero fields over them)
        float blendWidth = 0;     // [1] clip +0x30
        float rate = 1;           // [2]
        float delay = 0;          // [3] frames of the older clip before this one starts
        float frame = 0;          // [4]
        float advanced = 0;       // [5] frames advanced this poll (without the align correction)
        float weight = 0;         // [6] blend progress 0..1
        float advancedAll = 0;    // [7] frames advanced this poll with the correction
        float blendWeight = 0;    // [8] its share in the last blend
        scene::CharClipSamples* clip = nullptr;  // [9]
        float nextEvent = -1e30f; // [12]
        int eventIndex = -1;      // [13]
    };

    ClipDriver(std::string name, ClipSet* set);
    const std::string& name() const { return name_; }
    ClipSet* clipSet() const { return set_; }
    void setClipSet(ClipSet* set) { set_ = set; }

    // Driver properties.
    float beatScale = 1;          // +0x58
    bool realign = false;         // +0x54
    bool skipScaleDown = false;   // +0x55
    std::string starved;          // +0x44: the message sent while one node remains
    // The weight (CharWeightable; the weight owner's when it has one).
    std::function<float()> weight = [] { return 1.0f; };
    // Clip scripts (enter / exit / events) run with $dude = the character.
    std::function<void(const std::string& script)> runScript;
    // The clip whose script runs (0x8219BFF0 runs it with $this = the clip).
    const scene::CharClipSamples* scriptClip = nullptr;
    // The starved message to the character.
    std::function<void(const std::string& msg)> sendStarved;
    // Random phases (clips with a range).
    Random* rng = nullptr;
    // Each node started (the golden logs).
    std::function<void(const scene::CharClipSamples& clip, uint32_t flags)> onPlay;

    // play (0x8216B468 / 0x8216AB00 -> 0x8216ABF8): the clip for `sel`
    // (LRU by flags, a clip, a group's next) with `flags`. Returns the clip
    // or nullptr (a warning).
    scene::CharClipSamples* play(const ClipSel& sel, uint32_t flags);
    scene::CharClipSamples* playClip(scene::CharClipSamples* clip, uint32_t flags);
    // play_group (0x8216B4F0 -> 0x8216AE18).
    scene::CharClipSamples* playGroup(const std::string& group, uint32_t flags);
    // play_if_safe (0x8216C580 -> 0x8216C3A8).
    scene::CharClipSamples* playIfSafe(uint32_t flags, uint32_t playFlags, uint32_t safeFlags, float length);
    // CharDriverMidi: a clip starting at `frame` after `delay` (0x821A4BA8
    // with explicit frame / delay: weight 0).
    scene::CharClipSamples* playAt(scene::CharClipSamples* clip, uint32_t flags, float frame, float delay, float blendWidth = -1);
    // set_beat_scale (0x8216AE90).
    void setBeatScale(float scale);
    // offset: adds to the newest node's frame.
    void offset(float frames);

    // Poll: advance at song beat `beat` by `dBeats` / `dSeconds`, then
    // blend into `bones` (when given).
    void poll(double beat, float dBeats, float dSeconds, Bones* bones);

    // Queries.
    // 0x8216A588: the eased weight of the chain's clips with any of `flags`.
    float flagWeight(uint32_t flags) const;
    // 0x8216A6E0: the newest clip whose node has started (weight != 0).
    scene::CharClipSamples* firstPlaying() const;
    uint32_t firstFlags() const;          // its flags (0 when none)
    scene::CharClipSamples* newest() const { return chain_.empty() ? nullptr : chain_.back().clip; }
    const std::vector<Node>& chain() const { return chain_; }  // oldest first
    bool settled() const { return chain_.size() <= 1; }
    // 0x8216B3A0: the node (chain index) with the largest share of the
    // blend; -1 when the chain is empty.
    int dominant() const;
    // 0x8216A690: the newest node that has started (weight != 0), or -1.
    int newestStarted() const;
    std::vector<Node>& chainNodes() { return chain_; }
    // Drops the newest node when it has not started (0x8216A520).
    void dropUnstarted();
    void clear() { chain_.clear(); }

private:
    void fire(const scene::CharClipSamples* clip, const std::string& script);
    Node* push(scene::CharClipSamples* clip, uint32_t flags, float frame, float delay, bool explicitStart);
    void advance(size_t i, double beat, float dBeats, float dSeconds, float& total);
    float alignCorrection(const Node& n, double beat) const;
    void runEvents(Node& n, float from, float to);
    void deleteOlder(size_t keep);
    scene::CharClipSamples* resolve(const ClipSel& sel);

    std::string name_;
    ClipSet* set_ = nullptr;
    std::vector<Node> chain_;  // oldest first; back() is the newest (+0x34)
    ClipSel lastSel_;          // +0x48
    bool hasLastSel_ = false;
    float lastBeat_ = 1e30f;   // +0x50
};

// A transition from `from` at `frame` to `to` (0x8219E598) [C]: with
// kPlayFirst the first edge of from's table to `to` at or after the frame
// (0x8219E4A8), with kPlayLast the last (0x8219E520); otherwise, or without
// one, the default: src = the frame (kPlayLast: not before from's end less
// half to's blend width), dest = to's start at the same phase of to's beat
// align. kPlayNoBlend: none (false).
bool transition(const scene::CharClipSamples& from, float frame, const scene::CharClipSamples& to, uint32_t flags, float& src, float& dest);
// 0x8219E520 alone: the last edge at or after the frame (false when none).
bool lastEdge(const scene::CharClipSamples& from, float frame, const scene::CharClipSamples& to, float& src, float& dest);

// "kPlayLast", "{| kPlayLast kPlayGraphLoop}" values are ints by the time
// they reach the natives; this turns a script value into a ClipSel.
uint32_t clipFlagsByName(const std::string& macro);

}  // namespace chr
