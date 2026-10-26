// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

class Canvas;

// Development aid (gd01's GD01_SCREENSHOT): GD02_SCREENSHOT=<file.bmp> saves
// one frame and quits; with GD02_SCREENSHOT_AT=<song ms>, the first frame of
// a song at or after that time; with several times (<ms>,<ms>,...), one
// <file>_<ms>.bmp per time, quitting after the last. Works headless with SDL_VIDEO_DRIVER=offscreen
// SDL_AUDIO_DRIVER=dummy. Call after drawing a frame, before presenting it;
// returns true when the program should quit.
bool captureDebugScreenshot(Canvas& canvas, bool inSong, double songMs);
