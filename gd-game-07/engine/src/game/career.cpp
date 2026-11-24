// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/career.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "data/qbkey.h"
#include "game/shop.h"
#include "script/globals.h"

namespace career {

namespace {

uint32_t K(std::string_view s) { return qbkey::key(s); }

uint32_t keyOf(const qb::Value* v) {
    return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr || v->type == qb::Type::QsKey) ? v->key : 0;
}
int intOf(const qb::Value* v, int dflt = 0) { return v && v->type == qb::Type::Int ? v->i : dflt; }

std::string nameOf(uint32_t k) {
    const std::string* n = qbkey::names().find(k);
    return n ? *n : ("0x" + std::to_string(k));
}

// A struct carries flag `key`: a named item, or an unnamed bare Key item (the
// way $end_with_credits / { unlocked } are stored).
bool hasFlag(const qb::Struct& s, uint32_t key) {
    for (const qb::Item& it : s.items) {
        if (it.name == key) return true;
        if (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && it.value.key == key) return true;
    }
    return false;
}

const char* kPartText[kNumParts] = {"band", "guitar", "bass", "drums", "vocals"};
const char* kDiffText[5] = {"beginner", "easy", "medium", "hard", "expert"};

// ---- tag-store access (named global tags, the save's model) ----
qb::Struct& tag(TagStore& st, uint32_t name) { return st[name]; }
const qb::Struct* ctag(const TagStore& st, uint32_t name) {
    auto it = st.find(name);
    return it == st.end() ? nullptr : &it->second;
}
qb::Value& ensureField(qb::Struct& s, uint32_t name, qb::Type t) {
    for (qb::Item& it : s.items)
        if (it.name == name) return it.value;
    qb::Item it;
    it.name = name;
    it.value.type = t;
    s.items.push_back(it);
    return s.items.back().value;
}
void setInt(qb::Struct& s, uint32_t name, int v) {
    qb::Value& f = ensureField(s, name, qb::Type::Int);
    f.type = qb::Type::Int;
    f.i = v;
}
int getInt(const qb::Struct& s, uint32_t name, int dflt = 0) { return intOf(s.find(name), dflt); }
void setFlag(qb::Struct& s, uint32_t key) {
    if (!hasFlag(s, key)) setInt(s, key, 1);
}

// LevelZones.<level>.name -> here approximated by stripping the "load_" prefix
// off the level key's own name (load_z_hob -> z_hob); the venue flag is
// venue_<zone> (SaveGigComplete). [I] the exact LevelZones.name is not read.
std::string zoneOf(uint32_t level) {
    std::string n = nameOf(level);
    const std::string pfx = "load_";
    if (n.rfind(pfx, 0) == 0) n = n.substr(pfx.size());
    return n;
}

// Read a tier from a GH4_Career_<Part>_Songs tier struct.
Tier readTier(int num, const qb::Struct& ts) {
    Tier t;
    t.num = num;
    if (const qb::Value* songs = ts.find(K("songs")); songs && songs->type == qb::Type::Array && songs->arr)
        for (const qb::Value& s : songs->arr->values)
            if (uint32_t k = keyOf(&s)) t.songs.push_back(k);
    t.encore = keyOf(ts.find(K("encorep1")));
    t.level = keyOf(ts.find(K("level")));
    t.poster = keyOf(ts.find(K("poster_texture")));
    t.genre = keyOf(ts.find(K("genre")));
    t.defaultUnlocked = getInt(ts, K("defaultunlocked"));
    t.paytoplay = getInt(ts, K("paytoplay"));
    t.endWithCredits = hasFlag(ts, K("end_with_credits"));
    t.createAGig = hasFlag(ts, K("createagig"));
    t.debugGig = hasFlag(ts, K("debug_gig"));
    return t;
}

GigRef readGigRef(const qb::Struct& g) {
    GigRef r;
    r.name = keyOf(g.find(K("name")));
    r.num = getInt(g, K("num"));
    const qb::Value* fp = g.find(K("forced_pos"));
    r.forcedPos = (fp && fp->type == qb::Type::Int) ? fp->i : -1;
    r.debugDontUnlock = hasFlag(g, K("DEBUG_DONT_UNLOCK"));
    return r;
}

// Read every gig entry (gigN / special_eventN / pay_to_playN / None) of one set.
UnlockSet readUnlockSet(const Globals& g, const qb::Struct& set) {
    UnlockSet u;
    for (const qb::Item& it : set.items) {
        if (it.name == K("none")) {
            u.hasNone = true;
            continue;
        }
        if (it.name == 0 && (it.value.type == qb::Type::Key) && it.value.key == K("none")) {
            u.hasNone = true;
            continue;
        }
        if (it.value.type == qb::Type::Struct && it.value.st) u.gigs.push_back(readGigRef(*it.value.st));
    }
    (void)g;
    return u;
}

void readUnlockOrder(const Globals& g, Career& c, const qb::Struct& order) {
    // freegigs + freegigs_last start unlocked; unlockset1..N open progressively.
    for (const qb::Item& it : order.items) {
        const std::string n = nameOf(it.name);
        if (it.value.type != qb::Type::Struct || !it.value.st) continue;
        if (n == "freegigs" || n == "freegigs_last")
            c.freegigs.push_back(readUnlockSet(g, *it.value.st));
    }
    for (int i = 1;; ++i) {
        const uint32_t k = K("unlockset" + std::to_string(i));
        const qb::Value* v = order.find(k);
        if (!v || v->type != qb::Type::Struct || !v->st) break;
        c.unlockSets.push_back(readUnlockSet(g, *v->st));
    }
}

void readRewards(const Globals& g, Career& c, const char* arrName) {
    const qb::Value* v = g.find(K(arrName));
    if (!v || v->type != qb::Type::Array || !v->arr) return;
    for (const qb::Value& e : v->arr->values) {
        Reward r;
        if (e.type == qb::Type::Struct && e.st) {
            r.groupName = keyOf(e.st->find(K("group_name")));
            if (const qb::Value* cp = e.st->find(K("car_pieces")); cp && cp->type == qb::Type::Array && cp->arr)
                for (const qb::Value& p : cp->arr->values)
                    if (p.type == qb::Type::Struct && p.st)
                        r.carPieces.emplace_back(keyOf(p.st->find(K("part"))), keyOf(p.st->find(K("desc_id"))));
            if (const qb::Value* ch = e.st->find(K("characters")); ch && ch->type == qb::Type::Array && ch->arr)
                for (const qb::Value& x : ch->arr->values)
                    if (uint32_t k = keyOf(&x)) r.characters.push_back(k);
            if (const qb::Value* so = e.st->find(K("songs")); so && so->type == qb::Type::Array && so->arr)
                for (const qb::Value& x : so->arr->values)
                    if (uint32_t k = keyOf(&x)) r.songs.push_back(k);
        }
        c.rewards.push_back(std::move(r));  // empty {} entries kept so the index aligns to the gig
    }
}

std::string movieName(const qb::Value* v) {
    if (!v) return {};
    if (v->type == qb::Type::String || v->type == qb::Type::WString) return v->str;
    if (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr || v->type == qb::Type::QsKey) return nameOf(v->key);
    return {};
}

struct PartDef {
    Part part;
    const char* flag;
    const char* partKey;
    const char* songs;
    const char* order;
    const char* rewards;
};
const PartDef kParts[kNumParts] = {
    {Part::Band, "Career_Band", "band", "GH4_Career_Band_Songs", "GH4_Career_Band_Progression_Unlock_Order", "progression_band_rewards"},
    {Part::Guitar, "Career_Guitar", "guitar", "GH4_Career_Guitar_Songs", "GH4_Career_Guitar_Progression_Unlock_Order", "progression_guitar_rewards"},
    {Part::Bass, "Career_Bass", "Bass", "GH4_Career_Bass_Songs", "GH4_Career_Bass_Progression_Unlock_Order", "progression_bass_rewards"},
    {Part::Drums, "Career_Drum", "drum", "GH4_Career_Drum_Songs", "GH4_Career_Drum_Progression_Unlock_Order", "progression_drum_rewards"},
    {Part::Vocals, "Career_Vocals", "Vocals", "GH4_Career_Vocals_Songs", "GH4_Career_Vocals_Progression_Unlock_Order", "progression_vocals_rewards"},
};

}  // namespace

const char* partName(Part p) { return kPartText[int(p)]; }

const Tier* Career::tier(int num) const {
    if (num < 1 || num >= int(tiers.size())) return nullptr;
    const Tier& t = tiers[num];
    return t.num == num ? &t : nullptr;
}

std::vector<Career> load(const Globals& g) {
    std::vector<Career> out;
    for (const PartDef& pd : kParts) {
        const qb::Value* songs = g.find(K(pd.songs));
        if (!songs || songs->type != qb::Type::Struct || !songs->st) continue;
        Career c;
        c.part = pd.part;
        c.flag = K(pd.flag);
        c.partKey = K(pd.partKey);
        const qb::Struct& s = *songs->st;
        if (const qb::Value* p = s.find(K("prefix"))) c.prefix = (p->type == qb::Type::String) ? p->str : nameOf(keyOf(p));
        c.numTiers = getInt(s, K("num_tiers"));
        c.initialMovie = movieName(s.find(K("initial_movie")));
        c.endMovie = movieName(s.find(K("end_movie")));
        c.tiers.resize(c.numTiers + 1);
        for (int i = 1; i <= c.numTiers; ++i)
            if (const qb::Value* t = s.find(K("tier" + std::to_string(i))); t && t->type == qb::Type::Struct && t->st)
                c.tiers[i] = readTier(i, *t->st);
        if (const qb::Value* o = g.find(K(pd.order)); o && o->type == qb::Type::Struct && o->st)
            readUnlockOrder(g, c, *o->st);
        readRewards(g, c, pd.rewards);
        out.push_back(std::move(c));
    }
    return out;
}

const Career* find(const std::vector<Career>& cs, Part p) {
    for (const Career& c : cs)
        if (c.part == p) return &c;
    return nullptr;
}

std::vector<Movie> movies(const Globals& g) {
    std::vector<Movie> out;
    const qb::Value* v = g.find(K("Bonus_Videos"));
    if (!v || v->type != qb::Type::Array || !v->arr) return out;
    for (const qb::Value& e : v->arr->values) {
        if (e.type != qb::Type::Struct || !e.st) continue;
        Movie m;
        m.id = keyOf(e.st->find(K("id")));
        m.name = movieName(e.st->find(K("movie")));
        m.defaultUnlocked = hasFlag(*e.st, K("unlocked"));
        out.push_back(std::move(m));
    }
    return out;
}

uint32_t gigNameKey(const Career& c, int gigNum) {
    const std::string s = c.prefix + "_gig" + std::to_string(gigNum);
    const uint32_t k = K(s);
    qbkey::names().add(k, s);
    return k;
}

uint32_t songChecksumKey(const Career& c, uint32_t song, int diffIdx) {
    const int d = (diffIdx < 0 || diffIdx > 4) ? 4 : diffIdx;
    // The disc builds '%s%p%d' (song_name + part_text + difficulty_text_nl). The
    // port's save is its own format, so the component tokens are the port's (the
    // console's exact localized text_nl is [I], not needed to match).
    const std::string s = nameOf(song) + "_" + kPartText[int(c.part)] + "_" + kDiffText[d];
    const uint32_t k = K(s);
    qbkey::names().add(k, s);
    return k;
}

int cashForSong(const Globals& g, const Performance& p) {
    const qb::Value* cm = g.find(K("cash_milestones"));
    if (!cm || cm->type != qb::Type::Struct || !cm->st) return 0;
    const int d = (p.difficultyIndex < 0 || p.difficultyIndex > 4) ? 4 : p.difficultyIndex;
    auto base = [&](const char* name) -> int {
        const qb::Value* m = cm->st->find(K(name));
        if (!m || m->type != qb::Type::Struct || !m->st) return 0;
        const qb::Value* arr = m->st->find(K("cash"));
        if (!arr || arr->type != qb::Type::Array || !arr->arr || d >= int(arr->arr->values.size())) return 0;
        return intOf(&arr->arr->values[d]);
    };
    int total = 0;
    auto add = [&](int reward) { if (reward > 0) total += reward; };
    if (p.stars == 3) add(base("stars_3_bonus"));
    else if (p.stars == 4) add(base("stars_4_bonus"));
    else if (p.stars >= 5) add(base("stars_5_bonus"));
    if (p.perfect) add(base("perfect_bonus"));
    if (p.scoreImproved) add(base("Score"));
    if (p.neverInRed) add(base("never_in_red"));
    if (p.neverUsedStarPower) add(base("never_used_star_power"));
    if (p.fullStarPower) add(base("full_star_power"));
    if (p.noteStreak > 0) add(base("note_streak") * (p.noteStreak / 25));  // multiplier = best_run/25
    return total;
}

void startCareer(TagStore& st, qb::Struct& bandInfo, const Career& c) {
    if (!bandInfo.find(K("cash"))) setInt(bandInfo, K("cash"), 0);
    if (!bandInfo.find(K("career_earnings"))) setInt(bandInfo, K("career_earnings"), 0);
    for (const UnlockSet& set : c.freegigs)
        for (const GigRef& gr : set.gigs) {
            if (gr.debugDontUnlock || gr.num <= 0) continue;
            qb::Struct& gt = tag(st, gigNameKey(c, gr.num));
            setInt(gt, K("unlocked"), 1);
            if (const Tier* t = c.tier(gr.num))
                for (uint32_t song : t->songs) setInt(tag(st, songChecksumKey(c, song, 0)), K("unlocked"), 1);
        }
}

bool gigUnlocked(const TagStore& st, const Career& c, int gigNum) {
    const qb::Struct* t = ctag(st, gigNameKey(const_cast<Career&>(c), gigNum));
    return t && getInt(*t, K("unlocked")) == 1;
}
bool gigStarted(const TagStore& st, const Career& c, int gigNum) {
    const qb::Struct* t = ctag(st, gigNameKey(const_cast<Career&>(c), gigNum));
    return t && getInt(*t, K("started")) == 1;
}
bool gigCompleted(const TagStore& st, const Career& c, int gigNum) {
    const qb::Struct* t = ctag(st, gigNameKey(const_cast<Career&>(c), gigNum));
    return t && getInt(*t, K("completed")) >= 1;
}

int songStars(const TagStore& st, const Career& c, uint32_t song) {
    int best = 0;
    for (int d = 0; d < 5; ++d)
        if (const qb::Struct* t = ctag(st, songChecksumKey(const_cast<Career&>(c), song, d)))
            best = std::max(best, getInt(*t, K("stars")));
    return best;
}
bool songUnlocked(const TagStore& st, const Career& c, uint32_t song) {
    for (int d = 0; d < 5; ++d)
        if (const qb::Struct* t = ctag(st, songChecksumKey(const_cast<Career&>(c), song, d)))
            if (getInt(*t, K("unlocked")) == 1) return true;
    return false;
}

bool isGigComplete(const TagStore& st, const Career& c, int gigNum) {
    const Tier* t = c.tier(gigNum);
    if (!t || t->songs.empty()) return false;
    for (uint32_t song : t->songs)
        if (songStars(st, c, song) <= 2) return false;  // > 2 stars required (Check_Gig_Complete)
    return true;
}

int gigsCompleted(const TagStore& st, const Career& c) {
    int n = 0;
    for (int i = 1; i <= c.numTiers; ++i)
        if (gigCompleted(st, c, i)) ++n;
    return n;
}

SongWonResult recordSongResult(TagStore& st, qb::Struct& bandInfo, const Globals& g,
                               const Career& c, uint32_t song, const Performance& p) {
    SongWonResult r;
    qb::Struct& songTag = tag(st, songChecksumKey(const_cast<Career&>(c), song, p.difficultyIndex));
    const int oldStars = getInt(songTag, K("stars"));
    const int oldScore = getInt(songTag, K("Score"));
    r.newStars = std::max(oldStars, p.stars);
    if (p.stars > oldStars) {
        setInt(songTag, K("stars"), p.stars);
        r.starsImproved = true;
    }
    if (p.score > oldScore) setInt(songTag, K("Score"), p.score);
    setInt(songTag, K("unlocked"), 1);  // the played song is unlocked (SongWon)

    // Progression_CashMilestonesSongWon -> Progression_AwardCashMilestones: the
    // milestone sum goes into band_info.cash and career_earnings.
    r.cashAwarded = cashForSong(g, p);
    if (r.cashAwarded > 0) {
        setInt(bandInfo, K("cash"), getInt(bandInfo, K("cash")) + r.cashAwarded);
        setInt(bandInfo, K("career_earnings"), getInt(bandInfo, K("career_earnings")) + r.cashAwarded);
    }
    return r;
}

GigCompleteResult completeGig(TagStore& st, qb::Struct& bandInfo, const Globals& g,
                              const Career& c, int gigNum) {
    (void)g;
    GigCompleteResult res;
    qb::Struct& gt = tag(st, gigNameKey(const_cast<Career&>(c), gigNum));
    setInt(gt, K("started"), 1);
    const int completed = getInt(gt, K("completed"));
    if (completed == 0) {
        setInt(gt, K("completed"), 1);
        res.gigNewlyCompleted = true;
    } else if (completed == -1) {
        setInt(gt, K("completed"), 2);
    }

    const Tier* t = c.tier(gigNum);
    if (t && t->level) {
        // SaveGigComplete: venue_<zone> and <zone>_<part> unlocked.
        const std::string zone = zoneOf(t->level);
        const uint32_t vk = K("venue_" + zone);
        qbkey::names().add(vk, "venue_" + zone);
        setInt(tag(st, vk), K("unlocked"), 1);
        res.venueFlag = vk;
        setInt(tag(st, K(zone + "_" + kPartText[int(c.part)])), K("unlocked"), 1);
    }

    if (!res.gigNewlyCompleted) return res;

    // Progression_Career_Gig_Complete: open the next not-yet-unlocked unlockset
    // (its gigs + those tiers' songs). One set per completed gig.
    for (const UnlockSet& set : c.unlockSets) {
        bool allUnlocked = true;
        for (const GigRef& gr : set.gigs)
            if (gr.num > 0 && !gr.debugDontUnlock && !gigUnlocked(st, c, gr.num)) allUnlocked = false;
        if (set.gigs.empty()) allUnlocked = true;
        if (allUnlocked) continue;
        bool opened = false;
        for (const GigRef& gr : set.gigs) {
            if (gr.num <= 0 || gr.debugDontUnlock || gigUnlocked(st, c, gr.num)) continue;
            qb::Struct& ngt = tag(st, gigNameKey(const_cast<Career&>(c), gr.num));
            setInt(ngt, K("unlocked"), 1);
            setInt(ngt, K("first_time_unlocked"), 1);
            res.unlockedGigs.push_back(gr.num);
            opened = true;
            if (const Tier* nt = c.tier(gr.num))
                for (uint32_t song : nt->songs) {
                    setInt(tag(st, songChecksumKey(const_cast<Career&>(c), song, 0)), K("unlocked"), 1);
                    res.unlockedSongs.push_back(song);
                }
        }
        if (opened) break;  // set_got_unlocked -> return
    }

    // progression_add_new_unlocks: the reward for this many completed gigs
    // (count-1 indexes progression_<part>_rewards). car_pieces become band_info
    // `unlocked` flags the store reads; characters/songs are recorded too.
    const int count = gigsCompleted(st, c);
    const int idx = count - 1;
    if (idx >= 0 && idx < int(c.rewards.size())) {
        const Reward& rw = c.rewards[idx];
        for (const auto& [part, desc] : rw.carPieces) {
            if (!part || !desc) continue;
            if (!shop::partUnlockedFlag(bandInfo, part, desc)) {
                shop::grantUnlockPart(bandInfo, part, desc);
                res.unlockedCarPieces.emplace_back(part, desc);
            }
        }
        for (uint32_t ch : rw.characters) res.unlockedCharacters.push_back(ch);
        for (uint32_t so : rw.songs) {
            setInt(tag(st, songChecksumKey(const_cast<Career&>(c), so, 0)), K("unlocked"), 1);
            res.unlockedSongs.push_back(so);
        }
    }

    res.endCredits = t && t->endWithCredits && isGigComplete(st, c, gigNum);
    return res;
}

}  // namespace career
