// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "char/char_objects.h"

// GH2's CharBones: the named channel buffer the clips blend into and a
// CharServoBone poses its bones from (docs/band.md "Pose") [C]:
// - a channel is "bone_<x>.<type>" (pos, scale, quat, rotx/y/z, drotx/y/z);
//   the bone it moves is "bone_<x>.trans", else "bone_<x>.mesh" (0x82192DD8);
// - a CharClipSet lists its skeleton (CharBone objects) and, when its clips
//   move the root, bone_facing.pos / .rotz and bone_facing_delta.pos / .rotz
//   (0x821674D0); the drivers writing into a servo add their sets' channels;
// - each channel keeps an accumulated weight next to its value (0x8215DF28);
// - ScaleAdd (0x82162570): the clip's static set (sample 0) and its full set
//   at t = (frame - start) / (end - start) (clamped; x = t (n - 1), floor and
//   ceil samples added with weights (1 - f) w and f w, 0x821A1DB0 /
//   0x821A1938), the delta set between t - dframe / length and t, and the
//   root delta (bone_facing at t minus at t0, the position turned into the
//   t0 facing, 0x82162378) into bone_facing_delta; quaternions are added
//   sign-aligned to the accumulator (0x8215DF28) and normalised when posed;
// - ScaleDown: the clip's channels times a factor (before the newest clip of
//   a driver adds, by 1 - its chain weight x the driver's weight).
namespace chr {

class Bones {
public:
    struct Channel {
        std::string name;   // "bone_pelvis.quat"
        int type = scene::CharBonesSamples::kNone;
        float v[4] = {0, 0, 0, 0};
        float weight = 0;
    };
    // Adds a channel (no-op when present); returns its index.
    int add(const std::string& name);
    int find(const std::string& name) const;
    const std::vector<Channel>& channels() const { return ch_; }
    std::vector<Channel>& channels() { return ch_; }
    float* value(int i) { return ch_[size_t(i)].v; }

    // Adds `w` x the clip's pose at `frame` (and the root delta over the
    // last `dframe` frames).
    void scaleAdd(const scene::CharClipSamples& clip, float w, float frame, float dframe);
    // The clip's channels times `f`.
    void scaleDown(const scene::CharClipSamples& clip, float f);
    // Every channel of a set (adds them): a clip set's skeleton.
    void addChannelsOf(const scene::CharBonesSamples& set);

private:
    const std::vector<int>& map(const scene::CharBonesSamples& set);
    void addSample(const scene::CharBonesSamples& set, const std::vector<int>& idx, uint32_t sample, float w);
    std::vector<Channel> ch_;
    std::unordered_map<std::string, int> index_;
    std::unordered_map<const scene::CharBonesSamples*, std::vector<int>> maps_;
    int facingDeltaPos_ = -2, facingDeltaRot_ = -2;
};

// The value of a set's channels at t (0..1) interpolated (0x821A1A58);
// `out` gets 3 / 4 / 1 floats per channel at the set's offsets.
void sampleSet(const scene::CharBonesSamples& set, float t, std::vector<float>& out);
// Clip time 0..1 of a frame.
float clipT(const scene::CharClipSamples& clip, float frame);

}  // namespace chr
