// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// `gd02 --scripttest <iso | game folder>`: focused checks of the script
// interpreter (#merge, macros, tasks in seconds and beats, random ranges,
// handle, switch / cond, the L1 report), then every .dtb in the archive
// through the ScriptLoader (includes, merges, macros, directive counts), then
// config/gh2.dta's (system (init ...)) and ui/ui.dta's (init ...) run with stub
// objects, a smoke run of every handler of every `new` object, and a count of
// the builtins, funcs, classes, objects and methods the scripts reference.
// Returns 0 only if the checks pass, every file loads, the boot tree has no
// include errors and the boot run has no script errors.
int runScriptTest(const std::string& dataPath);

// The interpreter checks alone (no game data); returns the number of failures.
int runScriptUnitChecks();
