// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The universal GAMEPADS screen (docs/guidelines/universal-menus.md 1.2.1,
// the layout of gd01's / gd02's gamepad_panel) on gd24's canvas: four
// player boxes (Rock Band Unplugged has one player: 2..4 unavailable) with
// Instrument, Bind to (every device, Confirm detects the next one pressed)
// and Customize inputs; INPUTS MAPPING on the right lists the device's
// controls in their sections, two slots each, lit while pressed, with Reset,
// Save and Discard (everything else is locked until Save or Discard); Back,
// Export and Import (controllers-export.json in the save folder) below.
#pragma once

#include <string>
#include <vector>

#include "input/input.h"

class Canvas;

class GamepadsScreen {
public:
    explicit GamepadsScreen(Input& input) : input_(input) {}
    void enter();
    void exit();
    // A menu action; false when the screen wants to close.
    enum class Key { Up, Down, Left, Right, Confirm, Back };
    bool key(Key k);
    bool click(float x, float y);
    void draw(Canvas& c);

private:
    struct Hit {
        float x, y, w, h;
        int focus, slot;  // slot -1: not a binding
    };
    // Left focus: 0 Instrument, 1 Bind to, 2 Customize, 3 Back, 4 Export, 5 Import.
    enum Left { kInstrument, kBind, kCustomize, kBack, kExport, kImport, kLeftCount };
    std::vector<Input::Control> mappingRows() const;  // the edited device's controls
    int rightCount() const { return int(mappingRows().size()) + 3; }  // + Reset, Save, Discard
    bool activateLeft(int f, int dir);
    bool activateRight(int f, int dir);
    void finishEdit(bool save);

    Input& input_;
    int focus_ = 0, rfocus_ = 0, slot_ = 0, scroll_ = 0;
    std::string editId_;
    std::string status_;
    std::vector<Hit> hits_;
};
