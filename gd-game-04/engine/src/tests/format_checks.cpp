// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/format_checks.h"

#include <cstdio>
#include <exception>

#include "data/qb.h"
#include "data/qbkey.h"
#include "data/scene.h"

namespace {

constexpr uint32_t kQb = qbkey::key(".qb");
constexpr uint32_t kMqb = qbkey::key(".mqb");
constexpr uint32_t kSqb = qbkey::key(".sqb");

bool checkQb(const Pak& pak, const PakEntry& e, FormatStats& s, std::string& what) {
    auto span = pak.data(e);
    size_t unused = 0;
    try {
        qb::File f = qb::decode(span.first, span.second, &unused);
        ++s.qbFiles;
        s.qbSections += f.sections.size();
        for (const qb::Section& sec : f.sections)
            if (sec.value.type == qb::Type::Script) ++s.qbScripts;
        s.qbUnused += unused;
        if (f.fileKey != e.fullName && !f.sections.empty()) {
            what = "qb file key differs from the pak entry";
            return false;
        }
        if (unused) {
            what = std::to_string(unused) + " bytes not accounted for";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        what = ex.what();
        return false;
    }
}

// Scenes, models, skins (one format) and Havok collision (data/scene.h).
bool checkScene(const Pak& pak, const PakEntry& e, std::string& what) {
    auto [p, n] = pak.data(e);
    try {
        if (e.type == qbkey::key(".hkc")) scene::parseHavok(p, n);
        else
            scene::parse(p, n, e.type == qbkey::key(".skin") ? scene::Kind::Skin
                               : e.type == qbkey::key(".mdl") ? scene::Kind::Mdl
                                                              : scene::Kind::Scn);
        return true;
    } catch (const std::exception& ex) {
        what = ex.what();
        return false;
    }
}

}  // namespace

bool checkPakFile(const Pak& pak, const PakEntry& e, FormatStats& s, std::string& what) {
    const std::string type = qbkey::name(e.type);
    bool ok = true;
    if (e.type == kQb || e.type == kMqb || e.type == kSqb) ok = checkQb(pak, e, s, what);
    else if (e.type == qbkey::key(".scn") || e.type == qbkey::key(".mdl") || e.type == qbkey::key(".skin") ||
             e.type == qbkey::key(".hkc"))
        ok = checkScene(pak, e, what);
    else {
        ++s.notDecoded[type];
        return true;
    }
    if (ok) ++s.decoded[type];
    return ok;
}

void printFormatStats(const FormatStats& s) {
    for (auto& [t, n] : s.decoded) std::printf("decoded      %-8s %6zu\n", t.c_str(), n);
    for (auto& [t, n] : s.notDecoded) std::printf("not decoded  %-8s %6zu (no decoder yet)\n", t.c_str(), n);
}
