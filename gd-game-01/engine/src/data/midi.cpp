// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/midi.h"

#include <algorithm>
#include <stdexcept>

namespace {

uint32_t readVarLen(ByteReader& r) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t c = r.u8();
        v = (v << 7) | (c & 0x7F);
        if (!(c & 0x80)) return v;
    }
    throw std::runtime_error("MIDI variable-length value too long");
}

MidiTrack parseTrack(ByteReader& r) {
    MidiTrack track;
    uint32_t tick = 0;
    uint8_t running = 0;
    while (r.remaining()) {
        tick += readVarLen(r);
        MidiEvent e;
        e.tick = tick;
        uint8_t b = r.u8();
        if (b == 0xFF) {
            e.status = 0xFF;
            e.data1 = r.u8();
            e.text = r.str(readVarLen(r));
            if (e.data1 == 0x03 && track.name.empty()) track.name = e.text;
            track.events.push_back(std::move(e));
            if (track.events.back().data1 == 0x2F) break;  // end of track
            continue;
        }
        if (b == 0xF0 || b == 0xF7) {  // sysex: skip
            r.skip(readVarLen(r));
            continue;
        }
        if (b & 0x80) {
            running = b;
            e.data1 = r.u8();
        } else {
            if (!running) throw std::runtime_error("MIDI data byte without status");
            e.data1 = b;
        }
        e.status = running;
        uint8_t kind = running & 0xF0;
        if (kind != 0xC0 && kind != 0xD0) e.data2 = r.u8();
        track.events.push_back(std::move(e));
    }
    return track;
}

}  // namespace

MidiFile parseMidi(const Bytes& data) {
    ByteReader r(data);
    if (r.str(4) != "MThd") throw std::runtime_error("not a MIDI file");
    uint32_t headerLen = r.u32be();
    r.u16be();  // format
    uint16_t trackCount = r.u16be();
    MidiFile midi;
    midi.division = r.u16be();
    if (midi.division & 0x8000) throw std::runtime_error("SMPTE MIDI timing is not supported");
    r.skip(headerLen - 6);
    while (midi.tracks.size() < trackCount && r.remaining() >= 8) {
        std::string id = r.str(4);
        uint32_t len = r.u32be();
        if (len > r.remaining()) throw std::runtime_error("truncated MIDI track");
        if (id == "MTrk") {
            ByteReader tr(r.ptr(), len);
            midi.tracks.push_back(parseTrack(tr));
        }
        r.skip(len);
    }
    return midi;
}

void TempoMap::build(const MidiFile& midi) {
    division_ = midi.division;
    tempos_ = {{0, 0, 500000}};
    signatures_ = {{0, 0, 4}};
    if (midi.tracks.empty()) return;
    for (const MidiEvent& e : midi.tracks[0].events) {
        if (e.status != 0xFF) continue;
        if (e.data1 == 0x51 && e.text.size() == 3) {
            double us = (uint8_t(e.text[0]) << 16) | (uint8_t(e.text[1]) << 8) | uint8_t(e.text[2]);
            double ms = tickToMs(e.tick);
            if (tempos_.back().tick == e.tick) tempos_.pop_back();
            tempos_.push_back({double(e.tick), ms, us});
        } else if (e.data1 == 0x58 && e.text.size() >= 1) {
            double measure = tickToMeasure(e.tick);
            if (signatures_.back().tick == e.tick) signatures_.pop_back();
            signatures_.push_back({double(e.tick), measure, std::max(1, int(uint8_t(e.text[0])))});
        }
    }
}

double TempoMap::tickToMs(double tick) const {
    auto it = std::upper_bound(tempos_.begin(), tempos_.end(), tick,
                               [](double t, const Tempo& x) { return t < x.tick; });
    const Tempo& t = *(it == tempos_.begin() ? it : it - 1);
    return t.ms + (tick - t.tick) * t.usPerBeat / division_ / 1000.0;
}

double TempoMap::msToTick(double ms) const {
    auto it = std::upper_bound(tempos_.begin(), tempos_.end(), ms,
                               [](double m, const Tempo& x) { return m < x.ms; });
    const Tempo& t = *(it == tempos_.begin() ? it : it - 1);
    return t.tick + (ms - t.ms) * 1000.0 * division_ / t.usPerBeat;
}

double TempoMap::tickToMeasure(double tick) const {
    auto it = std::upper_bound(signatures_.begin(), signatures_.end(), tick,
                               [](double t, const Signature& x) { return t < x.tick; });
    const Signature& s = *(it == signatures_.begin() ? it : it - 1);
    return s.measure + (tick - s.tick) / (double(division_) * s.beatsPerMeasure);
}
