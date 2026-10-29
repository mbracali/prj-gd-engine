// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdio>
#include <memory>
#include <string>

class Canvas;
class GameData;
class Input;
class Mixer;

// GH2's videos (videos/intro.wmv, hmx.wmv, wg.wmv on the disc's file system,
// not in the archive): ASF files with WMV3 (VC-1 Main profile) 720x480 video
// at 59.94 or 29.97 fps and WMA v2 44.1 kHz stereo sound [C data], decoded
// with FFmpeg (a minimal LGPL build shipped as shared libraries, see
// build_common.sh). Built without FFmpeg (CMake GD03_VIDEO OFF) every video
// is skipped with a log line.
//
// Playback is GH2's MetaPanel play_movie (0x822A1850 -> 0x822A1630, player
// 0x82311310) [C]: a blocking modal loop, skippable at any time on any
// device by Start or A / green (kPad_Start, kPad_X), with two extra frames
// after the end. The sound plays through the mixer and is the clock the
// pictures follow. The 720x480 picture is shown 4:3 inside the 16:9 frame
// (pillarboxed, as the 360 shows 480p content on a widescreen output [I]);
// its YUV 4:2:0 planes are three textures converted in a shader
// (gfx::Renderer::videoProgramDesc).
class WmvPlayer {
public:
    enum class Result {
        Finished,     // played to the end
        Skipped,      // Start / A pressed
        Quit,         // the window was closed (or a development screenshot was taken)
        Unavailable,  // no video support in this build, or GD03_SKIP_MOVIES=1
        Failed,       // missing or undecodable file
    };

    WmvPlayer();
    ~WmvPlayer();
    WmvPlayer(const WmvPlayer&) = delete;
    WmvPlayer& operator=(const WmvPlayer&) = delete;

    // Whether this build plays videos (FFmpeg present, with the WMV3 and
    // WMA v2 decoders). Logs why not, once.
    static bool available();

    // Opens a disc file, e.g. "videos/intro.wmv"; false with `error` set on failure.
    bool open(const GameData& data, const std::string& discPath, std::string& error);
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

// MetaPanel's play_movie: plays videos/<name> modally and returns how it
// ended; with GD03_SKIP_MOVIES=1 (or no video support) returns Unavailable
// at once.
WmvPlayer::Result playMovie(const GameData& data, const std::string& name, Canvas& canvas, Mixer& mixer, Input& input);

// `gd03 --playmovie <name> <disc>`: plays one video in a window and quits
// (GD03_SCREENSHOT works in it; GD03_SCREENSHOT_AT is in video ms).
// With GD03_MOVIE_DUMP=<file.yuv> it only decodes, headless, writing
// GD03_MOVIE_FRAMES (default 120) pictures. Returns the exit code.
int runPlayMovie(const std::string& dataPath, const std::string& name);
