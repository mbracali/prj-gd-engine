// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The small part of YAML the launcher's files use (games.yaml,
// game-info.yaml, launcher-info.yaml): `key: value` maps, `- item` lists of
// scalars or of maps, `[a, b]` flow lists, quoted strings and # comments,
// nested by indentation.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace yaml {

struct Node {
    enum class Kind { Null, Scalar, List, Map } kind = Kind::Null;
    std::string scalar;
    std::vector<Node> list;
    std::map<std::string, Node> map;

    const Node& operator[](const std::string& key) const;
    std::string str(const std::string& fallback = "") const { return kind == Kind::Scalar ? scalar : fallback; }
    double num(double fallback = 0) const;
    bool flag(bool fallback = false) const;
    std::vector<std::string> strings() const;  // a list of scalars (or one scalar)
};

// Parses `text`; on a syntax it does not know, keeps what it read so far.
Node parse(const std::string& text);
// Reads and parses a file (UTF-8 path); Null when it cannot be read.
Node load(const std::string& path);

}  // namespace yaml
