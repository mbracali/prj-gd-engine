# miniaudio (vendored)

- **Version:** 0.11.25 (released 2026-03-03), `miniaudio.h` from the
  [0.11.25 tag](https://github.com/mackron/miniaudio/tree/0.11.25), unmodified.
- **SHA-256 of `miniaudio.h`:** `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89`
- **Licence:** public domain (Unlicense) or MIT No Attribution, your choice:
  [LICENSE](LICENSE). Shipped in the packages as `licenses/miniaudio-LICENSE.txt`.
- **Used by:** `gd-engine-core/audio` (the `miniaudio` backend). The
  implementation is compiled once in `audio/src/miniaudio_impl.c`, with the
  options in `audio/CMakeLists.txt`.

To update: replace `miniaudio.h` and `LICENSE` with a newer release's (at
least two weeks old), update this file, rebuild, run `gdcore_audio_selftest`.
