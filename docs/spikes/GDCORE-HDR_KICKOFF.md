# Kickoff: GDCORE-HDR, one shared HDR output for every game (gd-engine-core)

> **You are adding one HDR path to gd-engine-core and moving every game onto
> it.** Read this whole file before your first command. When it conflicts
> with a tool default, this file wins. When it conflicts with
> `docs/guidelines/`, the guidelines win, except where this file says a
> guideline changes (section 5), and only after the owner's yes.

## 1. The goal

| | |
|---|---|
| What | One HDR output in `gd-engine-core/render/` used by **every graphics backend** (Vulkan, SDL3, OpenGL3) and **every game** (gd-game-01..05 now, every later game by copying the pattern) |
| Output | HDR10 (BT.2020 PQ) or scRGB (linear, 1.0 = 80 nits) swapchain / surface; mastering metadata that follows the **chosen peak**; SDR→HDR expansion with a **knee**; an **OLED gamma 2.2** decode option; **UI / HUD in its own layer at paper white**; HDR on / off **without a restart** where the backend allows |
| Menus | Under the existing Enhanced > Performance & graphics > **HDR** row: **HDR Peak Brightness**, **HDR Paper White**, **HDR SDR Gamma**, **HDR Highlight Boost** (section 5) |
| Presets | **Steam Deck OLED Auto** under gamescope: peak 1000 nits, paper white 203, gamma 2.2, metadata 1000 / 203. Windows HDR from the OS values |
| Fallbacks | Never a black window or a crash: True HDR that cannot run uses Emulated and **logs and shows the reason**, as #80 did for gd01 |
| Backlog | proposed **GDCORE-012** (gd-engine-core section) with one row per game's adoption; the owner adds the IDs |
| Branch | the owner's, named when the run starts. Never create or switch branches yourself |
| Estimate | **8-10 agent hours** (section 6), measured from git history at each gate |

**Where the idea comes from.** `docs/spikes/colabs01.md`, last section (the
HDR study of a GPL-licensed community port), and `docs/spikes/study_hdr.md`.
**That port is GPL: we take the technique only.** Write every line of code
and shader fresh, in our own words. **No names** of that project or its
authors in code, comments, shaders, commit messages, game docs or
CHANGELOGs: the only place they appear is colabs01.md. The PQ curve (SMPTE
ST 2084), the BT.709→BT.2020 matrix (ITU-R BT.2087) and scRGB are public
standards and are already in our code.

## 2. Where we start (read before writing any code)

| Piece | Today |
|---|---|
| `gd-engine-core/render/include/gd/core/render/device.h` | `DeviceCreateInfo::hdrOutput`; `Device::hdrSurface()` (0 SDR, 1 scRGB, 2 PQ) and `hdrReason()` |
| Vulkan backend (`render/src/vulkan/vk_device.cpp`, #80, GD01-318) | Opt-in HDR10 / scRGB swapchain via `VK_EXT_swapchain_colorspace`, `VK_EXT_hdr_metadata` with **fixed** values; chosen when the swapchain is made; reasons logged (gamescope / desktop) |
| SDL3 GPU backend | SDR only; True HDR runs as Emulated and says so (`render.md` 4.2) |
| OpenGL3 backend (core) | SDR only |
| gd-game-01 | `render/hdr_output.*`, `core_post_fx.cpp`: RGBA16F scene, auto exposure, ACES shoulder up to the headroom; local GL True HDR (WGL scRGB, Wayland EGL scRGB / PQ + SMPTE 2086); Vulkan True HDR through #80, gamescope assumed 203 / 1000 nits; walk `enhanced_hdr_gamescope`, `enhanced_hdr_frame` |
| gd-game-02..05 | gd-game-02's local GL True HDR (copied into gd03 / gd04 / gd05); on core backends True HDR runs as Emulated |
| Guideline | `universal_mechanisms.md` "HDR": **one** option, None / Emulated / True HDR, "no sliders"; True HDR never greyed; takes effect at the next start |
| Docs | `gd-engine-core/docs/render.md` (True HDR notes per backend), `gd-game-01/README.md`, `gd-engine-launcher/package/STEAM-DECK.txt` |

Read also: `docs/guidelines/` (all of it, especially
`engine-backend-guidelines.md` rules 2-10 and `universal-menus.md`),
`gd-engine-core/docs/`, the #80 commit (`git show fbad201`) and the backlog's
gd-engine-core section.

## 3. The technique, in our words

The order of one frame with True HDR:

1. **Scene** (venue, band, effects): each game keeps its own scene look.
   gd-game-01 keeps its RGBA16F scene, auto exposure and ACES shoulder; the
   others keep their original SDR scene. The output is a linear RGBA16F
   texture (values above 1.0 allowed).
2. **UI layer**: the highway, HUD, menus and the port's universal menus
   draw into their **own premultiplied RGBA8 target**, never into the scene.
3. **Compose pass** (`core::render::HdrCompose`, one shader source, our own):
   - decode SDR values with the **sRGB curve or gamma 2.2** (the setting);
   - **Highlight Boost** (off by default: the original look): above a knee
     at 0.5 of SDR white, raise luminance along our own smooth curve
     (continuous and slope 1 at the knee, monotonic, reaching peak / paper
     at SDR white), scaling RGB by the luminance ratio so hue and
     saturation hold;
   - no boost where the UI covers the picture (fade it out as UI alpha
     passes about 25 %), so dim menu cards still darken the scene;
   - scene and UI scaled to **paper white** nits, the UI over the scene;
   - out: BT.2020 + PQ for HDR10, or BT.709 linear / 80 for scRGB, or the
     unchanged SDR encode when HDR is off.
4. **Metadata**: max luminance = MaxCLL = the chosen **peak**, MaxFALL =
   **paper white**, min 0 (OLED black stays black); primaries of what we
   draw (BT.709), D65. Sent again whenever the swapchain is rebuilt.

**HDR None and Emulated stay pixel-identical to today** (the compose pass
is skipped or its SDR branch is an exact pass-through, checked by frame
hashes). The pass lives in the core and knows nothing of any game
(`engine-backend-guidelines.md` rule 5).

### Auto values

| Where | Peak (Auto) | Paper white (Auto) | SDR gamma (Auto) |
|---|---|---|---|
| Windows / Wayland desktop | SDL `HDR_HEADROOM` × paper, capped at 1000 | SDL `SDR_WHITE_LEVEL` × 80 nits | sRGB |
| Steam Deck OLED, gamescope (`GAMESCOPE_WAYLAND_DISPLAY` + DMI `Galileo`) | 1000 | 203 | **2.2** |
| Other gamescope (LCD Deck docked to an HDR TV, desktops) | 1000 | 203 | sRGB |
| Unknown | 1000 | 203 | sRGB |

Values are re-read on `SDL_EVENT_WINDOW_HDR_STATE_CHANGED`. A user value
replaces Auto.

## 4. The design

**Device API** (`device.h`, additive; old calls keep working):
- `struct HdrDisplay { int surface; float paperNits, peakNits; const char* source; };`
  from `Device::hdrDisplay()`.
- `Device::setHdrMetadata(float maxCll, float maxFall)`.
- `Device::requestHdr(bool)`: live where the backend can (table below);
  returns whether it applied now or at the next start.
- `struct HdrSettings { mode, peakNits, paperNits, gamma22, boost; }`
  passed to the compose pass; games fill it from their settings.

| Backend | Swapchain / surface | Live on / off | Metadata |
|---|---|---|---|
| **Vulkan** | `VK_EXT_swapchain_colorspace`: HDR10 `A2B10G10R10` / `A2R10G10B10` + `HDR10_ST2084`, else scRGB `R16G16B16A16_SFLOAT` + `EXTENDED_SRGB_LINEAR` (#80's choice) | **Yes**: rebuild the swapchain | `vkSetHdrMetadataEXT` from the settings |
| **SDL3 GPU** | `SDL_SetGPUSwapchainParameters` with `HDR10_ST2084` / `HDR_EXTENDED_LINEAR`, checked with `SDL_WindowSupportsGPUSwapchainComposition` | **Yes** | None in SDL's API at our pinned version [I: check]; log "no metadata" |
| **OpenGL3** | Move gd-game-02's EGL (Wayland: scRGB, else PQ + SMPTE 2086) and WGL (RGBA16F scRGB) surface code into the core | **No**: chosen when the window is made, so the Restart Now / Later pop-up | EGL SMPTE 2086 from the settings |
| **local** (each game's own GL) | Keeps its surface code; uses the core's compose shader source through a small GL helper (question 3) | No (Restart Now) | as today, from the settings |

Every path logs one line: `hdr: <surface> (<source>), peak <n>, paper <n>,
gamma <srgb|2.2>, boost <on|off>` or `hdr: unavailable (<reason>), using
Emulated`. Reasons include: no colour-space extension, surface has no HDR
format, display HDR off, gamescope HDR off, OpenGL under gamescope ("set
Graphics Backend to Vulkan"), macOS / Android, Steam Deck LCD screen.

**Launcher window** (rule 9): a module that rebuilt the swapchain as HDR
gives the window back SDR; the launcher's own menus never change.

## 5. Menus and guidelines

The guideline today says "no sliders". This run changes it, **only with the
owner's yes** (question 1): `universal_mechanisms.md` "HDR" and the
Performance & graphics table in `universal-menus.md`, then every game.

| Row (shown under HDR, only when HDR is True HDR) | Values | Saved as | Default |
|---|---|---|---|
| HDR Peak Brightness | Auto, 400, 600, 800, 1000, 1500, 2000 nits | `hdr_peak` (0 = Auto) | Auto |
| HDR Paper White | Auto, 80, 100, 150, 203, 250, 300, 400 nits | `hdr_paper` (0 = Auto) | Auto |
| HDR SDR Gamma | Auto, sRGB, 2.2 | `hdr_gamma` | Auto |
| HDR Highlight Boost | Off, On | `hdr_boost` | Off (the original look) |

Rules kept: True HDR always selectable and never greyed, Emulated with the
reason in the help box's "Now:" line; Modes, Remove All Effects, Export /
Load User Settings and the value checks include the new keys; a file
without them loads as Auto / Off. The help box shows the Auto values in use
("Auto: 1000 nits, Steam Deck OLED"). Changes apply on the next frame;
the On / Off switch is live on Vulkan and SDL3, Restart Now on OpenGL.

## 6. Milestones (in order; each ends at a gate, section 7)

| # | Milestone | Est. | Gate |
|---|---|---|---|
| H0 | Backlog row; design in `gd-engine-core/docs/render.md` § True HDR; `PROGRESS.md` with the estimate | 0.3 h | doc reviewed against this file |
| H1 | **Core API**: `HdrDisplay`, `HdrSettings`, `setHdrMetadata`, live `requestHdr`; Vulkan live rebuild + metadata from settings; Auto values and the Deck OLED preset | 1.5 h | `gdcore_selftest` (preset and fallback cases); Vulkan logs on the fake gamescope env |
| H2 | **Compose pass** `HdrCompose` (one source, GL dialect + SPIR-V via `gdcore_spirv`), CPU reference of the same maths | 1.5 h | golden values (section 8) on CPU and on each backend's offscreen readback |
| H3 | **SDL3 GPU** HDR compositions behind the API, live toggle | 1.2 h | offscreen compose readback; reason logged where unsupported |
| H4 | **OpenGL3**: EGL / WGL surface code moved into the core, startup only | 1.2 h | Linux Wayland probe logged; Windows build; Wine reason logged |
| H5 | **Menus**: the four rows in gd-game-01 (reference), help texts, Auto display, Modes / Export / Load | 0.8 h | walks `enhanced_hdr_rows`, `enhanced_hdr_gamescope`, `enhanced_hdr_live` |
| H6 | **Adoption** gd-game-01 → 02 → 03 → 04 → 05: UI layer split, compose pass, rows, presets; gd01's hard-coded 203 / 1000 replaced by the preset | 2.5 h | per game: SDR frame identity, the HDR walks, a song on every backend pair |
| H7 | Docs (`render.md`, the games' READMEs, `STEAM-DECK.txt`), packages built locally, `verify_bundle.sh` | 0.8 h | section 7, end checks |

Total ≈ **9.8 h**; revise at each gate from the measured times. If the
owner prefers, H6 can stop after gd-game-01 and the rest follow game by game.

## 7. How you work and the gates

- GD-GAME-05_KICKOFF.md section 2 (PROGRESS / DECISIONS / OPEN_QUESTIONS,
  owner away, backlog at the start and end) applies.
- **Versions: never bump** the launcher or any game (`general_guidelines.md`,
  the new rule). Player-facing notes go under `## [Unreleased]` in each
  touched game's CHANGELOG; the owner bumps.
- **Scope:** `gd-engine-core/render/` (+ its tests and docs), the HDR code
  and Enhanced rows of gd-game-01..05, the two guideline sections (after the
  owner's yes), `STEAM-DECK.txt`, the backlog. Nothing else. Each game keeps
  its own scene look: the change is the last step only.
- **Commits** `git commit -s`, no AI lines, one per milestone, pushed; no PR.
- **Processes:** `timeout` on every run, one game at a time, scratch
  `XDG_DATA_HOME`, devices ignored, `pgrep -fa gd0` before each gate, kill
  by PID only.

A milestone is done only when: 0 warnings on Linux, Windows (MinGW) and
Android; `gdcore_selftest`, `render_selftest` and every touched game's
`--selftest` and walks pass **on a fresh binary**, pasted into
`PROGRESS.md`; **HDR None and Emulated frames identical** to before
(`enhanced_hdr_frame` hashes); `repo_guard.py` 0 problems; backlog updated;
committed and pushed; no game process left. At the end: all local packages
rebuilt, `verify_bundle.sh` lists every module on every platform, the
launcher plays each game 25 s and returns to its menu (Linux and Wine).

## 8. Tests we can run here (no HDR screen)

| Test | How | Checks |
|---|---|---|
| Compose maths (CPU) | `gdcore_selftest` | PQ(0) = 0, PQ(80) ≈ 0.4859, **PQ(203) ≈ 0.5807** (10-bit 594), PQ(1000) ≈ 0.7518 (769); scRGB 203 nits = 2.5375; decode of 0.05: sRGB 0.00394, gamma 2.2 0.00137; the knee continuous with slope 1, monotonic, reaching peak at SDR white; no boost under UI coverage; boost off = no change |
| Compose on each backend | `render_selftest`, offscreen: draw the pass into an RGB10A2 / RGBA16F texture and read it back (no swapchain needed) | GPU output within 1/1023 of the CPU reference on Vulkan, SDL3, OpenGL3 |
| Fake gamescope (as #80) | `GAMESCOPE_WAYLAND_DISPLAY=gamescope-0` + a test-only DMI override (e.g. `GDCORE_HDR_DMI=Galileo` / `Jupiter`) | log lines: OLED preset 1000 / 203 / 2.2 and metadata 1000 / 203; LCD reason; OpenGL reason names Vulkan |
| Live toggle | walk `enhanced_hdr_live` on Vulkan and SDL3 | "swapchain rebuilt" logged, no restart, no black frame (`check:no_black`); OpenGL shows Restart Now |
| Menus | walk `enhanced_hdr_rows` per game | rows hidden unless True HDR; values saved, exported, loaded; Auto text |
| SDR identity | walk `enhanced_hdr_frame` per game | None and Emulated hashes unchanged from before the run |
| Wine | the Windows build under Proton's wine | starts, logs the WGL / Vulkan reason, Emulated fallback |
| A song per backend pair | per game | local / local, OpenGL3 / miniaudio, SDL3 / OpenAL, Vulkan / FAudio |

**Needs a player with an HDR display** (list it in `PROGRESS.md` under "for a
player to check"; the owner's Deck is the original **LCD** model):
- Steam Deck OLED, Gaming Mode (Steam HDR on) and Desktop Mode (Plasma HDR):
  Auto preset, near-blacks deep, highlights not blinding, UI at a
  comfortable white, the toggle live on Vulkan, gamescope not tone mapping
  twice;
- Windows 10/11 with an HDR monitor: Auto from the OS values, each slider's
  effect, SDL3 (D3D12) and Vulkan;
- a Wayland desktop with HDR (KDE Plasma 6): OpenGL3 and Vulkan.

## 9. Risks

- No HDR test device in the team: the defaults are tuned blind until a
  player reports; keep Highlight Boost off by default.
- Moving the HUD / menus to their own layer changes each game's draw order
  (gd-game-02..05 draw the HUD into the frame): the SDR frame identity check
  is the guard.
- SDL3 GPU HDR is young; it may lack metadata or fail on some drivers:
  fall back with a reason.
- gamescope may tone map again if MaxCLL exceeds the panel's peak: Auto caps
  at 1000.
- No live rebuild on OpenGL: the Restart Now pop-up stays there.
- A temporal AI upscaler (FSR 3.1 / DLSS) takes 8-bit input: highlights are
  tone mapped first, as today.

## 10. Questions for the owner

1. **Guideline change:** may the four rows (Peak, Paper White, SDR Gamma,
   Highlight Boost) replace "no sliders" in `universal_mechanisms.md` and
   `universal-menus.md`?
2. **Branch and backlog ID:** which branch, and is GDCORE-012 the row?
3. **The `local` backends:** give gd-game-02..05's own GL path the new
   compose pass and rows too (more work in H6), or offer the new rows only
   on the core backends and keep local True HDR as it is?
4. **Highlight Boost:** default Off (the original look), as proposed?
5. **Steam Deck LCD:** keep True HDR selectable with the reason (the
   guideline), or hide it on the LCD screen? Do you have an HDR TV or
   monitor to dock the LCD Deck to (gamescope can offer HDR there [I])?
6. **Adoption order:** all of gd-game-01..05 in this run, or gd-game-01 first
   and the others after your test?
7. **A tester:** is there a player with a Deck OLED or a Windows HDR
   monitor we can ask, or do we ship it marked "untested on HDR screens"?

## Owner's answers

