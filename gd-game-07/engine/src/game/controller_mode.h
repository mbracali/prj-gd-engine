// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// How a controller plays. In GH3 a gamepad strums as a guitar does (the
// D-pad / left stick), its frets on LT LB RB RT A [C: the original in Xenia];
// Joypad marks the keyboard's "frets also strum" option (the port's).
enum class ControllerMode {
    Guitar,
    Joypad,
};
