// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd24's program: the window, the loop and our own screens until the
// original's front end is decoded (GD24-002 / M4): the status screen (the
// disc's title from PARAM.SFO, the song list from the disc), ENHANCED (the
// settings basics) and GAMEPADS.
#pragma once

#include <SDL3/SDL.h>

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio_out.h"
#include "data/song_list.h"
#include "input/input.h"
#include "platform/settings.h"
#include "render/canvas.h"
#include "ui/gamepads_screen.h"
#include "ui/song_screen.h"

namespace gd24 {
class Umd;
}

struct Options {
    std::string disc;
    std::string song;     // --song <FOLDER>: straight into that song
    int difficulty = -1;  // --difficulty 0..3 (Easy..Expert)
    int track = -1;       // --track 0..3 (drums, bass, guitar, vocals; default the song's initial_track)
};

class App {
public:
    App();
    ~App();
    int run(const Options& opt);

    // The menu actions of our own screens (the input layer's PSP buttons,
    // guitar frets and keys arrive as these).
    enum class Menu { Up, Down, Left, Right, Confirm, Back, Start, Select };

private:
    enum class Screen { Status, Enhanced, Gamepads, Song };

    bool openWindow(std::string& error);
    void applyScreenMode();
    void handleEvent(const SDL_Event& e);
    void menu(Menu m);
    void openGamepads();
    void click(float x, float y);
    void draw();
    void drawStatus();
    void drawEnhanced();
    void drawHelp(const std::vector<std::pair<std::string, std::string>>& items);
    void reloadSongs();
    void playPreview(int song);
    void startSong(int song);
    void pollPreview();
    void applyVolume();
    std::string songCounts() const;

    Options opt_;
    std::unique_ptr<gd24::Umd> umd_;
    std::string title_, discId_, discVersion_;
    std::vector<gd24::SongEntry> songs_;
    Settings settings_;
    Input input_;
    GamepadsScreen gamepads_{input_};
    SongScreen song_;
    int difficulty_ = 3;  // Expert
    std::vector<InputEvent> events_;
    SDL_Window* window_ = nullptr;
    Canvas canvas_;
    Screen screen_ = Screen::Status;
    int sel_ = 0, scroll_ = 0;  // the song list
    int esel_ = 0;              // ENHANCED's row
    std::string status_;        // a line under the list
    AudioOut audio_;
    std::future<std::shared_ptr<PcmClip>> decoding_;  // a preview being decoded
    int decodingSong_ = -1, playingSong_ = -1;
    bool quit_ = false;
    uint64_t startNs_ = 0;
};
