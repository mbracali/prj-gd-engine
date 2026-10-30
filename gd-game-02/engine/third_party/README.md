# Third-party code

| Folder | What | Version | Licence |
|---|---|---|---|
| `stb_vorbis/` | Sean Barrett's single-file Ogg Vorbis decoder: decodes the `.mogg` song audio (`src/audio/vorbis_stream.cpp`) | v1.22 | Public domain (Unlicense) or MIT, at your choice (`stb_vorbis/LICENSE.txt`; the same text closes `stb_vorbis.c`) |
| `rtmidi/` | RtMidi (MIDI input: ALSA, WinMM, CoreMIDI), unchanged, copied from gd-game-01; built by its own `CMakeLists.txt`; the Generic MIDI input of the Gamepad menu (`src/input/midi_in.cpp`) | 6.0.0 | MIT (`rtmidi/LICENSE`) |
| (not in the repo) FFmpeg | The videos' decoder (`src/video/wmv_player.cpp`): ASF demuxer, WMV3 and WMA v2 decoders only | 7.1.5 | LGPL 2.1 or later |

## FFmpeg: downloaded and built by the build scripts

Not committed: `build_common.sh` (`build_ffmpeg`) downloads
<https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz> into `build/ffmpeg-src/`,
checks its SHA-256 (`de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f`,
taken after verifying the tarball's `.asc` signature with FFmpeg's release key
`FCF986EA15E6E293A5644F10B4322F04D67658D8`), builds it unmodified as shared
libraries with `--disable-everything` plus only the ASF demuxer and the WMV3 /
WMA v2 decoders (no GPL, version3 or nonfree options; the build stops if
`config.h` does not say "LGPL version 2.1 or later"), and
`ship_ffmpeg_licence` puts the LGPL text and a notice with the exact source and
configure line into every download's `licenses/`. To update: change
`FFMPEG_VERSION` and `FFMPEG_SHA256` in `build_common.sh` (check the new
tarball's signature first) and the version here.

## stb_vorbis: pinned upstream source

- Upstream: <https://github.com/nothings/stb>
- Commit: `1ee679ca2ef753a528db5ba6801e1067b40481b8` (2021-07-11, "update
  version numbers"), the last commit that changed `stb_vorbis.c`. The file is
  byte-identical at upstream `HEAD` (checked 2026-10-25,
  `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`).
- Files, unmodified, from that commit:

| File | Upstream path | SHA-256 |
|---|---|---|
| `stb_vorbis/stb_vorbis.c` | `stb_vorbis.c` | `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636` |
| `stb_vorbis/LICENSE.txt` | `LICENSE` | `bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63` |

To check: `sha256sum stb_vorbis/*`. To update: take `stb_vorbis.c` and
`LICENSE` from a newer commit, and change the commit and both hashes here.

The build compiles `stb_vorbis.c` as C with `STB_VORBIS_NO_STDIO` (the
decoder reads from memory) and with its warnings silenced
(`CMakeLists.txt`). `build.sh` copies the licence into the download's
`licenses/` folder.
