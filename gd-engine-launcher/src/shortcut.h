// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// "Add instant icon" (#141): a desktop shortcut that starts one game directly,
// bypassing the launcher's list. It reuses the launcher's own command line
// (--play <id> --iso <disc> --exit-after). Linux writes a .desktop file,
// Windows a .lnk, macOS a double-clickable .command. The shortcut's visible
// name is exactly "(GD-ENGINE) <game name>".
#pragma once

#include <string>
#include <vector>

namespace shortcut {

// "(GD-ENGINE) <game name>": the exact label every shortcut carries (#141).
std::string shortcutName(const std::string& gameName);

// The launcher command line that starts a game at once with its disc, as
// separate argument tokens (no shell): --play <id> --iso <disc> --exit-after.
std::vector<std::string> launchArgs(const std::string& gameId, const std::string& isoPath);

// The body of a Linux .desktop file. Pure (no I/O): exec and each argument are
// quoted as the Desktop Entry spec needs, so a disc path with spaces is safe.
std::string desktopEntry(const std::string& name, const std::string& exec,
                         const std::vector<std::string>& args, const std::string& icon);

// Whether this platform can make a shortcut at all (false on Android, and on a
// system where the launcher's own path cannot be found).
bool supported();

// Create the shortcut for one game. gameName is the plain title (the function
// adds the "(GD-ENGINE) " prefix). On success returns true and outPath is the
// file written (the Desktop copy when there is a Desktop, else the one in the
// applications menu); otherwise false and error says why.
bool createForGame(const std::string& gameName, const std::string& gameId,
                   const std::string& isoPath, std::string& outPath, std::string& error);

// Self-test (main.cpp --shortcut-selftest): builds a shortcut into a throwaway
// folder and checks its name, launch command and icon. 0 ok, 1 failed.
int selfTest();

}  // namespace shortcut
