// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/sound_buss.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "data/qbkey.h"

namespace {
// vol (dB) -> percent / 100: 100 * 2^(v / 6), at most 400 %, 0 at -100 or below [C 0x8217AFB0].
float volGain(float v) { return v <= -100.0f ? 0.0f : std::min(4.0f, std::exp2(v / 6.0f)); }
// pitch (semitones) -> ratio: 2^(p / 12), at most 16, 0 at -48 or below [C 0x8217B0B8].
float pitchRatio(float p) { return p <= -48.0f ? 0.0f : std::min(16.0f, std::exp2(p / 12.0f)); }
}  // namespace

SoundBusses& soundBusses() {
    static SoundBusses b;
    return b;
}

void SoundBusses::add(uint32_t name, uint32_t parent) {
    if (index_.count(name) || int(busses_.size()) >= kMax) return;
    Buss b;
    b.name = name;
    if (auto it = index_.find(parent); it != index_.end()) b.parent = it->second;
    index_[name] = int(busses_.size());
    busses_.push_back(b);
    created_ = true;
}

int SoundBusses::index(uint32_t buss) const {
    std::lock_guard<std::mutex> g(m_);
    auto it = index_.find(buss);
    return it == index_.end() ? -1 : it->second;
}

bool SoundBusses::under(uint32_t buss, uint32_t ancestor) const {
    if (buss == ancestor) return true;
    std::lock_guard<std::mutex> g(m_);
    auto a = index_.find(ancestor), b = index_.find(buss);
    if (a == index_.end() || b == index_.end()) return false;
    for (int k = b->second, guard = 0; k >= 0 && guard < kMax; k = busses_[size_t(k)].parent, ++guard)
        if (k == a->second) return true;
    return false;
}

float SoundBusses::volDb(uint32_t buss) const {
    std::lock_guard<std::mutex> g(m_);
    auto it = index_.find(buss);
    return it == index_.end() ? 0.0f : busses_[size_t(it->second)].vol;
}

void SoundBusses::set(uint32_t buss, const float* volDb, const float* pitchSemis, float timeS) {
    std::lock_guard<std::mutex> g(m_);
    auto it = index_.find(buss);
    if (it == index_.end()) return;
    Buss& b = busses_[size_t(it->second)];
    // GD04_BUSSLOG=1: every buss change (development aid).
    static const bool log = SDL_getenv("GD04_BUSSLOG") != nullptr;
    if (log)
        SDL_Log("buss: %s vol %s pitch %s time %.2f%s", qbkey::name(buss).c_str(), volDb ? std::to_string(*volDb).c_str() : "-",
                pitchSemis ? std::to_string(*pitchSemis).c_str() : "-", double(timeS), b.locked ? " (locked: ignored)" : "");
    if (b.locked) return;  // SoundBussLock: the scripts unlock, set, lock again [C scripts]
    // A field not given keeps its target (a fade in progress goes on to it).
    b.volTo = volDb ? *volDb : (b.left > 0 ? b.volTo : b.vol);
    b.pitchTo = pitchSemis ? *pitchSemis : (b.left > 0 ? b.pitchTo : b.pitch);
    const double ms = std::max(0.0f, timeS) * 1000.0;
    if (ms <= 0) {
        b.vol = b.volTo, b.pitch = b.pitchTo, b.left = 0;
    } else {
        b.left = ms;
        b.volStep = float((b.volTo - b.vol) / ms), b.pitchStep = float((b.pitchTo - b.pitch) / ms);
    }
    recompute();
}

void SoundBusses::lock(uint32_t buss, bool locked) {
    std::lock_guard<std::mutex> g(m_);
    if (auto it = index_.find(buss); it != index_.end()) busses_[size_t(it->second)].locked = locked;
}

void SoundBusses::push() {
    std::lock_guard<std::mutex> g(m_);
    std::vector<std::pair<float, float>> s;
    for (const Buss& b : busses_) s.push_back({b.left > 0 ? b.volTo : b.vol, b.left > 0 ? b.pitchTo : b.pitch});
    pushed_.push_back(std::move(s));
}

void SoundBusses::pop() {
    std::lock_guard<std::mutex> g(m_);
    if (pushed_.empty()) return;
    const auto s = std::move(pushed_.back());
    pushed_.pop_back();
    for (size_t i = 0; i < s.size() && i < busses_.size(); ++i) busses_[i].vol = s[i].first, busses_[i].pitch = s[i].second, busses_[i].left = 0;
    recompute();
}

void SoundBusses::update(double dtMs) {
    std::lock_guard<std::mutex> g(m_);
    bool moved = false;
    for (Buss& b : busses_) {
        if (b.left <= 0) continue;
        const double step = std::min(dtMs, b.left);
        b.left -= step;
        if (b.left <= 0) b.vol = b.volTo, b.pitch = b.pitchTo;
        else b.vol += float(b.volStep * step), b.pitch += float(b.pitchStep * step);
        moved = true;
    }
    if (moved) recompute();
}

void SoundBusses::recompute() {
    for (size_t i = 0; i < busses_.size(); ++i) {
        float gn = 1, pr = 1;
        int guard = 0;
        for (int k = int(i); k >= 0 && guard < kMax; k = busses_[size_t(k)].parent, ++guard)
            gn *= volGain(busses_[size_t(k)].vol), pr *= pitchRatio(busses_[size_t(k)].pitch);
        gain_[i].store(gn, std::memory_order_relaxed);
        pitch_[i].store(pr, std::memory_order_relaxed);
    }
}
