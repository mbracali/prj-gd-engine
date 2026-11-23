// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The launcher's update screens (#114): the start-up check, the "Update
// available" prompt, the Updates menu (the version badge or U), the download's
// progress and the "Downloaded" notice. The work itself is updater.cpp's.
// Nothing is downloaded before the player picks "Download", and the current
// install is never changed: the player installs the new build themselves.
#include <cstdlib>

#include "launcher.h"
#include "platform.h"

using update::Kind;
using State = update::Updater::State;

namespace {
const char* const kProgressTitle = "Downloading update";
std::string mb(long long bytes) { return std::to_string((bytes + 512 * 1024) / (1024 * 1024)) + " MB"; }

// A file:// URL for the file manager to reveal a folder.
std::string folderUrl(const std::string& dir) {
    std::string p = dir;
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
#ifdef _WIN32
    for (char& c : p)
        if (c == '\\') c = '/';
    return "file:///" + p;
#else
    return "file://" + p;
#endif
}
}  // namespace

void Launcher::updateInit() {
    updater_.init(catalog_.launcherVersion());
    // The start-up check: not under RecompLauncher (it updates the launcher),
    // not when the setting is off, not for command-line or scripted runs.
    updateCheckPending_ = updater_.enabled() && updater_.checkAtStart() && cli_.play.empty() && cli_.explore.empty() &&
                          !SDL_getenv("GDUL_SCRIPT") && !SDL_getenv("GDUL_NO_UPDATE_CHECK");
}

void Launcher::updateFrame() {
    if (framesDrawn_ <= 2) return;
    if (updateCheckPending_) {
        updateCheckPending_ = false;
        updater_.startCheck(false);
    }
    const State s = updater_.state();
    if (s != updateSeen_) {
        const State was = updateSeen_;
        updateSeen_ = s;
        const update::Release r = updater_.release();
        switch (s) {
        case State::UpToDate:
            log_.add("update check: latest gdul " + r.version + ", this is " + updater_.version());
            if (updater_.manual()) {
                if (update::compareVersions(r.version, updater_.version()) > 0)
                    toast("v" + r.version + " is out, but there is no download for this system yet.");
                else toast("You have the latest version (v" + updater_.version() + ").");
            }
            break;
        case State::Available:
            log_.add("update check: gdul " + r.version + " is out (this is " + updater_.version() + ")");
            updatePromptPending_ = true;
            break;
        case State::Downloaded:
            log_.add("update: gdul " + r.version + " downloaded to " + updater_.downloadsDir());
            updateDownloadedDialog();
            break;
        case State::Failed:
            log_.add(std::string(was == State::Checking ? "update check failed: " : "update download failed: ") + updater_.error());
            if (was == State::Checking) {
                if (updater_.manual()) toast("Could not check for updates: " + updater_.error());
            } else if (updater_.error() == "cancelled") {
                if (dialog_.open && dialog_.title == kProgressTitle) dialog_.open = false;
                toast("Download cancelled. Nothing was changed.");
            } else {
                sounds_.play(UiSound::Error);
                showDialog("Download didn't finish",
                           updater_.error() + "\n\nYour launcher is unchanged: v" + updater_.version() +
                               ". You can try again, or get it from the release page.",
                           {{"OK", Fret::Green, nullptr}});
            }
            break;
        default:
            break;
        }
    }
    if (s == State::Downloading) updateProgressDialog();
    if (updatePromptPending_ && !dialog_.open && !explore_ && !cliPending_) {
        updatePromptPending_ = false;
        updatePrompt();
    }
}

void Launcher::updatePrompt() {
    const update::Release r = updater_.release();
    std::string body = "Version " + r.version + " is out (you have " + updater_.version() + ").";
    if (!r.highlights.empty()) body += "\n\n" + r.highlights;
    std::vector<Button> buttons;
    const update::Asset* a = r.find(updater_.assetName());
    const std::string page = updater_.releasesPage();
    if (updater_.kind() == Kind::Android && a) {
        body += "\n\nDownload opens the new APK in your browser: open it to install (Android may ask you to allow installs "
                "from the browser). Your current app, discs and saves are not touched.";
        const std::string url = a->url;
        buttons.push_back({"Download", Fret::Green, [this, url] {
                               log_.add("update: the player chose Download (the APK in the browser)");
                               if (!SDL_OpenURL(url.c_str())) toast("Cannot open the browser.");
                           }});
    } else if (a) {
        body += "\n\nDownload saves " + updater_.assetName() + " (" + mb(a->size) + ") to your Downloads folder; install it "
                "yourself when you like. Your current launcher, discs and saves are not touched.";
        buttons.push_back({"Download", Fret::Green, [this] {
                               log_.add("update: the player chose Download");
                               toast("Downloading to your Downloads folder\xE2\x80\xA6");
                               updater_.startDownload();
                           }});
    } else {
        body += "\n\nThere is no download for this system yet. Get it from the release page.";
        buttons.push_back({"Open release page", Fret::Green, [this, page] {
                               log_.add("update: release page opened");
                               if (!SDL_OpenURL(page.c_str())) toast("Visit " + page);
                           }});
    }
    const std::string v = r.version;
    buttons.push_back({"Skip this version", Fret::Yellow, [this, v] {
                           updater_.skip(v);
                           log_.add("update: v" + v + " skipped");
                           toast("v" + v + " skipped. Check for updates any time from the version badge.");
                       }});
    buttons.push_back({"Later", Fret::Red, nullptr});
    showDialog("Update available", body, buttons);
    dialog_.focus = int(dialog_.buttons.size()) - 1;  // Later: never download by a stray Start
}

void Launcher::updateProgressDialog() {
    long long done = 0, total = 0;
    updater_.progress(done, total);
    std::string body = "Downloading " + updater_.assetName() + ": ";
    body += done > 0 && total > 0 ? mb(done) + " of " + mb(total) : done > 0 ? mb(done) : "starting";
    body += "\n\nSaving to your Downloads folder. Your current install is not touched.";
    if (dialog_.open && dialog_.title == kProgressTitle) {
        dialog_.body = body;
        return;
    }
    if (dialog_.open) return;
    showDialog(kProgressTitle, body, {{"Cancel", Fret::Red, [this] { updater_.cancel(); }}});
}

void Launcher::updateDownloadedDialog() {
    const update::Release r = updater_.release();
    const std::string dir = updater_.downloadsDir();
    const std::string body = "v" + r.version + " is in your Downloads folder:\n" + updater_.assetName() +
                             "\n\nOpen it to install the new launcher. Your current launcher, discs and saves stay as they are.";
    sounds_.play(UiSound::Select);
    const std::string url = folderUrl(dir);
    showDialog("Downloaded", body,
               {{"Open folder", Fret::Green,
                 [this, url] {
                     log_.add("update: Downloads folder opened");
                     if (!SDL_OpenURL(url.c_str())) toast("Find it in your Downloads folder.");
                 }},
                {"Close", Fret::Red, nullptr}});
}

void Launcher::updateMenu() {
    std::string body = "You have v" + updater_.version() + " (" + platform::kOs + "-" + platform::kArch + ").";
    if (updater_.recomp()) {
        body += "\n\nRecompLauncher keeps this launcher up to date.";
        showDialog("Updates", body, {{"Close", Fret::Red, nullptr}});
        return;
    }
    body += std::string("\n\nCheck for updates when the launcher starts: ") + (updater_.checkAtStart() ? "on." : "off.");
    body += "\nThe check asks GitHub for the latest release; nothing is downloaded before you say so, and your install is never changed.";
    const State s = updater_.state();
    std::vector<Button> buttons;
    if (s == State::Checking) body += "\n\nChecking\xE2\x80\xA6";
    else
        buttons.push_back({"Check for updates", Fret::Green, [this] {
                               log_.add("update: check for updates (by hand)");
                               toast("Checking for updates\xE2\x80\xA6");
                               updater_.startCheck(true);
                           }});
    buttons.push_back({updater_.checkAtStart() ? "Turn off check at start" : "Turn on check at start", Fret::Yellow, [this] {
                           updater_.setCheckAtStart(!updater_.checkAtStart());
                           log_.add(std::string("update: check at start ") + (updater_.checkAtStart() ? "on" : "off"));
                           updateMenu();
                       }});
    buttons.push_back({"Close", Fret::Red, nullptr});
    showDialog("Updates", body, buttons);
}
