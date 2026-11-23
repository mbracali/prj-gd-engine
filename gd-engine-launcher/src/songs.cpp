// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "Hear the songs" screen: a game's songs, read straight from the disc,
// listed so the player can hear a short preview of one. The launcher lists
// and plays; the game module decodes the preview with its own audio loaders
// (gd_game_songs, sdk/gd_host.h), so the launcher knows nothing of the disc's
// audio formats. Opened from the Explore models screen (the M key) or the
// `songs=<game>` script command.
#include <algorithm>
#include <cmath>
#include <vector>

#include "launcher.h"
#include "module_host.h"
#include "palette.h"
#include "platform.h"
#include "songs.h"

using gfx::Color;
using gfx::FontId;
using gfx::Rect;
using gfx::rgb;
using gfx::withAlpha;
using namespace palette;

namespace {
const std::string kDot = " \xC2\xB7 ";
constexpr float kMargin = 24;
constexpr float kTopS = 92;
constexpr float kFooterHs = 58;
constexpr float kItemHs = 56;
// A generous preview: enough for a chorus, trimmed by the module if shorter.
constexpr int kPreviewRate = 48000;
constexpr int kPreviewSeconds = 30;
}  // namespace

// ---- Opening and closing ----

void Launcher::songsOpen(Game& g) {
    log_.add("hear the songs of " + g.id + " (" + g.title() + " " + g.version + ") with " + g.iso);
    drawLoading(g, "Reading the songs of");
    auto so = std::make_unique<Songs>();
    so->gameId = g.id;
    so->title = g.title();
    std::string error;
    if (!so->session.open(window_, context_, g, catalog_.launcherVersion(), log_, error)) {
        restoreWindow();
        log_.add("hear the songs of " + g.id + " failed: " + error);
        showDialog("No song previews yet",
                   "The songs of " + g.title() + " cannot be heard here yet: " + error +
                       ". A song preview needs the game module to add gd_game_songs (sdk/gd_host.h).",
                   {{"OK", Fret::Green, nullptr}});
        return;
    }
    restoreWindow();
    const GdSongs* api = so->session.api();
    const int n = api->song_count(api->self);
    for (int i = 0; i < n; ++i) {
        GdSong s{};
        if (api->song(api->self, i, &s) != 0) continue;
        Songs::Entry e;
        e.title = s.title ? s.title : (s.id ? s.id : "?");
        e.artist = s.artist ? s.artist : "";
        e.detail = s.detail ? s.detail : "";
        so->items.push_back(std::move(e));
    }
    log_.add("hear the songs of " + g.id + ": " + std::to_string(so->items.size()) + " songs");
    songs_ = std::move(so);
    songs_->lastFrame = now_;
}

void Launcher::songsStop() {
    if (!songs_) return;
    Songs& so = *songs_;
    if (so.stream) {
        SDL_DestroyAudioStream(so.stream);  // closes the device it opened too
        so.stream = nullptr;
    }
    if (so.streaming) {
        const GdSongs* api = so.session.api();
        if (api && GD_SONGS_HAS(api, stop_song)) api->stop_song(api->self);
    }
    so.streaming = false;
    so.paused = false;
    so.atEnd = false;
    so.playing = -1;
    so.parts.clear();
}

void Launcher::songsTogglePart(int part) {
    if (!songs_) return;
    Songs& so = *songs_;
    if (part < 0 || part >= int(so.parts.size())) return;
    so.parts[size_t(part)].on = !so.parts[size_t(part)].on;
    const GdSongs* api = so.session.api();
    if (api && GD_SONGS_HAS(api, set_part)) api->set_part(api->self, part, so.parts[size_t(part)].on ? 1 : 0);
    sounds_.play(UiSound::Move);
}

void Launcher::songsTogglePause() {
    if (!songs_) return;
    Songs& so = *songs_;
    if (!so.streaming || !so.stream) return;
    so.paused = !so.paused;
    if (so.paused) SDL_PauseAudioStreamDevice(so.stream);
    else SDL_ResumeAudioStreamDevice(so.stream);
    sounds_.play(UiSound::Move);
}

void Launcher::songsClose() {
    if (!songs_) return;
    songsStop();
    const std::string id = songs_->gameId;
    songs_->session.close();
    songs_.reset();
    restoreWindow();
    log_.add("hear the songs of " + id + ": closed");
}

// ---- Playback ----

void Launcher::songsPlay(int row) {
    if (!songs_ || row < 0 || row >= int(songs_->items.size())) return;
    Songs& so = *songs_;
    const GdSongs* api = so.session.api();
    if (!api) return;
    if (so.playing == row) {  // a second press stops it
        songsStop();
        return;
    }
    songsStop();

    // The whole song, with live per-part (stem) control, when the module offers
    // it (play_song/read_song). The launcher streams it chunk by chunk.
    if (GD_SONGS_HAS(api, play_song) && GD_SONGS_HAS(api, read_song)) {
        for (Game& g : catalog_.games())
            if (g.id == so.gameId) { drawLoading(g, "Opening"); break; }
        const int rc = api->play_song(api->self, row);
        SDL_GL_MakeCurrent(window_, context_);
        if (rc != 0) {
            sounds_.play(UiSound::Error);
            toast("This song could not be played.");
            return;
        }
        so.parts.clear();
        const int np = GD_SONGS_HAS(api, part_count) ? api->part_count(api->self, row) : 0;
        for (int i = 0; i < np; ++i) {
            GdSongPart p{};
            if (api->part(api->self, row, i, &p) != 0) continue;
            Songs::Part part;
            part.name = p.name ? p.name : "Part";
            part.on = p.default_on != 0;
            if (!part.on && GD_SONGS_HAS(api, set_part)) api->set_part(api->self, i, 0);
            so.parts.push_back(std::move(part));
        }
        so.chunk.assign(size_t(48000) * 2, 0.f);  // up to ~1 s per read (fewer at lower rates)
        int rate = 0;
        const int frames = api->read_song(api->self, so.chunk.data(), 48000, &rate);
        if (frames <= 0 || rate <= 0) {
            if (GD_SONGS_HAS(api, stop_song)) api->stop_song(api->self);
            sounds_.play(UiSound::Error);
            toast("This song could not be decoded.");
            return;
        }
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 2;
        spec.freq = rate;
        so.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!so.stream) {
            if (GD_SONGS_HAS(api, stop_song)) api->stop_song(api->self);
            sounds_.play(UiSound::Error);
            toast("No audio device for the song.");
            return;
        }
        SDL_PutAudioStreamData(so.stream, so.chunk.data(), frames * 2 * int(sizeof(float)));
        SDL_ResumeAudioStreamDevice(so.stream);
        so.playRate = rate;
        so.streaming = true;
        so.paused = false;
        so.atEnd = false;
        so.playing = row;
        sounds_.play(UiSound::Select);
        log_.add("songs: playing " + so.items[size_t(row)].title + " (" + std::to_string(so.parts.size()) + " parts)");
        return;
    }

    // Fallback: a one-shot preview (older module without play_song).
    for (Game& g : catalog_.games())
        if (g.id == so.gameId) { drawLoading(g, "Decoding a preview of"); break; }
    // The module decodes into the launcher's buffer (interleaved stereo floats).
    std::vector<float> pcm(size_t(kPreviewRate) * 2 * size_t(kPreviewSeconds));
    GdSongPcm out{};
    out.samples = pcm.data();
    out.capacity = int(pcm.size());
    out.frames = 0;
    out.rate = kPreviewRate;
    out.start_seconds = 0;
    const Uint64 t0 = SDL_GetTicksNS();
    const int rc = api->decode_preview(api->self, row, &out);
    SDL_GL_MakeCurrent(window_, context_);
    if (rc != 0 || out.frames <= 0 || out.rate <= 0) {
        sounds_.play(UiSound::Error);
        toast("This song could not be decoded.");
        log_.add("hear the songs of " + so.gameId + ": song " + std::to_string(row) + " failed");
        return;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = out.rate;
    so.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!so.stream) {
        sounds_.play(UiSound::Error);
        toast("No audio device for the preview.");
        return;
    }
    SDL_PutAudioStreamData(so.stream, pcm.data(), out.frames * 2 * int(sizeof(float)));
    SDL_FlushAudioStream(so.stream);
    SDL_ResumeAudioStreamDevice(so.stream);
    so.playing = row;
    sounds_.play(UiSound::Select);
    char secs[32];
    SDL_snprintf(secs, sizeof secs, "%.1f", double(out.frames) / out.rate);
    log_.add("hear the songs of " + so.gameId + ": " + so.items[size_t(row)].title + " preview " + secs + " s (" +
             std::to_string(double(SDL_GetTicksNS() - t0) / 1e9).substr(0, 4) + " s to decode)");
}

// ---- Input ----

bool Launcher::songsHandle(const SDL_Event& e) {
    Songs& so = *songs_;
    switch (e.type) {
    case SDL_EVENT_QUIT:
        songsClose();
        return true;
    case SDL_EVENT_DROP_BEGIN: case SDL_EVENT_DROP_COMPLETE: case SDL_EVENT_DROP_FILE: case SDL_EVENT_DROP_TEXT:
        return true;
    case SDL_EVENT_KEY_DOWN: {
        switch (e.key.key) {
        case SDLK_UP: case SDLK_W: so.cursor = std::max(0, so.cursor - 1); sounds_.play(UiSound::Move); return true;
        case SDLK_DOWN: case SDLK_S: so.cursor = std::min(int(so.items.size()) - 1, so.cursor + 1); sounds_.play(UiSound::Move); return true;
        case SDLK_PAGEUP: so.cursor = std::max(0, so.cursor - 6); return true;
        case SDLK_PAGEDOWN: so.cursor = std::min(int(so.items.size()) - 1, so.cursor + 6); return true;
        case SDLK_HOME: so.cursor = 0; return true;
        case SDLK_END: so.cursor = int(so.items.size()) - 1; return true;
        case SDLK_RETURN: case SDLK_KP_ENTER: if (!e.key.repeat) songsPlay(so.cursor); return true;
        case SDLK_SPACE:
            if (!e.key.repeat) { if (so.streaming) songsTogglePause(); else songsPlay(so.cursor); }
            return true;
        case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_M:
            if (!e.key.repeat) { sounds_.play(UiSound::Back); songsClose(); }
            return true;
        default: return true;  // the songs screen owns the keyboard while open
        }
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: so.cursor = std::max(0, so.cursor - 1); sounds_.play(UiSound::Move); return true;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: so.cursor = std::min(int(so.items.size()) - 1, so.cursor + 1); sounds_.play(UiSound::Move); return true;
        case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: songsPlay(so.cursor); return true;
        case SDL_GAMEPAD_BUTTON_NORTH: songsTogglePause(); return true;
        case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_BACK: sounds_.play(UiSound::Back); songsClose(); return true;
        default: return true;
        }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const float density = SDL_GetWindowPixelDensity(window_);
        const float mx = e.button.x * density, my = e.button.y * density;
        if (e.button.button == SDL_BUTTON_RIGHT) { sounds_.play(UiSound::Back); songsClose(); return true; }
        for (const Hit& h : so.hits) {
            if (!h.r.contains(mx, my)) continue;
            if (h.row == -3) {  // a footer fret button
                if (Fret(h.col) == Fret::Red) { sounds_.play(UiSound::Back); songsClose(); }
                else if (Fret(h.col) == Fret::Green) songsPlay(so.cursor);
                else if (Fret(h.col) == Fret::Blue) songsTogglePause();
                else if (Fret(h.col) == Fret::Orange) songsStop();
            } else if (h.row == -4) {  // a stem checkbox
                songsTogglePart(h.col);
            } else if (h.row >= 0) {
                so.cursor = h.row;
                songsPlay(h.row);
            }
            return true;
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        so.scrollTarget -= e.wheel.y * 72;
        return true;
    default:
        return false;
    }
}

// ---- A frame ----

void Launcher::songsFrame() {
    Songs& so = *songs_;
    so.lastFrame = now_;
    const GdSongs* api = so.session.api();
    if (so.streaming && so.stream && api) {
        // Keep ~0.5 s queued so the song plays without gaps. Each read decodes
        // the next chunk with the current part mask (a muted stem drops out live).
        const int bytesPerFrame = 2 * int(sizeof(float));
        const int target = std::max(1, so.playRate / 2) * bytesPerFrame;
        for (int guard = 0; !so.atEnd && !so.paused && SDL_GetAudioStreamQueued(so.stream) < target && guard < 8; ++guard) {
            int rate = 0;
            const int frames = api->read_song(api->self, so.chunk.data(), int(so.chunk.size() / 2), &rate);
            if (frames <= 0) { so.atEnd = true; break; }
            SDL_PutAudioStreamData(so.stream, so.chunk.data(), frames * bytesPerFrame);
        }
        if (so.atEnd && SDL_GetAudioStreamQueued(so.stream) <= 0) songsStop();  // reached the end, device drained
    } else if (so.stream && so.playing >= 0) {
        // A one-shot preview finished? (nothing queued and the device drained.)
        if (SDL_GetAudioStreamQueued(so.stream) <= 0) songsStop();
    }
    SDL_GetWindowSizeInPixels(window_, &pw_, &ph_);
    s_ = std::max(0.25f, std::min(float(pw_) / 1280.f, float(ph_) / 720.f));
    W_ = float(pw_) / s_, H_ = float(ph_) / s_;
    gfx_.begin(pw_, ph_);
    drawBackground();
    songsDraw();
    drawToast();
    gfx_.end();
    if (!pendingShot_.empty()) {
        screenshot(pendingShot_);
        pendingShot_.clear();
    }
    SDL_GL_SwapWindow(window_);
}

void Launcher::songsDraw() {
    Songs& so = *songs_;
    so.hits.clear();
    // Header.
    gfx_.image(gfx_.icon(), R(kMargin, 12, 68, 68));
    text(FontId::Title, 40, 104, 14, "Hear the songs", kCream);
    const float subW = text(FontId::SemiBold, 15, 106, 62, so.gameId + "/" + so.title, kSand, 1.5f);
    gfx_.rect(R(106, 84, std::min(140.f, subW), 3), kOxblood, 1.5f * s_);
    const std::string count = std::to_string(so.items.size()) + " songs";
    text(FontId::SemiBold, 18, W_ - kMargin - measure(FontId::SemiBold, 18, count), 22, count, kCream);
    const std::string from = "straight from your disc image";
    text(FontId::Regular, 15, W_ - kMargin - measure(FontId::Regular, 15, from), 48, from, kDim);

    // The list, centred. A strip of stem checkboxes sits above the footer while
    // a song plays (the parts the module reports, each a mute/unmute toggle).
    const bool showStems = so.playing >= 0 && !so.parts.empty();
    const float stemH = showStems ? 64 : 0;
    const float listX = kMargin, listW = W_ - 2 * kMargin;
    const float listTop = kTopS, listBottom = H_ - kFooterHs - 14 - stemH;
    const float view = listBottom - listTop;
    const float content = float(so.items.size()) * kItemHs;
    if (so.cursor >= 0) {
        const float cy = float(so.cursor) * kItemHs;
        if (cy < so.scrollTarget) so.scrollTarget = cy;
        if (cy + kItemHs > so.scrollTarget + view) so.scrollTarget = cy + kItemHs - view;
    }
    so.scrollTarget = std::clamp(so.scrollTarget, 0.f, std::max(0.f, content - view));
    so.scroll += (so.scrollTarget - so.scroll) * 0.3f;

    const Rect panel = R(listX, listTop, listW, view);
    gfx_.rect(panel, rgb(0x1d1a20, 0.9f), rgb(0x131115, 0.94f), 16 * s_);
    gfx_.ring(panel, withAlpha(kCream, 0.08f), 16 * s_, 1 * s_);
    const Rect clip = R(listX, listTop, listW, view);
    gfx_.clip(&clip);
    for (size_t i = 0; i < so.items.size(); ++i) {
        const float ry = listTop + float(i) * kItemHs - so.scroll;
        if (ry + kItemHs < listTop || ry > listBottom) continue;
        const Songs::Entry& it = so.items[i];
        const bool cur = int(i) == so.cursor;
        const bool playing = int(i) == so.playing;
        const Rect row = R(listX + 10, ry + 4, listW - 20, kItemHs - 8);
        if (cur) {
            gfx_.rect(row, rgb(0x2c2731, 0.98f), rgb(0x232027, 0.98f), 10 * s_);
            gfx_.ring(row, withAlpha(kCream, 0.6f), 10 * s_, 1.5f * s_);
        }
        // A play / speaker gem.
        const Color gem = playing ? fretColor(Fret::Green) : withAlpha(kMuted, 0.7f);
        if (playing) gfx_.glow(R(listX + 24, ry + kItemHs / 2 - 5, 10, 10), withAlpha(gem, 0.5f), 5 * s_, 8 * s_);
        gfx_.circle((listX + 29) * s_, (ry + kItemHs / 2) * s_, 5 * s_, gem);
        const std::string name = gfx_.elide(FontId::SemiBold, 19 * s_, it.title, (listW - 90) * s_);
        text(FontId::SemiBold, 19, listX + 46, ry + 8, name, cur || playing ? kCream : rgb(0xd6cab3));
        const std::string sub = it.artist + (it.artist.empty() || it.detail.empty() ? "" : kDot) + it.detail;
        if (!sub.empty())
            text(FontId::Regular, 13, listX + 46, ry + 32, gfx_.elide(FontId::Regular, 13 * s_, sub, (listW - 90) * s_), cur ? kSand : kDim);
        if (playing) {
            const std::string p = "Playing";
            text(FontId::SemiBold, 13, listX + listW - 24 - measure(FontId::SemiBold, 13, p), ry + kItemHs / 2 - 8, p, fretColor(Fret::Green));
        }
        so.hits.push_back({row, int(i), -1});
    }
    gfx_.clip(nullptr);
    so.hits.erase(std::remove_if(so.hits.begin(), so.hits.end(),
                                 [&](const Hit& hh) { return hh.row >= 0 && (hh.r.y + hh.r.h < listTop * s_ || hh.r.y > listBottom * s_); }),
                  so.hits.end());
    if (so.items.empty())
        text(FontId::Regular, 18, listX + 24, listTop + 20, "This disc has no songs the module can list.", kDim);

    // The stem checkboxes: one per part of the playing song, a live mute toggle.
    // The whole mix starts on; click a part to drop it out.
    if (showStems) {
        float mx = -1, my = -1;
        SDL_GetMouseState(&mx, &my);
        const float density = SDL_GetWindowPixelDensity(window_);
        mx *= density, my *= density;
        const float sy = H_ - kFooterHs - stemH;
        gfx_.rect(R(0, sy, W_, stemH), rgb(0x100e12, 0.92f), rgb(0x0b0a0d, 0.96f));
        gfx_.rect(R(0, sy, W_, 1), withAlpha(kCream, 0.08f));
        const float cyp = sy + stemH / 2;
        text(FontId::SemiBold, 12, kMargin, cyp - 7, "PARTS", kMuted, 1.5f);
        float sx = kMargin + 70;
        for (size_t i = 0; i < so.parts.size(); ++i) {
            const Songs::Part& p = so.parts[i];
            const float bw = measure(FontId::SemiBold, 15, p.name) + 50;
            const Rect r = R(sx, cyp - 17, bw, 34);
            const bool hover = r.contains(mx, my);
            const Color fc = p.on ? fretColor(Fret::Green) : kMuted;
            gfx_.rect(r, rgb(0x1f1b23, p.on ? 0.95f : 0.55f), rgb(0x18151b, p.on ? 0.95f : 0.55f), 17 * s_);
            gfx_.ring(r, withAlpha(fc, p.on ? (hover ? 0.95f : 0.65f) : (hover ? 0.5f : 0.22f)), 17 * s_, 1.3f * s_);
            const Rect box = R(sx + 12, cyp - 9, 18, 18);
            if (p.on)
                gfx_.rect(box, fc, gfx::mix(fc, kInk, 0.2f), 5 * s_);
            else {
                gfx_.rect(box, rgb(0x16141a), rgb(0x16141a), 5 * s_);
                gfx_.ring(box, withAlpha(kMuted, 0.6f), 5 * s_, 1.2f * s_);
            }
            text(FontId::SemiBold, 15, sx + 40, cyp - 9, p.name, p.on ? (hover ? kCream : kSand) : kMuted);
            so.hits.push_back({r, -4, int(i)});
            sx += bw + 8;
        }
    }

    // Footer.
    const float y = H_ - kFooterHs;
    gfx_.rect(R(0, y, W_, kFooterHs), rgb(0x0d0b0f, 0.92f), rgb(0x080709, 0.98f));
    gfx_.rect(R(0, y, W_, 1), withAlpha(kCream, 0.1f));
    float x = kMargin;
    const float cy = y + kFooterHs / 2;
    float mx = -1, my = -1;
    SDL_GetMouseState(&mx, &my);
    const float density = SDL_GetWindowPixelDensity(window_);
    mx *= density, my *= density;
    auto button = [&](Fret f, const std::string& label, bool on) {
        const Color fc = fretColor(f);
        const float bw = measure(FontId::SemiBold, 16, label) + 30;
        const Rect r = R(x, cy - 17, bw, 34);
        const bool hover = on && r.contains(mx, my);
        gfx_.rect(r, hover ? gfx::mix(fc, kInk, 0.55f) : rgb(0x1f1b23, on ? 0.95f : 0.6f), hover ? gfx::mix(fc, kInk, 0.7f) : rgb(0x18151b, on ? 0.95f : 0.6f), 17 * s_);
        gfx_.ring(r, on ? withAlpha(fc, hover ? 0.95f : 0.6f) : withAlpha(kCream, 0.08f), 17 * s_, (hover ? 1.6f : 1.2f) * s_);
        text(FontId::SemiBold, 16, x + 15, cy - 10, label, on ? (hover ? kCream : kSand) : kMuted);
        if (on) so.hits.push_back({r, -3, int(f)});
        x += bw + 10;
    };
    button(Fret::Green, so.playing == so.cursor ? "Stop" : "Play", !so.items.empty());
    if (so.streaming) button(Fret::Blue, so.paused ? "Resume" : "Pause", true);
    button(Fret::Orange, "Stop", so.playing >= 0);
    button(Fret::Red, "Back", true);
    const std::string hint = so.streaming ? "Click a part to mute it" + kDot + "Space pauses" + kDot + "Esc to go back"
                                          : "Up / Down to choose" + kDot + "Enter to play" + kDot + "Esc to go back";
    if (x + measure(FontId::Regular, 15, hint) + 20 < W_ - kMargin)
        text(FontId::Regular, 15, W_ - kMargin - measure(FontId::Regular, 15, hint), cy - 10, hint, kDim);
}
