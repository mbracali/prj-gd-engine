// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "data/bytes.h"

// A character animation clip (.acp, AnimClipSamples; see FINDINGS.md
// "Character animation" and tools/char_anim.py). Frames are beats at the
// clip's authored tempo; samples are spread evenly over [start, end].
class AnimClip {
public:
    // Throws std::runtime_error on malformed data.
    explicit AnimClip(const Bytes& data);

    const std::string& name() const { return name_; }
    float startFrame() const { return start_; }
    float endFrame() const { return end_; }
    float framesPerSec() const { return fps_; }
    float blendWidth() const { return blendWidth_; }
    uint32_t playFlags() const { return playFlags_; }

    enum class Kind { Pos, Quat, RotX, RotY, RotZ };
    struct Value {
        const std::string* bone;  // e.g. "bone_pelvis.mesh"
        Kind kind;
        std::array<float, 4> v;   // pos xyz / quat xyzw / angle in v[0]
    };
    // Every channel's value at `frame` (FUN_0017ace8: clamped, linear).
    void sample(float frame, std::vector<Value>& out) const;

private:
    struct Set {
        std::vector<std::string> pos, quat, rotz;  // bone object names (rotz: every .rot<axis>)
        std::vector<Kind> rotKind;
        int samples = 0;
        std::vector<float> data;  // per sample: pos xyz..., quat xyzw..., rotz...
        int stride() const { return int(pos.size() * 3 + quat.size() * 4 + rotz.size()); }
    };
    void readHeader(ByteReader& r, Set& s, uint32_t version);
    void readSamples(ByteReader& r, Set& s);
    void sampleSet(const Set& s, float frame, std::vector<Value>& out) const;

    std::string name_;
    float start_ = 0, end_ = 1, fps_ = 1.6666666f, blendWidth_ = 0;
    uint32_t playFlags_ = 0;
    std::vector<bool> compressed_;
    Set full_, one_;
};
