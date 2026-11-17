// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/beatmatch.h"

#include <cmath>
#include <cstring>
#include <map>
#include <unordered_set>

#include "data/str_file.h"

namespace gd24 {

namespace {
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
float f32(const uint8_t* p) {
    const uint32_t u = le32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
}  // namespace

int GemList::chords() const {
    int n = 0;
    for (const Gem& g : gems) n += (g.lanes & (g.lanes - 1)) != 0;
    return n;
}

bool GemList::twoLane() const {
    for (const Gem& g : gems)
        if (g.lanes & ~6u) return false;
    return true;
}

bool GemList::offGrid() const {
    for (const Gem& g : gems)
        if (g.tick % 15) return true;  // 480 / 32
    return false;
}

bool Beatmatch::parse(std::vector<uint8_t> bytes, std::string* error) {
    file = std::move(bytes);
    StrFile f;
    if (!f.parse(file.data(), std::min<size_t>(file.size(), 0x10000), file.size(), error)) return false;
    images.clear();
    for (const StrChunk& c : f.chunks) {
        if (c.index < 125 || c.index > 128 || c.size < 0x300) continue;
        BeatmatchImage im;
        im.entry = c.index, im.offset = c.offset, im.size = c.size;
        im.difficulty = le32(file.data() + c.offset + 0x2F4);
        images.push_back(im);
    }
    if (images.size() != 4) {
        if (error) *error = "expected the four difficulty images (entries 125..128), found " + std::to_string(images.size());
        return false;
    }
    return true;
}

void Beatmatch::kPadSymbolsAt(const BeatmatchImage& im, int& pointed, int& total) const {
    const uint8_t* m = image(im);
    std::unordered_set<uint32_t> words;
    for (uint32_t o = 0; o + 4 <= im.size; o += 4) words.insert(le32(m + o));
    pointed = total = 0;
    static const char kPad[] = "kPad_";
    for (uint32_t o = 1; o + 6 < im.size; ++o) {
        if (m[o - 1] != 0 || std::memcmp(m + o, kPad, 5) != 0) continue;
        ++total;
        pointed += words.count(kBase + o) > 0;
    }
}

std::vector<GemList> Beatmatch::readGemLists(const BeatmatchImage& im, size_t minGems) const {
    const uint8_t* m = image(im);
    const uint32_t n = im.size;
    auto plausible = [&](uint32_t o) {
        if (o + 20 > n) return false;
        const float t = f32(m + o);
        return std::isfinite(t) && t > 0 && t < 600000 && le32(m + o + 4) < 2000000 && m[o + 14] >= 1 && m[o + 14] <= 15 &&
               m[o + 12] < 128;
    };
    std::vector<GemList> lists;
    for (uint32_t o = 0; o + 40 <= n;) {
        if (!plausible(o) || !plausible(o + 20)) {
            o += 4;
            continue;
        }
        uint32_t p = o;
        while (plausible(p + 20) && le32(m + p + 24) >= le32(m + p + 4) && f32(m + p + 20) >= f32(m + p)) p += 20;
        const size_t count = (p - o) / 20 + 1;
        if (count < minGems) {
            o += 4;
            continue;
        }
        GemList l;
        l.address = kBase + o;
        for (uint32_t r = o; r <= p; r += 20) {
            Gem g;
            g.ms = f32(m + r), g.tick = le32(m + r + 4);
            g.a = uint16_t(m[r + 8] | m[r + 9] << 8), g.b = uint16_t(m[r + 10] | m[r + 11] << 8);
            g.id = m[r + 12], g.lanes = m[r + 14], g.flags = m[r + 15], g.flags2 = le32(m + r + 16);
            l.gems.push_back(g);
        }
        lists.push_back(std::move(l));
        o = p + 20;
    }
    return lists;
}

std::vector<Beatmatch::Track> Beatmatch::readTracks(const BeatmatchImage& im, std::string* error) const {
    const uint8_t* m = image(im);
    const uint32_t n = im.size;
    auto inImage = [&](uint32_t a) { return a >= kBase && a < kBase + n; };
    auto validGem = [&](uint32_t o) {
        if (o + 20 > n) return false;
        const float t = f32(m + o);
        return std::isfinite(t) && t >= 0 && t < 3600000 && m[o + 14] <= 15;
    };
    // The track names: "PART ..." strings, and the words that point to them.
    std::vector<std::pair<uint32_t, std::string>> namePtrs;  // (word offset, name)
    std::unordered_set<uint32_t> nameAt;
    for (uint32_t o = 1; o + 6 < n; ++o)
        if (m[o - 1] == 0 && std::memcmp(m + o, "PART ", 5) == 0) nameAt.insert(o);
    // The gem vectors {pointer, count, capacity == count}.
    struct Vec {
        uint32_t at, ptr, count, tag;
    };
    std::vector<Vec> vecs;
    for (uint32_t o = 0; o + 12 <= n; o += 4) {
        const uint32_t w = le32(m + o);
        if (!inImage(w)) continue;
        if (nameAt.count(w - kBase)) {
            namePtrs.push_back({o, std::string(reinterpret_cast<const char*>(m + (w - kBase)))});
            continue;
        }
        const uint32_t cnt = le32(m + o + 4);
        // A difficulty's gem vector follows a small number, the same for
        // all of a song's (0xAA in ABC, 0x5A in WOULD) [C data, all 41
        // songs]; the other 20-byte vectors near it (a few records, e.g.
        // phrase spans [I]) follow 0x7F800001 or another vector's capacity.
        if (o < 8) continue;
        const uint32_t tag = le32(m + o - 4);
        if (tag == 0 || tag > 0xFFFF || le32(m + o - 8) == tag) continue;
        if (cnt == 0 || cnt > 20000 || le32(m + o + 8) != cnt) continue;
        const uint32_t g = w - kBase;
        if (uint64_t(g) + uint64_t(cnt) * 20 > n || !validGem(g) || !validGem(g + (cnt - 1) * 20)) continue;
        bool ascending = true;
        for (uint32_t k = 1; k < cnt && ascending; ++k) ascending = le32(m + g + k * 20 + 4) >= le32(m + g + (k - 1) * 20 + 4);
        if (ascending) vecs.push_back({o, w, cnt, le32(m + o - 4)});
    }
    // The song's tag: the most common one among the candidates.
    std::map<uint32_t, int> tags;
    for (const Vec& v : vecs) ++tags[v.tag];
    uint32_t songTag = 0;
    for (const auto& [t, c] : tags)
        if (!songTag || c > tags[songTag]) songTag = t;
    std::vector<Track> tracks;
    std::map<std::string, size_t> byName;
    for (const Vec& v : vecs) {
        if (v.tag != songTag) continue;
        // The nearest track name pointer before the vector.
        const std::pair<uint32_t, std::string>* owner = nullptr;
        for (const auto& np : namePtrs)
            if (np.first < v.at && (!owner || np.first > owner->first)) owner = &np;
        if (!owner || v.at - owner->first > 0x800) continue;
        auto it = byName.find(owner->second);
        if (it == byName.end()) it = byName.emplace(owner->second, tracks.size()).first, tracks.push_back({owner->second, {}});
        GemList l;
        l.address = v.ptr;
        for (uint32_t k = 0, r = v.ptr - kBase; k < v.count; ++k, r += 20) {
            Gem gm;
            gm.ms = f32(m + r), gm.tick = le32(m + r + 4);
            gm.a = uint16_t(m[r + 8] | m[r + 9] << 8), gm.b = uint16_t(m[r + 10] | m[r + 11] << 8);
            gm.id = m[r + 12], gm.lanes = m[r + 14], gm.flags = m[r + 15], gm.flags2 = le32(m + r + 16);
            l.gems.push_back(gm);
        }
        tracks[it->second].difficulty.push_back(std::move(l));
    }
    for (const Track& t : tracks)
        if (t.difficulty.size() != 4 && error) *error += t.name + " has " + std::to_string(t.difficulty.size()) + " gem lists; ";
    return tracks;
}

std::vector<TimeSig> Beatmatch::readTimeSigs(const BeatmatchImage& im) const {
    const uint8_t* d = image(im);
    const uint32_t n = im.size & ~3u;
    std::vector<TimeSig> best;
    for (uint32_t o = 0; o + 12 <= n; o += 4) {
        const uint32_t ptr = le32(d + o), count = le32(d + o + 4);
        if (count == 0 || count > 400 || le32(d + o + 8) != count || ptr < kBase || (ptr & 3)) continue;
        const uint64_t rel = uint64_t(ptr) - kBase;
        if (rel + uint64_t(count) * 16 > n || count <= best.size()) continue;
        std::vector<TimeSig> v;
        bool ok = true;
        for (uint32_t k = 0; k < count && ok; ++k) {
            const uint8_t* r = d + rel + 16 * k;
            TimeSig t{int(le32(r)), int(le32(r + 4)), int(le32(r + 8)), le32(r + 12)};
            ok = t.num >= 1 && t.num <= 16 && (t.den == 2 || t.den == 4 || t.den == 8 || t.den == 16);
            if (ok && k == 0) ok = t.bar == 0 && t.tick == 0;
            if (ok && k > 0) {
                const TimeSig& p = v.back();
                ok = t.bar > p.bar && int64_t(t.tick) - int64_t(p.tick) == int64_t(t.bar - p.bar) * p.num * 1920 / p.den;
            }
            v.push_back(t);
        }
        if (ok) best = std::move(v);
    }
    if (best.empty()) best.push_back(TimeSig{});
    return best;
}

}  // namespace gd24
