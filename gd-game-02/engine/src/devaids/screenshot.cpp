// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "devaids/screenshot.h"

#include <SDL3/SDL.h>

#include <cstdlib>

#include "render/canvas.h"

bool captureDebugScreenshot(Canvas& canvas, bool inSong, double songMs) {
    const char* file = SDL_getenv("GD02_SCREENSHOT");
    if (!file) return false;
    if (const char* at = SDL_getenv("GD02_SCREENSHOT_AT"); at && !(inSong && songMs >= std::atof(at))) return false;
    if (SDL_Surface* shot = canvas.readPixels()) {
        if (!SDL_SaveBMP(shot, file)) SDL_Log("GD02_SCREENSHOT: cannot save %s: %s", file, SDL_GetError());
        else SDL_Log("GD02_SCREENSHOT: saved %s at song time %.0f ms", file, songMs);
        SDL_DestroySurface(shot);
    } else {
        SDL_Log("GD02_SCREENSHOT: cannot read the frame: %s", SDL_GetError());
    }
    return true;
}
