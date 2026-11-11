// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/sfx.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

#include "audio/audio_device.h"
#include "audio/sound_buss.h"
#include "audio/xma_decoder.h"
#include "audio/xma_stream.h"
#include "data/disc.h"
#include "data/fsb.h"
#include "data/pak.h"
#include "data/qbkey.h"

namespace {
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
}  // namespace

Sfx::~Sfx() { close(); }

bool Sfx::open(std::string& error) {
    // Its own logical device, or the gd-engine-core output (audiodev::openFx).
    device_ = audiodev::openFx(error);
    return device_;
}

void Sfx::close() {
    stopAll();
    for (auto& j : jobs_)
        if (j->thread.joinable()) j->thread.join();
    jobs_.clear();
    if (device_) audiodev::closeFx();
    device_ = false;
}

void Sfx::addPak(const Disc& disc, const std::string& pakPath) {
    try {
        const auto pakPtr = Pak::shared(disc, pakPath);  // a zone's pak: just read by its venue
        const Pak& pak = *pakPtr;
        for (const PakEntry& e : pak.entries()) {
            if (e.type != qbkey::key(".wav")) continue;
            auto en = std::make_shared<Entry>();
            en->riff = pak.copy(e);
            entries_[e.fullName] = en;
            entries_.emplace(e.name, en);
        }
    } catch (const std::exception& ex) {
        SDL_Log("sfx: %s: %s", pakPath.c_str(), ex.what());
    }
}

void Sfx::addFsb(const Disc& disc, const std::string& fsbPath, const std::string& datPath) {
    try {
        FsbBank bank = loadFsb(disc, fsbPath, datPath);
        for (auto& [key, index] : bank.stems) {
            if (index >= bank.samples.size()) continue;
            const FsbSample& s = bank.samples[index];
            auto en = std::make_shared<Entry>();
            en->disc = &disc, en->bank = fsbPath, en->offset = s.offset, en->stored = s.stored, en->length = s.length;
            en->rate = s.rate, en->channels = s.channels, en->encrypted = bank.encrypted;
            en->xma = bank.fsb4 ? 2 : 1;
            entries_.emplace(key, en);
        }
    } catch (const std::exception& ex) {
        SDL_Log("sfx: %s: %s", fsbPath.c_str(), ex.what());
    }
}

namespace {
// A RIFF entry's XMA data (and its rate and channels into the entry's fields).
// GH3's .wav: an XMAWAVEFORMAT "fmt " chunk (XMA1). gh4's: "data" first,
// then a big-endian XMA2WAVEFORMAT "XMA2" chunk (version 4, streams,
// loop count, loop begin / end, rate at 12, ..., the streams' channel
// bytes from 40) and a "seek" chunk [C data: global_sfx.pak's 1,017 .wav].
bool riffData(const Bytes& d, int& rate, int& channels, int& xma, const uint8_t*& data, size_t& dataSize) {
    data = nullptr, dataSize = 0;
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0) return false;
    for (size_t o = 12; o + 8 <= d.size();) {
        const uint32_t size = le32(d.data() + o + 4);
        if (o + 8 + size > d.size()) break;
        if (std::memcmp(d.data() + o, "fmt ", 4) == 0 && size >= 32) {
            const uint8_t* f = d.data() + o + 8;
            rate = int(le32(f + 12 + 4));
            channels = f[12 + 17];
            if (rate < 8000 || rate > 96000) rate = 44100;
            if (channels < 1 || channels > 2) channels = le16(f + 2) == 2 ? 2 : 1;
        } else if (std::memcmp(d.data() + o, "XMA2", 4) == 0 && size >= 44) {
            const uint8_t* f = d.data() + o + 8;
            const uint32_t r = uint32_t(f[12]) << 24 | uint32_t(f[13]) << 16 | uint32_t(f[14]) << 8 | f[15];
            rate = r >= 8000 && r <= 96000 ? int(r) : 48000;
            channels = std::clamp(int(f[40]), 1, 2);
            xma = 2;
        } else if (std::memcmp(d.data() + o, "data", 4) == 0) {
            data = d.data() + o + 8;
            dataSize = size;
        }
        o += 8 + size + (size & 1);
    }
    return data != nullptr;
}
}  // namespace

bool Sfx::startStream(Entry& e) {
    if (!xma::available()) return false;
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (e.disc) {
        size = e.stored;
    } else if (!riffData(e.riff, e.rate, e.channels, e.xma, data, size)) {
        return false;
    }
    if (size <= kStreamBytes) return false;
    auto job = std::make_shared<Job>();
    e.job = job, e.decoded = true;
    jobs_.push_back(job);
    // The worker reads its own copy (an FSB sample from the disc, decrypted).
    Bytes bytes = e.disc ? Bytes() : Bytes(data, data + size);
    const Disc* disc = e.disc;
    const std::string bank = e.bank;
    const uint64_t offset = e.offset;
    const bool encrypted = e.encrypted;
    const int rate = e.rate, channels = e.channels;
    const uint32_t length = e.length;
    const int xma = e.xma;
    Job* j = job.get();
    job->thread = std::thread([j, bytes = std::move(bytes), disc, bank, offset, size, encrypted, rate, channels, length, xma]() mutable {
        auto fail = [j] {
            std::lock_guard<std::mutex> g(j->m);
            j->bad = j->done = true;
        };
        if (disc) {
            bytes.resize(size);
            try {
                disc->readRange(bank, offset, bytes.data(), bytes.size());
            } catch (const std::exception&) {
                return fail();
            }
            if (encrypted) fsbDecrypt(bytes.data(), bytes.size(), offset);
        }
        // As xma::decodeXma1, one packet at a time [audio/xma_decoder.cpp].
        XmaStream xs;
        std::string err;
        if (!xs.open(xma, rate, channels, err)) return fail();
        std::vector<float> chunk;
        for (size_t at = 0; at < bytes.size(); at += 2048) {
            chunk.clear();
            if (!xs.feed(bytes.data() + at, std::min<size_t>(2048, bytes.size() - at), chunk, err)) return fail();
            std::lock_guard<std::mutex> g(j->m);
            j->pcm.insert(j->pcm.end(), chunk.begin(), chunk.end());
        }
        chunk.clear();
        if (!xs.flush(chunk, err)) return fail();
        std::lock_guard<std::mutex> g(j->m);
        j->pcm.insert(j->pcm.end(), chunk.begin(), chunk.end());
        if (length) j->pcm.resize(size_t(length) * size_t(channels), 0.0f);  // trimmed or padded as decodeXma1
        if (j->pcm.empty()) j->bad = true;
        j->done = true;
    });
    return true;
}

bool Sfx::decode(Entry& e) {
    e.decoded = true;
    if (e.disc) {
        Bytes data(e.stored);
        try {
            e.disc->readRange(e.bank, e.offset, data.data(), data.size());
        } catch (const std::exception&) {
            return e.bad = true, false;
        }
        if (e.encrypted) fsbDecrypt(data.data(), data.size(), e.offset);
        std::string err;
        const bool ok = e.xma == 2 ? xma::decode(data.data(), data.size(), e.rate, e.channels, e.length, e.pcm, err)
                                   : xma::decodeXma1(data.data(), data.size(), e.rate, e.channels, e.length, e.pcm, err);
        if (!ok) {
            ++stats_.failures;
            return e.bad = true, false;
        }
        ++stats_.decoded;
        return true;
    }
    // XMAWAVEFORMAT: a 12-byte header (tag 0x165, bits, options, skip,
    // streams, loop count, version), then 20-byte streams: rate at +4,
    // channels at +17 [C data: all 227 .wav].
    const uint8_t* data = nullptr;
    size_t dataSize = 0;
    if (!riffData(e.riff, e.rate, e.channels, e.xma, data, dataSize)) return e.bad = true, false;
    std::string err;
    const bool ok = e.xma == 2 ? xma::decode(data, dataSize, e.rate, e.channels, 0, e.pcm, err)
                               : xma::decodeXma1(data, dataSize, e.rate, e.channels, 0, e.pcm, err);
    if (!ok) {
        SDL_Log("sfx: decode failed: %s", err.c_str());
        ++stats_.failures;
        return e.bad = true, false;
    }
    ++stats_.decoded;
    return true;
}

uint32_t Sfx::play(uint32_t sample, float volume, float pitch, uint32_t buss, int loops) {
    // GD10_SFXLOG=1: every sound asked for (development aid).
    static const bool log = SDL_getenv("GD10_SFXLOG") != nullptr;
    if (log) SDL_Log("sfx: play %s (%s)", qbkey::name(sample).c_str(), entries_.count(sample) ? "found" : "unknown");
    auto it = entries_.find(sample);
    if (it == entries_.end()) {
        ++stats_.unknown;
        return 0;
    }
    Entry& e = *it->second;
    if (!e.decoded && !startStream(e)) decode(e);
    std::shared_ptr<Job> job = e.job;
    if (job) {
        std::lock_guard<std::mutex> g(job->m);
        if (job->bad) return 0;
    }
    if (log && !job) {
        float peak = 0;
        for (float f : e.pcm) peak = std::max(peak, std::fabs(f));
        SDL_Log("sfx:   %llu samples, %d Hz, %d ch, peak %.3f, volume %.2f%s", (unsigned long long)e.pcm.size(), e.rate, e.channels, double(peak),
                double(volume),
                device_ ? "" : ", no device");
    }
    if (!job && (e.bad || e.pcm.empty())) return 0;
    ++stats_.plays;
    const uint32_t id = nextVoice_++;
    if (!device_) return id;
    SDL_AudioSpec src{SDL_AUDIO_F32, e.channels, e.rate};
    SDL_AudioStream* s = SDL_CreateAudioStream(&src, nullptr);
    if (!s) return 0;
    // The voice's level: its own volume and pitch times its buss's (audio/sound_buss.h).
    Voice voice;
    voice.id = id, voice.stream = s, voice.sample = sample, voice.buss = buss;
    voice.bussIndex = soundBusses().index(buss);
    voice.volume = volume, voice.pitch = pitch > 0 ? pitch : 1;
    voice.loops = loops;
    applyBuss(voice);
    // Bound first: a stream made without an output format holds nothing
    // until it has one (the data put before was dropped: no sound effect
    // was ever heard).
    const bool bound = audiodev::bindFx(s);
    bool put = true;
    if (job) {
        voice.job = job;
        feed(voice);  // what is decoded so far; Sfx::update feeds the rest
    } else {
        put = SDL_PutAudioStreamData(s, e.pcm.data(), int(e.pcm.size() * sizeof(float)));
        SDL_FlushAudioStream(s);
    }
    if (!bound || !put || (!voice.job && SDL_GetAudioStreamAvailable(s) <= 0)) ++stats_.lost;
    if (log) SDL_Log("sfx:   bound %d, put %d (%s), available %d", int(bound), int(put), put ? "" : SDL_GetError(), SDL_GetAudioStreamAvailable(s));
    if (audiodev::dumpEnabled())
        SDL_Log("audiodump: sfx %s at frame %lld, wall %.0f ms, volume %.2f, buss %s %.1f dB", qbkey::name(sample).c_str(), audiodev::dumpFrames(1),
                double(SDL_GetTicksNS()) / 1e6, double(volume), qbkey::name(buss).c_str(),
                20.0 * std::log10(std::max(1e-5f, soundBusses().gainAt(voice.bussIndex))));
    voices_.push_back(voice);
    return id;
}

void Sfx::feed(Voice& v) {
    if (!v.job) return;
    std::lock_guard<std::mutex> g(v.job->m);
    const std::vector<float>& pcm = v.job->pcm;
    if (pcm.size() > v.fed) {
        SDL_PutAudioStreamData(v.stream, pcm.data() + v.fed, int((pcm.size() - v.fed) * sizeof(float)));
        v.fed = pcm.size();
    }
    if (v.job->done) {
        SDL_FlushAudioStream(v.stream);
        v.job.reset();
    }
}

bool Sfx::playing(uint32_t voice) const {
    for (const Voice& v : voices_)
        if (v.id == voice) return v.job || SDL_GetAudioStreamAvailable(v.stream) > 0;
    return false;
}

void Sfx::stop(uint32_t voice) {
    static const bool log = SDL_getenv("GD10_SFXLOG") != nullptr;
    if (log)
        for (const Voice& v : voices_)
            if (v.id == voice) SDL_Log("sfx: stop %s", qbkey::name(v.sample).c_str());
    for (Voice& v : voices_)
        if (v.id == voice) SDL_ClearAudioStream(v.stream), v.job.reset(), v.loops = 0;
}

void Sfx::stopSample(uint32_t sample) {
    static const bool log = SDL_getenv("GD10_SFXLOG") != nullptr;
    if (log) SDL_Log("sfx: stop sample %s", qbkey::name(sample).c_str());
    for (Voice& v : voices_)
        if (v.sample == sample) SDL_ClearAudioStream(v.stream), v.job.reset(), v.loops = 0;
}

bool Sfx::samplePlaying(uint32_t sample) const {
    for (const Voice& v : voices_)
        if (v.sample == sample && (v.paused || v.job || SDL_GetAudioStreamAvailable(v.stream) > 0)) return true;
    return false;
}

void Sfx::stopBuss(uint32_t buss) {
    for (Voice& v : voices_)
        if (buss && soundBusses().under(v.buss, buss)) SDL_ClearAudioStream(v.stream), v.job.reset(), v.loops = 0;
}

void Sfx::pauseBuss(uint32_t buss, bool paused) {
    for (Voice& v : voices_) {
        if (!buss || !soundBusses().under(v.buss, buss) || v.paused == paused) continue;
        v.paused = paused;
        if (paused) audiodev::unbindFx(v.stream);
        else if (device_) audiodev::bindFx(v.stream);
    }
}

void Sfx::stopAll() {
    for (Voice& v : voices_) audiodev::unbindFx(v.stream), SDL_DestroyAudioStream(v.stream);
    voices_.clear();
}

void Sfx::applyBuss(Voice& v) {
    const SoundBusses& sb = soundBusses();
    const float gain = std::max(0.0f, v.volume * sb.gainAt(v.bussIndex));
    const float pr = sb.pitchAt(v.bussIndex);
    const float ratio = std::clamp(v.pitch * (pr > 0 ? pr : 0.01f), 0.01f, 100.0f);
    if (gain != v.gainSet) SDL_SetAudioStreamGain(v.stream, gain), v.gainSet = gain;
    if (ratio != v.ratioSet) SDL_SetAudioStreamFrequencyRatio(v.stream, ratio), v.ratioSet = ratio;
}

void Sfx::update() {
    for (Voice& v : voices_) applyBuss(v), feed(v);  // the busses' fades; the streamed decodes
    // A looping voice gets its sample again while under 0.25 s is left.
    for (Voice& v : voices_) {
        if (!v.loops || v.paused || v.job) continue;
        auto it = entries_.find(v.sample);
        if (it == entries_.end()) continue;
        const Entry& e = *it->second;
        if (SDL_GetAudioStreamQueued(v.stream) >= e.rate * e.channels * int(sizeof(float)) / 4) continue;
        if (e.job) {
            std::lock_guard<std::mutex> g(e.job->m);
            if (!e.job->done || e.job->bad || e.job->pcm.empty()) continue;
            SDL_PutAudioStreamData(v.stream, e.job->pcm.data(), int(e.job->pcm.size() * sizeof(float)));
        } else {
            if (e.pcm.empty()) continue;
            SDL_PutAudioStreamData(v.stream, e.pcm.data(), int(e.pcm.size() * sizeof(float)));
        }
        SDL_FlushAudioStream(v.stream);
        if (v.loops > 0) --v.loops;
    }
    for (auto& j : jobs_) {
        std::lock_guard<std::mutex> g(j->m);
        if (j->done && !j->counted) j->counted = true, ++(j->bad ? stats_.failures : stats_.decoded);
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) {
                                     if (v.paused || v.job || SDL_GetAudioStreamAvailable(v.stream) > 0) return false;
                                     audiodev::unbindFx(v.stream);
                                     SDL_DestroyAudioStream(v.stream);
                                     return true;
                                 }),
                  voices_.end());
}

bool Sfx::pcm(uint32_t sample, std::vector<float>& out, int& rate, int& channels) {
    auto it = entries_.find(sample);
    if (it == entries_.end()) return false;
    Entry& e = *it->second;
    if (!e.decoded) decode(e);
    if (e.job) {  // a streamed decode: wait for its end
        if (e.job->thread.joinable()) e.job->thread.join();
        std::lock_guard<std::mutex> g(e.job->m);
        if (e.job->bad) return false;
        out = e.job->pcm, rate = e.rate, channels = e.channels;
        return true;
    }
    if (e.bad || e.pcm.empty()) return false;
    out = e.pcm, rate = e.rate, channels = e.channels;
    return true;
}
