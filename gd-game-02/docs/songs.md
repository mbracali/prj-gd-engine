# Guitar Hero II (Xbox 360): song list

Sources (decoded with `tools/dtb.py`; text copies in `extracted/dta/`):

- `config/gen/songs.dtb`: one entry per song (91 entries: 48 career, 26 bonus,
  9 tutorial, 8 test entries inside `#ifndef _SHIP`).
- `config/gen/campaign.dtb`: `order` gives the 8 career tiers. Each tier is
  keyed by its venue and lists 6 songs in play order.
- `ui/eng/gen/locale.dtb`: tier titles (`song_header_<venue>`), venue names,
  `performed_by` / `mtv_made_famous` captions.
- `config/gen/store.dtb`: the `song` list is the 26 bonus songs bought in the
  store, all at 550 cash.
- `.mogg` headers in `songs/<id>/`: the channel count and sample rate in the
  table come from the Ogg Vorbis identification header. Every mogg is version
  10 (unencrypted). The channel count always equals the length of the entry's
  `pans`.

Other files that list songs by id: `config/gen/leaderboards.dtb` (Live
leaderboard views per song), `config/gen/contexts.dtb` (Live rich-presence
values), `config/gen/coop_max_scores.dtb` (four numbers per song, not decoded
yet), `config/gen/arkbuild.dtb` / `arkorder*.dtb` (build lists), and
`ui/gen/endgame.dtb` (achievement checks such as beating `trogdor` and
`thunderhorse`).

## Fields in a song entry

`(<id> (name "...") (artist "...") ...)`. The id is the symbol used everywhere
else (campaign, store, leaderboards, file paths).

| Field | Present | Meaning |
|---|---|---|
| `name` | all | Title shown in game (string). |
| `artist` | all | Artist (string). Tutorials and tests use "You". |
| `caption` | 30 | Always `performed_by` ("as performed by"): the audio is the original master recording. Without it the game shows `mtv_made_famous` ("as made famous by"), i.e. a cover. All 26 bonus songs have it, plus `dead`, `johnthefisherman`, `possum` and `stop`. |
| `song` | all | Main audio and chart (sub-fields below). |
| `song_coop` | 17 | Separate audio for co-op (`<id>_coop.mogg`). Its second part is `rhythm` instead of `bass`. Same MIDI file. |
| `song_practice_1..3` | 68 | Practice-mode versions, one per slower speed (`<id>_p85`, `_p65`, `_p50`, or `_p90`/`_p75`/`_p60`). Each lists 2 channels (`bass 0`, `guitar 1`). **None of these audio files is on the disc** (see notes). |
| `practice_speeds` | 69 | Four percentages: full speed, then the speeds of `song_practice_1..3` (`100 85 65 50` or `100 90 75 60`). `kicked` has this but no `song_practice_*`. |
| `anim_tempo` | all | `kTempoMedium` (1) or `kTempoFast` (2): picks the band and character animation set (as in GH1). |
| `preview` | all | Start and end of the song-select audio preview, in milliseconds. |
| `quickplay` | all | Defaults for Quickplay: `character_outfit`, `guitar`, `venue`. |
| `band` | 11 | Extra band members shown on stage (`metal_bass`, `metal_drummer`, `metal_keyboard`, `female_singer`). |
| `bpm` | 17 | Only on tutorial and test entries (108 or 120). |
| `validate_ignore` | 17 | `TRUE` on tutorial and test entries; presumably tells the build-time validator to skip them. |

Sub-fields of `song`, `song_coop` and `song_practice_N`:

| Field | Meaning |
|---|---|
| `name` | Audio path without extension: the game adds `.mogg` (multitrack Ogg Vorbis). |
| `tracks` | Instrument to channel map: `(guitar (2 3))`, `(bass 4)`, `(rhythm (4 5))`. Channels not listed are the backing band (always channels 0-1). |
| `pans` | One pan per channel, -1.0 left to 1.0 right. Stereo pairs are `-1 1`, a mono part is `0`. |
| `vols` | One volume per channel in dB (negative = attenuation). |
| `cores` | One flag per channel: `1` on the lead-guitar channels, `-1` elsewhere. Inference: the "core" channels are the ones that drop out when the player misses (the guitar track). |
| `midi_file` | Chart (`songs/<id>/<id>.mid`). |
| `hopo_threshold` | 31 blocks (`250`): overrides the hammer-on/pull-off note spacing for this song (in MIDI ticks, presumably). The default is set elsewhere. |

`campaign.dtb` also has `required_songs` (two numbers per difficulty:
Easy 4 5, Medium 5 6, Hard/Expert 6 6; probably songs needed to clear a tier
and to unlock the encore, not confirmed), cash awards, and
`unlock_game_modes multi_fo`.

## Notes

- Audio files: 91 `.mogg` (74 songs + 17 co-op) and 69 `.voc` (unknown;
  probably singer lip-sync data) in `songs/`, plus `tutorial/*.mogg`. The test
  entries reuse `songs/yyz/yyz.mogg` with charts in `test/budget_songs/`,
  which are not in the archive.
- Sample rates differ by song: 32000, 32250 or 44100 Hz.
- Every song has band 0-1 and guitar 2-3 (stereo). Bass or rhythm is 1 or 2
  channels. Three co-op moggs have mono guitar (`guitar 2, rhythm 3`).
- The practice moggs (`*_p85` etc.) referenced by 68 songs are missing from
  the Xbox 360 archive. The 360 version probably slows the normal audio for
  practice. The entries look carried over from the PS2 version.
- Some notes below are abbreviated: "master" = has `caption performed_by`;
  "qp venue" = Quickplay default venue; "no practice" = no `practice_speeds`
  or `song_practice_*`.

## Songs

Career songs are in tier order, then bonus, tutorial and test entries.
Tier column: tier number, title, venue key, position in the tier (#6 is the
last song; inference: it is the tier's encore).

| id | Name | Artist | Career tier / unlock | Audio (mogg: channel layout) | Co-op audio | MIDI | Notes | Preview ms |
|---|---|---|---|---|---|---|---|---|
| `surrender` | Surrender | Cheap Trick | 1. Opening Licks (battle) #1 | `songs/surrender/surrender.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/surrender/surrender.mid` | qp venue small2 | 46000 66000 |
| `possum` | Possum Kingdom | Toadies | 1. Opening Licks (battle) #2 | `songs/possum/possum.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/possum/possum.mid` | master, qp venue battle | 40000 60000 |
| `heartshapedbox` | Heart-Shaped Box | Nirvana | 1. Opening Licks (battle) #3 | `songs/heartshapedbox/heartshapedbox.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/heartshapedbox/heartshapedbox.mid` | qp venue big | 44000 63300 |
| `salvation` | Salvation | Rancid | 1. Opening Licks (battle) #4 | `songs/salvation/salvation.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/salvation/salvation.mid` | qp venue small1 | 10000 30000 |
| `strutter` | Strutter | Kiss | 1. Opening Licks (battle) #5 | `songs/strutter/strutter.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) | `strutter_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/strutter/strutter.mid` | qp venue big | 48950 69300 |
| `shoutatthedevil` | Shout At The Devil | Mötley Crüe | 1. Opening Licks (battle) #6 | `songs/shoutatthedevil/shoutatthedevil.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/shoutatthedevil/shoutatthedevil.mid` | qp venue small2 | 65500 85500 |
| `mother` | Mother | Danzig | 2. Amp-Warmers (small1) #1 | `songs/mother/mother.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) | `mother_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/mother/mother.mid` | qp venue battle | 61100 81500 |
| `lifewasted` | Life Wasted | Pearl Jam | 2. Amp-Warmers (small1) #2 | `songs/lifewasted/lifewasted.mogg` (5 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/lifewasted/lifewasted.mid` | no practice, qp venue big | 16000 36000 |
| `cherrypie` | Cherry Pie | Warrant | 2. Amp-Warmers (small1) #3 | `songs/cherrypie/cherrypie.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/cherrypie/cherrypie.mid` | qp venue small1 | 13000 29700 |
| `woman` | Woman | Wolfmother | 2. Amp-Warmers (small1) #4 | `songs/woman/woman.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/woman/woman.mid` | qp venue battle | 15700 38400 |
| `youreallygotme` | You Really Got Me | Van Halen | 2. Amp-Warmers (small1) #5 | `songs/youreallygotme/youreallygotme.mogg` (5 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/youreallygotme/youreallygotme.mid` | qp venue small2 | 16700 36700 |
| `tonightimgonna` | Tonight I'm Gonna Rock You Tonight | Spinal Tap | 2. Amp-Warmers (small1) #6 | `songs/tonightimgonna/tonightimgonna.mogg` (4 ch, 32250 Hz: band 0-1, guitar 2-3) | `tonightimgonna_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/tonightimgonna/tonightimgonna.mid` | qp venue small1 | 28900 47000 |
| `carryonwayward` | Carry On Wayward Son | Kansas | 3. String Snappers (small2) #1 | `songs/carryonwayward/carryonwayward.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/carryonwayward/carryonwayward.mid` | qp venue theatre | 93200 115000 |
| `searchanddestroy` | Search and Destroy | Iggy Pop and the Stooges | 3. String Snappers (small2) #2 | `songs/searchanddestroy/searchanddestroy.mogg` (5 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/searchanddestroy/searchanddestroy.mid` | qp venue small1 | 21150 40000 |
| `messageinabottle` | Message In A Bottle | The Police | 3. String Snappers (small2) #3 | `songs/messageinabottle/messageinabottle.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/messageinabottle/messageinabottle.mid` | qp venue big | 28600 52900 |
| `billiondollar` | Billion Dollar Babies | Alice Cooper | 3. String Snappers (small2) #4 | `songs/billiondollar/billiondollar.mogg` (5 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/billiondollar/billiondollar.mid` | qp venue fest | 39200 56600 |
| `thembones` | Them Bones | Alice in Chains | 3. String Snappers (small2) #5 | `songs/thembones/thembones.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/thembones/thembones.mid` | qp venue theatre | 35950 51700 |
| `warpigs` | War Pigs | Black Sabbath | 3. String Snappers (small2) #6 | `songs/warpigs/warpigs.mogg` (6 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/warpigs/warpigs.mid` | qp venue battle | 83150 103150 |
| `monkeywrench` | Monkey Wrench | Foo Fighters | 4. Thrash and Burn (big) #1 | `songs/monkeywrench/monkeywrench.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/monkeywrench/monkeywrench.mid` | hopo 250, qp venue small1 | 36700 56200 |
| `hush` | Hush | Deep Purple | 4. Thrash and Burn (big) #2 | `songs/hush/hush.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/hush/hush.mid` | no practice, qp venue big | 84000 114300 |
| `girlfriend` | Girlfriend | Matthew Sweet | 4. Thrash and Burn (big) #3 | `songs/girlfriend/girlfriend.mogg` (4 ch, 32250 Hz: band 0-1, guitar 2-3) | `girlfriend_coop.mogg` (band 0-1, guitar 2, rhythm 3) | `songs/girlfriend/girlfriend.mid` | qp venue small2 | 54000 73800 |
| `whowasinmyroom` | Who Was In My Room Last Night? | The Butthole Surfers | 4. Thrash and Burn (big) #4 | `songs/whowasinmyroom/whowasinmyroom.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) | `whowasinmyroom_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/whowasinmyroom/whowasinmyroom.mid` | hopo 250, qp venue fest | 94700 115400 |
| `cantyouhearme` | Can't You Hear Me Knockin' | The Rolling Stones | 4. Thrash and Burn (big) #5 | `songs/cantyouhearme/cantyouhearme.mogg` (4 ch, 32250 Hz: band 0-1, guitar 2-3) | `cantyouhearme_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/cantyouhearme/cantyouhearme.mid` | qp venue theatre | 49000 69700 |
| `sweetchild` | Sweet Child O' Mine | Guns n' Roses | 4. Thrash and Burn (big) #6 | `songs/sweetchild/sweetchild.mogg` (6 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/sweetchild/sweetchild.mid` | qp venue arena | 14550 34500 |
| `rockandroll` | Rock and Roll Hoochie Koo | Rick Derringer | 5. Return of the Shred (theatre) #1 | `songs/rockandroll/rockandroll.mogg` (5 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/rockandroll/rockandroll.mid` | no practice, qp venue battle | 28500 48500 |
| `tattooedloveboys` | Tattooed Love Boys | The Pretenders | 5. Return of the Shred (theatre) #2 | `songs/tattooedloveboys/tattooedloveboys.mogg` (5 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/tattooedloveboys/tattooedloveboys.mid` | hopo 250, band: metal_bass metal_drummer female_singer, qp venue small1 | 20000 46000 |
| `johnthefisherman` | John the Fisherman | Primus | 5. Return of the Shred (theatre) #3 | `songs/johnthefisherman/johnthefisherman.mogg` (6 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/johnthefisherman/johnthefisherman.mid` | master, qp venue theatre | 90700 109900 |
| `jessica` | Jessica | The Allman Brothers Band | 5. Return of the Shred (theatre) #4 | `songs/jessica/jessica.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) | `jessica_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/jessica/jessica.mid` | band: metal_bass metal_drummer metal_keyboard, qp venue big | 15000 35000 |
| `badreputation` | Bad Reputation | Thin Lizzy | 5. Return of the Shred (theatre) #5 | `songs/badreputation/badreputation.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/badreputation/badreputation.mid` | qp venue fest | 11400 32000 |
| `lastchild` | Last Child | Aerosmith | 5. Return of the Shred (theatre) #6 | `songs/lastchild/lastchild.mogg` (4 ch, 32250 Hz: band 0-1, guitar 2-3) | `lastchild_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/lastchild/lastchild.mid` | qp venue arena | 38500 60000 |
| `crazyonyou` | Crazy on You | Heart | 6. Relentless Riffs (fest) #1 | `songs/crazyonyou/crazyonyou.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/crazyonyou/crazyonyou.mid` | band: metal_bass metal_drummer female_singer, qp venue fest | 82300 102800 |
| `trippinonahole` | Trippin' on a Hole in a Paper Heart | Stone Temple Pilots | 6. Relentless Riffs (fest) #2 | `songs/trippinonahole/trippinonahole.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/trippinonahole/trippinonahole.mid` | qp venue small2 | 16000 46000 |
| `dead` | Dead! | My Chemical Romance | 6. Relentless Riffs (fest) #3 | `songs/dead/dead.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) | `dead_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/dead/dead.mid` | master, hopo 250, no practice, qp venue battle | 41300 63600 |
| `killinginthenameof` | Killing In The Name | Rage Against The Machine | 6. Relentless Riffs (fest) #4 | `songs/killinginthenameof/killinginthenameof.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/killinginthenameof/killinginthenameof.mid` | qp venue small1 | 21300 46000 |
| `freya` | Freya | The Sword | 6. Relentless Riffs (fest) #5 | `songs/freya/freya.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/freya/freya.mid` | qp venue big | 256500 277400 |
| `stop` | Stop | Jane's Addiction | 6. Relentless Riffs (fest) #6 | `songs/stop/stop.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/stop/stop.mid` | master, qp venue fest | 35000 49500 |
| `madhouse` | Madhouse | Anthrax | 7. Furious Fretwork (arena) #1 | `songs/madhouse/madhouse.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/madhouse/madhouse.mid` | qp venue small1 | 87000 110500 |
| `trooper` | The Trooper | Iron Maiden | 7. Furious Fretwork (arena) #2 | `songs/trooper/trooper.mogg` (5 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/trooper/trooper.mid` | no practice, qp venue arena | 90000 120000 |
| `rockthistown` | Rock This Town | Stray Cats | 7. Furious Fretwork (arena) #3 | `songs/rockthistown/rockthistown.mogg` (6 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/rockthistown/rockthistown.mid` | qp venue battle | 59000 79000 |
| `laidtorest` | Laid To Rest | Lamb of God | 7. Furious Fretwork (arena) #4 | `songs/laidtorest/laidtorest.mogg` (4 ch, 32250 Hz: band 0-1, guitar 2-3) | `laidtorest_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/laidtorest/laidtorest.mid` | qp venue big | 44600 72500 |
| `psychobilly` | Psychobilly Freakout | Reverend Horton Heat | 7. Furious Fretwork (arena) #5 | `songs/psychobilly/psychobilly.mogg` (5 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/psychobilly/psychobilly.mid` | qp venue small2 | 48200 68200 |
| `yyz` | YYZ | Rush | 7. Furious Fretwork (arena) #6 | `songs/yyz/yyz.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/yyz/yyz.mid` | band: metal_bass metal_drummer metal_keyboard, qp venue theatre | 84000 110000 |
| `beastandtheharlot` | Beast And the Harlot | Avenged Sevenfold | 8. Face-Melters (stone) #1 | `songs/beastandtheharlot/beastandtheharlot.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/beastandtheharlot/beastandtheharlot.mid` | qp venue fest | 90000 116000 |
| `carrymehome` | Carry Me Home | The Living End | 8. Face-Melters (stone) #2 | `songs/carrymehome/carrymehome.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/carrymehome/carrymehome.mid` | qp venue small1 | 32400 53000 |
| `institutionalized` | Institutionalized | Suicidal Tendencies | 8. Face-Melters (stone) #3 | `songs/institutionalized/institutionalized.mogg` (4 ch, 32000 Hz: band 0-1, guitar 2-3) | `institutionalized_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/institutionalized/institutionalized.mid` | qp venue battle | 83000 103000 |
| `misirlou` | Misirlou | Dick Dale | 8. Face-Melters (stone) #4 | `songs/misirlou/misirlou.mogg` (4 ch, 32000 Hz: band 0-1, guitar 2, bass 3) |  | `songs/misirlou/misirlou.mid` | band: metal_bass metal_drummer metal_keyboard, qp venue theatre | 64800 84400 |
| `hangar18` | Hangar 18 | Megadeth | 8. Face-Melters (stone) #5 | `songs/hangar18/hangar18.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/hangar18/hangar18.mid` | qp venue arena | 80850 99000 |
| `freebird` | Free Bird | Lynyrd Skynyrd | 8. Face-Melters (stone) #6 | `songs/freebird/freebird.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) | `freebird_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/freebird/freebird.mid` | qp venue theatre | 37500 55700 |
| `arterialblack` | Arterial Black | Drist | Bonus (store, $550) | `songs/arterialblack/arterialblack_sp.mogg` (4 ch, 32000 Hz: band 0-1, guitar 2-3) | `arterialblack_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/arterialblack/arterialblack.mid` | master, qp venue big | 9760 29370 |
| `collide` | Collide | Anarchy Club | Bonus (store, $550) | `songs/collide/collide.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/collide/collide.mid` | master, qp venue small1 | 30700 50000 |
| `elephantbones` | Elephant Bones | That Handsome Devil | Bonus (store, $550) | `songs/elephantbones/elephantbones.mogg` (5 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/elephantbones/elephantbones.mid` | master, qp venue big | 29700 54000 |
| `fallofpangea` | Fall Of Pangea | Valient Thorr | Bonus (store, $550) | `songs/fallofpangea/fallofpangea.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/fallofpangea/fallofpangea.mid` | master, qp venue theatre | 114000 134000 |
| `ftk` | FTK | Vagiant | Bonus (store, $550) | `songs/ftk/ftk.mogg` (5 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/ftk/ftk.mid` | master, band: female_singer metal_drummer metal_bass, qp venue small1 | 63000 83000 |
| `gemini` | Gemini | Brian Kahanek | Bonus (store, $550) | `songs/gemini/gemini.mogg` (6 ch, 32000 Hz: band 0-1, guitar 2-3, rhythm 4-5) |  | `songs/gemini/gemini.mid` | master, band: metal_bass metal_drummer, qp venue fest | 68400 89500 |
| `jordan` | Jordan | Buckethead | Bonus (store, $550) | `songs/jordan/jordan.mogg` (5 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/jordan/jordan.mid` | master, band: metal_drummer metal_keyboard, qp venue small1 | 20500 40000 |
| `kicked` | Kicked To The Curb | Noble Rot | Bonus (store, $550) | `songs/kicked/kicked.mogg` (5 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/kicked/kicked.mid` | master, hopo 250, speeds only, qp venue small2 | 21300 46000 |
| `ladylightning` | Push Push (Lady Lightning) | Bang Camaro | Bonus (store, $550) | `songs/ladylightning/ladylightning.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/ladylightning/ladylightning.mid` | master, qp venue big | 64000 84500 |
| `laughtrack` | Laughtrack | The Acro-brats | Bonus (store, $550) | `songs/laughtrack/laughtrack.mogg` (4 ch, 32250 Hz: band 0-1, guitar 2-3) | `laughtrack_coop.mogg` (band 0-1, guitar 2-3, rhythm 4) | `songs/laughtrack/laughtrack.mid` | master, qp venue arena | 24795 44795 |
| `lesstalkmorerokk` | Less Talk More Rokk | Freezepop | Bonus (store, $550) | `songs/lesstalkmorerokk/lesstalkmorerokk.mogg` (4 ch, 32000 Hz: band 0-1, guitar 2-3) | `lesstalkmorerokk_coop.mogg` (band 0-1, guitar 2-3, rhythm 4-5) | `songs/lesstalkmorerokk/lesstalkmorerokk.mid` | master, band: female_singer metal_drummer metal_bass, qp venue small2 | 101000 121000 |
| `mrfixit` | Mr. Fix It | The Amazing Crowns | Bonus (store, $550) | `songs/mrfixit/mrfixit.mogg` (5 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/mrfixit/mrfixit.mid` | master, qp venue big | 29000 49000 |
| `newblack` | The New Black | Every Time I Die | Bonus (store, $550) | `songs/newblack/newblack.mogg` (5 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/newblack/newblack.mid` | master, qp venue fest | 53000 73000 |
| `onefortheroad` | One For the Road | Breaking Wheel | Bonus (store, $550) | `songs/onefortheroad/onefortheroad.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/onefortheroad/onefortheroad.mid` | master, hopo 250, qp venue battle | 24000 39700 |
| `parasite` | Parasite | The Neighborhoods | Bonus (store, $550) | `songs/parasite/parasite.mogg` (6 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/parasite/parasite.mid` | master, qp venue small1 | 32000 54000 |
| `radiumeyes` | Radium Eyes | Count Zero | Bonus (store, $550) | `songs/radiumeyes/radiumeyes.mogg` (6 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/radiumeyes/radiumeyes.mid` | master, qp venue fest | 104500 124500 |
| `redlottery` | Red Lottery | Megasus | Bonus (store, $550) | `songs/redlottery/redlottery.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/redlottery/redlottery.mid` | master, qp venue small1 | 24000 39700 |
| `rawdog` | Raw Dog | The Last Vegas | Bonus (store, $550) | `songs/rawdog/rawdog.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/rawdog/rawdog.mid` | master, hopo 250, qp venue theatre | 53500 63600 |
| `six` | Six | All That Remains | Bonus (store, $550) | `songs/six/six.mogg` (5 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/six/six.mid` | master, qp venue fest | 82600 102600 |
| `soybomb` | Soy Bomb | Honest Bob and the Factory-to-Dealer Incentives | Bonus (store, $550) | `songs/soybomb/soybomb.mogg` (4 ch, 32000 Hz: band 0-1, guitar 2-3) | `soybomb_coop.mogg` (band 0-1, guitar 2, rhythm 3) | `songs/soybomb/soybomb.mid` | master, band: metal_bass metal_drummer, qp venue theatre | 23000 43000 |
| `thelightthatblinds` | The Light That Blinds | Shadows Fall | Bonus (store, $550) | `songs/thelightthatblinds/thelightthatblinds.mogg` (5 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/thelightthatblinds/thelightthatblinds.mid` | master, qp venue arena | 29000 49000 |
| `thunderhorse` | Thunderhorse | Dethklok | Bonus (store, $550) | `songs/thunderhorse/thunderhorse.mogg` (5 ch, 32000 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/thunderhorse/thunderhorse.mid` | master, hopo 250, qp venue fest | 28000 48000 |
| `trogdor` | Trogdor | Strong Bad | Bonus (store, $550) | `songs/trogdor/trogdor.mogg` (4 ch, 32000 Hz: band 0-1, guitar 2-3) | `trogdor_coop.mogg` (band 0-1, guitar 2, rhythm 3) | `songs/trogdor/trogdor.mid` | master, qp venue small2 | 3100 19100 |
| `xstream` | X-Stream | Voivod | Bonus (store, $550) | `songs/xstream/xstream.mogg` (5 ch, 32250 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/xstream/xstream.mid` | master, hopo 250, qp venue big | 3100 19100 |
| `drinkup` | Drink Up | Ounce of Self | Bonus (store, $550) | `songs/drinkup/drinkup.mogg` (6 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4-5) |  | `songs/drinkup/drinkup.mid` | master, qp venue big | 30000 44500 |
| `yeswecan` | Yes We Can | Made in Mexico | Bonus (store, $550) | `songs/yeswecan/yeswecan.mogg` (5 ch, 44100 Hz: band 0-1, guitar 2-3, bass 4) |  | `songs/yeswecan/yeswecan.mid` | master, band: metal_bass metal_drummer female_singer, qp venue small1 | 83950 97900 |
| `tutorial102` | Basic 2 Playing Notes | You | Tutorial | `tutorial/tutorial102.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) |  | `tutorial/tutorial102.mid` | bpm 108, no practice, qp venue small2 | 11981 35990 |
| `tutorial103` | Basic 3 Different Notes | You | Tutorial | `tutorial/tutorial103.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) |  | `tutorial/tutorial103.mid` | bpm 108, no practice, qp venue small2 | 11981 35990 |
| `tutorial104` | Basic 4 Durations | You | Tutorial | `tutorial/tutorial104.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) |  | `tutorial/tutorial104.mid` | bpm 108, no practice, qp venue small2 | 11981 35990 |
| `tutorial105` | Basic 5 Chords | You | Tutorial | `tutorial/tutorial105.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) |  | `tutorial/tutorial105.mid` | bpm 108, no practice, qp venue small2 | 11981 35990 |
| `tutorial202` | StarPower 2 Combos | You | Tutorial | `tutorial/tutorial202.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) |  | `tutorial/tutorial202.mid` | bpm 108, no practice, qp venue small2 | 11981 35990 |
| `tutorial203` | StarPower 3 Whammy Bar | You | Tutorial | `tutorial/tutorial203.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) |  | `tutorial/tutorial203.mid` | bpm 108, no practice, qp venue small2 | 11981 35990 |
| `tutorial204` | StarPower 4 Wail | You | Tutorial | `tutorial/tutorial204.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) |  | `tutorial/tutorial204.mid` | bpm 108, no practice, qp venue small2 | 11981 35990 |
| `tutorial205` | StarPower 5 Hammer-Ons | You | Tutorial | `tutorial/tutorial205.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) |  | `tutorial/tutorial205.mid` | bpm 108, no practice, qp venue small2 | 11981 35990 |
| `tutorial206` | StarPower 6 Pull-Offs | You | Tutorial | `tutorial/tutorial206.mogg` (4 ch, 44100 Hz: band 0-1, guitar 2-3) |  | `tutorial/tutorial206.mid` | bpm 108, no practice, qp venue small2 | 11981 35990 |
| `budget_two_note_chords` | Techniques: Speed | You | Test (non-ship) | `songs/yyz/yyz.mogg` (6 ch, 44100 Hz: band 0-1 4-5, guitar 2-3) |  | `test/budget_songs/two_note_chords.mid` | bpm 120, no practice, qp venue small2 | 11981 35990 |
| `budget_two_note_chords_coop` | Techniques: Speed | You | Test (non-ship) | `songs/yyz/yyz.mogg` (6 ch, 44100 Hz: band 0-1 4-5, guitar 2-3) |  | `test/budget_songs/two_note_chords.mid` | bpm 120, no practice, qp venue small2 | 11981 35990 |
| `budget_two_note_tails` | Techniques: Speed | You | Test (non-ship) | `songs/yyz/yyz.mogg` (6 ch, 44100 Hz: band 0-1 4-5, guitar 2-3) |  | `test/budget_songs/two_note_tails.mid` | bpm 120, no practice, qp venue small2 | 11981 35990 |
| `budget_two_note_tails_coop` | Techniques: Speed | You | Test (non-ship) | `songs/yyz/yyz.mogg` (6 ch, 44100 Hz: band 0-1 4-5, guitar 2-3) |  | `test/budget_songs/two_note_tails.mid` | bpm 120, no practice, qp venue small2 | 11981 35990 |
| `budget_three_note_chords` | Techniques: Speed | You | Test (non-ship) | `songs/yyz/yyz.mogg` (6 ch, 44100 Hz: band 0-1 4-5, guitar 2-3) |  | `test/budget_songs/three_note_chords.mid` | bpm 120, no practice, qp venue small2 | 11981 35990 |
| `budget_three_note_chords_coop` | Techniques: Speed | You | Test (non-ship) | `songs/yyz/yyz.mogg` (6 ch, 44100 Hz: band 0-1 4-5, guitar 2-3) |  | `test/budget_songs/three_note_chords.mid` | bpm 120, no practice, qp venue small2 | 11981 35990 |
| `budget_three_note_tails` | Techniques: Speed | You | Test (non-ship) | `songs/yyz/yyz.mogg` (6 ch, 44100 Hz: band 0-1 4-5, guitar 2-3) |  | `test/budget_songs/three_note_tails.mid` | bpm 120, no practice, qp venue small2 | 11981 35990 |
| `budget_three_note_tails_coop` | Techniques: Speed | You | Test (non-ship) | `songs/yyz/yyz.mogg` (6 ch, 44100 Hz: band 0-1 4-5, guitar 2-3) |  | `test/budget_songs/three_note_tails.mid` | bpm 120, no practice, qp venue small2 | 11981 35990 |
