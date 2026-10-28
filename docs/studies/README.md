# Viability studies

These are short studies of platforms and distribution ideas. Each one gives a
verdict, effort, costs, blockers, steps and sources. Written 2026-10-28. Prices
and policies change, so check the linked sources before acting.

| Study | Verdict (short) |
|---|---|
| [steam-deck.md](steam-deck.md) | No separate build. Make the Linux launcher Deck-aware (Steam virtual pads, handheld defaults). Flathub later. Not the Steam store. |
| [nintendo-3ds.md](nintendo-3ds.md) | Not viable for gdul/gd02. A reduced gd01 homebrew would take months. Not recommended. |
| [lg-webos-tv.md](lg-webos-tv.md) | Not now. A native homebrew (Developer Mode, SDL2) spike is possible. The store/web route is impractical. |
| [samsung-tizen-tv.md](samsung-tizen-tv.md) | Not viable in practice: web/WASM only, the 7 GB disc can't be reached, high store risk. |
| [macos-trust.md](macos-trust.md) | Do it: US$99/yr, Developer ID + hardened runtime + notarize + staple. No library-validation opt-out needed. |
| [windows-trust.md](windows-trust.md) | Keep unsigned for now. SignPath is closed to us (non-OSI licence), and Artifact Signing is US/CA-only for individuals. OV cloud certificate later. |
| [google-play.md](google-play.md) | Technically close (API 36, 16 KB, SAF). Needs an AAB, Play App Signing and 12 testers for 14 days. The main risk is IP. Register for developer verification anyway. |
| [hdr.md](hdr.md) | Possible as an opt-in: FP16 pipeline plus GL→Vulkan HDR presenter. 3 to 5 weeks. |
