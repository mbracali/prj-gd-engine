// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "script/bytecode.h"

#include <cstring>
#include <stdexcept>

namespace qs {

namespace {

enum class Kind : uint8_t { None, U32, F32, Vec2, Vec3, U16, U16x2, Str, WStr, Random, Struct, Table, Bad };

Kind kindOf(uint8_t op) {
    switch (op) {
    case kEndOfLineNumber: case kName: case kInteger: case kHexInteger: case kEnum: case kJump: return Kind::U32;
    case kFloat: return Kind::F32;
    case kVector: return Kind::Vec3;
    case kPair: return Kind::Vec2;
    case kFastIf: case kFastElse: case kShortJump: return Kind::U16;
    case kElseIf: return Kind::U16x2;
    case kString: return Kind::Str;
    case kLocalString: case kWideString: return Kind::WStr;
    case kRandom: case kRandom2: case kRandomNoRepeat: case kRandomPermute: return Kind::Random;
    case kInlineStruct: case kInlineStruct2: return Kind::Struct;
    case kChecksumTable: return Kind::Table;
    default: break;
    }
    if (op <= kColon || op == kGlobal || op == kNotEqual) return Kind::None;
    return Kind::Bad;
}

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
float rdf(const uint8_t* p) {
    uint32_t v = rd32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

}  // namespace

std::vector<Token> tokenize(const uint8_t* code, size_t n) {
    std::vector<Token> out;
    size_t i = 0;
    auto need = [&](size_t k, size_t at) {
        if (i + k > n) throw std::runtime_error("qscript: operand past the end at " + std::to_string(at));
    };
    while (i < n) {
        Token t;
        t.at = uint32_t(i);
        const uint8_t op = code[i++];
        const Kind kind = kindOf(op);
        if (kind == Kind::Bad) throw std::runtime_error("qscript: unknown opcode " + std::to_string(op) + " at " + std::to_string(t.at));
        t.op = Op(op);
        switch (kind) {
        case Kind::None: break;
        case Kind::U32: need(4, t.at), t.u = rd32(code + i), i += 4; break;
        case Kind::F32: need(4, t.at), t.f[0] = rdf(code + i), i += 4; break;
        case Kind::Vec2: need(8, t.at), t.f[0] = rdf(code + i), t.f[1] = rdf(code + i + 4), i += 8; break;
        case Kind::Vec3:
            need(12, t.at);
            for (int k = 0; k < 3; ++k) t.f[k] = rdf(code + i + 4 * size_t(k));
            i += 12;
            break;
        case Kind::U16: need(2, t.at), t.u = rd16(code + i), i += 2; break;
        case Kind::U16x2: need(4, t.at), t.u = rd16(code + i), t.u2 = rd16(code + i + 2), i += 4; break;
        case Kind::Str: {
            need(4, t.at);
            const uint32_t len = rd32(code + i);
            i += 4;
            need(len, t.at);
            if (len == 0 || code[i + len - 1] != 0) throw std::runtime_error("qscript: string without NUL at " + std::to_string(t.at));
            t.str.assign(reinterpret_cast<const char*>(code + i), len - 1);
            i += len;
            break;
        }
        case Kind::WStr: {
            need(4, t.at);
            const uint32_t len = rd32(code + i);
            i += 4;
            need(len, t.at);
            // UTF-16 (big-endian, as the QB files) to UTF-8 [I: byte order].
            for (uint32_t k = 0; k + 1 < len; k += 2) {
                const uint32_t c = uint32_t(code[i + k]) << 8 | code[i + k + 1];
                if (!c) break;
                if (c < 0x80) t.str += char(c);
                else if (c < 0x800) t.str += char(0xC0 | c >> 6), t.str += char(0x80 | (c & 0x3F));
                else t.str += char(0xE0 | c >> 12), t.str += char(0x80 | ((c >> 6) & 0x3F)), t.str += char(0x80 | (c & 0x3F));
            }
            i += len;
            break;
        }
        case Kind::Random: {
            need(4, t.at);
            const uint32_t cnt = rd32(code + i);
            i += 4;
            need(size_t(cnt) * 6, t.at);
            for (uint32_t k = 0; k < cnt; ++k) t.weights.push_back(rd16(code + i + 2 * size_t(k)));
            i += 2 * size_t(cnt);
            for (uint32_t k = 0; k < cnt; ++k) t.offsets.push_back(int32_t(rd32(code + i + 4 * size_t(k))));
            i += 4 * size_t(cnt);
            break;
        }
        case Kind::Struct: {
            need(2, t.at);
            const uint16_t len = rd16(code + i);
            i += 2;
            while (i % 4) ++i;
            need(len, t.at);
            t.st = qb::decodeStruct(code + i, len);
            i += len;
            break;
        }
        case Kind::Table: i = n; break;
        case Kind::Bad: break;
        }
        t.end = uint32_t(i);
        out.push_back(std::move(t));
    }
    return out;
}

const char* opName(Op op) {
    switch (op) {
    case kName: return "name";
    case kEndOfLine: return "eol";
    case kInteger: return "int";
    case kFloat: return "float";
    case kString: return "string";
    case kFastIf: return "if";
    case kFastElse: return "else";
    case kElseIf: return "elseif";
    case kEndIf: return "endif";
    case kBegin: return "begin";
    case kRepeat: return "repeat";
    case kSwitch: return "switch";
    case kCase: return "case";
    default: return "op";
    }
}

}  // namespace qs
