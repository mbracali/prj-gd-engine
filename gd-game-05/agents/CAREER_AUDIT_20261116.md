# Career audit 2026-11-16 (Rock Band 1, gd05)

Owner: "It's time to give a rundown on the career mode ... making sure
career mode is working as it should." Branch
`feature/20261116-bugfixes-list`, build `engine/build/bl05b` at 2ec0189.

Status: **works** (a walk or log shows it doing what RB does), **broken**
(it runs but does something RB does not, or stops), **missing** (RB has it,
the port does not), **n/a** (RB1 has no such thing). Evidence: walks run on
this build (`--walktest <walk>`, logs in `agents/scratch/career16/`,
git-ignored); RB references: the disc's scripts dumped in
`extracted/dta/` and the XEX (`ghidra/`).

## Before the fixes (as found)

All ten career walks of earlier waves pass on this build: rb_career (1 m
49 s), rb_career_band (161 s), rb_tour_rules (82 s), rb_shop_items (83
s), rb_band_lobby (75 s), rb_tattoo (75 s), rb_facepaint (42 s),
rb_makeup (39 s), rb_body_deform (39 s), rb_shop_drum (44 s),
rb_shop_mic (44 s): `walktest <walk>: OK` and each `_restarted: OK`.
They cover the start of each tour only; nothing played a tour to its
end. The new walks rb_full_solo and rb_full_band do (the test aid
below); the rows they found broken are marked so.

### Solo Tour (guitar, drums, vocals; RB1 has no bass in the Solo Tour: modes.dta tour_solo (allow_bass FALSE))

| Feature | Status | Evidence | RB reference |
|---|---|---|---|
| Tiers (9: tier1_1 .. tier3_4, moscow = "Impossible Songs") and their order | works | rb_full_solo: towns open tier1_1 tier1_2 tier1_3 tier2_1 tier2_2 tier3_2 tier3_1 tier3_4 moscow, as the data; 0 events listed before the towns before theirs were done | config/tour_solo.dta (towns ...); open towns RB 0x823E5BB8, 0x823DE560 (D75) |
| Songs per tier (5, the instrument's ranked list) | works | rb_full_solo: 44 tier gigs + the last, 0 wrong songs (guitar, Medium); rb_tour_rules: the first town's songs | (songs N) RB 0x823FE820, ranked lists RB 0x82434020 (D75) |
| Unlock rule: next tier when every open gig of the tiers before is completed | works | rb_full_solo (above) | RB 0x823DE560 (D75) |
| Bonus songs (Rio, (unlock N)): one more each time a bonus gig is first completed | works | rb_full_solo: 13 bonus gigs = the 13 guitar bonus songs; rb_tour_rules: new_unlock announced | RB 0x823E1178, 0x823DE7F8 (D75) |
| Easy stops at song 30 | works (rule) | max_song kDifficultyEasy 30 in the open-events rule; not walked to the end on Easy (GD05_WALK_EASY=1 exists) | tour_solo.dta (max_song ...), RB 0x823E46B0 |
| Encore | n/a | RB1's Solo Tour gigs are single songs (every solo event's (songs N) holds one) | tour_solo.dta |
| Stars needed | n/a | a solo gig is completed by passing the song; stars only pay | RB 0x823DBF80 event_status (D75) |
| Tokyo's (download ...) gig | **broken** | listed as an open gig with no song: rb_full_solo with GD05_DOWNLOADGIG_LEGACY=1 ends with 'download' left unplayable | RB 0x823FDB20: the event named download is the template of one event per downloaded song (none) |
| The end of the tour (the last tier's last gig wins: congratulations, credits, send-off) | **broken** | is_winning answered the flag endgame.dta sets from it, never 1: rb_full_solo with GD05_TOURWIN_LEGACY=1: "the tour's winning gig is ''" | TourCharLocal is_winning vfunc +0x5c RB 0x823E47E0 -> 0x823CFA58 (assembly); modes.dta tour_win_screen; ui/tour_solo.dta tour_solo_win_screen |
| Cash from gigs (reward_per_star x stars) and outfits won | works | rb_career: cash 1 + 5 x 1 = 6, torso worntshirt_line6 owned; rb_full_solo: cash 9850 after 58 gigs | RB 0x823EEAE8 (D67) |
| Save / quit / relaunch | works | rb_career_restarted, rb_tour_rules_restarted, rb_full_solo_restarted (58 gigs kept) | rb_profile.json (D4, D69) |

### Band World Tour

| Feature | Status | Evidence | RB reference |
|---|---|---|---|
| Create a band, its name | works | rb_career_band, rb_band_lobby: tour_band_create_screen, NAME, ACCEPT, the hub | ui/tour_band.dta tour_band_create_panel |
| Band lobby (players pick / make characters) | works | rb_band_lobby (D80) | matchmaking_band.dta, RB 0x8282EA48 |
| Hometown | **broken** | any of the 17 towns offered; the port played the tiers in the towns by (order n) and swapped the first with the hometown [I] | RB 0x823FCED8 places the tiers relative to the hometown |
| Towns and their gigs | **broken** | a tier was "the town of the same order": tier3_1 in Rio, tier3_2 in Reykjavik, ...; Berlin, Stockholm, Moscow, Tokyo, Sydney had no gig at all; the towns' own files (tour/<town>.dta: Make a Setlist, Mystery, Local Artists, marathons, the Hall of Fame, the Endless Setlist) never loaded (D75 left) | RB 0x823FE1D8 (tiers, (world ...)), 0x823FC400 (the tour folder's files), 0x823F3AD0 (a town's events) |
| Towns unlocking (van, bus, jet) | **broken** | any town listed; leaving home needed the van only [I] | RB 0x823F2E20 open towns, 0x823FD058 travel need, 0x823EE2B8 vehicle level |
| Gig unlocks: fans (buzz), stars (town / world fame), staff items | **broken** | buzz and fame checked; (item ...) (the roadies, promoter, security, tour manager the late gigs need) ignored | RB 0x823EC890 event_status, 0x823FF898 the event record |
| Single songs, setlists, mystery setlists (random), make your own (any) | works through the natives; **not walked through the menus** for the "any" picks | rb_full_band plays 173 gigs incl. diy / random / marathons (after the fixes) | tour/<town>.dta |
| Venue a town's gig is played in | **broken** | a random venue of the kind | RB 0x823F4C00 venue_variant: the town file's venue (seattle's big_club_07) |
| Fans, buzz, cash | works | rb_career_band rb_bwt_buzz: buzz / fans from the performance table and (fans ...) line; cash 100 + 5 x 2 = 110 to the member | RB 0x823EF7D0, 0x823EE350, 0x823EE4A0 (D67, D75) |
| Vehicles (van, bus, jet) won | works (award) | unlock_van / unlock_bus / unlock_jet award the item; the newvan / newbus / newjet hints | tour_band.dta (award_item ...) |
| Staff (roadies, promoter, security, tour manager) | works (award); **broken** as requirements (above) | | tour_band.dta (item ...) |
| Active moments (double or nothing, charity, sellout) | missing | has_next_moment answers 0 | tour_band.dta (moments ...) [not read in the XEX] |
| Buzz caps hints, fans lost hint | missing | buzz_state / have_lost_fans answer 0 | tour_band.dta hints |
| Hall of Fame (win_item) and the win screen | **broken** | unreachable (Moscow had no gig); is_winning never 1 | TourBand is_winning RB 0x823EDB00 |
| Endless Setlist (hardcore_item, 58 songs) | **broken** | unreachable; is_hardcore_winning answered is_winning's flag | RB 0x823EC428; modes.dta tour_win_screen |
| Event names on the gig list | **broken** | the bare event name ("townall", "diy") | ui/eng/locale_events.dta (<town>_<venue>_<event>) |
| min_difficulty of a gig | missing | not checked anywhere | event record +0x60 (RB 0x823FF898); RB's check not read |
| Save / relaunch | works | rb_career_band_restarted_restarted (money, fame) | rb_profile.json |

### Characters and the Rock Shop

| Feature | Status | Evidence | RB reference |
|---|---|---|---|
| Create (name, gender, attitude, skin, hair, face, body sliders) | works | rb_career (rb_creator_attitude, rb_creator_look), rb_body_deform | ui/charcreator.dta (D68, D82) |
| Buy clothes at the catalog's price, worn on stage | works | rb_career: cash 100 - 99 = 1, flannelcoat_checked on stage | char/outfitloader.dta (D67) |
| Instruments, finishes, too dear | works | rb_shop_items (shop_error_money_screen, 5120_sparkle at 1299), rb_shop_drum, rb_shop_mic | char/instruments.dta, RB 0x82859E08 (D69, D76, D81) |
| Make-up, tattoos, face paint | works | rb_makeup, rb_tattoo (+ _restarted, a gig), rb_facepaint | D84, D93 |
| Money from gigs into the shop | works | rb_career: the gig's cash, then the shop | D67 |
| Save | works | rb_career_restarted, rb_shop_items_restarted, rb_tattoo_restarted | rb_profile.json |
| The creator's patch canvas preview, warp, held d-pad | missing | D93 left | patchcreator.dta |

### Screen flow

| Feature | Status | Evidence |
|---|---|---|
| Solo: char select, creator, hub, shop, gig guide, difficulty, hints, event list, game, endgame, gig complete, new unlock / new town, win, credits, send-off | works (after the fixes) | rb_full_solo screens line: ... tour_solo_win_screen > tour_solo_video_credits_screen > tour_solo_text_credits_screen > tour_solo_sendoff_screen > tour_solo_event_screen |
| Band: sel, create, hub, hints, town, venue, event, seldiff, game, next song, endgame, gig complete, win | works (after the fixes) | rb_full_band (below) |
| Back buttons | works | the walks' Triangle presses (shop, gig guide); frontend.py lists each screen's back |

## The test aid

`tests/walktest_career.cpp` (walk checks, no game rule of its own):
`rb_full_solo_autoplay` / `rb_full_band_autoplay` complete the tours'
gigs through the tour objects' natives as ui/endgame.dta does
(complete_song <stars> <score> per set song, complete_event, the hints'
advance), 5 stars a song, and recompute the expectations from the data
(the towns' order, each event's song, the bonus count, the regions'
travel need, the vehicles, every tier event played once). The gig that
ends each tour is left for the menus.

### Band World Tour as found, measured

rb_full_band on the code as found (GD05_BWTTOWNS_LEGACY=1, hometown
boston): every gig the town lists offer played, 5 stars a song: 71 gigs
(the tiers' 71 events, no town's own), in 12 towns (no Berlin, Stockholm,
Moscow, Tokyo, Sydney), first in San Francisco, not the hometown; 12
towns played without the vehicle RB's travel rule needs (the bus won
before the van); nothing left at the end: no hall of fame, no endless
setlist, no win.

## Fixes (this rundown)

Each with a legacy switch and a walk that fails before and passes after.

| Row | Cause | Fix | Switch | Before -> after |
|---|---|---|---|---|
| GD05-119 Solo Tour end | is_winning answered the endgame's own flag | RB 0x823E47E0 / 0x823CFA58 (D124) | GD05_TOURWIN_LEGACY | rb_full_solo: no winning gig -> song45 wins: tour_solo_win_screen, credits, send-off (guitar, drums, vocals) |
| GD05-119 Tokyo's download gig | the (download ...) template listed as a gig | RB 0x823FDB20: one gig per downloaded song, none (D124) | GD05_DOWNLOADGIG_LEGACY | 'download' left unplayable, Tokyo counted as the last town -> no Tokyo solo gig |
| GD05-118 Band towns, travel, staff, venues, win | tiers by order, town files unread, any town open | RB 0x823FE1D8 / 0x823FCED8 / 0x823F3AD0 / 0x823FD058 / 0x823EC890 / 0x823F4C00 / 0x823EDB00 (D123) | GD05_BWTTOWNS_LEGACY | rb_full_band: 71 gigs, 12 towns, 12 entered without the vehicle, no end -> 173 gigs, 17 towns, 0, Hall of Fame -> tour_band_win_screen, Endless Setlist -> tour_bwt_complete, kept through a restart |
| GD05-120 Gig names | rows filled list.lbl, the resources use event.lbl | event.lbl (D125) | GD05_EVENTLBL_LEGACY | blank rows -> "Say It Ain't So", "Make a Setlist", "Mystery Requests" (rb_bwt_diy shot); solo rows song + artist |

## After the fixes

- rb_full_solo, Medium: guitar, drum, vocals each: towns open in the
  data's order, 44 tier gigs + 13 bonus gigs, 0 wrong songs, song45
  played through the menus wins (screens ... tour_solo_gigcomplete_screen
  > meta_loading_tour_win_screen > tour_solo_win_screen >
  tour_solo_video_credits_screen > tour_solo_text_credits_screen >
  tour_solo_sendoff_screen > tour_solo_event_screen), 58 gigs kept through
  a restart, cash 9850.
- rb_full_band (boston): tiers in newyork 1_2, chicago 1_3, losangeles
  2_1, seattle 2_2, sanfrancisco 2_3, berlin 3_1 .. amsterdam 3_6; van at
  gig 7, bus 14, roadies 53, jet 57, promoter 97, security 147, tour
  manager 148; the Hall of Fame the only gig left, 8 songs through the
  menus (tour_nextsong_screen between), tour_band_win_screen; the Endless
  Setlist then open (status 2), is_winning / is_hardcore_winning 1, 58
  songs, tour_bwt_complete; restart: world fame 3215 and the item kept.
- rb_bwt_diy: Make a Setlist through tour_selsong_screen twice, the set
  missqueen inbloom, the game started.
- Regression: rb_career, rb_career_band, rb_tour_rules, rb_band_lobby,
  rb_shop_items OK on the fixed build.

## Rundown 2 (the audit's leftovers, the screens through the menus)

New walks: `rb_career_screens_band` (the real band lobby, two characters
made there, a new band, the hub, the gig guide, the difficulty screen, the
shop, band profile, band roster) and `rb_career_screens_solo` (a new
guitarist, the hub, the difficulty, the gig guide and its unlock row, the
shop, the character profile). Each screen: `rb_texts:<tag>` (a shot, the
labels shown with no text or "Loading"), plus row / data checks. Shots in
`agents/scratch/career17/shots/` (git-ignored).

| Row | Found | Cause | Fix | Switch | Before -> after |
|---|---|---|---|---|---|
| GD05-126 hub "Loading" | four "Loading" labels, no band in the practice space | slot characters drawn only on lobby screens; the CharLoadingPanel's loading<i>.lbl never hidden | RB 0x828433C0: the mode's slots' players' characters unless show_char FALSE, loading only while loading; coop_hub's band-target teleports (D130) | GD05_HUBCHARS_LEGACY | Loading 4 -> 0; members' pixels 4.6 % (29 % left) -> 19.2 % (82 % left) |
| GD05-127 gig row labels | star / fame / medal / new / songs / item / town / venue / score empty | the providers filled event.lbl only | EventProvider::Text RB 0x823F6848, SoloEventProvider::Text RB 0x823FA578; medal and song score records (D131) | GD05_GIGROWS_LEGACY | band rows star 0 -> 1, medal 0 -> 1, new 0 -> 4, songs 0 -> 5; solo star / score 0 -> 1, new 0 -> 5 |
| GD05-122 golden / clear instruments | the Hall of Fame awarded no instrument | TourBand complete_event's win_instruments not done | RB 0x823EF0F0 (D133) | GD05_WININSTR_LEGACY | rb_full_band: clear guitar not owned -> owned, event_unlock win_instruments; solo gold: already awarded (now checked) |
| min_difficulty | works | seldiff.dta disables the difficulties below {{tour event} get min_difficulty} (the port answered it) | none: the audit's "missing" was wrong | | rb_seldiff_min: song45 (Medium) easy disabled; localartists (Hard) easy, medium |
| GD05-123 Rio unlock row | bonus gigs inline | the provider had no modes | RB 0x823FA248 / 0x823FA578 modes (D132) | GD05_SOLOUNLOCK_LEGACY | no unlock row -> row, X: mode 1 (unlock1), Triangle: mode 0 |
| GD05-125 moments, buzz caps, hints | never offered; cap by vehicle | natives answered 0 | LocalTourBand RB 0x8240BE80 / 0x8240A768 / 0x8240A088 / 0x823EEAE8 (D134) | GD05_MOMENTS_LEGACY, GD05_BUZZCAP_LEGACY | see GD05-125 |
| GD05-128 hometowns | data order | the provider sorts by localized name | RB 0x823FC338 / 0x823F4768 (D135) | GD05_HOMETOWN_LEGACY | newyork chicago boston ... -> amsterdam berlin boston ... stockholm |
| GD05-124 profile screens | character profile "kDataUnhandled", band profile all "kDataUnhandled", band roster no rows | CharProfilePanel / BandProfilePanel / BandLineupPanel natives missing | RB 0x82841A00 / 0x828388C0 messages (D136) | GD05_CHARPROFILE_LEGACY, GD05_BANDPROFILE_LEGACY | 18 unhandled natives -> 0 |

Screens walked with labels and textures (rb_texts, shots looked at):
tour_band_sel, matchmaking_band (lobby), tour_band_create, tour_band_hub,
tour_band_venue, tour_band_event, tour_band_seldiff, shop (band),
tour_band_profile, tour_band_lineup; tour_solo_char, charcreator_maker,
tour_solo_hub, tour_solo_diff, tour_solo_event (normal and unlock), shop
(solo), tour_solo_char_profile. Win screens: rb_full_solo /
rb_full_band (tour_solo_win_screen, credits, send-off;
tour_band_win_screen).

## Left (not fixed)

- The profile screens' damask background draws bright yellow, so the grey
  labels on it are hard to read (char / band profile shots): material or
  colour of tour_char_profile.milo / tour_band_profile.milo not checked.
- The guitarist's arm in the practice space (lobby, hub, roster) is held
  up behind the head: the lobby's idle / attitude clip (seen before this
  rundown, not investigated).
- The solo tiers (tier1_1 ...) have no locale token: RB's Localize would
  show the token; the port leaves the list's town / venue labels empty
  for them (D131) [I].
- tour_solo_diff.milo's / tour_band_event.milo's loading.lbl are posed
  shown but draw nothing (allowed in the walks).
- Where RB clears the moment taken, and the event's (sellout TRUE) field,
  were not traced (D134).
- The Easy band can't reach buzz-blocked gigs now (RB's caps): intended.
- The creator's patch canvas preview, warp, held d-pad (D93 left).
