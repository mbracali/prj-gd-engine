// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "action_log.h"

#include <SDL3/SDL.h>

#include "platform.h"

void ActionLog::open(const std::string& path) {
    path_ = path;
    user_ = platform::userName();
    for (char& c : user_)
        if (c == ' ' || c == '\t' || c == '\n') c = '_';  // one field
}

void ActionLog::add(const std::string& action) {
    std::string line = platform::isoNow() + " " + user_ + " " + action;
    for (char& c : line)
        if (c == '\n' || c == '\r') c = ' ';
    SDL_Log("gdul: %s", action.c_str());
    if (path_.empty()) return;
    line += "\n";
    if (SDL_IOStream* io = SDL_IOFromFile(path_.c_str(), "ab")) {
        SDL_WriteIO(io, line.data(), line.size());
        SDL_CloseIO(io);
    }
}
