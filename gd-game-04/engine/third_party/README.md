# Third-party code

| Folder | What | Version | Licence |
|---|---|---|---|
| (not in the repo) FFmpeg | GH3's Bink videos and XMA audio: the Bink demuxer, the Bink video / audio decoders and the XMA1 / XMA2 decoders only | 7.1.5 | LGPL 2.1 or later |

## FFmpeg: downloaded and built by the build scripts

Not committed: `build_common.sh` (`build_ffmpeg`) downloads
<https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz> into `build/ffmpeg-src/`,
checks its SHA-256 (`de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f`,
taken after verifying the tarball's `.asc` signature with FFmpeg's release key
`FCF986EA15E6E293A5644F10B4322F04D67658D8`; the same pin as gd-game-02), builds
it unmodified as shared libraries with `--disable-everything` plus only the
decoders above (no GPL, version3 or nonfree options; the build stops if
`config.h` does not say "LGPL version 2.1 or later"), and `ship_ffmpeg_licence`
puts the LGPL text and a notice with the exact source and configure line into
every download's `licenses/`.
