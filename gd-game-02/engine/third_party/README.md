# Third-party code

| Folder | What | Version | Licence |
|---|---|---|---|
| `stb_vorbis/` | Sean Barrett's single-file Ogg Vorbis decoder: decodes the `.mogg` song audio (`src/audio/vorbis_stream.cpp`) | v1.22 | Public domain (Unlicense) or MIT, at your choice (`stb_vorbis/LICENSE.txt`; the same text closes `stb_vorbis.c`) |

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
