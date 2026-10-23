// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data/bytes.h"

struct MidiEvent {
    uint32_t tick = 0;
    uint8_t status = 0;  // 0xFF for meta events
    uint8_t data1 = 0;   // channel events: note / controller; meta: type
    uint8_t data2 = 0;
    std::string text;    // meta payload

    bool isNoteOn() const { return (status & 0xF0) == 0x90 && data2 != 0; }
    bool isNoteOff() const { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0); }
};

struct MidiTrack {
    std::string name;
    std::vector<MidiEvent> events;  // in file order, ticks absolute
};

struct MidiFile {
    uint16_t division = 480;  // ticks per beat
    std::vector<MidiTrack> tracks;
};

MidiFile parseMidi(const Bytes& data);

// Tick <-> time conversion from the tempo and time-signature events of track 0.
class TempoMap {
public:
    void build(const MidiFile& midi);
    double tickToMs(double tick) const;
    double msToTick(double ms) const;
    // Position in measures (0-based, fractional), as used for Star Power drain.
    double tickToMeasure(double tick) const;
    int division() const { return division_; }

private:
    struct Tempo { double tick, ms, usPerBeat; };
    struct Signature { double tick, measure; int beatsPerMeasure; };
    std::vector<Tempo> tempos_;
    std::vector<Signature> signatures_;
    int division_ = 480;
};
