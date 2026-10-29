// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/glitches.h"

#include <SDL3/SDL_log.h>

#include <string>
#include <vector>

#include "data/qbkey.h"
#include "script/vm.h"

namespace {

using qs::Vm;
uint32_t K(const std::string& s) { return qbkey::key(s); }

constexpr int kBand = 5;  // num_career_bands [C data]
const char* const kDiffs[] = {"easy", "medium", "hard", "expert"};

bool setInt(qb::Struct& st, const char* name, int v) {
    const qb::Value* cur = Vm::param(st, K(name));
    if (cur && cur->type == qb::Type::Int && cur->i == v) return false;
    Vm::set(st, K(name), Vm::makeInt(v));
    return true;
}

// A song list's tiers and songs in `tags` (a band section or the live tags),
// as GlobalTags_UnlockAll sets them [C script].
bool unlockList(Vm& vm, std::unordered_map<uint32_t, qb::Struct>& tags, const char* list, bool songsOnly, bool onlyExisting) {
    const qb::Value* l = vm.global(K(list));
    if (!l || !l->st) return false;
    const qb::Value* prefix = l->st->find(K("prefix"));
    const qb::Value* tiers = l->st->find(K("num_tiers"));
    if (!prefix || !tiers) return false;
    bool changed = false;
    for (int t = 1; t <= tiers->i; ++t) {
        const qb::Value* tier = l->st->find(K("tier" + std::to_string(t)));
        const qb::Value* songs = tier && tier->st ? tier->st->find(K("songs")) : nullptr;
        if (!songs || !songs->arr) continue;
        const uint32_t tierKey = K(prefix->str + "tier" + std::to_string(t));
        if (!onlyExisting || tags.count(tierKey)) {
            qb::Struct& ts = tags[tierKey];
            for (const char* f : {"unlocked", "complete", "encore_unlocked", "boss_unlocked"}) changed |= setInt(ts, f, 1);
            changed |= setInt(ts, "num_songs_to_progress", 0);
        }
        for (size_t s = 1; s <= songs->arr->values.size(); ++s) {
            const uint32_t sk = K(prefix->str + "_song" + std::to_string(s) + "_tier" + std::to_string(t));
            if (onlyExisting && !tags.count(sk)) continue;
            qb::Struct& ss = tags[sk];
            changed |= setInt(ss, "unlocked", 1);
            if (!songsOnly) changed |= setInt(ss, "stars", 5), changed |= setInt(ss, "score", 1000000);
        }
    }
    return changed;
}

}  // namespace

bool ensureGlitches(Vm& vm) {
    bool changed = false;
    for (const char* mode : {"p1_career", "p2_career"}) {
        // The band slot: named The Glitches unless the player already uses it.
        const uint32_t info = K("band" + std::to_string(kBand) + "_info_" + mode);
        qb::Struct& bi = vm.globalTags[info];
        const qb::Value* name = Vm::param(bi, K("name"));
        const bool empty = !name || name->str.empty();
        if (!empty && name->str != "The Glitches") continue;
        if (empty) {
            qb::Value n;
            n.type = qb::Type::WString;
            n.str = "The Glitches";
            Vm::set(bi, K("name"), n);
            changed = true;
        }
        changed |= setInt(bi, "cash", 1000000);
        changed |= setInt(bi, "first_play", 0);
        // Its career at every difficulty (the band's sections, push_bandtags) [C script].
        const char* list = std::string(mode) == "p1_career" ? "GH3_Career_Songs" : "GH3_CoopCareer_Songs";
        for (const char* d : kDiffs) {
            const uint32_t sec = K(std::string(mode) + "_band" + std::to_string(kBand) + "_" + d);
            auto it = vm.globalTagSections.find(sec);
            if (it == vm.globalTagSections.end()) continue;  // the save's sections come with setup_bandtags
            changed |= unlockList(vm, it->second, list, false, true);
        }
        // The band popped into the live tags now (progression_pop_current) gets them there too.
        const qb::Value* popped = vm.global(K("progression_pop_count"));
        const qb::Value* gm = vm.global(K("game_mode"));
        const auto prog = vm.globalTags.find(K("Progression"));
        const qb::Value* cur = prog != vm.globalTags.end() ? Vm::param(prog->second, K("current_band")) : nullptr;
        if (popped && popped->i == 1 && cur && cur->i == kBand && gm && gm->key == K(mode))
            changed |= unlockList(vm, vm.globalTags, list, false, true);
    }
    // Save-wide: the quick play songs the career unlocks (GlobalTags_UnlockAll's songs part) [C script].
    changed |= unlockList(vm, vm.globalTags, "GH3_General_Songs", true, false);
    changed |= unlockList(vm, vm.globalTags, "GH3_GeneralP2_Songs", true, false);
    if (changed) SDL_Log("The Glitches: band %d, everything unlocked", kBand);
    return changed;
}

namespace {

struct StoreState {
    bool active = false;
    // The save's own store tags (absent ones as `present = false`).
    struct Kept {
        bool present = false;
        qb::Struct tag;
    };
    std::unordered_map<uint32_t, Kept> kept;
};
StoreState& store() {
    static StoreState s;
    return s;
}

// The Vault's tags and whether "bought" also sets unlocked_for_purchase [C scripts store_*_buy, GlobalTags_UnlockAll].
std::vector<std::pair<uint32_t, bool>> storeTags(Vm& vm) {
    std::vector<std::pair<uint32_t, bool>> out;
    for (const char* arr : {"Bonus_Guitars", "Bonus_Guitar_Finishes", "Secret_Guitars", "Bonus_Basses", "Bonus_Bass_Finishes", "Secret_Basses",
                            "Secret_Characters", "Bonus_Outfits", "Bonus_Styles", "Bonus_videos"}) {
        const qb::Value* a = vm.global(K(arr));
        if (!a || !a->arr) continue;
        const bool purchase = std::string(arr).find("Guitar") != std::string::npos || std::string(arr).find("Bass") != std::string::npos;
        for (const qb::Value& e : a->arr->values)
            if (const qb::Value* id = e.st ? e.st->find(K("id")) : nullptr) out.push_back({id->key, purchase});
    }
    // The bonus songs: each song's own tag and its bonus_song<n>_tier1 (store_songs_buy).
    if (const qb::Value* l = vm.global(K("GH3_Bonus_Songs")); l && l->st)
        if (const qb::Value* t = l->st->find(K("tier1")); t && t->st)
            if (const qb::Value* songs = t->st->find(K("songs")); songs && songs->arr)
                for (size_t i = 0; i < songs->arr->values.size(); ++i) {
                    out.push_back({songs->arr->values[i].key, false});
                    out.push_back({K("bonus_song" + std::to_string(i + 1) + "_tier1"), false});
                }
    return out;
}

bool glitchesSection(uint32_t sec) {
    for (const char* mode : {"p1_career", "p2_career"})
        for (const char* d : kDiffs)
            if (sec == K(std::string(mode) + "_band" + std::to_string(kBand) + "_" + d)) return true;
    return false;
}

bool bandSection(uint32_t sec) {
    for (const char* mode : {"p1_career", "p2_career"})
        for (int b = 1; b <= kBand; ++b)
            for (const char* d : kDiffs)
                if (sec == K(std::string(mode) + "_band" + std::to_string(b) + "_" + d)) return true;
    return false;
}

void activate(Vm& vm) {
    StoreState& st = store();
    if (st.active) return;
    st.kept.clear();
    for (const auto& [key, purchase] : storeTags(vm)) {
        auto it = vm.globalTags.find(key);
        st.kept[key] = StoreState::Kept{it != vm.globalTags.end(), it != vm.globalTags.end() ? it->second : qb::Struct{}};
        qb::Struct& t = vm.globalTags[key];
        setInt(t, "unlocked", 1);
        if (purchase) setInt(t, "unlocked_for_purchase", 1);
    }
    st.active = true;
    SDL_Log("The Glitches: the Vault reads all bought for their band");
}

void restore(Vm& vm) {
    StoreState& st = store();
    if (!st.active) return;
    for (const auto& [key, k] : st.kept) {
        if (k.present) vm.globalTags[key] = k.tag;
        else vm.globalTags.erase(key);
    }
    st.kept.clear();
    st.active = false;
}

}  // namespace

void installGlitchesStore(Vm& vm) {
    store() = StoreState{};
    vm.onTagSection = [](Vm& v, uint32_t sec, bool pop) {
        if (pop && glitchesSection(sec)) activate(v);
        else if ((pop && bandSection(sec)) || (!pop && glitchesSection(sec))) restore(v);
    };
}

std::unordered_map<uint32_t, qb::Struct> glitchesSaveView(const Vm& vm) {
    std::unordered_map<uint32_t, qb::Struct> tags = vm.globalTags;
    const StoreState& st = store();
    if (st.active)
        for (const auto& [key, k] : st.kept) {
            if (k.present) tags[key] = k.tag;
            else tags.erase(key);
        }
    return tags;
}
