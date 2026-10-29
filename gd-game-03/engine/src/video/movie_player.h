// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>

class Canvas;
class GameData;
class Input;
class Mixer;

// This disc's movies (VIDEOS/INTRO.PSS on the disc's file system; the
// scripts' `meta play_movie intro.pss`): Sony .pss, MPEG-2 video at NTSC
// rate and 16-bit stereo PCM (SShd type 1, 48 kHz, 0x200-byte L/R blocks)
// [C data], decoded with gd-game-01's own MPEG-2 / PSS code (video/mpeg2.h,
// video/pss.h). Playback as GH2's play_movie: modal, skippable by Start,
// select, back or green; the sound through the mixer is the clock the
// pictures follow; the 4:3 picture pillarboxed in the 16:9 frame.
class MoviePlayer {
public:
    enum class Result {
        Finished,     // played to the end
        Skipped,      // a button pressed
        Quit,         // the window was closed (or a development screenshot was taken)
        Unavailable,  // GD03_SKIP_MOVIES=1
        Failed,       // missing or undecodable file
    };

    MoviePlayer();
    ~MoviePlayer();
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;

    // Opens a disc file, e.g. "videos/intro.pss"; false with `error` set on failure.
    bool open(const GameData& data, const std::string& discPath, std::string& error);
    // Plays the opened movie modally (its own event loop).
    Result play(Canvas& canvas, Mixer& mixer, Input& input);
    // Development aid: decodes up to `frames` pictures, headless; returns the count, -1 on error.
    int decodeFrames(int frames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// MetaPanel's play_movie: plays videos/<name> modally; with
// GD03_SKIP_MOVIES=1 returns Unavailable at once.
MoviePlayer::Result playMovie(const GameData& data, const std::string& name, Canvas& canvas, Mixer& mixer, Input& input);

// `gd03 --playmovie <name> <disc>`: plays one movie in a window and quits.
// With GD03_MOVIE_DUMP=1 it only decodes GD03_MOVIE_FRAMES (default 120)
// pictures headless and prints the count. Returns the exit code.
int runPlayMovie(const std::string& dataPath, const std::string& name);
