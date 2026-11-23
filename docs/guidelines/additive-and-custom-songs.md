# Additive songs mod and custom songs

The owner's requirements (request of 2026-11-22), captured as a guideline so
they survive outside any one task. Work happens in
`feature/20261122-additive-songs-mod`. Read with
[universal-menus.md](universal-menus.md), [universal_launcher.md](universal_launcher.md)
and the engine-side study [../spikes/study_additive_music_menu.md](../spikes/study_additive_music_menu.md)
(GitHub #58, GDALL-042). This spec **extends and in places supersedes** that
study: the study's inline per-game song sections become a **universal overlay
menu** (§7 below), and the launcher gains a **management tool** (§5) and a
**UI refresh** (§2).

## 1. What the additive songs mod is

Each rhythm game we ship can access and play the songs of the **other** games
the user has loaded, plus **custom songs** the user drops in (Clone Hero /
YARG / Guitar Hero DLC / Rock Band formats). It is universal to every game we
work on from now on.

- Songs are read **directly from each original ISO** (no extraction/copy), as
  the engine study describes (`gd::core::songs`).
- Custom songs live in a **folder at the root of the game**, present in every
  build (desktop and Android).
- Example: playing GH2 with GH1 + GH3 + RB1 discs loaded → GH2 gains new
  by-game song lists reachable from quickplay (left/right), one list per source.

## 2. Universal launcher UI refresh (Wave 1)

Refresh the game-row UI. Use **Material icons** throughout.

- **Playability % icon** (replaces the filled green bar), colour by bucket:
  - not booting yet → `white(circle)`
  - 10 % → `neon_yellow(clock_loader_10)`
  - 20 % → `neon_yellow(clock_loader_20)`
  - 20–40 % → `neon_yellow(clock_loader_40)`
  - 40–60 % → `neon_green(clock_loader_60)`
  - 60–80 % → `neon_green(clock_loader_80)`
  - >80 % → `neon_green(clock_loader_90)`
  - 100 % → `gold(stars)`
  - the header shows a `percent` icon for the column.
- **Buttons** (coloured only when the game's ISO is found and ready; grey
  otherwise — **except Load ISO**, see below):
  - Play → `green(play_circle)`
  - Load ISO → ISO available: `yellow(album)`; not available: `yellow(disc_full)`.
    Load ISO stays **yellow for every not-found game** and grey for found ones.
  - Additive music menu → `neon_red(music_note_add)`
  - Mod → `neon_purple(add_shopping_cart)` — **disabled for now**; hover tip
    "Coming soon".
  - Explore files → `blue(database_search)`
- **Dream Forge engine** button in the header: `yellow(handyman) Dream forge
  engine`; **not selectable**; hover tip "Coming soon!".
- **Layout**: a table with a header row of column names
  (`GAME NAME | % Play | Load iso | Additive musics | Mod menu | Explore files`).
  The **Play button comes before the game name**. Sticky **horizontal and
  vertical** scrollbars. Footer "Developed with ❤ by Marcelo Bracali".
- If a user clicks **Play** on a not-found game, open the **Load ISO** flow.

## 3. Additive Music management tool (Wave 1, §5 of the request)

A launcher screen reached from each game's **Additive music menu** button.

- **Left (2/5 of the screen)**: a table of **playable** games only, columns:
  - Game name (module + name, exactly as the main list shows it)
  - Number of songs (that game's song count)
  - Additive musics (checkbox)
  - Custom songs (checkbox)
- **Right**: a box to create / delete / rename folders and drop new music —
  the folder at the game's root. Traditional folder-tree navigation. A button
  to open the folder in the OS file explorer. **Each song is its own folder**;
  many folders allowed for organising sets.
  - Dropping a song runs a **quick validation** (is this a valid song?).
  - Bulk/manual moves: detect new content and **quick-validate all** songs.
  - A button to **evaluate all** songs with the quick validation.
  - Right-click a song → remove / quick-evaluate / rename / open in OS explorer.
- **Bottom buttons**: Save, Back (to the launcher), Refresh.
- A **loading screen** is fine when the tool opens and when the user saves.
- **Additive checkbox**: on → this game's songs are available in other games,
  and other games' songs (that also have it on) are available here. Direct
  loading from each original ISO (no extraction).
- **Custom songs checkbox**: on → this game loads every custom song from the
  custom-songs folder.

## 4. Per-game stem behaviour (§6 of the request)

When a song from another game or a custom song plays in a host, each stem is
either **played normally** (the host has that instrument) or **added to the
background mix** (the host does not). If the host has instruments the song
lacks, those gameplay tracks are left empty and the mix still plays.

| id | Game | Guitar | Bass | Drum | Vocal | Keys |
|---|---|---|---|---|---|---|
| gd-game-01 | Guitar Hero | play | mix | mix | mix | mix |
| gd-game-02 | Guitar Hero II | play | play | mix | mix | mix |
| gd-game-03 | Encore: Rocks the 80s | play | play | mix | mix | mix |
| gd-game-04 | Guitar Hero III | play | play | mix | mix | mix |
| gd-game-05 | Rock Band | play | play | play | play | mix |
| gd-game-06 | Guitar Hero: Aerosmith | play | play | mix | mix | mix |
| gd-game-07 | Guitar Hero World Tour | play | play | play | play | mix |
| gd-game-08 | Rock Band 2 | play | play | play | play | mix |
| gd-game-09 | The Beatles: Rock Band | play | play | play | play | mix |
| gd-game-10 | Guitar Hero: Metallica | play | play | play | play | mix |
| gd-game-11 | Guitar Hero: Smash Hits | play | play | play | play | mix |
| gd-game-12 | Guitar Hero 5 | play | play | play | play | mix |
| gd-game-13 | Lego Rock Band | play | play | play | play | mix |
| gd-game-14 | Band Hero | play | play | play | play | mix |
| gd-game-15 | Guitar Hero: Van Halen | play | play | play | play | mix |
| gd-game-16 | Green Day: Rock Band | play | play | play | play | mix |
| gd-game-17 | Warriors of Rock | play | play | play | play | mix |
| gd-game-18 | Rock Band 3 | play | play | play | play | **play** |

**Not compatible** with the additive menu or custom songs: gd-game-19 (RB4),
20–23 (On Tour / Band Hero DS), 24 (RB Unplugged), 25–26 (DJ Hero 1/2).

Notes: games with extra stems (e.g. two guitars) preserve the original
behaviour in their own game and mix the extras elsewhere. Games not decompiled
yet only get general guidelines until implemented. A GH1 song played in RB
(no stems/notes) just plays the mix with no notes.

## 5. Universal custom/shared songs in-game menu (§7 of the request)

A new **core universal menu**, reusable by every game, reached from
**quickplay** by a new **"Custom songs"** button. Pick the button colour per
game by this order, avoiding colours the game's quickplay already uses:
yellow → blue → orange → select. Mouse can click it. Do not overlap existing
buttons; adapt the layout per game.

Opens a **semi-transparent overlay** over quickplay, same look & feel as our
custom menus:

- Top: a **search bar** (search songs by name).
- Centre: a box listing songs by folder — first every game with sharing on,
  then the custom-songs folders (up to two sub-levels). All folders start
  **collapsed**; the game currently played also appears. Each song shows
  **instrument icons** (focus: Guitar, Bass, Drums, Vocals).
- Controls: Green = select/expand-collapse; Red = back to quickplay; Blue =
  next folder; Orange = change sort order (disabled for now).
- The lists include **cut content** (e.g. GH1's in-disc unused songs).

## 6. Band behaviour for custom songs (§8 of the request)

- Trigger **random animations** when a custom song has no predefined ones.
- If a custom song is a **DLC of the game being played**, load that game's
  proper mechanisms for it; otherwise fall back to random. May warrant its own
  wave.

## 7. Formats and engineering notes

- Custom song formats to support: **Clone Hero, YARG, Guitar Hero DLC, Rock
  Band**.
- Build **universal core components**; code must be extensible (many games,
  some not decompiled yet).
- "We cannot break anything." Understand each game deeply; test on Linux
  first, then the other platforms.

## 8. Waves

1. **Launcher UI refresh (§2) + Additive Music management tool (§5)** — the
   launcher side: settings persisted, custom-songs folders created/validated.
   *(in progress, 2026-11-22)*
2. The engine song library (`gd::core::songs`, the study's wave 1) + the
   universal in-game menu (§5 here) wired into **GH1 and GH2** with the stem
   rules (§4) → **stop for the owner to test**.
3. Remaining games, custom-song format parsing, band behaviour (§6).
