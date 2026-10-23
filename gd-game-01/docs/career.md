# GH1 (PS2, SLUS_212.24) career mode: `campaign`, store, unlocks and save data

[C] = read from the decompilation (`analysis/decomp/*.c`), the ELF bytes or the decoded scripts.
[I] = inference. Addresses are Ghidra addresses in SLUS_212.24.
Globals: `DAT_002edbe8` = the Campaign singleton, `DAT_002edb88` = GameConfig (`game`)
(+0x90 difficulty 0..3, +0x94 venue symbol, +0x98 song symbol), `DAT_002edb68` = Options.

Scripts were flattened with a small DTA pretty-printer (my scratchpad `compact.py`). Script files:
`config/gen/{campaign,store,songs,guitars,characters,mc,cheats,cheats_funcs,long_cheats,system_script}.dta`,
`ghui/gen/{career,endgame,splash,main,mem_card,options,pause,lose,loading,game,tutorials}.dta`,
`arena/gen/arena_game.dta`.

---------------------------------------------------------------------------------------------
## 0. Summary of the rules (the parts you need to match 1:1)

- Tiers = `campaign.dta (order ...)`: basement, small_club, big_club, theatre, fest, arena, 5 songs each. [C]
- **Easy has only 5 tiers** (arena is excluded); Medium/Hard/Expert have 6 (`FUN_0013a788`: count−1 when difficulty==0). [C]
- A tier is **complete** when the number of *beaten* songs in it ≥ `required_songs[diff]` (Easy 4, Medium 4, Hard 5, Expert 5),
  except the **last tier of that difficulty needs all 5** (`FUN_0013b6c8`). [C]
- The first time a song is beaten (= `finish_song`, i.e. the song was passed), if its tier becomes complete the
  **next tier and its 5 songs are unlocked** (`FUN_0013e130`). The last tier has no successor. [C]
- **Career won** = beating the 5th (last unbeaten) song of the last tier (`final_song`, `FUN_0013eb20`). [C]
- **Status** (0..7) = number of complete+unlocked tiers, **+1** if every career song of that difficulty is 5★ and, on
  Medium/Hard/Expert, every bonus (store) song is also 5★ (`FUN_0013e7e8`, `FUN_0013e940`). [C]
- **Cash** (per difficulty): first pass of a song with S stars pays the cumulative `star_awards` (Medium+: 3★ $100, 4★ $300,
  5★ $650; Easy $0); improving later pays the difference; bonus songs pay no star money. A status increase pays
  `status_awards[diff][newStatus]` (Medium+: 1→$100, 2→$250, 3→$600, 4→$1200, 5→$2200, 6→$4900, 7→$0; Easy all $0). [C]
- Stars are 3/4/5 only (≥3 on any pass; see docs/end_of_song.md). [C]
- **Unlocks, cash, scores, stars and character info are all per difficulty.** Only videos (and `unlock_global_item`) are global. [C]
- **No encores exist in GH1**: no "encore" string in the ELF or any script. [C]
- Unlock-all (`campaign+0x34`) makes `is_unlocked` always TRUE and every list contain every item; it does not
  change progress, status or cash. [C]

---------------------------------------------------------------------------------------------
## 1. Data model

### 1.1 Campaign object (`Campaign`, ctor `FUN_001374b0`, vtable `Campaign_vtable` @0x2f7fa8, handler `Campaign_v1` @0x1395e0, dtor `Campaign_v0` @0x137790)

| off | field | notes |
|---|---|---|
| +0x00..0x0b | DataObject base, name `campaign` | [C] |
| +0x08 | `songs` config DataArray (`FUN_00224270("songs")`) | [C] |
| +0x0c | **CampaignData** (12 bytes, `FUN_0013a300`): +0 `campaign` config array, +4 its `order` sub-array, +8 `store song` array | [C] |
| +0x18..+0x24 | `vector<ProfileState*>`, always 8 entries (`ProfileState(data, i)` for i=0..7) | [C] |
| +0x28 | current profile slot (int) | [C] |
| +0x2c | **HighScoreDB** `highscores` (`FUN_001508c8`, built from the list of all 47 songs) | [C] |
| +0x30 | **needs_save** flag | [C] |
| +0x34 | **all_access** (unlock-all) | [C] |
| +0x38 | **tutorial_access** | [C] |
| +0x3c | **skip_tutorials** | [C] |
| +0x40/+0x44 | `list<CashAward>` pending awards ({int amount, Symbol reason}) | [C] |

### 1.2 CampaignData helpers (operate on `campaign.dta`) [C]

| fn | meaning |
|---|---|
| `FUN_0013a4d8` | venue list = first symbol of each `(order (venue songs...)...)` entry, in order |
| `FUN_0013ab10(venue)` | that venue's 5 songs |
| `FUN_0013a788(diff)` | **number of venues for diff** = 6, or 5 when diff==Easy |
| `FUN_0013a838(venue)` | venue index |
| `FUN_0013adb8(diff)` | all career songs of the first N(diff) venues |
| `FUN_0013b2a0` | store (bonus) songs = `store.dta (song ...)` keys, 17 |
| `FUN_0013a8d8` | all songs = 30 career (order) + 17 store |
| `FUN_0013b550(song)` | venue that contains the song ("" if none) |
| `FUN_0013b638(song)` | **is_store_song** |
| `FUN_0013b150(venue, diff)` | next venue, or "" if venue is the last for diff |
| `FUN_0013b6c8(diff, venue)` | **required songs** = 5 if venue index == N(diff)−1, else `required_songs[diff]` |
| `FUN_0013b8a8` | `cash starting` (0) |
| `FUN_0013b920(diff, stars)` | Σ `star_awards[diff][1..stars−2]` (cumulative; 3★ = first entry) |
| `FUN_0013b9e0(diff, status)` | `status_awards[diff]` element at index `status` (element 0 is the kDifficulty symbol) |
| `FUN_0013b770(venue)` | venue index, −1 if not found |

### 1.3 ProfileState (0x4c bytes, ctor `FUN_0014f490(this, data, slot)`) [C]

| off | field | default |
|---|---|---|
| +0x00 | **empty** flag (bool) | 1 |
| +0x04 | String **name** (c_str at +0x18) | `sprintf("profile_%d", slot)` |
| +0x1c | **tutorials_done** (int, highest tutorial started) | 0 |
| +0x20..+0x2c | `vector<CampaignState>` × 4 (one per difficulty, 0x28 bytes each) | see 1.4 |
| +0x30..+0x47 | per-profile **options** copy: lefty[0], lefty[1], band vol, guitar vol, fx vol, stereo (`FUN_001071e8`) | 0, 0, 1.0, 1.0, 1.0, 1 |
| +0x48 | **last_difficulty** | 0 (kDifficultyEasy) |

The options block is copied **from** the global Options (`DAT_002edb68+8..+0x1f`) into the current profile on save
(`FUN_00138b90`) and **to** the Options on `set_profile_slot` (`FUN_00138c98`). So volumes/stereo/lefty are per profile. [C]

### 1.4 CampaignState (one per difficulty, 0x28 bytes, ctor `FUN_0013ba60(this, diff, data)`) [C]

| off | field | default |
|---|---|---|
| +0x00 | CampaignData* | |
| +0x04 | Symbol **character** | "" (empty means *new campaign*) |
| +0x08 | Symbol **guitar** | "" |
| +0x0c | Symbol **guitar skin** | "" |
| +0x10 | int **cash** | `cash starting` = 0 |
| +0x14..+0x20 | `vector<CampaignItem>` (0x20 bytes each) | built below |
| +0x24 | difficulty (0..3) | |

### 1.5 CampaignItem (0x20 bytes, `FUN_00154cd8(cat, sym, is_store, unlocked, global)`) [C]

| off | field |
|---|---|
| +0x00 | category: 0 venue, 1 song, 2 character, 3 guitar, 4 skin, 5 video |
| +0x04 | Symbol |
| +0x08 | **is_store** (purchasable) |
| +0x0c | **unlocked** |
| +0x10 | **global** (buying unlocks it in all 4 difficulties) |
| +0x14 | **beaten** (songs) |
| +0x18 | **best score** (songs) |
| +0x1c | **stars** (u8, songs; 0, 3, 4, 5) |

Item list, in this order (116 items), with initial flags [C]:

| builder | category | items | is_store | unlocked | global |
|---|---|---|---|---|---|
| `FUN_0013baf8` | 0 venue | basement..arena (6) | 0 | **index 0 only** | 0 |
| `FUN_0013be78` | 1 song | 30 career songs in tier order | 0 | **tier 0's 5 songs** | 0 |
| `FUN_0013be78` | 1 song | 17 store songs | 1 | 0 | 0 |
| `FUN_0013c5d0` | 2 character | `characters.dta` order: metal nu_metal punk classic alterna hair_metal hiphop grim | in `store character` | !is_store | 0 |
| `FUN_0013c958` | 3 guitar | `guitars.dta` order (13) | in `store guitar` | !is_store && sym != `battleaxe` | 0 |
| `FUN_0013cd20` | 4 skin | every `(guitars <g> (skins ...))` entry (39) | in `store skin` | !is_store | 0 |
| `FUN_0013d128` | 5 video | `store video` keys video1..3 | 1 | 0 | **1** |

So a new profile owns: basement + its songs; characters metal, nu_metal, punk, classic, alterna, hiphop; guitars
gibson_sg, gibson_lespaul, gibson_flying_v (+ grim_guitar, guitar_red_octane, which the guitar selector filters, see
docs/select_panels.md); the first skin of every guitar. Locked: hair_metal ($5000), grim ($8000), 7 store guitars,
28 store skins, 17 bonus songs, 3 videos, and `battleaxe` (not sold; unlocked by beating Expert). [C]

### 1.6 Derived quantities (CampaignState methods) [C]

```
beaten(song)            = item.beaten                              FUN_0013d658
unlocked(sym)           = item.unlocked                            FUN_0013d638
beatenInVenue(v)        = #songs of v with beaten                  FUN_0013e278
venueComplete(v)        = beatenInVenue(v) >= required(diff, v)    FUN_0013e220
completedVenues         = #v in venues[0..N(diff)) with unlocked(v) && venueComplete(v)   FUN_0013ed90
allFiveStar             = every song of venues[0..N) has stars==5 &&
                          (diff==Easy || every store song has stars==5)                    FUN_0013e940
status                  = completedVenues + (allFiveStar ? 1 : 0)                          FUN_0013e7e8
statusSym               = "status_%d"(status)  (locale: 0 NOVICE, 1 ROCKER, 2 YOUNG GUN, 3 SHREDDER,
                          4 PRO, 5 GUITAR HERO, 6 GUITAR LEGEND, 7 GUITAR GOD)              FUN_0013e8f8
songsBeaten             = #career songs of venues[0..N) with beaten                         FUN_0013ee98
totalSongs              = 5*N(diff) (25 Easy, 30 otherwise)                                 FUN_0013f030
careerScore             = Σ best score over unlocked songs (category 1, incl. bought bonus)  FUN_0013ec68
```

### 1.7 Unlock rules [C]

`unlockNextVenue(state, song)` (`FUN_0013e130`), called on the first beat of a song and by `cheat_beat_song`:
```
if is_store_song(song): return ""
v = venueOf(song)
if venueComplete(v):
    nv = nextVenue(v, diff)            // "" when v is the last venue for diff
    if nv != "" && !unlocked(nv):
        unlock nv and all 5 songs of nv (FUN_0013e368 sets +0x0c = 1)
        return nv
return ""
```
`final_song` (`FUN_0013eb20`, for the current song = GameConfig+0x98):
```
TRUE iff song is a career song, not store, not yet beaten,
     venueOf(song) is the last venue for diff, and beatenInVenue == required-1 (= 4)
```
`is_unlocked sym` (`FUN_00137b00` → `FUN_00137b28`): `all_access || state[curDiff].item(sym).unlocked`. [C]
Characters and guitars elsewhere use the "any difficulty" variant (`FUN_00137b28` with d=0..3). [C]

### 1.8 Cash rules (`finish_song`, `FUN_0013dd80(state, song, score)`) [C]

```
oldStatus = status
item = song item
result = {first=0, venue="", status=0, awards=[]}
if !item.beaten:
    item.beaten = 1; result.first = 1
    nv = unlockNextVenue(song)
    if nv != "": result.venue = nv; result.status = venueIndex(nv)
if item.score < score:
    oldStars = item.score ? numStars(item.score) : 0      // FUN_0011bdd0(score, player 0): 3/4/5 for this song
    newStars = numStars(score)
    item.score = score                                     // FUN_00154d08 (keeps max)
    if oldStars < newStars: item.stars = newStars          // FUN_00154d20 (keeps max)
    cash = 0
    if !is_store_song(song) && oldStars < newStars:
        cash = starCum(diff, newStars) - (oldStars ? starCum(diff, oldStars) : 0)
    newStatus = status
    if oldStatus < newStatus:
        result.status = newStatus
        cash += status_awards[diff][newStatus]
    if cash > 0:
        reason = "award_%d_star"(newStars) + (oldStatus < newStatus ? "_status" : "")
        result.awards.push_back({cash, reason}); state.cash += cash
```
- The handler (`FUN_00138ec0`) stores `$new_venue = result.venue`, `$new_status = result.status`, **replaces** the
  pending award list with `result.awards` (list `operator=`, `FUN_0029e900`), and sets needs_save if
  `result.first` or awards are non-empty. It returns 0. [C]
- When a status rise and star rise coincide, one award is paid (status amount added to the star amount). [C]
- If two status steps happened at once (only possible in contrived cases), only `status_awards[newStatus]` is paid. [C]
- Locale also contains `award_god` and "Each 1 Million Career Points" (CASH_AWARD_BLURB5), but **no code produces them**
  (no `award_god` string in the ELF). [C]
- The cash award screen's "gross" (`amount / 0.05`) and the seven fees are cosmetic script math. [C]

---------------------------------------------------------------------------------------------
## 2. Script messages

### 2.1 `campaign` (Campaign_v1 @0x1395e0) — every handled message

Return convention: int unless stated; "sets save" = sets needs_save (+0x30). Unknown messages return kDataUnhandled. [C]

| message | args | behaviour / return | fn |
|---|---|---|---|
| `all_access` | | +0x34 | |
| `set_all_access b` | b | +0x34 = b!=0 | |
| `get_random_song` | | symbol: random song from all 47, re-rolled until it is a career song or an unlocked (cur diff) store song | `FUN_00138620` |
| `num_profiles` | | number of non-empty slots | `FUN_00137890` |
| `max_num_profiles` | | 8 | |
| `has_profile_name s editing` | | TRUE if a non-empty slot (skipping the current slot when `editing`) has that name, **case-insensitive** (`FUN_0028d930`) | `FUN_001378c8` |
| `set_profile_name s` | | current slot: name = s, empty = 0; sets save | `FUN_001379a8` |
| `profile_name i` | | slot i's name, or "Someone" if empty | `FUN_001379e8` |
| `is_empty_profile i` | | empty flag | `FUN_00137a30` |
| `empty_slot` | | first empty slot or −1 | `FUN_00137a48` |
| `delete_slot i` | | replaces slot i with a fresh ProfileState (high scores of that slot are **not** cleared) | `FUN_00137a80` |
| `profile_slot` | | +0x28 | |
| `set_profile_slot i` | | +0x28 = i; copies the profile's options into the global Options | `FUN_00138c98` |
| `new_campaign` | | TRUE if `state[curDiff].character == ""` | `FUN_0013d5c8` |
| `set_character_info` | | state[curDiff].{character, guitar, skin} = player 0's config; sets save if changed | `FUN_001393b0` |
| `get_character_info` | | player 0 config ← state[curDiff].{character, guitar, skin} | `FUN_00139338` |
| `is_unlocked sym` | | see 1.7 | `FUN_00137b00` |
| `is_store_song sym` | | song is in `store song` | `FUN_0013b638` |
| `foreach_venue $v {body...}` | | for each of the 6 venues in order: `$v = venue`, run the body | `FUN_00138cf0` |
| `finish_song score $venue $status` | | see 1.8; writes the two variables; returns 0 | `FUN_00138ec0` |
| `cheat_beat_song song score` | | beaten=1; score=max; if score==1000000 → stars 5; unlockNextVenue; **no cash, no save flag** | `FUN_0013e090` |
| `has_cash_award` | | pending award list non-empty | |
| `next_cash_award $amt $reason` | | pops the front award: `$amt`=int, `$reason`=symbol, returns 1; if empty sets both to 0, returns 0 | `FUN_00139068` |
| `status` | | status (cur diff) | `FUN_0013e7e8` |
| `get_cur_status_sym` | | `status_<n>` symbol (cur diff) | `FUN_0013e8f8` |
| `get_status_progress diff` | | "" if 0 songs beaten, else `sprintf(localize status_progress, localize status_<n>, songsBeaten, totalSongs)` → e.g. "PRO STATUS (17 OF 30 SONGS)" | `FUN_00139440` |
| `mark_needs_save` | | +0x30 = 1 | |
| `needs_save` | | +0x30 | |
| `save_complete` | | +0x30 = 0 | |
| `add_cash n` | | state[curDiff].cash += n; sets save | `FUN_00139598` |
| `cash` | | state[curDiff].cash | `FUN_0013e770` |
| `career_score` | | see 1.6 | `FUN_0013ec68` |
| `high_score song` | | best score of the song (cur diff) | `FUN_0013ed50` |
| `buy_item sym price` | | if price ≤ cash: unlocked=1, cash −= price; if the item is global (videos) also unlocks it in all 4 difficulties; **always** sets save | `FUN_00138538` / `FUN_0013e788` |
| `unlock_global_item sym` | | for d=0..3: unlock at price 0 (always succeeds); does **not** set save | `FUN_001385b8` |
| `skip_tutorials b` | | +0x3c | |
| `tutorials_done` | | 3 if skip_tutorials or tutorial_access, else profile.tutorials_done | `FUN_001392b8` |
| `do_tutorial n` | | profile.tutorials_done = max(·, n); sets save if raised | `FUN_001392f0` |
| `tutorial_access` / `set_tutorial_access b` | | +0x38 | |
| `update_difficulty` | | profile.last_difficulty = GameConfig difficulty (no save flag) | `FUN_00139538` |
| `last_difficulty` | | profile.last_difficulty | |
| `final_song` | | see 1.7 | `FUN_00139568` |

Script uses (all files): `career.dta` (cash, career_score, get_status_progress×4, last_difficulty,
update_difficulty, new_campaign, get_character_info, set_character_info×3, get_cur_status_sym, is_unlocked×5,
buy_item, foreach_venue, cheat_beat_song, beat_song, profile_name, profile_slot), `endgame.dta` (finish_song, status×2,
has_cash_award×3, next_cash_award, profile_name/profile_slot×4, unlock_global_item, is_store_song), `game.dta`
(is_store_song), `arena_game.dta` (final_song), `splash.dta` (num_profiles, is_empty_profile, profile_name,
set_profile_slot, has_profile_name, set_profile_name, get_random_song), `main.dta` (tutorials_done,
tutorial_access), `tutorials.dta` (tutorials_done, do_tutorial×3, tutorial_access), `options.dta` (profile_name,
profile_slot, num_profiles, max_num_profiles, needs_save, empty_slot, is_unlocked), `mem_card.dta` (save_complete×2,
delete_slot), `system_script.dta` (needs_save), `cheats_funcs.dta` (all_access, set_all_access,
set_tutorial_access, add_cash), `long_cheats.dta` (set_all_access). [C]

**Quirk:** `sel_song beat_song` (no-arg form, career.dta) sends `campaign beat_song`, which Campaign_v1 does
**not** handle (returns kDataUnhandled). It is a debug path. The keyboard cheats `b`/`5` call the one-arg form →
`cheat_beat_song song 1` / `1000000`. [C]

### 2.2 `store` (StorePanel, `new_store_panel`, handler `StorePanel_v1` @0x1535d0) [C]

| message | behaviour |
|---|---|
| `price cat item` | `store.<cat>.<item>.price` (`FUN_001533b0`); cat ∈ guitar, skin, song, character, video |
| `low_cost cat` | minimum price in the category (start 10,000,000) |
| `high_cost cat` | maximum price (start 0) |
| other | panel base (`get`/`set` of script properties like `category`, `itemIdx`; `is_up`) |

Prices (`store.dta`): guitars es335/xplorer/firebird/lespaul_special 600, corvus/moderne 700, eds 850;
skins 150/200/250 per tier (eds_white 250); songs fireitup, cheatonthechurch, cavemanrejoice, eureka 300, the other
13 songs 250; characters hair_metal 5000, grim 8000; videos video1 350, video2 325, video3 325 (files wavegroup.pss,
character.pss, venues.pss). [C]

Buying (career.dta `store` screen): on screen 2 a select buys when `!is_unlocked item && cash >= price` →
`campaign buy_item item price`, refresh cash, push `store_bought` (text `<category>_bought_blurb`), sfx
cash_register; otherwise sfx button_error. Back from screen 1 → `autosave_goto career`. The store button is
disabled on Easy and the cash labels are hidden there. Items already owned show `st_sold.mesh`; unowned characters
are drawn unlit (`char_store_lights_on FALSE`). Help shows `help_buyitem` only for unowned items. [C]

### 2.3 Store / career providers [C]

| provider | list | text / symbol |
|---|---|---|
| `song_provider` (SongProvider, ctor `FUN_00118400`; flags +0x10 quickplay, +0x14 store_mode) | career (quickplay=0): `state[curDiff]` songs that are unlocked (store_mode=0) or that are store items (store_mode=1); unlock-all includes all (`FUN_00137c18` → `FUN_0013d898` → `FUN_00154f18`). Quickplay: `FUN_00137d58` (below). | `get_text i` = songs.dta `name`; `get_symbol i`; `list_length`; `set_quickplay b`; `get_quickplay`; `set_store_mode b` |
| `store_guitar_provider` (`FUN_0011a828`) | store guitars of state[curDiff] (all store items, owned or not) | |
| `store_skin_provider` (`FUN_0011aa28`) | store skins whose **parent guitar is unlocked** at curDiff (`FUN_0011ac28`) | `get_guitar i` = parent guitar |
| `character_provider` (store_mode at +0x10) | store_mode: store characters (hair_metal, grim); else unlocked characters | |
| `{game video_provider}` (VideoProvider) | the 3 `store video` entries | `get_video_file sym` |

Career song-list filter (`FUN_0013d698`, applied to category 1 lists): if the career is not complete
(completedVenues ≠ N(diff)) and the selected venue's index < status, the songs of `venues[status]` (the newest,
in-progress tier) are removed. So new-tier songs only appear while you are at the newest venue. [C]

Quickplay list (`FUN_00137d58`): songs of the **first two tiers always**, plus every song unlocked in **any**
difficulty of the current profile (bought bonus songs included); listed in `career order + store order`; unlock-all
shows all. [C]

SongProvider columns (jump table @0x301f10, verified by disassembly): [C]
- 0 name (`SongProvider_v2`);
- 1 score: career `"%i"` of the best score, quickplay "" (`FUN_00118c58`);
- 2 stars: career `"***"`/`"****"`/`"*****"` for 3/4/5, else "" (`FUN_00118cb8`);
- 3 header (`FUN_00118d48`): on the first row of each tier, `localize song_header_<venue>` ("1. Opening Licks" …
  "6. Face-Melters", "Bonus Tracks" for `store`);
- 4 footer (`FUN_00118dd8`), only on the last career row (+0x28 = row before the first bonus song, or the last row):
  - career: "" if the career is complete (status ≥ N); `song_old_venue` "Play in the latest venue to continue your
    career" if venueIndex < status; `song_last_venue` "Beat 5/5 songs to win" at the last venue; else
    `song_current_venue` "Beat %i/5 songs to continue" with `required(curDiff, curVenue)`.
  - quickplay: `song_quickplay` "Play career mode to unlock more songs" unless some difficulty in Medium..Expert has
    status ≥ N−1.
- `SongProvider_v5` (row active/checkmark [I]): career && beaten && not a store song. `v8(row)`: is a header row.
- There are **no locked rows**: locked songs are simply absent.

GameConfig career helpers [C]:
- `set_career_venue` (`FUN_00113c30`, sel_venue enter): if the career is complete and the current venue is unlocked
  and not `multi_only` → keep; else select the last venue i such that venues 0..i are all unlocked and available
  (= the newest tier).
- `set_career_song` (`FUN_00113b80`, sel_song enter): starting from the current song index (or 0), advance to the
  first row that is not (beaten && career); if none is found keep the current index.
- `is_venue_available v` = the venue's `arena venues` entry has no `(multi_only TRUE)` (`FUN_00113d78`).

### 2.4 `highscores` (HighScoreDB, `FUN_001508c8`, handler `HighScoreDB_v1` @0x151298) [C]

Table: for each of the 47 songs, `[8 profiles][4 difficulties]` lists of 5 `{char name[8], int score}`,
sorted descending. Default per list: Kendall 50000, Marc 40000, Kris 30000, Sinc 20000, Keith 10000
(`highscore_dummy_0..4`). Lookups use (GameConfig song, campaign profile slot, GameConfig difficulty).

| message | behaviour |
|---|---|
| `check_highscore score` | insert position: first entry whose score < `score` (ties go below existing); −1 if past the 5th (`FUN_002a2ad8`) |
| `get_highscore slot $name $score` | entry `slot` |
| `add name score` | insert at that position (name truncated to 8 chars), drop the 6th; **sets campaign needs_save** (`FUN_00150e70`) |

Used only on the quickplay `highscore` screen; career never records high scores. [C]

### 2.5 `meta` career bits (splash.dta MetaPanel script) [C]

`setup_career`: main_screen=career, continue_screen=sel_song, win_screen=endgame, lose_screen=lose, mode=career,
`nameprof can_go_back FALSE`. `won_campaign` is cleared on `loading` enter, set at `game_outro` when
`mode==career && campaign final_song`. `endgame` sets continue_screen=career when `$new_venue != ""`. `pause` quit
in career sets continue_screen=career. `complete` re-runs `setup_career` after any choice.

---------------------------------------------------------------------------------------------
## 3. Career screen flow [C unless noted]

```
main ──career──► sel_difficulty (panels meta sel_diff_career helpbar char_single)
   labels: get_status_progress for each difficulty; focus = campaign last_difficulty
   SELECT: game set_difficulty; campaign update_difficulty;
           new_campaign ? sel_character_new : (get_character_info; career)
sel_character_new ──select──► sel_guitar ──(guitar[, skin])──► goto_career:
           game set_guitar/set_guitar_skin; campaign set_character_info; career
career (panels meta career helpbar char_single; enter: turn_off_ambient, meta setup_career)
   shows character, guitar skin, cash (hidden on Easy), band name, career_score, status sym, difficulty
   ROCK → autosave_goto sel_venue │ hero → sel_character_edit │ guitar → sel_guitar_edit │ store → store (disabled on Easy)
   back → sel_difficulty
sel_venue: foreach_venue → enable/disable buttons by is_unlocked (only available venues)
           game set_career_venue; map frame = index+1; focus moves game set_venue; SELECT → sel_song; back → career
sel_song (sel_song_career TRUE): provider in career mode; game set_career_song; song preview
           SELECT → game set_multiplayer FALSE → loading → game
game ── win ── game_outro: if career && final_song → meta won_campaign TRUE → post_show
     ── lose ─ game_outro → lose (restart / selsong→meta_loading_goto continue_screen / quit→main_screen=career)
post_show → won_campaign ? (Easy ? win_easy : win_game) : endgame
   win_easy: 6 s input delay, band name, beat_easy sfx
   win_game: 4 s white fade-in, 6 s delay; on Expert the first select runs
             `campaign unlock_global_item battleaxe` and shows the Battle Axe screen; then fade → endgame
endgame (enter): song, difficulty, score, %hit, streak, star_rating, headline;
             career: `campaign finish_song score $new_venue $new_status`; new venue → continue_screen career
   select: $new_status ? status : (has_cash_award ? cashaward : goto_endgame_complete)
status: StatusPanel, scene ghui/status_<campaign status>.gh, labels from `text status<N>` (band name / character
        name, `sprintf(localize token, name)`); hides `status_guitarhero_newvenue.view` when won && status 5 && Easy;
        select → cashaward or goto_endgame_complete
cashaward: next_cash_award per page (reason text = locale `award_<n>_star[_status]`); select → next or complete
goto_endgame_complete: career && won_campaign → meta_loading_goto career; else complete
complete: selsong ("CONTINUE" for career non-bonus songs) → meta_loading_goto continue_screen;
          restart → game; quit → meta_loading_goto main_screen; then meta setup_career
meta_loading → TRANSITION_COMPLETE → autosave_goto next
```
- Tier-unlock announcement = the `status` screen (status rises whenever a tier completes). There is no separate
  "new venue" popup; the new venue appears in sel_venue on the next visit, which `continue_screen=career`
  routes through. [C]/[I]
- `game.dta` mtv_overlay: bonus songs use `mtv_unlock_line*`, career songs `mtv_campaign_line*` with
  "made famous by". [C]
- store: see 2.2. sel_character_edit / sel_guitar_edit: on select `campaign set_character_info` → career. [C]

---------------------------------------------------------------------------------------------
## 4. Save data

### 4.1 When it saves [C]

`autosave_goto S` (system_script.dta): `if (campaign needs_save && $autosave && !$disable_save)` → set
`$mc_save_success_screen = $mc_save_failed_screen = S`, go to `autosave` (memcard save, `campaign save_complete`
on success); else go straight to S. Call sites:
- `career` ROCK (`autosave_goto sel_venue`), store back (`autosave_goto career`), new profile name accepted
  (`autosave_goto main`), game_settings back (`autosave_goto options`);
- every `meta_loading_goto` (after a song: complete/lose/pause quit, attract exit, tutorials) via `meta_loading`.

needs_save is set by: set_profile_name, set_character_info (when changed), finish_song (first pass or award),
add_cash, buy_item, do_tutorial (when raised), highscores add, mark_needs_save. [C]
`$autosave`: TRUE after the boot memory-card check (bootup_load), FALSE if the player declined to create a save.
Toggle: options → data settings. `$disable_save` = 1 after the unlock-all or crowd-meter long cheats (also disables
the data settings button). Manual save/load/delete: data_settings → memcard_save_confirm / memcard_load_confirm /
pre_delete_confirm. [C]

### 4.2 Memory card container [C]

- `config/mc.dta`: dir `base_dir "BASLUS-21224"`, file name `"data"` (@0x309d08), title "Guitar Hero",
  icon `mc/gh.icn` + bg_colors / lit_dirs / lit_colors / ambient for icon.sys (`FUN_0014a470`).
- One file holds **everything** (8 profiles + high scores). Buffer 0x1e000 = 122,880 bytes at 0x344fb8, always written
  whole (`FUN_0014a650` / `FUN_0014a730`); creating needs ≥ 0x33000 bytes free (else kMCNotEnoughSpace −106).
- BinStream: **little-endian, not encrypted** (`FUN_0022b618(..., 1)`, cipher pointer 0). Strings/Symbols =
  `u32 len` + chars, no NUL.

### 4.3 Binary layout (`FUN_00138b90` save / `FUN_00138c30` load), version 6 [C]

```
u32 version = 6                    (DAT_002edbe4; load requires > 4; item stars are read only when >= 6)
8 × Profile                        (FUN_0014f920)
    u8   empty
    str  name
    u32  tutorials_done
    u32  last_difficulty
    4 × { str character; str guitar; str skin; u32 cash }       (FUN_0013f3d8)
    options: u32 ver=1; u8 lefty0; u8 lefty1; f32 band; f32 guitar; f32 fx; u8 stereo   (FUN_00107218)
u32 item_count                     (items of profile 0 / Expert, in item order; 116)
item_count × {
    str  symbol
    u32  category
    8 profiles × 4 difficulties × {                             (FUN_00154d40)
        u8 flags = is_store | unlocked<<1 | (song ? beaten<<2 : 0)
        if category == song: u32 best_score; u8 stars
    }
}
u32 highscore_version = 1          (FUN_00150ec8)
u32 song_count (47)
song_count × { str song; 8 profiles × 4 diffs × 5 × { char name[8]; u32 score } }
```
On load, unknown items or songs are read into a scratch record and dropped; missing ones keep their defaults
(`FUN_0013f500`, `FUN_00150fc0`). The current slot, cheats and pending awards are not saved. [C]

### 4.4 Defaults for a new profile [C]

empty until named; name = typed band name (max 20 chars, `band_name` style); tutorials_done 0; last_difficulty Easy;
per difficulty: character/guitar/skin "" (→ new_campaign TRUE), cash 0, items as in 1.5; options lefty off/off,
volumes 1.0, stereo on; high scores = the dummy table.

### 4.5 Proposed PC save file (JSON, one file = one PS2 card file)

```json
{
  "format": "gd01-career", "version": 6,
  "profiles": [                                  // exactly 8 slots
    {
      "empty": false,
      "name": "THE BAND",
      "tutorials_done": 2,
      "last_difficulty": "medium",              // easy|medium|hard|expert
      "options": { "lefty": [false, false], "band_volume": 1.0, "guitar_volume": 1.0,
                   "fx_volume": 1.0, "stereo": true },
      "careers": {                               // exactly 4 keys
        "medium": {
          "character": "metal", "guitar": "gibson_sg", "skin": "gibson_sg_cherry", "cash": 1350,
          "items": {                             // only entries that differ from defaults are required
            "venue:small_club":   { "unlocked": true },
            "song:ironman":       { "unlocked": true, "beaten": true, "score": 123456, "stars": 4 },
            "guitar:gibson_es335":{ "unlocked": true },
            "video:video1":       { "unlocked": true }
          }
        },
        "easy": { "...": "..." }, "hard": { "...": "..." }, "expert": { "...": "..." }
      }
    }
  ],
  "highscores": {
    "ironman": [                                 // [slot][difficulty] → 5 entries, descending
      [ [ {"name": "KENDALL", "score": 50000}, "... 5 entries" ], "... 4 difficulties" ],
      "... 8 slots"
    ]
  }
}
```
Rules for the loader: build every profile from the defaults of 1.5, then apply stored items by `"<category>:<symbol>"`;
ignore unknown keys (like `FUN_0013f500`); `is_store` and `global` come from the config, not the file. Store whole
lists if you prefer a dense form (`"items": [[flags, score, stars], ...]` in item order). This holds exactly the
state of 4.3, and the PS2 `data` file can be converted 1:1 by following that layout.

---------------------------------------------------------------------------------------------
## 5. C++ sketch

```cpp
enum ItemCat { kVenue, kSong, kCharacter, kGuitar, kSkin, kVideo };
struct Item { ItemCat cat; Symbol sym; bool isStore, unlocked, global, beaten; int score; uint8_t stars; };
struct CareerState { int diff; Symbol character, guitar, skin; int cash; std::vector<Item> items; };
struct Profile { bool empty = true; std::string name; int tutorialsDone = 0; int lastDifficulty = 0;
                 Options opts; CareerState careers[4]; };
struct CashAward { int amount; Symbol reason; };
struct Campaign { Profile slots[8]; int slot = 0; bool needsSave = false, allAccess = false,
                  tutorialAccess = false, skipTutorials = false; std::deque<CashAward> awards; HighScoreDB hs; };
int numVenues(int d) { return d == 0 ? 5 : 6; }
int required(int d, int venueIdx) { return venueIdx == numVenues(d) - 1 ? 5 : kRequired[d]; } // {4,4,5,5}
```
Implement `status`, `unlockNextVenue`, `finishSong`, `finalSong` and the list filters exactly as in 1.6–1.8 and 2.3.

---------------------------------------------------------------------------------------------
## 6. Open points

- `SongProvider_v5` is read as "show checkmark" [I]; the exact column-to-label mapping of `song_*.txt` is [I].
- The text of the `store_guitar_provider` / `store_skin_provider` `get_text` (localized symbol) is [I].
- `skip_tutorials` is never sent by the shipped scripts (debug). [C]
