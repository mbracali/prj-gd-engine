// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/castest.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "data/band.h"
#include "data/disc.h"
#include "data/qbkey.h"
#include "game/car.h"
#include "game/save.h"
#include "game/shop.h"
#include "script/globals.h"

// --castest: drive World Tour's Create-A-Rocker on the real disc, headless.
namespace {

uint32_t K(const char* s) { return qbkey::key(s); }
std::string name(uint32_t k) {
    const std::string* n = qbkey::names().find(k);
    return n ? *n : ("0x" + std::to_string(k));
}
uint32_t keyOf(const qb::Value* v) {
    return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0;
}

qb::Struct bandInfoWithCash(int cash) {
    qb::Struct bi;
    qb::Item c;
    c.name = K("cash");
    c.value.type = qb::Type::Int;
    c.value.i = cash;
    bi.items.push_back(c);
    return bi;
}

// A real option (desc_id) with a mesh in a CAS part array, to build a body.
uint32_t firstMeshOption(const Globals& g, uint32_t part) {
    const qb::Value* a = g.find(part);
    if (!a || a->type != qb::Type::Array || !a->arr) return 0;
    for (const qb::Value& e : a->arr->values) {
        if (e.type != qb::Type::Struct || !e.st) continue;
        const uint32_t d = keyOf(e.st->find(K("desc_id")));
        if (!d || d == K("none")) continue;
        if (e.st->find(K("mesh"))) return d;
    }
    return 0;
}

}  // namespace

int runCasTest(const std::string& discPath) {
    std::string error;
    auto disc = Disc::open(discPath, error);
    if (!disc) {
        std::printf("castest: %s\n", error.c_str());
        return 2;
    }
    disc->loadNames();
    Globals g;
    try {
        g.load(*disc);
    } catch (const std::exception& e) {
        std::printf("castest: globals: %s\n", e.what());
        return 2;
    }

    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) {
            std::printf("castest: FAIL %s\n", what.c_str());
            ++failures;
        }
        return ok;
    };

    // (a) The editable CAS slot set (master_editable_list, gender-filtered).
    std::vector<uint32_t> male = car::editableSlots(g, false);
    std::vector<uint32_t> female = car::editableSlots(g, true);
    check(!male.empty(), "no editable slots for a male character");
    check(!female.empty(), "no editable slots for a female character");
    // The common slots are in both; the gendered slots split; neither carries
    // the other gender's CAS_*_ body parts.
    auto has = [](const std::vector<uint32_t>& v, uint32_t k) {
        for (uint32_t x : v) if (x == k) return true;
        return false;
    };
    check(has(male, K("cas_body")) && has(female, K("cas_body")), "cas_body missing from an editable set");
    check(has(male, K("cas_physique")) && has(female, K("cas_physique")), "cas_physique missing");
    check(has(male, K("CAS_Male_Torso")) && !has(male, K("CAS_Female_Torso")), "male torso slot wrong");
    check(has(female, K("CAS_Female_Torso")) && !has(female, K("CAS_Male_Torso")), "female torso slot wrong");
    check(has(male, K("CAS_Guitar_Body")) && has(female, K("CAS_Guitar_Body")), "guitar body slot missing");
    std::printf("castest: editable slots — male %zu, female %zu (master_editable_list, gender-filtered)\n", male.size(), female.size());

    // (b) Create a male character from the default profile.
    qb::Struct savegame;
    const uint32_t c0 = car::newCharacter(g, savegame, /*female=*/false);
    check(c0 == K("custom_character_0"), "first created id is not custom_character_0: " + name(c0));
    check(car::count(savegame) == 1, "count != 1 after first create");
    check(car::profile(savegame, c0) != nullptr, "created profile not found");
    check(!car::isFemale(savegame, c0), "a male character reads as female");
    // The default appearance carries the default instrument + physique.
    check(car::partOf(savegame, c0, K("cas_physique")) == K("MalePhysique"), "default physique not MalePhysique");
    check(car::partOf(savegame, c0, K("CAS_Guitar_Body")) != 0, "default appearance has no guitar body");
    std::printf("castest: created %s (male); default guitar body = %s\n", name(c0).c_str(),
                name(car::partOf(savegame, c0, K("CAS_Guitar_Body"))).c_str());

    // (c) Unique naming: two more get custom_character_1 and _2.
    const uint32_t c1 = car::newCharacter(g, savegame, false);
    const uint32_t c2 = car::newCharacter(g, savegame, true);
    check(c1 == K("custom_character_1") && c2 == K("custom_character_2"), "created ids not sequential/unique");
    check(car::count(savegame) == 3, "count != 3 after three creates");
    check(car::isFemale(savegame, c2), "the female character does not read as female");

    // (d) Slot edits on the male character. A non-editable slot (the other
    // gender's) is refused; a bogus option is refused; a real default option
    // (no save context) is accepted.
    check(car::setPart(g, savegame, c0, K("CAS_Female_Torso"), firstMeshOption(g, K("CAS_Female_Torso")), nullptr) == car::Set::NotEditable,
          "a female torso on a male character was not refused");
    check(car::setPart(g, savegame, c0, K("CAS_Male_Torso"), K("a_part_that_does_not_exist_zzz"), nullptr) == car::Set::NoSuchOption,
          "a bogus option was not refused");
    // Build the body from real options (no ownership context = default parts).
    int bodySet = 0;
    for (const char* p : {"CAS_Male_Hair", "CAS_Male_Torso", "CAS_Male_Legs", "CAS_Male_Shoes"}) {
        const uint32_t opt = firstMeshOption(g, K(p));
        if (opt && car::setPart(g, savegame, c0, K(p), opt, nullptr) == car::Set::Ok) ++bodySet;
    }
    check(bodySet > 0, "could not set any male body slot from real options");
    check(car::partOf(savegame, c0, K("CAS_Male_Torso")) == firstMeshOption(g, K("CAS_Male_Torso")), "torso slot did not persist in memory");

    // (e) Ownership gate on the female character, via the store catalog.
    std::vector<shop::Item> cat = shop::catalog(g);
    const shop::Item* buyable = nullptr;
    const shop::Item* gated = nullptr;
    for (const shop::Item& it : cat) {
        if (!car::isEditableSlot(g, true, it.part)) continue;  // a female-editable slot
        if (it.priced && !it.locked && it.price > 0 && !buyable) buyable = &it;
        if (it.priced && it.locked && it.price > 0 && !gated) gated = &it;
    }
    if (check(buyable != nullptr, "no priced, free-to-buy item on a female-editable slot")) {
        // Not owned yet -> refused.
        qb::Struct bi = bandInfoWithCash(buyable->price + 500);
        check(car::setPart(g, savegame, c2, buyable->part, buyable->descId, &bi) == car::Set::NotOwned,
              "an unpurchased priced option was accepted");
        check(car::partOf(savegame, c2, buyable->part) != buyable->descId, "a refused slot was written");
        // Buy it, then it sets.
        check(shop::purchase(bi, *buyable) == shop::Buy::Ok, "could not buy the option");
        check(car::setPart(g, savegame, c2, buyable->part, buyable->descId, &bi) == car::Set::Ok,
              "an owned option was refused");
        check(car::partOf(savegame, c2, buyable->part) == buyable->descId, "the bought slot did not persist");
        std::printf("castest: female %s slot %s set to bought option %s ($%d)\n", name(c2).c_str(),
                    name(buyable->part).c_str(), name(buyable->descId).c_str(), buyable->price);
    }
    if (gated) {
        qb::Struct bi = bandInfoWithCash(gated->price + 1000);
        check(car::setPart(g, savegame, c2, gated->part, gated->descId, &bi) == car::Set::NotOwned,
              "a career-gated option was accepted before unlock");
        shop::grantUnlock(bi, *gated);
        check(shop::purchase(bi, *gated) == shop::Buy::Ok, "could not buy the gated option after unlock");
        check(car::setPart(g, savegame, c2, gated->part, gated->descId, &bi) == car::Set::Ok,
              "a gated option was refused after unlock+purchase");
        std::printf("castest: career-gated %s/%s accepted only after unlock+purchase\n", name(gated->part).c_str(), name(gated->descId).c_str());
    }

    // (f) Name the character.
    check(car::setFullName(savegame, c0, "Riff Dream"), "setFullName failed");
    check(car::fullName(savegame, c0) == "Riff Dream", "fullName did not round-trip in memory");

    // (g) Save round-trip: the created characters persist. Capture c2's bought
    // slot's current value (the free-to-buy and career-gated picks may share a
    // slot, so the last write wins) and assert that survives.
    const uint32_t c2slot = buyable ? buyable->part : 0;
    const uint32_t c2val = c2slot ? car::partOf(savegame, c2, c2slot) : 0;
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "gd07_castest";
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        save::setFolder(dir.string() + "/");
        save::Tags tags;
        tags[K("savegame")] = savegame;  // the savegame root (holds custom_profiles)
        std::string err;
        check(save::write(tags, save::Sections{}, err), "save write failed: " + err);
        save::Tags rd;
        save::Sections rs;
        if (check(save::read(rd, rs, err), "save read failed: " + err)) {
            auto it = rd.find(K("savegame"));
            if (check(it != rd.end(), "savegame tag not in the save")) {
                const qb::Struct& sg = it->second;
                check(car::count(sg) == 3, "custom_profiles count wrong after save round-trip");
                check(car::profile(sg, c0) != nullptr, "character 0 gone after save round-trip");
                check(car::partOf(sg, c0, K("CAS_Male_Torso")) == firstMeshOption(g, K("CAS_Male_Torso")), "torso slot gone after save round-trip");
                check(car::fullName(sg, c0) == "Riff Dream", "name gone after save round-trip");
                if (c2slot) check(car::partOf(sg, c2, c2slot) == c2val, "an edited slot gone after save round-trip");
                std::printf("castest: save round-trip ok — 3 characters, slots + name persisted\n");
            }
        }
        save::setFolder("");
        fs::remove_all(dir, ec);
    }

    // (h) Used in-game: the created profile builds a playable band member.
    {
        const qb::Struct* p = car::profile(savegame, c0);
        band::Member m;
        const bool built = p && band::memberGh4(g, *p, K("guitarist"), K("guitar"), 0, m);
        check(built, "memberGh4 could not build the created character");
        check(built && !m.meshes.empty(), "the created character has no meshes (not renderable)");
        if (built)
            std::printf("castest: in-game — %s builds a playable guitarist (%zu meshes, skeleton %s)\n",
                        name(c0).c_str(), m.meshes.size(), name(m.skeleton).c_str());
    }

    // (i) Delete + id reuse: deleting character 0 frees custom_character_0 for
    // the next create (new_custom_character_name picks the first free id).
    check(car::deleteCharacter(savegame, c0), "deleteCharacter failed");
    check(car::count(savegame) == 2, "count != 2 after delete");
    check(car::profile(savegame, c0) == nullptr, "deleted character still present");
    const uint32_t reuse = car::newCharacter(g, savegame, false);
    check(reuse == c0, "freed id custom_character_0 was not reused: " + name(reuse));
    std::printf("castest: delete + reuse ok — custom_character_0 freed and reclaimed\n");

    std::printf("castest: %s (%d checks failed)\n", failures ? "FAIL" : "all create-a-rocker checks passed", failures);
    return failures ? 1 : 0;
}
