// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/crowd_audio.h"

#include <algorithm>
#include <cmath>

#include "audio/vorbis_stream.h"
#include "data/dta.h"
#include "data/game_data.h"

namespace {

constexpr int kRate = 44100;
constexpr float kFadeSeconds = 0.25f;  // [I] replacing a stream fades instead of cutting
constexpr double kCrowdAudioDelay = 1.0;  // config/sound.dta (crowd_audio_delay 1) [C data]

const char* const kLevelNames[] = {"kExcitementBoot", "kExcitementBad", "kExcitementOkay", "kExcitementGreat", "kExcitementPeak"};

// (name dB file (pan n) (loop_ms a b)) -> Entry.
CrowdAudio::Entry entryOf(const DataNode& e) {
    CrowdAudio::Entry r;
    if (e.children.size() < 3) return r;
    r.db = e.floatAt(1);
    r.file = e.textAt(2);
    if (const DataNode* l = e.find("loop_ms"); l && l->children.size() >= 3) r.loopStartMs = l->intAt(1), r.loopEndMs = l->intAt(2);
    return r;
}

}  // namespace

CrowdAudio::CrowdAudio(const GameData& data, const std::string& venue, const DataNode* sound) : venue_(venue) {
    if (!sound) return;
    if (const DataNode* b = sound->find("bank"); b && b->children.size() > 1) bank_ = b->textAt(1);
    const DataNode* crowd = sound->find("crowd");
    if (!crowd) return;
    if (const DataNode* i = crowd->find("intro")) entries_[kIntro] = entryOf(*i);
    if (const DataNode* i = crowd->find("encore_intro")) entries_[kEncoreIntro] = entryOf(*i);
    if (const DataNode* i = crowd->find("encore_outro")) entries_[kEncoreOutro] = entryOf(*i);
    if (const DataNode* lv = crowd->find("levels"))
        for (size_t k = 1; k < lv->children.size(); ++k) {
            const DataNode& e = lv->children[k];
            if (!e.isList() || e.children.empty()) continue;
            for (int n = 0; n < 5; ++n)
                if ((e.children[0].isText() && e.children[0].s == kLevelNames[n]) ||
                    (e.children[0].type == DataNode::Type::Int && e.children[0].i == n))
                    entries_[size_t(kBoot + n)] = entryOf(e);
        }
    // "world/%s/streams" (0x8204958c) + the file. Read here (GameData is not
    // shared with threads); decoded on the worker.
    for (size_t s = 0; s < kStreams; ++s) {
        if (entries_[s].file.empty()) continue;
        const std::string path = "world/" + venue + "/streams/" + entries_[s].file + ".mogg";
        try {
            files_[s] = data.read(path);
        } catch (const std::exception& e) {
            SDL_Log("crowd audio: %s: %s", path.c_str(), e.what());
        }
    }
    worker_ = std::thread([this] { decodeAll(); });
}

CrowdAudio::~CrowdAudio() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    if (worker_.joinable()) worker_.join();
}

void CrowdAudio::decodeAll() {
    for (size_t s = 0; s < kStreams; ++s) {
        if (files_[s].empty()) continue;
        // The same file twice (Peak reuses Great's): decode once.
        bool dup = false;
        for (size_t o = 0; o < s; ++o)
            if (entries_[o].file == entries_[s].file && pcm_[o].ok) {
                pcm_[s] = pcm_[o];
                dup = true;
                break;
            }
        if (dup) continue;
        try {
            VorbisStream v(std::move(files_[s]));
            const int ch = v.channels();
            Pcm p;
            p.rate = v.sampleRate();
            p.frames.reserve(size_t(v.lengthFrames()) * 2);
            std::vector<std::vector<float>> buf(size_t(ch), std::vector<float>(4096));
            std::vector<float*> ptrs(static_cast<size_t>(ch), nullptr);
            for (int c = 0; c < ch; ++c) ptrs[size_t(c)] = buf[size_t(c)].data();
            for (;;) {
                const int n = v.decode(ptrs.data(), 4096);
                if (n <= 0) break;
                for (int i = 0; i < n; ++i) {
                    const float l = buf[0][size_t(i)], r = ch > 1 ? buf[1][size_t(i)] : l;
                    p.frames.push_back(l);
                    p.frames.push_back(r);
                }
                if (n < 4096) break;
            }
            p.ok = !p.frames.empty();
            pcm_[s] = std::move(p);
        } catch (const std::exception& e) {
            SDL_Log("crowd audio: %s: %s", entries_[s].file.c_str(), e.what());
        }
        files_[s] = Bytes();
    }
    decoded_.store(true);
}

int CrowdAudio::decodedStreams() {
    if (worker_.joinable()) worker_.join();
    int n = 0;
    for (const Pcm& p : pcm_) n += p.ok ? 1 : 0;
    return n;
}

bool CrowdAudio::open() {
    if (stream_) return true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &CrowdAudio::callback, this);
    if (!stream_) {
        SDL_Log("crowd audio: no audio device: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void SDLCALL CrowdAudio::callback(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<CrowdAudio*>(user);
    const int frames = additional / int(sizeof(float) * 2);
    if (frames <= 0) return;
    self->scratch_.assign(size_t(frames) * 2, 0.0f);
    self->mix(self->scratch_.data(), frames);
    SDL_PutAudioStreamData(stream, self->scratch_.data(), frames * int(sizeof(float) * 2));
}

// Mixes the voices (linear resampling; a fade of kFadeSeconds between gains).
void CrowdAudio::mix(float* out, int frames) {
    std::lock_guard<std::mutex> g(lock_);
    if (paused_ || !decoded_.load()) return;
    const float vol = volume_.load();
    const float step = 1.0f / (kFadeSeconds * kRate);
    for (Voice& v : voices_) {
        if (v.stream < 0) continue;
        const Pcm& p = pcm_[size_t(v.stream)];
        if (!p.ok) {
            v.stream = -1;
            continue;
        }
        const double total = double(p.frames.size() / 2);
        const double inc = double(p.rate) / kRate;
        const double loopStart = v.loopStart * p.rate / 1000.0, loopEnd = v.loopEnd * p.rate / 1000.0;
        for (int i = 0; i < frames; ++i) {
            if (v.gain < v.target) v.gain = std::min(v.target, v.gain + step);
            else if (v.gain > v.target) v.gain = std::max(v.target, v.gain - step);
            if (v.loop && loopEnd > loopStart && v.pos >= loopEnd) v.pos = loopStart + std::fmod(v.pos - loopEnd, loopEnd - loopStart);
            if (v.loop && v.pos >= total - 1) v.pos = loopEnd > loopStart ? loopStart : 0;
            if (v.pos >= total - 1 || (v.gain <= 0 && v.target <= 0)) {
                v.stream = -1;
                break;
            }
            const size_t k = size_t(v.pos);
            const float t = float(v.pos - double(k));
            const float l = p.frames[k * 2] + (p.frames[k * 2 + 2] - p.frames[k * 2]) * t;
            const float r = p.frames[k * 2 + 1] + (p.frames[k * 2 + 3] - p.frames[k * 2 + 1]) * t;
            out[i * 2] += l * v.gain * vol;
            out[i * 2 + 1] += r * v.gain * vol;
            v.pos += inc;
        }
    }
}

// The stream replaces what plays (0x822DFC10 [I: fade]); dB from the dta.
void CrowdAudio::play(Stream s, bool loop) {
    pendingAt_ = -1;
    const Entry& e = entries_[size_t(s)];
    if (!enabled_ || e.file.empty()) return;
    std::lock_guard<std::mutex> g(lock_);
    // The current voice fades out in the second slot.
    if (voices_[0].stream >= 0) {
        voices_[1] = voices_[0];
        voices_[1].target = 0;
    }
    Voice v;
    v.stream = int(s);
    v.loop = loop;
    v.target = std::pow(10.0f, e.db / 20.0f);
    v.gain = voices_[1].stream >= 0 ? 0 : v.target;
    if (e.loopStartMs >= 0 && e.loopEndMs > e.loopStartMs) v.loopStart = e.loopStartMs, v.loopEnd = e.loopEndMs;
    voices_[0] = v;
    playing_.store(int(s));
}

void CrowdAudio::introStart(bool fast, bool encore) {
    started_ = 0;
    play(encore && !fast ? kEncoreIntro : kIntro, false);
}

void CrowdAudio::musicStart(int excitement, double now) {
    started_ = 1;
    level_ = 2;
    this->excitement(excitement, now);
}

void CrowdAudio::excitement(int level, double now) {
    if (level < 0 || level > 4) return;
    level_ = level;
    if (!started_ || !enabled_) return;
    pendingAt_ = now + kCrowdAudioDelay;
}

void CrowdAudio::update(double now) {
    if (pendingAt_ >= 0 && now >= pendingAt_) play(Stream(kBoot + level_), true);
}

void CrowdAudio::encoreOutro() { play(kEncoreOutro, true); }

void CrowdAudio::turnOff() {
    pendingAt_ = -1;
    started_ = 0;
    std::lock_guard<std::mutex> g(lock_);
    for (Voice& v : voices_) v.target = 0;
    playing_.store(kStreams);
}

void CrowdAudio::setPaused(bool paused) {
    std::lock_guard<std::mutex> g(lock_);
    paused_ = paused;
}

void CrowdAudio::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    if (!on) turnOff();
}

std::string CrowdAudio::reaction(int prev, int level) {
    static const char* const kTo[] = {"danger", "poor", "norm", "good", "good"};
    if (level < 0 || level > 4) return {};
    return std::string(prev <= level ? "crowd_upto_" : "crowd_dnto_") + kTo[level];
}
