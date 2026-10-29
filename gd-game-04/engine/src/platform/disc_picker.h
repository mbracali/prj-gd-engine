// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct SDL_Window;

// Where the user's Guitar Hero III: Legends of Rock (Xbox 360) disc image is (gd01's
// loadRememberedPath / rememberPath / AndroidIsoImport, for gd04).

// The disc image the game starts with: the one picked last time ("data=" in
// config.txt in the save folder), else an .iso lying next to the program (the
// Steam Deck instructions put it there; Game Mode may show no file picker),
// else (Android) an .iso in the app's external files folder. "" if none.
std::string loadRememberedPath();
// Remembers `path` in config.txt (other lines of the file are kept).
void rememberPath(const std::string& path);

// The first screen's "choose your disc image": the system file dialog (it
// answers on another thread; poll takePicked() every frame). On Android the
// chosen file is a content:// URI, read in place (data/random_file.cpp;
// GD04-028): no copy.
// Development aid: GD04_DIALOG=<path> answers the dialog at once (headless tests).
class DiscPicker {
public:
    ~DiscPicker();
    void open(SDL_Window* window);
    bool busy() const { return busy_; }
    // "" or a progress / error line for the first screen.
    std::string status();
    // The chosen disc image once there is one (then "" again).
    std::string takePicked();

private:
    static void picked(void* self, const char* const* files, int filter);
    void finish(const std::string& path, const std::string& status);
#ifdef __ANDROID__
    std::thread worker_;
    std::atomic<long long> done_{0}, total_{0};
#endif

    std::atomic<bool> busy_{false};
    std::mutex mutex_;
    std::string status_, picked_;
};
