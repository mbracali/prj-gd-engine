# gd-engine-core/audio

`gd::core::audio::Output` (`include/gd/core/audio/output.h`) and its
backends: `miniaudio`, `openal` (OpenAL Soft, loaded at run time) and
`faudio` (FAudio). CMake target `gdcore_audio`; self-test
`gdcore_audio_selftest`.

The design, the pinned versions, how each backend behaves headless, the
packaging of OpenAL Soft and how to add a backend:
[../docs/audio.md](../docs/audio.md).
