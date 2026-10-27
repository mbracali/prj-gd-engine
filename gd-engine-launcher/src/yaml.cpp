// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "yaml.h"

#include <SDL3/SDL.h>

#include <cstdlib>

namespace yaml {

namespace {

const Node kNull;

struct Line {
    int indent;
    std::string text;  // without indentation and comment
};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// Removes a comment (a # outside quotes, at the start or after a space).
std::string stripComment(const std::string& s) {
    char quote = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
            return s.substr(0, i);
        }
    }
    return s;
}

std::string unquote(const std::string& v) {
    if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
        return v.substr(1, v.size() - 2);
    return v;
}

Node scalarOrFlow(const std::string& raw) {
    Node n;
    std::string v = trim(raw);
    if (v.empty()) return n;
    if (v.front() == '[' && v.back() == ']') {
        n.kind = Node::Kind::List;
        std::string item;
        char quote = 0;
        for (size_t i = 1; i + 1 < v.size(); ++i) {
            char c = v[i];
            if (quote) {
                if (c == quote) quote = 0;
                item += c;
            } else if (c == '"' || c == '\'') {
                quote = c, item += c;
            } else if (c == ',') {
                if (!trim(item).empty()) n.list.push_back(scalarOrFlow(item));
                item.clear();
            } else {
                item += c;
            }
        }
        if (!trim(item).empty()) n.list.push_back(scalarOrFlow(item));
        return n;
    }
    n.kind = Node::Kind::Scalar;
    n.scalar = unquote(v);
    return n;
}

// The position of a mapping colon (`key: value` or `key:`), or npos.
size_t keyColon(const std::string& s) {
    if (s.empty() || s[0] == '"' || s[0] == '\'' || s[0] == '[') return std::string::npos;
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == ':' && (i + 1 == s.size() || s[i + 1] == ' ')) return i;
    return std::string::npos;
}

class Parser {
public:
    explicit Parser(std::vector<Line> lines) : lines_(std::move(lines)) {}
    Node block(int indent) {
        if (pos_ >= lines_.size()) return {};
        if (lines_[pos_].text.rfind("- ", 0) == 0 || lines_[pos_].text == "-") return list(lines_[pos_].indent);
        return map(indent);
    }

private:
    Node value(const std::string& rest, int parentIndent) {
        if (!trim(rest).empty()) return scalarOrFlow(rest);
        if (pos_ < lines_.size() && (lines_[pos_].indent > parentIndent ||
                                     (lines_[pos_].indent == parentIndent && lines_[pos_].text.rfind("- ", 0) == 0)))
            return block(lines_[pos_].indent);
        return {};
    }
    Node map(int indent) {
        Node n;
        n.kind = Node::Kind::Map;
        while (pos_ < lines_.size() && lines_[pos_].indent == indent) {
            const std::string& t = lines_[pos_].text;
            size_t colon = keyColon(t);
            if (colon == std::string::npos || t.rfind("- ", 0) == 0) break;
            std::string key = unquote(trim(t.substr(0, colon)));
            std::string rest = t.substr(colon + 1);
            ++pos_;
            n.map[key] = value(rest, indent);
        }
        return n;
    }
    Node list(int indent) {
        Node n;
        n.kind = Node::Kind::List;
        while (pos_ < lines_.size() && lines_[pos_].indent == indent &&
               (lines_[pos_].text.rfind("- ", 0) == 0 || lines_[pos_].text == "-")) {
            std::string item = lines_[pos_].text.size() > 2 ? trim(lines_[pos_].text.substr(2)) : std::string();
            if (keyColon(item) != std::string::npos) {
                // "- key: value" starts a map whose keys sit at the item's column.
                lines_[pos_].indent = indent + 2;
                lines_[pos_].text = item;
                n.list.push_back(map(indent + 2));
            } else {
                ++pos_;
                n.list.push_back(value(item, indent));
            }
        }
        return n;
    }
    std::vector<Line> lines_;
    size_t pos_ = 0;
};

}  // namespace

const Node& Node::operator[](const std::string& key) const {
    if (kind != Kind::Map) return kNull;
    auto it = map.find(key);
    return it == map.end() ? kNull : it->second;
}

double Node::num(double fallback) const {
    if (kind != Kind::Scalar || scalar.empty()) return fallback;
    char* end = nullptr;
    double v = std::strtod(scalar.c_str(), &end);
    return end && *end == 0 ? v : fallback;
}

bool Node::flag(bool fallback) const {
    if (kind != Kind::Scalar) return fallback;
    std::string s;
    for (char c : scalar) s += char(c >= 'A' && c <= 'Z' ? c + 32 : c);
    if (s == "true" || s == "yes" || s == "on") return true;
    if (s == "false" || s == "no" || s == "off") return false;
    return fallback;
}

std::vector<std::string> Node::strings() const {
    std::vector<std::string> out;
    if (kind == Kind::Scalar) out.push_back(scalar);
    for (const Node& n : list)
        if (n.kind == Kind::Scalar) out.push_back(n.scalar);
    return out;
}

Node parse(const std::string& text) {
    std::vector<Line> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string raw = stripComment(text.substr(start, end - start));
        size_t ind = raw.find_first_not_of(' ');
        std::string t = trim(raw);
        if (!t.empty() && t != "---") lines.push_back({int(ind), t});
        start = end + 1;
    }
    if (lines.empty()) return {};
    Parser p(std::move(lines));
    return p.block(0);
}

Node load(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return {};
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    return parse(text);
}

}  // namespace yaml
