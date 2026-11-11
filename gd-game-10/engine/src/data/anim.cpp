// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/anim.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "data/bytes.h"

namespace anim {
namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what); }

void need(bool ok, const char* what) {
    if (!ok) fail(what);
}

uint16_t be16(const uint8_t* d, size_t size, size_t o) {
    ByteReader r(d, size);
    r.seek(o);
    return r.u16be();
}
uint32_t be32(const uint8_t* d, size_t size, size_t o) {
    ByteReader r(d, size);
    r.seek(o);
    return r.u32be();
}
float bef(const uint8_t* d, size_t size, size_t o) {
    uint32_t u = be32(d, size, o);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
float s16f(uint16_t v) { return float(int16_t(v)) * (1.0f / 16384.0f); }

void zeros(const uint8_t* d, size_t size, size_t a, size_t b, const char* what) {
    need(a <= b && b <= size, what);
    for (size_t i = a; i < b; ++i) need(d[i] == 0, what);
}

}  // namespace

int Skeleton::find(uint32_t nameKey) const {
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].name == nameKey) return int(i);
    return -1;
}

// .ske: u32 0x00010030, u32 count, 2 x u32 0, then the offsets of: names,
// parents, mirror names (u32 keys), mirror indices (i32), flags (u8), inverse
// bind matrices (16 f32), translations (vec4, w 1), rotations (xyzw) [C: all 35].
Skeleton parseSkeleton(const uint8_t* d, size_t size) {
    need(size >= 0x30 && be32(d, size, 0) == 0x00010030, "ske: header");
    need(be32(d, size, 8) == 0 && be32(d, size, 12) == 0, "ske: header");
    const uint32_t n = be32(d, size, 4);
    uint32_t off[8];
    for (int i = 0; i < 8; ++i) off[i] = be32(d, size, 16 + 4 * size_t(i));
    const uint32_t on = off[0], op = off[1], ofn = off[2], ofi = off[3], ofl = off[4], om = off[5],
                   ot = off[6], oq = off[7];
    need(n > 0 && n < 4096, "ske: bone count");
    need(ot == 0x30 && oq == ot + 16 * n && om == oq + 16 * n && on == om + 64 * n && op == on + 4 * n &&
             ofn == op + 4 * n && ofi == ofn + 4 * n && ofl == ofi + 4 * n,
         "ske: table layout");
    const size_t end = size_t(ofl) + n;
    need(((end + 3) & ~size_t(3)) == size, "ske: size");
    zeros(d, size, end, size, "ske: tail");
    Skeleton s;
    s.bones.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        Bone& b = s.bones[i];
        b.name = be32(d, size, on + 4 * i);
        b.parentName = be32(d, size, op + 4 * i);
        b.mirrorName = be32(d, size, ofn + 4 * i);
        b.mirror = int32_t(be32(d, size, ofi + 4 * i));
        b.flags = d[ofl + i];
        for (int k = 0; k < 3; ++k) b.pos[k] = bef(d, size, ot + 16 * i + 4 * k);
        need(bef(d, size, ot + 16 * i + 12) == 1.0f, "ske: translation w");
        for (int k = 0; k < 4; ++k) b.rot[k] = bef(d, size, oq + 16 * i + 4 * k);
        for (int k = 0; k < 16; ++k) b.invBind[k] = bef(d, size, om + 64 * i + 4 * k);
        const float n2 = b.rot[0] * b.rot[0] + b.rot[1] * b.rot[1] + b.rot[2] * b.rot[2] + b.rot[3] * b.rot[3];
        need(std::fabs(n2 - 1.0f) < 1e-3f, "ske: rotation not unit");
    }
    for (uint32_t i = 0; i < n; ++i) {
        Bone& b = s.bones[i];
        if (b.parentName != 0) {
            int p = s.find(b.parentName);
            need(p >= 0 && uint32_t(p) < i, "ske: parent");
            b.parent = p;
        }
        if (b.mirror < 0) {
            need(b.mirror == -1 && b.mirrorName == 0, "ske: mirror");
        } else {
            need(uint32_t(b.mirror) < n && s.bones[size_t(b.mirror)].name == b.mirrorName, "ske: mirror name");
        }
    }
    return s;
}

namespace {

// Q key [C: Function_824213B8 @ 0x824213b8, the 16-bit path at 0x824215e4].
size_t decodeRot(const uint8_t* d, size_t size, size_t p, size_t end, uint32_t flags, RotKey& k) {
    uint16_t sep = 0;
    const bool hasSep = flags & 0x100;
    if (hasSep) {
        sep = be16(d, size, p);
        p += 2;
    }
    const uint16_t w = be16(d, size, p);
    p += 2;
    uint16_t c[3] = {0, 0, 0};
    uint32_t t;
    if (!(w & 0x4000)) {
        for (auto& v : c) {
            v = be16(d, size, p);
            p += 2;
        }
        t = w;
    } else if (!(w & 0x3800)) {
        need(p < end && d[p] == 0, "ska: standard key index (only 0 is known)");
        p += 1;
        t = w & 0x7FFu;
    } else {
        const uint16_t bits[3] = {0x2000, 0x1000, 0x0800};
        for (int i = 0; i < 3; ++i) {
            if (w & bits[i]) {
                need(p < end, "ska: q key");
                c[i] = d[p++];
            } else {
                if (p & 1) {
                    need(p < end && d[p] == 0, "ska: q pad");
                    ++p;
                }
                c[i] = be16(d, size, p);
                p += 2;
            }
        }
        t = w & 0x7FFu;
    }
    if (p & 1) {
        need(p < end && d[p] == 0, "ska: q key pad");
        ++p;
    }
    need(p <= end, "ska: q track overrun");
    k.frame = float(hasSep ? sep : t);
    const float x = s16f(c[0]), y = s16f(c[1]), z = s16f(c[2]);
    const float n2 = x * x + y * y + z * z;
    need(n2 <= 1.0001f, "ska: quaternion xyz longer than 1");
    k.q = {x, y, z, std::sqrt(std::max(0.0f, 1.0f - n2))};
    return p;
}

// T key [C: FUN_82421840 @ 0x82421840]; float alignment is to 16 bytes from
// the start of the file (the game's buffers are 16-aligned).
size_t decodePos(const uint8_t* d, size_t size, size_t p, size_t end, const PosKey* prev, PosKey& k) {
    need(p < end, "ska: t key");
    const uint8_t b = d[p++];
    if (b & 0x40) {
        k.frame = float(b & 0x3F);
    } else {
        k.frame = float(int16_t(be16(d, size, p)));
        p += 2;
    }
    if (b & 0x80) {  // [I] hold: no value, keep the previous one (none on the disc)
        need(prev != nullptr, "ska: hold key without a previous key");
        k.v = prev->v;
        if (p & 1) ++p;
    } else {
        const size_t a = (p + 15) & ~size_t(15);
        zeros(d, size, p, std::min(a, end), "ska: t pad");
        for (int i = 0; i < 3; ++i) {
            k.v[i] = bef(d, size, a + 4 * size_t(i));
            need(std::isfinite(k.v[i]), "ska: translation");
        }
        p = a + 12;
    }
    need(p <= end, "ska: t track overrun");
    return p;
}

}  // namespace

// .ska: file header (u32 0, -1, file size, 0x20, -1, -1, mask offset or -1, -1),
// anim header at 0x20: u32 0x48, u32 flags, f32 duration, u16 bones, u16 Q
// keys (mod 65536), 2 x vec4, u16 T keys, u16 0, u32 -1, offsets of Q data,
// T data, Q track sizes (u16 per bone), T track sizes; zeros to 0x80 [C: all
// 18,747; pointer fix-up FUN_82420f90, decompressor Function_82421900].
Clip parseClip(const uint8_t* d, size_t size) {
    need(size >= 0x80, "ska: short");
    need(be32(d, size, 0) == 0 && be32(d, size, 4) == 0xFFFFFFFFu && be32(d, size, 8) == size &&
             be32(d, size, 12) == 0x20 && be32(d, size, 16) == 0xFFFFFFFFu && be32(d, size, 20) == 0xFFFFFFFFu &&
             be32(d, size, 28) == 0xFFFFFFFFu,
         "ska: file header");
    const size_t a = 0x20;
    need(be32(d, size, a) == 0x48, "ska: version");
    Clip c;
    c.flags = be32(d, size, a + 4);
    c.duration = bef(d, size, a + 8);
    const uint16_t nb = be16(d, size, a + 0x0C), nq16 = be16(d, size, a + 0x0E);
    for (int i = 0; i < 4; ++i) {
        c.headerA[i] = bef(d, size, a + 0x10 + 4 * size_t(i));
        c.headerB[i] = bef(d, size, a + 0x20 + 4 * size_t(i));
    }
    const uint16_t nt16 = be16(d, size, a + 0x30);
    need(be16(d, size, a + 0x32) == 0 && be32(d, size, a + 0x34) == 0xFFFFFFFFu, "ska: custom keys");
    const uint32_t oq = be32(d, size, a + 0x38), ot = be32(d, size, a + 0x3C), oqs = be32(d, size, a + 0x40),
                   ots = be32(d, size, a + 0x44);
    zeros(d, size, a + 0x48, 0x80, "ska: header tail");
    need(oq == 0x80 && oq <= ot && ot <= oqs && oqs <= ots && ots + 2u * nb <= size, "ska: offsets");
    need(!(c.flags & 0x8000) && !(c.flags & 0x2000), "ska: 8-bit or custom-key clip (not on the disc)");
    c.singlePose = c.flags & 0x10000000;
    c.bones.resize(nb);
    size_t nq = 0, nt = 0;
    size_t o = oq;
    for (uint16_t b = 0; b < nb; ++b) {
        const size_t end = o + be16(d, size, oqs + 2u * b);
        need(end <= ot, "ska: q sizes");
        auto& keys = c.bones[b].rot;
        if (c.singlePose) {
            need((end - o) % 16 == 0, "ska: pose q size");
            for (size_t p = o; p < end; p += 16) {
                RotKey k;
                for (int i = 0; i < 4; ++i) k.q[size_t(i)] = bef(d, size, p + 4 * size_t(i));
                keys.push_back(k);
            }
        } else {
            for (size_t p = o; p < end;) {
                RotKey k;
                p = decodeRot(d, size, p, end, c.flags, k);
                keys.push_back(k);
            }
        }
        nq += keys.size();
        o = end;
    }
    zeros(d, size, o, ot, "ska: q pad");
    o = ot;
    for (uint16_t b = 0; b < nb; ++b) {
        const size_t end = o + be16(d, size, ots + 2u * b);
        need(end <= oqs, "ska: t sizes");
        auto& keys = c.bones[b].pos;
        if (c.singlePose) {
            need((end - o) % 16 == 0, "ska: pose t size");
            for (size_t p = o; p < end; p += 16) {
                PosKey k;
                for (int i = 0; i < 3; ++i) k.v[size_t(i)] = bef(d, size, p + 4 * size_t(i));
                need(bef(d, size, p + 12) == 1.0f, "ska: pose translation w");
                keys.push_back(k);
            }
        } else {
            for (size_t p = o; p < end;) {
                PosKey k;
                p = decodePos(d, size, p, end, keys.empty() ? nullptr : &keys.back(), k);
                keys.push_back(k);
            }
        }
        nt += keys.size();
        o = end;
    }
    zeros(d, size, o, oqs, "ska: t pad");
    zeros(d, size, oqs + 2u * nb, ots, "ska: sizes pad");
    need((nq & 0xFFFF) == nq16 && (nt & 0xFFFF) == nt16, "ska: key counts");
    const size_t tail = ots + 2u * nb;
    const uint32_t m = be32(d, size, 24);
    if (c.flags & 0x80000) {  // partial-animation bone mask [C]
        need(m != 0xFFFFFFFFu && m >= tail && m - tail < 4, "ska: mask offset");
        zeros(d, size, tail, m, "ska: mask pad");
        need(be32(d, size, m) == nb, "ska: mask count");
        const size_t words = (nb + 31u) / 32u;
        need(m + 4 + 4 * words == size, "ska: mask size");
        c.mask.resize(nb);
        for (uint16_t i = 0; i < nb; ++i) c.mask[i] = (be32(d, size, m + 4 + 4 * (i / 32u)) >> (i % 32u)) & 1u;
    } else {
        need(m == 0xFFFFFFFFu && size - tail < 4, "ska: tail");
        zeros(d, size, tail, size, "ska: tail");
    }
    const float frames = std::round(c.duration * Clip::kFps);
    for (const auto& tr : c.bones) {
        auto check = [&](const auto& keys) {
            if (keys.empty()) return;
            need(keys.front().frame == 0.0f, "ska: first key not at 0");
            for (size_t i = 1; i < keys.size(); ++i) need(keys[i].frame > keys[i - 1].frame, "ska: key order");
            if (!c.singlePose && keys.size() > 1) need(keys.back().frame == frames, "ska: last key != duration");
        };
        check(tr.rot);
        check(tr.pos);
    }
    return c;
}

namespace {

template <class Key>
size_t keyBefore(const std::vector<Key>& keys, float f) {
    auto it = std::upper_bound(keys.begin(), keys.end(), f, [](float v, const Key& k) { return v < k.frame; });
    return it == keys.begin() ? 0 : size_t(it - keys.begin()) - 1;
}

}  // namespace

void sample(const Clip& clip, float seconds, std::vector<BonePose>& out) {
    out.assign(clip.bones.size(), BonePose{});
    const float last = std::round(clip.duration * Clip::kFps);
    const float f = std::clamp(seconds * Clip::kFps, 0.0f, last);
    for (size_t b = 0; b < clip.bones.size(); ++b) {
        const Track& tr = clip.bones[b];
        BonePose& p = out[b];
        if (!tr.rot.empty()) {
            const size_t i = keyBefore(tr.rot, f);
            Quat q = tr.rot[i].q;
            if (i + 1 < tr.rot.size()) {
                const RotKey &k0 = tr.rot[i], &k1 = tr.rot[i + 1];
                const float t = (f - k0.frame) / (k1.frame - k0.frame);
                float dot = 0;
                for (size_t c = 0; c < 4; ++c) dot += k0.q[c] * k1.q[c];
                const float s = dot < 0 ? -1.0f : 1.0f;
                float n2 = 0;
                for (size_t c = 0; c < 4; ++c) {
                    q[c] = k0.q[c] + (s * k1.q[c] - k0.q[c]) * t;
                    n2 += q[c] * q[c];
                }
                const float inv = n2 > 0 ? 1.0f / std::sqrt(n2) : 1.0f;
                for (auto& v : q) v *= inv;
            }
            p.rot = q;
            p.hasRot = true;
        }
        if (!tr.pos.empty()) {
            const size_t i = keyBefore(tr.pos, f);
            Vec3 v = tr.pos[i].v;
            if (i + 1 < tr.pos.size()) {
                const PosKey &k0 = tr.pos[i], &k1 = tr.pos[i + 1];
                const float t = (f - k0.frame) / (k1.frame - k0.frame);
                for (size_t c = 0; c < 3; ++c) v[c] = k0.v[c] + (k1.v[c] - k0.v[c]) * t;
            }
            p.pos = v;
            p.hasPos = true;
        }
    }
}

}  // namespace anim
