# gd-game-04: decisions taken without the owner

Each: what was chosen, why, how to undo it.

**D1. Working branch and checkout.** Work in the main checkout on
`feature/20261028-gd-games-03-04` (owner's override of the kickoff's worktree
plan; two other agents share the checkout). Only `gd-game-04/`, the
gd-game-04 backlog section, `build_module_04` in the launcher's `build.sh`
and `games.yaml` are touched. Undo: none needed.

**D2. C++20, not C++17.** The kickoff says C++17, but gd-game-02 (the model,
whose code we copy) is C++20 (`set(CMAKE_CXX_STANDARD 20)`, designated
initialisers, `= default` comparisons, `std::u8string`). The guidelines say
"match gd-game-02's style". Undo: set 17 in `engine/CMakeLists.txt` and
replace the C++20 constructs.

**D3. Copied from gd-game-02 (engine-independent layers only).** Platform
(paths, icon, disc picker, module entry), XDVDFS + RandomFile (+ a `list()`
walk), raw inflate (moved to `data/`), JSON, dev aids (scripted keys,
screenshots), input (controllers, bindings, touch), audio device / mixer /
XMA decoder / DSP helpers, the GL device, texture codec (360 untiling, DXT),
2D canvas, shader test, build scripts, Android project, launcher module
entry. Removed from the copies: GH2's material shaders, post FX and
spotlights (the renderer), the `.mogg` / Vorbis path, the WMV player, the
Milo / DTA layers. Undo: delete the files.

**D4. Input layouts are gd-game-02's for now.** GH3's own controller tables
(QB) replace them once read (M3 / M8). Labelled [I] in
`game/controller_mode.h`; backlog row. Undo: n/a.

**D5. FFmpeg build: Bink instead of ASF/WMV.** GH3's movies are Bink 1
(`BIKi`), so the minimal LGPL build enables the `bink` demuxer and the
`bink`, `binkaudio_rdft`, `binkaudio_dct` decoders, plus `xma1` / `xma2` as
before. Undo: `FFMPEG_CONFIGURE` in `engine/build_common.sh`.

**D6. REA vs Ghidra.** REA cannot open the XEX, the Ghidra project or raw
PowerPC (agents/CONTEXT.md); every XEX finding comes from Ghidra headless
with XEXLoaderWV. Undo: if a later REA release supports PowerPC / XEX,
re-check findings with it.
