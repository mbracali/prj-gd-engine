// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Disc;

// Guitar Hero: World Tour's GHTunes music-studio songs (`.jam`, 19 on the disc
// in DATA/COMPRESSED/JAMS/jam1.pak.xen; c:/gh4/data/jams/*.jam).
//
// The container is a Neversoft keyed-struct stream (NOT the standard .qb file
// layout): a flat sequence of members, each
//     tag (1 byte) + name key (4 bytes, a standard QB key stored LITTLE-endian)
//     + a typed value
// with these tags [C data: all 19 jams parse their header to completion]:
//   0x00            end of the current struct (pop)
//   0x04 key wstrz  a UTF-16LE, NUL-terminated wide string
//   0x0a u16(LE)    an array header: the count, then that many struct elements
//   0x0c key        open a sub-struct (its members follow until 0x00)
//   0x12 key        an integer whose value is 0 (compressed, no value bytes)
//   0x10..0x18 key  an integer; the value is (tag & 0x0F) little-endian bytes
// The keys resolve against the disc's debug-name table read LITTLE-endian:
// e.g. 90 6b 67 ba -> 0x906b67ba = qbkey("time"); confirmed names: time,
// marker_name, marker_count, bpm, lightshow, editable_jam_markers(_size),
// editable_jam_album_cover(_size), editable_song_expert / _rhythm_expert /
// _drum_expert (the three instrument note tracks), last_root, last_scale.
//
// After the keyed header comes the per-instrument NOTE-PERFORMANCE payload, a
// separate encoding: a fixed note-record table (introduced by tag 0x01 + a u16
// count, 8-byte records of u16 duration / u16 note / u32 time) in the "studio"
// jams, or a 0x8D/0x8E/0x90/0x91 delta-opcode bytestream in the recorded jams.
// That payload is the raw musical content; this parser classifies and measures
// it but does NOT decode the notes themselves (that, plus synthesis, playback
// and the composition studio UI, is a dedicated GHTunes wave).
namespace jam {

struct Marker {           // one editable_jam_markers entry (a song section)
    std::string name;     // marker_name (e.g. "Intro", "Riff", "Solo")
    uint32_t time = 0;    // the section start time (ticks)
    int bpm = 0;          // tempo at this section
    int lightshow = 0;    // the lightshow preset id
};

enum class NoteEncoding { None, NoteTable, OpcodeStream, Unknown };
const char* encodingName(NoteEncoding e);

struct Jam {
    std::string file;                // short name (e.g. "studioexample1.jam")
    std::vector<Marker> markers;     // editable_jam_markers
    int markerCount = 0;             // editable_jam_markers_size
    bool hasAlbumCover = false;      // editable_jam_album_cover present
    bool hasGuitarTrack = false;     // editable_song_expert
    bool hasRhythmTrack = false;     // editable_song_rhythm_expert
    bool hasDrumTrack = false;       // editable_song_drum_expert
    int structCount = 0;             // 0x0c containers in the header (incl. the sample dictionary)
    size_t headerBytes = 0;          // bytes of the keyed-struct header
    NoteEncoding noteEncoding = NoteEncoding::None;
    size_t notePayloadBytes = 0;     // the note-performance payload (excludes trailing zero pad)
    size_t totalBytes = 0;
};

// Parse one .jam blob. Returns false + `error` only on a hard malformation of
// the KEYED HEADER; the note payload is classified and measured, never a
// failure (it is not decoded here).
bool parse(const uint8_t* data, size_t size, Jam& out, std::string& error);

// Load and parse every `.jam` in DATA/COMPRESSED/JAMS/jam1.pak.xen.
std::vector<Jam> loadAll(const Disc& disc, std::string& error);

}  // namespace jam
