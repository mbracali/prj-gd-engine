// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/spu_reverb.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>

#include "data/dta.h"
#include "data/game_data.h"

namespace spu2 {

namespace {

// Register slots (the order of libsd's preset rows and of the SPU2's
// reverb register block).
enum Reg {
    dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2,
    mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF,
    mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN,
};

// LIBSD.IRX .data 0x4DE0 + mode x 0x44 + 4: the ten presets as libsd keeps
// them (before the x 4 of the address registers) [C data].
const std::array<std::array<uint16_t, 32>, kFXModeCount> kPresets = {{
    // Off
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0001, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // Room
    {0x007d, 0x005b, 0x6d80, 0x54b8, 0xbed0, 0x0000, 0x0000, 0xba80, 0x5800, 0x5300, 0x04d6, 0x0333, 0x03f0, 0x0227, 0x0374, 0x01ef,
     0x0336, 0x01b7, 0x0335, 0x01b6, 0x0334, 0x01b5, 0x0334, 0x01b5, 0x0334, 0x01b5, 0x01b4, 0x0136, 0x00b8, 0x005c, 0x8000, 0x8000},
    // Small studio
    {0x0033, 0x0025, 0x70f0, 0x4fa8, 0xbce0, 0x4410, 0xc0f0, 0x9c00, 0x5280, 0x4ec0, 0x03e4, 0x031b, 0x03a4, 0x02af, 0x0372, 0x0266,
     0x031c, 0x025d, 0x025c, 0x018e, 0x022f, 0x0135, 0x01d2, 0x00b7, 0x018f, 0x00b5, 0x00b4, 0x0080, 0x004c, 0x0026, 0x8000, 0x8000},
    // Medium studio
    {0x00b1, 0x007f, 0x70f0, 0x4fa8, 0xbce0, 0x4510, 0xbef0, 0xb4c0, 0x5280, 0x4ec0, 0x0904, 0x076b, 0x0824, 0x065f, 0x07a2, 0x0616,
     0x076c, 0x05ed, 0x05ec, 0x042e, 0x050f, 0x0305, 0x0462, 0x02b7, 0x042f, 0x0265, 0x0264, 0x01b2, 0x0100, 0x0080, 0x8000, 0x8000},
    // Large studio
    {0x00e3, 0x00a9, 0x6f60, 0x4fa8, 0xbce0, 0x4510, 0xbef0, 0xa680, 0x5680, 0x52c0, 0x0dfb, 0x0b58, 0x0d09, 0x0a3c, 0x0bd9, 0x0973,
     0x0b59, 0x08da, 0x08d9, 0x05e9, 0x07ec, 0x04b0, 0x06ef, 0x03d2, 0x05ea, 0x031d, 0x031c, 0x0238, 0x0154, 0x00aa, 0x8000, 0x8000},
    // Hall
    {0x01a5, 0x0139, 0x6000, 0x5000, 0x4c00, 0xb800, 0xbc00, 0xc000, 0x6000, 0x5c00, 0x15ba, 0x11bb, 0x14c2, 0x10bd, 0x11bc, 0x0dc1,
     0x11c0, 0x0dc3, 0x0dc0, 0x09c1, 0x0bc4, 0x07c1, 0x0a00, 0x06cd, 0x09c2, 0x05c1, 0x05c0, 0x041a, 0x0274, 0x013a, 0x8000, 0x8000},
    // Space
    {0x033d, 0x0231, 0x7e00, 0x5000, 0xb400, 0xb000, 0x4c00, 0xb000, 0x6000, 0x5400, 0x1ed6, 0x1a31, 0x1d14, 0x183b, 0x1bc2, 0x16b2,
     0x1a32, 0x15ef, 0x15ee, 0x1055, 0x1334, 0x0f2d, 0x11f6, 0x0c5d, 0x1056, 0x0ae1, 0x0ae0, 0x07a2, 0x0464, 0x0232, 0x8000, 0x8000},
    // Echo
    {0x0003, 0x0003, 0x7fff, 0x7fff, 0x0000, 0x0000, 0x0000, 0x8100, 0x0000, 0x0000, 0x1ffd, 0x0ffd, 0x1009, 0x0009, 0x0000, 0x0000,
     0x1009, 0x0009, 0x1fff, 0x1fff, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1008, 0x1004, 0x0008, 0x0004, 0x8000, 0x8000},
    // Delay
    {0x0003, 0x0003, 0x7fff, 0x7fff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1ffd, 0x0ffd, 0x1009, 0x0009, 0x0000, 0x0000,
     0x1009, 0x0009, 0x1fff, 0x1fff, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1ffe, 0x1008, 0x1004, 0x0008, 0x0004, 0x8000, 0x8000},
    // Pipe
    {0x0017, 0x0013, 0x70f0, 0x4fa8, 0xbce0, 0x4510, 0xbef0, 0x8500, 0x5f80, 0x54c0, 0x0371, 0x02af, 0x02e5, 0x01df, 0x02b0, 0x01d7,
     0x0358, 0x026a, 0x01d6, 0x011e, 0x012d, 0x00b1, 0x011f, 0x0059, 0x01a0, 0x00e3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000},
}};

// LIBSD.IRX .data 0x4DB8: the work area of each mode in 8-word units [C data].
constexpr std::array<uint32_t, kFXModeCount> kAreaUnits = {0x2, 0x4d8, 0x3e8, 0x908, 0xdfc, 0x15bc, 0x1ed8, 0x3008, 0x3008, 0x780};

// The SPU2's 39-tap half-band filter (psx-spx / PCSX2) [I: hardware].
constexpr std::array<int32_t, 39> kTaps = {-1, 0, 2, 0, -10, 0, 35, 0, -103, 0, 266, 0, -616, 0, 1332, 0, -2960, 0, 10246, 16384,
                                           10246, 0, -2960, 0, 1332, 0, -616, 0, 266, 0, -103, 0, 35, 0, -10, 0, 2, 0, -1};

int32_t clamp16(int64_t v) { return int32_t(std::clamp<int64_t>(v, -32768, 32767)); }
// The hardware's volume multiply: (a x b) >> 15 with b a signed 16-bit register.
int64_t mul(int64_t a, uint16_t reg) { return (a * int16_t(reg)) >> 15; }

const char* const kModeNames[kFXModeCount] = {"kFXModeOff", "kFXModeRoom", "kFXModeSmallStudio", "kFXModeMedStudio", "kFXModeLargeStudio",
                                              "kFXModeHall", "kFXModeSpace", "kFXModeEcho", "kFXModeDelay", "kFXModePipe"};

// A number, or one of the macros (system/run/config/macros.dta) [C data].
bool number(const DataNode& n, float& out) {
    if (n.type == DataNode::Type::Int) return out = float(n.i), true;
    if (n.type == DataNode::Type::Float) return out = n.f, true;
    if (n.isText()) {
        if (n.s == "kDbSilence") return out = -96.0f, true;
        for (int m = 0; m < kFXModeCount; ++m)
            if (n.s == kModeNames[m]) return out = float(m), true;
        if (n.s == "TRUE") return out = 1, true;
        if (n.s == "FALSE") return out = 0, true;
    }
    return false;
}

void field(const DataNode& block, const char* key, float& v) {
    if (const DataNode* f = block.find(key); f && f->children.size() > 1) number(f->children[1], v);
}

}  // namespace

const char* const kRegNames[32] = {"dAPF1", "dAPF2", "vIIR", "vCOMB1", "vCOMB2", "vCOMB3", "vCOMB4", "vWALL", "vAPF1", "vAPF2", "mLSAME",
                                   "mRSAME", "mLCOMB1", "mRCOMB1", "mLCOMB2", "mRCOMB2", "dLSAME", "dRSAME", "mLDIFF", "mRDIFF", "mLCOMB3",
                                   "mRCOMB3", "mLCOMB4", "mRCOMB4", "dLDIFF", "dRDIFF", "mLAPF1", "mRAPF1", "mLAPF2", "mRAPF2", "vLIN", "vRIN"};

FxConfig defaultFxConfig() {
    FxConfig c;
    c.core[0] = {kFXModeDelay, -15.0f, 0.6000000238418579f, -19.0f};
    c.core[1] = {kFXModeMedStudio, -96.0f, 0.0f, -96.0f};
    return c;
}

bool loadFxConfig(const GameData& data, FxConfig& out) {
    out = defaultFxConfig();
    try {
        DataNode root = parseDtb(data.read("config/gen/synth.dtb"));
        resolveConditionals(root, {});
        const DataNode* fx = root.find("fx");
        if (!fx) return true;
        float chain = out.chain ? 1.0f : 0.0f;
        field(*fx, "chain", chain);
        out.chain = chain != 0;
        for (int c = 0; c < 2; ++c) {
            const DataNode* core = fx->find(c == 0 ? "core_0" : "core_1");  // 0x00225D50 builds "core_%i"
            if (!core) continue;
            FxCoreConfig& k = out.core[size_t(c)];
            float mode = float(k.mode);
            field(*core, "mode", mode);
            k.mode = std::clamp(int(mode), 0, kFXModeCount - 1);
            field(*core, "volume", k.volumeDb);
            field(*core, "delay", k.delay);
            field(*core, "feedback", k.feedbackDb);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

float fxDbToLin(float db) { return db <= -96.0f ? 0.0f : float(std::pow(10.0, double(db / 20.0f))); }

int16_t fxDepth(float volumeDb) {
    const float v = std::clamp(fxDbToLin(volumeDb), 0.0f, 1.0f) * 32767.0f;
    return int16_t(int(v > 0.0f ? v + 0.5f : v - 0.5f));  // cvt.w.s truncates
}

int fxDelay(float delay) {
    const float v = delay * 127.0f;
    return int(v > 0.0f ? v + 0.5f : v - 0.5f);
}

int fxFeedback(float feedbackDb) {
    const float v = std::clamp(fxDbToLin(feedbackDb), 0.0f, 1.0f) * 127.0f;
    return int(v > 0.0f ? v + 0.5f : v - 0.5f);
}

const std::array<uint16_t, 32>& preset(int mode) { return kPresets[size_t(std::clamp(mode, 0, kFXModeCount - 1))]; }

uint32_t workAreaWords(int mode) { return kAreaUnits[size_t(std::clamp(mode, 0, kFXModeCount - 1))] * 8; }

void SpuReverb::configure(int mode, int delay, int feedback, int16_t depthL, int16_t depthR) {
    mode_ = std::clamp(mode, 0, kFXModeCount - 1);
    reg_ = preset(mode_);
    if (mode_ == kFXModeEcho || mode_ == kFXModeDelay) {
        // sceSdSetEffectAttr 0x0D54..0x0F5C: the delay lines and the wall
        // from attr.delay / attr.feedback, kept to 16 bits (sh) [C].
        const uint32_t d1 = uint32_t(delay + 1), h = d1 << 5;
        reg_[mLSAME] = uint16_t((d1 << 6) - reg_[dAPF1]);
        reg_[mRSAME] = uint16_t(h - reg_[dAPF2]);
        reg_[dLSAME] = uint16_t(reg_[dRSAME] + h);
        reg_[mLCOMB1] = uint16_t(reg_[mRCOMB1] + h);
        reg_[mRAPF1] = uint16_t(reg_[mRAPF2] + h);
        reg_[mLAPF1] = uint16_t(reg_[mLAPF2] + h);
        reg_[vWALL] = uint16_t(feedback * 258);
    }
    depthL_ = depthL, depthR_ = depthR;
    mem_.assign(workAreaWords(mode_), 0);  // command 3 clears the work area
    x_ = cycle_ = pos_ = 0;
    for (auto& d : down_) d.fill(0);
    for (auto& u : up_) u.fill(0);
}

uint32_t SpuReverb::at(int32_t offset) const {
    const int64_t n = int64_t(mem_.size());
    int64_t i = (int64_t(x_) + offset) % n;
    return uint32_t(i < 0 ? i + n : i);
}

// One channel's step of the reverb network (psx-spx "SPU Reverb Formula",
// PCSX2 V_Core::DoReverb): the same-side and cross ("diff") reflections
// into the work area through the IIR, the four comb taps out, then the two
// all-pass filters [I: hardware]. Address registers are x 4 words.
int32_t SpuReverb::tick(bool r, int32_t input) {
    auto a = [&](int reg) { return int32_t(reg_[size_t(reg)]) * 4; };
    const int32_t sameDst = a(r ? mRSAME : mLSAME), diffDst = a(r ? mRDIFF : mLDIFF);
    const uint32_t sameSrc = at(a(r ? dRSAME : dLSAME)), samePrv = at(sameDst - 1), sameW = at(sameDst);
    const uint32_t diffSrc = at(a(r ? dLDIFF : dRDIFF)), diffPrv = at(diffDst - 1), diffW = at(diffDst);
    const int32_t apf1Dst = a(r ? mRAPF1 : mLAPF1), apf2Dst = a(r ? mRAPF2 : mLAPF2);
    const uint32_t apf1Src = at(apf1Dst - a(dAPF1)), apf2Src = at(apf2Dst - a(dAPF2));
    const int64_t in = mul(input, reg_[r ? vRIN : vLIN]);
    const int64_t same = mul(in + mul(mem_[sameSrc], reg_[vWALL]) - mem_[samePrv], reg_[vIIR]) + mem_[samePrv];
    const int64_t diff = mul(in + mul(mem_[diffSrc], reg_[vWALL]) - mem_[diffPrv], reg_[vIIR]) + mem_[diffPrv];
    int64_t out = mul(mem_[at(a(r ? mRCOMB1 : mLCOMB1))], reg_[vCOMB1]) + mul(mem_[at(a(r ? mRCOMB2 : mLCOMB2))], reg_[vCOMB2]) +
                  mul(mem_[at(a(r ? mRCOMB3 : mLCOMB3))], reg_[vCOMB3]) + mul(mem_[at(a(r ? mRCOMB4 : mLCOMB4))], reg_[vCOMB4]);
    const int64_t apf1 = out - mul(mem_[apf1Src], reg_[vAPF1]);
    out = mem_[apf1Src] + mul(apf1, reg_[vAPF1]);
    const int64_t apf2 = out - mul(mem_[apf2Src], reg_[vAPF2]);
    out = mem_[apf2Src] + mul(apf2, reg_[vAPF2]);
    mem_[sameW] = int16_t(clamp16(same));
    mem_[diffW] = int16_t(clamp16(diff));
    mem_[at(apf1Dst)] = int16_t(clamp16(apf1));
    mem_[at(apf2Dst)] = int16_t(clamp16(apf2));
    return clamp16(out);
}

void SpuReverb::process(const float* in, float* out, int frames) {
    if (mem_.empty() || mode_ == kFXModeOff) return;
    for (int f = 0; f < frames; ++f) {
        const size_t p = pos_ & 63;
        for (int c = 0; c < 2; ++c) down_[size_t(c)][p] = clamp16(std::lround(double(in[2 * f + c]) * 32768.0));
        const bool right = (cycle_ & 1) != 0;
        x_ = uint32_t((cycle_ >> 1) % mem_.size());
        // Down to the half rate: the filter over this channel's last 39 inputs.
        int64_t acc = 0;
        for (size_t k = 0; k < kTaps.size(); ++k) acc += int64_t(down_[size_t(right)][(pos_ - 38 + k) & 63]) * kTaps[k];
        const int32_t y = tick(right, clamp16(acc >> 15));
        up_[size_t(right)][p] = y;
        up_[size_t(!right)][p] = 0;
        // Back up: both channels through the filter (x 2 for the zeros).
        int64_t l = 0, rr = 0;
        for (size_t k = 0; k < kTaps.size(); ++k) {
            const size_t q = (pos_ - 38 + k) & 63;
            l += int64_t(up_[0][q]) * kTaps[k], rr += int64_t(up_[1][q]) * kTaps[k];
        }
        const int64_t wl = (int64_t(clamp16(l >> 14)) * depthL_) >> 15, wr = (int64_t(clamp16(rr >> 14)) * depthR_) >> 15;
        out[2 * f] += float(wl) * (1.0f / 32768.0f);
        out[2 * f + 1] += float(wr) * (1.0f / 32768.0f);
        ++pos_, ++cycle_;
    }
}

}  // namespace spu2
