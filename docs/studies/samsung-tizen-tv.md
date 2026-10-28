# Samsung Tizen TV app

Status: study, 2026-10-28.

## Verdict

**Not viable in practice.** The only route open to us is a **web app**
(`.wgt`): HTML + WebAssembly + WebGL2. Native C/C++ Tizen TV apps are for
Samsung partners, and NaCl ended with the 2021 models. Running the whole stack
in WASM on a TV, with a 7 GB disc image the web sandbox can't reach, is a
months-long port with a likely store rejection at the end. This is weaker than
LG's case, because Samsung has no native homebrew route.

## How a Tizen TV app could be built

| Topic | Facts | Impact on us |
|---|---|---|
| App types | Web apps (Tizen Web, `.wgt`) for everyone. WebAssembly is supported from Tizen 5.5 (2020 TVs). Native/.NET apps are partner-only. NaCl ended with the 2021 models. | Emscripten build of gdul + gd02, statically linked (no `dlopen`) |
| Graphics | Chromium-based engine (the version depends on the model year). WebGL2 on 2020+ models. | GLES3 to WebGL2 is close; check our extensions (float/10-bit targets) |
| SDL | SDL3 Emscripten backend (canvas, Web Audio, Gamepad API). SDL has no Tizen-specific port. | The launcher's SDL code mostly carries over |
| Storage / disc | No File System Access API. USB storage is reachable only through Samsung's partner-level filesystem APIs. wasm32 has a 4 GB address-space limit. | **Blocker:** the GH2 ISO is 7.3 GB. It would need asset pre-extraction on a PC plus upload or LAN streaming. |
| Input | Remote keys need `tizen.tvinputdevice.registerKey`. Gamepads have been supported since 2016 (up to 4, Gamepad API; not in the emulator). | Map remote keys to the frets. HID guitars might show up as gamepads; XInput guitars won't. |
| Audio | Web Audio plus TV processing adds latency | An audio-latency calibration screen is required |
| FFmpeg | Must be built for WASM (single-threaded unless COOP/COEP; size about 5 to 10 MB) | gd02's Bink/XMA → our decoders also need WASM builds |

## Developer mode and distribution

- **Sideload:** enable Developer Mode on the TV (Apps panel, type `12345`, set
  the host PC's IP). Then use Tizen Studio + TV Extension, create a Samsung
  author/distributor certificate in Certificate Manager (tied to the TV's DUID),
  and install with `sdb`/`tizen install`. This works for testing only. Friends
  can't install it without the same procedure and our certificate per TV.
- **Store:** Samsung Apps TV Seller Office, with a seller account (partnership
  approval for some regions), QA, and a content review. Apps that need external
  copyrighted game data, named after Activision's IP, are a high rejection
  risk.
- Samsung's Gaming Hub is a cloud-streaming portal, not open to independent apps.

## Performance

TVs from 2020 to 2025 use Cortex-A53/A55/A73-class SoCs with Mali GPUs. gd02 in
WASM + WebGL2 would need the lightest settings, and probably 720p rendering.

## Effort and costs

- WASM port of gdul + gd02 (shared with webOS's web route): 2 to 3 months.
- Tizen packaging, remote input, and a certificate flow: 1 to 2 weeks.
- Seller account: free. Review risk: high. Tizen Studio is heavy (Java, about
  10 GB), so it should not run in Actions. Build locally.

## Steps (only if pursued)

1. Make an Emscripten build of the launcher list (no games), to learn the cost of SDL3/WebGL2.
2. Make a gd02 "song from an extracted folder" WASM spike in desktop Chrome, then on a 2020+ TV in Developer Mode.
3. Only then think about storage and store submission.

## Sources

- WebAssembly on Samsung TVs: https://developer.samsung.com/smarttv/develop/extension-libraries/webassembly/overview.html
- NaCl end of support: https://developer.samsung.com/tv/develop/extension-libraries/nacl/overview
- Gamepad guide: https://developer.samsung.com/tv/develop/guides/user-interaction/gamepad
- Developer Mode / certificates: https://developer.samsung.com/smarttv/develop/getting-started/using-sdk/tv-device.html
- SDL Emscripten: https://wiki.libsdl.org/SDL3/README-emscripten
