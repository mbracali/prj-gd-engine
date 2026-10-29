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
#include "data/qbkey.h"
#include "render/canvas.h"
#include "render/texture_cache.h"
#include "render/venue_stage.h"
#include "script/globals.h"

namespace {

constexpr double kStepMs = 1000.0 / 60.0;  // GH3 runs its judge once per game frame [I: 60 Hz]
// The intro before the music: fastintro's 3000 ms (quickplay) [C script start_gem_scroller, fastintro props].
constexpr double kLeadInMs = 3000.0;
const SDL_Color kFretColors[5] = {{40, 200, 60, 255}, {220, 40, 40, 255}, {230, 210, 40, 255}, {40, 100, 230, 255}, {240, 130, 30, 255}};

}  // namespace

SongScreen::SongScreen() = default;
SongScreen::~SongScreen() = default;

bool SongScreen::start(const Disc& disc, const Globals& g, const SongInfo& song, int difficulty, bool autoplay, bool noFail,
                       Mixer& mixer, std::string& error, const TextureCache* textures) {
    textures_ = textures;
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
    // The guitarist's highway: sys_axel_1_highway (Axel until the character
    // select runs, M6) [C data: material 0x89A130C2 in axel_1.pak].
    if (textures_)
        if (uint32_t t = textures_->materialTexture(0x89A130C2)) highway_.setSurfaceTexture(t);
    if (!highwayOk_) SDL_Log("highway: the 2D highway's tables are missing; the debug highway is drawn");
    auto stream = SongStream::open(disc, song.name, error);
    if (!stream) return false;
    endMs_ = std::max(endMs_, double(stream->length()) * 1000.0 / std::max(1, stream->sampleRate()));
    // Six channels: guitar L R (muted on a miss), rhythm L R, song L R.
    std::vector<Mixer::Track> tracks(6);
    for (int i = 0; i < 6; ++i) tracks[size_t(i)].pan = (i % 2) ? 1.0f : -1.0f;
    tracks[0].guitar = tracks[1].guitar = true;
    mixer.play(std::move(stream), std::move(tracks), kLeadInMs);
    // The venue: its scene, cameras and light show (uploaded on the first frame).
    if (!SDL_getenv("GD04_NO_VENUE")) {
        disc_ = &disc;
        venue_ = std::make_unique<VenueStage>();
        std::string verr;
        const std::string zone = VenueStage::chooseZone(g);
        if (!venue_->start(disc, g, zone, chart_, qbkey::key(song.name), kLeadInMs, nullptr, verr)) {
            SDL_Log("venue: %s", verr.c_str());
            venue_.reset();
        }
    }
    stepMs_ = -kLeadInMs;
    SDL_Log("song %s (%s, %s): %llu notes", song.name.c_str(), song.title.c_str(), difficultyName(difficulty_),
            static_cast<unsigned long long>(track_.notes.size()));
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
        if (venue_) venue_->update(stepMs_, player_->stats().health);
        hud_.update(player_->stats(), stepMs_);
        for (const PlayerEvent& ev : player_->events()) {
            if (venue_ && ev.note >= 0 && (ev.kind == PlayerEvent::Kind::Hit || ev.kind == PlayerEvent::Kind::Miss))
                venue_->playerNote(stepMs_, ev.kind == PlayerEvent::Kind::Hit);
            if (ev.kind == PlayerEvent::Kind::Hit) {
                mixer.setGuitarAudible(true);
                if (ev.note >= 0) highway_.noteHit(track_.notes[size_t(ev.note)].frets, stepMs_, player_->stats().starPowerActive);
            }
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
    // GD04_END_AT=<song ms> / GD04_FAIL_AT=<song ms>: end (win) or fail the song then (walk tests).
    static const double endAt = SDL_getenv("GD04_END_AT") ? std::atof(SDL_getenv("GD04_END_AT")) : walkEndMs_;
    static const double failAt = SDL_getenv("GD04_FAIL_AT") ? std::atof(SDL_getenv("GD04_FAIL_AT")) : walkFailMs_;
    if (failAt >= 0 && now >= failAt && finishedAt_ < 0) forcedFail_ = true, finishedAt_ = now - 2000.0;
    if (endAt >= 0 && now >= endAt && finishedAt_ < 0) finishedAt_ = now - 2000.0;
    if (now >= endMs_ || (player_->stats().failed && finishedAt_ < 0)) {
        if (finishedAt_ < 0) finishedAt_ = now;
    }
    const bool more = finishedAt_ < 0 || now < finishedAt_ + 2000.0;
    if (!more) {
        const Hud2D::Counters& h = hud_.counters();
        SDL_Log("hud: score %llu, needle %llu, tubes %llu, multiplier %llu, streak bar in %llu / out %llu, dial flips %llu, combos %llu, "
                "danger %llu, star ready %llu / on %llu / off %llu, flames %llu, missing textures %llu",
                (unsigned long long)h.scoreChanges, (unsigned long long)h.needleChanges, (unsigned long long)h.tubeChanges,
                (unsigned long long)h.multiplierChanges, (unsigned long long)h.streakBarIns, (unsigned long long)h.streakBarOuts,
                (unsigned long long)h.dialFlips, (unsigned long long)h.combos, (unsigned long long)h.dangerStarts,
                (unsigned long long)h.starReady, (unsigned long long)h.starActivations, (unsigned long long)h.starOffs,
                (unsigned long long)highway_.flamesStarted, (unsigned long long)h.missingTextures);
    }
    return more;
}

double SongScreen::baseScore() const {
    double b = 0;
    for (const TrackNote& n : track_.notes) b += 50.0 * __builtin_popcount(unsigned(n.frets));
    return b;
}

int SongScreen::phraseCount() const {
    int last = -1, count = 0;
    for (const TrackNote& n : track_.notes)
        if (n.phrase >= 0 && n.phrase != last) last = n.phrase, ++count;
    return count;
}

void SongScreen::draw(Canvas& c, double now, const TextureCache* textures) {
    if (venue_) {
        // The venue first, under the 2D highway and HUD.
        std::string verr;
        if (!venue_->view() && !venue_->attach(c.renderer().device(), *disc_, verr)) {
            SDL_Log("venue: %s", verr.c_str());
            venue_.reset();
        } else {
            c.flush();
            venue_->advance(now);
            venue_->draw(now);
        }
    }
    const PlayerStats& st = player_->stats();
    char text[160];
    if (highwayOk_ && textures && !std::getenv("GD04_DEBUG_HIGHWAY")) {
        highway_.draw(c, *textures, track_, *player_, now, shownHeld_);
        hud_.draw(c, *textures, now);
        if (std::getenv("GD04_HUD_NUMBERS")) {  // development aid: the numbers behind the HUD
            std::snprintf(text, sizeof text, "%lld   x%d   streak %d", static_cast<long long>(st.score), st.multiplier(), st.currentRun);
            c.text(40, 680, 2, SDL_Color{255, 255, 255, 255}, text);
            std::snprintf(text, sizeof text, "rock %.2f  star %.0f%s", double(st.health), double(st.starPower), st.starPowerActive ? " ON" : "");
            c.text(1000, 680, 2, SDL_Color{255, 255, 255, 255}, text);
        }
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
