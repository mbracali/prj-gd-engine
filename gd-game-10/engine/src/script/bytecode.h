// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "data/qb.h"

// QScript bytecode tokens (port of tools/qbscript.py; docs/formats.md
// "QScript bytecode"). Operands are little-endian.
namespace qs {

enum Op : uint8_t {
    kNop = 0x00, kEndOfLine = 0x01, kEndOfLineNumber = 0x02, kStartStruct = 0x03, kEndStruct = 0x04,
    kStartArray = 0x05, kEndArray = 0x06, kEquals = 0x07, kDot = 0x08, kComma = 0x09, kMinus = 0x0A,
    kAdd = 0x0B, kDivide = 0x0C, kMultiply = 0x0D, kOpenParen = 0x0E, kCloseParen = 0x0F, kDebugInfo = 0x10,
    kSameAs = 0x11, kLessThan = 0x12, kLessThanEqual = 0x13, kGreaterThan = 0x14, kGreaterThanEqual = 0x15,
    kName = 0x16, kInteger = 0x17, kHexInteger = 0x18, kEnum = 0x19, kFloat = 0x1A, kString = 0x1B,
    kLocalString = 0x1C, kArray = 0x1D, kVector = 0x1E, kPair = 0x1F, kBegin = 0x20, kRepeat = 0x21,
    kBreak = 0x22, kScript = 0x23, kEndScript = 0x24, kIf = 0x25, kElse = 0x26, kElseIf = 0x27, kEndIf = 0x28,
    kReturn = 0x29, kUndefined = 0x2A, kChecksumTable = 0x2B, kAllArgs = 0x2C, kArg = 0x2D, kJump = 0x2E,
    kRandom = 0x2F, kRandomRange = 0x30, kAt = 0x31, kOr = 0x32, kAnd = 0x33, kXor = 0x34, kShiftLeft = 0x35,
    kShiftRight = 0x36, kRandom2 = 0x37, kRandomRange2 = 0x38, kNot = 0x39, kAnd2 = 0x3A, kOr2 = 0x3B,
    kSwitch = 0x3C, kEndSwitch = 0x3D, kCase = 0x3E, kDefault = 0x3F, kRandomNoRepeat = 0x40,
    kRandomPermute = 0x41, kColon = 0x42, kFastIf = 0x47, kFastElse = 0x48, kShortJump = 0x49,
    kInlineStruct = 0x4A, kGlobal = 0x4B, kWideString = 0x4C, kNotEqual = 0x4D, kInlineStruct2 = 0x4E,
};

struct Token {
    uint32_t at = 0;    // byte offset in the script
    uint32_t end = 0;   // offset after the token
    Op op = kNop;
    uint32_t u = 0;     // name key, int (as bits), u32 / u16 operands (first)
    uint32_t u2 = 0;    // second u16 (elseif)
    float f[3] = {0, 0, 0};
    std::string str;    // strings (without the NUL)
    std::shared_ptr<qb::Struct> st;  // inline structs
    std::vector<uint16_t> weights;   // random
    std::vector<int32_t> offsets;
    int32_t i() const { return int32_t(u); }
};

// Tokenizes a script; throws std::runtime_error on an unknown opcode or a
// truncated operand.
std::vector<Token> tokenize(const uint8_t* code, size_t size);
const char* opName(Op op);

}  // namespace qs
