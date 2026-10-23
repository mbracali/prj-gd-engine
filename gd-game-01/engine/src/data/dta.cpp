// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/dta.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace {

// Stream cipher used by BinStream for .dtb files (FUN_0024a028 / FUN_0024a140).
class DtbCipher {
public:
    explicit DtbCipher(uint32_t seed) {
        for (auto& word : table_) {
            uint32_t a = seed * 0x41C64E6Du + 0x3039u;
            seed = a * 0x41C64E6Du + 0x3039u;
            word = (a >> 16) | (seed & 0x7FFF0000u);
        }
    }
    uint32_t next() {
        uint32_t v = table_[a_] ^ table_[b_];
        table_[a_] = v;
        a_ = a_ + 1 > 0xF8 ? 0 : a_ + 1;
        b_ = b_ + 1 > 0xF8 ? 0 : b_ + 1;
        return v;
    }

private:
    std::array<uint32_t, 256> table_{};
    uint32_t a_ = 0, b_ = 0x67;
};

class DtbParser {
public:
    explicit DtbParser(const Bytes& plain) : r_(plain) {}

    DataNode root() {
        if (r_.u8() != 1) throw std::runtime_error("dtb has no root array");
        DataNode n = list(DataNode::Type::Array);
        if (r_.remaining() != 0) throw std::runtime_error("dtb has trailing data");
        return n;
    }

private:
    DataNode list(DataNode::Type type) {
        DataNode n;
        n.type = type;
        uint16_t count = r_.u16le();
        r_.u16le();  // source line
        r_.u16le();  // id
        n.children.reserve(count);
        for (uint16_t k = 0; k < count; ++k) n.children.push_back(node());
        return n;
    }

    std::string text() { return r_.str(r_.u32le()); }

    DataNode node() {
        using T = DataNode::Type;
        uint32_t t = r_.u32le();
        DataNode n;
        switch (t) {
        case 0x00: n.type = T::Int; n.i = static_cast<int32_t>(r_.u32le()); break;
        case 0x01: n.type = T::Float; n.f = r_.f32le(); break;
        case 0x02: n.type = T::Var; n.s = text(); break;
        case 0x05: n.type = T::Symbol; n.s = text(); break;
        case 0x06: n.type = T::Unhandled; r_.u32le(); break;
        case 0x07: n.type = T::IfDef; n.s = text(); break;
        case 0x08: n.type = T::Else; r_.u32le(); break;
        case 0x09: n.type = T::EndIf; r_.u32le(); break;
        case 0x10: return list(T::Array);
        case 0x11: return list(T::Command);
        case 0x12: n.type = T::String; n.s = text(); break;
        case 0x13: return list(T::Property);
        case 0x20: n.type = T::Define; n.s = text(); break;  // value is the next sibling
        case 0x21: n.type = T::Include; n.s = text(); break;
        case 0x22: n.type = T::Merge; n.s = text(); break;
        case 0x23: n.type = T::IfNDef; n.s = text(); break;
        default: throw std::runtime_error("dtb has unknown node type " + std::to_string(t));
        }
        return n;
    }

    ByteReader r_;
};

}  // namespace

DataNode parseDtb(const Bytes& encrypted) {
    ByteReader seedReader(encrypted);
    DtbCipher cipher(seedReader.u32le());
    Bytes plain(encrypted.begin() + 4, encrypted.end());
    for (auto& b : plain) b ^= static_cast<uint8_t>(cipher.next());
    return DtbParser(plain).root();
}

DataNode parseDataArray(const Bytes& plain) { return DtbParser(plain).root(); }

std::string dtbPath(std::string_view dtaPath) {
    std::string p(dtaPath);
    size_t slash = p.rfind('/');
    std::string dir = slash == std::string::npos ? "" : p.substr(0, slash + 1);
    std::string file = slash == std::string::npos ? p : p.substr(slash + 1);
    if (file.size() > 4 && file.compare(file.size() - 4, 4, ".dta") == 0) file.resize(file.size() - 4);
    return dir + "gen/" + file + ".dtb";
}

const DataNode* DataNode::find(std::string_view key) const {
    for (const auto& c : children) {
        if (c.isList() && !c.children.empty() && c.children[0].isText() && c.children[0].s == key) return &c;
    }
    return nullptr;
}

int DataNode::intAt(size_t idx) const {
    if (idx >= children.size()) throw std::runtime_error("script value missing");
    const DataNode& c = children[idx];
    if (c.type == Type::Int) return c.i;
    if (c.type == Type::Float) return static_cast<int>(c.f);
    throw std::runtime_error("script value is not a number");
}

float DataNode::floatAt(size_t idx) const {
    if (idx >= children.size()) throw std::runtime_error("script value missing");
    const DataNode& c = children[idx];
    if (c.type == Type::Float) return c.f;
    if (c.type == Type::Int) return static_cast<float>(c.i);
    throw std::runtime_error("script value is not a number");
}

const std::string& DataNode::textAt(size_t idx) const {
    if (idx >= children.size() || !children[idx].isText()) throw std::runtime_error("script value is not text");
    return children[idx].s;
}

DataNode parseDtaText(std::string_view text) {
    using T = DataNode::Type;
    std::vector<DataNode> stack(1);
    std::vector<char> closers;
    stack[0].type = T::Array;
    size_t i = 0;
    auto isDelim = [](char c) { return std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' || c == ';'; };
    while (i < text.size()) {
        char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == ';') {
            while (i < text.size() && text[i] != '\n') ++i;
        } else if (c == '(' || c == '{' || c == '[') {
            DataNode n;
            n.type = c == '(' ? T::Array : c == '{' ? T::Command : T::Property;
            stack.push_back(std::move(n));
            closers.push_back(c == '(' ? ')' : c == '{' ? '}' : ']');
            ++i;
        } else if (c == ')' || c == '}' || c == ']') {
            if (closers.empty() || closers.back() != c) throw std::runtime_error("dta text: unbalanced brackets");
            closers.pop_back();
            DataNode n = std::move(stack.back());
            stack.pop_back();
            stack.back().children.push_back(std::move(n));
            ++i;
        } else if (c == '\'' || c == '"') {
            size_t end = text.find(c, i + 1);
            if (end == std::string_view::npos) throw std::runtime_error("dta text: unterminated string");
            DataNode n;
            n.type = c == '"' ? T::String : T::Symbol;
            n.s = std::string(text.substr(i + 1, end - i - 1));
            stack.back().children.push_back(std::move(n));
            i = end + 1;
        } else {
            size_t start = i;
            while (i < text.size() && !isDelim(text[i])) ++i;
            std::string tok(text.substr(start, i - start));
            DataNode n;
            char* end = nullptr;
            long iv = std::strtol(tok.c_str(), &end, 10);
            if (end && *end == 0 && !tok.empty()) {
                n.type = T::Int, n.i = int(iv);
            } else if (float fv = std::strtof(tok.c_str(), &end); end && *end == 0 && tok.find_first_of("0123456789") != std::string::npos &&
                                                                     tok.find_first_not_of("+-.0123456789eE") == std::string::npos) {
                n.type = T::Float, n.f = fv;
            } else if (tok[0] == '$') {
                n.type = T::Var, n.s = tok.substr(1);
            } else if (tok == "kDataUnhandled") {
                n.type = T::Unhandled;
            } else {
                n.type = T::Symbol, n.s = tok;
            }
            stack.back().children.push_back(std::move(n));
        }
    }
    if (!closers.empty()) throw std::runtime_error("dta text: unbalanced brackets");
    return std::move(stack[0]);
}
