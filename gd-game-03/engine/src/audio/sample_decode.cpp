// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/sample_decode.h"

#include "audio/adpcm.h"

#include <algorithm>

bool decodeSampleData(const scene::SynthSample& s, std::vector<float>& pcm, std::string& error, uint32_t* frames) {
    const scene::SampleData& d = s.sample;
    pcm.clear();
    if (!d.hasData) return error = "no sample data", false;
    switch (d.format) {
        case scene::SampleData::kVag: {
            // The disc's blocks never carry the end flag and the data is
            // padded to 64 bytes [C data]: the sample count says where the
            // sound ends.
            const AdpcmSample a = decodeAdpcm(s.data(), d.size);
            const size_t n = std::min<size_t>(a.pcm.size(), d.sampleCount ? d.sampleCount : a.pcm.size());
            pcm.reserve(n);
            for (size_t i = 0; i < n; ++i) pcm.push_back(float(a.pcm[i]) * (1.0f / 32768.0f));
            break;
        }
        case scene::SampleData::kPcm16:
        case scene::SampleData::kPcm16b: {
            const uint8_t* p = s.data();
            for (uint32_t i = 0; i + 1 < d.size; i += 2) pcm.push_back(float(int16_t(p[i] | p[i + 1] << 8)) * (1.0f / 32768.0f));
            break;
        }
        default:
            return error = "sample format " + std::to_string(d.format) + " is not decoded", false;
    }
    if (frames) *frames = uint32_t(pcm.size());
    if (pcm.empty()) return error = "empty sample", false;
    return true;
}
