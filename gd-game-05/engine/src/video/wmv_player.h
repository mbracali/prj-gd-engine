// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

class Canvas;
class GameData;
class Input;
class Mixer;

// Rock Band's videos (videos/*.bik on the disc's file system, not in the
// archive; config/gen/videos.dtb names them): Bink video with Bink audio
// (RDFT or DCT) [C data, --videotest], decoded with FFmpeg (a minimal LGPL
// build shipped as shared libraries, see build_common.sh). The class keeps
// gd-game-02's name (WmvPlayer) and its GH2 playback notes below until RB's
// movie player is read (M3). Built without FFmpeg (CMake GD05_VIDEO OFF) every video
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
        Unavailable,  // no video support in this build, or GD05_SKIP_MOVIES=1
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
    // The opened video's frame count from its header (Bink: the file's own
    // count), and whether it has a decodable sound track; -1 / false if unknown.
    long long frameCount() const;
    bool hasSound() const;

private:
    friend class MenuMovie;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// RB's MoviePanel with (loop TRUE) (ui/main.dta movie_panel: (videos
// background), rb_video*.bik, 1280x720, no sound): a video playing behind
// the menus' panels, looping, drawn in the panel's place in the screen's
// draw order. Decoded on its own thread; the picture follows a clock that
// runs only while the movie is drawn (so it does not race ahead during a
// song). Without video support it draws nothing.
class MenuMovie {
public:
    MenuMovie();
    ~MenuMovie();
    MenuMovie(const MenuMovie&) = delete;
    MenuMovie& operator=(const MenuMovie&) = delete;
    // Opens videos/<name> and starts decoding (a looping movie); false with `error`.
    bool open(const GameData& data, const std::string& name, std::string& error);
    void close();
    const std::string& name() const { return name_; }
    // Draws the current picture over the whole frame (its aspect kept);
    // false when there is none yet.
    bool draw(Canvas& canvas);
    int framesShown() const { return shown_; }

private:
    struct Gl;
    std::unique_ptr<WmvPlayer> player_;
    std::unique_ptr<Gl> gl_;
    std::string name_;
    double clockMs_ = 0;
    uint64_t lastNs_ = 0;
    int shown_ = 0;
};

// MetaPanel's play_movie: plays videos/<name> modally and returns how it
// ended; with GD05_SKIP_MOVIES=1 (or no video support) returns Unavailable
// at once.
WmvPlayer::Result playMovie(const GameData& data, const std::string& name, Canvas& canvas, Mixer& mixer, Input& input);

// `gd05 --playmovie <name> <disc>`: plays one video in a window and quits
// (GD05_SCREENSHOT works in it; GD05_SCREENSHOT_AT is in video ms).
// With GD05_MOVIE_DUMP=<file.yuv> it only decodes, headless, writing
// GD05_MOVIE_FRAMES (default 120) pictures. Returns the exit code.
int runPlayMovie(const std::string& dataPath, const std::string& name);
