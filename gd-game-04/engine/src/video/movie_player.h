// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdio>
#include <memory>
#include <string>

class Canvas;
class Disc;
class Input;
class Mixer;

// GH3's videos (DATA/MOVIES/BIK/<name>.bik.xen): Bink 1 ("BIKi"), 1280x720
// (a few 1280x780) [C data], decoded with FFmpeg's Bink decoders (a minimal
// LGPL build shipped as shared libraries, see build_common.sh). Built without
// FFmpeg (CMake GD04_VIDEO OFF) every video is skipped with a log line.
//
// Playback: copied from gd-game-02's player (a blocking modal loop,
// skippable by Start or A / green, the sound as the clock); how GH3 itself
// plays and skips its movies (the boot movies, the intro) is to be read from
// its scripts (PlayMovie...) in M3 [I].
class MoviePlayer {
public:
    enum class Result {
        Finished,     // played to the end
        Skipped,      // Start / A pressed
        Quit,         // the window was closed (or a development screenshot was taken)
        Unavailable,  // no video support in this build, or GD04_SKIP_MOVIES=1
        Failed,       // missing or undecodable file
    };

    MoviePlayer();
    ~MoviePlayer();
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;

    // Whether this build plays videos (FFmpeg present, with the Bink
    // decoders). Logs why not, once.
    static bool available();

    // Opens a disc file, e.g. "DATA/MOVIES/BIK/atvi.bik.xen"; false with `error` set on failure.
    bool open(const Disc& disc, const std::string& discPath, std::string& error);
    // Plays the opened video, modally (its own event loop), until it ends,
    // is skipped or the window closes. `input` gets every event (it keeps
    // opening controllers as they connect).
    Result play(Canvas& canvas, Mixer& mixer, Input& input);
    // Development aid: decodes up to `frames` pictures as fast as possible,
    // writing each one's Y, U and V planes (yuv420p, no padding) to `out`;
    // returns the count decoded, -1 on error.
    int dumpFrames(int frames, std::FILE* out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Plays DATA/MOVIES/BIK/<name>.bik.xen modally and returns how it
// ended; with GD04_SKIP_MOVIES=1 (or no video support) returns Unavailable
// at once.
MoviePlayer::Result playMovie(const Disc& disc, const std::string& name, Canvas& canvas, Mixer& mixer, Input& input);

// `gd04 --playmovie <name> <disc>`: plays one video in a window and quits
// (GD04_SCREENSHOT works in it; GD04_SCREENSHOT_AT is in video ms).
// With GD04_MOVIE_DUMP=<file.yuv> it only decodes, headless, writing
// GD04_MOVIE_FRAMES (default 120) pictures. Returns the exit code.
int runPlayMovie(const std::string& dataPath, const std::string& name);
