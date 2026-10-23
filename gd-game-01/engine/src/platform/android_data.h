// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct SDL_Window;

// Android: the user's Guitar Hero disc image cannot ship in the APK. The game
// reads it from the app's external files folder
// (Android/data/io.github.mbracali.gd01/files/, where it can be copied over USB or
// with `adb push`), or the user picks it once with the system file picker and
// it is copied there (content:// URIs cannot be read as plain files).
class AndroidIsoImport {
public:
    ~AndroidIsoImport();
    // The folder the game looks in, and the first .iso found there ("" if none).
    static std::string folder();
    static std::string findIso();
    // Opens the system file picker; the chosen file is copied in the background.
    void pick(SDL_Window* window);
    bool busy() const { return busy_; }
    // "" or a progress / error line for the data screen.
    std::string status();
    // The imported image's path once the copy is done (then "" again).
    std::string takeReady();

private:
    static void picked(void* self, const char* const* files, int filter);
    void copy(std::string uri);

    std::atomic<bool> busy_{false};
    std::atomic<long long> done_{0}, total_{0};
    std::mutex mutex_;
    std::string status_, ready_;
    std::thread worker_;
};
