// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <vector>

#include "input/input.h"
#include "script/vm.h"
#include "ui/natives_flow.h"
#include "ui/ui_system.h"

class Canvas;
class Disc;
class Globals;
class TextureCache;

// GH3's front end run from its own scripts (L1): the VM boots with
// guitar_startup, the flow manager builds the screens with screen elements
// (ui/ui_system.h), and the natives the scripts call (script/natives_*.cpp,
// ui/natives_*.cpp) answer for the engine. Game time advances in 60 Hz
// steps.
class FrontEnd {
public:
    FrontEnd(const Disc& disc, Globals& globals, TextureCache& textures, gfx::RenderDevice& device);
    ~FrontEnd();
    bool start();
    void handle(const InputEvent& e);
    void frame(Canvas& c, double dtMs);
    qs::Vm& vm() { return *vm_; }
    ui::UiSystem& ui() { return *ui_; }
    // Buttons pressed since the last GetButtonsPressed (natives).
    struct Pad {
        int makes = 0;
        int device = 0;
    } pad;
    FlowState flow;

private:
    const Disc& disc_;
    Globals& globals_;
    std::unique_ptr<qs::Vm> vm_;
    std::unique_ptr<ui::UiSystem> ui_;
    double stepMs_ = 0, accum_ = 0;
};
