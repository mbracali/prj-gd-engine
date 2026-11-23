// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Update from inside the launcher (#114, scoped down from GDUL-031). It asks
// GitHub's public REST API for the latest gdul release (no token, User-Agent
// "gdul/<version>", nothing personal), and, only after the player says so,
// downloads this build's package to the user's Downloads folder and checks its
// SHA-256 against the release's SHA256SUMS.txt. It never touches the current
// install: downloading is the end of it and the player installs the new build
// themselves. A failed check is silent (one gdul.log line) and never blocks.
//
// Per download: Windows .zip, the portable Linux and Steam Deck .tar.gz, the
// macOS .zip and the AppImage are saved to Downloads. Android opens the APK in
// the browser (the system download manager and installer take it from there).
// Under RecompLauncher (RECOMPLAUNCHER_MODE) there is no check at all:
// RecompLauncher updates the launcher.
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

// The asset this build would download from `r` (the first of assetNames that
// the release actually has), or "" when the release has none for this build.
std::string pickAsset(Kind k, const Release& r);
// True when the launcher should warn the player: the remote version is newer,
// there is a download for this build, and it is not a version the player
// skipped (a manual check warns about a skipped version too).
bool shouldWarn(const std::string& remoteVersion, const std::string& localVersion, const std::string& asset, bool manual,
                bool skipped);

std::string sha256Hex(const void* data, size_t size);
bool sha256File(const std::string& path, std::string& hex);
// The hex sum SHA256SUMS.txt gives `name` ("<hex>  <name>" lines), or "".
std::string expectedSum(const std::string& sums, const std::string& name);

int selfTest();  // 0: all checks passed (prints each)

// ---- The running launcher's updater ----

class Updater {
public:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Downloaded, Failed };

    void init(const std::string& launcherVersion);
    Kind kind() const { return kind_; }
    // RecompLauncher installs never check; a development build still checks.
    bool enabled() const { return !recomp_; }
    bool recomp() const { return recomp_; }
    const std::string& version() const { return version_; }
    // Where a downloaded package is saved (the user's Downloads folder).
    const std::string& downloadsDir() const { return downloadsDir_; }

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
    // After Downloaded: the saved file's full path ("" before then).
    std::string downloadedPath() const;

    std::string releasesPage() const;

private:
    static int threadMain(void* self);
    void runCheck();
    void runDownload();
    void fail(const std::string& why);
    bool httpGet(const std::string& url, std::string& body, std::string& error);
    void saveSettings();

    std::string version_;
    Kind kind_ = Kind::None;
    bool recomp_ = false, checkAtStart_ = true, manual_ = false;
    std::string skip_, downloadsDir_;
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> cancel_{false};
    std::atomic<long long> total_{0};
    mutable std::mutex lock_;
    Release release_;
    std::string error_, asset_, downloadPath_;
    SDL_Thread* thread_ = nullptr;
    int job_ = 0;  // 1 check, 2 download
};

}  // namespace update
