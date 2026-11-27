// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/jam.h"

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"

namespace jam {

namespace {

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// Value-byte width of an integer tag (0x10..0x18); 0x12 is the compressed zero.
// Returns -1 for a tag that is not an integer member.
int intWidth(uint8_t t) {
    switch (t) {
        case 0x10: return 1;
        case 0x11: return 2;
        case 0x12: return 0;  // value 0
        case 0x13: return 3;
        case 0x14: return 4;
        case 0x15: return 5;
        case 0x16: return 6;
        case 0x17: return 7;
        case 0x18: return 8;
        default: return -1;
    }
}

bool isMemberTag(uint8_t t) {
    return t == 0x00 || t == 0x04 || t == 0x0a || t == 0x0c || intWidth(t) >= 0;
}

}  // namespace

const char* encodingName(NoteEncoding e) {
    switch (e) {
        case NoteEncoding::None: return "none";
        case NoteEncoding::NoteTable: return "note-table(0x01)";
        case NoteEncoding::OpcodeStream: return "opcode-stream(0x8x)";
        case NoteEncoding::Unknown: return "unknown";
    }
    return "?";
}

bool parse(const uint8_t* data, size_t size, Jam& out, std::string& error) {
    const uint32_t kMarkerName = qbkey::key("marker_name");
    const uint32_t kTime = qbkey::key("time");
    const uint32_t kBpm = qbkey::key("bpm");
    const uint32_t kLight = qbkey::key("lightshow");
    const uint32_t kMarkerSize = qbkey::key("editable_jam_markers_size");
    const uint32_t kAlbum = qbkey::key("editable_jam_album_cover");
    const uint32_t kGtr = qbkey::key("editable_song_expert");
    const uint32_t kRhythm = qbkey::key("editable_song_rhythm_expert");
    const uint32_t kDrum = qbkey::key("editable_song_drum_expert");

    out.totalBytes = size;
    size_t i = 0;
    uint32_t pendingTime = 0;  // a marker element lists `time` before `marker_name`
    while (i < size) {
        const uint8_t t = data[i];
        if (!isMemberTag(t)) break;  // the keyed header ends; the note payload begins here
        if (t == 0x00) {             // struct close (pop); a 1-byte token
            ++i;
            continue;
        }
        if (t == 0x0a) {  // array header: a u16 count, then that many struct elements inline
            if (i + 3 > size) { error = "truncated array header"; return false; }
            i += 3;
            continue;
        }
        if (i + 5 > size) { error = "truncated member key"; return false; }
        const uint32_t key = le32(data + i + 1);
        i += 5;
        if (t == 0x04) {  // UTF-16LE NUL-terminated wide string
            std::string s;
            while (i + 1 < size && !(data[i] == 0 && data[i + 1] == 0)) {
                const uint16_t c = uint16_t(data[i]) | uint16_t(data[i + 1]) << 8;
                if (c < 0x80) s.push_back(char(c));  // ASCII (the jam names are ASCII)
                else s.push_back('?');
                i += 2;
            }
            i += 2;  // the terminator
            if (key == kMarkerName) {
                Marker m;
                m.name = s;
                m.time = pendingTime;
                out.markers.push_back(m);
            }
            continue;
        }
        if (t == 0x0c) {  // open a sub-struct
            ++out.structCount;
            if (key == kAlbum) out.hasAlbumCover = true;
            else if (key == kGtr) out.hasGuitarTrack = true;
            else if (key == kRhythm) out.hasRhythmTrack = true;
            else if (key == kDrum) out.hasDrumTrack = true;
            continue;
        }
        const int w = intWidth(t);
        uint32_t v = 0;
        for (int b = 0; b < w; ++b) v |= uint32_t(data[i + b]) << (8 * b);
        i += w;
        if (key == kTime) pendingTime = v;
        else if (key == kBpm) { if (!out.markers.empty()) out.markers.back().bpm = int(v); }
        else if (key == kLight) { if (!out.markers.empty()) out.markers.back().lightshow = int(v); }
        else if (key == kMarkerSize) out.markerCount = int(v);
    }

    out.headerBytes = i;
    // Classify the note-performance payload by its first byte.
    if (i >= size) {
        out.noteEncoding = NoteEncoding::None;
    } else if (data[i] == 0x01) {
        out.noteEncoding = NoteEncoding::NoteTable;
    } else if (data[i] >= 0x80) {
        out.noteEncoding = NoteEncoding::OpcodeStream;
    } else {
        out.noteEncoding = NoteEncoding::Unknown;
    }
    // Measure it, excluding the trailing zero padding.
    size_t end = size;
    while (end > i && data[end - 1] == 0) --end;
    out.notePayloadBytes = end > i ? end - i : 0;
    return true;
}

std::vector<Jam> loadAll(const Disc& disc, std::string& error) {
    std::vector<Jam> out;
    const std::string pakPath = "DATA/COMPRESSED/JAMS/jam1.pak.xen";
    if (!disc.exists(pakPath)) {
        error = "jam pak not on the disc: " + pakPath;
        return out;
    }
    Pak pak = Pak::load(disc, pakPath);
    const uint32_t jamType = qbkey::key(".jam");
    for (const PakEntry& e : pak.entries()) {
        if (e.type != jamType) continue;
        auto [p, n] = pak.data(e);
        Jam j;
        const std::string* nm = qbkey::names().find(e.fullName);
        if (!nm) nm = qbkey::names().find(e.name);
        std::string full = nm ? *nm : ("0x" + std::to_string(e.fullName));
        const size_t slash = full.find_last_of("/\\");
        j.file = slash == std::string::npos ? full : full.substr(slash + 1);
        std::string err;
        if (!parse(p, n, j, err)) {
            error = j.file + ": " + err;
            return out;
        }
        out.push_back(std::move(j));
    }
    return out;
}

}  // namespace jam
