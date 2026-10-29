// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/sfxtest.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#include "audio/xma_decoder.h"
#include "data/disc.h"
#include "data/fsb.h"

namespace {

struct Job {
    const FsbBank* bank;
    const FsbSample* sample;
};

}  // namespace

int runSfxTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::fprintf(stderr, "sfxtest: %s\n", error.c_str());
        return 2;
    }
    if (!xma::available()) {
        std::printf("sfxtest: this build has no XMA decoder (FFmpeg); FAILED\n");
        return 1;
    }
    std::vector<FsbBank> banks;
    int failures = 0;
    for (const std::string& f : disc->list()) {
        const bool music = f.rfind("DATA/MUSIC/", 0) == 0, streams = f.rfind("DATA/STREAMS/", 0) == 0;
        if ((!music && !streams) || f.size() < 8 || f.compare(f.size() - 8, 8, ".fsb.xen") != 0) continue;
        std::string dat = f.substr(0, f.size() - 8);
        for (size_t i = dat.rfind('/') + 1; i < dat.size(); ++i) dat[i] = char(std::tolower(static_cast<unsigned char>(dat[i])));
        try {
            banks.push_back(loadFsb(*disc, f, dat + ".dat.xen"));
            if (banks.back().stems.size() != banks.back().samples.size()) {
                std::printf("FAIL %s: the .dat names %zu of %zu samples\n", f.c_str(), banks.back().stems.size(), banks.back().samples.size());
                ++failures;
            }
        } catch (const std::exception& e) {
            std::printf("FAIL %s: %s\n", f.c_str(), e.what());
            ++failures;
        }
    }
    std::vector<Job> jobs;
    for (const FsbBank& b : banks)
        for (const FsbSample& s : b.samples) jobs.push_back(Job{&b, &s});
    // GD04_SFXTEST_LIMIT=<n>: only the first n samples (quick runs).
    if (const char* lim = std::getenv("GD04_SFXTEST_LIMIT")) jobs.resize(std::min(jobs.size(), size_t(std::atoi(lim))));

    std::atomic<size_t> next{0};
    std::atomic<int> bad{0};
    std::atomic<uint64_t> frames{0};
    std::mutex logMutex;
    auto worker = [&]() {
        for (size_t i = next++; i < jobs.size(); i = next++) {
            const Job& j = jobs[i];
            std::string why;
            try {
                std::vector<uint8_t> data(j.sample->stored);
                disc->readRange(j.bank->path, j.sample->offset, data.data(), data.size());
                if (j.bank->encrypted) fsbDecrypt(data.data(), data.size(), j.sample->offset);
                std::vector<float> pcm;
                uint32_t decoded = 0;
                if (!xma::decodeXma1(data.data(), data.size(), j.sample->rate, j.sample->channels, 0, pcm, why, &decoded)) {
                } else if (decoded < j.sample->length) {
                    why = "decoded " + std::to_string(decoded) + " of " + std::to_string(j.sample->length) + " frames";
                } else {
                    for (float v : pcm)
                        if (!std::isfinite(v)) {
                            why = "non-finite samples";
                            break;
                        }
                    frames += decoded;
                }
            } catch (const std::exception& e) {
                why = e.what();
            }
            if (!why.empty()) {
                std::lock_guard<std::mutex> lock(logMutex);
                if (++bad <= 30) std::printf("FAIL %s %s: %s\n", j.bank->path.c_str(), j.sample->name.c_str(), why.c_str());
            }
        }
    };
    const unsigned n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < n; ++t) threads.emplace_back(worker);
    for (auto& t : threads) t.join();
    failures += bad;
    std::printf("banks: %zu, samples decoded: %zu, %.1f hours of audio\n", banks.size(), jobs.size(),
                double(frames.load()) / 44100.0 / 3600.0);
    if (failures) {
        std::printf("sfxtest: %d FAILED\n", failures);
        return 1;
    }
    std::printf("sfxtest: every sound decodes\n");
    return 0;
}
