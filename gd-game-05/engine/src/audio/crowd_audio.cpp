// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/crowd_audio.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "audio/audio_device.h"
#include "audio/vorbis_stream.h"
#include "data/dta.h"
#include "data/game_data.h"

namespace {

constexpr int kRate = 44100;
constexpr double kCrowdAudioDelay = 1.0;  // config/sound.dta (crowd_audio_delay 1) [C data]; +0x34

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

// GD05_CROWDSTREAMS_LEGACY=1: no crowd streams in Rock Band (before GD05-115):
// the venues' WorldDir types have no GH2 (sound (crowd ...)), so the crowd
// was silent until the song's own crowd channels and reactions.
bool crowdStreamsLegacy() {
    static const bool on = [] {
        const char* e = SDL_getenv("GD05_CROWDSTREAMS_LEGACY");
        return e && *e == '1';
    }();
    return on;
}

}  // namespace

CrowdAudio::CrowdAudio(const GameData& data, const std::string& venue, const DataNode* sound) : venue_(venue) {
    if (sound)
        if (const DataNode* b = sound->find("bank"); b && b->children.size() > 1) bank_ = b->textAt(1);
    const DataNode* crowd = sound ? sound->find("crowd") : nullptr;
    std::string root = "world/" + venue + "/streams";
    // GD05-115: Rock Band's CrowdAudio (ctor 0x8243B660) [C]: the type is
    // the venue's name up to its last '_' (0x8241F3E8: big_club_11 ->
    // big_club), from config/crowd_audio.dta's (CrowdAudio (types (<type>
    // (streams (root world/<type>/streams) (intro dB file) (levels ...))))),
    // the volume config/sound.dta's crowd_volume (-4 dB, +0x30). Its
    // play_intro (0x8243A2E8, intro_start: game.dta pick_intro, before the
    // count-in) plays the intro stream: the crowd cheers from the venue's
    // first shot, not only once the song's crowd channels start.
    DataNode rbConfig, rbSound;
    if (!crowd && !crowdStreamsLegacy() && data.exists("config/gen/crowd_audio.dtb")) {
        try {
            rbConfig = parseDtb(data.read("config/gen/crowd_audio.dtb"));
            if (data.exists("config/gen/sound.dtb")) {
                rbSound = parseDtb(data.read("config/gen/sound.dtb"));
                if (const DataNode* v = rbSound.find("crowd_volume"); v && v->children.size() > 1) crowdVolumeDb_ = v->floatAt(1);
            }
        } catch (const std::exception& e) {
            SDL_Log("crowd audio: config/gen/crowd_audio.dtb: %s", e.what());
        }
        const size_t us = venue.rfind('_');
        const std::string type = us == std::string::npos ? venue : venue.substr(0, us);
        const DataNode* ca = rbConfig.find("CrowdAudio");
        const DataNode* types = ca ? ca->find("types") : nullptr;
        const DataNode* t = types ? types->find(type) : nullptr;
        const DataNode* dis = t ? t->find("disable") : nullptr;
        const bool disabled = dis && dis->children.size() > 1 && (dis->children[1].isText() ? dis->children[1].s == "TRUE" : dis->intAt(1) != 0);
        if (t && !disabled) crowd = t->find("streams");
        if (crowd)
            if (const DataNode* r = crowd->find("root"); r && r->children.size() > 1) root = r->textAt(1);
        static const bool log = [] { const char* l = SDL_getenv("GD05_SFXLOG"); return l && *l && *l != '0'; }();
        if (log) SDL_Log("crowd audio: %s: type %s, %s, crowd_volume %.1f dB", venue.c_str(), type.c_str(), crowd ? root.c_str() : "no streams", double(crowdVolumeDb_));
    }
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
        const std::string path = root + "/" + entries_[s].file + ".mogg";
        try {
            files_[s] = data.read(path);
        } catch (const std::exception& e) {
            SDL_Log("crowd audio: %s: %s", path.c_str(), e.what());
        }
    }
    worker_ = std::thread([this] { decodeAll(); });
}

CrowdAudio::~CrowdAudio() {
    if (stream_) audiodev::closeStream(stream_);
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
    stream_ = audiodev::openStream(spec, &CrowdAudio::callback, this);
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

// Mixes the stream (linear resampling).
void CrowdAudio::mix(float* out, int frames) {
    std::lock_guard<std::mutex> g(lock_);
    if (paused_ || !decoded_.load()) return;
    Voice& v = voice_;
    if (v.stream < 0) return;
    const Pcm& p = pcm_[size_t(v.stream)];
    if (!p.ok) {
        v.stream = -1;
        return;
    }
    const double total = double(p.frames.size() / 2);
    const double inc = double(p.rate) / kRate;
    const double loopStart = v.loopStart * p.rate / 1000.0, loopEnd = v.loopEnd * p.rate / 1000.0;
    for (int i = 0; i < frames; ++i) {
        if (v.loop && loopEnd > loopStart && v.pos >= loopEnd) v.pos = loopStart + std::fmod(v.pos - loopEnd, loopEnd - loopStart);
        if (v.loop && v.pos >= total - 1) v.pos = loopEnd > loopStart ? loopStart : 0;
        if (v.pos >= total - 1) {
            v.stream = -1;
            playing_.store(kStreams);
            break;
        }
        const size_t k = size_t(v.pos);
        const float t = float(v.pos - double(k));
        const float l = p.frames[k * 2] + (p.frames[k * 2 + 2] - p.frames[k * 2]) * t;
        const float r = p.frames[k * 2 + 1] + (p.frames[k * 2 + 3] - p.frames[k * 2 + 1]) * t;
        out[i * 2] += l * v.gain;
        out[i * 2 + 1] += r * v.gain;
        v.pos += inc;
    }
}

// 0x822DF2F8: fx volume + crowd_volume + set_volume + the entry's dB.
float CrowdAudio::gainOf(Stream s) const {
    return std::pow(10.0f, (fxDb_ + crowdVolumeDb_ + volumeDb_ + entries_[size_t(s)].db) / 20.0f);
}

// 0x822DF860 [C]: while enabled, the old stream is deleted and the new one
// starts (no cross-fade).
void CrowdAudio::play(Stream s, bool loop) {
    const Entry& e = entries_[size_t(s)];
    if (!enabled_) return;
    std::lock_guard<std::mutex> g(lock_);
    voice_ = Voice();
    playing_.store(kStreams);
    if (e.file.empty()) return;
    // Development aid: GD05_SFXLOG=1 also logs the crowd's streams.
    static const bool log = [] { const char* l = SDL_getenv("GD05_SFXLOG"); return l && *l && *l != '0'; }();
    if (log) SDL_Log("crowd audio: %s plays %s%s", venue_.c_str(), e.file.c_str(), loop ? " (loop)" : "");
    voice_.stream = int(s);
    voice_.loop = loop;
    voice_.gain = gainOf(s);
    if (e.loopStartMs >= 0 && e.loopEndMs > e.loopStartMs) voice_.loopStart = e.loopStartMs, voice_.loopEnd = e.loopEndMs;
    playing_.store(int(s));
}

void CrowdAudio::introStart(bool fast, bool encore) { play(encore && !fast ? kEncoreIntro : kIntro, false); }

std::string CrowdAudio::musicStart(int excitement, double now) {
    started_ = 1;
    level_ = 2;
    return this->excitement(excitement, now);
}

// 0x822DF590 [C].
std::string CrowdAudio::excitement(int level, double now) {
    if (!started_ || !enabled_ || level < 0 || level > 4) return {};
    const std::string r = reaction(level_, level);
    level_ = level;
    pending_.push_back({now + kCrowdAudioDelay, level});
    return r;
}

void CrowdAudio::update(double now) {
    // excitement_loop (0x822DFD40): the level's loop, in message order.
    while (!pending_.empty() && now >= pending_.front().first) {
        const int level = pending_.front().second;
        pending_.erase(pending_.begin());
        play(Stream(kBoot + level), true);
        static const bool log = [] { const char* l = SDL_getenv("GD05_SFXLOG"); return l && *l && *l != '0'; }();
        if (log) SDL_Log("crowd timing: excitement_loop %d at song %.0f ms, stream %d", level, now * 1000.0, playing_.load());
    }
}

void CrowdAudio::encoreOutro() { play(kEncoreOutro, true); }

// 0x822DF0E8 [C]: only the stream goes (started, the level and the pending
// excitement_loop messages stay).
void CrowdAudio::turnOff() {
    std::lock_guard<std::mutex> g(lock_);
    voice_ = Voice();
    playing_.store(kStreams);
}

void CrowdAudio::setPaused(bool paused) {
    std::lock_guard<std::mutex> g(lock_);
    paused_ = paused;
}

// 0x822DF9D8 [C].
std::string CrowdAudio::setEnabled(bool on, double now) {
    enabled_ = on;
    if (!on) {
        turnOff();
        return {};
    }
    return excitement(level_, now);
}

// set_volume (0x822DFD98 -> 0x822DF2F8): applied to the stream at once.
void CrowdAudio::setVolumeDb(float db) {
    volumeDb_ = db;
    std::lock_guard<std::mutex> g(lock_);
    if (voice_.stream >= 0) voice_.gain = gainOf(Stream(voice_.stream));
}

std::string CrowdAudio::reaction(int prev, int level) {
    // 0x822DFAB0 [C]: crowd_%s_%s of dnto / upto and the levels.
    static const char* const kTo[] = {"danger", "poor", "norm", "good", "peak"};
    if (level < 0 || level > 4) return {};
    return std::string(prev <= level ? "crowd_upto_" : "crowd_dnto_") + kTo[level];
}
