// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/sfxtest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>

#include "audio/crowd_audio.h"
#include "audio/sfx_dsp.h"
#include "audio/sfx_player.h"
#include "audio/xaudio_reverb.h"
#include "audio/synth.h"
#include "audio/xma_decoder.h"
#include "data/game_data.h"
#include "game/venue.h"
#include "scene/milo.h"
#include "scene/synth_objects.h"

namespace {

using namespace scene;

// The sounds GH2's scripts and the port's game code name [C data: ui/,
// hud/, tutorial/ play_sfx / play_sequence / sfx; config/synth.dta's
// background_sfx].
const char* const kScriptSounds[] = {
    "button_select", "button_back.cue", "button_toggle", "button_play", "button_error", "meta_lick", "cheat", "cheat_off",
    "postshow_loop", "cash_award", "cash_award.cue", "cash_register", "store_grate.cue", "slider_sfx", "slider_gtr",
    "slider_band", "flush", "drummer_exp", "click.cue", "sp_deploystart.cue", "sp_deployed", "sp_deployed_multi",
    "sp_available", "track_exp", "meter_slide", "nowbar_1", "nowbar_2", "nowbar_3", "nowbar_4", "nowbar_5", "bkgd_music",
    "tuning_complete", "s19901", "s19902", "meta_people",
};
// The venue banks' cues (game/crowd.cpp, ui scripts' play_meta_sfx).
const char* const kVenueSounds[] = {"crowd_begin", "crowd_win", "crowd_lose", "encore_yes", "encore_no", "blew_bignote.cue",
                                    "claps"};

void writeWav(const std::string& path, const std::vector<float>& s, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t bytes = uint32_t(s.size() * 2);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f), u32(36 + bytes), std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16), u16(1), u16(1), u32(uint32_t(rate)), u32(uint32_t(rate) * 2), u16(2), u16(16);
    std::fwrite("data", 1, 4, f), u32(bytes);
    for (float x : s) {
        const int v = int(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
        u16(uint16_t(int16_t(v)));
    }
    std::fclose(f);
}

std::string safe(std::string s) {
    for (char& c : s)
        if (c == '/' || c == '\\' || c == ' ') c = '_';
    return s;
}

bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// The voice DSP against the rules confirmed in GH2's code (numbers only, no
// game data): envelope (0x826844C8), pan (0x826898C8), sends, the stereo
// fold, the limiter (0x82682958), the voice's rate and length (0x82689768),
// RandomGroupSeq's choice (0x822F4F18), the master fader and release on stop.
void dspChecks(const std::function<void(const std::string&)>& fail) {
    using namespace sfxdsp;
    int checks = 0;
    auto check = [&](bool ok, const std::string& what) {
        ++checks;
        if (!ok) fail("dsp: " + what);
    };
    float x[kBlock];
    // Envelope: the data's usual 1.3 ms attack (62 frames at 48 kHz) rises
    // linearly 0 -> 1 inside the first pass, then holds.
    {
        Envelope e(0.0013f, 0.0058f);
        std::fill(x, x + kBlock, 1.0f);
        e.process(x);
        check(x[0] == 0.0f && x[30] > 0.45f && x[30] < 0.52f && x[61] > 0.95f && x[62] == 1.0f && x[255] == 1.0f &&
                  e.state() == Envelope::kSustain,
              "1.3 ms attack shape");
        // Release 5.8 ms = 278 frames: 60 dB x 256 / 278 = 55.3 dB in the first pass
        // (linear 1 -> -55.3 dB), the rest of the way to -60 dB in the next, then silence.
        e.release();
        std::fill(x, x + kBlock, 1.0f);
        e.process(x);
        check(x[0] == 1.0f && near(e.gain(), dbToLin(-55.25f), 2e-4) && e.state() == Envelope::kRelease, "release pass 1");
        std::fill(x, x + kBlock, 1.0f);
        e.process(x);
        check(e.state() == Envelope::kDone && x[255] == 0.0f && x[0] > 0.0f && x[0] < 0.002f, "release pass 2 ends");
        std::fill(x, x + kBlock, 1.0f);
        e.process(x);
        check(x[0] == 0.0f && x[255] == 0.0f, "done is silent");
    }
    {
        // A 1 s attack: 0.32 dB per pass from -60 dB, so -28 dB after 100 passes.
        Envelope e(1.0f, 1.0f);
        for (int b = 0; b < 100; ++b) std::fill(x, x + kBlock, 1.0f), e.process(x);
        check(near(linToDb(e.gain()), -28.0, 0.01) && e.state() == Envelope::kAttack, "1 s attack is linear in dB");
        for (int b = 0; b < 88; ++b) std::fill(x, x + kBlock, 1.0f), e.process(x);
        check(e.state() == Envelope::kSustain, "1 s attack done after 188 passes");
        // Released at once (0 dB, 1 s release): -0.32 dB per pass.
        e.release();
        for (int b = 0; b < 50; ++b) std::fill(x, x + kBlock, 1.0f), e.process(x);
        check(near(linToDb(e.gain()), -16.0, 0.01), "1 s release");
    }
    // Pan: equal power between neighbouring 5.1 speakers; wraps at +-4.
    {
        auto g = panGains(0.0f);
        check(g[kC] == 1.0f && g[kFL] == 0.0f && g[kFR] == 0.0f, "pan 0 = centre");
        g = panGains(-1.0f);
        check(g[kFL] == 1.0f && near(g[kC], 0, 1e-7), "pan -1 = front left");
        g = panGains(0.5f);
        check(near(g[kC], 0.70710678, 1e-6) && near(g[kFR], 0.70710678, 1e-6), "pan 0.5 = centre / right");
        g = panGains(2.0f);
        check(near(g[kFR], 0.70710678, 1e-6) && near(g[kRR], 0.70710678, 1e-6), "pan 2 = right / rear right");
        g = panGains(-3.0f);
        check(g[kRL] == 1.0f, "pan -3 = rear left");
        check(near(wrapPan(4.5f), -3.5f, 1e-6) && near(wrapPan(-4.25f), 3.75f, 1e-6) && wrapPan(1.0f) == 1.0f, "pan wraps mod 8");
        const auto sg = sendGains(panGains(-1.0f), 0.5f, true);
        check(sg[0] == 0.5f && sg[1] == 0.0f && near(sg[2], 0.25, 1e-7) && sg[3] == 0.5f && sg[4] == 0.0f, "reverb send fold");
        const auto sp = sendGains(panGains(-1.0f), 0.5f, false);
        check(sp[2] == 0.0f && sp[3] == 0.0f, "plain send has no fold");
        float fl = 1, fr = 0, c = 1, lfe = 1, rl = 0, rr = 1, st[2];
        const float* ch[6] = {&fl, &fr, &c, &lfe, &rl, &rr};
        downmix51(ch, st, 1);
        check(near(st[0], 1.70710678, 1e-6) && near(st[1], 1.41421356, 1e-6), "stereo fold");
    }
    // Limiter: a full-scale square settles at the -2 dB threshold.
    {
        Limiter l;
        l.configure(1, 50, -2, 48000);
        std::vector<float> sq(2 * 4800, 1.0f);
        l.process(sq.data(), 4800, 2);
        check(sq[0] < 1.0f && near(sq[2 * 4799], dbToLin(-2), 0.02), "limiter settles at -2 dB");
        std::vector<float> q(2 * 48000, 0.1f);
        l.process(q.data(), 48000, 2);
        check(near(l.divisor(), 1.0, 1e-6) && q[2 * 47999] == 0.1f, "limiter releases in 50 ms");
    }
    // A voice: rate x 0.9975, pan, volume clamp, length, fader, release.
    {
        auto pcm = std::make_shared<SfxPcm>();
        pcm->rate = 44100;
        pcm->samples.assign(44100, 1.0f);
        SfxPlayer pl;
        SfxPlayer::Params p;
        p.gain = 0.5f, p.pan = -1.0f;
        const auto id = pl.play(pcm, p);
        std::vector<float> out(2 * 512);
        pl.render(out.data(), 512);
        check(out[0] == 0.0f && near(out[2 * 300], 0.5, 1e-6) && out[2 * 300 + 1] == 0.0f, "voice: attack, volume, pan -1");
        // 44100 frames at int(44100 x 0.9975) = 43989 Hz last 48120.3 output frames.
        std::vector<float> rest(2 * 47616);
        pl.render(rest.data(), 47616);  // up to frame 48128 (188 passes)
        check(pl.playing(id) == false, "voice: ends after 48120 frames");
        const auto id2 = pl.play(pcm, p);
        pl.render(out.data(), 256);  // 48128 .. 48384: the pass where frame 48120 fell is done
        check(pl.playing(id2), "voice: plays on");
        p.gain = 4.0f;
        pl.stopAll();
        pl.render(out.data(), 512);
        check(pl.voices() == 0, "stop releases (5 ms)");
        pl.play(pcm, p);
        pl.render(out.data(), 512);
        check(near(out[2 * 400], 1.0, 1e-6), "volume clamps to 1");
        pl.setMasterDb(-6.0f);
        pl.render(out.data(), 256);
        check(near(out[0], 1.0, 1e-6), "the fader multiplies before the clamp");
        pl.setMasterDb(-96.0f);
        pl.render(out.data(), 256);
        check(out[0] == 0.0f, "set_master_vol kDbSilence mutes");
    }
    // A send: the voice's volume 0 (dry silent), the send at full: the
    // reverb's front comes back on FL / FR, the centre voice (half of each
    // front) on C, the rear voice (front folded in) on RL / RR; nothing on LFE.
    {
        I3DL2Params plate{};
        check(i3dl2Preset("plate", plate) && plate.decayTime > 1.2f && plate.decayTime < 1.4f, "plate preset");
        auto pcm = std::make_shared<SfxPcm>();
        pcm->rate = 48000;
        pcm->samples.assign(4800, 0.0f);
        pcm->samples[0] = 1.0f;
        SfxPlayer pl;
        SfxPlayer::Params p;
        p.gain = 0, p.pan = -1, p.attack = 0;
        p.send = pl.reverbBus(plate);
        auto run = [&](float sendVolume) {
            p.sendVolume = sendVolume;
            SfxPlayer fresh;
            p.send = fresh.reverbBus(plate);
            fresh.play(pcm, p);
            std::vector<float> st(2 * 48000), m;
            fresh.render(st.data(), 48000, &m);
            std::array<double, 6> e{};
            for (int c = 0; c < 6; ++c)
                for (int i = 0; i < 48000; ++i) e[size_t(c)] += double(m[size_t(c) * 48000 + size_t(i)]) * m[size_t(c) * 48000 + size_t(i)];
            return e;
        };
        const auto e1 = run(1.0f), e2 = run(0.5f);
        check(e1[kFL] > 1e-4 && e1[kFR] > 1e-6 && e1[kC] > 1e-6 && e1[kRL] > 1e-4 && e1[kLFE] == 0.0, "send reaches front, centre, rears");
        check(near(e2[kFL] / e1[kFL], 0.25, 1e-3) && near(e2[kRL] / e1[kRL], 0.25, 1e-3), "send energy follows send_volume^2");
        check(near(e1[kRL], e1[kFL], e1[kFL] * 1e-3), "rear voice = front voice (pan -1: RL gets FL folded)");
    }
    // XAudio's reverb (audio/xaudio_reverb.h), checked bit-exact against the
    // original PowerPC code: an impulse's energy per output channel over 1 s.
    {
        struct Vec {
            const char* env;
            int ch;
            double fl, fr, sl, sr;
        };
        const Vec vecs[] = {{"plate", 2, 0.0129503979, 0.00573494756, 0.00416357051, 0.00216193242},
                            {"plate", 1, 0.0189575645, 0.0191079021, 0.00646199684, 0.00627867755},
                            {"small_room", 2, 0.0106911439, 0.00382208813, 0.0078954201, 0.00297681728},
                            {"small_room", 1, 0.0147344038, 0.0141741088, 0.0109683839, 0.00966638129}};
        for (const Vec& v : vecs) {
            I3DL2Params pr{};
            i3dl2Preset(v.env, pr);
            XAudioReverb r(v.ch);
            r.setI3DL2(pr);
            std::vector<float> in(size_t(48000 * v.ch), 0.0f), out(48000 * 6);
            in[0] = 1.0f;
            r.process6(in.data(), out.data(), 48000);
            double e[6] = {};
            int first = -1;
            for (int i = 0; i < 48000; ++i)
                for (int c = 0; c < 6; ++c) {
                    const double y = out[size_t(i) * 6 + size_t(c)];
                    e[c] += y * y;
                    if (y != 0 && first < 0) first = i;
                }
            auto rel = [](double a, double b) { return std::fabs(a - b) <= 1e-8 * b; };
            check(rel(e[0], v.fl) && rel(e[1], v.fr) && rel(e[4], v.sl) && rel(e[5], v.sr) && e[2] == 0 && e[3] == 0 && first == 303,
                  std::string("reverb ") + v.env + (v.ch == 2 ? " stereo" : " mono") + " impulse energy");
        }
    }
    // RandomGroupSeq: 1 -> uniform pick; n -> n distinct in order; 0 -> none.
    {
        std::minstd_rand rng(7);
        auto r01 = [&] { return float(rng() & 0xffff) * 1.5258789e-05f; };
        auto ri = [&](int lo, int hi) { return lo + int((rng() & 0xffff) % uint32_t(hi - lo)); };
        std::vector<int> hits(5, 0);
        bool shape = true;
        for (int t = 0; t < 20000; ++t) {
            const auto v = Synth::randomGroupPick(5, 2, r01, ri);
            shape = shape && v.size() == 2 && v[0] < v[1];
            for (int k : v) ++hits[size_t(k)];
        }
        bool even = true;
        for (int h : hits) even = even && std::abs(h - 8000) < 400;
        check(shape && even, "num_simul 2 of 5: two distinct, each 2/5 of the time");
        check(Synth::randomGroupPick(5, 0, r01, ri).empty() && Synth::randomGroupPick(3, 9, r01, ri) == std::vector<int>({0, 1, 2}) &&
                  Synth::randomGroupPick(4, 1, r01, ri).size() == 1,
              "num_simul 0 / beyond / 1");
    }
    std::printf("sfxtest: voice DSP: %d checks (envelope, 5.1 pan, sends, reverb routing, XAudio reverb vectors, stereo fold, limiter, voice rate, fader, RandomGroupSeq)\n", checks);
}

}  // namespace

int runSfxTest(const std::string& dataPath) {
    try {
        GameData data(dataPath);
        if (!xma::available()) {
            std::printf("sfxtest: FAIL: no XMA decoder (FFmpeg without xma2, or GD02_VIDEO OFF)\n");
            return 1;
        }
        const char* dump = SDL_getenv("GD02_SFX_DUMP");
        std::vector<std::string> paths;
        for (const auto& p : data.paths())
            if (p.size() > 10 && p.compare(p.size() - 10, 10, ".milo_xbox") == 0) paths.push_back(p);
        std::sort(paths.begin(), paths.end());

        int failures = 0, reverbs = 0, sends = 0, samples = 0, exact = 0, loops = 0, sfx = 0, seqs = 0, refs = 0, unresolved = 0, banks = 0;
        std::map<uint32_t, int> rates;
        double seconds = 0, decodeMs = 0, peakMax = 0, rmsMin = 1e9, rmsMax = 0;
        uint64_t clipped = 0;
        auto fail = [&](const std::string& what) {
            if (failures++ < 20) std::printf("  FAIL %s\n", what.c_str());
        };
        dspChecks(fail);
        for (const auto& p : paths) {
            std::unique_ptr<Milo> milo;
            try {
                milo = std::make_unique<Milo>(data.read(p), p);
            } catch (const std::exception& e) {
                continue;  // --scenetest reports broken milos
            }
            bool bank = false;
            milo->root().forEach([&](Object& o) {
                if (auto* fx = o.as<FxSendReverb>()) {
                    ++reverbs;
                    I3DL2Params pr{};
                    if (!o.decoded) return fail(p + ": " + o.name + ": " + o.error);
                    if (!i3dl2Preset(fx->environment, pr)) fail(p + ": " + o.name + ": unknown environment " + fx->environment);
                    return;
                }
                if (!o.as<SynthSample>() && !o.as<Sfx>() && !o.as<SequenceObj>()) return;
                bank = true;
                if (!o.decoded) return fail(p + ": " + o.name + ": " + o.error);
                auto resolves = [&](const std::string& n, bool sample) {
                    if (n.empty()) return;  // an empty ObjPtr (song_default_bank's SfxSeqs): plays nothing
                    ++refs;
                    const Object* t = o.dir ? o.dir->resolve(n) : nullptr;
                    const bool ok = t && (sample ? t->as<SynthSample>() != nullptr : (t->as<Sfx>() || t->as<SequenceObj>()));
                    if (!ok) ++unresolved, fail(p + ": " + o.name + " -> " + n + " unresolved");
                };
                if (auto* x = o.as<Sfx>()) {
                    ++sfx;
                    for (const auto& m : x->maps) resolves(m.sample, true);
                    if (!x->send.empty()) {
                        const Object* t = o.dir ? o.dir->resolve(x->send) : nullptr;
                        if (t && t->as<FxSendReverb>()) ++sends;
                        else fail(p + ": " + o.name + " send " + x->send + " is not an FxSendReverb");
                    }
                } else if (auto* g = o.as<GroupSeq>()) {
                    ++seqs;
                    for (const auto& c : g->children) resolves(c, false);
                } else if (auto* s1 = o.as<SfxSeq>()) {
                    ++seqs;
                    resolves(s1->child, false);
                } else if (o.as<WaitSeq>()) {
                    ++seqs;
                }
                auto* s = o.as<SynthSample>();
                if (!s) return;
                ++samples;
                ++rates[s->sample.rate];
                const SampleData& d = s->sample;
                if (d.format != SampleData::kXma || !d.hasData || d.size % 2048 != 0)
                    return fail(p + ": " + s->name + ": format " + std::to_string(d.format) + " size " + std::to_string(d.size));
                std::vector<float> pcm;
                std::string err;
                uint32_t got = 0;
                const uint64_t t0 = SDL_GetTicksNS();
                const bool ok = xma::decode(s->data(), d.size, int(d.rate), 1, d.sampleCount, pcm, err, &got);
                decodeMs += double(SDL_GetTicksNS() - t0) / 1e6;
                if (!ok) return fail(p + ": " + s->name + ": " + err);
                if (got != d.sampleCount)
                    return fail(p + ": " + s->name + ": decoded " + std::to_string(got) + " frames, SampleData says " +
                                std::to_string(d.sampleCount));
                ++exact;
                seconds += double(d.sampleCount) / double(d.rate);
                if (s->looping) {
                    ++loops;
                    const int64_t end = s->loopEnd < 0 ? int64_t(d.sampleCount) : int64_t(s->loopEnd);
                    if (s->loopStart < 0 || s->loopStart >= end || end > int64_t(d.sampleCount))
                        fail(p + ": " + s->name + ": loop " + std::to_string(s->loopStart) + ".." + std::to_string(s->loopEnd) +
                             " outside " + std::to_string(d.sampleCount));
                }
                double sum = 0, peak = 0;
                for (float x : pcm) {
                    sum += double(x) * x;
                    peak = std::max(peak, double(std::fabs(x)));
                    if (std::fabs(x) >= 1.0f) ++clipped;
                }
                const double rms = pcm.empty() ? 0 : std::sqrt(sum / double(pcm.size()));
                peakMax = std::max(peakMax, peak), rmsMin = std::min(rmsMin, rms), rmsMax = std::max(rmsMax, rms);
                if (rms < 1e-3) fail(p + ": " + s->name + ": silent (rms " + std::to_string(rms) + ")");
                if (peak > 1.5) fail(p + ": " + s->name + ": peak " + std::to_string(peak) + " (broken decode?)");
                if (dump) writeWav(std::string(dump) + "/" + safe(p.substr(0, p.size() - 10)) + "__" + safe(s->name), pcm, int(d.rate));
            });
            banks += bank;
        }
        std::string rateList;
        for (const auto& [r, n] : rates) rateList += (rateList.empty() ? "" : ", ") + std::to_string(r) + " Hz " + std::to_string(n);
        std::printf("sfxtest: %d milos with sounds: %d SynthSamples (%s), %d decoded to exactly their sample count, "
                    "%.1f s of audio in %.0f ms, %d looping\n",
                    banks, samples, rateList.c_str(), exact, seconds, decodeMs, loops);
        std::printf("sfxtest: peak %.3f, rms %.3f..%.3f, %llu samples at full scale; %d Sfx, %d sequences, %d references, %d unresolved\n",
                    peakMax, rmsMin, rmsMax, (unsigned long long)clipped, sfx, seqs, refs, unresolved);
        std::printf("sfxtest: %d FxSendReverbs (environments known), %d Sfx send to one\n", reverbs, sends);
        if (reverbs == 0 || sends == 0) fail("no reverb sends found");

        // The game's banks and the names the scripts play.
        Synth synth;
        for (const auto& pr : synth.loadBanks(data)) fail(pr);
        std::string bankList;
        for (const auto& [k, d] : synth.banks()) bankList += (bankList.empty() ? "" : ", ") + k;
        int found = 0;
        for (const char* n : kScriptSounds) {
            if (synth.find(n)) ++found;
            else fail(std::string("no bank has ") + n);
        }
        const int before = synth.stats().voices;
        for (const char* n : kScriptSounds) synth.play(n);
        const int scriptVoices = synth.stats().voices - before;
        if (scriptVoices < int(std::size(kScriptSounds))) fail("the scripts' sounds started only " + std::to_string(scriptVoices) + " voices");
        // Each venue's bank: the crowd cues and reactions.
        int venueFound = 0, venueMissing = 0;
        std::set<std::string> silentReactions;
        for (const auto& v : venues::names()) {
            std::unique_ptr<Milo> bank;
            try {
                bank = loadMilo(data, "world/" + v + "/gen/" + v + "_bank.milo_xbox");
            } catch (const std::exception& e) {
                fail(v + " bank: " + e.what());
                continue;
            }
            std::vector<std::string> names(std::begin(kVenueSounds), std::end(kVenueSounds));
            for (const char* n : {"crowd_upto_poor", "crowd_upto_norm", "crowd_upto_good", "crowd_dnto_danger", "crowd_dnto_poor", "crowd_dnto_norm"})
                names.push_back(n);
            for (const auto& n : names) {
                const Object* o = bank->find(n);
                if (o && synth.play(*o)) ++venueFound;
                else ++venueMissing, fail(v + " bank: no " + n);
            }
            // Level changes whose reaction the banks lack: silent in GH2 too (a null ObjPtr).
            for (int a = 0; a < 5; ++a)
                for (int b = 0; b < 5; ++b)
                    if (a != b && !bank->find(CrowdAudio::reaction(a, b))) silentReactions.insert(CrowdAudio::reaction(a, b));
            synth.stopAll();
            synth.forget(bank->root());
        }
        const Synth::Stats& st = synth.stats();
        std::string silent;
        for (const auto& n : silentReactions) silent += " " + n;
        std::printf("sfxtest: banks [%s]: %d / %zu script sounds found, %d voices started; venue banks: %d cues played, %d missing "
                    "(silent reactions:%s); synth decoded %d samples (%d failures, %d unresolved)\n",
                    bankList.c_str(), found, std::size(kScriptSounds), scriptVoices, venueFound, venueMissing, silent.c_str(), st.decoded,
                    st.decodeFailures, st.unresolved);
        if (st.decodeFailures || st.unresolved) fail("synth decode failures / unresolved references");
        if (samples == 0 || exact != samples) fail("not every sample decoded exactly");
        std::printf("sfxtest: %s\n", failures ? "FAIL" : "OK");
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::printf("sfxtest: FAIL: %s\n", e.what());
        return 1;
    }
}
