// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// How a controller plays: a guitar strums, a joypad's fret press is a strum.
// [I] for GH3: the guitar / pad split is the port's input layer (copied from
// gd-game-02); GH3's own rule for pads is to be confirmed in the XEX
// (OPEN_QUESTIONS.md).
enum class ControllerMode {
    Guitar,
    Joypad,
};
