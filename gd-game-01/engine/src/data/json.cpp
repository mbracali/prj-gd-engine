// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/json.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {

void escape(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void write(std::string& out, const Json& j, int indent, int depth) {
    auto newline = [&](int d) {
        if (indent <= 0) return;
        out += '\n';
        out.append(size_t(d * indent), ' ');
    };
    switch (j.type) {
    case Json::Type::Null: out += "null"; break;
    case Json::Type::Bool: out += j.b ? "true" : "false"; break;
    case Json::Type::Number: {
        char buf[32];
        if (std::fabs(j.n - std::round(j.n)) < 1e-9 && std::fabs(j.n) < 1e15) std::snprintf(buf, sizeof buf, "%lld", (long long)std::llround(j.n));
        else std::snprintf(buf, sizeof buf, "%.9g", j.n);
        out += buf;
        break;
    }
    case Json::Type::String: escape(out, j.s); break;
    case Json::Type::Array:
        out += '[';
        for (size_t i = 0; i < j.a.size(); ++i) {
            if (i) out += ',';
            newline(depth + 1);
            write(out, j.a[i], indent, depth + 1);
        }
        if (!j.a.empty()) newline(depth);
        out += ']';
        break;
    case Json::Type::Object: {
        out += '{';
        bool first = true;
        for (const auto& [k, v] : j.o) {
            if (!first) out += ',';
            first = false;
            newline(depth + 1);
            escape(out, k);
            out += indent > 0 ? ": " : ":";
            write(out, v, indent, depth + 1);
        }
        if (!j.o.empty()) newline(depth);
        out += '}';
        break;
    }
    }
}

struct Parser {
    const std::string& t;
    size_t p = 0;
    [[noreturn]] void fail() { throw std::runtime_error("json: bad input at " + std::to_string(p)); }
    void ws() {
        while (p < t.size() && (t[p] == ' ' || t[p] == '\n' || t[p] == '\r' || t[p] == '\t')) ++p;
    }
    bool lit(const char* s) {
        size_t n = std::char_traits<char>::length(s);
        if (t.compare(p, n, s) != 0) return false;
        p += n;
        return true;
    }
    std::string str() {
        if (t[p] != '"') fail();
        ++p;
        std::string out;
        while (p < t.size() && t[p] != '"') {
            char c = t[p++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= t.size()) fail();
            char e = t[p++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'u': {
                if (p + 4 > t.size()) fail();
                unsigned v = unsigned(std::stoul(t.substr(p, 4), nullptr, 16));
                p += 4;
                if (v < 0x80) out += char(v);
                else if (v < 0x800) out += char(0xC0 | (v >> 6)), out += char(0x80 | (v & 0x3F));
                else out += char(0xE0 | (v >> 12)), out += char(0x80 | ((v >> 6) & 0x3F)), out += char(0x80 | (v & 0x3F));
                break;
            }
            default: out += e;
            }
        }
        if (p >= t.size()) fail();
        ++p;
        return out;
    }
    Json value() {
        ws();
        if (p >= t.size()) fail();
        char c = t[p];
        if (c == '{') {
            ++p;
            Json j = Json::object();
            ws();
            if (t[p] == '}') return ++p, j;
            for (;;) {
                ws();
                std::string k = str();
                ws();
                if (t[p++] != ':') fail();
                j.o[k] = value();
                ws();
                if (t[p] == ',') {
                    ++p;
                    continue;
                }
                if (t[p++] != '}') fail();
                return j;
            }
        }
        if (c == '[') {
            ++p;
            Json j = Json::array();
            ws();
            if (t[p] == ']') return ++p, j;
            for (;;) {
                j.a.push_back(value());
                ws();
                if (t[p] == ',') {
                    ++p;
                    continue;
                }
                if (t[p++] != ']') fail();
                return j;
            }
        }
        if (c == '"') return Json::string(str());
        if (lit("true")) return Json::boolean(true);
        if (lit("false")) return Json::boolean(false);
        if (lit("null")) return Json{};
        size_t used = 0;
        double v = std::stod(t.substr(p, 40), &used);
        if (used == 0) fail();
        p += used;
        return Json::number(v);
    }
};

}  // namespace

const Json& Json::operator[](const std::string& key) const {
    static const Json null;
    if (type != Type::Object) return null;
    auto it = o.find(key);
    return it == o.end() ? null : it->second;
}

std::string Json::dump(int indent) const {
    std::string out;
    write(out, *this, indent, 0);
    return out;
}

Json Json::parse(const std::string& text) {
    Parser ps{text};
    Json j = ps.value();
    return j;
}
