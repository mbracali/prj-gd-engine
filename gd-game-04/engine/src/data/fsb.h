// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Disc;

// GH3's FMOD sound banks (port of tools/fsb.py; docs/formats.md "Audio").
// DATA/MUSIC/<song>.fsb.xen: FSB3, encrypted with FMOD's scheme (plain =
// bit-reverse(stored XOR key[i % 10]), key "5atu6w4zaw", given by the
// script SongLoadFSB [C]); DATA/STREAMS/streamall.fsb.xen: FSB3, plain.
// The samples are XMA1, 2048-byte packets [C data].
struct FsbSample {
    std::string name;       // "slowride_guitar.xma"
    uint32_t length = 0;    // frames
    uint32_t stored = 0;    // bytes
    uint32_t mode = 0;
    int rate = 0, channels = 0;
    uint64_t offset = 0;    // of the data in the file
    // XMA: the codec data's seek table (after its 24-byte head, one u32 LE a
    // 2048-byte packet): the frames decoded before each packet [C data: one
    // entry a packet, the last near the length; meaning I].
    std::vector<uint32_t> seek;
};

struct FsbBank {
    std::string path;
    bool encrypted = false;
    std::vector<FsbSample> samples;
    // The .dat: stem name key -> sample index.
    std::vector<std::pair<uint32_t, uint32_t>> stems;
    const FsbSample* stem(uint32_t nameKey) const;
};

// The FMOD decryption of `n` bytes read from file offset `fileOffset`.
void fsbDecrypt(uint8_t* data, size_t n, uint64_t fileOffset);

// Reads the bank's headers (and its .dat, if any); throws std::runtime_error.
FsbBank loadFsb(const Disc& disc, const std::string& fsbPath, const std::string& datPath);
