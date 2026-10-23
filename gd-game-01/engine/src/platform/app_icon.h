// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include "data/bitmap.h"

struct SDL_Window;

// The Guitar Dream Engine badge, built into the program from
// engine/res/gd01_icon.png (256 x 256, made by tools/make_icons.py).

// Sets it as the window's icon (title bar, taskbar, Alt-Tab; the .exe, .app and
// Android launcher icons come from the package instead).
void setAppWindowIcon(SDL_Window* window);

// The badge as RGBA pixels (for the first screen); false if it cannot be read.
bool appIconImage(Image& out);
