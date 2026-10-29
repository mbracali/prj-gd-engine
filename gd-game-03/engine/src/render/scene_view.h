// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>

// gd03 --view <milo> <disc>: a development viewer that draws one .milo_ps2
// scene (e.g. world/stone/gen/stone.milo_ps2) through GH2's materials and
// standard shader: textures, meshes (skinned ones at their bind pose), the
// first Environ's ambient, lights and fog, from the scene's first Cam (or a
// camera framing every mesh). Left/Right cycle the scene's cameras, Esc
// quits. GD03_SCREENSHOT=<file.bmp> saves the first frame and quits.
// A PanelDir (ui/gen/*.milo_ps2) draws as a UI panel with its components
// and texts through render/ui_view.h, from the panel's camera (or GH2's ui
// camera); GD03_VIEW_LANG picks the locale, GD03_VIEW_FOCUS the focused
// button, GD03_VIEW_CLEAN=1 hides the info line, GD03_VIEW_UI=0 turns it off.
int runSceneView(const std::string& dataPath, const std::string& miloPath);
