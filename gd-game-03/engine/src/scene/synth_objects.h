// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "data/bytes.h"
#include "scene/objects.h"

// GH2's sound bank classes (sfx/gen/*_bank, world/<v>/gen/<v>_bank,
// tutorial/gen/tutorial_bank, char/shared/gen/guitar_smashing_sounds), as
// their Load routines in the Xbox 360 executable read them
// (docs/audio_and_charts.md "Sound effects"; each Load found through the
// class's ClassName virtual -> vtable). Every record on the disc decodes
// exactly to its end marker (--scenetest, --sfxtest).

namespace scene {

// SampleData v11 (Load @ 0x823005F8): the sample's audio.
struct SampleData {
    enum Format : uint32_t { kPcm16 = 0, kPcm16b = 1, kVag = 2, kXma = 3 };  // Q: names (sizes @ 0x82300790)
    uint32_t format = 0;       // 360 disc: 3 (XMA); PS2 disc: 2 (SPU ADPCM / VAG) for every sample
    uint32_t sampleCount = 0;  // frames after decoding
    uint32_t rate = 0;         // 44100 / 22050 / 16000
    uint32_t size = 0;         // bytes of `data` (PS2 VAG: whole 16-byte blocks padded to 64 bytes, so
                               // up to 3 blocks past sampleCount; every block's flag byte is 2, no end flag)
    bool hasData = false;      // the u8 before the data (rev > 10; always 1)
    size_t dataOffset = 0;     // where the data starts in the record
};

// SynthSample v5 (Load @ 0x822FEAB8): one mono sample. XMA data is plain
// XMA2 (one stream, 2048-byte packets in stream order).
class SynthSample : public Object {
public:
    std::string file;          // "../samples/x.wav" (the source file)
    bool looping = false;
    int32_t loopStart = 0;     // frames
    int32_t loopEnd = -1;      // frames, -1 = the end
    SampleData sample;
    const uint8_t* data() const { return record.data() + sample.dataOffset; }
};

// ADSR v1 (Load @ 0x822F8E50; names from SyncProperty @ 0x822EC0E0) [C]:
// attack_rate, decay_rate, sustain_rate, release_rate (s), sustain_level,
// attack_mode, sustain_mode, release_mode. The PS2 packs them into SPU2
// ADSR registers (0x822F8CE0); the 360 uses only attack_rate and
// release_rate (SampleInst360::SetADSR 0x8268A590, audio/sfx_dsp.h) [C].
struct Adsr {
    float attackRate = 0, decayRate = 0, sustainRate = 0, releaseRate = 0, sustainLevel = 1;
    uint32_t attackMode = 0, sustainMode = 0, releaseMode = 0;
};

// Sfx v5 (Load @ 0x822EE8B0, also the PS2 disc's v4 without the send; names from SyncProperty @ 0x822EE598): plays
// its sample maps together (a stereo sound is two mono maps panned -1 / 1).
class Sfx : public Object {
public:
    struct Map {                  // SfxMap (Load @ 0x822FF580)
        std::string sample;       // a SynthSample of the bank
        float volume = 0;         // dB
        float pan = 0;            // -1 left .. 1 right (beyond: the surround speakers)
        float transpose = 0;      // semitones
        int32_t fxCore = -1;      // kFXCoreNone / kFXCore0 / kFXCore1: the PS2's SPU2 cores; a no-op on the 360 [C]
        Adsr adsr;
    };
    std::vector<Map> maps;
    std::string send;             // an FxSend (FXverb.send, SPverb.send) or ""
    float sendVolume = 1;         // "send_volume"
};

// Sequence v1 (Load @ 0x822EFB30; names @ 0x822EFBE8): the random spreads a
// sequence adds to what it plays (each value uniformly in avg +- spread,
// @ 0x822F0398 / 0x822F0410 / 0x822F0488) [C].
struct SequenceParams {
    float avgVolume = 0, volumeSpread = 0;        // dB
    float avgTranspose = 0, transposeSpread = 0;  // semitones
    float avgPan = 0, panSpread = 0;
};

// The sequence classes: SfxSeq v3 (@ 0x822F1280), WaitSeq v2
// (@ 0x822EFFE0), RandomGroupSeq v1 (@ 0x822F5100) and SerialGroupSeq v1
// (@ 0x822F5200), the last two on GroupSeq v3 (@ 0x822F4C20).
class SequenceObj : public Object {
public:
    SequenceParams params;
};
// Plays one sequence (or Sfx): `child`.
class SfxSeq : public SequenceObj {
public:
    std::string child;
};
// Waits avg_wait_seconds +- wait_spread (@ 0x822EF118).
class WaitSeq : public SequenceObj {
public:
    float avgWaitSeconds = 0, waitSpread = 0;
};
// Plays its children: RandomGroupSeq `numSimul` picked at random at once,
// SerialGroupSeq one after another.
class GroupSeq : public SequenceObj {
public:
    std::vector<std::string> children;
};
class RandomGroupSeq : public GroupSeq {
public:
    uint32_t numSimul = 1;  // "num_simul"
};
class SerialGroupSeq : public GroupSeq {};

// FxSendReverb v1 (Load @ 0x822FC6F0): FxSend (Load @ 0x823000D8, rev 1:
// Hmx::Object, next_send ObjPtr, stage i32), then the environment symbol
// (an I3DL2 preset name: "plate", "small_room" in the data) [C].
class FxSendReverb : public Object {
public:
    std::string nextSend;
    int32_t stage = 0;
    std::string environment;
};

bool isSynthClass(const std::string& cls);
std::unique_ptr<Object> makeSynthObject(const std::string& cls);
// Decodes the record after its u32 version (`pos` = 4); throws on a layout
// mismatch.
void readSynthObject(Object& o, const Bytes& record, size_t& pos);

}  // namespace scene
