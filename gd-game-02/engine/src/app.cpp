// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "app_ids.h"
#include "audio/mixer.h"
#include "data/game_data.h"
#include "devaids/screenshot.h"
#include "game/autoplay.h"
#include "game/song_db.h"
#include "game/song_setup.h"
#include "input/keyboard.h"
#include "render/canvas.h"
#include "render/highway.h"

namespace {

// Silence before the audio starts, so the first gems scroll in [I]: GH2's
// own intro timing is not traced yet.
constexpr double kLeadInMs = 2000;
// How long the highway stays after the song's [end] before the song closes.
constexpr float kOutroMs = 2000;
constexpr uint64_t kMinFrameNs = 4'000'000;

// songs.dtb's per-channel lists as mixer tracks: vols are dB [I: by name and
// range], pans -1..1, and the channels flagged in `cores` (exactly the
// guitar channels [C data]) are the ones muted while the player misses [I].
std::vector<Mixer::Track> mixerTracks(const SongAudio& a) {
    std::vector<Mixer::Track> tracks(a.pans.size());
    for (size_t c = 0; c < tracks.size(); ++c) {
        tracks[c].pan = a.pans[c];
        tracks[c].volume = c < a.vols.size() ? std::pow(10.0f, a.vols[c] / 20.0f) : 1.0f;
        tracks[c].guitar = c < a.cores.size() && a.cores[c] == 1;
    }
    return tracks;
}

class App {
public:
    explicit App(const Options& o) : options_(o), difficulty_(std::clamp(o.difficulty, 0, kDifficultyCount - 1)) {}
    int run();

private:
    bool startSong(size_t index);
    void endSong();
    void handleEvent(const SDL_Event& e);
    void frameSong();
    void frameList();

    Options options_;
    int difficulty_;
    SDL_Window* window_ = nullptr;
    Canvas canvas_;
    Mixer mixer_;
    std::unique_ptr<GameData> data_;
    Rules rules_;
    std::vector<SongInfo> songs_;
    bool running_ = true;

    // The song list.
    size_t cursor_ = 0;
    std::string message_;

    // The song being played.
    const SongInfo* info_ = nullptr;
    std::unique_ptr<Song> song_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Autoplay> bot_;
    Highway highway_;
    std::vector<float> flashes_ = std::vector<float>(5, 0.0f);
    double lastFrameMs_ = 0;
    float failMs_ = -1e9f;
};

int App::run() {
    SDL_SetAppMetadata(app::kDisplayName, app::kVersion, app::kAppId);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "%s: cannot start SDL: %s\n", app::kProgram, SDL_GetError());
        return 2;
    }
    try {
        data_ = std::make_unique<GameData>(options_.dataPath);
        rules_ = loadRules(*data_);
        songs_ = loadSongs(*data_);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "%s: %s\n", app::kProgram, ex.what());
        SDL_Quit();
        return 2;
    }
    SDL_Log("%s %s: %zu songs from %s", app::kProgram, app::kVersion, songs_.size(), data_->description().c_str());

    window_ = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), SDL_WINDOW_RESIZABLE);
    std::string error;
    if (!window_ || !canvas_.init(window_, error)) {
        std::fprintf(stderr, "%s: cannot open the window: %s\n", app::kProgram, window_ ? error.c_str() : SDL_GetError());
        SDL_Quit();
        return 2;
    }
    if (!mixer_.open(error)) {
        std::fprintf(stderr, "%s: cannot open the audio device: %s\n", app::kProgram, error.c_str());
        canvas_.shutdown();
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return 2;
    }

    int exitCode = 0;
    if (!options_.song.empty()) {
        auto it = std::find_if(songs_.begin(), songs_.end(), [&](const SongInfo& s) { return s.id == options_.song; });
        if (it == songs_.end()) {
            std::fprintf(stderr, "%s: no song '%s' (see the song list without --song)\n", app::kProgram, options_.song.c_str());
            running_ = false, exitCode = 2;
        } else if (!startSong(size_t(it - songs_.begin()))) {
            std::fprintf(stderr, "%s: %s\n", app::kProgram, message_.c_str());
            running_ = false, exitCode = 2;
        }
    }

    while (running_) {
        const uint64_t frameStart = SDL_GetTicksNS();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        if (!running_) break;
        if (player_) frameSong();
        else frameList();
        // Without vsync (e.g. SDL_VIDEO_DRIVER=offscreen) keep to about 250
        // frames a second instead of spinning; gd01's frame limiter comes
        // with its renderer.
        const uint64_t spent = SDL_GetTicksNS() - frameStart;
        if (spent < kMinFrameNs) SDL_DelayNS(kMinFrameNs - spent);
    }

    endSong();
    mixer_.close();
    canvas_.shutdown();
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return exitCode;
}

bool App::startSong(size_t index) {
    const SongInfo& info = songs_[index];
    try {
        song_ = std::make_unique<Song>(loadChart(*data_, info, rules_));
        auto stream = std::make_unique<VorbisStream>(data_->read(info.audio.moggPath));
        player_ = std::make_unique<Player>(*song_, Part::Guitar, static_cast<Difficulty>(difficulty_), rules_,
                                           ControllerMode::Guitar, options_.noFail || options_.autoplay);
        bot_ = options_.autoplay ? std::make_unique<Autoplay>(player_->chart()) : nullptr;
        SDL_Log("%s (%s): %d Hz, %d channels, %zu %s gems, hopo_threshold %d", info.id.c_str(), info.name.c_str(),
                stream->sampleRate(), stream->channels(), player_->chart().gems.size(),
                difficultyName(static_cast<Difficulty>(difficulty_)), hopoThreshold(info.audio, rules_));
        mixer_.play(std::move(stream), mixerTracks(info.audio), kLeadInMs);
    } catch (const std::exception& ex) {
        message_ = info.id + ": " + ex.what();
        SDL_Log("%s", message_.c_str());
        player_.reset(), bot_.reset(), song_.reset();
        return false;
    }
    info_ = &info;
    std::fill(flashes_.begin(), flashes_.end(), 0.0f);
    lastFrameMs_ = -kLeadInMs;
    failMs_ = -1e9f;
    return true;
}

void App::endSong() {
    if (!player_) return;
    SDL_Log("%s: score %d, %d/%zu notes, longest streak %d%s (audio underruns: %lld frames)", info_->id.c_str(),
            player_->score(), player_->notesHit(), player_->chart().gems.size(), player_->longestStreak(),
            player_->failed() ? ", failed" : "", static_cast<long long>(mixer_.underruns()));
    mixer_.stop();
    bot_.reset(), player_.reset(), song_.reset();
    info_ = nullptr;
    if (!options_.song.empty()) running_ = false;  // --song: one song, then quit
}

void App::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) {
        running_ = false;
        return;
    }
    std::optional<KeyAction> a = keyAction(e);
    if (player_) {
        if (!a) return;
        if (a->kind == KeyAction::Back) {
            endSong();
            return;
        }
        if (bot_) return;  // autoplay owns the frets
        const float t = float(mixer_.songTimeMs(a->timestampNs));
        player_->update(t);
        switch (a->kind) {
        case KeyAction::FretDown: player_->fretDown(a->fret, t); break;
        case KeyAction::FretUp: player_->fretUp(a->fret, t); break;
        case KeyAction::Strum: player_->strum(t); break;
        case KeyAction::StarPower: player_->starPowerButton(t); break;
        case KeyAction::WhammyDown: player_->setWhammy(true); break;
        case KeyAction::WhammyUp: player_->setWhammy(false); break;
        case KeyAction::Back: break;
        }
        return;
    }
    // The song list: Up / Down pick, Left / Right difficulty, Enter plays, Esc quits.
    if (e.type != SDL_EVENT_KEY_DOWN || songs_.empty()) return;
    switch (e.key.key) {
    case SDLK_UP: cursor_ = (cursor_ + songs_.size() - 1) % songs_.size(); break;
    case SDLK_DOWN: cursor_ = (cursor_ + 1) % songs_.size(); break;
    case SDLK_LEFT: difficulty_ = std::max(0, difficulty_ - 1); break;
    case SDLK_RIGHT: difficulty_ = std::min(kDifficultyCount - 1, difficulty_ + 1); break;
    case SDLK_RETURN: case SDLK_KP_ENTER: message_.clear(), startSong(cursor_); break;
    case SDLK_ESCAPE: running_ = false; break;
    default: break;
    }
}

void App::frameSong() {
    const float t = float(mixer_.frameTimeMs());
    if (bot_) bot_->feed(*player_, t);
    player_->update(t);
    const float dt = std::clamp(float(t - lastFrameMs_), 0.0f, 100.0f);
    lastFrameMs_ = t;
    for (float& f : flashes_) f = std::max(0.0f, f - dt / 150.0f);
    for (const PlayerEvent& e : player_->takeEvents()) {
        if (e.kind != PlayerEvent::Kind::Hit || e.gem < 0) continue;
        const Gem& g = player_->chart().gems[size_t(e.gem)];
        for (int f = 0; f < 5; ++f)
            if (g.frets & (1u << f)) flashes_[size_t(f)] = 1.0f;
    }
    mixer_.setGuitarAudible(player_->guitarAudible());

    canvas_.begin({12, 10, 16, 255});
    std::string title = info_->name + " - " + info_->artist + "  [" + difficultyName(static_cast<Difficulty>(difficulty_)) +
                        (bot_ ? ", autoplay]" : "]");
    highway_.draw(canvas_, *song_, *player_, t, rules_.trackSpeed[size_t(difficulty_)], title, flashes_);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d:%02d / %d:%02d", int(std::max(0.0f, t) / 60000), int(std::max(0.0f, t) / 1000) % 60,
                  int(song_->endMs / 60000), int(song_->endMs / 1000) % 60);
    canvas_.text(24, 44, 1.5f, {160, 160, 170, 255}, buf);
    if (player_->failed()) canvas_.textCentered(kScreenW * 0.5f, 330, 4, {230, 60, 60, 255}, "SONG FAILED");
    if (captureDebugScreenshot(canvas_, true, t)) running_ = false;
    canvas_.end();
    // A failed song closes a little after the fail (GH2's lose screen comes later).
    if (player_->failed() && failMs_ < -1e8f) failMs_ = t;
    if (t > song_->endMs + kOutroMs || (player_->failed() && t > failMs_ + 3000)) endSong();
}

void App::frameList() {
    canvas_.begin({14, 12, 20, 255});
    canvas_.text(40, 30, 3, {235, 235, 240, 255}, std::string(app::kWindowTitle) + "  " + app::kVersion);
    canvas_.text(40, 70, 1.5f, {150, 150, 165, 255},
                 "Up/Down: song   Left/Right: difficulty   Enter: play   Esc: quit   In a song: 1-5 frets, Enter/Up/Down strum, Esc back");
    canvas_.text(40, 100, 2, {255, 210, 80, 255}, std::string("Difficulty: ") + difficultyName(static_cast<Difficulty>(difficulty_)));
    const int rows = 26;
    const size_t first = cursor_ >= size_t(rows / 2) ? std::min(cursor_ - size_t(rows / 2), songs_.size() > size_t(rows) ? songs_.size() - size_t(rows) : 0) : 0;
    for (size_t i = first; i < songs_.size() && i < first + size_t(rows); ++i) {
        const SongInfo& s = songs_[i];
        const bool sel = i == cursor_;
        if (sel) canvas_.fillRect(36, 136 + float(i - first) * 21 - 3, 1208, 20, {50, 44, 70, 255});
        canvas_.text(44, 136 + float(i - first) * 21, 2, sel ? SDL_Color{255, 255, 255, 255} : SDL_Color{180, 180, 190, 255},
                     s.name + "  -  " + s.artist);
    }
    if (!message_.empty()) canvas_.text(40, 694, 1.5f, {230, 90, 90, 255}, message_);
    if (captureDebugScreenshot(canvas_, false, 0)) running_ = false;
    canvas_.end();
}

}  // namespace

int runApp(const Options& options) { return App(options).run(); }
