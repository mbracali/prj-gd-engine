# Samsung Tizen TV

Status: study, 2026-10-28. It merges the first study on this topic
(written earlier the same day).

## Verdict

**Sideloading is possible, but only as a web app (`.wgt`: HTML + WebAssembly +
WebGL2), with a per-TV certificate and Developer Mode. No native route is
open to us.** Native C/C++ Tizen TV apps are for Samsung partners, NaCl ended
with the 2021 models, and there is no practical root/homebrew scene for
2020+ models. So a Samsung build means an **Emscripten port** of gdul + the
games and a solution for the disc (which the web sandbox can't reach). That
is about **2 to 3 months**, shared with any other web build. **Not
recommended now.** Revisit if a WASM build exists for another reason. If
"Samsung TV (Coming soon)" must be kept, it is honestly a long-term item.

## Can we sideload? Yes, but it is a developer flow

1. On the TV: **Apps** → type **1 2 3 4 5** on the remote → Developer Mode
   **On** → enter the PC's IP → restart the TV.
2. On the PC: **Tizen Studio** (+ TV Extensions and Samsung Certificate
   Extension) or Samsung's lighter **VS Code "Tizen TV" / "Tizen TV WASM"
   extension**. Create a **Samsung certificate profile** in Certificate
   Manager: an author certificate plus a **distributor certificate tied to the
   TV's DUID** (signed in with a Samsung account).
3. Connect (`sdb connect <tv-ip>`), then `tizen install -n gdul.wgt -t <device>`
   (or "Run on TV" in the IDE).

Limits:
- Each user needs the same procedure and **their own certificate for their
  TV's DUID**. We can't ship one signed `.wgt` that installs everywhere.
  Community installers (**Jellyfin2Samsung**, **Apps2Samsung**) automate the
  certificate and install. That is the model to copy (an "install gdul on my
  Samsung TV" helper in the launcher's PC build is conceivable).
- Turning off Developer Mode removes sideloaded apps. Certificates expire and
  then need to be regenerated.
- Samsung's current docs say **installing widgets from USB is no longer
  supported**. The install is network-only.

## Restrictions for native apps

- Native and .NET Tizen TV apps are **partner-only**. The public SDK targets
  web apps. NaCl/PNaCl ended with the 2021 models.
- **WebAssembly** is supported from Tizen 5.5 (2020 TVs). Samsung ships a
  WASM toolchain and VS Code extension for it. **Moonlight (moonlight-tizen)**
  runs this way, which proves that a C/C++ game-style client works as a WASM
  `.wgt`.
- 2024+ models (Tizen 8/9) do not change this: still web/WASM. The engine is
  a newer Chromium with WebGL2.

## How a Tizen web build would work

| Topic | Facts | Impact |
|---|---|---|
| Code | Emscripten: SDL3 Emscripten backend (canvas, Web Audio, Gamepad API). GLES3 → WebGL2. **No `dlopen`**: static games (see `study_ios.md`'s re-entrancy item). | The launcher's SDL code mostly carries over. Threads need COOP/COEP (SharedArrayBuffer). Check on the TV. |
| Disc | No File System Access API. USB access through `tizen.filesystem` is not dependable for third-party apps (it was partner-level on older models). wasm32 has 4 GB of address space. | **Blocker for the 7.3 GB GH2 ISO.** Options: (a) a PC-side "asset server" (the gdul desktop build extracts and serves the needed files over LAN), or (b) pre-extracted subsets stored in the app's storage (quota limits). gd01's 2.2 GB ISO is easier, but still not in memory. |
| Decoders | FFmpeg, Bink/XMA → our decoders built for WASM (single-threaded without COOP/COEP, about 5 to 10 MB) | Slow on A53-class CPUs: FMV probably off |
| Input | Remote keys need `tizen.tvinputdevice.registerKey`. Gamepads are supported since 2016 (up to 4, Gamepad API). | HID guitars might appear as gamepads. XInput guitars won't. |
| Audio | Web Audio + TV processing adds latency | A calibration screen is required |
| Performance | Cortex-A53/A55/A73 + Mali, plus WASM/WebGL overhead | gd01 at 1080p is likely fine. gd02 needs 720p with light settings. |

## Store

Samsung Apps TV **Seller Office**: a seller account (partnership approval in
some regions), QA, and a content review. An app that needs external
copyrighted game data named after Activision's IP is a **high rejection
risk**. Gaming Hub is a cloud-streaming portal, not open to independent apps.

## Effort and costs

- WASM port of gdul + one game (shared with a web/webOS-web build): 2 to 3 months.
- Disc solution (LAN asset server from the desktop launcher): 2 to 3 weeks.
- Tizen packaging, remote keys, certificate/installer helper: 1 to 2 weeks.
- Money: free seller account. Tizen Studio is heavy (Java, about 10 GB), so do
  not run it in Actions. The VS Code extension + CLI could run locally only.

## Steps (only if pursued)

1. Make an Emscripten build of the launcher's list (no games) to learn the
   SDL3/WebGL2 cost. Run it in desktop Chrome.
2. Spike: a gd01 song from an extracted folder, served over HTTP, in desktop
   Chrome, then on a 2020+ Samsung TV in Developer Mode.
3. Only then build the LAN asset server, the installer helper, and decide on
   the store.

## Sources

- WebAssembly on Samsung TVs: https://developer.samsung.com/smarttv/develop/extension-libraries/webassembly/overview.html
- NaCl end of support: https://developer.samsung.com/tv/develop/extension-libraries/nacl/overview
- Developer Mode / TV device connection / certificates: https://developer.samsung.com/smarttv/develop/getting-started/using-sdk/tv-device.html
- VS Code Tizen TV extension: https://developer.samsung.com/smarttv/develop/tools/additional-tools/vscode-extension.html , https://github.com/Samsung/vscode-tizentv-wasm
- Gamepad guide: https://developer.samsung.com/tv/develop/guides/user-interaction/gamepad
- Community installers (certificate automation): https://github.com/Jellyfin2Samsung/Samsung-Jellyfin-Installer
- Moonlight for Tizen (WASM game client precedent): https://github.com/brightcraft/moonlight-tizen/wiki/Installation-Guide
- SDL Emscripten: https://wiki.libsdl.org/SDL3/README-emscripten

## Backlog candidates

- **(GDUL, Enhancement, low priority) Emscripten/WebGL2 build of the launcher**:
  list only, desktop Chrome. 1 to 2 weeks. It is the gate for Samsung (and an
  LG web app).
- **(GD01, Enhancement, low priority) WASM spike**: one extracted song over
  HTTP, on a Samsung TV in Developer Mode. 2 to 3 weeks after the above.
- **(GDUL, Enhancement, later) LAN asset server** in the desktop launcher for
  TV/web clients. 2 to 3 weeks.
- The audio-latency calibration and TV remote items are shared with
  `study_lg_tv.md`.
