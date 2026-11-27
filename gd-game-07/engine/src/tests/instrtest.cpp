// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/instrtest.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "data/disc.h"
#include "data/qbkey.h"
#include "game/instrument.h"
#include "game/save.h"
#include "game/shop.h"
#include "script/globals.h"

namespace {

std::string name(uint32_t k) {
    const std::string* n = qbkey::names().find(k);
    return n ? *n : ("0x" + std::to_string(k));
}

qb::Struct bandInfoWithCash(int cash) {
    qb::Struct bi;
    qb::Item c;
    c.name = qbkey::key("cash");
    c.value.type = qb::Type::Int;
    c.value.i = cash;
    bi.items.push_back(c);
    return bi;
}

}  // namespace

int runInstrTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) { std::printf("instrtest: %s\n", error.c_str()); return 2; }
    disc->loadNames();
    Globals g;
    try {
        g.load(*disc);
    } catch (const std::exception& e) {
        std::printf("instrtest: globals: %s\n", e.what());
        return 2;
    }

    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) { std::printf("instrtest: FAIL %s\n", what.c_str()); ++failures; }
        return ok;
    };

    // (a) Every instrument kind enumerates its CAS slots, and each slot offers
    // options from the disc's part arrays.
    const instrument::Kind kinds[] = {instrument::Kind::Guitar, instrument::Kind::Bass,
                                       instrument::Kind::Drums, instrument::Kind::Mic};
    for (instrument::Kind k : kinds) {
        std::vector<uint32_t> sl = instrument::slots(k);
        int withOptions = 0, totalOptions = 0;
        for (uint32_t part : sl) {
            int n = int(instrument::options(g, part).size());
            if (n > 0) ++withOptions;
            totalOptions += n;
        }
        std::printf("instrtest: %-6s %zu slots, %d populated, %d options total\n",
                    instrument::kindName(k), sl.size(), withOptions, totalOptions);
        check(!sl.empty(), std::string(instrument::kindName(k)) + ": no slots");
        check(withOptions > 0, std::string(instrument::kindName(k)) + ": no slot has any option");
    }

    // The guitar editor in detail.
    qb::Struct band = bandInfoWithCash(50000);
    instrument::Instrument gtr = instrument::makeDefault(g, instrument::Kind::Guitar);
    check(!gtr.parts.empty(), "default guitar has no parts");
    std::printf("instrtest: default guitar has %zu parts set\n", gtr.parts.size());

    // (b) Choosing a valid, free option succeeds; a bogus desc_id is NotAnOption.
    const uint32_t body = qbkey::key("CAS_Guitar_Body");
    std::vector<uint32_t> bodyOpts = instrument::options(g, body);
    if (check(bodyOpts.size() >= 2, "need >=2 CAS_Guitar_Body options")) {
        // A free (not in the store catalog) body option, if any.
        uint32_t freeOpt = 0;
        for (uint32_t o : bodyOpts) {
            shop::Item it;
            bool priced = false;
            for (const shop::Item& c : shop::catalog(g))
                if (c.part == body && c.descId == o) { priced = true; break; }
            if (!priced) { freeOpt = o; break; }
        }
        if (freeOpt) {
            instrument::SetPart r = instrument::choose(gtr, g, band, body, freeOpt);
            check(r == instrument::SetPart::Ok, std::string("free body choose: ") + instrument::setPartName(r));
            check(gtr.descIdFor(body) == freeOpt, "body choice not applied");
            std::printf("instrtest: chose free body %s -> Ok\n", name(freeOpt).c_str());
        }
        instrument::SetPart bogus = instrument::choose(gtr, g, band, body, qbkey::key("not_a_real_option_xyz"));
        check(bogus == instrument::SetPart::NotAnOption,
              std::string("bogus option expected NotAnOption, got ") + instrument::setPartName(bogus));
        // A part that is not a guitar slot is NotASlot.
        instrument::SetPart notslot = instrument::choose(gtr, g, band, qbkey::key("CAS_Drums"), bodyOpts.front());
        check(notslot == instrument::SetPart::NotASlot,
              std::string("CAS_Drums on a guitar expected NotASlot, got ") + instrument::setPartName(notslot));
    }

    // (c) Ownership gate: a priced guitar/bass part cannot be chosen until it is
    // owned (store purchase / career unlock); then it can.
    const shop::Item* pricedGuitarPart = nullptr;
    std::vector<uint32_t> guitarSlots = instrument::slots(instrument::Kind::Guitar);
    std::vector<uint32_t> bassSlots = instrument::slots(instrument::Kind::Bass);
    for (const shop::Item& it : shop::catalog(g)) {
        bool gslot = false;
        for (uint32_t s : guitarSlots) if (s == it.part) gslot = true;
        if (gslot && it.priced && !it.locked && it.price > 0) { pricedGuitarPart = new shop::Item(it); break; }
    }
    if (pricedGuitarPart) {
        instrument::SetPart before = instrument::choose(gtr, g, band, pricedGuitarPart->part, pricedGuitarPart->descId);
        check(before == instrument::SetPart::NotOwned,
              std::string("priced part before purchase expected NotOwned, got ") + instrument::setPartName(before));
        shop::Buy b = shop::purchase(band, *pricedGuitarPart);
        check(b == shop::Buy::Ok, std::string("purchase for editor: ") + shop::buyName(b));
        instrument::SetPart after = instrument::choose(gtr, g, band, pricedGuitarPart->part, pricedGuitarPart->descId);
        check(after == instrument::SetPart::Ok,
              std::string("priced part after purchase expected Ok, got ") + instrument::setPartName(after));
        std::printf("instrtest: priced part %s/%s gated until bought ($%d), then equippable\n",
                    name(pricedGuitarPart->part).c_str(), name(pricedGuitarPart->descId).c_str(), pricedGuitarPart->price);
        delete pricedGuitarPart;
    } else {
        std::printf("instrtest: (no priced free-to-buy guitar part on this disc; ownership gate not exercised)\n");
    }

    // (d) Name it and SAVE -> RELOAD round-trip: kind, name and every chosen
    // part survive the versioned save unchanged.
    gtr.name = "My Axe #1";
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "gd07_instrtest";
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        save::setFolder(dir.string() + "/");
        save::Tags tags;
        tags[qbkey::key("band_info")] = instrument::toBandInfo(gtr);
        std::string err;
        check(save::write(tags, save::Sections{}, err), "save write failed: " + err);
        save::Tags read;
        save::Sections readSec;
        if (check(save::read(read, readSec, err), "save read failed: " + err)) {
            auto it = read.find(qbkey::key("band_info"));
            if (check(it != read.end(), "band_info not in the save")) {
                instrument::Instrument back = instrument::fromBandInfo(it->second);
                check(back.kind == gtr.kind, "kind changed on round-trip");
                check(back.name == gtr.name, "name changed on round-trip: '" + back.name + "'");
                check(back.parts.size() == gtr.parts.size(),
                      "part count changed: " + std::to_string(back.parts.size()) + " != " + std::to_string(gtr.parts.size()));
                bool allMatch = back.parts.size() == gtr.parts.size();
                for (const auto& p : gtr.parts)
                    if (back.descIdFor(p.first) != p.second) allMatch = false;
                check(allMatch, "a chosen part did not survive the round-trip");
                std::printf("instrtest: round-trip OK — kind=%s name='%s' parts=%zu\n",
                            instrument::kindName(back.kind), back.name.c_str(), back.parts.size());
            }
        }
        save::setFolder("");
        fs::remove_all(dir, ec);
    }

    std::printf("instrtest: %s (%d checks failed)\n", failures ? "FAIL" : "all instrument-editor checks passed", failures);
    return failures ? 1 : 0;
}
