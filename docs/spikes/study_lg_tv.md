# LG webOS TV

Status: study, 2026-10-28. It merges the first study on this topic
(written earlier the same day).

## Verdict

**Yes, we can sideload, and a native build is realistic for enthusiasts. It
is not possible through LG's store.** The route is the **native homebrew**
one: an ARM ELF built with the webOSbrew toolchain and SDL, packaged as an
`.ipk`, installed with `ares-install` through LG's **Developer Mode app**, or
through the **Homebrew Channel** (with or without root). It is about **2 to 4
weeks** for gdul + gd02/gd01 on a 2020+ TV. The official LG Content Store
only takes web apps, and a "bring your own Xbox 360 disc" WASM app is both a
large port and a likely rejection. This fits the owner's "LG TV (Coming
soon)" release slot as a homebrew release.

## Can we sideload? Yes, three ways

| Way | What the user does | Limits |
|---|---|---|
| **Developer Mode app** (official, no root) | Create a free LG developer account. Install "Developer Mode" from the LG Content Store and sign in. Turn on Dev Mode and the key server, and reboot. On the PC, `ares-setup-device` (TV IP, passphrase from the app), then `ares-install gdul.ipk`. | The session expires. Originally **50 hours**, with an "Extend" button in the app. Recent firmware reports show **1000 hours**. When it expires, dev-mode apps are **removed**. Renewal can be automated (the Homebrew Channel or a script calling LG's extend endpoint). Apps run **jailed** (an unprivileged user, some system services blocked). That is fine for a game. |
| **Homebrew Channel without root** | Install HBC's `.ipk` in Dev Mode. Users then install our app from the webOSbrew **repo** inside HBC. | The same Dev Mode timer applies (HBC can keep it renewed). |
| **Homebrew Channel with root** | Root the TV with an exploit: **faultmanager-autoroot** (webOS 4.0 to 10.0, but patched on the latest firmware of most models as of 2025-08), **DejaVuln** (webOS 3.5+), older RootMyTV/crashd. Check **CanI.RootMy.TV** for the firmware. | No timer and no jail. But root depends on not updating the firmware, and new 2025+ firmware is mostly patched. We should never require root. |

The **CLI**: LG's `ares-cli` (now the npm package `@webos-tools/cli`):
`ares-setup-device`, `ares-install`, `ares-launch`, `ares-inspect`,
`ares-shell` (Dev Mode shell), and `ares-package` (builds the `.ipk` from
`appinfo.json` + files).

## Native vs web

| | Native (`.ipk`, ELF), recommended | Web app (`.ipk`, HTML/WASM) |
|---|---|---|
| Code | ARMv7 (most TVs) / aarch64 ELF from the **webOSbrew toolchain** (buildroot-nc4). SDL2 has a working webOS port (Moonlight, ScummVM, RetroArch ship this way). **SDL3 has no webOS port**: use SDL2 through a thin shim, or port SDL3's video bits (Wayland + webOS shell), 1 to 2 weeks. | Emscripten: SDL3's Emscripten backend, GLES3 → WebGL2 (webOS 5+), FFmpeg in WASM |
| GPU | GLES 2/3 through EGL/Wayland. Our Android GLES3 path fits. | WebGL2 on the TV's Chromium (webOS 5+ only; 4.x is WebGL1) |
| Game modules | `dlopen` works as on Linux. The launcher model applies unchanged. | No `dlopen`. Static link (`study_ios.md`'s re-entrancy item). |
| Disc | `fopen` from a USB stick (`/tmp/usb/sdX/...`). A 2.2/7.3 GB ISO works. | **Blocker**: a web app can't reach a 7.3 GB ISO on USB, and wasm32 has 4 GB of address space |
| Store | Not possible (the Content Store takes no third-party native apps) | LG Seller Lounge QA (vendors report 8 to 12 weeks). High IP rejection risk. |

## Input

- **Magic Remote**: a pointer (mouse) plus arrow/OK keys. The launcher's
  mouse and arrow navigation covers it. Back is key code 461 (web) or an SDL
  key on native, and must map to "back".
- **Gamepads**: USB/Bluetooth pads through evdev/SDL. **Guitars**: PS3/Wii
  USB guitars are HID and probably work on native. Xbox 360 guitars
  (XInput) need `xpad`, which most TV kernels lack.
- **Audio latency**: TV audio adds 50 to 150 ms, and the Game Optimizer
  (ALLM) mode helps. **A latency calibration screen is required** (gd01 has
  calibration, but gd02 does not yet: backlog M7).

## Performance

2020 to 2025 webOS TVs: Cortex-A53/A55/A73 SoCs (α5/α7/α9 Gen 3 to Gen 8),
with Mali GPUs at about the level of a 2016 to 2019 phone. gd01 runs at
1080p. gd02 needs the "handheld" Enhanced preset (no DOF/bloom at full
resolution, 720p internal). The memory available to apps is about 1 to 1.5 GB.

## Effort and costs

- Native homebrew: 2 to 4 weeks (toolchain + CMake toolchain file, SDL2 shim
  or SDL3 port, USB disc paths, remote mapping, `.ipk` packaging, latency
  calibration). A free LG developer account. The release job uses the
  webOSbrew toolchain tarball on Linux (cheap, release-only).
- Distribution: GitHub Releases `.ipk` + the webOSbrew repo (a PR to
  `webosbrew/apps-repo` with a manifest). No money.
- Web/store route: 2 to 3 months (the WASM port shared with Samsung). Not recommended.

## Steps

1. Spike (1 week): build gd02 standalone (or gdul + gd02) with the webOSbrew
   toolchain + SDL2. Read the ISO from USB, install with `ares-install` in Dev
   Mode, and measure fps and audio latency on a 2020+ TV.
2. Add the latency calibration (gd02) and the remote mapping.
3. Package the `.ipk` (our own icon, no trademarks), add a release job, and
   add a webOSbrew repo entry. Document the Dev Mode / HBC install.

## Sources

- webOSbrew developer docs, toolchain, Dev Mode: https://www.webosbrew.org/develop , https://www.webosbrew.org/devmode/
- Homebrew Channel / apps repo: https://github.com/webosbrew/webos-homebrew-channel , https://github.com/webosbrew/apps-repo
- Root tools and status: https://github.com/throwaway96/faultmanager-autoroot , https://github.com/throwaway96/dejavuln-autoroot , https://cani.rootmy.tv/
- LG Developer Mode app: https://webostv.developer.lge.com/develop/getting-started/developer-mode-app
- ares CLI: https://webostv.developer.lge.com/develop/tools/cli-introduction
- Dev Mode timer and renewal: https://elis.nu/blog/2022/07/keeping-webos-developer-mode-alive-with-home-assistant/
- ScummVM native webOS build (SDL2, Dev Mode): https://lists.scummvm.org/pipermail/scummvm-devel/2026-August/013079.html
- LG review time (vendor report): https://docs.easeltv.com/docs/lg-tv-submission

## Backlog candidates

- **(GDUL, Enhancement) webOS native spike**: webOSbrew toolchain, SDL2/SDL3
  video, ISO on USB, Dev Mode install, fps and latency measured. 1 week.
- **(GD02, Enhancement) Audio-latency calibration screen** (also needed for
  Android TV and Samsung). 3 to 5 days.
- **(GDUL, Enhancement) TV remote navigation**: Back key 461 / webOS keys,
  pointer, big-screen safe areas. 2 to 3 days.
- **(GDREL, Enhancement) LG TV homebrew release**: `.ipk` release job,
  webOSbrew repo entry, install guide (Dev Mode + HBC). 3 to 5 days.
