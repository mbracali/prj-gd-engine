// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/dta.h"

#include <cstring>

namespace gd24 {

const DtaNode* DtaNode::find(const std::string& name) const {
    for (const DtaNode& n : array)
        if (n.type == Type::Array && !n.array.empty() && n.array[0].type == Type::Symbol && n.array[0].s == name) return &n;
    return nullptr;
}

uint32_t DtaImage::u32(uint32_t a) const {
    const uint8_t* p = d_ + (a - base_);
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool DtaImage::cstr(uint32_t a, std::string& out) const {
    out.clear();
    if (!ok(a, 1)) return false;
    for (uint32_t p = a; ok(p, 1); ++p) {
        const char c = char(d_[p - base_]);
        if (!c) return true;
        if (out.size() > 512) return false;
        out.push_back(c);
    }
    return false;
}

bool DtaImage::readArray(uint32_t header, int depth, DtaNode& out) const {
    if (!ok(header, 12)) return false;
    const uint32_t nodes = u32(header), count = u32(header + 8) & 0xFFFF;
    if (count > 4096 || !ok(nodes, count * 8)) return false;
    out = DtaNode{};
    out.type = DtaNode::Type::Array;
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t v = u32(nodes + 8 * k), t = u32(nodes + 8 * k + 4);
        DtaNode n;
        switch (t) {
        case 0: n.type = DtaNode::Type::Int, n.i = int32_t(v); break;
        case 1: n.type = DtaNode::Type::Float, std::memcpy(&n.f, &v, 4); break;
        case 5: n.type = cstr(v, n.s) ? DtaNode::Type::Symbol : DtaNode::Type::Other; break;
        case 0x12: n.type = cstr(v, n.s) ? DtaNode::Type::String : DtaNode::Type::Other; break;
        case 0x10: case 0x11: case 0x13:
            if (depth <= 0 || !readArray(v, depth - 1, n)) n.type = DtaNode::Type::Other;
            break;
        default: n.type = DtaNode::Type::Other; break;
        }
        out.array.push_back(std::move(n));
    }
    return true;
}

std::vector<DtaNode> DtaImage::arraysNamed(const std::string& head, int depth) const {
    std::vector<DtaNode> out;
    // The symbol's strings ("\0head\0"), the nodes {string, 5} pointing to
    // them, then the arrays whose node pointer is that node.
    std::vector<uint32_t> strings, nodes;
    const size_t n = head.size();
    for (uint32_t o = 1; o + n + 1 <= size_; ++o)
        if (d_[o - 1] == 0 && d_[o + n] == 0 && std::memcmp(d_ + o, head.data(), n) == 0) strings.push_back(base_ + o);
    if (strings.empty()) return out;
    for (uint32_t o = 0; o + 8 <= size_; o += 4) {
        const uint32_t v = u32(base_ + o);
        if (u32(base_ + o + 4) != 5) continue;
        for (uint32_t s : strings)
            if (v == s) nodes.push_back(base_ + o);
    }
    for (uint32_t o = 0; o + 12 <= size_; o += 4) {
        const uint32_t v = u32(base_ + o);
        for (uint32_t nd : nodes) {
            if (v != nd) continue;
            DtaNode a;
            if (readArray(base_ + o, depth, a)) out.push_back(std::move(a));
        }
    }
    return out;
}

}  // namespace gd24
