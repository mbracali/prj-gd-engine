# GH2's career, encores, cash, unlocks and the store

How gd02 plays Guitar Hero II's career (M10.1): the Campaign object, the
bands' tours per difficulty, the encores, the cash, the unlock table and the
store, all from `default.xex` (addresses below; `ghidra/analysis/`) and
GH2's own scripts (`ui/career.dta`, `ui/endgame.dta`, `ui/game.dta`,
`config/campaign.dta`, `config/store.dta`, `config/guitars.dta`). **[C]**
confirmed in code, **[I]** inferred. The code: `engine/src/ui/campaign.*`
(the rules), `ui/store.*` (StorePanel and the store's providers),
`ui/natives_meta.cpp` (`campaign`, `song_provider`), `ui/career.*` (the save).

## The data

- **Campaign** (handler 0x82287F58): CampaignData at +0x2c (0x822A3910:
  campaign.dta's `order`, store.dta's `(song ...)`, the `downloads`), the
  eight profiles at +0x3c, the chosen slot +0x48, the high scores +0x64,
  pro face-off +0x68 (saved), needs_save +0x69, all access (the cheat)
  +0x6a, tutorial access +0x6b, skip tutorials +0x6c, the guitar awards
  +0x7c [C].
- **Profile** (0x822A88C8, 0x44 bytes): in use +0x4 (0 = in use), name +0x8,
  tutorials +0x1c, four per-difficulty records +0x20, the difficulty last
  played +0x2c, cash +0x30, cash spent +0x34, the unlock table +0x38 [C].
- **Per-difficulty record** (0x822AAD68, 0x24 bytes): the band's look (four
  symbols, docs/band.md) and a table of 16-byte entries at +0x14, its
  difficulty at +0x20 [C]. Entries (0x822AE698): +0 type (0 venue, 1 song,
  2 character / outfit, 3 guitar, 4 skin, 5 video, 6 game mode), +4 symbol,
  +8 best score, +0xc the best score's stars (bits 0..6) and the perfect
  flag 0x80 (100 % of the notes: the gold stars), +0xd the most stars, +0xe
  flags: 0x80 sold in the store, 0x40 unlocked, 0x10 encore, 0x08 a venue
  completed, 0x02 always [C].
- A record is made (0x822A9E90 / 0x822A9FA0 / 0x822AA2D8) with every venue
  (the first unlocked), every tier song (the first venue's unlocked; the
  last song of each venue flagged encore, at every difficulty), the store's
  songs (sold, locked) and `unlock_game_modes` (multi_fo) [C].
- **The unlock table** (+0x38; 0x822A7B30 / 0x822A7C60 / 0x822A7EC8 /
  0x822A7FA8): every guitars.dta guitar and skin, every character and outfit
  of gh2.dta, the store's videos. An item store.dta sells is locked until
  bought, every other one unlocked [C]. This is the "per-difficulty unlock
  table at profile +0x38" M9.10 left open: it is the profile's, not per
  difficulty; venues and songs are per difficulty, in the records.
- **Passed** (0x822AE8F8): a song with three stars or more (every passed
  song has at least three), a venue with its completed flag [C].

## Status, tiers and encores [C]

- Tiers per difficulty (0x822A2BC8): the eight venues, Easy seven (no
  Stonehenge).
- `required_songs` (0x822A3468): `(kDifficultyEasy 4 5) (kDifficultyMedium 5
  6) (kDifficultyHard 6 6) (kDifficultyExpert 6 6)`: the first number for
  every tier but the difficulty's last, the second for the last.
- **Encores** (0x822A9B38 / 0x822A9578): an encore song is unlocked once its
  venue has `required - 1` songs passed; Easy has no encores (never
  unlocked) [C].
- **A venue completes** (0x822A9BB0, at the difficulty played): on Easy any
  tour song that brings the venue's passed songs to `required`; at the other
  difficulties only its encore (with `required` passed). The next venue
  (none after the difficulty's last tier) is then unlocked with its songs
  (0x822A9088) and is finish_song's `$new_venue`. So Medium needs four songs
  and the encore, Hard and Expert all five and the encore.
- **Status** (0x822A9D30): the completed tiers (0x822A9750), plus one when
  every tier song has five stars (0x822A91A8; Easy ignores its encores).
  `won_campaign`: status ≥ tiers; `is_max_status`: status > tiers;
  `get_cur_status_sym` "status_%d"; `get_status_progress` "(%i OF %i
  SONGS)" (passed / total, Easy without the encores) or `no_progress`.
- **Tour passed** (0x822A9DD8): all tiers completed; **five stars**
  (0x822A91A8).

## finish_song (0x82286C28 -> 0x822A8078 -> 0x822AA598) [C]

`{campaign finish_song $score $cash $reason $venue $status $status_award}`
(post_show_screen), the outputs by name:

1. The profile's difficulty becomes the game's (+0x2c).
2. For every difficulty up to the one played: at the one played, a higher
   score than the best sets it and the stars (the song's stars from the
   score, 0x822CE350: player0 num_stars), the venue may complete (above),
   each status reached adds `status_awards` (`$status`, `$status_award`),
   `$reason` is `award_<stars>_star`, 100 % sets the perfect flag. At every
   difficulty up to it, more stars than the song's most there add each new
   star's `star_awards` (3 / 4 / 5 stars: 150 / 200 / 250 at Medium, Hard,
   Expert; 0 at Easy), but not for store songs or downloads. So a first
   Hard pass with three stars pays 150 at Hard and 150 at Medium.
3. The cash goes to the band; with a new status, the venue played: none of
   the status cash when a harder difficulty completed it, else the easier
   difficulties' `status_awards[status + 1]` too, down to one that completed
   it. `status_awards` are 100 200 300 400 500 700 800 1000 0 (Medium..
   Expert), all 0 on Easy; `starting` cash 50 (tour_start_screen, not on Easy).
4. Tour passed / five stars at this difficulty: guitars.dta's `(require
   tour_passed | tour_5_star) (difficulty d)` guitars are announced
   (`num_guitar_awards`, `next_guitar_award` pops the last:
   unlock_guitar_screen). They then are in stock in the store (below).
5. A tour won (status ≥ 8): pro face-off unlocked (+0x68, `is_unlocked
   multi_fo`).

Scripts then show (endgame.dta `goto_endgame_complete`): status_screen
(the sponsorship of the new status: `sponsorship<status+1>.milo`, cash =
song cash + status cash), unlock_venue_screen (`unlockvenue<status>.milo`,
its 3 s animation), cashaward_screen (the song's cash with GH2's joke
deductions), unlock_guitar_screen, then complete_screen.

## The encore in game [C]

- GamePanel enter (game.dta): `[encore_unlock_check]` = career and
  `encore_unlock_potential` (0x822AAB58: not Easy, the venue not complete,
  its encore still locked, this song not the encore nor passed, and passing
  it brings the venue to `required - 1`); `[encore_song]` = the song is an
  encore.
- `want_encore_fx` (0x822CA5D0): the mode's `encore_fx` (career only) and
  force_encore, or an encore whose venue's encore is unlocked. The intro is
  `intro_start_msg $fast $encore` with it, the venue's INTRO_ENCORE shots,
  and every player's crowd rating starts at 1 (0x822CCA60; SetRating
  0x822DE530: raw at that point of the range).
- At the end (0x822CB5B8, 0x822CA6E0 = encore_fx and (force_encore or
  encore_unlock_check)): `game_won_msg $encore` (the crowd chants,
  `encore_stream play`; crowds full), or `game_lost` then `encore_lost`.
- post_show_screen: `encore_newly_unlocked $freebird` (0x822AAA78: the
  song passed and not the encore; `$freebird` when the venue's encore is
  Free Bird) -> endgame_encore_screen ("Give your audience an encore?", the
  score, stars and `$%d earned`); YES: `game set_song {campaign
  get_cur_encore}` (the venue's encore), `retain_world_bank`, loading: the
  encore plays in the same venue. Free Bird asks three times.
- `final_song` (0x822A93B8): a tour song not passed that completes the last
  tier; game.dta then sets `won_campaign` (win_game_screen,
  `win_<difficulty>.milo`).

## The song lists [C]

- **Career** (`song_provider` kSongsCareer, 0x822C5A68 -> 0x822A75D0 ->
  0x822AA758): the record's songs in order (tiers, then the store's) that
  are unlocked (all with the cheat); before the tour is complete, the
  newest venue's (venues[status]) songs are left out unless that venue (or
  a later one) is the one chosen (0x822AA3F8). An encore row is inactive
  (drawn in the type's `disabled` colours, not chosen) until unlocked
  (0x822C33C0). Rows: the best score (0x822C3428), `***` .. `*****`, or the
  gold `stars_gold_<n>.mat` after a perfect run (0x822C34E8 / 0x822C41D8),
  `checkmark.mat` on passed tour songs. The last active tour row's blurb
  (0x822C0070): `song_old_venue` (an older venue chosen),
  `song_current_venue` ("Beat %i/5 songs to continue": required − 1, Easy
  required), `song_current_venue_encore`, on the last tier `song_win_easy`
  / `song_win_other`.
- **Quick play** (0x82287600): the first two tiers' songs, every song a band
  in use has unlocked at any difficulty (bought ones too), downloads; in
  campaign order. The `song_quickplay` blurb until some band is a tier from
  the end at Medium or harder.
- `game set_career_venue` (0x822B42C8): in career, before the tour is won,
  the newest unlocked venue; `set_career_song` (0x822B89B8): the venue's
  first unlocked song not passed yet (after the tour, the current one).

## IsUnlocked (0x82285CC0) [C]

The all-access cheat: everything. `multi_fo`: pro face-off. In career, the
band's: an item of its table (0x822A7150), else its record at the
difficulty (0x822A9B38). Elsewhere (quick play, the menus): any of the eight
profiles (empty ones too, their defaults) at any difficulty (0x822A7218).
Videos (`is_video_unlocked`, 0x82284890): bought by any band in use.

## The store (career.dta store_screen) [C]

- Prices: store.dta; StorePanel (0x8229D678) `price cat item`
  (0x8229D4A8), `low_cost` / `high_cost` (0x8229D518 / 0x8229D5C8: the
  category's lowest / highest).
- `campaign buy_item item price` (0x822A7278): with enough cash, a table
  item gets its unlocked flag, a song is unlocked in all four records; cash
  − price, cash spent + price; the save is marked.
- Providers: `store_item_provider` (0x822C0658) a store.dta category's
  items (`set_category`); `store_guitar_provider` store.dta's guitars
  (0x822C44D0), `in_stock` (0x822C02A8): a tour reward once the band passed
  / five-starred that difficulty's tour (else "???", not buyable);
  `store_skin_provider` (0x822C6128) the sold skins (the table's order:
  guitars.dta's) of the guitars the band has, `get_guitar` (0x822C04C0);
  `store_video_provider` (0x822C3D10) store.dta's videos and files.
- Bought items are usable at once: guitars and skins in the guitar select,
  characters and outfits in the character select, songs in the career and
  quick play lists, videos in Bonus Videos. The store is closed on Easy
  (career_panel disables it, no cash shown).
- `achievements submit money` at $10000 spent, `check_achievement`: Xbox
  Live's (STUBs).

## The save (career.json, format gd02-save version 2)

As Campaign::Save (0x822869A0 -> 0x82286628 -> 0x822A7360): per band its
name, tutorials, last difficulty, cash, cash spent, per difficulty its look
and its record (`tour`: per venue / song `unlocked`, `completed`, `score`,
`stars`, `perfect`, `max_stars`), the table's bought items (`items`); the
high scores; `pro_faceoff`; the options. A band not in use keeps its
defaults (not written). 0.1.1's saves are read: their quick play results
become the records' best scores, the save-wide items every band's.

## The port's UI fixes for these screens

- `ui push_screen` (0x8236B800) enters a screen over the current one
  without exiting it; `pop_screen` (0x8236AE40) goes back to it as it was
  (store_bought_screen over the store's item page) [C]; the screens below
  draw first [I].
- A screen's `set_focus_panel` (endgame_encore_screen).
- A panel's own ScreenMasks draw at their place in its draw order with
  their mat's texture and blend (unlockvenue<n>.milo's black matte through
  light_gw.tex is the vignette behind the map) [C data, I: the blend
  mapping].
- `animate (name task)`: the task is an object while it runs (`{exists
  unlock_anim}`).
- The store's guitar and characters draw in store.milo at `guitar.pxy` /
  `char_store.placer`, in its draw order (GH2's UIProxy draws its loaded
  directory) [I: the port hooks the store's, whose display panels come
  before store_panel]; the store's proxy draws though its own showing is
  off [I].
- The band's world animations (the drum kit's EventTrigger filters) stop
  when the band goes; the encore keeps the venue (retain_world_bank).

## Tests

- `--walktest career_full`: a new band at Medium, three battle songs passed
  with GH2's `campaign cheat_beat_song` (0x82284CC0), salvation played
  (autoplay, cut at 35 s): the encore is offered ($150), the encore played:
  battle complete, small1 unlocked, status 1 (status_screen $250,
  unlock_venue_screen), $450; the store: the first finish (lp_cayenne,
  $250) bought and on the Les Paul; career.json checked (cash 200, spent
  250, battle completed, small1 unlocked, the stars, the item).
  `GD02_WALK_SHOTS=<dir>` also saves the store's item page, a store
  character and the venue map.
- `--uitest` campaign checks: tiers, required songs, encores, the cash of
  stars across difficulties, the encore and tier at Hard, status cash,
  locked store items, buying, the lists, career.json round trip.

## Open

- Co-op's records (Campaign +0x4c, the bass unlocks `(require (songs n)
  (stars 3))`, 0x822A6330 / 0x822A66E0, finish_coop_song) are M10's co-op.
- win_game_screen (the tour won) and unlock_guitar_screen are scripted but
  not walked; the stone ending's `crowd_audio play_stream` is a STUB.
- Tutorials (`do_tutorial`, the tutorial count) are M11's.
- The guitar provider's `get_text` and the inactive row's selection
  (ignored) are [I]; the guitar list's order (store.dta's) is [C].
