// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd24: Rock Band Unplugged (PlayStation Portable), reimplemented. Reads the
// player's own UMD image (.iso or .chd) at run time; see ../README.md and
// ../docs/FORMATS.md. Not playable yet (GD24-002, the first boot).
//
//   gd24 <disc>                  the game: for now our own status screen (the
//                                disc's title and songs), ENHANCED and GAMEPADS;
//                                Confirm plays a song on the original highway
//   gd24 <disc> --song <FOLDER> [--difficulty 0..3] [--track 0..3]
//                                straight into a song (Easy..Expert; drums, bass, guitar, vocals)
//   gd24 --formattest <disc>     every file the port reads, parsed; one line per failure
//   gd24 --charttest <disc> [<song> | all] [--notes]
//                                BEATMATCH.STR: difficulty images, gem lists, tempo-map check
//   gd24 --highwaytest <disc> [--sheet <f.bmp>]
//                                COMMON.STR's textures and meshes: the highway's pieces
//   gd24 --rulestest <disc> [<song> | all] [--difficulty 0..3]   the gameplay rules, headless
//   gd24 --synctest <disc> [<song>]
//                                the preview's place in the chart: DTA (preview ...) vs onsets
//   gd24 --inputtest             the input layer with virtual controllers (no disc)
//   gd24 --audiotest <disc> [<song> | all] [--play]
//                                decodes the song previews (ATRAC3plus) and checks them
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app.h"
#include "data/str_file.h"
#include "data/text_table.h"
#include "data/umd.h"
#include "tests/audiotest.h"
#include "tests/charttest.h"
#include "tests/highwaytest.h"
#include "tests/synctest.h"
#include "tests/inputtest.h"

namespace {

constexpr const char* kDiscId = "ULUS-10418";  // Rock Band Unplugged, USA

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool endsWith(const std::string& s, const char* tail) {
    const size_t n = std::strlen(tail);
    return s.size() >= n && gd24::upperPath(s.substr(s.size() - n)) == gd24::upperPath(tail);
}

// Every file the port reads, checked against its format (docs/FORMATS.md).
int formatTest(const gd24::Umd& umd) {
    int failures = 0, checked = 0;
    auto fail = [&](const std::string& path, const std::string& why) {
        std::printf("formattest: FAIL %s: %s\n", path.c_str(), why.c_str());
        ++failures;
    };
    std::printf("formattest: disc %s, volume \"%s\", %zu files\n", umd.discId().c_str(), umd.volumeId().c_str(),
                umd.files().size());
    if (umd.discId() != kDiscId) fail("UMD_DATA.BIN", "disc ID " + umd.discId() + ", expected " + kDiscId);

    // The songs: BEATMATCH.STR (charts) and preview audio. decodeAtm is
    // implemented, but is not yet wired into gameplay playback.
    int songs = 0, tutorials = 0;
    for (const gd24::Umd::Entry* e : umd.list("PSP_GAME/USRDIR/MUSIC/")) {
        if (!endsWith(e->path, "/BEATMATCH.STR")) continue;
        const std::string dir = e->path.substr(0, e->path.size() - std::strlen("BEATMATCH.STR"));
        const std::string name = dir.substr(std::strlen("PSP_GAME/USRDIR/MUSIC/"), dir.size() - std::strlen("PSP_GAME/USRDIR/MUSIC/") - 1);
        const bool tutorial = name.size() == 3 && name[0] == 'T';
        tutorial ? ++tutorials : ++songs;
        if (tutorial) continue;
        std::vector<uint8_t> at3;
        const std::string preview = dir + name + "_PREVIEW.AT3";
        ++checked;
        if (!umd.read(preview, at3)) fail(preview, "missing");
        else if (at3.size() < 0x3C || std::memcmp(at3.data(), "RIFF", 4) || std::memcmp(&at3[8], "WAVEfmt ", 8) ||
                 (at3[20] | at3[21] << 8) != 0xFFFE)
            fail(preview, "not a RIFF WAVE with an extensible (ATRAC3plus) format");
    }
    std::printf("formattest: %d songs, %d tutorial tracks\n", songs, tutorials);
    if (songs != 41) fail("PSP_GAME/USRDIR/MUSIC", "41 songs expected, found " + std::to_string(songs));

    // The heap images (STR container) and the text tables.
    int strs = 0, chunks = 0, texts = 0, strings = 0;
    for (const gd24::Umd::Entry& e : umd.files()) {
        const std::string up = gd24::upperPath(e.path);
        if (endsWith(up, ".STR") && up.find("/PRESET/") == std::string::npos && !endsWith(up, "GLOBAL_BINARY.STR") &&
            !endsWith(up, "JUKEBOX.STR")) {
            std::vector<uint8_t> head(std::min<uint32_t>(e.size, 0x10000));
            std::string why;
            gd24::StrFile f;
            ++checked, ++strs;
            if (!umd.readAt(e, 0, head.data(), head.size())) fail(e.path, "read");
            else if (!f.parse(head.data(), head.size(), e.size, &why)) fail(e.path, why);
            else chunks += int(f.chunks.size());
        } else if (endsWith(up, ".TXT") && up.find("/TEXT/") != std::string::npos) {
            std::vector<uint8_t> bytes;
            gd24::TextTable t;
            std::string why;
            ++checked, ++texts;
            if (!umd.read(e.path, bytes)) fail(e.path, "read");
            else if (!t.parse(bytes, &why)) fail(e.path, why);
            else strings += int(t.strings.size());
        } else if (endsWith(up, ".PMF")) {
            uint8_t m[4];
            ++checked;
            if (!umd.readAt(e, 0, m, 4) || std::memcmp(m, "PSMF", 4) != 0) fail(e.path, "no PSMF header");
        }
    }
    std::vector<uint8_t> gb;
    ++checked;
    if (!umd.read("PSP_GAME/USRDIR/STREAMS/GLOBAL_BINARY.STR", gb) || gb.size() < 12 ||
        uint64_t(le32(&gb[0])) + le32(&gb[4]) != le32(&gb[8]) || le32(&gb[8]) != gb.size())
        fail("PSP_GAME/USRDIR/STREAMS/GLOBAL_BINARY.STR", "its two parts do not add up to the file");
    std::printf("formattest: %d STR files (%d chunks), %d text tables (%d strings)\n", strs, chunks, texts, strings);
    if (failures) std::printf("formattest: %d failures in %d files\n", failures, checked);
    else std::printf("formattest: all %d files parse\n", checked);
    return failures ? 1 : 0;
}

}  // namespace

int gd24Main(int argc, char** argv) {
    bool rulestest = false;
    bool formattest = false, audiotest = false, charttest = false, highwaytest = false, synctest = false, play = false, notes = false;
    std::string sheet;
    std::string disc, song, playSong;
    int difficulty = -1, track = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--formattest") formattest = true;
        else if (a == "--audiotest") audiotest = true;
        else if (a == "--charttest") charttest = true;
        else if (a == "--highwaytest") highwaytest = true;
        else if (a == "--synctest") synctest = true;
        else if (a == "--rulestest") rulestest = true;
        else if (a == "--sheet" && i + 1 < argc) sheet = argv[++i];
        else if (a == "--play") play = true;
        else if (a == "--notes") notes = true;
        else if (a == "--song" && i + 1 < argc) playSong = argv[++i];
        else if (a == "--difficulty" && i + 1 < argc) difficulty = std::atoi(argv[++i]);
        else if (a == "--track" && i + 1 < argc) track = std::atoi(argv[++i]);
        else if ((audiotest || charttest || synctest || rulestest) && !disc.empty() && !a.empty() && a[0] != '-') song = a;
        else if (a == "--inputtest") return inputTest();
        else if (a == "--version") return std::printf("gd24 %s\n", GD24_VERSION), 0;
        else if (!a.empty() && a[0] != '-') disc = a;
    }
    if (disc.empty()) {
        std::fprintf(stderr, "usage: gd24 [--formattest | --audiotest [<song>] [--play]] <Rock Band Unplugged disc image (.iso / .chd)>\n");
        return 2;
    }
    std::string error;
    auto umd = gd24::Umd::open(disc, &error);
    if (!umd) {
        SDL_Log("gd24: cannot read the disc %s: %s", disc.c_str(), error.c_str());
        return 1;
    }
    SDL_Log("gd24 %s: disc %s (%zu files)", GD24_VERSION, umd->discId().c_str(), umd->files().size());
    if (formattest) return formatTest(*umd);
    if (audiotest) return audioTest(*umd, song, play);
    if (charttest) return chartTest(*umd, song, notes);
    if (highwaytest) return highwayTest(*umd, sheet);
    if (synctest) return gd24::syncTest(*umd, song);
    if (rulestest) return rulesTest(*umd, song, difficulty);
    umd.reset();
    Options opt;
    opt.disc = disc;
    opt.song = playSong, opt.difficulty = difficulty, opt.track = track;
    App app;
    return app.run(opt);
}

#ifndef GD24_MODULE
int main(int argc, char** argv) { return gd24Main(argc, argv); }
#endif
