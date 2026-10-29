// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/sfx.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstring>
#include <exception>

#include "audio/xma_decoder.h"
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
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device_) {
        error = SDL_GetError();
        return false;
    }
    return true;
}

void Sfx::close() {
    stopAll();
    if (device_) SDL_CloseAudioDevice(device_);
    device_ = 0;
}

void Sfx::addPak(const Disc& disc, const std::string& pakPath) {
    try {
        Pak pak = Pak::load(disc, pakPath);
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
            entries_.emplace(key, en);
        }
    } catch (const std::exception& ex) {
        SDL_Log("sfx: %s: %s", fsbPath.c_str(), ex.what());
    }
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
        if (!xma::decodeXma1(data.data(), data.size(), e.rate, e.channels, e.length, e.pcm, err)) {
            ++stats_.failures;
            return e.bad = true, false;
        }
        ++stats_.decoded;
        return true;
    }
    const Bytes& d = e.riff;
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0) return e.bad = true, false;
    const uint8_t* data = nullptr;
    size_t dataSize = 0;
    for (size_t o = 12; o + 8 <= d.size();) {
        const uint32_t size = le32(d.data() + o + 4);
        if (o + 8 + size > d.size()) break;
        if (std::memcmp(d.data() + o, "fmt ", 4) == 0 && size >= 32) {
            // XMAWAVEFORMAT: a 12-byte header (tag 0x165, bits, options,
            // skip, streams, loop count, version), then 20-byte streams:
            // rate at +4, channels at +17 [C data: all 227 .wav].
            const uint8_t* f = d.data() + o + 8;
            e.rate = int(le32(f + 12 + 4));
            e.channels = f[12 + 17];
            if (e.rate < 8000 || e.rate > 96000) e.rate = 44100;
            if (e.channels < 1 || e.channels > 2) e.channels = le16(f + 2) == 2 ? 2 : 1;
        } else if (std::memcmp(d.data() + o, "data", 4) == 0) {
            data = d.data() + o + 8;
            dataSize = size;
        }
        o += 8 + size + (size & 1);
    }
    if (!data) return e.bad = true, false;
    std::string err;
    if (!xma::decodeXma1(data, dataSize, e.rate, e.channels, 0, e.pcm, err)) {
        SDL_Log("sfx: decode failed: %s", err.c_str());
        ++stats_.failures;
        return e.bad = true, false;
    }
    ++stats_.decoded;
    return true;
}

uint32_t Sfx::play(uint32_t sample, float volume, float pitch) {
    auto it = entries_.find(sample);
    if (it == entries_.end()) {
        ++stats_.unknown;
        return 0;
    }
    Entry& e = *it->second;
    if (!e.decoded) decode(e);
    if (e.bad || e.pcm.empty()) return 0;
    ++stats_.plays;
    const uint32_t id = nextVoice_++;
    if (!device_) return id;
    SDL_AudioSpec src{SDL_AUDIO_F32, e.channels, e.rate};
    SDL_AudioStream* s = SDL_CreateAudioStream(&src, nullptr);
    if (!s) return 0;
    SDL_SetAudioStreamGain(s, std::clamp(volume, 0.0f, 4.0f));
    if (pitch > 0 && pitch != 1) SDL_SetAudioStreamFrequencyRatio(s, std::clamp(pitch, 0.25f, 4.0f));
    SDL_PutAudioStreamData(s, e.pcm.data(), int(e.pcm.size() * sizeof(float)));
    SDL_FlushAudioStream(s);
    SDL_BindAudioStream(device_, s);
    voices_.push_back(Voice{id, s});
    return id;
}

bool Sfx::playing(uint32_t voice) const {
    for (const Voice& v : voices_)
        if (v.id == voice) return SDL_GetAudioStreamAvailable(v.stream) > 0;
    return false;
}

void Sfx::stop(uint32_t voice) {
    for (Voice& v : voices_)
        if (v.id == voice) SDL_ClearAudioStream(v.stream);
}

void Sfx::stopAll() {
    for (Voice& v : voices_) SDL_DestroyAudioStream(v.stream);
    voices_.clear();
}

void Sfx::update() {
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) {
                                     if (SDL_GetAudioStreamAvailable(v.stream) > 0) return false;
                                     SDL_DestroyAudioStream(v.stream);
                                     return true;
                                 }),
                  voices_.end());
}
