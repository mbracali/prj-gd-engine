# iOS / iPadOS app

Status: study, 2026-10-28.

## Verdict

**Technically feasible. gdul with gd01 and gd02 statically linked is about
4 to 7 weeks of work, mostly reusing the Android port (GLES 3.0, touch, SAF
→ document picker). Distribution is the hard part.** App Store approval is
uncertain, mostly because of IP and trademarks rather than the emulator
rule. **TestFlight plus the EU marketplaces (AltStore PAL) plus sideloading**
are the realistic channels. It needs the Apple Developer Program (US$99/yr,
which also covers the macOS signing in `study_macos_trust.md`).

Recommended route: first ship **GLES 3.0 through ANGLE's Metal backend** (or
Apple's own deprecated GLES) to get a running build quickly. Move to Metal
through the backend work in `study_backends.md` later.

## Technical facts

| Topic | Fact | Impact on us |
|---|---|---|
| SDL3 | Fully supports iOS/iPadOS/tvOS. Built as `SDL3.xcframework`. `SDL_main.h` handles the UIKit app delegate. Touch, Game Controller framework pads (Xbox, DualSense, MFi), and audio sessions all work. | The launcher's SDL code carries over. Android's touch mode is reused. |
| OpenGL ES | **Deprecated since iOS 12**, frozen at ES 3.0, still shipping in 2026 but with no fixes, and removable at any time. SDL3 still creates GLES contexts on iOS. | Our GLES3 path (Android) runs. Treat it as a stopgap. |
| ANGLE | Google's GLES 3.0 implementation on **Metal** (used by Safari/WebKit and Chrome). It drops in as a library, keeping our GLES code. | The safest short path. It avoids Apple's deprecated driver. Some extra CPU overhead. |
| Metal | Apple's API. Reached with SDL_GPU, MoltenVK/KosmicKrisp (Vulkan), or a native backend. | The long-term answer (`study_backends.md`) |
| JIT | Not needed: we are native ahead-of-time code, unlike emulators that recompile. | None of the JIT problems emulators hit (PPSSPP/Dolphin) |
| Code loading | **App Store 2.5.2: no downloading or executing code** that changes features. Frameworks bundled and signed inside the app *can* be `dlopen`ed, but `dlclose` does not reliably unload them on iOS. | **Link the games statically** into gdul. Use a table of `gd_game_run` entries instead of `.so` files. **Blocker:** the launcher relies on unloading a module to reset its globals (`gd-engine-launcher/README.md`, "module unloaded"). Statically linked games must become re-entrant, with an explicit reset of their globals, or run once per app launch. |
| Disc files | `UIDocumentPickerViewController` with security-scoped bookmarks to remember the ISO. Or the Files app's "On My iPhone/gdul" folder (`UIFileSupportsFileSharing` + `LSSupportsOpeningDocumentsInPlace`). Opening in place avoids copying 2 to 7 GB. iCloud Drive files must be downloaded first. | Reuse the Android SAF flow's design: pick once, keep a bookmark, re-resolve at start |
| Memory | Jetsam kills apps over a per-device limit (about 50 to 70% of RAM). The `com.apple.developer.kernel.increased-memory-limit` entitlement raises it. | gd02 needs it on 4 to 6 GB devices. Stream rather than keep whole archives in memory. |
| Guitars | Xbox 360 guitars (XInput) are not seen. Some Bluetooth/MIDI guitars may appear through the Game Controller framework. | Touch and gamepad play, like Android |
| Display | ProMotion at 120 Hz (`CADisableMinimumFrameDurationOnPhone`), safe areas, notch | Pacing code (gd02's L7) and safe-area insets in the menus |

## Distribution options

| Channel | Who can install | Review | Notes |
|---|---|---|---|
| **App Store** | Everyone | Full App Review | Guideline **4.7** (2024 changes) explicitly allows retro console and PC **emulators** and lets them offer game downloads. We download nothing, because users bring their disc, as Delta/PPSSPP/RetroArch do with "import your own". The real risks are **5.2 (IP)**: "Guitar Hero" names, any trademarked art in screenshots, songs; and **4.2/4.3** (minimum functionality: the app shows nothing without a disc, so reviewers need a demo path, for example a built-in test song we own). Likely outcome: back-and-forth, and possibly a rejection. |
| **TestFlight** | Up to 10,000 external testers by link, builds valid for 90 days | Light "beta review" for external testers | The best way to start. Still needs the paid program. |
| **EU alternative marketplaces** (DMA) | EU users | Apple **notarization** (baseline checks, not full review) | AltStore PAL hosts Delta and others. Fees under Apple's EU terms (Core Technology Fee/Commission) change often: check the current terms. A free app is normally exempt or nearly so. |
| **Sideloading** | Anyone with a Mac/PC | None | AltStore Classic/SideStore with a **free** Apple ID (7-day re-sign, 3 apps), or a paid account (1 year). The fallback for non-EU users. |

## Effort and costs

- Xcode project or CMake `-G Xcode` with the iOS toolchain. Static linking
  of gdul + gd01 + gd02 + SDL3 + FFmpeg (LGPL: static linking needs care,
  either relinkable object files or dynamic FFmpeg frameworks, which are
  allowed because they ship inside the bundle): 1 to 2 weeks.
- Game re-entrancy (reset the globals without unloading the module) for gd01
  and gd02: 1 to 2 weeks. This also helps the Vita/3DS-style static builds.
- Document picker, bookmarks, Files app folder, app sandbox paths: about 1 week.
- GLES via ANGLE, touch/pad, safe areas, ProMotion pacing, memory
  entitlement: 1 to 2 weeks.
- Store assets, privacy manifest (`PrivacyInfo.xcprivacy`), review rounds: 1 week+.
- **Money**: Apple Developer Program **US$99/yr**. A Mac is required (the
  owner has an M4 Pro). CI: macOS runners cost 10x minutes, so build iOS
  locally or only on release tags.

## Steps

1. Join the Developer Program (shared with macOS notarization).
2. Make gd01/gd02 re-entrant as static libraries (no reliance on `dlclose`).
3. Build gdul for iOS with ANGLE (GLES 3.0 on Metal). Get the ISO picker
   working and test both games on an iPad.
4. Run a TestFlight external beta.
5. Submit to the App Store with neutral naming and screenshots (no
   trademarks, our own test song). In parallel, list on AltStore PAL (EU).
6. Later, switch to the Metal backend from `study_backends.md`.

## Sources

- App Review Guidelines (2.5.2, 4.7, 5.2): https://developer.apple.com/app-store/review/guidelines/
- 2024 emulator rule changes: https://www.macrumors.com/2024/08/01/app-store-review-guidelines-pc-emulator-apps/ , https://www.macworld.com/article/2290431/apple-now-allows-retro-game-emulators-in-the-app-store.html
- AltStore PAL (EU marketplace, fees): https://www.macrumors.com/2024/04/17/altstore-eu-alternative-app-marketplace/ , https://alternativeto.net/news/2024/8/altstore-pal-drops-1-50-fee-for-eu-users-thanks-to-epic-games-megagrant/
- SDL3 on iOS: https://wiki.libsdl.org/SDL3/README-ios
- OpenGL ES deprecation: https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/OpenGLES_ProgrammingGuide/AdoptingOpenGLES3/AdoptingOpenGLES3.html
- ANGLE (Metal backend): https://chromium.googlesource.com/angle/angle
- Increased memory limit entitlement: https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.increased-memory-limit
- Document picker / security-scoped URLs: https://developer.apple.com/documentation/uikit/uidocumentpickerviewcontroller
- TestFlight: https://developer.apple.com/testflight/
- Apple Developer Program fee: https://developer.apple.com/programs/

## Backlog candidates

- **(GDUL + GD01 + GD02, Enhancement) Re-entrant static game modules**: an
  explicit global reset, so a game can run twice in one process without
  `dlclose`. 1 to 2 weeks. It is needed for iOS (and for any static
  console build).
- **(GDUL, Enhancement) iOS/iPadOS build with ANGLE (GLES 3.0 on Metal)**:
  Xcode/CMake target, static games, document picker + bookmarks, touch and
  pads, safe areas. 3 to 4 weeks.
- **(GDREL, Enhancement) iOS distribution**: Developer Program, TestFlight
  beta, then an App Store submission and AltStore PAL listing. 1 week plus
  review time, and US$99/yr (shared with macOS notarization).
- **(GDUL, Enhancement) Built-in demo song (our own content)** so the app is
  usable without a disc: it is needed for store review on iOS and Google
  Play. 3 to 5 days.
