// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

class GameData;

// The asset / reference audit (kickoff section 4 rule 3, `--audittest`):
// while enabled, every scene object the engine looks up by name is recorded
// (scene::Directory::get) and every name a lookup failed to resolve
// (Directory::resolve). writeReport() then compares those with every object
// name the game's scripts reference (each .dtb's symbols and strings with a
// scene suffix: .mesh, .mat, .view, .grp, .tnm, ...) and every sound the
// banks define, and writes a markdown report: per script file, the names the
// engine never looked up during the run (not drawn / played / resolved) and
// the ones it asked for but could not find.
namespace audit {

void enable();
bool enabled();
void touch(const std::string& name);
void miss(const std::string& name);
// Writes the report to `path`; prints a summary to stdout. Returns the
// number of referenced names never resolved (0 = no gap).
int writeReport(const GameData& data, const std::string& path, const std::string& run);

}  // namespace audit
