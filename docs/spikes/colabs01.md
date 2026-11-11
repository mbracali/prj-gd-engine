# Colabs 01: what other recompilation projects can give us

Study, 2026-11-10 (backlog GDALL-043). Read-only: nothing was built, run or imported. Decide here, game by game, how
and when to start. Every section ends with an empty **Owner's take:** line for you.

## Executive summary

| Game (our folder) | Repo(s) | Licence | What they are | We take | Saves on our port | Make it ours (1-10) | Our menus / behaviours (1-10) | Disc here |
|---|---|---|---|---|---|---|---|---|
| Guitar Hero 5 (gd-game-12) | ElGuslml/GH5Recomp | MIT | Static recompilation of the XEX, Windows only, beta | Knowledge: ~60 addresses, script ids, engine quirks, fixes (the repo Band Hero's was derived from) | 10-15 % | 8 | 3 (on our engine) | Yes (added 11-10) |
| Band Hero (gd-game-14) | ElGuslml/BandHRecomp | MIT | Same engine as GH5 (99.3 % of functions match) | Same as GH5, checkable on our disc | 10-15 % (70-80 % from our GH5 port) | 8 | 3 | Yes |
| Warriors of Rock (gd-game-17) | ElGuslml/GHWoRRecomp, Piscacho/Guitar-Hero-Warriors-of-Rock-PC, ronniecloud/re-wor | MIT, MIT, **none** | Three static recompilations | Piscacho's ~1,550 script-function map, addresses, clock drift, widescreen layering | 5-8 % (40-50 % from our GH3 engine) | 9 | 3 | Yes |
| Metallica (gd-game-10) | ElGuslml/GHMetalRecomp | MIT | Static recompilation, v0.4.1 | 50 named addresses, 28 fix notes | 8-12 % | 10 | 2 | Yes |
| Van Halen (gd-game-15) | ElGuslml/GHVanHRecomp | MIT | Copy of the Metallica repo with addresses moved | Metallica-to-Van Halen function map | 5-8 % (60-70 % from our Metallica port) | 10 | 2 | Yes |
| HDR for every game (gd-engine-core) | gminustwo/msr2ds | **GPL-2.0 / GPL-3.0** | A port with a well-tuned HDR output | The technique only (peak / paper white, OLED gamma, UI layer) | n/a: 8-10 agent hours, effort 6 | own code | n/a | n/a |

**The key finding.** Every game repo is a *static recompilation*: at build time it translates the user's Xbox 360
executable to C++ and runs the **original game code** on an Xbox emulation runtime (ReXGlue / XenonRecomp, Xenia-derived),
Windows only. We *reimplement* the engines. Their output is machine-translated game code, which our rules (and
`docs/legal.md`) forbid us to commit or ship. So **none of these projects can be imported as code**: what we gain is
**knowledge** (addresses as Ghidra leads, formats, original-game bugs and their fixes, feature ideas), re-found on our
own discs and written in our own words. That is why the savings are 5-15 % per game, not more. The big savings come
from **our own engines**: GH3 (gd-game-04) for Warriors of Rock, and one engine per family (GH5 then Band Hero,
Metallica then Van Halen).

**Engine families** (build order suggestion): World Tour engine = Metallica, Van Halen (and gd-game-07 World Tour);
GH5 engine = Band Hero, GH5 (then Warriors of Rock, its successor); all are Neversoft engines descending from our GH3 port.

Owner's take:

## Licensing: what you asked vs what the licences allow

| Repo | Licence | Copy code? | Notice needed |
|---|---|---|---|
| ElGuslml (5 repos), Piscacho | MIT | Yes, but we don't need to (see above) | MIT **requires** keeping its copyright + permission notice in copies or substantial portions |
| ronniecloud/re-wor | **No licence** (all rights reserved) | **No** | - |
| gminustwo/msr2ds | **GPL-2.0 / GPL-3.0** | **No** (would force our project under GPL) | - |

- "Remove every reference to the maintainers" can be met **in our code**, but not by deleting the MIT notice from
  what we ship. How to satisfy both:
  - Preferred: **clean-room**. Read their docs, re-find each fact in Ghidra on our own disc, write it fresh. Facts
    (addresses, formats, causes of bugs) are not copyrightable, so no notice and no names are needed.
  - If any code or substantial text is ever adapted: their notice goes **only** into `licenses/` in the packages and a
    repo-level `THIRD_PARTY_NOTICES.md`. Our source files carry no names, branding or links to them.
- Never import their dependencies: ReXGlue SDK / Xenia (BSD-3), FFmpeg (LGPL), DLSS (proprietary), their launchers.

Owner's take:

## Good features from these projects for all our games

| Feature | Seen in | Why |
|---|---|---|
| Quit prompt on the main menu | GH5, Band Hero | PC players expect one |
| Fix for the original's clock drift (49.875 MHz timebase, 0.25 %, ~150 ms a minute) | all | Audio / chart drift in long songs; check gd-game-04 for the same constant |
| Fixes for logic that counts frames (for > 60 FPS) | all | High-FPS correctness |
| A switch per screen effect, and per fix of an original bug | Metallica, GH5 | Players choose; fits our Enhanced menu |
| Ultrawide with the highway and HUD kept 16:9 in the centre | all | Widescreen quality |
| Precompiling shaders on first launch | all | No first-play stutter (our "no stutter" rule) |
| FSR1 / NIS upscaling, low-latency audio (WASAPI exclusive) | all | Performance, latency |
| Whammy / tilt axis remap and calibration; auto-strum | Metallica, GH5 | Controllers, accessibility |
| DLC read in place from the user's own packages | all | Content the player owns |
| Guest players | Metallica | Multiplayer |
| A function matcher between builds of the same engine | Van Halen | Speeds up each new Neversoft game |
| HDR peak / paper white sliders, OLED gamma 2.2, UI in its own layer, HDR toggle without restart | msr2ds | Better HDR for every game (section HDR) |

Owner's take:

## Links

| Project | Link |
|---|---|
| Guitar Hero 5 | https://github.com/ElGuslml/GH5Recomp |
| Guitar Hero: Warriors of Rock | https://github.com/Piscacho/Guitar-Hero-Warriors-of-Rock-PC |
| Guitar Hero: Warriors of Rock | https://github.com/ronniecloud/re-wor |
| Guitar Hero: Warriors of Rock | https://github.com/ElGuslml/GHWoRRecomp |
| Guitar Hero: Van Halen | https://github.com/ElGuslml/GHVanHRecomp |
| Guitar Hero: Metallica | https://github.com/ElGuslml/GHMetalRecomp |
| Band Hero | https://github.com/ElGuslml/BandHRecomp |
| HDR (msr2ds) | https://github.com/gminustwo/msr2ds |


---

## Colabs study: GH5Recomp (gd-game-12) and BandHRecomp (gd-game-14)

Read-only study, 2026-11-10. Sources: ElGuslml/GH5Recomp (v0.2.0, 1 squashed commit) and
ElGuslml/BandHRecomp (beta, 1 commit, 2026-11-09). Both MIT, Copyright (c) 2026 Gustavo
Garcia. Submodules (`thirdparty/rexglue-sdk`, `native_x_renderer`, `recomp_launcher`) are **not checked out** in the
clones: the SDK changes, the renderer and the launcher code were not reviewed, only described by the docs.

### Shared facts (both games)

| Topic | Finding |
|---|---|
| Approach | Static recompilation: ReXGlue SDK (BSD-3, Xenia-derived) translates `assets/default.xex` (PowerPC) to C++ in `generated/` **at build time from the user's XEX**; a Xenia-derived runtime emulates the kernel, XAudio/XMA, input, STFS. |
| Hand-written | `src/*.cpp` (~2.7k lines each): ~52 `[[midasm_hook]]`s declared in `*_functions.toml` (threads' waits, timer, frame rate, widescreen, screen FX, skinning, players, quit prompt, renderer hooks), `launcher/` profile, docs. |
| Generated / tool output | `*_functions_auto.toml` (gap finder over the XEX), the whole `generated/` tree (not in repo), `*_manifest.toml`. |
| Renderer | NativeXRenderer (their MIT lib: hooks the XDK D3D library, draws on Vulkan via DiligentCore; SMAA/FXAA, FSR1/NIS, DLSS) or Xenos emulation. Vulkan only. |
| Audio | Game's own FMOD on the SDK's XAudio/XMA emulation (FFmpeg LGPL); WASAPI exclusive option. |
| Platforms | Shipped: Windows 10/11 x64 only (Clang + Ninja, Inno Setup, PowerShell). Linux presets exist in CMakePresets, Linux/Steam Deck "in the works". No macOS/Android. |
| Launcher | RecompLauncher (SDL3 + ImGui, ISO install via extract-xiso, profiles, achievements, DLC page, auto-update). Competes with our Universal Launcher. |
| Legal shape | Their release `.exe` **contains machine-translated game code**. Our rule forbids committing or distributing recompiler output from the game binary: the approach itself is unusable for us as code. |
| Licensing chain | Own code MIT; SDK BSD-3 (+ Xenia BSD-3); FFmpeg/libmspack LGPL 2.1 (static, with source offer); DLSS proprietary; fonts OFL. |

#### Licensing: how to satisfy MIT and "no maintainer references in our code"
- Facts (addresses, checksums, formats, causes of bugs) are not copyrightable: re-deriving them on our disc and writing
  them in our own words in `docs/` needs no notice and no name.
- Any **code or substantial doc text** copied or closely adapted (e.g. the element hash-table lookup, the Hor+ math, a
  pipeline-list format) keeps MIT's copyright + permission notice. Put it **only** in a third-party notices file
  (`licenses/third-party-colab.txt`, shipped by `build_module_NN` into each package's `licenses/`, per
  `docs/platform/adding-a-game.md` step 4) and list it in a repo-level `THIRD_PARTY_NOTICES.md`. Source files carry no
  name; at most a neutral `// third-party: see licenses/` tag. The notice cannot be dropped from packages.
- Preferred: clean-room (read their doc, re-find the fact in Ghidra on our XEX, implement fresh): no notice needed at all.
- Never import SDK/Xenia (BSD-3 notice + no-endorsement clause), FFmpeg/libmspack (LGPL obligations), DLSS (proprietary).

Owner's take:

---

### Guitar Hero 5 (gd-game-12) - GH5Recomp

**1. What it is.** GH5 Xbox 360 (title 41560840, XDK 2.0.8276, XEX CRC32 25F6E170) recompiled to Windows; fixes ported
from their GH Van Halen / GH Metallica recomps by matching functions across executables. Maturity: beta; guitar songs
played, drums and vocals untested, AMD untested, >60 FPS experimental (Havok cloth runs at 160/60 speed above 60 FPS,
jam-studio arrows slow), keyboard experimental, only GH5-format DLC adds songs (92 songs in Quickplay).

**2. What we can take (knowledge unless stated).**

| Item | Where | Use in our reimplementation |
|---|---|---|
| Function map (~60 named addresses: job workers, frame loop `sub_82413AA0`, vblank, timer `sub_823B08F0`, frustum `sub_823D9828`, sprites, skinning `sub_824767F0`, composite `sub_82643598`, shadow filter `sub_825CCE98`, element table `0x82DA78C4`) | `gh5_functions.toml`, docs | Ghidra labels/entry points to read the original logic faster (needs a GH5 XEX; most map 1:1 to Band Hero). |
| QB checksums: main menu `0xA7988D2C`, Party Play `0x22F50BBE`, `dialog_box_container`, `button_checker` `0x716BADDC`, star-power drain `0x74B936AB`, drummer `0x473164C7`, self-timed scripts list | docs, `src/` | Script names/ids for our QScript VM port (gd-game-04 `qscript.md` lineage). |
| Engine behaviour: scene drawn over 2 frames (`IsPrimaryFrame`: stage+crowd, then band+FX), 320x180 DOF depth via aliased MSAA, `_Uber_ScreenFX` flag bits per effect, 400x400 shadow map with 9-tap PCF, CPU skinning from 128-matrix palette | `native-x-renderer-gh5.md`, `clothes-stretching.md` | Spec for our renderer: we can draw in one frame (no 2-frame trick) and keep 5 FX switches as Enhanced options. |
| Timer: engine forces 49.875 MHz timebase -> 0.25 % audio drift | `fixes-from-gh-van-halen.md` | Use real host clock; never port the constant. Audit gd-game-04 for the same constant. |
| Above-60-FPS list: what counts frames (vblank clocks, `Wait N gameframes`, whammy 5-frame run, strings 62 Hz + 0.99 fade, loading anims, fret fall 100 ms) | same | Checklist for our fixed-timestep/high-FPS design. |
| Ultrawide rules: Hor+ 3D, 2D at 16:9 centre, side bars outside songs, red-meter border full width, highway depth-mask quad | `widescreen.cpp`, docs | Layout rules for our HUD/highway at 21:9. |
| DLC: STFS (CON/LIVE/PIRS) -> `CONT0:\bmanifest_e35cd47b.pak.xen` + `bdlcNNNN_song.pak.xen` | `dlc.md` | DLC loader spec (shared with Band Hero). |
| `NtCancelIoFile` lesson: model loads cancelled when switching characters fast | `characters-not-loading.md` | Test case for our async loader. |

Code reuse: practically none; every `src/` file is guest-register/address hooking on the SDK. Only small ideas (hash
lookup layout, Hor+ maths) could be adapted, with the MIT notice.

**3. Head-start: ~10-15 % of a gd-game-12 port.** GH5 is the Neversoft engine after GH3/WT: our gd-game-04 (GH3, 0.96)
already gives formats, QB VM and highway; their value is addresses, ids and engine quirks that save Ghidra hours on
renderer/FX, timing, ultrawide, DLC. No gameplay rules, formats or menus are documented. The GH5 disc is now in `original_isos/`
(Guitar Hero 5 (USA, Europe), added 2026-11-10): every address and id can be verified on our own XEX, as for Band Hero.

**4. Not used.** The recompiler/generated code (distributes game code); ReXGlue SDK and NativeXRenderer (emulation
layer, Vulkan/Windows only, BSD/LGPL/DLSS baggage, conflicts with our SDL3/Vulkan/OpenGL3 + miniaudio/OpenAL/FAudio
backends); RecompLauncher, Inno Setup, PowerShell scripts, auto-updater, shared ElGus profile store (we have our
launcher); fixes that only exist because the original code runs (thread-wait spins, job wake-one, FMOD thread placement,
critical-section spin, skinning race, 2-frame EDRAM tricks); branding, icons, screenshots.

**5. Effort.** Make their code look like ours: **8/10** (different paradigm, nothing maps file-to-file). Our menus and
universal behaviours on their approach: **7/10** (ImGui overlay possible like their quit prompt, but backends, GAMEPADS
remapping and help bar fight the guest); on our reimplementation using their knowledge: **3/10** (gd-game-04's shared
menus carry over).

**6. Features worth adding to our other games.** See the shared list at the end.

**7. Import plan (gd-game-12).**
1. Record the study in `docs/spikes/` and the backlog (GDALL-043): facts only, our words, no names. Owner's take:
2. Build GH5 first: it is the parent (their Band Hero repo is GH5's with `fixes-from-gh5.md`), so gd-game-14 becomes a delta of gd-game-12 (same code base, 99.3 %). Owner's take:
3. Check our GH5 ISO's XEX against theirs (title, CRC); extract (never committed), run our Ghidra headless flow, import their address list as labels (verify each). Owner's take:
4. Data layer from gd-game-04 (pak/qb/toc/FSB) adapted to the GH5-era formats; QB ids, renderer rules, ultrawide, high-FPS checklist. Owner's take:
5. DLC loader from the shared STFS spec; test with an owner-owned GH5 package. Owner's take:
6. Wire `GD12_MODULE` + `build_module_12`, game-info, self-tests, launcher listing. Owner's take:

Owner's take:

---

### Band Hero (gd-game-14) - BandHRecomp

**1. What it is.** Band Hero Xbox 360 (title 4156085C) on the same toolchain; the XEX's internal name is
`gh5_1_burn_Final.exe`, same XDK as GH5, **99.3 % of functions identical to GH5's** (bar branch/data addresses) and
96.4 % of its 9005 scripts exist in GH5 (90 % of those byte-identical); 326 scripts are Band Hero-only. All GH5 fixes
ported with translated addresses (174 functions checked, struct offsets compared). Maturity: beta; guitar songs played
at 160 FPS, languages and low-latency audio checked; drums, vocals, DLC untested; a 1-px column glitch in ultrawide
menus. Tested on the USA disc; ours is "USA, Europe (En,Fr,De,Es,It)": XEX may differ (check CRC before trusting addresses).

**2. What we can take (knowledge unless stated).**

| Item | Where | Use for us |
|---|---|---|
| Band Hero address map: setjmp `0x829117A0`, timer hook `sub_823B0CC0`/`0x823B0D5C`, composite `sub_8263E130` (hooks `0x8263E508/658/71C`, flags in r19), presentation setting `+584` (`sub_8254FB40`, `sub_824285D0`) | `ghbandhero_functions.toml`, `fixes-from-gh5.md` | Labels for **our own disc** in Ghidra: directly checkable. |
| GH5<->BH function correspondence (their matcher: same instructions, +/-6 around, same regs) | docs | Method to make one module serve both games (shared engine dir, per-game tables). |
| Struct deltas: renderer block +32..48 bytes from ~+12600, player input object +8 from +128 | `fixes-from-gh5.md` | Watch-list when sharing structs between 12 and 14. |
| `_projection1` FX fragment (bit `0x200`), no `_visualizer1` in pass 1 | same | Renderer differences vs GH5. |
| Frame alignment scripts `0xCAD261DF`, `0x6506F6C8` + callers; drummer script; 5 self-timed scripts (same as GH5; none of the 326 new ones) | same | High-FPS checklist; confirms GH5 scripts are reusable. |
| Everything in the GH5 table above | GH5 docs | Applies here with translated addresses. |

Code reuse: same as GH5, effectively none.

**3. Head-start: ~10-15 % of a gd-game-14 port.** Same knowledge as GH5 plus addresses we can verify on the disc we own,
and the hard numbers on code/script overlap that justify one shared engine for 12+14. Gameplay, formats beyond DLC, menus,
band/vocals/drums logic still have to be reimplemented. Practical bonus: with GH5 built first, gd-game-12 covers ~70-80 % of gd-game-14 (diff the 326/9005 scripts).

**4. Not used.** Same as GH5. Also: their per-game "copy everything and rename" structure (we would share one engine
source between 12 and 14 instead).

**5. Effort.** Make their code look like ours: **8/10**. Our menus/behaviours on their approach: **7/10**; on our
reimplementation with their knowledge: **3/10**.

**7. Import plan (gd-game-14).**
1. Check our ISO's XEX (title 4156085C, CRC vs their USA one); extract to `gd-game-14/extracted/` (never committed). Owner's take:
2. Ghidra headless on our XEX; apply their addresses as labels, verify each, log results in `docs/FINDINGS.md` [C]/[I]. Owner's take:
3. Start from gd-game-12 (GH5, built first): share its engine source, apply Band Hero's deltas (struct offsets, scripts, content); validate over the whole disc first. Owner's take:
4. QScript VM + script ids from their docs; design high-FPS timing from their checklist (real clock, no 49.875 MHz). Owner's take:
5. Renderer: one-frame scene, FX passes with 5 switches as Enhanced options, Hor+ ultrawide, shadow PCF scaled with map size, bloom at console resolution. Owner's take:
6. Highway, band, venues, menus with our universal menus, GAMEPADS screen, help bar, backends (all gd-engine-core). Owner's take:
7. DLC (STFS -> `bmanifest_*.pak.xen`), quit prompt, self-tests, `GD14_MODULE`, `build_module_14`, launcher listing, release bundle check. Owner's take:
8. If any code is adapted: add the MIT notice to `licenses/` and THIRD_PARTY_NOTICES only, no names in source. Owner's take:

Owner's take:

---

### 6. Features worth adding to our other games (from both)
- Quit prompt from the main menu (B asks, A quits; suppressed when a dialog box is open).
- DLC install from the user's own STFS packages (CON/LIVE/PIRS), with a launcher page.
- Shader/pipeline precompile on first play ("Compiling shaders") to avoid first-time stalls; skip, not wait, for steady passes.
- Per-effect switches: bloom, depth of field, grain, colour correction, vignette.
- Hor+ ultrawide with the HUD/highway kept 16:9 in the centre and side bars in menus.
- High-FPS audit list: anything counting frames (whammy, strings, fret fall-back, loading anims, script waits, drummer).
- Bloom/blur passes kept at console resolution at high output resolutions (no lattice); shadow filter offsets scaled with shadow-map size.
- Auto-strum on gamepad/keyboard (GH2/GH3 style), only during songs.
- Whammy rest calibration, tilt/whammy axis assignment, PS3/CRKD guitar protocol.
- Low-latency (exclusive) audio option with a recalibration hint.
- First controller to press takes the profile; others join as guests.
- Achievement toasts and list (F7).
- Audit gd-game-04 (and other X360 ports) for the 49.875 MHz timebase constant (0.25 % drift).

Owner's take:

---

## GH: Warriors of Rock (gd-game-17) — study of three community ports (2026-11-10)

All three are **static recompilations** of the Xbox 360 `default.xex` (PowerPC → C++ → x86-64), generated at build time from the user's own XEX. None reimplements the game: the original code runs, and the repos add a runtime plus hooks at fixed guest addresses. Our gd-game-17 is an empty skeleton (0.0.0); our closest model is gd-game-04 (GH3, same Neversoft lineage, 1.1.6, 45k lines, our own PAK/QB/QScript VM/FSB/scene code).

### 1. The repos

| | ElGuslml GHWoRRecomp | Piscacho GHWoR-PC | ronniecloud re-wor |
|---|---|---|---|
| Licence | MIT © 2026 Gustavo Garcia | MIT © 2026 Piscacho | **None** (no LICENSE, no headers, no README section): all rights reserved |
| Approach | ReXGlue SDK fork (Xenia-derived runtime) + own NativeXRenderer (Vulkan, native draws) or Xenos emulation path | XenonRecomp (patched: `xenonrecomp_gh6.diff`) + **hand-written** runtime: kernel/XAM, Xenos→D3D12 (4.5k lines), XMA via FFmpeg, WASAPI, net | ReXGlue SDK `rexglue init` scaffold; 16 function fixes, XAM stubs, Discord RPC |
| Hand-written vs generated | ~2.8k lines hooks (`src/`) + 400 lines function/hook TOML; all game logic generated | ~20k lines runtime + launcher; game logic generated; 11k lines switch tables, 1.5k CFunc address map | ~400 lines; everything else generated/SDK |
| Platforms | Windows (Linux/Deck "in the works") | Windows (D3D12) | Windows (D3D12) |
| Maturity | 0.2.0 beta (2026-11-10); shares fixes with author's GH5/VH/Metallica recomps | v1.5.0 (2026-11-07), 24 session journals | "TEST version" (2026-03-06), dormant |
| Known bugs | drums/vocals untested; >60 FPS experimental; keyboard experimental; WoR DLC songs unplayed; USB audio latency; Xenos path pop-in | lobby portrait alpha/aliasing, "doors" in lobby unresolved, timing-script desync; online experimental | audio delay, 720p only, crashes on scene/song transitions |

Owner's take:

### 2. Comparison — what each does best

| Area | Best | Why |
|---|---|---|
| Documented fixes (symptom/cause/fix/off) | ElGus | `docs/fixes-from-gh5.md`, `native-x-renderer-ghwor.md`, `dlc.md` |
| Widescreen / HFR / timing | ElGus | Hor+ via projection fn, 2D layer kept 16:9, highway depth-mask quad, frame-counting list, 0.25 % clock fix |
| Engine knowledge (script natives) | Piscacho | `runtime/cfunc_hooks.inc`: ~1,550 QScript CFunc name→address pairs |
| Self-contained tooling | Piscacho | Python `xdvdfs.py`, `xex2.py`, `stfs_extract.py`, `ppcdis.py`, `xenos_dis.py`, input-script test harness |
| Controllers | Piscacho / ElGus | CRKD preset, controller report recorder; ElGus auto-strum, whammy/tilt reassignment, keyboard guitar |
| Anything | — (re-wor) | Nothing it does is not done better by the other two |

"Extract the best of each": ElGus's behaviour notes + Piscacho's CFunc map and tooling, re-derived and verified in our own Ghidra project; nothing from re-wor.

Owner's take:

### 3. What we can take advantage of (knowledge, not code)

Mismatch: they **run the original code**; we **reimplement** it (gd-game-17 README: no original code). Their hooks patch guest addresses we never execute, so their code has almost no place in our engine. What transfers is *knowledge* (facts are not copyrightable; we confirm each in our Ghidra and mark [C]/[I]).

| Item | Source | Use in gd-game-17 |
|---|---|---|
| CFunc name→address map (~1,550) | Piscacho `cfunc_hooks.inc` | Seed Ghidra labels; prioritise QScript natives for our VM (the gd-game-04 `script/` model) |
| Engine addresses: timer `sub_823A0238`, projection `sub_824C6EE0`, highway mask `sub_822C53A0`, sustain tails `sub_822C4558`, setjmp/longjmp, D3D device `0x829BBA74` | ElGus `src/*.cpp` comments, TOML | Ghidra entry points for timing, camera, highway, tails |
| Clock: engine assumes 49.875 MHz timebase → 0.25 % drift on later consoles | ElGus docs | Our song clock: use the audio clock; document the original's quirk (optional "console-accurate" flag) |
| Frame-counted logic list (vblank clocks, script frame waits, button polls, whammy, loading star; 19 scripts self-timed) | ElGus `framerate.cpp` docs | Checklist for our fixed-tick vs render-rate split (universal HFR) |
| Widescreen layering: scene Hor+, 2D layer 16:9 centered, highway depth-only near-plane quad, clip windows scaled by 2D zoom | ElGus | Our renderer design; avoids the "black band beside the highway" bug |
| Screen-effect chain (bloom, DOF, film grain, colour correction, vignette; grain tied to CC) | ElGus | Our post-FX pass + Enhanced-menu toggles |
| DLC: STFS/LIVE/PIRS/CON read in place, Customs Database dependency, GH5 packs into Quickplay+ | ElGus `dlc.md`, Piscacho `stfs_extract.py` | Later DLC support; tool reference |
| Switch tables / missed functions | Piscacho `config/`, ElGus TOML | Ghidra analysis fixes only |
| Formats (PAK/PAB, QB `.qb.xen`, FSB4 XMA, `.img`, scenes) | Not documented by any repo | Must come from gd-game-04 + our own RE |

Possible code reuse (MIT, tools only, never in a shipped module unless needed): Piscacho's Python `xdvdfs.py` / `stfs_extract.py` / `xex2.py` for `gd-engine-tools` — but we already read ISOs and prefer our own; rewrite rather than copy. SMAA/FSR1/NIS/CAS come from their upstreams (MIT), not via these repos.

Owner's take:

### 4. Estimated saving on a gd-game-17 port

**~5–8 %.** The bulk of the work (WoR formats on top of gd-game-04's GH3 code, QScript VM natives, Quest/powers mode, band/drums/vocals, venues, character creator, GHTunes) is not covered by any repo: their game logic is generated, not explained. Savings come from RE shortcuts (CFunc map, key addresses, ~2–3 %), timing/HFR/widescreen pitfalls known up front (~2–3 %) and DLC/tool notes (~1–2 %). gd-game-04 itself saves far more (~40–50 %).

Owner's take:

### 5. What we will NOT use

| Item | Why |
|---|---|
| Any re-wor code or config | No licence → all rights reserved; we may only read it. Little value anyway |
| Recompilation approach (XenonRecomp/ReXGlue, generated `ppc/`/`generated/`) | Runs original code; breaks "no original code" and "no recompiler output" rules; Windows/x86 only |
| Hook sources (`widescreen.cpp`, `framerate.cpp`, `thread_waits.cpp`…) | Bound to guest addresses/registers; meaningless in a reimplementation |
| Piscacho D3D12/Xenos runtime, kernel/XAM emulation | Emulation layer; we use gd-engine-core (OpenGL/SDL3) |
| NativeXRenderer, DLSS (NVIDIA proprietary), FFmpeg (LGPL), libmspack (LGPL) | Not needed; licence burden; we decode XMA/FSB ourselves as in gd-game-04 |
| Their launchers (ImGui/RecompLauncher), Discord RPC, online P2P, achievements/profile services | We have the Universal Launcher and our own flavour |
| Switch tables / address dumps as committed files | Derived from the game binary; keep in git-ignored `ghidra/` only |

Owner's take:

### 6. Effort (1–10)

| Task | Effort | Note |
|---|---|---|
| Make their code look like ours | **9** | Not a refactor: a different architecture. Realistically 0 lines carried over; we write ours (gd-game-04 style) |
| Our custom menus + universal behaviours in **our** gd-game-17 | **3** | Shared from gd-engine-core/gd-game-04 (Enhanced menu, GAMEPADS, help bar, Easy Chords) |
| Same inside a recomp (for comparison) | 8 | Would need overlay over guest renderer and input interception |

Owner's take:

### 7. Features worth adding to our other games

- Per-effect toggles: bloom, depth of field, film grain, colour correction, vignette.
- SMAA / FXAA / CAS sharpening and FSR1 / NIS upscaling (MIT upstreams).
- Auto-strum (GH2/GH3-style "frets only") as an accessibility option.
- Whammy/tilt reassignment for faulty third-party guitars; CRKD preset; "controller report" recorder.
- Keyboard as guitar (D F J K L).
- Shader/pipeline pre-warm at first start (no first-time stutter).
- Optional exclusive-mode low-latency audio.
- DLC folder read in place (no copying).
- Song clock from the audio clock (no drift) — check our GH3/GH5-lineage modules.

Owner's take:

### 8. Import plan

1. Record GDALL-043 findings in the backlog; keep this study in `docs/spikes/`. Owner's take:
2. Add a `THIRD_PARTY_NOTICES` entry template only if we ever copy MIT code (see Licensing); knowledge needs none. Owner's take:
3. Set up gd-game-17's Ghidra project (git-ignored) from the user's ISO; import CFunc names and key addresses from Piscacho/ElGus as *hints*, verify each, label [C]. Owner's take:
4. Fork the gd-game-04 engine skeleton into gd-game-17 (modelled on gd-game-02 per agent-instructions), wire `gd_game_run` and the launcher entry. Owner's take:
5. Port the data layer: WoR PAK/QB/FSB/IMG/scene deltas vs GH3, in `docs/formats.md`. Owner's take:
6. QScript VM natives, prioritised by the CFunc map; log every unhandled native. Owner's take:
7. Song clock + highway (audio clock, tail geometry, highway near-plane mask) with self-tests. Owner's take:
8. Renderer: Hor+ scene, 16:9 2D layer, post-FX chain with Enhanced-menu toggles; HFR split using the frame-counted list. Owner's take:
9. Universal menus/behaviours from gd-engine-core; controllers (whammy/tilt mapping, auto-strum). Owner's take:
10. Modes (Quickplay+, Quest, band), then DLC; package all platforms and verify bundles. Owner's take:

### Licensing

- **MIT (ElGus, Piscacho):** if a copy or substantial portion is used, ship the copyright line and permission text in a `THIRD_PARTY_NOTICES` file (or `licenses/` folder) inside the packages and repo. Our source files keep only our SPDX header — no names, no "based on" comments; provenance lives in the notices file. Facts learned (addresses, behaviour) need no notice; a courtesy credit there is optional.
- **re-wor:** no licence anywhere (checked all 13 files): we can read it, but **cannot copy any code, config or assets**.
- Their dependencies keep their licences (ReXGlue/Xenia BSD-3, FFmpeg/libmspack LGPL, DLSS proprietary, fonts OFL); we take none of them.

Owner's take:

---

## Colabs 01: Guitar Hero: Metallica and Guitar Hero: Van Halen recomps

Repos studied (read only, never built or run): `ElGuslml_GHMetalRecomp` (v0.4.1, 1 squashed commit) and
`ElGuslml_GHVanHRecomp` (v0.2.0, 1 squashed commit). Both MIT, Copyright (c) 2026 Gustavo Garcia.
Same engine: Metallica's XEX is `gh4_1_burn_Final.exe`, Van Halen's `gh4_3_Final.exe` (Neversoft "gh4", the
World Tour engine, XDK 2.0.7978). VH is a mechanical port of the Metallica repo: same 22 `src/` files, same 50
hooks, addresses translated by a function matcher (`.data` +0x1FE80, `.rdata` ~+0x1FDD0). Our targets:
gd-game-10 (Metallica) and gd-game-15 (Van Halen), both empty skeletons today; gd-game-07 (World Tour) shares the engine.

Licensing (both): MIT on their own code keeps the copyright line + permission notice in any copy or substantial
portion. We satisfy it without maintainer names in our code: no file of ours carries their header; if we copy a
substantial portion (we do not plan to), the module ships `licenses/ElGus-recomps-MIT.txt` (their LICENSE verbatim)
and a line in the package's `THIRD_PARTY_NOTICES` (as `adding-a-game.md` step 4 already does for libraries).
Knowledge (addresses, causes, constants) restated in our own words in `docs/` is not a copy and needs no notice;
a "References" line in the game's docs may name the project, not the person. Their deps: ReXGlue SDK (BSD-3,
Xenia-derived), NativeXRenderer (MIT; pulls DiligentCore Apache-2.0, DLSS NVIDIA licence), RecompLauncher, FFmpeg
and libmspack (LGPL), fonts (OFL). Submodules are not cloned here (empty dirs); none of it is needed by us.

---

### A. Guitar Hero: Metallica (GHMetalRecomp → gd-game-10)

#### 1. What it is
| Item | Finding |
|---|---|
| Kind | Static recompilation: ReXGlue turns `default.xex` PPC into C++ at build time (`generated/default`, not committed: only `rexglue.cmake`). Hand-written: 22 files / ~3.3k lines in `src/` (hooks, options, startup), 2 TOML maps, 28 docs |
| Function maps | `ghmetallica_functions.toml` (281 lines: setjmp/longjmp, missed functions, 50 `[[midasm_hook]]`s with named addresses), `_auto.toml` (gap finder) |
| Renderer | Xenos emulation (Xenia-derived, Vulkan) or NativeXRenderer (D3D9-style calls to Vulkan natively); FSR1/NIS/CAS, SMAA |
| Platforms | Windows 10/11 x64 only. Linux/Steam Deck "in the works". No macOS/Android |
| Maturity | Beta. Guitar/bass full songs; vocals experimental; drums untested; DLC (Death Magnetic) works |
| Known bugs | drums untested; >60 FPS experimental (frame-counted timers); keyboard experimental; ultrawide band on 2nd controller sign-in screen; first-use effect hitch on Xenos; AMD untested |

#### 2. What we can take (knowledge, not code)
They run the original code; we rewrite the game. So their C++ is glue around generated code and almost none of
it is reusable; their **notes and named addresses** are the value, read as research leads for our Ghidra work.

| Asset | Use in gd-game-10 |
|---|---|
| Hook address list (50 named functions: timer `sub_822D0FB8`, frustum `sub_82356538`, sprite/tail/highway-board draws, controller port, job system, vblank) | Seed names in our Ghidra project; jump-start `FINDINGS.md` |
| `song-sync.md`: game uses 49.875 MHz timebase correction → 0.25 % drift (~150 ms/min) | Our clock: use real time; record as "original bug, fixed" with a toggle |
| `crowd-claps.md`: fretbar script waits fixed 0.18 s, ignores calibration | Reimplement claps offset by calibration (Enhanced toggle, default faithful) |
| `ultrawide.md`: 1280×720 canvas, Hor+ via frustum, 2D kept 16:9, highway depth quad | Confirms our layout model; projection math reference |
| `frame-rate.md`: which timers count frames (boot star, calibration beeps, fret spring-back) | Checklist of frame-rate-dependent behaviour to do in time-based form |
| `screen-effects.md`: bloom, DOF, grain, colour correction, vignette, ink blotches | List of post-effects to reimplement + per-effect switches |
| `guitar-controller.md`, PS3 yellow/blue swap, whammy rest, tilt axis | Input profiles for gd-engine-core inputs |
| `dlc.md`: STFS packages, `XamContentAggregateCreateEnumerator`, title 41560830 | DLC read-in-place design for our disc/package reader |
| `song-audio.md`, `menu-music.md`, `microphone.md`, `second-controller.md` | Behaviour notes (all tracks mixed, USB mic, guest players) |

#### 3. Saving
**~8–12 % of a port.** The heavy work (QB/QScript, .pak/.fsb/.xen formats, highway, venues, band) is not in the
repo at all; most is shared with gd-game-04 (GH3) / gd-game-07 anyway. Savings are research time: named addresses
for timing, input, projection and 2D layers, and a ready list of original bugs and frame-dependent behaviour.

#### 4. Not used
| Item | Why |
|---|---|
| Generated C++ / ReXGlue SDK / recompiled XEX | Is the original code; forbidden by `docs/legal.md` |
| `src/*.cpp` hooks | Patch registers of the recompiled binary; meaningless in a reimplementation |
| NativeXRenderer, Xenos path | Emulates D3D9/Xenos; we use gd-engine-core backends |
| RecompLauncher, profiles, achievements toast, updater | Our Universal Launcher covers these |
| DLSS/FFmpeg/LGPL deps | Licence/size burden, not needed |

#### 5. Effort (1–10)
| Effort | Score | Why |
|---|---|---|
| Make their code look like ours | 10 | Not convertible: architecture is "original binary + patches"; only rewrite works |
| Our menus + universal behaviours | 2 | We write the engine, so Enhanced menu, GAMEPADS, help bar plug in as in gd04 (on their side it would be 8–9) |

#### 6. Good features for our other games
- Per-effect post-processing switches (bloom, DOF, grain, colour, vignette).
- "Original bug fixes" with a toggle each (clock drift, claps vs calibration).
- Shader/pipeline pre-compile at first launch ("Compiling shaders").
- FSR1 / NIS / CAS upscaling + sharpness row.
- Exclusive-mode low-latency audio option.
- Remappable whammy/tilt axis for third-party guitars; PS3 guitar fret fix.
- Auto-strum (fret-only) mode for controllers/keyboard.
- DLC folder read in place, grouped by game.

Owner's take:

#### 7. Import plan
1. Copy their docs index into `gd-game-10/docs/colabs-notes.md` as our own summary (no text pasted), with a References line naming the project. Owner's take:
2. Ghidra on our Metallica XEX: name the 50 hooked functions from their TOML as leads; confirm each before use. Owner's take:
3. Reuse gd-game-04/07 Neversoft formats and QScript; diff gh4 vs GH3 in `FINDINGS.md`. Owner's take:
4. Reimplement timer (real-time clock), claps-with-calibration and frame-independent timers as toggles (default = original). Owner's take:
5. Add the per-effect switches, DLC folder reader and guitar-axis mapping to gd-engine-core for all games. Owner's take:
6. Add `licenses/` + notice line only if any substantial portion is ever copied (not planned). Owner's take:

Owner's take:

---

### B. Guitar Hero: Van Halen (GHVanHRecomp → gd-game-15)

#### 1. What it is
| Item | Finding |
|---|---|
| Kind | Same as Metallica: ReXGlue static recomp of `default.xex` (`gh4_3_Final.exe`); generated code not committed |
| Hand-written | 22 `src/` files (~3.2k lines), all Metallica files renamed with addresses translated; 5 docs (`fixes-from-gh-metallica.md` is the main one: every fix with its VH address) |
| Function maps | `ghvanhalen_functions.toml` (281 lines, 50 hooks), `_auto.toml` (94 gaps) |
| Platforms | Windows 10/11 x64 only |
| Maturity | Beta, younger than Metallica (first boot 2026-10-26). Guitar/bass full songs tested; drums and vocals untested; DLC not supported |
| Known bugs | same as Metallica + drums/vocals untested, no DLC (World Tour DLC import "in the works") |

#### 2. What we can take (knowledge, not code)
| Asset | Use in gd-game-15 |
|---|---|
| Metallica↔VH address translation (`fixes-from-gh-metallica.md`, both TOMLs side by side) | The key find: a function-level map between the two XEXs. Lets us build gd-game-15 as a delta of gd-game-10 |
| Data shift `.data` +0x1FE80, `.rdata` ~+0x1FDD0 | Port our own Ghidra labels from 10 to 15 quickly |
| VH hook list (timer `sub_822E2C58`, frustum `sub_823688F8`, controller `sub_823B0680`, tails, highway board) | Ghidra leads, as for Metallica |
| Same bug list (clock drift, claps, frame-counted timers) | Same toggles reused from gd-game-10 |

#### 3. Saving
**~5–8 % on its own; the real gain is ~60–70 % of gd-game-15 being shared with gd-game-10** (same engine, same
code shape), which their address map confirms. Do Metallica first, then VH as a data/script delta.

#### 4. Not used
Same as Metallica (generated code, SDK, hooks, renderer, launcher, LGPL/DLSS deps). Also their "Guitar Hero: World
Tour DLC import" plan: out of scope until our gd-game-07 exists.

#### 5. Effort (1–10)
| Effort | Score | Why |
|---|---|---|
| Make their code look like ours | 10 | Same reason as Metallica |
| Our menus + universal behaviours | 2 | Inherited from gd-game-10's module once it exists |

#### 6. Good features for our other games
- A function matcher between two builds of one engine (instruction index) for our own Ghidra projects (GH3 → Aerosmith, WT → Metallica → VH, RB → RB2).
- Guest players: the controller that presses Start owns the profile, others join as guests.
- Quit prompt on B at the title/main menu.

Owner's take:

#### 7. Import plan
1. After gd-game-10 reaches playable, branch gd-game-15 from it (shared engine code in one place, per-game data). Owner's take:
2. Use their address map as leads to transfer our gd-game-10 Ghidra names to the VH XEX; verify each. Owner's take:
3. Diff VH's QB scripts and data vs Metallica (career, venues, characters, setlist) and record in `gd-game-15/docs/`. Owner's take:
4. Drums and vocals: test early since they are untested on their side too (no knowledge to borrow). Owner's take:
5. Notices: none needed for knowledge only; same `licenses/` rule as Metallica if code is ever copied. Owner's take:

Owner's take:

---

## msr2ds HDR study (GDALL-043 colabs 01)

**Licence: we cannot copy any of it.** Root `license.txt` is GPL-2.0, `native/LICENSE` (it covers `video.c`, `shaders/hdr.frag`, `win/hdr_win.c`, `msr/settings.cpp`) is GPL-3.0, and `src/msr/overlay.cpp` is "GPLv2 or later". Only `externals/msr-imgui` (MIT) and the AMD FSR headers (MIT) are permissive, and we don't need either. GPL can't go into a PolyForm-NC project, so we take the *technique* only and write our own code. The PQ constants and the BT.709→2020 matrix come from public standards (ST 2084, BT.2087), and we already have them.

### 1. What msr2ds is and how its HDR works
msr2ds is a native PC port of *Metroid: Samus Returns* (3DS) that runs on the user's own dump. It renders in Vulkan (with an OpenGL fallback that has no HDR) and uses an ImGui settings overlay. All of the HDR code is about 120 lines.
| Aspect | What they do |
|---|---|
| Swapchain | Uses `VK_EXT_swapchain_colorspace`. Prefers HDR10 (`A2B10G10R10`/`A2R10G10B10` with `HDR10_ST2084`), else scRGB (`R16G16B16A16_SFLOAT` with `EXTENDED_SRGB_LINEAR`). Needs Vulkan 1.3. The swapchain is rebuilt live when the toggle or the display changes. |
| Detection | Windows: DXGI `IDXGIOutput6::GetDesc1` (ColorSpace = G2084_P2020 → "Use HDR" is on; MaxLuminance = peak) plus `DISPLAYCONFIG_SDR_WHITE_LEVEL` (= the OS paper white). Linux: HDR is offered whenever the surface lists HDR formats. Checked again each time the settings open. |
| Steam Deck / gamescope | Nothing specific: no env vars, no own WSI layer, no DXVK/vkd3d. They rely on gamescope's WSI layer, which lists HDR10 formats only while Deck HDR is on. Their README still says "HDR is Windows only". |
| Metadata | `vkSetHdrMetadataEXT` with BT.2020 primaries, D65, maxLuminance = MaxCLL = **user peak**, MaxFALL = **paper white**, min 0. It is set again when the swapchain is rebuilt. |
| Tone mapping | Inverse tone map only (the 3DS art is SDR). Decodes with **gamma 2.2** (not the piecewise sRGB curve). Above a luminance **knee of 0.5**, it scales luminance smoothly (t² ramp, slope 1 at the knee) up to peak/paper at SDR white. RGB is scaled by Y'/Y, so hue and saturation hold. Then ×paper nits → BT.2020 → PQ, or /80 for scRGB. |
| Defaults | Peak = display peak, **capped at 1000 nits** ("a 3,900-nit glow is blinding"). Paper = the OS SDR white (fallback 200). minLuminance 0, so OLED black stays true black. Nothing else is OLED-specific. |
| UI | HUD and menus go to their **own premultiplied layer** and are composited at paper white in the same pass. Where UI coverage passes 25% (smoothstep), the picture under it is *not* expanded, so dim cards still darken the scene as in SDR (bug fixed 2026-11-03). |
| Settings | HDR on/off (greyed out with a hint when the OS isn't in HDR). "HDR highlights" 400–4000 nits. "HDR paper white" 80–500 nits. The Deck preset (found by DMI name Jupiter/Galileo) covers fps and resolution only, not HDR. |
| Fallbacks | No colour-space extension, no HDR formats, no Vulkan 1.3, or OpenGL → SDR with the option hidden or disabled. |

Owner's take:

### 2. Compared with ours
| Point | Ours today | Verdict |
|---|---|---|
| HDR source | gd01: RGBA16F scene, auto exposure, ACES with an HDR shoulder up to the headroom (`core_post_fx.cpp`) | **Ours is better**: real scene light, not SDR expansion |
| Swapchain | gd-engine-core Vulkan `hdrOutput` (#80): PQ / scRGB, gd01 only. gd02/03/05 use OpenGL "local" (WGL scRGB, Wayland EGL). sdl3 and core GL3: none | They're no better. Our gap is coverage (sdl3, GL3, gd02–05 on Vulkan) |
| Detection | SDL window properties (SDR white, headroom). On gamescope: hard-coded 203 / 1000 nits | Same quality. SDL3 already wraps what their DXGI code reads |
| Metadata | Fixed: BT.709 primaries, 1000 / 400 nits, sent for PQ only | **Theirs is better**: tied to the chosen peak |
| User control | None / Emulated / True HDR only | **Theirs is better**: peak and paper sliders |
| SDR decode | Piecewise sRGB | **Theirs is better on OLED**: gamma 2.2 keeps near-blacks deep (sRGB decode lifts them, the usual "washed-out HDR" complaint) |
| UI | Drawn into the frame after post, converted together at paper white | **Theirs is better**: a separate layer, and no glow under dimmed menus |
| SDR-art highlights | Only lights above 1.0 reach the headroom; the HUD and highway stay SDR | Theirs adds an inverse-TM knee. That's handy for venues/videos made in SDR, but it isn't "match the original" unless optional |
| Live toggle | Restart needed (the surface is chosen when the window is made) | **Theirs is better**: the swapchain is rebuilt live |

Owner's take:

### 3. In gd-engine-core for every game
- **Device API** (`device.h`): keep `hdrSurface()` and add `struct HdrDisplay { int surface; float paperNits, peakNits; const char* source; }` from `hdrDisplay()`. Add `setHdrMetadata(maxCll, maxFall)`, applied when the swapchain is rebuilt. Make `requestHdr(bool)` rebuild the swapchain live (Vulkan, and SDL_GPU via `SDL_SetGPUSwapchainParameters` HDR10_ST2084 / HDR_EXTENDED_LINEAR). OpenGL3: move gd02's local EGL/WGL path into core, at startup only.
- **Shared pass** `core::HdrCompose` (our own GLSL, through gdcore_spirv / the GL dialect). Inputs: the scene (RGBA16F) and a UI layer (RGBA8 premultiplied). Parameters: mode (scRGB/PQ), paper, peak, knee, decode (sRGB/2.2), expand (on/off). It owns PQ, the 2020 matrix, our own knee curve and the "no expansion under UI coverage" rule. Games keep their own scene tone maps (gd01's ACES shoulder); the pass is only the last step.
- **Enhanced menu** (universal-menus.md, Performance & graphics, under HDR, shown only with True HDR): `HDR Peak Brightness` (Auto, 400–2000 nits; Auto = display peak capped at 1000), `HDR Paper White` (Auto, 80–400; Auto = OS value or 203), `HDR SDR Gamma` (sRGB / 2.2), `HDR Highlight Boost` (Off / On, default Off to match the original).
- **Steam Deck OLED preset**: DMI `Galileo` + `GAMESCOPE_WAYLAND_DISPLAY` → Auto gives peak 1000, paper 203, gamma 2.2, metadata 1000 / 203. On the LCD Deck (`Jupiter`) True HDR is hidden.
- **Effort**: 6/10, about **8–10 agent h** (core API + Vulkan live rebuild + metadata ~3 h, SDL_GPU ~1.5 h, GL3 port ~1.5 h, wiring gd01–05 + menu + docs ~3 h).
- **Risks**: no HDR test device in the team (the owner's Deck is the original LCD model): a player with a Deck OLED or an HDR monitor must confirm; SDL_GPU HDR is young; a separate UI layer changes each game's draw order (gd02–05 draw the HUD into the frame); gamescope may tone-map again if MaxCLL > panel peak; a live rebuild on GL isn't possible.

Owner's take:

### 4. Implementation plan
1. Backlog item and branch (only when the owner asks); write the design into `gd-engine-core/docs/render.md` §True HDR.
Owner's take:
2. Device API: `hdrDisplay()`, `setHdrMetadata()`, live `requestHdr()`; Vulkan implementation with metadata from settings; offscreen tests for the fallbacks.
Owner's take:
3. `core::HdrCompose` shader (own code): PQ/scRGB, sRGB/2.2 decode, optional knee boost, UI layer with the coverage rule; golden-value unit tests (PQ(203) ≈ 0.58, black = 0).
Owner's take:
4. sdl3 backend: SDL_GPU HDR swapchain compositions behind the same API.
Owner's take:
5. opengl3 backend: move gd02's EGL/WGL HDR surface into core (startup only).
Owner's take:
6. gd01: replace `outputHdr` / `hdrOutput` with HdrCompose; split the HUD/menus into the UI layer; drop the hard-coded gamescope 203/1000 for the preset.
Owner's take:
7. Enhanced menu rows (Peak, Paper White, SDR Gamma, Highlight Boost) in universal-menus.md and gd01, with the Deck OLED Auto preset.
Owner's take:
8. Roll out to gd02–gd05 (True HDR also on Vulkan, not only local GL); STEAM-DECK.txt and the READMEs.
Owner's take:
9. A player with a Deck OLED (Gaming and Desktop Mode) and someone with a Windows HDR monitor test it; tune the defaults; CHANGELOGs.
Owner's take:
