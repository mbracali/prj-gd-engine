// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

struct SDL_Surface;
struct SDL_Window;

// The Guitar Dream Engine badge, built into the program from
// engine/res/gd04_icon.png (256 x 256, made by tools/make_icons.py), as in gd01.

// Sets it as the window's icon (title bar, taskbar, Alt-Tab; the .exe, .app and
// Android launcher icons come from the package instead).
void setAppWindowIcon(SDL_Window* window);

// The badge as an RGBA32 surface (for the first screen), or null if it cannot
// be read. The caller destroys it (SDL_DestroySurface).
SDL_Surface* appIconSurface();

// Linux downloads (a LICENSE next to the program; not development builds):
// the application menu entry io.github.mbracali.gd04.desktop and its icon in
// the user's data folder, pointing at this copy of the program, so desktops
// show the game's icon on its window and taskbar entry (Wayland matches the
// window to its entry by app id). Rewritten when the program moves; set
// GD04_NO_DESKTOP_ENTRY to skip it.
void installDesktopEntry();
