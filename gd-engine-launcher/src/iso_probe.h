// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Identifies a disc image by its content, never by its file name: the file
// system (ISO 9660 for PlayStation 2 discs, XDVDFS for Xbox / Xbox 360 discs),
// the volume name, the root files, the PlayStation 2 boot program
// (SYSTEM.CNF's BOOT2) and the Xbox 360 title ID (default.xex's execution
// info). A game's iso_signatures (game-info.yaml) are matched against it.
#pragma once

#include <string>
#include <vector>

struct DiscInfo {
    std::string format;              // "iso9660", "xdvdfs" or "" (not a disc image)
    std::string volumeId;            // ISO 9660 volume identifier, trimmed
    std::string ps2Boot;             // "SLUS_212.24" (SYSTEM.CNF BOOT2), PlayStation 2
    std::string xexTitleId;          // "415607E7" (default.xex), Xbox 360
    std::vector<std::string> files;  // the root folder's names, lower case
    std::string error;               // why it could not be read

    std::string describe() const;  // "Xbox 360 disc, title ID 415607E7"
};

struct IsoSignature {
    std::string game;    // the game id it identifies
    std::string format;  // must equal DiscInfo::format
    std::string ps2Boot, volumeId, xexTitleId;
    std::vector<std::string> files;  // must all be in the root folder
    bool matches(const DiscInfo& d) const;
};

DiscInfo probeDisc(const std::string& path);
