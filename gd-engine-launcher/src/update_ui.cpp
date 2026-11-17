// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The launcher's update screens (GDUL-031, #114): the start-up check, the
// "Update available" prompt, the Updates menu (the version badge or U), the
// download's progress and the restart. The work itself is updater.cpp's.
// Nothing is downloaded or changed before the player picks "Update now".
#include <cstdlib>

#include "launcher.h"
#include "platform.h"

using update::Kind;
using State = update::Updater::State;

namespace {
const char* const kProgressTitle = "Updating";
std::string mb(long long bytes) { return std::to_string((bytes + 512 * 1024) / (1024 * 1024)) + " MB"; }
}  // namespace

void Launcher::updateInit() {
    updater_.init(catalog_.launcherVersion());
    updatedFrom_ = updater_.finishPending();
    if (!updatedFrom_.empty()) log_.add("updated from gdul " + updatedFrom_ + " to " + updater_.version() + ": started fine");
    // The start-up check: not under RecompLauncher (it updates the launcher),
    // not when the setting is off, not for command-line or scripted runs.
    updateCheckPending_ = updater_.enabled() && updater_.checkAtStart() && cli_.play.empty() && cli_.explore.empty() &&
                          !SDL_getenv("GDUL_SCRIPT") && !SDL_getenv("GDUL_NO_UPDATE_CHECK");
}

void Launcher::updateFrame() {
    if (framesDrawn_ <= 2) return;
    if (!updatedFrom_.empty()) {  // the new launcher runs: tell the old one, which then quits
        updater_.confirmStarted();
        toast("Updated from v" + updatedFrom_ + " to v" + updater_.version() + ". Rock on!");
        updatedFrom_.clear();
    }
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
        case State::Failed:
            log_.add(std::string(was == State::Checking ? "update check failed: " : "update failed: ") + updater_.error());
            if (was == State::Checking) {
                if (updater_.manual()) toast("Could not check for updates: " + updater_.error());
            } else if (updater_.error() == "cancelled") {
                if (dialog_.open && dialog_.title == kProgressTitle) dialog_.open = false;
                toast("Update cancelled. Nothing was changed.");
            } else {
                sounds_.play(UiSound::Error);
                showDialog("We can't update this time",
                           updater_.error() + "\n\nYour launcher is unchanged: v" + updater_.version() + ".",
                           {{"OK", Fret::Green, nullptr}});
            }
            break;
        case State::Ready:
            log_.add("update: gdul " + r.version + " downloaded and checked (SHA-256), installing");
            showDialog("Installing v" + r.version, "Replacing the launcher's program files. Your discs, saves and settings are not touched.", {});
            updateApplyPending_ = true;  // after this card is on screen
            return;
        default:
            break;
        }
    }
    if (s == State::Downloading) updateProgressDialog();
    if (s == State::Ready && updateApplyPending_) {
        updateApplyPending_ = false;
        if (updater_.apply()) {
            log_.add("update: files swapped (the old ones kept in .gdul-update/), starting gdul " + updater_.release().version);
            updateSeen_ = State::Applying;
        }
        // a failed swap: State::Failed, its dialog next frame
    }
    if (s == State::Applying) {
        const int started = updater_.pollStarted();
        if (started > 0) {
            log_.add("update: gdul " + updater_.release().version + " started, this one quits");
            running_ = false;
            return;
        }
        if (started == 0 && !dialog_.open)
            showDialog("Starting v" + updater_.release().version, "The new launcher is starting\xE2\x80\xA6", {});
    }
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
    if (updater_.canSelfUpdate()) {
        body += "\n\nUpdate now downloads " + updater_.assetName() + (a ? " (" + mb(a->size) + ")" : "") +
                ", checks it and restarts the launcher. Your discs, saves and settings stay as they are.";
        buttons.push_back({"Update now", Fret::Green, [this] {
                               log_.add("update: the player chose Update now");
                               updater_.startDownload();
                           }});
    } else if (updater_.kind() == Kind::Android && a) {
        body += "\n\nUpdate now downloads the new APK in your browser: open it to install (Android may ask you to allow "
                "installs from the browser). Your discs and saves stay.";
        const std::string url = a->url;
        buttons.push_back({"Update now", Fret::Green, [this, url] {
                               log_.add("update: the player chose Update now (the APK in the browser)");
                               if (!SDL_OpenURL(url.c_str())) toast("Cannot open the browser.");
                           }});
    } else {
        body += updater_.kind() == Kind::AppImage
                    ? "\n\nThis AppImage updates with AppImageUpdate, AppManager or Gear Lever. Or get it from the release page."
                    : "\n\nThis launcher can't update itself here (" + updater_.whyNoSelfUpdate() + "). Get it from the release page.";
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
    dialog_.focus = int(dialog_.buttons.size()) - 1;  // Later: never update by a stray Start
}

void Launcher::updateProgressDialog() {
    long long done = 0, total = 0;
    updater_.progress(done, total);
    std::string body = "Downloading " + updater_.assetName() + ": ";
    body += done > 0 && total > 0 ? mb(done) + " of " + mb(total) : done > 0 ? mb(done) : "starting";
    body += "\n\nYour discs, saves and settings are not touched.";
    if (dialog_.open && dialog_.title == kProgressTitle) {
        dialog_.body = body;
        return;
    }
    if (dialog_.open) return;
    showDialog(kProgressTitle, body, {{"Cancel", Fret::Red, [this] { updater_.cancel(); }}});
}

void Launcher::updateMenu() {
    std::string body = "You have v" + updater_.version() + " (" + platform::kOs + "-" + platform::kArch + ").";
    if (updater_.recomp()) {
        body += "\n\nRecompLauncher keeps this launcher up to date.";
        showDialog("Updates", body, {{"Close", Fret::Red, nullptr}});
        return;
    }
    body += std::string("\n\nCheck for updates when the launcher starts: ") + (updater_.checkAtStart() ? "on." : "off.");
    body += "\nThe check asks GitHub for the latest release; nothing is downloaded before you say so.";
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
