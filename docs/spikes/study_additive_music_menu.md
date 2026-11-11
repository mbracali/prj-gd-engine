# Additive music menu: every installed game's songs in every game (#58)

Status: study, 2026-11-10. GitHub issue #58 ("Song Sharing"): with GH1 and
GH2 installed, play the GH1 songs inside GH2, and so on for every game. The
owner calls it the **Additive music menu**; 1.1.13 laid the groundwork
(gd-engine-core, "Preparation for mod support") but not the feature.

## Verdict

**Do it, in three waves, with one shared song library in gd-engine-core
(`gd::core::songs`) and a thin adapter in each game.** The launcher already
knows every disc the user has (`Catalog`); it passes them all to the module
it starts (an appended `GdHost` field). The core reads each foreign disc's
song list, chart and audio **at load time, in memory**: nothing is written
to any disc and no song file is copied. Each host keeps its original song
list untouched and shows the extra songs **after its own, in a separate
section per source game** (GH1 / GH2 / Encore: header rows after the
bonus songs; GH3: its own DOWNLOADS tab; RB: an "other games" section).
Foreign songs are **quickplay only** (no career, no store, no practice in the
first wave), play by the **host's rules** (hit window, scoring, Star Power)
with the **source's HOPO marking**, and keep their scores in a separate
versioned JSON next to the host's save.

Order: **(1) GH1 + GH2 + Encore** (all Harmonix: ARK, DTA/DTB, MIDI,
VGS / mogg), **(2) GH3** (pak / qb notes, FSB XMA audio), **(3) Rock Band**
(guitar and bass only in the GH games; GH songs as guitar / bass songs in
RB). Effort: about **11 to 13 hours of agent time** (see "Estimate"), plus
the owner's tests.

## Where we are

| | gd01 GH1 (PS2) | gd02 GH2 (360) | gd03 Encore (PS2) | gd04 GH3 (360) | gd05 RB1 (360) |
|---|---|---|---|---|---|
| Disc | ISO 9660 + ARK (`data/iso9660`, `game_data`) | XDVDFS + ARK (`data/xdvdfs`, `game_data`) | ISO 9660 + ARK (`crypt` for its header) | XDVDFS or folder, `DATA/COMPRESSED` deflate (`data/disc`, `pak`, `qb`) | XDVDFS + ARK |
| Song list | `config/songs.dta` → `SongInfo` (`game/song_db.h`), only songs whose chart and audio exist | `config/gen/songs.dtb` (`_SHIP` entries), `song` / `song_coop` blocks | as gd02 (GH2 engine) | `scripts\guitar\songlist.qb`: `gh3_songlist` (order) + `gh3_songlist_props` (`SongDb`) | `songs.dtb` read as Rock Band's |
| Chart | Harmonix MIDI (`data/midi`, `game/chart`): 4 difficulties, Star Power phrases, HOPO if single, other fret, gap ≤ 169 ticks | Harmonix MIDI: parts Guitar / GuitarCoop / Bass / Rhythm, HOPO gap < `hopo_threshold` (strict), venue notes (BAND DRUMS / BASS, lighting, effects) | as gd02 | `songs\<name>.mid.qb` arrays per part and difficulty, star / battle / boss phrases, cameras, lightshow, drums, anim; HOPO by time tolerance × `hammer_on_measure_scale`, flip notes (`note_track.cpp`) | RB MIDI: Guitar / Bass / Drums / Vocals, Overdrive (116), solos (103), fills (120-124); drums have no HOPOs |
| Audio | `.vgs` PS-ADPCM multitrack, per-channel pan / vol / core (guitar) | `.mogg` v10 (plain Ogg, stb_vorbis), tracks → channels | `.vgs` (`audio/vgs_stream`) | `MUSIC/<name>.fsb.xen`: FSB3, FMOD-encrypted, XMA1 stems (guitar, rhythm, song) through FFmpeg | `.mogg` v11 (AES-128 CTR, own `aes128`) |
| Song select | `SongProvider`: rows per campaign venue + `store` (`docs/song_list.md` §2) | `song_provider`, tier headers + bonus (`docs/ui_and_frontend.md`) | as gd02 | SETLIST / BONUS / **DOWNLOADS** tabs (`docs/ui.md`, `setlist_tabs`); `Downloads_EnumContent` exists in the flow | RB's song select (sorts) |
| Save | own JSON (`game/campaign`, per-song high scores) | GH2's profiles (high scores at profile +0x64), port save | as gd02 | GH3 global tags as versioned JSON (`game/save.h`, L12) | RB profiles |
| FFmpeg | no | yes (Bink) | yes | yes (Bink + XMA) | yes |

The launcher (`gd-engine-launcher/src/catalog.h`) reads `games.yaml` →
every `game-info.yaml`, identifies each disc by its signature and keeps one
`iso` per game (saved in `config.txt`; Android keeps a `content://` URI and
hands the module `/proc/self/fd/<n>`). A module is told **only its own
disc**: the last `argv` of `GdHost` (`sdk/gd_host.h`, ABI 1, fields only
appended, `GD_HOST_HAS`).

"Mod support preparation" (1.1.13) is: the games read their song lists from
the data, not from fixed tables (gd05 kickoff §8: "keep the song library open
for added songs later"), and the shared core (`gd-engine-core`: platform
disc reader, audio `Output`, image, render). There is **no mod or overlay
loader yet**; `gd-tool-03` ("Music and DLC Mods") is an empty concept, and
GD01-207 / 214 (modding, new songs) are parked. This study gives song mods a
place to land later (a "Mods" source next to the discs) but does not build it.

Each game has its own copy of the readers (dta, midi, mogg, vgs, xdvdfs,
iso9660, ark), mostly forks of each other. That is what wave 1 consolidates.

## Design

### 1. Launcher → module: every disc, not only the host's

- `GdHost` gets appended fields (ABI stays 1, `size` grows):
  `const GdDiscRef* discs; uint32_t disc_count;` with
  `GdDiscRef { const char* game_id; const char* path; }`, one per game whose
  disc the catalog knows (playable or not: the foreign game only needs a
  reader in the core, not a finished port). The host's own disc stays the
  last `argv`. On Android each gets its own `/proc/self/fd/<n>`, as today.
- Standalone runs and walks: `--extra-disc <game-id>=<path>` (repeatable) and
  `GD_EXTRA_DISCS="<id>=<path>;..."`.
- A module built against ABI 1 without the field simply sees no foreign
  songs. Nothing else in the launcher changes; no new screen.

### 2. `gd::core::songs` (new area in gd-engine-core)

```
struct SourceDisc { std::string gameId, path; };
struct SongRef {                      // one foreign song, metadata only
  std::string key;                    // "gd-game-01:aceofspades" (stable, saved)
  std::string gameId, songId, title, artist, year, sourceLabel; // "Guitar Hero"
  uint32_t parts;                     // bit per Part (guitar, coop, rhythm, bass, drums, vocals)
  std::array<int8_t, 6> rank;         // RB tier ranks, -1 none
  float previewStartMs, previewEndMs, bpm;  // bpm: the first tempo (anim tempo)
};
class Library {
  static Library open(std::vector<SourceDisc>, std::string cacheDir, std::string hostGameId);
  const std::vector<SongRef>& songs() const;            // index only, cheap
  std::unique_ptr<LoadedSong> load(const std::string& key, uint32_t parts) const;
};
struct LoadedSong { NeutralChart chart; std::unique_ptr<StemStream> audio; };
```

- **NeutralChart**: tempo map (tick ↔ ms), time signatures, beat lines; per
  part and difficulty the gems `{ms, tick, durMs, durTicks, frets (5 bits),
  hopo, phrase}` with HOPOs **already marked by the source game's rule**;
  Star Power / Overdrive phrases; solos; drum notes and vocals (RB only);
  sections; animation hints when the source has them (Harmonix BAND DRUMS /
  BASS notes, GH3 `drums` / `anim` arrays, singer phrases).
- **StemStream**: pull float PCM, `seek(ms)`, channels tagged with a role
  (guitar, rhythm, bass, drums, vocals, backing) plus pan and volume from the
  song entry; the guitar role is what the host mutes on a miss.
- **Readers, one per source** (moved from the games, which then use them
  too, so there is one copy): ARK (PS2 and 360 variants, Encore's encrypted
  header), DTA / DTB, Harmonix MIDI (GH1, GH2 / Encore, RB parsers), VGS,
  mogg v10 / v11 (stb_vorbis, AES-128), XDVDFS, ISO 9660; GH3 pak / qb /
  qbkey, FSB3 decryption, XMA via FFmpeg (wave 2).
- **Index cache**: `<host save folder>/song_index/<game-id>-<size>-<mtime>.json`
  (versioned, L12) with the `SongRef`s; a disc is opened and its song list
  parsed only when the cache is missing or stale. Charts and audio are read
  only when a song starts (lazy), audio streamed as each host already does.
- The host's own disc is skipped (no duplicates); a song id present on two
  sources is shown under both (they are different recordings or charts).

### 3. How each game shows them

The original list stays exactly as it is; the extra songs come after it,
only in quickplay (and multiplayer modes the chart supports), behind an
Enhanced option **Songs from Other Games** (Off / On, default On when another
disc is known; the owner may prefer Off: it is the one non-original thing on
the list).

| Host | Where | How |
|---|---|---|
| gd01 GH1 | after the `store` (bonus) rows | one header row per source ("GUITAR HERO II", "ENCORE", ...), the songs under it; the provider's header table grows by one pair per source |
| gd02 GH2, gd03 Encore | after the bonus tier | same idea: tier-like headers per source in `song_provider` |
| gd04 GH3 | the **DOWNLOADS** tab (GH3's own DLC tab, empty in the port) | `Downloads_EnumContent` / download songlist filled from the library; headers per source |
| gd05 RB | its song select, a source section after the disc's songs | RB sorts get "Other games" as a group [I: RB1's DLC grouping to confirm in the code] |

### 4. Parts, difficulties, instruments

- Easy / Medium / Hard / Expert map one to one in every game.
- 5-fret guitar is common ground. Lead guitar always; GH2 / Encore co-op and
  bass / rhythm parts when the host has a second-player part (GH1 has none,
  so GH1 hosts lead only). GH3 rhythm / bass and co-op map to GH2's Bass /
  Rhythm / GuitarCoop by the song's `rhythm_track` flag.
- RB songs in the GH games: guitar and bass only; drums and vocals are mixed
  into the backing. GH songs in RB: guitar and bass; a drums or vocals
  player cannot pick them (greyed, "not on this song").
- HOPOs: marked by the source (GH1 ≤ 169 ticks; GH2 / Encore / RB1 strict
  `< hopo_threshold`; GH3 time tolerance × `hammer_on_measure_scale` with
  flip notes), so a song plays as it was charted. Each host's chart builder
  takes preset flags for foreign songs (gd04's `note_track` skips its own
  natural / flip computation for them).
- Star Power: the source's phrases become the host's (GH3 battle / boss /
  face-off arrays dropped; RB Overdrive = GH Star Power). Solos and drum
  fills are dropped in GH hosts.

### 5. Timing and scoring

The host's rules apply: hit window, gem and sustain points, multipliers,
Star Power length and activation, rock meter, whammy. The calibrated audio
and video delays (GDALL-038) apply as for the host's songs. Per-song offsets
the source carries (GH3 `gemOffset` / `inputOffset`) are applied to the
chart at conversion, so the host never sees them.

### 6. What a foreign song lacks, and what to show

- **Venue**: the host's quickplay venue choice (its default rotation, or the
  venue the player picked). No source venues are loaded.
- **Band**: the host's default band; guitarist tempo from the song's BPM
  (GH1's `animTempo`: fast at 140 bpm and up [I: threshold to pick]).
  Drummer and bassist from the source's animation notes when present
  (Harmonix BAND tracks, GH3 drum arrays), else generated from the beat lines
  and the bass / guitar gems. Singer: the host's idle singing loop, no lip
  sync. Guitarist character / guitar: the player's choice.
- **Camera and lights**: the host's director with no song events (GH2's and
  GH3's directors already have defaults), or beat-driven cuts in GH1.
- **Practice**: not offered (no slowed audio on the disc for foreign songs).
  A later item can add time-stretched practice.
- **Album art / song intro text**: the host's generic one, with the source
  game's name as the "from" line.

### 7. Save data

`<host save folder>/shared_songs.json` (versioned, atomic with `.bak`, L12):
per song key, part and difficulty the best score, stars, percent and best
streak. The original save formats are not touched; foreign songs never count
for career totals, unlocks or money. If a disc is removed its songs disappear
from the list and their scores stay for when it comes back.

### 8. Performance

Opening a foreign disc and parsing its list costs a few hundred ms (ARK
header + one DTB / qb); the index cache makes later starts cost only a stat
per disc. The list is built once per boot. Song start reads one chart and
opens one audio stream, like a host song. Memory: one extra open disc handle
during a foreign song. XMA (GH3 songs) needs FFmpeg's audio decoders: gd01
gets the core's audio-only FFmpeg build in wave 2, or GH3 songs stay hidden
in gd01 until it does.

### 9. Legal

Only discs the user owns and points the launcher at are read, the same way
each game reads its own; everything is converted in memory; nothing is
written to a disc or copied out of it, and nothing is downloaded. The index
cache holds titles and ids only, in the user's save folder. The README and
the release notes say it: "songs come from your own discs".

## Plan and estimate

Measured agent time from the git history, for comparable work:
gd-engine-core with gd01 on it (11-06 00:34 → 01:41, about 1 h), gd02 on the
core (11:35 → 12:37, about 1 h), gd03 + gd04 on the core in parallel
(15:48 → 17:14, about 1.5 h), their packages (about 1 h); gd05 from a copy
of gd02 to RB charts and decrypted moggs (11-09 14:05 → 14:25); GH3 playable
(0.1.12) about 2 h.

| Wave | Work | Agent time |
|---|---|---|
| 1 | `GdHost` discs + `--extra-disc`; `gd::core::songs` (Library, index cache, NeutralChart, StemStream); ARK / DTA / DTB / Harmonix MIDI / VGS / mogg v10 readers moved into the core; GH1, GH2, Encore as sources and hosts (sections, Enhanced option, shared_songs.json, fallbacks); walks: each host plays one song of each other source | 4 to 5 h |
| 2 | GH3 as a source (pak / qb / FSB / XMA in the core, FFmpeg audio-only for gd01) and as a host (DOWNLOADS tab, preset HOPO flags) | 3 h |
| 3 | RB as a source (mogg v11, RB MIDI guitar / bass) in the GH hosts; RB as a host (section, greyed drums / vocals) | 2 to 3 h |
| — | Packages (every module, `verify_bundle`), owner test and fixes | 2 h |

Total: **about 11 to 13 hours of agent time**, two to three days with the
owner's testing in between. Wave 1 alone gives the issue's example (GH1
songs in GH2) and is a good release point.

Risks: GH1's and GH2's song select panels are scripted (DTA) with native
providers, so the extra header rows must not upset scroll and tier logic;
RB's song select grouping is not studied yet; GH3's DOWNLOADS flow expects
DLC paks (the library has to stand in for `LoadPakAsync`); gd-game-04 is
being changed by another task now, so its host work waits for that.

## Sources (in-repo)

- gd-engine-launcher/src/catalog.h, module_host.cpp, sdk/gd_host.h
- gd-game-01/engine/src/game/song_db.h, chart.cpp; gd-game-01/docs/song_list.md
- gd-game-02/engine/src/game/song_db.h, chart.h / .cpp, data/mogg.h; gd-game-02/docs/songs.md, ui_and_frontend.md
- gd-game-03/engine/src/game/chart.cpp, audio/vgs_stream.h
- gd-game-04/engine/src/game/song_db.h, chart.h, note_track.cpp, save.h; data/disc.h, fsb.h; audio/xma_decoder.h; gd-game-04/docs/ui.md, flow.md
- gd-game-05/engine/src/game/chart.h, data/mogg.h; docs/spikes/GD-GAME-05_KICKOFF.md §8
- docs/tasks/gd-backlog.md (GDCORE-001..010 times, GD01-207 / 214)

## Backlog candidates

- **(GDALL, Enhancement) #58 Additive music menu**: GDALL-042 (this study).
  Split into the three waves when it starts:
  - wave 1: launcher discs + core song library + GH1 / GH2 / Encore;
  - wave 2: GH3 source and host;
  - wave 3: Rock Band source and host.
- Later: practice for foreign songs (time-stretch); song mods from a folder
  as one more source of the same library (GD01-207 / 214, gd-tool-03).
