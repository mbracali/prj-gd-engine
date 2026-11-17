// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/song_screen.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "audio/audio_out.h"
#include "data/song_list.h"
#include "data/umd.h"
#include "game/preview_sync.h"
#include "render/canvas.h"

namespace gr = gd::core::render;

namespace {

// The camera and strike line [I] (song_screen.h): relative to the active
// track's centre; GD24_CAMERA="ex,ey,ez,tx,ty,tz,fov" overrides.
struct Camera {
    float eye[3] = {0, -10.2f, 3.6f};
    float target[3] = {0, 0.5f, 0};
    float fov = 40;
};

Camera camera() {
    Camera c;
    if (const char* v = SDL_getenv("GD24_CAMERA")) {
        float f[7];
        if (std::sscanf(v, "%f,%f,%f,%f,%f,%f,%f", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]) == 7) {
            c.eye[0] = f[0], c.eye[1] = f[1], c.eye[2] = f[2], c.target[0] = f[3], c.target[1] = f[4], c.target[2] = f[5];
            c.fov = f[6];
        }
    }
    return c;
}

constexpr float kTrackGap = 0.1f;  // [I]
const char* const kGemModel[4] = {"gem_4a", "gem_4b", "gem_4c", "gem_4d"};
const char* const kCrashModel[4] = {"gem_4a_crash", "gem_4b_crash", "gem_4c_crash", "gem_4d_crash"};
const char* const kPlateModel[4] = {"plate_4a", "plate_4b", "plate_4c", "plate_4d"};
// The tracks' icons on the track plane, drums / bass / guitar / vocals.
const char* const kIcon[4] = {"icon_drum", "icon_bass", "icon_guitar", "icon_mic"};
const char* const kShortName[4] = {"Drums", "Bass", "Guitar", "Vocals"};

}  // namespace

const char* SongScreen::difficultyName(int d) {
    static const char* const k[4] = {"Easy", "Medium", "Hard", "Expert"};
    return k[std::clamp(d, 0, 3)];
}

SongScreen::~SongScreen() {
    if (decoding_.valid()) decoding_.wait();
}

void SongScreen::release() { renderer_.shutdown(); }

bool SongScreen::start(const gd24::Umd& umd, const gd24::SongEntry& song, int difficulty, int track, std::string& error) {
    if (decoding_.valid()) decoding_.wait();
    if (!assetsLoaded_) {
        assetsLoaded_ = assets_.load(umd, &assetsError_);
        if (!assetsLoaded_) {
            error = assetsError_;
            return false;
        }
        for (int t = 0; t < 4; ++t) iconTex_[t] = assets_.texture(kIcon[t]);
        if (!assets_.laneMarks(laneX_, strikeY_)) SDL_Log("song: the strike line's lane marks were not found: default lanes");
        SDL_Log("song: lanes x %.3f %.3f %.3f %.3f, strike line y %.3f", double(laneX_[0]), double(laneX_[1]), double(laneX_[2]),
                double(laneX_[3]), double(strikeY_));
    }
    std::vector<uint8_t> bytes;
    gd24::Beatmatch bm;
    if (!umd.read(song.beatmatchPath(), bytes) || !bm.parse(std::move(bytes), &error)) {
        if (error.empty()) error = "cannot read " + song.beatmatchPath();
        return false;
    }
    const gd24::BeatmatchImage* im = &bm.images[0];
    for (const auto& x : bm.images)
        if (int(x.difficulty) == difficulty) im = &x;
    std::vector<gd24::Beatmatch::Track> tracks = bm.readTracks(*im, &error);
    if (tracks.empty()) return false;
    cfg_ = gd24::readSongConfig(bm, *im, song.folder);
    if (!cfg_.found) SDL_Log("song %s: no (preview ...) entry in its DTA: the preview is placed at 0 ms", song.folder.c_str());
    const std::vector<gd24::TimeSig> sigs = bm.readTimeSigs(*im);
    play_.start(tracks, sigs, difficulty, track >= 0 ? track : cfg_.initialTrack, cfg_.slopMs);
    play_.startAt(cfg_.previewStartMs);
    finished_ = false;
    // The song ends with its preview (the full audio is out of scope, D6);
    // without a preview entry, with the chart.
    endMs_ = cfg_.found && cfg_.previewEndMs > cfg_.previewStartMs ? double(cfg_.previewEndMs) : play_.chartEndMs() + 1000.0;
    songMs_ = cfg_.previewStartMs, lastPos_ = -1;
    camTrack_ = float(play_.active());
    title_ = song.title;
    const char* ap = SDL_getenv("GD24_AUTOPLAY");
    autoplay_ = ap && ap[0] == '1';
    audioStarted_ = false;
    lastNs_ = 0;
    SDL_Log("song %s: %zu time signatures, first %d/%d; phrases %zu / %zu / %zu / %zu", song.folder.c_str(), sigs.size(), sigs[0].num,
            sigs[0].den, play_.tracks()[0].phrases.size(), play_.tracks()[1].phrases.size(), play_.tracks()[2].phrases.size(),
            play_.tracks()[3].phrases.size());
    SDL_Log("song %s: %s, preview %d..%d ms, song_scroll_speed %d, y_per_second %.1f, slop %d, initial track %d, gems %zu / %zu / %zu / %zu",
            song.folder.c_str(), difficultyName(difficulty), cfg_.previewStartMs, cfg_.previewEndMs, cfg_.scrollSpeedMs,
            double(yPerSecond()), play_.slop(), play_.active(), play_.tracks()[0].gems.size(), play_.tracks()[1].gems.size(),
            play_.tracks()[2].gems.size(), play_.tracks()[3].gems.size());
    if (!song.hasPreview) return true;  // the chart scrolls on the system clock
    const gd24::Umd* u = &umd;
    const std::string path = song.previewPath();
    const int startMs = cfg_.previewStartMs;
    lagMs_ = 0;
    decoding_ = std::async(std::launch::async, [u, path, tracks, startMs, this]() -> std::shared_ptr<PcmClip> {
        std::vector<uint8_t> b;
        auto clip = std::make_shared<PcmClip>();
        std::string e;
        if (!u->read(path, b) || !decodeAt3(b, *clip, e)) {
            SDL_Log("preview %s: %s", path.c_str(), e.empty() ? "cannot read" : e.c_str());
            return nullptr;
        }
        // Where the preview sits (game/preview_sync.h): applied when the
        // onsets agree clearly enough.
        const gd24::PreviewLag pl = gd24::measurePreviewLag(*clip, tracks, startMs);
        measuredLag_ = pl.strength > 1.3 ? pl.lagMs : 0;
        SDL_Log("preview %s: onsets %+d ms from the chart (strength %.2f): %s", path.c_str(), pl.lagMs, pl.strength,
                pl.strength > 1.3 ? "applied" : "not applied");
        return clip;
    });
    return true;
}

void SongScreen::stop(AudioOut& audio) {
    if (decoding_.valid()) decoding_.wait(), decoding_ = {};
    SDL_Log("song: stopped at %.0f ms: score %d, %d hits, %d misses, %d wrong presses, longest streak %d phrases (track %d)%s", songMs_,
            play_.score(), play_.hits(), play_.misses(), play_.wrongPresses(), play_.bestStreak(), play_.active(),
            play_.failed() ? ", song failed" : "");
    audio.stop();
    audioStarted_ = false;
}

float SongScreen::yPerSecond() const {
    const float v = cfg_.yPerSecond[std::clamp(play_.difficulty(), 0, 3)];
    return v > 0 ? v : 9.5f;
}

float SongScreen::trackX(int t) const {
    float w = 3.2f;
    if (const gd24::HwModel* m = assets_.model("track_plane_A")) w = m->max[0] - m->min[0];
    return (float(t) - camTrack_) * (w + kTrackGap);
}

void SongScreen::update(AudioOut& audio) {
    const uint64_t now = SDL_GetTicksNS();
    const double dt = lastNs_ ? double(now - lastNs_) / 1e9 : 0;
    lastNs_ = now;
    if (decoding_.valid() && decoding_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::shared_ptr<PcmClip> clip = decoding_.get();
        lagMs_ = measuredLag_;
        if (clip && audio.isOpen()) {
            audio.play(clip, true);
            audioStarted_ = true;
        }
    }
    if (audioStarted_) {
        const double pos = audio.positionSec();
        if (lastPos_ >= 0 && pos + 1.0 < lastPos_ && !finished_) finish();  // the preview looped: the end
        lastPos_ = pos;
        if (!finished_) songMs_ = cfg_.previewStartMs + pos * 1000.0 - lagMs_;
    } else if (!decoding_.valid() && !finished_) {
        songMs_ += dt * 1000.0;  // no audio: the system clock
    }
    if (finished_) return;
    // Development aid (GD24_AUTOPLAY=1): the active track's gems pressed on
    // time through the same judging, track changes and Overdrive
    // (game/song_play.h autoplayStep).
    if (autoplay_) gd24::autoplayStep(play_, songMs_);
    play_.update(songMs_);
    {
        // Development log: the Phrase Indicator as it moves.
        const gd24::SongPlay::Track& at = play_.tracks()[size_t(play_.active())];
        const int ind = at.indicator >= 0 ? play_.active() * 1000 + at.indicator : -1;
        if (ind != lastIndicator_) {
            lastIndicator_ = ind;
            if (at.indicator >= 0)
                SDL_Log("song: %.0f ms: phrase indicator on %s, phrase %d (%.0f..%.0f ms, %zu gems), multiplier x%d", songMs_,
                        kShortName[play_.active()], at.indicator, at.phrases[size_t(at.indicator)].startMs,
                        at.phrases[size_t(at.indicator)].endMs, at.phrases[size_t(at.indicator)].end - at.phrases[size_t(at.indicator)].first,
                        play_.multiplier());
            else
                SDL_Log("song: %.0f ms: no phrase indicator (%s %s)", songMs_, kShortName[play_.active()],
                        at.captured ? "captured" : at.failed ? "failed" : "-");
        }
    }
    if (play_.failed() || songMs_ >= endMs_) finish();
    const float target = float(play_.active());
    camTrack_ += (target - camTrack_) * float(std::min(1.0, dt * 10.0));
    if (std::fabs(target - camTrack_) < 0.001f) camTrack_ = target;
}

void SongScreen::finish() {
    finished_ = true;
    const gd24::SongPlay::Stats& st = play_.stats();
    SDL_Log("song: %s at %.0f ms: score %d (gems %d, captures %d), longest streak %d phrases, max multiplier %d, Overdrive %d, rescues %d",
            play_.failed() ? "failed" : "complete", songMs_, st.score, st.gemScore, st.captureScore, st.bestPhraseStreak, st.maxMultiplier,
            st.odActivations, st.rescues);
    for (int t = 0; t < gd24::SongPlay::kTracks; ++t) {
        const gd24::SongPlay::Track& tr = play_.tracks()[size_t(t)];
        SDL_Log("song:   %-6s phrases %d / %d, health %.2f%s", kShortName[t], tr.capturedCount, tr.attempted, double(tr.health),
                tr.failed ? " (failed)" : "");
    }
}

bool SongScreen::action(Action a, bool down, AudioOut& audio) {
    if (finished_) {
        if (down && (a == Action::Circle || a == Action::Start || a == Action::Cross || a == Action::Overdrive)) {
            stop(audio);
            return false;
        }
        return true;
    }
    const int lane = a == Action::Lane0 ? 0 : a == Action::Lane1 ? 1 : a == Action::Lane2 ? 2 : a == Action::Lane3 ? 3 : -1;
    if (lane >= 0) {
        lanesDown_[lane] = down;
        if (down) {
            laneFlash_[lane] = songMs_;
            play_.press(lane, songMs_);
        }
        return true;
    }
    if (!down) return true;
    if (a == Action::Overdrive) play_.overdrive(songMs_);
    else if (a == Action::TrackLeft) play_.switchTrack(-1, songMs_);
    else if (a == Action::TrackRight) play_.switchTrack(1, songMs_);
    else if (a == Action::Circle || a == Action::Start) {
        stop(audio);
        return false;
    }
    return true;
}

void SongScreen::drawOverlay(Canvas& c) {
    c.clearTransparent();
    const Rgba text{236, 228, 214}, dim{170, 160, 150}, accent{240, 140, 40};
    c.fill(0, 0, Canvas::kW, 30, Rgba{0, 0, 0, 140});
    char line[200];
    std::snprintf(line, sizeof line, "%s  -  %s  -  %s", title_.c_str(), difficultyName(play_.difficulty()), kShortName[play_.active()]);
    c.text(12, 8, line, accent, 2);
    std::snprintf(line, sizeof line, "%6.1f s", songMs_ / 1000.0);
    c.text(Canvas::kW - 12 - Canvas::textWidth(line, 2), 8, line, dim, 2);
    const char* help = "1-4 lanes  <- -> tracks  Space Overdrive  Esc";
    c.text(Canvas::kW - 12 - Canvas::textWidth(help, 1.5f), 38, help, dim, 1.5f);
    if (!audioStarted_ && decoding_.valid()) c.text(12, 40, "Loading the preview...", text, 2);

    // Our own HUD (the original's is not decoded yet): score, multiplier,
    // phrase streak, Energy meter, crowd meter and each track's health.
    c.fill(0, Canvas::kH - 34, Canvas::kW, 34, Rgba{0, 0, 0, 150});
    std::snprintf(line, sizeof line, "Score %d", play_.score());
    float x = 12;
    x += c.text(x, Canvas::kH - 24, line, text, 2) + 20;
    std::snprintf(line, sizeof line, "x%d", play_.multiplier());
    x += c.text(x, Canvas::kH - 24, line, play_.inOverdrive() ? Rgba{120, 200, 255} : accent, 2) + 20;
    std::snprintf(line, sizeof line, "Streak %d", play_.streak());
    x += c.text(x, Canvas::kH - 24, line, text, 2) + 24;
    // Energy (Overdrive) meter: half way marked.
    const float ew = 160, ex = x, ey = Canvas::kH - 26;
    c.text(ex, ey - 6, "Energy", dim, 1);
    c.frame(ex, ey + 4, ew, 10, dim);
    c.fill(ex + 1, ey + 5, (ew - 2) * play_.energy(), 8, play_.inOverdrive() ? Rgba{120, 200, 255} : Rgba{230, 230, 230});
    c.fill(ex + ew / 2, ey + 2, 1, 14, accent);
    x += ew + 24;
    // Crowd meter.
    const float cw = 120;
    c.text(x, ey - 6, "Crowd", dim, 1);
    c.frame(x, ey + 4, cw, 10, dim);
    const float crowd = std::clamp(play_.crowd(), 0.0f, 1.0f);
    c.fill(x + 1, ey + 5, (cw - 2) * crowd, 8, crowd < 0.3f ? Rgba{230, 60, 50} : Rgba{90, 200, 90});
    x += cw + 24;
    // The tracks' health, in their order on the highway.
    for (int t = 0; t < gd24::SongPlay::kTracks; ++t) {
        const gd24::SongPlay::Track& tr = play_.tracks()[size_t(t)];
        const float hx = x + float(t) * 58;
        const Rgba col = tr.failed ? Rgba{120, 30, 30}
                         : tr.captured ? Rgba{120, 200, 255}
                         : tr.health < play_.rules().healthDanger ? Rgba{230, 60, 50}
                                                                  : Rgba{200, 200, 200};
        c.text(hx, ey - 6, tr.failed ? "FAIL" : kShortName[t], t == play_.active() ? accent : dim, 1);
        c.frame(hx, ey + 4, 44, 10, dim);
        c.fill(hx + 1, ey + 5, 42 * std::clamp(tr.health, 0.0f, 1.0f), 8, col);
    }
    if (finished_) drawResults(c);
}

// The end of the song: our own simple screen with the original's results
// (STR_EN.TXT: score, each instrument's phrases captured / attempted,
// "LONGEST STREAK: n phrases", SONG FAILED).
void SongScreen::drawResults(Canvas& c) {
    const Rgba text{236, 228, 214}, dim{170, 160, 150}, accent{240, 140, 40};
    const float w = 560, h = 330, x = (Canvas::kW - w) / 2, y = 90;
    c.fill(x, y, w, h, Rgba{10, 10, 14, 225});
    c.frame(x, y, w, h, accent);
    const gd24::SongPlay::Stats& st = play_.stats();
    const char* head = play_.failed() ? "SONG FAILED" : "SONG COMPLETE";
    c.text(x + (w - Canvas::textWidth(head, 3)) / 2, y + 16, head, play_.failed() ? Rgba{230, 70, 60} : accent, 3);
    char line[160];
    float ly = y + 58;
    auto row = [&](const char* k, const std::string& v) {
        c.text(x + 30, ly, k, dim, 2);
        c.text(x + w - 30 - Canvas::textWidth(v, 2), ly, v, text, 2);
        ly += 24;
    };
    row("SCORE", std::to_string(st.score));
    std::snprintf(line, sizeof line, "%d phrase%s", st.bestPhraseStreak, st.bestPhraseStreak == 1 ? "" : "s");
    row("LONGEST STREAK", line);
    int hit = 0, total = 0;
    for (int t = 0; t < gd24::SongPlay::kTracks; ++t) {
        const gd24::SongPlay::Track& tr = play_.tracks()[size_t(t)];
        hit += tr.hits, total += tr.hits + tr.misses;
        const int pct = tr.attempted ? tr.capturedCount * 100 / tr.attempted : 0;
        std::snprintf(line, sizeof line, "%d / %d  (%d%%)%s", tr.capturedCount, tr.attempted, pct, tr.failed ? "  FAILED" : "");
        std::string k = std::string(kShortName[t]) + " phrases";
        row(k.c_str(), line);
    }
    std::snprintf(line, sizeof line, "%d / %d", hit, total);
    row("NOTES HIT", line);
    std::snprintf(line, sizeof line, "x%d", st.maxMultiplier);
    row("BEST MULTIPLIER", line);
    std::snprintf(line, sizeof line, "%d  (rescues %d)", st.odActivations, st.rescues);
    row("OVERDRIVE", line);
    const char* back = "Confirm / Esc: back";
    c.text(x + (w - Canvas::textWidth(back, 1.5f)) / 2, y + h - 20, back, dim, 1.5f);
}

gr::Texture SongScreen::draw3d(gr::Device& dev, gr::Texture overlay) {
    if (!renderer_.ready()) {
        std::string error;
        if (!assetsLoaded_ || !renderer_.init(dev, assets_, error)) {
            if (!error.empty()) SDL_Log("song: %s", error.c_str());
            return overlay;
        }
    }
    const Camera cam = camera();
    const float aspect = float(gd24::HighwayRenderer::kW) / float(gd24::HighwayRenderer::kH);
    const float up[3] = {0, 0, 1};
    const gd24::Mat4 vp = gd24::Mat4::perspective(cam.fov, aspect, 0.1f, 200.0f) * gd24::Mat4::lookAt(cam.eye, cam.target, up);
    const float clear[4] = {0.03f, 0.03f, 0.05f, 1};
    renderer_.begin(vp, clear);

    const float yps = yPerSecond();
    const gd24::HwModel* plane = assets_.model("track_plane_A");
    const float farY = plane ? plane->max[1] : 6.3f;
    const double aheadMs = (farY - strikeY_) / yps * 1000.0, behindMs = 300.0;
    auto yOf = [&](double ms) { return strikeY_ + float((ms - songMs_) / 1000.0) * yps; };
    // [I] the tracks' state as tints until the original's effects are
    // decoded: the active track lit, a captured one dimmed, a track in
    // danger red (blinking when critical), a failed one dark red.
    auto tint = [&](gd24::HighwayRenderer::Draw& d, int t) {
        const gd24::SongPlay::Track& tr = play_.tracks()[size_t(t)];
        const float k = t == play_.active() ? 1.0f : 0.45f;
        d.tint[0] = d.tint[1] = d.tint[2] = k;
        if (tr.failed) {
            d.tint[0] = 0.55f, d.tint[1] = d.tint[2] = 0.12f;
        } else if (tr.captured) {
            d.tint[0] = d.tint[1] = d.tint[2] = k * 0.6f;
        } else if (tr.health < play_.rules().healthDanger) {
            const bool blinkOff = tr.health < play_.rules().healthCritical && std::fmod(songMs_, 400.0) < 200.0;
            d.tint[0] = k, d.tint[1] = d.tint[2] = blinkOff ? k : k * 0.35f;
        }
        d.overdrive = play_.inOverdrive();
    };

    for (int pass = 0; pass < 2; ++pass) {
        const bool blended = pass == 1;
        for (int t = 0; t < gd24::SongPlay::kTracks; ++t) {
            const float x = trackX(t);
            if (std::fabs(x) > 12) continue;
            gd24::HighwayRenderer::Draw d;
            d.blendedPass = blended;
            tint(d, t);
            d.model = gd24::Mat4::translate(x, 0, 0);
            // The track's instrument icon: materials track_instrument_icon0..3
            // use icon_drum / bass / guitar / mic [C data]; the plane's own
            // icon material has a placeholder (file4, black).
            gd24::HighwayRenderer::Draw pd = d;
            pd.swapSub = "track_instrument_icon", pd.swapTexture = iconTex_[t];
            renderer_.draw("track_plane_A", pd);
            // Beat lines from the chart's ticks (a bar every 4 beats [I: 4/4]).
            if (blended) {
                const double t0 = play_.msToTick(songMs_ - behindMs), t1 = play_.msToTick(songMs_ + aheadMs);
                for (long b = std::max(0L, long(std::ceil(t0 / 480.0))); b * 480.0 <= t1; ++b) {
                    const double ms = play_.tickToMs(double(b) * 480.0);
                    gd24::HighwayRenderer::Draw bd = d;
                    bd.model = gd24::Mat4::translate(x, yOf(ms), 0);
                    renderer_.draw(b % 4 == 0 ? "beat_bar2_a" : "beat_bar_a", bd);
                }
            }
            // [I] the Phrase Indicator: bright bars at the start and the end
            // of the phrase to play (the original's frame is not decoded).
            const gd24::SongPlay::Track& trk = play_.tracks()[size_t(t)];
            if (blended && trk.indicator >= 0) {
                const gd24::SongPlay::Phrase& ph = trk.phrases[size_t(trk.indicator)];
                const double pad = 120;
                const gd24::HwModel* bar = assets_.model("beat_bar2_a");
                const float bw = bar ? bar->max[0] - bar->min[0] : 2.9f, bdp = bar ? std::max(0.01f, bar->max[1] - bar->min[1]) : 0.1f;
                const float bcx = bar ? (bar->max[0] + bar->min[0]) / 2 : 0, bcy = bar ? (bar->max[1] + bar->min[1]) / 2 : 0;
                auto strip = [&](float cx, float cy, float w, float len) {
                    gd24::HighwayRenderer::Draw bd = d;
                    bd.tint[0] = 1.0f, bd.tint[1] = 0.6f, bd.tint[2] = 0.1f;
                    const float sx = w / bw, sy = len / bdp;
                    bd.model = gd24::Mat4::translate(cx - bcx * sx, cy - bcy * sy, 0.01f) * gd24::Mat4::scale(sx, sy, 1);
                    for (int k = 0; k < 2; ++k) renderer_.draw("beat_bar2_a", bd);  // twice: brighter (additive)
                };
                // The frame: its near / far edges across the track, its sides
                // along the visible part of the phrase.
                const float half = (laneX_[3] - laneX_[0]) / 2 + 0.37f;
                for (double ms : {ph.startMs - pad, ph.endMs + pad})
                    if (ms >= songMs_ - behindMs && ms <= songMs_ + aheadMs) strip(x, yOf(ms), 2 * half * 1.02f, 0.12f);
                const double s0 = std::max(ph.startMs - pad, songMs_ - behindMs), s1 = std::min(ph.endMs + pad, songMs_ + aheadMs);
                if (s1 > s0) {
                    const float y0 = yOf(s0), y1 = yOf(s1);
                    for (float side : {-half, half}) strip(x + side * 1.02f, (y0 + y1) / 2, 0.08f, y1 - y0);
                }
            }
            // The lane plates on the strike line.
            for (int l = 0; l < 4; ++l) {
                gd24::HighwayRenderer::Draw pd = d;
                pd.model = gd24::Mat4::translate(x + laneX_[l], strikeY_, 0);
                if (t == play_.active() && lanesDown_[l]) pd.model = pd.model * gd24::Mat4::scale(1, 1, 0.4f);
                renderer_.draw(kPlateModel[l], pd);
            }
            // The gems.
            const gd24::SongPlay::Track& tr = play_.tracks()[size_t(t)];
            auto first = std::lower_bound(tr.gems.begin(), tr.gems.end(), songMs_ - behindMs,
                                          [](const gd24::Gem& g, double ms) { return g.ms < ms; });
            for (size_t i = size_t(first - tr.gems.begin()); i < tr.gems.size() && tr.gems[i].ms <= songMs_ + aheadMs; ++i) {
                const gd24::Gem& g = tr.gems[i];
                const bool hit = tr.state[i] == gd24::SongPlay::State::Hit || tr.state[i] == gd24::SongPlay::State::Auto;
                for (int l = 0; l < 4; ++l) {
                    if (!(g.lanes & (1 << l))) continue;
                    gd24::HighwayRenderer::Draw gd = d;
                    if (hit) {
                        // The crash piece for 150 ms on the strike line.
                        const double age = songMs_ - tr.hitMs[i];
                        if (age > 150) continue;
                        const float s = 1.0f + float(age / 150.0) * 0.6f;
                        gd.model = gd24::Mat4::translate(x + laneX_[l], strikeY_, 0) * gd24::Mat4::scale(s, s, s);
                        renderer_.draw(kCrashModel[l], gd);
                        continue;
                    }
                    if (tr.state[i] == gd24::SongPlay::State::Missed) gd.tint[0] *= 0.5f, gd.tint[1] *= 0.5f, gd.tint[2] *= 0.5f;
                    gd.model = gd24::Mat4::translate(x + laneX_[l], yOf(g.ms), 0);
                    renderer_.draw(kGemModel[l], gd);
                }
            }
        }
    }
    return renderer_.end(overlay);
}
