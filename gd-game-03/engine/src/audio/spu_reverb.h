// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

class GameData;

// The PS2's sound effect reverb: each of the SPU2's two cores has one effect
// unit, set up from config/synth.dta (fx (chain) (core_0 ...) (core_1 ...)),
// and a voice whose SfxMap names a core (fx_core) feeds that core's unit.
//
// What SLUS_215.86 does [C]:
//   - SynthPS2 init (0x002276D8) hands (synth (fx ...)) to 0x00225D50, which
//     calls, for core_0 and core_1 in turn, SetFXMode (vtable 0x003ECB20
//     +0x9C = 0x0022CDD8: the mode is the libsd mode number as is, IOP
//     command 3), SetFXVolume (0x0022CE28: depth L = R = trunc(clamp(dB->lin,
//     0, 1) x 32767 + 0.5), command 4), SetFXDelay (0x0022CF48:
//     trunc(delay x 127 + 0.5), unclamped, command 4) and SetFXFeedback
//     (0x0022CFF0: trunc(clamp(dB->lin, 0, 1) x 127 + 0.5), command 4); then
//     SetFXChain (0x0022D100, command 5). dB->lin is 0x002DAC88: 0 at or below
//     -96 dB, else 10^(dB / 20).
//   - SYNTH_S.IRX: command 3 (0x0750) puts the core's work area at the top
//     of its 128 K-word half of SPU2 RAM (EEA 0x1DFFFF / 0x1FFFFF), turns the
//     effect off and on and calls sceSdSetEffectAttr with the work area
//     cleared, then EVOLL / EVOLR = depth; command 4 (0x082C) calls
//     sceSdSetEffectModeParams and sets EVOL again; command 5 (0x0878)
//     sets (TRUE) or clears (FALSE) bits 0-1 of core 1's MMIX, i.e. whether
//     core 0's output also enters core 1's effect.
//   - A voice (SampleInst ctor 0x00222E68 -> SetFXCore 0x0022E3B8, RPC
//     0x2C4; IOP 0x1580 stores it, the key-on at 0x0D04 allocates the voice
//     on that core, or on any core for kFXCoreNone, and turns its effect
//     send (VMIXEL / VMIXER) on exactly when fx_core is not -1).
//   - LIBSD.IRX (PsIIlibsd 3020): sceSdSetEffectAttr (0x0C2C) copies the
//     mode's preset (the 10 x 0x44-byte table at 0x4DE0: a mask word, then
//     the 32 reverb registers in the order of kRegNames) and the work area
//     size (0x4DB8, in 8-word units); for Echo and Delay only it rewrites
//     the delay lines from attr.delay (d1 = delay + 1: mLSAME = 64 d1 -
//     dAPF1, mRSAME = 32 d1 - dAPF2, dLSAME = dRSAME + 32 d1, mLCOMB1 =
//     mRCOMB1 + 32 d1, mLAPF1 = mLAPF2 + 32 d1, mRAPF1 = mRAPF2 + 32 d1)
//     and vWALL = feedback x 258; the address registers are written x 4
//     (0x0110: SPU2 word addresses relative to the work area's start).
// The reverb network itself is the SPU2 hardware's (the same as the PS1
// SPU's, run on 16-bit work-area words at half the 48 kHz rate, left on
// even and right on odd ticks, with the 39-tap half-band filter down and
// up), as documented by the console's emulators (psx-spx, PCSX2) [I: not in
// the game's code; the hardware is not visible to us].
namespace spu2 {

enum FxMode : int {
    kFXModeOff = 0, kFXModeRoom, kFXModeSmallStudio, kFXModeMedStudio, kFXModeLargeStudio,
    kFXModeHall, kFXModeSpace, kFXModeEcho, kFXModeDelay, kFXModePipe, kFXModeCount
};

// One core's (core_N ...) block as the dta gives it.
struct FxCoreConfig {
    int mode = kFXModeOff;
    float volumeDb = -96;
    float delay = 0;        // 0..1 of the echo's longest delay
    float feedbackDb = -96;
};
struct FxConfig {
    bool chain = false;
    std::array<FxCoreConfig, 2> core{};
};
// system/run/config/default.dta's synth fx (Delay -15 dB 0.6 -19 / MedStudio
// silent), the base that config/synth.dta overrides [C data].
FxConfig defaultFxConfig();
// config/synth.dta's (fx ...) block (the game's own; macros by name or
// number). Missing pieces keep the defaults; false when the file is unreadable.
bool loadFxConfig(const GameData& data, FxConfig& out);

// The game's conversions (addresses above) [C].
float fxDbToLin(float db);          // 0x002DAC88
int16_t fxDepth(float volumeDb);    // 0x0022CE28: EVOL
int fxDelay(float delay);           // 0x0022CF48: attr.delay
int fxFeedback(float feedbackDb);   // 0x0022CFF0: attr.feedback

// libsd's preset for a mode (registers in kRegNames order) and its work
// area in SPU2 words [C data: LIBSD.IRX 0x4DE0 / 0x4DB8].
const std::array<uint16_t, 32>& preset(int mode);
uint32_t workAreaWords(int mode);
extern const char* const kRegNames[32];

// One core's effect unit, fed and read at 48 kHz in float (-1..1 = 16-bit
// full scale).
class SpuReverb {
public:
    // sceSdSetEffectAttr: the mode's preset, Echo / Delay rewritten from
    // delay (0..127) and feedback (0..127), the work area cleared; and EVOL.
    void configure(int mode, int delay, int feedback, int16_t depthL, int16_t depthR);
    void configure(const FxCoreConfig& c) {
        configure(c.mode, fxDelay(c.delay), fxFeedback(c.feedbackDb), fxDepth(c.volumeDb), fxDepth(c.volumeDb));
    }
    // Adds EVOL x the effect's output of `in` (stereo interleaved) into
    // `out` (stereo interleaved); `frames` 48 kHz frames.
    void process(const float* in, float* out, int frames);
    int mode() const { return mode_; }
    // The 32 registers in use (after the delay / feedback rewrite).
    const std::array<uint16_t, 32>& registers() const { return reg_; }
    bool active() const { return !mem_.empty() && (depthL_ != 0 || depthR_ != 0); }

private:
    int32_t tick(bool right, int32_t input);  // one channel's half-rate step
    uint32_t at(int32_t offset) const;         // work area index of X + offset

    int mode_ = kFXModeOff;
    std::array<uint16_t, 32> reg_{};
    int16_t depthL_ = 0, depthR_ = 0;
    std::vector<int16_t> mem_;
    uint32_t x_ = 0;      // the work area position (advances every second tick)
    uint64_t cycle_ = 0;  // 48 kHz ticks
    // The half-band filters' histories (39 taps; ring of 64 per channel).
    std::array<std::array<int32_t, 64>, 2> down_{}, up_{};
    uint64_t pos_ = 0;
};

}  // namespace spu2
