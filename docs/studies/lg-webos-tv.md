# LG webOS TV app

Status: study, 2026-10-28.

## Verdict

**Not worth doing now.** There are two possible routes. The **native homebrew
route** (webOSbrew toolchain + SDL2, sideloaded in Developer Mode) could run a
build of gd02 on a recent LG TV for enthusiasts, with roughly 2 to 4 weeks of
porting. The **official route** (a web app in the LG Content Store) means a
WebAssembly + WebGL2 port of the whole stack, a 7 GB disc image inside a TV
browser sandbox, and a store review that is unlikely to accept a "bring your
own Xbox 360 disc" app. Revisit if a WASM build exists anyway (see
`samsung-tizen-tv.md`, which shares it).

## Two routes

| | Web app (`.ipk`, official) | Native app (homebrew) |
|---|---|---|
| Code | Emscripten: SDL3 (has an Emscripten backend), GLES3 maps to WebGL2, FFmpeg built for WASM | ARMv7/aarch64 ELF from the webOSbrew toolchain. SDL2 works (Moonlight and ScummVM ship this way). SDL3 is not ported, so we would use SDL2 or port SDL3's webOS bits. |
| Rendering | WebGL2 on the TV's Chromium. That needs webOS 5+ (Chromium 68+); webOS 4.x and older are WebGL1 only. | GLES2/3 through the SDL2 port's EGL. Our GLES3 path (Android) mostly fits. |
| Game modules | No `dlopen`. Link the games statically or use Emscripten side modules. | `dlopen` works as on Linux. |
| Disc | **The main blocker.** A 7.3 GB GH2 ISO can't be picked or streamed from USB in a web app, and wasm32 is limited to 4 GB of address space. Only a pre-extracted subset of assets could work. | Read from a USB stick (`/tmp/usb/...`) with `fopen`. This works. |
| Install | LG Developer Mode + `ares-install`, or the Content Store | Developer Mode (the session expires about every 50 hours and must be extended in the app), or a rooted TV with the Homebrew Channel |
| Store | LG Seller Lounge, QA review (vendors report 8 to 12 weeks) | Not possible. The LG Content Store does not take third-party native apps. |

## Input

- **Magic Remote:** a pointer (mouse) plus arrow and OK keys. The launcher's
  mouse and arrow-key navigation already covers it. Back is a separate key code
  (`461` on the web, a keycode on native) that must be mapped to "back".
- **Gamepads:** USB and Bluetooth pads work through the Gamepad API (web) or
  evdev/SDL (native). **Guitars:** PS3/Wii-era USB guitars are plain HID and are
  likely seen by SDL on native. Xbox 360 guitars are XInput (not HID) and need
  a driver the TV lacks. They are unlikely to work in either route.
- **Audio latency:** TV audio paths add 50 to 150 ms. That needs calibration,
  which gd02 does not have yet (backlog M7). Without it the game is unplayable
  on time-based charts.

## Performance

webOS TVs from 2020 to 2025 use ARM Cortex-A53/A55/A73 SoCs with Mali GPUs,
about the level of a 2016 to 2019 phone. gd02 at 1080p with the venue and
post-FX would need the lighter "handheld" settings. The web route has extra
overhead on top of that: WebGL validation and WASM without native SIMD.

## Effort and costs

- Native homebrew: 2 to 4 weeks (toolchain, SDL2 shim or SDL3 port, USB discs,
  remote mapping, latency calibration). A free LG developer account. No Actions
  cost if built locally; otherwise one extra job.
- Web app plus store: 2 to 3 months (WASM port of the launcher and gd02, asset
  pre-extraction, storage). Store review risk is high. Copyright and impersonation
  checks are likely to flag "Guitar Hero" in a listing.

## Steps (only if pursued)

1. Do the native spike first: build gd02 as a standalone ELF with the
   webOSbrew toolchain, read the ISO from USB, run it in Developer Mode, and
   measure FPS and audio latency.
2. Add an audio-latency calibration screen (needed for TVs anyway).
3. Decide on the web route only if a WASM build exists for another reason.

## Sources

- webOSbrew developer docs and toolchains: https://www.webosbrew.org/develop , https://www.webosbrew.org/devmode/
- ScummVM native webOS build (SDL2, Developer Mode): https://lists.scummvm.org/pipermail/scummvm-devel/2026-August/013079.html
- LG webOS TV developer site (web apps, CLI, Developer Mode): https://webostv.developer.lge.com/develop/getting-started/developer-mode-app
- LG review time (vendor report): https://docs.easeltv.com/docs/lg-tv-submission
