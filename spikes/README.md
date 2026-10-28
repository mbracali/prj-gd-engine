# Spikes and studies

Short studies of platforms, distribution ideas and technical options. Each one
gives a verdict, effort, costs, blockers, steps and sources. Prices and
policies change, so check the linked sources before acting. A study found
implementable goes into [../gd-backlog.md](../gd-backlog.md) as an
**Enhancement** (its ID is given below).

File names: `study_<topic>.md`. A `_old` suffix marks an earlier study that a
newer one on the same topic replaces or merges.

| Study | Written | Verdict (short) | Backlog |
|---|---|---|---|
| [study_steam_deck.md](study_steam_deck.md) | 2026-10-28 | Said: no separate build, make the Linux launcher Deck-aware. **Overruled by the owner (wave B): the separate Steam Deck release comes back**, with its own out-of-the-box settings. | GDREL-001, GDUL-007, GDREL-007 (Flathub) |
| [study_hdr.md](study_hdr.md) | 2026-10-28 | Possible as an opt-in: FP16 pipeline plus GL→Vulkan HDR presenter. 3 to 5 weeks. | GDALL-001 |
| [study_macos_trust.md](study_macos_trust.md) | 2026-10-28 | Do it: US$99/yr, Developer ID + hardened runtime + notarize + staple. | GDREL-004 |
| [study_windows_trust.md](study_windows_trust.md) | 2026-10-28 | Keep unsigned for now; OV cloud certificate later. | GDREL-005 |
| [study_google_play.md](study_google_play.md) | 2026-10-28 | Technically close (API 36, 16 KB, SAF). Needs an AAB, Play App Signing and 12 testers for 14 days. Main risk is IP. | GDREL-006 |
| [study_ps_vita.md](study_ps_vita.md) | 2026-10-28 | gd01 only, as homebrew (HENkaku/HENlo + Ensō, VPK on VitaDB, vitaGL + SDL3). 2 to 3 person-months; do it after the backend/device-layer work. | candidates in file |
| [study_3ds.md](study_3ds.md) | 2026-10-28 | Not viable for gdul/gd02; a cut-down gd01 on the New 3DS needs a citro3d renderer written from scratch (3 to 6 months). Not recommended; spike only on request. Merges the first 3DS study. | candidates in file |
| [study_ios.md](study_ios.md) | 2026-10-28 | Feasible (4 to 7 weeks): static games, ANGLE/GLES first, Metal later. App Store risk is IP more than the emulator rule; TestFlight, AltStore PAL and sideloading are the realistic channels. US$99/yr. | candidates in file |
| [study_backends.md](study_backends.md) | 2026-10-28 | Use SDL_GPU (Vulkan / D3D12 / Metal) behind gd02's `RenderDevice`, with GL as fallback. Order: gd02 (Metal first), then launcher ABI v2, then gd01. HDR comes almost free. | candidates in file |
| [study_lg_tv.md](study_lg_tv.md) | 2026-10-28 | Sideloading works (Developer Mode + `ares-install`, or Homebrew Channel). A native SDL build is 2 to 4 weeks; no store route. Merges the first LG study. | candidates in file |
| [study_samsung_tv.md](study_samsung_tv.md) | 2026-10-28 | Sideloading only as a WASM web app, with a per-TV certificate; no native route; the disc is out of the sandbox's reach. 2 to 3 months. Not now. Merges the first Samsung study. | candidates in file |
| [study_shader_ports.md](study_shader_ports.md) | 2026-10-28 | Worth it selectively: GH2 bloom/grade/DOF, GPU flare test and crowd cards fit GH1 (days each); normal/spec/env maps lack data. Proposes a shared `gd-engine-fx` library. | candidates in file |

Each study ends with "Backlog candidates"; the IDs are given in
[../gd-backlog.md](../gd-backlog.md) once the items are added there.
