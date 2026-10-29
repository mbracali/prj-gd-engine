// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/xenos_glsl.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <vector>

namespace xenos {
namespace {

uint32_t bits(uint64_t v, int lo, int n) { return uint32_t((v >> lo) & ((uint64_t(1) << n) - 1)); }

// Control flow opcodes.
enum Cf : uint8_t {
    kNop, kExec, kExecEnd, kCondExec, kCondExecEnd, kCondExecPred, kCondExecPredEnd, kLoopStart, kLoopEnd,
    kCondCall, kReturn, kCondJmp, kAlloc, kCondExecPredClean, kCondExecPredCleanEnd, kMarkVsFetchDone
};

const char* const kVecNames[] = {"add", "mul", "max", "min", "seq", "sgt", "sge", "sne", "frc", "trunc", "floor", "mad",
                                 "cndeq", "cndge", "cndgt", "dp4", "dp3", "dp2add", "cube", "max4", "setp_eq_push",
                                 "setp_ne_push", "setp_gt_push", "setp_ge_push", "kill_eq", "kill_gt", "kill_ge",
                                 "kill_ne", "dst", "maxa"};
const char* const kScaNames[] = {"adds", "adds_prev", "muls", "muls_prev", "muls_prev2", "maxs", "mins", "seqs", "sgts",
                                 "sges", "snes", "frcs", "truncs", "floors", "exp", "logc", "log", "rcpc", "rcpf", "rcp",
                                 "rsqc", "rsqf", "rsq", "maxas", "maxasf", "subs", "subs_prev", "setp_eq", "setp_ne",
                                 "setp_gt", "setp_ge", "setp_inv", "setp_pop", "setp_clr", "setp_rstr", "kills_eq",
                                 "kills_gt", "kills_ge", "kills_ne", "kills_one", "sqrt", "op41", "mulsc0", "mulsc1",
                                 "addsc0", "addsc1", "subsc0", "subsc1", "sin", "cos", "retain_prev"};

struct CfInstr {
    uint64_t v = 0;
    Cf op() const { return Cf(bits(v, 44, 4)); }
    uint32_t address() const { return bits(v, 0, 12); }
    uint32_t count() const { return bits(v, 12, 3); }
    uint32_t sequence() const { return bits(v, 16, 12); }
    uint32_t boolAddress() const { return bits(v, 34, 8); }
    bool condition() const { return bits(v, 42, 1) != 0; }
};

std::vector<CfInstr> controlFlow(const std::vector<uint32_t>& w) {
    std::vector<CfInstr> out;
    size_t limit = w.size() / 3;  // CF slots end where the first exec's instructions start
    for (size_t i = 0; i < limit && i * 3 + 2 < w.size(); ++i) {
        const uint64_t a = w[i * 3] | (uint64_t(w[i * 3 + 1] & 0xFFFF) << 32);
        const uint64_t b = (w[i * 3 + 1] >> 16) | (uint64_t(w[i * 3 + 2]) << 16);
        for (uint64_t x : {a, b}) {
            CfInstr c{x};
            out.push_back(c);
            switch (c.op()) {
            case kExec: case kExecEnd: case kCondExec: case kCondExecEnd: case kCondExecPred: case kCondExecPredEnd:
            case kCondExecPredClean: case kCondExecPredCleanEnd:
                if (c.count()) limit = std::min<size_t>(limit, c.address());
                break;
            default: break;
            }
        }
    }
    return out;
}

bool isExec(Cf op) {
    return op == kExec || op == kExecEnd || op == kCondExec || op == kCondExecEnd || op == kCondExecPred ||
           op == kCondExecPredEnd || op == kCondExecPredClean || op == kCondExecPredCleanEnd;
}
bool isEnd(Cf op) { return op == kExecEnd || op == kCondExecEnd || op == kCondExecPredEnd || op == kCondExecPredCleanEnd; }

std::string fmtFloat(float f) {
    if (f != f) return "(0.0/0.0)";
    if (f > 3.0e38f) return "3.402823e38";
    if (f < -3.0e38f) return "-3.402823e38";
    char b[48];
    std::snprintf(b, sizeof b, "%.9g", double(f));
    std::string s = b;
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return s;
}

const char kComp[] = "xyzw";

class Translator {
public:
    Translator(const matlib::Shader& s, bool pixel) : s_(s), pixel_(pixel) {}

    // Body of gdRun() plus the declarations it needs; throws on what is not supported.
    void run();

    const matlib::Shader& s_;
    bool pixel_;
    std::string code_;
    int maxReg_ = -1;
    std::set<int> exports_;  // export registers written (vertex: 0..15, 62 position; pixel: 0..3, 61)
    uint16_t samplers_ = 0, cubes_ = 0, attribs_ = 0;
    std::set<int> attribLocs_;
    std::string indent_ = "    ";

    void line(const std::string& s) { code_ += indent_ + s + "\n"; }
    void reg(int r) { maxReg_ = std::max(maxReg_, r); }
    std::string constRef(uint32_t index, bool rel, bool a0) const;
    std::string operand(const uint32_t* w, int i, bool& usesConst, int& constSlot);
    void alu(const uint32_t* w);
    void fetch(const uint32_t* w, uint32_t address);
};

std::string Translator::constRef(uint32_t index, bool rel, bool a0) const {
    if (rel) return std::string("gdC(") + std::to_string(index) + (a0 ? " + a0)" : ")");
    const uint32_t lit = s_.literalReg, litEnd = s_.literalReg + uint32_t(s_.literals.size() / 4);
    if (!s_.literals.empty() && index >= lit && index < litEnd) {
        const float* f = &s_.literals[size_t(index - lit) * 4];
        bool zero = true;
        for (int k = 0; k < 4; ++k) zero = zero && f[k] == 0.0f;
        // A literal range also covers registers it does not define (zeros):
        // registers the engine sets keep their uniform.
        if (!zero || index >= 248)
            return "vec4(" + fmtFloat(f[0]) + ", " + fmtFloat(f[1]) + ", " + fmtFloat(f[2]) + ", " + fmtFloat(f[3]) + ")";
    }
    if (index >= 176 && index < 192) return "cO[" + std::to_string(index - 176) + "]";
    if (index >= 216 && index < 228) return "cO[" + std::to_string(index - 200) + "]";
    if (index >= 128) return "cG[" + std::to_string(index - 128) + "]";
    if (index < 64) return std::string(pixel_ ? "cP[" : "cV[") + std::to_string(index) + "]";
    return "vec4(0.0)";
}

// Source operand i (1..3) of an ALU instruction as a vec4 expression.
std::string Translator::operand(const uint32_t* w, int i, bool& usesConst, int& constSlot) {
    const uint32_t r = bits(w[2], 16 - 8 * (i - 1), 8);
    const bool temp = bits(w[2], 32 - i, 1) != 0;  // src1_sel bit 31, src2 30, src3 29
    const uint32_t swz = bits(w[1], 16 - 8 * (i - 1), 8);
    const bool neg = bits(w[1], 27 - i, 1) != 0;  // src1 bit 26, src2 25, src3 24
    std::string base;
    bool absv;
    if (temp) {
        if (r & 0x40) throw std::runtime_error("relative temp register");
        reg(int(r & 0x3F));
        base = "r" + std::to_string(r & 0x3F);
        absv = (r & 0x80) != 0;
    } else {
        const bool rel = constSlot == 0 ? bits(w[1], 31, 1) : bits(w[1], 30, 1);
        base = constRef(r, rel, bits(w[1], 29, 1) != 0);
        absv = bits(w[0], 7, 1) != 0;
        usesConst = true;
        ++constSlot;
    }
    if (absv) base = "abs(" + base + ")";
    std::string sw = ".";
    for (int k = 0; k < 4; ++k) sw += kComp[((swz >> (2 * k)) + uint32_t(k)) & 3];
    if (sw != ".xyzw") base += sw;
    return neg ? "(-" + base + ")" : base;
}

std::string maskStr(uint32_t m) {
    std::string s;
    for (int k = 0; k < 4; ++k)
        if (m >> k & 1) s += kComp[k];
    return s;
}

void Translator::alu(const uint32_t* w) {
    const uint32_t vdst = bits(w[0], 0, 6), sdst = bits(w[0], 8, 6);
    const bool sdstRel = bits(w[0], 14, 1) != 0, exp = bits(w[0], 15, 1) != 0;
    const uint32_t vmask = bits(w[0], 16, 4), smask = bits(w[0], 20, 4);
    const bool vclamp = bits(w[0], 24, 1) != 0, sclamp = bits(w[0], 25, 1) != 0;
    const uint32_t sop = bits(w[0], 26, 6), vop = bits(w[2], 24, 5);
    const bool predicated = bits(w[1], 28, 1) != 0, predCond = bits(w[1], 27, 1) != 0;
    if (bits(w[0], 6, 1)) throw std::runtime_error("relative vector destination");
    if (vop > 29) throw std::runtime_error("vector opcode " + std::to_string(vop));
    if (sop > 50 || sop == 41) throw std::runtime_error("scalar opcode " + std::to_string(sop));
    if (sdstRel && !exp) throw std::runtime_error("relative scalar destination");

    std::string body;
    auto add = [&](const std::string& s) { body += s; };
    bool uc = false;
    int slot = 0;
    const std::string s1 = operand(w, 1, uc, slot), s2 = operand(w, 2, uc, slot);
    const bool constScalar = sop >= 42 && sop <= 47;
    std::string s3, sa, sb;
    if (constScalar) {
        // MUL/ADD/SUB_CONST_0/1: a = a constant's component, b = a temporary's
        // (register (opcode & 1) | src3_sel << 1 | swizzle & 0x3C) [ucode.h].
        const uint32_t swz = bits(w[1], 0, 8);
        const bool rel = slot == 0 ? bits(w[1], 31, 1) : bits(w[1], 30, 1);
        std::string c = constRef(bits(w[2], 0, 8), rel, bits(w[1], 29, 1) != 0);
        if (bits(w[0], 7, 1)) c = "abs(" + c + ")";
        sa = c + "." + kComp[((swz >> 6) + 3) & 3];
        if (bits(w[1], 24, 1)) sa = "(-" + sa + ")";
        const int tr = int((sop & 1) | (bits(w[2], 29, 1) << 1) | (swz & 0x3C));
        reg(tr);
        sb = "r" + std::to_string(tr) + "." + kComp[swz & 3];
        s3 = "vec4(0.0)";
    } else {
        s3 = operand(w, 3, uc, slot);
        const uint32_t swz = bits(w[1], 0, 8);
        // Scalar operands: a from the swizzle's w slot, b from its x slot.
        const uint32_t ca = ((swz >> 6) + 3) & 3, cb = swz & 3;
        // Rebuild from the operand without its own swizzle.
        const uint32_t r = bits(w[2], 0, 8);
        const bool temp = bits(w[2], 29, 1) != 0;
        std::string base;
        if (temp) {
            base = "r" + std::to_string(r & 0x3F);
            if (r & 0x80) base = "abs(" + base + ")";
        } else {
            const int cslot = slot - 1;
            const bool rel = cslot == 0 ? bits(w[1], 31, 1) : bits(w[1], 30, 1);
            base = constRef(r, rel, bits(w[1], 29, 1) != 0);
            if (bits(w[0], 7, 1)) base = "abs(" + base + ")";
        }
        sa = base + "." + kComp[ca];
        sb = base + "." + kComp[cb];
        if (bits(w[1], 24, 1)) sa = "(-" + sa + ")", sb = "(-" + sb + ")";
    }

    // Vector result.
    std::string v;
    std::string predPush;  // p0 update from a vector pred-set op
    std::string kill;
    switch (vop) {
    case 0: v = s1 + " + " + s2; break;
    case 1: v = s1 + " * " + s2; break;
    case 2: v = "max(" + s1 + ", " + s2 + ")"; break;
    case 3: v = "min(" + s1 + ", " + s2 + ")"; break;
    case 4: v = "vec4(equal(" + s1 + ", " + s2 + "))"; break;
    case 5: v = "vec4(greaterThan(" + s1 + ", " + s2 + "))"; break;
    case 6: v = "vec4(greaterThanEqual(" + s1 + ", " + s2 + "))"; break;
    case 7: v = "vec4(notEqual(" + s1 + ", " + s2 + "))"; break;
    case 8: v = "fract(" + s1 + ")"; break;
    case 9: v = "trunc(" + s1 + ")"; break;
    case 10: v = "floor(" + s1 + ")"; break;
    case 11: v = s1 + " * " + s2 + " + " + s3; break;
    case 12: v = "mix(" + s3 + ", " + s2 + ", vec4(equal(" + s1 + ", vec4(0.0))))"; break;
    case 13: v = "mix(" + s3 + ", " + s2 + ", vec4(greaterThanEqual(" + s1 + ", vec4(0.0))))"; break;
    case 14: v = "mix(" + s3 + ", " + s2 + ", vec4(greaterThan(" + s1 + ", vec4(0.0))))"; break;
    case 15: v = "vec4(dot(" + s1 + ", " + s2 + "))"; break;
    case 16: v = "vec4(dot((" + s1 + ").xyz, (" + s2 + ").xyz))"; break;
    case 17: v = "vec4(dot((" + s1 + ").xy, (" + s2 + ").xy) + (" + s3 + ").x)"; break;
    case 18: v = "gdCube(" + s1 + ", " + s2 + ")"; break;
    case 19: v = "vec4(gdMax4(" + s1 + "))"; break;
    case 20: case 21: case 22: case 23: {
        static const char* const cmp[] = {"==", "!=", ">", ">="};
        const char* c = cmp[vop - 20];
        v = "((t1.x == 0.0 && t2.x " + std::string(c) + " 0.0) ? vec4(0.0) : vec4(t1.x + 1.0))";
        predPush = std::string("(t1.w == 0.0 && t2.w ") + c + " 0.0)";
        break;
    }
    case 24: case 25: case 26: case 27: {
        static const char* const f[] = {"equal", "greaterThan", "greaterThanEqual", "notEqual"};
        kill = std::string("any(") + f[vop - 24] + "(t1, t2))";
        v = "vec4(" + kill + " ? 1.0 : 0.0)";
        break;
    }
    case 28: v = "vec4(1.0, (" + s1 + ").y * (" + s2 + ").y, (" + s1 + ").z, (" + s2 + ").w)"; break;
    case 29: v = "max(" + s1 + ", " + s2 + ")"; add("int na = int(clamp(floor((" + s1 + ").w + 0.5), -256.0, 255.0)); "); break;
    }
    const bool usesT = !predPush.empty() || !kill.empty();

    // Scalar result.
    std::string sr, predSet, skill;
    const std::string a = sa, b = sb;
    switch (sop) {
    case 0: sr = a + " + " + b; break;
    case 1: sr = a + " + pv"; break;
    case 2: sr = a + " * " + b; break;
    case 3: sr = a + " * pv"; break;
    case 4: sr = "((pv == -3.402823e38 || isinf(pv) || isinf(" + b + ") || " + b + " <= 0.0) ? -3.402823e38 : " + a + " * pv)"; break;
    case 5: sr = "max(" + a + ", " + b + ")"; break;
    case 6: sr = "min(" + a + ", " + b + ")"; break;
    case 7: sr = "(" + a + " == 0.0 ? 1.0 : 0.0)"; break;
    case 8: sr = "(" + a + " > 0.0 ? 1.0 : 0.0)"; break;
    case 9: sr = "(" + a + " >= 0.0 ? 1.0 : 0.0)"; break;
    case 10: sr = "(" + a + " != 0.0 ? 1.0 : 0.0)"; break;
    case 11: sr = "fract(" + a + ")"; break;
    case 12: sr = "trunc(" + a + ")"; break;
    case 13: sr = "floor(" + a + ")"; break;
    case 14: sr = "exp2(" + a + ")"; break;
    case 15: sr = "max(log2(" + a + "), -3.402823e38)"; break;
    case 16: sr = "log2(" + a + ")"; break;
    case 17: sr = "clamp(1.0 / " + a + ", -3.402823e38, 3.402823e38)"; break;
    case 18: sr = "gdRcpFF(" + a + ")"; break;
    case 19: sr = "(1.0 / " + a + ")"; break;
    case 20: sr = "clamp(inversesqrt(" + a + "), -3.402823e38, 3.402823e38)"; break;
    case 21: sr = "gdRcpFF(sqrt(" + a + "))"; break;
    case 22: sr = "inversesqrt(" + a + ")"; break;
    case 23: sr = "max(" + a + ", " + b + ")"; add("int na = int(clamp(floor(" + a + " + 0.5), -256.0, 255.0)); "); break;
    case 24: sr = "max(" + a + ", " + b + ")"; add("int na = int(clamp(floor(" + a + "), -256.0, 255.0)); "); break;
    case 25: sr = a + " - " + b; break;
    case 26: sr = a + " - pv"; break;
    case 27: predSet = "(" + a + " == 0.0)"; sr = "(" + a + " == 0.0 ? 0.0 : 1.0)"; break;
    case 28: predSet = "(" + a + " != 0.0)"; sr = "(" + a + " != 0.0 ? 0.0 : 1.0)"; break;
    case 29: predSet = "(" + a + " > 0.0)"; sr = "(" + a + " > 0.0 ? 0.0 : 1.0)"; break;
    case 30: predSet = "(" + a + " >= 0.0)"; sr = "(" + a + " >= 0.0 ? 0.0 : 1.0)"; break;
    case 31: predSet = "(" + a + " == 1.0)"; sr = "(" + a + " == 1.0 ? 0.0 : (" + a + " == 0.0 ? 1.0 : " + a + "))"; break;
    case 32: predSet = "(" + a + " - 1.0 <= 0.0)"; sr = "(" + a + " - 1.0 <= 0.0 ? 0.0 : " + a + " - 1.0)"; break;
    case 33: predSet = "false"; sr = "3.402823e38"; break;
    case 34: predSet = "(" + a + " == 0.0)"; sr = "(" + a + " == 0.0 ? 0.0 : " + a + ")"; break;
    case 35: skill = a + " == 0.0"; break;
    case 36: skill = a + " > 0.0"; break;
    case 37: skill = a + " >= 0.0"; break;
    case 38: skill = a + " != 0.0"; break;
    case 39: skill = a + " == 1.0"; break;
    case 40: sr = "sqrt(" + a + ")"; break;
    case 42: case 43: sr = a + " * " + b; break;
    case 44: case 45: sr = a + " + " + b; break;
    case 46: case 47: sr = a + " - " + b; break;
    case 48: sr = "sin(" + a + ")"; break;
    case 49: sr = "cos(" + a + ")"; break;
    case 50: sr = "pv"; break;
    }
    if (!skill.empty()) sr = "((" + skill + ") ? 1.0 : 0.0)";

    // Masks (exports: vector and scalar share the vector destination; both
    // masks set: 1, neither (with scalar_dest_rel): 0) [ucode.h].
    uint32_t vm = vmask, sm = smask, one = 0, zero = 0;
    if (exp) {
        vm = vmask & ~smask;
        sm = smask & ~vmask;
        one = vmask & smask;
        zero = sdstRel ? (0xF & ~(vmask | smask)) : 0;
    }
    std::string vdstName, sdstName;
    if (exp) {
        vdstName = "e" + std::to_string(vdst);
        sdstName = vdstName;
        exports_.insert(int(vdst));
    } else {
        reg(int(vdst));
        reg(int(sdst));
        vdstName = "r" + std::to_string(vdst);
        sdstName = "r" + std::to_string(sdst);
    }

    // Every source is read before any destination is written.
    std::string stmt = "{ ";
    if (usesT) stmt += "vec4 t1 = " + s1 + ", t2 = " + s2 + "; ";
    if (vm || !predPush.empty() || !kill.empty()) stmt += "vec4 v = " + v + "; ";
    stmt += "float s = " + sr + "; ";
    if (!predPush.empty() || !predSet.empty()) stmt += "bool np = " + (predPush.empty() ? predSet : predPush) + "; ";
    if (!skill.empty() && pixel_) stmt += "bool sk = " + skill + "; ";
    stmt += body;
    if (vm) {
        const std::string mm = maskStr(vm);
        std::string val = vclamp ? "clamp(v, 0.0, 1.0)" : "v";
        stmt += vdstName + "." + mm + " = " + val + "." + mm + "; ";
    }
    if (sm) {
        const std::string mm = maskStr(sm);
        std::string val = sclamp ? "clamp(s, 0.0, 1.0)" : "s";
        stmt += sdstName + "." + mm + " = " + (mm.size() > 1 ? "vec" + std::to_string(mm.size()) + "(" + val + ")" : val) + "; ";
    }
    if (one) {
        const std::string mm = maskStr(one);
        stmt += vdstName + "." + mm + " = " + (mm.size() > 1 ? "vec" + std::to_string(mm.size()) + "(1.0)" : "1.0") + "; ";
    }
    if (zero) {
        const std::string mm = maskStr(zero);
        stmt += vdstName + "." + mm + " = " + (mm.size() > 1 ? "vec" + std::to_string(mm.size()) + "(0.0)" : "0.0") + "; ";
    }
    if (!kill.empty() && pixel_) stmt += "if (" + kill + ") discard; ";
    if (!skill.empty() && pixel_) stmt += "if (sk) discard; ";
    if (!body.empty()) stmt += "a0 = na; ";
    if (!predPush.empty() || !predSet.empty()) stmt += "p0 = np; ";
    stmt += "pv = sclampv(s, " + std::string(sclamp ? "true" : "false") + "); }";
    // ps holds the scalar result as written (with its clamp).
    if (predicated) stmt = std::string("if (") + (predCond ? "p0" : "!p0") + ") " + stmt;
    line(stmt);
}

void Translator::fetch(const uint32_t* w, uint32_t address) {
    const uint32_t op = bits(w[0], 0, 5);
    const uint32_t src = bits(w[0], 5, 6), dst = bits(w[0], 12, 6);
    if (bits(w[0], 11, 1) || bits(w[0], 18, 1)) throw std::runtime_error("relative fetch register");
    reg(int(src));
    reg(int(dst));
    const uint32_t dsw = bits(w[1], 0, 12);
    const bool predicated = bits(w[1], 31, 1) != 0, predCond = bits(w[2], 31, 1) != 0;
    std::string value;
    if (op == 0) {
        int loc = -1;
        for (const matlib::Fetch& f : s_.fetches)
            if (f.address == address) loc = attribLocation(f.usage, f.index);
        if (loc >= 0) {
            attribLocs_.insert(loc);
            attribs_ = uint16_t(attribs_ | (1u << loc));
            value = "aIn" + std::to_string(loc);
        } else {
            value = "vec4(0.0, 0.0, 0.0, 1.0)";
        }
    } else if (op == 1) {
        uint32_t tf = bits(w[0], 20, 5);
        if (tf > 15 && pixel_) throw std::runtime_error("texture fetch constant " + std::to_string(tf));
        tf &= 15;  // vertex textures: fetch constants 16..31 [I: no vertex textures on venues or characters]
        const uint32_t ssw = bits(w[0], 26, 6);
        std::string coord = "r" + std::to_string(src) + ".";
        for (int k = 0; k < 3; ++k) coord += kComp[(ssw >> (2 * k)) & 3];
        const uint32_t dim = bits(w[2], 14, 2);
        const bool denorm = bits(w[0], 25, 1) != 0;
        const int ox = int(int8_t(uint8_t(bits(w[2], 16, 5) << 3)) >> 3), oy = int(int8_t(uint8_t(bits(w[2], 21, 5) << 3)) >> 3);
        const int lodBias = int(int8_t(uint8_t(bits(w[2], 2, 7) << 1)) >> 1);
        samplers_ = uint16_t(samplers_ | (1u << tf));
        const std::string sname = "sT" + std::to_string(tf);
        if (dim == 3) {
            cubes_ = uint16_t(cubes_ | (1u << tf));
            value = pixel_ ? "texture(" + sname + ", gdCubeDir(" + coord + "))" : "textureLod(" + sname + ", gdCubeDir(" + coord + "), 0.0)";
        } else {
            std::string uv = "(" + coord + ").xy";
            if (denorm) uv = "(" + uv + " / vec2(textureSize(" + sname + ", 0)))";
            if (ox || oy)
                uv = "(" + uv + " + vec2(" + fmtFloat(float(ox) * 0.5f) + ", " + fmtFloat(float(oy) * 0.5f) + ") / vec2(textureSize(" + sname + ", 0)))";
            if (!pixel_) value = "textureLod(" + sname + ", " + uv + ", 0.0)";
            else if (lodBias) value = "texture(" + sname + ", " + uv + ", " + fmtFloat(float(lodBias) / 16.0f) + ")";
            else value = "texture(" + sname + ", " + uv + ")";
        }
    } else {
        // LOD / gradient queries and overrides (special effects only): no-ops.
        return;
    }
    std::string stmt = "{ vec4 f = " + value + "; ";
    for (int k = 0; k < 4; ++k) {
        const uint32_t c = (dsw >> (3 * k)) & 7;
        if (c == 7) continue;
        const std::string d = "r" + std::to_string(dst) + "." + kComp[k];
        if (c < 4) stmt += d + " = f." + kComp[c] + "; ";
        else if (c == 4) stmt += d + " = 0.0; ";
        else if (c == 5) stmt += d + " = 1.0; ";
    }
    stmt += "}";
    if (predicated) stmt = std::string("if (") + (predCond ? "p0" : "!p0") + ") " + stmt;
    line(stmt);
}

void Translator::run() {
    const std::vector<CfInstr> cf = controlFlow(s_.ucode);
    // Straight-line code unless the program jumps: then a pc loop over the
    // control flow instructions (particles, water).
    bool jumps = false;
    for (const CfInstr& c : cf) {
        if (c.op() == kCondJmp) jumps = true;
        else if (c.op() == kLoopStart || c.op() == kLoopEnd || c.op() == kCondCall || c.op() == kReturn)
            throw std::runtime_error(std::string("control flow opcode ") + std::to_string(int(c.op())));
    }
    if (jumps) {
        line("int pc = 0;");
        line("for (int guard = 0; guard < 4096; ++guard) {");
        indent_ += "    ";
        line("switch (pc) {");
    }
    for (size_t ci = 0; ci < cf.size(); ++ci) {
        const CfInstr& c = cf[ci];
        const Cf op = c.op();
        if (jumps) {
            line("case " + std::to_string(ci) + ": {");
            indent_ += "    ";
        }
        std::string cond;
        if (op == kCondExec || op == kCondExecEnd || op == kCondJmp) {
            // A conditional jump: bit 13 unconditional, bit 14 on the predicate.
            if (op == kCondJmp && bits(c.v, 13, 1)) cond = "(true)";
            else if (op == kCondJmp && bits(c.v, 14, 1)) cond = c.condition() ? "p0" : "!p0";
            else cond = std::string("(gdBool(") + std::to_string(c.boolAddress()) + ") == " + (c.condition() ? "true" : "false") + ")";
        } else if (op == kCondExecPred || op == kCondExecPredEnd || op == kCondExecPredClean || op == kCondExecPredCleanEnd) {
            cond = c.condition() ? "p0" : "!p0";
        }
        if (op == kCondJmp) {
            line("if " + (cond[0] == '(' ? cond : "(" + cond + ")") + " { pc = " + std::to_string(bits(c.v, 0, 13)) + "; break; }");
        } else if (isExec(op)) {
            if (!cond.empty()) {
                line("if " + (cond[0] == '(' ? cond : "(" + cond + ")") + " {");
                indent_ += "    ";
            }
            for (uint32_t k = 0; k < c.count(); ++k) {
                const uint32_t addr = c.address() + k;
                if (size_t(addr) * 3 + 2 >= s_.ucode.size()) throw std::runtime_error("instruction past the microcode");
                const uint32_t* w = &s_.ucode[size_t(addr) * 3];
                if ((c.sequence() >> (2 * k)) & 1) fetch(w, addr);
                else alu(w);
            }
            if (isEnd(op)) line("return;");
            if (!cond.empty()) {
                indent_.resize(indent_.size() - 4);
                line("}");
            }
        }
        if (jumps) {
            line("pc = " + std::to_string(ci + 1) + "; break;");
            indent_.resize(indent_.size() - 4);
            line("}");
        }
    }
    if (jumps) {
        line("default: return;");
        line("}");
        indent_.resize(indent_.size() - 4);
        line("}");
    }
}

const char* kCommon = R"(
layout(std140) uniform GdXG { vec4 cG[128]; };
layout(std140) uniform GdXO { vec4 cO[28]; };
float gdMax4(vec4 v) { return max(max(v.x, v.y), max(v.z, v.w)); }
float gdRcpFF(float x) { float r = 1.0 / x; return isinf(r) ? 0.0 : r; }
float sclampv(float s, bool c) { return c ? clamp(s, 0.0, 1.0) : s; }
// CUBE: operands (z, z, x, y) and (y, x, z, z) of the direction; result
// (t, s, 2 * major axis, face).
vec4 gdCube(vec4 a, vec4 b) {
    vec3 d = vec3(a.z, a.w, a.x);
    vec3 ad = abs(d);
    float sc, tc, ma, face;
    if (ad.z >= ad.x && ad.z >= ad.y) { ma = d.z; face = d.z < 0.0 ? 5.0 : 4.0; sc = d.z < 0.0 ? -d.x : d.x; tc = -d.y; }
    else if (ad.y >= ad.x) { ma = d.y; face = d.y < 0.0 ? 3.0 : 2.0; sc = d.x; tc = d.y < 0.0 ? -d.z : d.z; }
    else { ma = d.x; face = d.x < 0.0 ? 1.0 : 0.0; sc = d.x < 0.0 ? d.z : -d.z; tc = -d.y; }
    return vec4(tc, sc, 2.0 * abs(ma), face);
}
// A cube fetch's (s, t, face) back to a direction (s, t in 1..2, as the
// shaders compute them from CUBE's result).
vec3 gdCubeDir(vec3 c) {
    float sc = 2.0 * c.x - 3.0, tc = 2.0 * c.y - 3.0;
    int f = int(floor(c.z + 0.5));
    if (f == 0) return vec3(1.0, -tc, -sc);
    if (f == 1) return vec3(-1.0, -tc, sc);
    if (f == 2) return vec3(sc, 1.0, tc);
    if (f == 3) return vec3(sc, -1.0, -tc);
    if (f == 4) return vec3(sc, -tc, 1.0);
    return vec3(-sc, -tc, -1.0);
}
)";

std::string declarations(const Translator& t, const char* matBlock) {
    std::string s = kCommon;
    s += std::string("layout(std140) uniform ") + matBlock + " { vec4 " + (t.pixel_ ? "cP" : "cV") + "[64]; };\n";
    s += std::string("bool gdBool(int i) { return ((int(") + (t.pixel_ ? "cP" : "cV") +
         "[63].y) >> i) & 1) != 0; }\n";
    s += std::string("vec4 gdC(int i) { if (i >= 176 && i < 192) return cO[i - 176]; if (i >= 216 && i < 228) return cO[i - 200];\n"
                     "    return i >= 128 ? cG[clamp(i - 128, 0, 127)] : (i < 64 ? ") + (t.pixel_ ? "cP" : "cV") +
         "[clamp(i, 0, 63)] : vec4(0.0)); }\n";
    for (int k = 0; k < 16; ++k) {
        if (!(t.samplers_ >> k & 1)) continue;
        s += std::string("uniform ") + ((t.cubes_ >> k & 1) ? "samplerCube" : "sampler2D") + " sT" + std::to_string(k) + ";\n";
    }
    for (int r = 0; r <= t.maxReg_; ++r) s += "vec4 r" + std::to_string(r) + " = vec4(0.0);\n";
    s += "float pv = 0.0;\nbool p0 = false;\nint a0 = 0;\n";
    return s;
}

}  // namespace

int attribLocation(uint8_t usage, uint8_t index) {
    switch (usage) {
    case 0: return index == 0 ? 0 : index == 1 ? 12 : -1;  // POSITION0, POSITION1 (the flares' quad centre)
    case 3: return index == 0 ? 1 : -1;               // NORMAL
    case 10: return index == 0 ? 2 : index == 1 ? 10 : -1;  // COLOR
    case 5: {  // TEXCOORD0..6 -> 3, 6, 8, 9, 13, 14, 15 (venue meshes declare their shadow map's set as 4 or 5)
        static const int loc[7] = {3, 6, 8, 9, 13, 14, 15};
        return index < 7 ? loc[index] : -1;
    }
    case 1: return index == 0 ? 4 : -1;               // BLENDWEIGHT
    case 6: return index == 0 ? 5 : -1;               // TANGENT
    case 7: return index == 0 ? 7 : -1;               // BINORMAL
    case 2: return index == 0 ? 11 : -1;              // BLENDINDICES
    default: return -1;
    }
}

Program translate(const matlib::Shader& vs, const matlib::Shader& ps) {
    Program p;
    try {
        if (vs.pixel || !ps.pixel) throw std::runtime_error("not a vertex + pixel shader pair");
        Translator tv(vs, false), tp(ps, true);
        tv.run();
        tp.run();
        // Pixel inputs: the vertex exports of the same semantic.
        std::string vOut, vAssign, pIn, pLoad;
        std::set<int> vsSem;
        for (const matlib::Link& l : vs.links) {
            if (l.reg > 15) continue;
            vsSem.insert(l.semantic);
            vOut += "out vec4 gx" + std::to_string(l.semantic) + ";\n";
            vAssign += "    gx" + std::to_string(l.semantic) + " = e" + std::to_string(l.reg) + ";\n";
            tv.exports_.insert(l.reg);
        }
        for (const matlib::Link& l : ps.links) {
            tp.reg(l.reg);
            if (!vsSem.count(l.semantic)) continue;
            pIn += "in vec4 gx" + std::to_string(l.semantic) + ";\n";
            pLoad += "    r" + std::to_string(l.reg) + " = gx" + std::to_string(l.semantic) + ";\n";
        }

        std::string v = declarations(tv, "GdXVM");
        for (int loc : tv.attribLocs_) v += "layout(location = " + std::to_string(loc) + ") in vec4 aIn" + std::to_string(loc) + ";\n";
        v += "vec4 e62 = vec4(0.0, 0.0, 0.0, 1.0);\nvec4 e63 = vec4(0.0);\n";
        for (int e : tv.exports_)
            if (e < 62) v += "vec4 e" + std::to_string(e) + " = vec4(0.0);\n";
        v += vOut;
        v += "void gdRun() {\n" + tv.code_ + "}\n";
        v += "void main() {\n    gdRun();\n" + vAssign +
             "#ifdef GD_CLIP_ZO\n    gl_Position = e62;\n#else\n    gl_Position = vec4(e62.xy, 2.0 * e62.z - e62.w, e62.w);\n#endif\n}\n";

        std::string f = declarations(tp, "GdXPM");
        f += pIn;
        f += "out vec4 fragColor;\n";
        for (int e = 0; e < 4; ++e) f += "vec4 e" + std::to_string(e) + " = vec4(0.0);\n";
        if (tp.exports_.count(61)) f += "vec4 e61 = vec4(0.0);\n";
        for (int e : tp.exports_)
            if (e > 3 && e != 61) throw std::runtime_error("pixel export " + std::to_string(e));
        f += "void gdRun() {\n" + tp.code_ + "}\n";
        f += "void main() {\n" + pLoad + "    gdRun();\n    if (cP[63].x >= 0.0 && e0.a < cP[63].x) discard;\n    fragColor = e0;\n";
        if (tp.exports_.count(61)) f += "    gl_FragDepth = clamp(e61.x, 0.0, 1.0);\n";
        f += "}\n";
        p.vertex = std::move(v);
        p.fragment = std::move(f);
        p.samplers = tp.samplers_;
        p.cubes = tp.cubes_;
        p.attribs = tv.attribs_;
        p.ok = true;
    } catch (const std::exception& ex) {
        p.ok = false;
        p.error = ex.what();
    }
    return p;
}

std::string disassemble(const matlib::Shader& s) {
    std::string out;
    char b[256];
    for (const CfInstr& c : controlFlow(s.ucode)) {
        std::snprintf(b, sizeof b, "cf %d addr %u count %u seq %03x cond %d bool %u\n", int(c.op()), c.address(), c.count(),
                      c.sequence(), int(c.condition()), c.boolAddress());
        out += b;
        if (!isExec(c.op())) continue;
        for (uint32_t k = 0; k < c.count(); ++k) {
            const uint32_t a = c.address() + k;
            if (size_t(a) * 3 + 2 >= s.ucode.size()) break;
            const uint32_t* w = &s.ucode[size_t(a) * 3];
            if ((c.sequence() >> (2 * k)) & 1) {
                std::snprintf(b, sizeof b, "  %3u fetch op %u dst r%u src r%u const %u  [%08x %08x %08x]\n", a, bits(w[0], 0, 5),
                              bits(w[0], 12, 6), bits(w[0], 5, 6), bits(w[0], 20, 5), w[0], w[1], w[2]);
            } else {
                const uint32_t vop = bits(w[2], 24, 5), sop = bits(w[0], 26, 6);
                std::snprintf(b, sizeof b, "  %3u %s/%s %s%u.%x %u.%x  [%08x %08x %08x]\n", a, vop < 30 ? kVecNames[vop] : "?",
                              sop < 51 ? kScaNames[sop] : "?", bits(w[0], 15, 1) ? "e" : "r", bits(w[0], 0, 6),
                              bits(w[0], 16, 4), bits(w[0], 8, 6), bits(w[0], 20, 4), w[0], w[1], w[2]);
            }
            out += b;
        }
    }
    return out;
}

}  // namespace xenos
