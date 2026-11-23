// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The Additive Music management tool's state (additive.cpp; GDUL-036). In a
// header so the TU that defines Launcher's destructor (explorer.cpp) sees the
// complete type behind the unique_ptr, as it does for Explore and Songs.
#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "launcher.h"

struct Launcher::Additive {
    // One playable game, listed on the left with its toggles.
    struct Row {
        std::string id, title, isoName;
        int songCount = -1;   // -1: unknown / not counted yet
        bool additive = false;
        bool custom = false;
    };
    // One node of the custom-songs folder tree on the right.
    struct Node {
        std::string path, name;
        bool isSong = false;    // a leaf song folder (else an organising folder)
        bool expanded = true;   // folders start expanded? no — collapsed on first build
        bool evaluated = false; // the quick validation has run on this song
        bool valid = false;     // it passed validation
        std::string kind;       // "clone-hero", "yarg", "rock-band", "gh-dlc", "folder", "invalid", "unknown"
        int depth = 0;
        int parent = -1;        // index of the parent node (-1: top level)
        bool visible = true;    // shown now (every ancestor expanded)
    };

    std::vector<Row> rows;
    int gameSel = 0;            // the selected game row (its custom-songs folder is shown)
    std::string customRoot;     // the selected game's custom-songs folder (absolute)

    std::vector<Node> tree;     // the flattened, visible folder tree
    int treeSel = 0;
    float treeScroll = 0, treeScrollTarget = 0;
    float listScroll = 0, listScrollTarget = 0;

    std::string search;         // (reserved) not used in the launcher tool
    std::string status;         // a one-line status message under the tree
    double statusAt = -100;

    bool dirty = false;         // unsaved toggle changes
    bool loading = false;       // a loading overlay (open / save / evaluate)
    std::string loadingWhat;

    int focus = 0;              // 0 game list, 1 folder tree, 2 bottom buttons
    int btnSel = 0;             // bottom-button cursor
    double lastFrame = 0;

    std::vector<Hit> hits;      // clickable regions (see additive.cpp for the row codes)

    int ctxNode = -1;           // the tree node whose right-click menu is open (-1: none)
    gfx::Rect ctxAt{};          // where to draw that menu (design units)

    // A small modal for creating / renaming a folder (text entry).
    int promptMode = 0;         // 0 none, 1 new folder, 2 rename node
    std::string promptText;
    int promptNode = -1;
};
