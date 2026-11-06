// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gdcore_platform checks: path helpers, the save folder, file IO, disc images
// (made here, in memory: never game data), shared libraries.
#include <SDL3/SDL.h>

#include <cstring>
#include <string>
#include <vector>

#include "gd/core/platform/disc.h"
#include "gd/core/platform/file.h"
#include "gd/core/platform/library.h"
#include "gd/core/platform/paths.h"
#include "selftest.h"

namespace p = gd::core::platform;

namespace {

void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = uint8_t(v);
    b[at + 1] = uint8_t(v >> 8);
}
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + size_t(i)] = uint8_t(v >> (8 * i));
}
void put32be(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + size_t(3 - i)] = uint8_t(v >> (8 * i));
}

// An ISO 9660 directory record (both-endian fields, as ECMA-119 9.1).
std::vector<uint8_t> isoRecord(const std::string& name, uint32_t lba, uint32_t size, bool dir) {
    size_t len = 33 + name.size();
    len += len & 1;
    std::vector<uint8_t> r(len, 0);
    r[0] = uint8_t(len);
    put32(r, 2, lba);
    put32be(r, 6, lba);
    put32(r, 10, size);
    put32be(r, 14, size);
    r[25] = dir ? 2 : 0;
    r[32] = uint8_t(name.size());
    std::memcpy(&r[33], name.data(), name.size());
    return r;
}

// A small PlayStation 2 style image: SYSTEM.CNF and DATA/SUB/FILE.BIN.
std::vector<uint8_t> makeIso(const std::string& cnf, const std::vector<uint8_t>& payload) {
    constexpr size_t S = 2048;
    std::vector<uint8_t> img(32 * S, 0);
    auto dirSector = [&](size_t sector, const std::vector<std::vector<uint8_t>>& recs) {
        size_t at = sector * S;
        for (const auto& r : recs) {
            std::memcpy(&img[at], r.data(), r.size());
            at += r.size();
        }
    };
    // PVD
    img[16 * S] = 1;
    std::memcpy(&img[16 * S + 1], "CD001", 5);
    img[16 * S + 6] = 1;
    std::memset(&img[16 * S + 40], ' ', 32);
    std::memcpy(&img[16 * S + 40], "GDCORE_TEST", 11);
    auto root = isoRecord(std::string(1, '\0'), 18, S, true);
    std::memcpy(&img[16 * S + 156], root.data(), 34);
    img[17 * S] = 255;  // terminator
    std::memcpy(&img[17 * S + 1], "CD001", 5);
    dirSector(18, {isoRecord(std::string(1, '\0'), 18, S, true), isoRecord(std::string(1, '\1'), 18, S, true),
                   isoRecord("DATA", 19, S, true), isoRecord("SYSTEM.CNF;1", 21, uint32_t(cnf.size()), false)});
    dirSector(19, {isoRecord(std::string(1, '\0'), 19, S, true), isoRecord(std::string(1, '\1'), 18, S, true),
                   isoRecord("SUB", 20, S, true)});
    dirSector(20, {isoRecord(std::string(1, '\0'), 20, S, true), isoRecord(std::string(1, '\1'), 19, S, true),
                   isoRecord("FILE.BIN;1", 22, uint32_t(payload.size()), false)});
    std::memcpy(&img[21 * S], cnf.data(), cnf.size());
    std::memcpy(&img[22 * S], payload.data(), payload.size());
    return img;
}

// An XDVDFS entry at `off` of a folder sector.
size_t xEntry(std::vector<uint8_t>& b, size_t off, uint16_t left, uint16_t right, uint32_t sector, uint32_t size,
              bool dir, const std::string& name) {
    put16(b, off, left);
    put16(b, off + 2, right);
    put32(b, off + 4, sector);
    put32(b, off + 8, size);
    b[off + 12] = dir ? 0x10 : 0x20;
    b[off + 13] = uint8_t(name.size());
    std::memcpy(&b[off + 14], name.data(), name.size());
    return (14 + name.size() + 3) & ~size_t(3);
}

// A partition-only Xbox image: default.xex, media/ with song.bin, an empty folder.
std::vector<uint8_t> makeXdvdfs(const std::vector<uint8_t>& payload) {
    constexpr size_t S = 2048;
    std::vector<uint8_t> img(40 * S, 0);
    std::memcpy(&img[32 * S], "MICROSOFT*XBOX*MEDIA", 20);
    put32(img, 32 * S + 20, 33);  // root folder sector
    put32(img, 32 * S + 24, S);
    std::vector<uint8_t> root(S, 0xFF), media(S, 0xFF);
    // root: "default.xex" (offset 0) left -> "empty" (dword 8), right -> "media" (dword 16)
    xEntry(root, 0, 8, 16, 35, 4, false, "default.xex");
    xEntry(root, 32, 0, 0, 0, 0, true, "empty");
    xEntry(root, 64, 0, 0, 34, S, true, "media");
    xEntry(media, 0, 0, 0, 36, uint32_t(payload.size()), false, "song.bin");
    std::memcpy(&img[33 * S], root.data(), S);
    std::memcpy(&img[34 * S], media.data(), S);
    std::memcpy(&img[35 * S], "XEX2", 4);
    std::memcpy(&img[36 * S], payload.data(), payload.size());
    return img;
}

}  // namespace

GDCORE_TEST(platform_path_helpers) {
    GDCORE_CHECK(p::join("a", "b") == "a/b");
    GDCORE_CHECK(p::join("a/", "/b") == "a/b");
    GDCORE_CHECK(p::join("", "b") == "b");
    GDCORE_CHECK(p::parentDir("/x/y/z.iso") == "/x/y/");
    GDCORE_CHECK(p::parentDir("/x/y/") == "/x/");
    GDCORE_CHECK(p::parentDir("z.iso").empty());
    GDCORE_CHECK(p::fileName("/x/y/z.iso") == "z.iso");
    GDCORE_CHECK(p::fileName("/x/y/") == "y");
    GDCORE_CHECK(p::isContentUri("content://com.android.providers/document/1"));
    GDCORE_CHECK(!p::isContentUri("/sdcard/a.iso"));
    GDCORE_CHECK(p::isDescriptorPath("/proc/self/fd/42"));
    GDCORE_CHECK(!p::isDescriptorPath("/proc/self/fd/"));
    GDCORE_CHECK(!p::isDescriptorPath("/proc/self/fd/4x"));
}

// The save folder is exactly what every game computes today
// (SDL_GetPrefPath("gd-engine", "gdNN")), here under a scratch XDG_DATA_HOME.
GDCORE_TEST(platform_pref_dir_matches_games) {
#if defined(__linux__) && !defined(__ANDROID__)
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XDG_DATA_HOME", ctx.scratch.c_str(), true);
#endif
    for (const char* app : {"gd01", "gd02", "gd03", "gd04", "gdul"}) {
        char* sdl = SDL_GetPrefPath("gd-engine", app);
        const std::string want = sdl ? sdl : "";
        SDL_free(sdl);
        GDCORE_CHECK_MSG(p::gamePrefDir(app) == want, p::gamePrefDir(app) + " vs " + want);
        GDCORE_CHECK(p::prefDir("gd-engine", app, p::PrefLayout::Sdl) == want);
#ifndef __ANDROID__
        GDCORE_CHECK(p::prefDir("gd-engine", app, p::PrefLayout::Nested) == want);
#endif
        GDCORE_CHECK(p::isDir(want));
#if defined(__linux__) && !defined(__ANDROID__)
        GDCORE_CHECK_MSG(want == ctx.scratch + "gd-engine/" + app + "/", want);
#endif
    }
#if defined(__linux__) && !defined(__ANDROID__)
    GDCORE_CHECK(p::xdgDataHome() == ctx.scratch);
#endif
}

GDCORE_TEST(platform_module_dir) {
    const std::string dir = p::moduleDir();
    ctx.note("moduleDir: " + dir);
    GDCORE_CHECK(!dir.empty());
#ifndef __ANDROID__
    // Linked into this program: its folder.
    GDCORE_CHECK_MSG(dir == p::baseDir(), dir + " vs " + p::baseDir());
#endif
}

GDCORE_TEST(platform_file_io) {
    // A UTF-8 name (Windows: wide-character open).
    const std::string path = ctx.scratch + "dados_\xC3\xA7\xC3\xA3o_\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88.bin";
    std::vector<uint8_t> data(100000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = uint8_t(i * 7 + 3);
    std::string err;
    GDCORE_CHECK_MSG(p::writeFileAtomic(path, data.data(), data.size(), &err), err);
    GDCORE_CHECK(!p::isFile(path + ".tmp"));
    GDCORE_CHECK(p::isFile(path));
    std::vector<uint8_t> back;
    GDCORE_CHECK_MSG(p::readFile(path, back, &err), err);
    GDCORE_CHECK(back == data);
    // Overwrite in place.
    const char small[] = "abc";
    GDCORE_CHECK(p::writeFileAtomic(path, small, 3, &err));
    GDCORE_CHECK(p::readFile(path, back) && back.size() == 3 && back[2] == 'c');

    auto f = p::RandomAccessFile::open(path, &err);
    GDCORE_CHECK_MSG(f != nullptr, err);
    if (f) {
        uint8_t b[3];
        GDCORE_CHECK(f->size() == 3);
        GDCORE_CHECK(f->read(1, b, 2) && b[0] == 'b' && b[1] == 'c');
        GDCORE_CHECK(!f->read(2, b, 2));  // past the end
        GDCORE_CHECK(!f->read(~uint64_t(0), b, 1));
    }
    err.clear();
    GDCORE_CHECK(p::RandomAccessFile::open(ctx.scratch + "missing.iso", &err) == nullptr);
    GDCORE_CHECK(!err.empty());
    GDCORE_CHECK(p::makeDirs(ctx.scratch + "a/b/c") && p::isDir(ctx.scratch + "a/b/c"));
}

GDCORE_TEST(platform_disc_iso9660) {
    const std::string cnf = "BOOT2 = cdrom0:\\SLUS_000.00;1\r\nVER = 1.00\r\n";
    std::vector<uint8_t> payload(5000);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = uint8_t(i ^ 0x5A);
    // Through a real file, as a game reads it.
    const std::string path = ctx.scratch + "test.iso";
    const auto img = makeIso(cnf, payload);
    GDCORE_CHECK(p::writeFileAtomic(path, img.data(), img.size()));
    std::string err;
    std::shared_ptr<p::RandomAccessFile> file = p::RandomAccessFile::open(path, &err);
    GDCORE_CHECK_MSG(file != nullptr, err);
    if (!file) return;
    auto fs = p::openDisc(file, &err);
    GDCORE_CHECK_MSG(fs != nullptr, err);
    if (!fs) return;
    GDCORE_CHECK(fs->format() == "iso9660");
    GDCORE_CHECK(fs->volumeId() == "GDCORE_TEST");
    p::DiscEntry e;
    GDCORE_CHECK(fs->find("system.cnf", e) && !e.isDir && e.size == cnf.size());
    std::vector<uint8_t> got;
    GDCORE_CHECK(fs->readFile(e, got) && std::string(got.begin(), got.end()) == cnf);
    GDCORE_CHECK(fs->find("/Data/sub/file.bin", e) && e.size == payload.size());
    GDCORE_CHECK(fs->readFile(e, got) && got == payload);
    uint8_t mid[4];
    GDCORE_CHECK(fs->read(e, 4000, mid, 4) && mid[0] == payload[4000] && mid[3] == payload[4003]);
    GDCORE_CHECK(!fs->read(e, 4998, mid, 4));
    GDCORE_CHECK(fs->find("DATA", e) && e.isDir);
    GDCORE_CHECK(!fs->find("DATA/missing", e));
    GDCORE_CHECK(!fs->find("SYSTEM.CNF/x", e));
    std::vector<p::DiscEntry> root;
    GDCORE_CHECK(fs->list("", root) && root.size() == 2);
    GDCORE_CHECK(!fs->list("SYSTEM.CNF", root));
    // Not a disc.
    GDCORE_CHECK(p::openDisc(p::RandomAccessFile::fromMemory(std::vector<uint8_t>(100 * 2048, 0)), &err) == nullptr);
}

GDCORE_TEST(platform_disc_xdvdfs) {
    std::vector<uint8_t> payload(3000);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = uint8_t(i * 13);
    std::shared_ptr<p::RandomAccessFile> img = p::RandomAccessFile::fromMemory(makeXdvdfs(payload));
    auto fs = p::openDisc(img);
    GDCORE_CHECK(fs != nullptr);
    if (!fs) return;
    GDCORE_CHECK(fs->format() == "xdvdfs");
    p::DiscEntry e;
    GDCORE_CHECK(fs->find("DEFAULT.XEX", e) && e.size == 4);
    std::vector<uint8_t> got;
    GDCORE_CHECK(fs->readFile(e, got) && std::memcmp(got.data(), "XEX2", 4) == 0);
    GDCORE_CHECK(fs->find("media/Song.bin", e) && fs->readFile(e, got) && got == payload);
    std::vector<p::DiscEntry> list;
    GDCORE_CHECK(fs->list("/", list) && list.size() == 3);
    GDCORE_CHECK(fs->list("empty", list) && list.empty());
}

// Optional: disc images given after "--" are opened and their root listed
// (a check against real discs; nothing is written).
GDCORE_TEST(platform_disc_given_images) {
    for (const std::string& path : ctx.args) {
        std::string err;
        std::shared_ptr<p::RandomAccessFile> file = p::RandomAccessFile::open(path, &err);
        GDCORE_CHECK_MSG(file != nullptr, err);
        if (!file) continue;
        auto fs = p::openDisc(file, &err);
        GDCORE_CHECK_MSG(fs != nullptr, path + ": " + err);
        if (!fs) continue;
        std::vector<p::DiscEntry> root;
        GDCORE_CHECK(fs->list("", root) && !root.empty());
        std::string names;
        for (size_t i = 0; i < root.size() && i < 8; ++i) names += (i ? ", " : "") + root[i].name;
        ctx.note(p::fileName(path) + ": " + std::string(fs->format()) + " \"" + fs->volumeId() + "\", " +
                 std::to_string(root.size()) + " root entries (" + names + ")");
    }
}

GDCORE_TEST(platform_shared_library) {
    p::SharedLibrary lib;
    std::string err;
    GDCORE_CHECK(!lib.open({"gdcore-no-such-library.so"}, &err));
    GDCORE_CHECK(!err.empty() && !lib.isOpen());
#if defined(__linux__) && !defined(__ANDROID__)
    // SDL itself, found by the system's search.
    GDCORE_CHECK_MSG(lib.open({"gdcore-no-such-library.so", "libSDL3.so.0"}, &err), err);
    GDCORE_CHECK(lib.symbol("SDL_GetVersion") != nullptr);
    GDCORE_CHECK(lib.symbol("gdcore_no_such_symbol") == nullptr);
    ctx.note("loaded " + lib.path());
#endif
}
