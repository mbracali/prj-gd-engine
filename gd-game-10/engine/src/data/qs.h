// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "data/qb.h"

class Disc;

// gh4 (Metallica) localized strings: the .qs entries of PAK/qs.pak.xen
// (English; qs_<f|g|i|s|n>.pak.xen for the other languages) are UTF-16LE
// text with a BOM, one line per string: `<8 hex digits key> "<text>"`
// (docs/formats.md, "QS") [C data]. QB values of type 0x1C and the script
// token 0x4E name a string by that key.
namespace qstr {

// Loads the English table (once; later calls add nothing). Never throws:
// a missing pak leaves the table empty (logged).
void load(const Disc& disc);
// The text of `key`, or null when the table has no such string.
const std::string* find(uint32_t key);
// The text shown for `key`: the string, without the leading "\L" mark the
// table puts on many entries [I: the mark's meaning], or "" if unknown.
std::string text(uint32_t key);
size_t size();
// Every string of the table (tests).
void each(const std::function<void(uint32_t, const std::string&)>& fn);
// Turns every QsKey inside `v` (structs and arrays included) into a String
// with its text, so the UI code reads them as GH3's strings.
void resolve(qb::Value& v);

}  // namespace qstr
