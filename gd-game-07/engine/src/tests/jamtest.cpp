// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/jamtest.h"

#include <cstdio>
#include <string>
#include <vector>

#include "data/disc.h"
#include "data/jam.h"

int runJamTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) { std::printf("jamtest: %s\n", error.c_str()); return 2; }
    disc->loadNames();

    std::vector<jam::Jam> jams = jam::loadAll(*disc, error);
    if (!error.empty()) { std::printf("jamtest: %s\n", error.c_str()); return 2; }

    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) { std::printf("jamtest: FAIL %s\n", what.c_str()); ++failures; }
        return ok;
    };

    check(jams.size() == 19, "expected 19 jams, got " + std::to_string(jams.size()));

    int totalMarkers = 0, withTracks = 0;
    long long totalPayload = 0;
    int noteTable = 0, opcodeStream = 0;
    for (const jam::Jam& j : jams) {
        totalMarkers += int(j.markers.size());
        if (j.hasGuitarTrack || j.hasRhythmTrack || j.hasDrumTrack) ++withTracks;
        totalPayload += j.notePayloadBytes;
        if (j.noteEncoding == jam::NoteEncoding::NoteTable) ++noteTable;
        else if (j.noteEncoding == jam::NoteEncoding::OpcodeStream) ++opcodeStream;

        // Build a compact section/bpm summary.
        std::string secs;
        int bpm = 0;
        for (const jam::Marker& m : j.markers) {
            if (!secs.empty()) secs += ",";
            secs += m.name;
            if (m.bpm) bpm = m.bpm;
        }
        std::printf("jamtest: %-24s %6zuB hdr=%4zuB markers=%2zu bpm=%-3d tracks=%c%c%c payload=%7zuB enc=%s  [%s]\n",
                    j.file.c_str(), j.totalBytes, j.headerBytes, j.markers.size(), bpm,
                    j.hasGuitarTrack ? 'G' : '-', j.hasRhythmTrack ? 'R' : '-', j.hasDrumTrack ? 'D' : '-',
                    j.notePayloadBytes, jam::encodingName(j.noteEncoding), secs.c_str());

        // Every jam parsed its header (loadAll would have failed otherwise) and
        // has a classified, non-empty note payload.
        check(j.headerBytes > 0, j.file + ": empty header");
        check(j.noteEncoding != jam::NoteEncoding::None && j.notePayloadBytes > 0,
              j.file + ": no note payload found");
        check(j.noteEncoding != jam::NoteEncoding::Unknown,
              j.file + ": unclassified note payload");
    }

    std::printf("jamtest: %zu jams parsed, 0 header parse errors; %d markers total, %d with instrument tracks; "
                "note payloads: %d note-table + %d opcode-stream, %lld bytes total\n",
                jams.size(), totalMarkers, withTracks, noteTable, opcodeStream, totalPayload);
    std::printf("jamtest: %s (%d checks failed)\n",
                failures ? "FAIL" : "all jam-parse checks passed", failures);
    return failures ? 1 : 0;
}
