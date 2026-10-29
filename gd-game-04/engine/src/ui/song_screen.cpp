// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/song_screen.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include "audio/mixer.h"
#include "audio/song_stream.h"
#include "data/disc.h"
#include "render/canvas.h"
#include "render/texture_cache.h"
#include "script/globals.h"

namespace {

constexpr double kStepMs = 1000.0 / 60.0;  // GH3 runs its judge once per game frame [I: 60 Hz]
constexpr double kLeadInMs = 2000.0;       // silence before the audio [I: until the song start order (M4)]
const SDL_Color kFretColors[5] = {{40, 200, 60, 255}, {220, 40, 40, 255}, {230, 210, 40, 255}, {40, 100, 230, 255}, {240, 130, 30, 255}};

}  // namespace

bool SongScreen::start(const Disc& disc, const Globals& g, const SongInfo& song, int difficulty, bool autoplay, bool noFail,
                       Mixer& mixer, std::string& error) {
    song_ = song;
    difficulty_ = std::clamp(difficulty, 0, 3);
    try {
        chart_ = loadChart(disc, song.name);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    if (!tuning_.load(g)) {
        error = "the gameplay tuning is incomplete";
        return false;
    }
    const float scale = song.hammerOnMeasureScale > 0 ? song.hammerOnMeasureScale : tuning_.defaultHammerOnMeasureScale;
    track_ = buildTrack(chart_, Part::Guitar, difficulty_, tuning_, scale);
    if (track_.notes.empty()) {
        error = song.name + " has no " + difficultyName(difficulty_) + " guitar chart";
        return false;
    }
    endMs_ = double(track_.notes.back().time + track_.notes.back().length) + 3000.0;
    player_ = std::make_unique<Player>(track_, tuning_, difficulty_, noFail);
    player_->setSongLengthMs(endMs_);
    if (autoplay) autoplay_ = std::make_unique<Autoplay>(track_, true);
    highwayOk_ = highway_.init(g, difficulty_);
    if (!highwayOk_) SDL_Log("highway: the 2D highway's tables are missing; the debug highway is drawn");
    auto stream = SongStream::open(disc, song.name, error);
    if (!stream) return false;
    endMs_ = std::max(endMs_, double(stream->length()) * 1000.0 / std::max(1, stream->sampleRate()));
    // Six channels: guitar L R (muted on a miss), rhythm L R, song L R.
    std::vector<Mixer::Track> tracks(6);
    for (int i = 0; i < 6; ++i) tracks[size_t(i)].pan = (i % 2) ? 1.0f : -1.0f;
    tracks[0].guitar = tracks[1].guitar = true;
    mixer.play(std::move(stream), std::move(tracks), kLeadInMs);
    stepMs_ = -kLeadInMs;
    SDL_Log("song %s (%s, %s): %zu notes", song.name.c_str(), song.title.c_str(), difficultyName(difficulty_), track_.notes.size());
    return true;
}

void SongScreen::handle(const InputEvent& e) {
    if (isFret(e.action)) {
        const uint8_t bit = uint8_t(1u << fretOf(e.action));
        held_ = e.down ? uint8_t(held_ | bit) : uint8_t(held_ & ~bit);
        // A joypad's fret press also strums (gd-game-02's input layer) [I for GH3].
        if (e.down && e.joypad) strum_ = true;
    } else if (e.action == Action::Strum && e.down) {
        strum_ = true;
    } else if (e.action == Action::StarPower) {
        star_ = e.down;
    } else if (e.action == Action::Whammy) {
        whammy_ = e.down ? 1.0f : 0.0f;
    }
}

void SongScreen::tick(double nowMs, Mixer& mixer) {
    // The judge runs on its own fixed 60 Hz steps up to the song clock, so
    // any frame rate plays the same (guidelines: frame-rate independence).
    while (stepMs_ + kStepMs <= nowMs) {
        stepMs_ += kStepMs;
        FrameInput in;
        if (autoplay_) {
            in = autoplay_->input(stepMs_, kStepMs, *player_);
        } else {
            in.frets = held_;
            in.strum = strum_;
            in.starPower = star_;
            in.whammy = whammy_;
            strum_ = false;
        }
        shownHeld_ = in.frets;
        player_->frame(stepMs_, kStepMs, in);
        for (const PlayerEvent& ev : player_->events()) {
            if (ev.kind == PlayerEvent::Kind::Hit) mixer.setGuitarAudible(true);
            else if (ev.kind == PlayerEvent::Kind::Miss || ev.kind == PlayerEvent::Kind::Overstrum) mixer.setGuitarAudible(false);
        }
        player_->clearEvents();
    }
}

bool SongScreen::frame(Canvas& canvas, Mixer& mixer, const TextureCache* textures) {
    const double now = mixer.frameTimeMs() - kLeadInMs;
    lastMs_ = now;
    tick(now, mixer);
    draw(canvas, now, textures);
    if (now >= endMs_ || (player_->stats().failed && finishedAt_ < 0)) {
        if (finishedAt_ < 0) finishedAt_ = now;
    }
    return finishedAt_ < 0 || now < finishedAt_ + 2000.0;
}

void SongScreen::draw(Canvas& c, double now, const TextureCache* textures) {
    const PlayerStats& st = player_->stats();
    char text[160];
    if (highwayOk_ && textures && !std::getenv("GD04_DEBUG_HIGHWAY")) {
        highway_.draw(c, *textures, track_, *player_, now, shownHeld_);
        // The HUD numbers (GH3's own HUD, create_2d_hud_elements, comes with the screen elements) [approximation].
        std::snprintf(text, sizeof text, "%lld   x%d   streak %d", static_cast<long long>(st.score), st.multiplier(), st.currentRun);
        c.text(40, 680, 2, SDL_Color{255, 255, 255, 255}, text);
        std::snprintf(text, sizeof text, "rock %.2f  star %.0f%s", double(st.health), double(st.starPower), st.starPowerActive ? " ON" : "");
        c.text(1000, 680, 2, SDL_Color{255, 255, 255, 255}, text);
        c.text(40, 20, 2, SDL_Color{200, 200, 200, 255}, song_.title + " - " + song_.artist);
        if (finishedAt_ >= 0) {
            std::snprintf(text, sizeof text, "%s  %d / %d notes", st.failed ? "SONG FAILED" : "SONG COMPLETE", st.notesHit, int(track_.notes.size()));
            c.textCentered(640, 200, 3, SDL_Color{255, 255, 255, 255}, text);
        }
        return;
    }
    // The debug highway (development aid until M4): five lanes, gems
    // scrolling down to the strike line over 1.5 s.
    const float x0 = 440, laneW = 80, top = 60, strike = 620;
    const double ahead = 1500.0;
    for (int l = 0; l < 5; ++l) c.fillRect(x0 + l * laneW + 38, top, 4, strike - top, SDL_Color{60, 60, 70, 255});
    c.fillRect(x0, strike - 2, laneW * 5, 4, SDL_Color{200, 200, 200, 255});
    for (size_t i = player_->current() > 3 ? player_->current() - 3 : 0; i < track_.notes.size(); ++i) {
        const TrackNote& n = track_.notes[i];
        if (n.time > now + ahead) break;
        const float y = strike - float((n.time - now) / ahead) * (strike - top);
        const float tail = n.sustain ? float(n.holdMs / ahead) * (strike - top) : 0;
        for (int l = 0; l < 5; ++l) {
            if (!(n.frets & (1 << l))) continue;
            const SDL_Color col = kFretColors[l];
            if (tail > 0) c.fillRect(x0 + l * laneW + 34, std::max(top, y - tail), 12, std::min(tail, y - top), col);
            if (y > strike + 30 || i < player_->current()) continue;
            const float s = n.hopo ? 22 : 30;
            c.fillRect(x0 + l * laneW + 40 - s / 2, y - s / 3, s, s * 2 / 3, n.phrase >= 0 ? SDL_Color{200, 230, 255, 255} : col);
        }
    }
    for (int l = 0; l < 5; ++l)
        if (held_ & (1 << l)) c.fillRect(x0 + l * laneW + 20, strike + 10, 40, 8, kFretColors[l]);
    const PlayerStats& s = player_->stats();
    char line[160];
    std::snprintf(line, sizeof line, "%lld  x%d  streak %d  health %.2f  star %.0f%s", static_cast<long long>(s.score), s.multiplier(),
                  s.currentRun, double(s.health), double(s.starPower), s.starPowerActive ? " ACTIVE" : "");
    c.text(40, 680, 2, SDL_Color{255, 255, 255, 255}, line);
    c.text(40, 20, 2, SDL_Color{200, 200, 200, 255}, song_.title + " - " + song_.artist);
    c.text(40, 44, 1, SDL_Color{150, 150, 150, 255}, "debug highway (development aid; GH3's own highway comes with M4)");
    if (finishedAt_ >= 0) {
        std::snprintf(line, sizeof line, "%s  %d / %d notes", s.failed ? "SONG FAILED" : "SONG COMPLETE", s.notesHit, int(track_.notes.size()));
        c.textCentered(640, 340, 3, SDL_Color{255, 255, 255, 255}, line);
    }
}
