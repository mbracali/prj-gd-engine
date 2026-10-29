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
#include "data/band.h"
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
                       Mixer& mixer, std::string& error, TextureCache* textures) {
    SongSetup one;
    one.difficulty = {difficulty, difficulty};
    return start(disc, g, song, one, autoplay, noFail, mixer, error, textures);
}

bool SongScreen::start(const Disc& disc, const Globals& g, const SongInfo& song, const SongSetup& setup, bool autoplay, bool noFail,
                       Mixer& mixer, std::string& error, TextureCache* textures) {
    textures_ = textures;
    song_ = song;
    startMs_ = setup.startMs;
    devilFinish_ = setup.devilFinish;
    difficulty_ = std::clamp(setup.difficulty[0], 0, 3);
    twoPlayers_ = setup.mode != GameMode::Solo;
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
    const int players = twoPlayers_ ? 2 : 1;
    highwayOk_ = highway_.init(g, difficulty_, players, 0);
    if (twoPlayers_) highwayOk_ = highway2_.init(g, std::clamp(setup.difficulty[1], 0, 3), 2, 1) && highwayOk_;
    // The players and the rules between them (game/match.h).
    MatchSetup ms;
    ms.mode = setup.mode;
    ms.players = players;
    ms.difficulty = setup.difficulty;
    for (int i = 0; i < 2; ++i) ms.part[size_t(i)] = setup.rhythm[size_t(i)] ? Part::Rhythm : Part::Guitar;
    ms.autoplay = {autoplay, autoplay};
    ms.travelMs = {double(highway_.travelMs()), double(twoPlayers_ ? highway2_.travelMs() : highway_.travelMs())};
    ms.hopoScale = scale;
    ms.noFail = noFail;
    ms.useCoopTracks = song.useCoopNotetracks;
    ms.suddenDeath = setup.suddenDeath;
    ms.startHealth = setup.startHealth;
    ms.autoplayFires = setup.autoplayFires;
    ms.startMs = startMs_;
    // input_offset = time_input_offset (the song's input_offset; the 360's
    // default -66 ms is left out, D7) - lag_calibration [C script gem_scroller].
    ms.inputOffsetMs = float(song.inputOffset - setup.lagMs);
    match_ = std::make_unique<Match>();
    if (!match_->start(chart_, tuning_, &g, ms, setup.bossProps, error)) return false;
    player_ = &match_->player(0);
    if (match_->lastNoteEndMs() <= 0) {
        error = song.name + " has no " + difficultyName(difficulty_) + " chart for this mode";
        return false;
    }
    endMs_ = match_->lastNoteEndMs() + 3000.0;
    if (twoPlayers_) hud2pOk_ = hud2p_.init(g, *match_);
    // Each player's highway: its character's sys_<name>_1_highway material
    // (player_status.highway_material, set by create_guitarist), else the
    // global fallback [C scripts create_guitarist, setup_highway].
    if (textures) {
        if (uint32_t t = highwayTexture(disc, g, *textures, 0, setup.bossProps)) highway_.setSurfaceTexture(t);
        if (twoPlayers_)
            if (uint32_t t = highwayTexture(disc, g, *textures, 1, setup.bossProps)) highway2_.setSurfaceTexture(t);
    }
    // The intro counts from current_starttime; the scroller's first frame is
    // the transition's time (fastintro 3000 ms) before it [C start_gem_scroller].
    const double first = startMs_ - kLeadInMs;
    hud_.setStart(startMs_);
    hud2p_.setStart(startMs_);
    for (int p = 0; p < players; ++p) {
        Highway2D& hw = p == 0 ? highway_ : highway2_;
        hw.setStart(startMs_, first);
        hw.setLefty(setup.lefty[size_t(p)] && !(setup.mode == GameMode::Boss && p == 1));
        hw.setGemOffset(song.gemOffset);  // gem_offset = default (0 on PC, D7) + the song's [C script gem_scroller]
    }
    // devil_finish: player 2's gem container at x 1000 (off screen) [C script setup_highway].
    if (devilFinish_ && twoPlayers_) highway2_.setContainerX(1000);
    hud2p_.setDevilFinish(devilFinish_);
    hud2p_.setSong(qbkey::key(song.name));
    {
        // get_song_artist_text: the songlist's artist_text ($artist_text_by ...) [C scripts].
        std::string by = "by";
        if (const qb::Value* props = g.find(qbkey::key("gh3_songlist_props")); props && props->st)
            if (const qb::Value* ss = g.field(*props->st, qbkey::key(song.name)); ss && ss->st)
                if (const qb::Value* at = g.field(*ss->st, qbkey::key("artist_text"))) {
                    if (at->type == qb::Type::String || at->type == qb::Type::WString) by = at->str;
                    else if (const qb::Value* gs = g.find(at->key); gs && (gs->type == qb::Type::String || gs->type == qb::Type::WString))
                        by = gs->str;
                }
        intro_.setup(song.title, by, song.artist, startMs_, first);
    }
    if (!highwayOk_) SDL_Log("highway: the 2D highway's tables are missing; the debug highway is drawn");
    auto stream = SongStream::open(disc, song.name, error);
    if (!stream) return false;
    if (startMs_ > 0 && !stream->seek(startMs_)) {
        error = song.name + ": cannot start the audio at " + std::to_string(int(startMs_)) + " ms";
        return false;
    }
    endMs_ = std::max(endMs_, double(stream->length()) * 1000.0 / std::max(1, stream->sampleRate()));
    // Six channels: guitar L R (muted on a miss), rhythm L R, song L R.
    std::vector<Mixer::Track> tracks(6);
    for (int i = 0; i < 6; ++i) tracks[size_t(i)].pan = (i % 2) ? 1.0f : -1.0f;
    // Player 1's part's stem mutes on its misses; player 2's (on the other
    // part: co-op, the boss's rhythm) on player 2's (Track::guitar2).
    const bool p1Rhythm = setup.mode == GameMode::Coop && setup.rhythm[0];
    const bool p2Other = setup.mode == GameMode::Coop || setup.mode == GameMode::Boss;
    const int p1Stem = p1Rhythm ? 2 : 0;
    tracks[size_t(p1Stem)].guitar = tracks[size_t(p1Stem + 1)].guitar = true;
    if (p2Other) tracks[size_t(2 - p1Stem)].guitar2 = tracks[size_t(3 - p1Stem)].guitar2 = true;
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
    stepMs_ = startMs_ - kLeadInMs;
    if (twoPlayers_)
        SDL_Log("song %s (%s): %s, %s / %s%s", song.name.c_str(), song.title.c_str(), gameModeName(setup.mode), difficultyName(difficulty_),
                difficultyName(setup.difficulty[1]), setup.suddenDeath ? ", sudden death" : "");
    else
        SDL_Log("song %s (%s, %s): %llu notes", song.name.c_str(), song.title.c_str(), difficultyName(difficulty_),
                static_cast<unsigned long long>(match_->track(0).notes.size()));
    return true;
}

void SongScreen::handle(const InputEvent& e) {
    const size_t p = (twoPlayers_ && e.player == 1) ? 1 : 0;
    if (isFret(e.action)) {
        const uint8_t bit = uint8_t(1u << fretOf(e.action));
        held_[p] = e.down ? uint8_t(held_[p] | bit) : uint8_t(held_[p] & ~bit);
        // A joypad's fret press also strums (gd-game-02's input layer) [I for GH3].
        if (e.down && e.joypad) strum_[p] = true;
    } else if (e.action == Action::Strum && e.down) {
        strum_[p] = true;
    } else if (e.action == Action::StarPower) {
        star_[p] = e.down;
    } else if (e.action == Action::Whammy) {
        whammy_[p] = e.down ? 1.0f : 0.0f;
    }
}

void SongScreen::tick(double nowMs, Mixer& mixer) {
    // The judge runs on its own fixed 60 Hz steps up to the song clock, so
    // any frame rate plays the same (guidelines: frame-rate independence).
    while (stepMs_ + kStepMs <= nowMs) {
        stepMs_ += kStepMs;
        // Decided (a fail, a KO, a boss beaten): the gem scroller stops [C kill_gem_scroller].
        if (match_->outcome() != Match::Outcome::None) continue;
        std::array<FrameInput, 2> in{};
        for (size_t p = 0; p < 2; ++p) {
            in[p].frets = held_[p];
            in[p].strum = strum_[p];
            in[p].starPower = star_[p];
            in[p].whammy = whammy_[p];
            strum_[p] = false;
        }
        match_->frame(stepMs_, kStepMs, in);
        for (int p = 0; p < match_->players(); ++p) shownHeld_[size_t(p)] = match_->shownInput(p).frets;
        if (venue_) venue_->update(stepMs_, player_->stats().health);
        if (twoPlayers_) hud2p_.update(*match_, stepMs_);
        else hud_.update(player_->stats(), stepMs_);
        const GameMode mode = match_->mode();
        const bool twoModes = mode == GameMode::Coop || mode == GameMode::FaceOff || mode == GameMode::ProFaceOff;
        for (int p = 0; p < match_->players(); ++p) {
            Player& pl = match_->player(p);
            Highway2D& hw = p == 0 ? highway_ : highway2_;
            hw.step(pl.stats(), stepMs_, kStepMs, match_->battleMode(), twoModes);
            const bool other = match_->mode() == GameMode::Coop || match_->mode() == GameMode::Boss;
            for (const PlayerEvent& ev : pl.events()) {
                if (p == 0 && venue_ && ev.note >= 0 && (ev.kind == PlayerEvent::Kind::Hit || ev.kind == PlayerEvent::Kind::Miss))
                    venue_->playerNote(stepMs_, ev.kind == PlayerEvent::Kind::Hit);
                const bool hit = ev.kind == PlayerEvent::Kind::Hit;
                if (hit || ev.kind == PlayerEvent::Kind::Miss || ev.kind == PlayerEvent::Kind::Overstrum) {
                    if (p == 1 && other) mixer.setGuitar2Audible(hit);
                    else mixer.setGuitarAudible(hit);
                }
                // GuitarEvent_MissedNote: a passing note missed with the guitar already
                // muted pulses the highway black; else the guitar mutes [C script].
                if (ev.kind == PlayerEvent::Kind::Miss && muted_[size_t(p)]) hw.missPulse(stepMs_);
                if (hit || ev.kind == PlayerEvent::Kind::Miss || ev.kind == PlayerEvent::Kind::Overstrum) muted_[size_t(p)] = !hit;
                if (hit && ev.note >= 0)
                    hw.noteHit(match_->track(p).notes[size_t(ev.note)].frets, shownHeld_[size_t(p)], stepMs_, pl.stats().starPowerActive);
            }
        }
        match_->clearEvents();
    }
}

bool SongScreen::frame(Canvas& canvas, Mixer& mixer, const TextureCache* textures) {
    const double now = mixer.frameTimeMs() - kLeadInMs + startMs_;
    lastMs_ = now;
    tick(now, mixer);
    draw(canvas, now, textures);
    // GD04_END_AT=<song ms> / GD04_FAIL_AT=<song ms>: end (win) or fail the song then (walk tests).
    static const double endAt = SDL_getenv("GD04_END_AT") ? std::atof(SDL_getenv("GD04_END_AT")) : walkEndMs_;
    static const double failAt = SDL_getenv("GD04_FAIL_AT") ? std::atof(SDL_getenv("GD04_FAIL_AT")) : walkFailMs_;
    if (failAt >= 0 && now >= failAt && finishedAt_ < 0) forcedFail_ = true, finishedAt_ = now - 2000.0;
    if (endAt >= 0 && now >= endAt && finishedAt_ < 0) finishedAt_ = now - 2000.0;
    if (now >= endMs_ || (match_->outcome() != Match::Outcome::None && finishedAt_ < 0)) {
        if (finishedAt_ < 0) finishedAt_ = now;
    }
    const bool more = finishedAt_ < 0 || now < finishedAt_ + 2000.0;
    if (!more && twoPlayers_) {
        // The two players' numbers and the mode's (walks and the self-test read them).
        const Match::Counters& k = match_->counters();
        const Hud2P::Counters& h = hud2p_.counters();
        for (int p = 0; p < 2; ++p) {
            const PlayerStats& s = match_->player(p).stats();
            SDL_Log("player %d: score %lld, %d / %d notes, best streak %d, health %.3f, star power %.1f (%d used), attacks fired %d, received %d, "
                    "cards %d, stolen %d, repaired %d",
                    p + 1, static_cast<long long>(s.score), s.notesHit, s.totalNotes, s.bestRun, double(s.health), double(s.starPower),
                    s.starPowerUses, k.fired[size_t(p)], k.received[size_t(p)], k.cards[size_t(p)], k.stolen[size_t(p)], k.repaired[size_t(p)]);
        }
        SDL_Log("match: %s, outcome %d, winner %d, raise-axe %d, co-op star power %d, boss forced misses %d; hud2p: score %llu, needle %llu, "
                "lead %llu, cards %llu, attack texts %llu, missing textures %llu",
                gameModeName(match_->mode()), int(match_->outcome()), match_->winnerAtEnd() + 1, k.raiseAxe, k.coopActivations, k.forcedMisses[1],
                (unsigned long long)h.scoreChanges, (unsigned long long)h.needleChanges, (unsigned long long)h.leadChanges,
                (unsigned long long)h.cardsShown, (unsigned long long)h.attackTexts, (unsigned long long)h.missingTextures);
    } else if (!more) {
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

Highway2D::Effects SongScreen::effects(int p, double now) const {
    Highway2D::Effects fx;
    if (!match_->battleMode()) return fx;
    const Match::BattleSide& b = match_->battle(p);
    fx.leftyGems = b.leftyGems, fx.leftyButtons = b.leftyButtons;
    const Match* m = match_.get();
    fx.hidden = [m, p](size_t n) { return m->gemHidden(p, n); };
    // Star gems: sys_BattleGEM_*, with the phrase's card icon (rolled when its first gem was made) [C 0x82120C90, 0x821217A0].
    fx.battle = true;
    fx.card = [m, p](size_t n) -> uint32_t {
        const NoteTrack& t = m->track(p);
        if (n >= t.notes.size() || t.notes[n].phrase < 0) return 0;
        const std::vector<int>& pc = m->battle(p).phraseCard;
        const size_t ph = size_t(t.notes[n].phrase);
        const int card = ph < pc.size() ? pc[ph] : -1;
        return card >= 0 && card < int(m->battleData().powerups.size()) ? m->battleData().powerups[size_t(card)].cardTexture : 0;
    };
    // Amp Overload bobs the highway +-8 px every 0.05 s [C battle_lightning].
    if (b.shakeUntil > now) fx.dy = (int(now / 50.0) % 2) ? 8.0f : -8.0f;
    // hammer_highway: pushed (-70,50) / (70,50) over 0.1 s and back [C script].
    const double t = now - b.highwayPushAt;
    if (t >= 0 && t < 200) {
        const float u = float(t < 100 ? t / 100 : 1 - (t - 100) / 100);
        fx.dx += (p == 0 ? -70.0f : 70.0f) * u, fx.dy += 50.0f * u;
    }
    return fx;
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
    if (twoPlayers_ && highwayOk_ && textures) {
        // GH3's two highways (x_offset_p2 left / right) and the mode's HUD.
        for (int p = 0; p < 2; ++p) {
            Highway2D& hw = p == 0 ? highway_ : highway2_;
            hw.setEffects(effects(p, now));
            hw.draw(c, *textures, match_->track(p), match_->player(p), now, shownHeld_[size_t(p)]);
        }
        if (hud2pOk_) hud2p_.draw(c, *textures, *match_, now);
        intro_.draw(c, introText_, now);
        return;
    }
    const NoteTrack& track_ = match_->track(0);
    if (highwayOk_ && textures && !std::getenv("GD04_DEBUG_HIGHWAY")) {
        highway_.draw(c, *textures, track_, *player_, now, shownHeld_[0]);
        hud_.draw(c, *textures, now);
        intro_.draw(c, introText_, now);
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
        if (held_[0] & (1 << l)) c.fillRect(x0 + l * laneW + 20, strike + 10, 40, 8, kFretColors[l]);
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

uint32_t SongScreen::highwayTexture(const Disc& disc, const Globals& g, TextureCache& tex, int p, uint32_t bossProps) const {
    // player_status.character_id -> the musician profile (find_profile_by_id:
    // the profile whose name's key is the id), outfit / style -> the body
    // Guitarist_<name>_Outfit<o>_Style<s>, whose pak holds the material
    // sys_<name>_1_highway; a boss's player 2 is its character_profile, outfit
    // 1 style 1 [C scripts create_guitarist, start_gem_scroller].
    auto structOf = [&g](uint32_t k) -> const qb::Struct* {
        const qb::Value* v = g.find(k);
        return v && v->st ? v->st.get() : nullptr;
    };
    auto keyOf = [](const qb::Value* v) -> uint32_t {
        if (!v) return 0;
        if (v->type == qb::Type::String || v->type == qb::Type::WString) return qbkey::key(v->str);
        return v->key;
    };
    auto numOf = [](const qb::Value* v) { return v && (v->type == qb::Type::Int || v->type == qb::Type::Float) ? v->number() : 1.0f; };
    const qb::Struct* ps = structOf(qbkey::key(p ? "player2_status" : "player1_status"));
    uint32_t charId = ps ? keyOf(g.field(*ps, qbkey::key("character_id"))) : 0;
    int outfit = ps ? std::max(1, int(numOf(g.field(*ps, qbkey::key("outfit"))))) : 1;
    int style = ps ? std::max(1, int(numOf(g.field(*ps, qbkey::key("style"))))) : 1;
    if (p == 1 && bossProps)
        if (const qb::Struct* b = structOf(bossProps))
            if (const qb::Value* cp = g.field(*b, qbkey::key("character_profile"))) charId = keyOf(cp), outfit = style = 1;
    const qb::Struct* prof = nullptr;
    if (const qb::Value* a = g.find(qbkey::key("musician_profiles")); a && a->arr)
        for (const qb::Value& e : a->arr->values)
            if (e.st)
                if (const qb::Value* n = e.st->find(qbkey::key("name")); n && keyOf(n) == charId) prof = e.st.get();
    if (!prof) prof = band::findProfile(g, "axel");
    if (!prof) return 0;
    const qb::Value* nv = prof->find(qbkey::key("name"));
    const std::string name = nv && (nv->type == qb::Type::String || nv->type == qb::Type::WString) ? nv->str : "axel";
    const uint32_t material = qbkey::key("sys_" + name + "_1_highway_sys_" + name + "_1_highway");
    if (uint32_t t = tex.materialTexture(material)) return t;
    char body[160];
    std::snprintf(body, sizeof body, "Guitarist_%s_Outfit%d_Style%d", name.c_str(), outfit, style);
    if (const qb::Struct* bd = band::bodyDesc(g, qbkey::key(body)))
        if (const qb::Value* pk = bd->find(qbkey::key("pak")); pk && (pk->type == qb::Type::String || pk->type == qb::Type::WString)) {
            const std::string path = band::pakPath(pk->str);
            if (disc.exists(path)) tex.loadPak(disc, path);
        }
    const uint32_t t = tex.materialTexture(material);
    if (!t) SDL_Log("highway: no %s highway material for player %d (the global highway)", name.c_str(), p + 1);
    return t;
}
