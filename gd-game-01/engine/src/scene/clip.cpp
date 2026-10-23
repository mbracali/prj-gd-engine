// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/clip.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {

std::string readString(ByteReader& r) { return r.str(r.u32le()); }

// FUN_00188790: the letter after a '.' picks pos / quat / rotz.
int channelKind(const std::string& name) {
    for (size_t i = name.find('.'); i != std::string::npos; i = name.find('.', i + 1)) {
        char c = i + 1 < name.size() ? name[i + 1] : 0;
        if (c == 'p') return 0;
        if (c == 'q') return 1;
        if (c == 'r') return 2;
    }
    return 3;
}

// "bone_x.quat" -> "bone_x.mesh"
std::string boneName(const std::string& channel) { return channel.substr(0, channel.rfind('.')) + ".mesh"; }

}  // namespace

AnimClip::AnimClip(const Bytes& data) {
    ByteReader r(data);
    readString(r);  // class
    name_ = readString(r);
    uint32_t v = r.u32le();
    start_ = r.f32le();
    end_ = r.f32le();
    if (v >= 2) fps_ = r.f32le();
    if (v > 3) r.u32le();  // flags
    if (v > 16) {
        playFlags_ = r.u32le();
        blendWidth_ = r.f32le();
    }
    uint32_t sv = r.u32le();
    readHeader(r, full_, sv);
    readHeader(r, one_, sv);
    readSamples(r, full_);
    readSamples(r, one_);
    if (r.remaining()) throw std::runtime_error("clip: trailing bytes");
}

void AnimClip::readHeader(ByteReader& r, Set& s, uint32_t version) {
    uint32_t n = r.u32le();
    std::vector<std::string> channels(n);
    for (auto& c : channels) c = readString(r);
    s.samples = static_cast<int>(r.u32le());
    compressed_.push_back(version > 3 && r.u32le() != 0);
    // Samples store every .pos channel, then every .quat, then every .rotz.
    for (const std::string& c : channels) {
        switch (channelKind(c)) {
        case 0: s.pos.push_back(boneName(c)); break;
        case 1: s.quat.push_back(boneName(c)); break;
        case 2: {
            s.rotz.push_back(boneName(c));
            char axis = c.back();
            s.rotKind.push_back(axis == 'x' ? Kind::RotX : axis == 'y' ? Kind::RotY : Kind::RotZ);
            break;
        }
        default: throw std::runtime_error("clip: unknown channel " + c);
        }
    }
}

// FUN_0017c358
void AnimClip::readSamples(ByteReader& r, Set& s) {
    bool comp = compressed_[&s == &full_ ? 0 : 1];
    s.data.reserve(size_t(s.samples) * s.stride());
    auto s16 = [&] { return static_cast<float>(static_cast<int16_t>(r.u16le())); };
    for (int i = 0; i < s.samples; ++i) {
        for (size_t c = 0; c < s.pos.size(); ++c)
            for (int k = 0; k < 3; ++k) s.data.push_back(r.f32le());
        for (size_t c = 0; c < s.quat.size(); ++c)
            for (int k = 0; k < 4; ++k) s.data.push_back(comp ? s16() / 32767.0f : r.f32le());
        for (size_t c = 0; c < s.rotz.size(); ++c) s.data.push_back(comp ? s16() / 1638.4f : r.f32le());
    }
}

void AnimClip::sample(float frame, std::vector<Value>& out) const {
    out.clear();
    sampleSet(full_, frame, out);
    sampleSet(one_, frame, out);
}

void AnimClip::sampleSet(const Set& s, float frame, std::vector<Value>& out) const {
    if (s.samples <= 0) return;
    float t = end_ > start_ ? std::clamp((frame - start_) / (end_ - start_), 0.0f, 1.0f) : 0.0f;
    float x = t * float(s.samples - 1);
    int i0 = std::min(static_cast<int>(x), s.samples - 1), i1 = std::min(i0 + 1, s.samples - 1);
    float f = x - float(i0);
    const float* a = &s.data[size_t(i0) * s.stride()];
    const float* b = &s.data[size_t(i1) * s.stride()];
    auto lerp = [&](int k) { return a[k] + (b[k] - a[k]) * f; };
    int k = 0;
    for (const std::string& bone : s.pos) {
        out.push_back({&bone, Kind::Pos, {lerp(k), lerp(k + 1), lerp(k + 2), 0}});
        k += 3;
    }
    for (const std::string& bone : s.quat) {
        Value v{&bone, Kind::Quat, {lerp(k), lerp(k + 1), lerp(k + 2), lerp(k + 3)}};
        float len = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2] + v.v[3] * v.v[3]);
        if (len > 0)
            for (float& c : v.v) c /= len;
        out.push_back(v);
        k += 4;
    }
    for (size_t i = 0; i < s.rotz.size(); ++i) {
        out.push_back({&s.rotz[i], s.rotKind[i], {lerp(k), 0, 0, 0}});
        ++k;
    }
}
