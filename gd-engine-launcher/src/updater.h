// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Update from inside the launcher (GDUL-031, #114). It asks GitHub's public
// REST API for the latest gdul release (no token, User-Agent "gdul/<version>",
// nothing personal), and, only after the player says so, downloads this
// build's package, checks its SHA-256 against the release's SHA256SUMS.txt,
// extracts it next to the install and swaps the program files in one by one.
// Only the files the new package has are touched: original_isos/, the saves,
// settings and logs (in the save folder), and anything the player added stay
// where they are. The replaced files go to .gdul-update/previous-<version>/;
// a journal undoes the swap when the new launcher does not start.
//
// Per download: Windows .zip, the portable Linux and Steam Deck .tar.gz and
// the macOS .zip update themselves this way. The AppImage keeps its zsync
// update (AppImageUpdate, AppManager, Gear Lever): the launcher says so and
// opens the release page. Android opens the APK in the browser (the system
// installer takes it from there). Under RecompLauncher (RECOMPLAUNCHER_MODE)
// there is no check at all: RecompLauncher updates the launcher.
#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace update {

// ---- Pure logic (tested by `gdul --update-selftest`) ----

// "gdul/v1.1.16", "v1.1.16" or "1.1.16": true and the three numbers.
bool parseVersion(const std::string& s, int out[3]);
// <0, 0, >0 as a is older, the same, newer than b; unparsable versions are the oldest.
int compareVersions(const std::string& a, const std::string& b);

enum class Kind { None, Windows, Portable, SteamDeck, MacOS, AppImage, Android };
const char* kindName(Kind k);
// The release assets this build takes, the preferred first (the portable
// Linux build also reads the name used up to 1.1.15, gdul-<v>-linux-x86_64.tar.gz).
std::vector<std::string> assetNames(Kind k, const std::string& version);

struct Asset {
    std::string name, url;
    long long size = 0;
};
struct Release {
    std::string tag, version, htmlUrl, highlights;
    bool draft = false, prerelease = false;
    std::vector<Asset> assets;
    const Asset* find(const std::string& name) const;
};
// GitHub's release JSON. False and `error` when it is not one.
bool parseRelease(const std::string& json, Release& out, std::string& error);
// The release body's "Highlights" list, one "- " line each (at most `maxLines`).
std::string highlights(const std::string& body, int maxLines = 5);

std::string sha256Hex(const void* data, size_t size);
bool sha256File(const std::string& path, std::string& hex);
// The hex sum SHA256SUMS.txt gives `name` ("<hex>  <name>" lines), or "".
std::string expectedSum(const std::string& sums, const std::string& name);

// A package's folder is whole: the program, gd-engine-games/manifest.json and
// every module the manifest lists (non-empty). `exe` is relative to `root`.
bool checkPackage(const std::string& root, const std::string& exe, std::string& error);

// Moves every file of `src` (recursively; never an original_isos folder) to
// the same place in `dst`. A file already there goes first to the same place
// in `backup`. Each step is written to `journal`; on any failure the steps
// done are undone and false comes back with `error`.
bool swapIn(const std::string& src, const std::string& dst, const std::string& backup, const std::string& journal,
            std::string& error);
// Undoes a swap from its journal: the new files go, the old ones come back.
bool rollback(const std::string& journal, std::string& error);

int selfTest();  // 0: all checks passed (prints each)

// ---- The running launcher's updater ----

class Updater {
public:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Applying, Failed };

    void init(const std::string& launcherVersion);
    Kind kind() const { return kind_; }
    // RecompLauncher installs never check; a development build has no kind.
    bool enabled() const { return !recomp_; }
    bool recomp() const { return recomp_; }
    // Can replace its own files (Windows, portable Linux, Steam Deck, macOS,
    // in a folder it can write to); else the prompt opens the release page.
    bool canSelfUpdate() const { return selfUpdate_; }
    std::string whyNoSelfUpdate() const { return whyNot_; }
    const std::string& version() const { return version_; }

    bool checkAtStart() const { return checkAtStart_; }
    void setCheckAtStart(bool on);
    void skip(const std::string& version);
    bool skipped(const std::string& version) const { return skip_ == version; }

    void startCheck(bool manual);
    void startDownload();
    void cancel();
    void shutdown();  // at quit: stops a check or download, waits for its thread
    State state() const { return state_.load(); }
    bool manual() const { return manual_; }
    Release release() const;
    std::string error() const;
    // Download progress: bytes on disk, expected size.
    void progress(long long& done, long long& total) const;
    // The asset picked for this build ("" when none).
    std::string assetName() const;

    // After Ready: swaps the files in and starts the new launcher. False and
    // error() when the swap failed (nothing changed then).
    bool apply();
    // While the new launcher starts: 1 it runs fine (the old one quits), 0
    // waiting, -1 it failed and the old files are back (error()).
    int pollStarted();

    // At start (the new launcher): an update just applied, tell the old one
    // it starts fine; older leftovers are cleaned. Returns the version updated
    // from ("" when none).
    std::string finishPending();
    void confirmStarted();

    std::string releasesPage() const;

private:
    static int threadMain(void* self);
    void runCheck();
    void runDownload();
    void fail(const std::string& why);
    bool run(const std::vector<std::string>& args, std::string& error);  // stops (killed) on cancel()
    bool httpGet(const std::string& url, std::string& body, std::string& error);
    void saveSettings();
    std::string updateDir() const;   // <install>/.gdul-update/
    std::string statePath() const;

    std::string version_;
    Kind kind_ = Kind::None;
    bool recomp_ = false, selfUpdate_ = false, checkAtStart_ = true, manual_ = false;
    std::string whyNot_, skip_, installRoot_, exeRel_;
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> cancel_{false};
    std::atomic<long long> done_{0}, total_{0};
    mutable std::mutex lock_;
    Release release_;
    std::string error_, asset_, archive_, packageRoot_, downloadPath_;
    SDL_Thread* thread_ = nullptr;
    int job_ = 0;  // 1 check, 2 download
    SDL_Process* child_ = nullptr;  // the new launcher, starting
    Uint64 childStartNs_ = 0;
    std::string journal_;
};

}  // namespace update
