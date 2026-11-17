# gd-game-24: decisions

Each entry: what, why, how to undo.

## D1. Discovery only until the owner approves the plan (2026-11-17)

What: GD24-001 produced the rip, the decrypted EBOOT, the Ghidra export,
`docs/FORMATS.md` and the plan in `PROGRESS.md`; no engine code yet.
Why: the dev wave rule ("the owner approves the plan first").
Undo: n/a.

## D2. EBOOT decrypted locally with pspdecrypt (2026-11-17)

What: the owner approved decrypting the EBOOT "like we did with the other
games". pspdecrypt was built outside the repository; the decrypted ELF is in
the git-ignored `ghidra/`. The engine will never need it.
Why: the behaviour comes from the executable. Undo: delete `ghidra/EBOOT.prx`.

## D3. Ghidra with kotcrab's ghidra-allegrex extension (2026-11-17)

What: release v21.4 (built for Ghidra 12.1.4) installed into the local
Ghidra's `Extensions/`; NID names from PPSSPP's tables
(`tools/prx_imports.py` + `NameStubs.java`); extra functions from
`FindPspFunctions.java`.
Why: the stock MIPS language lacks the Allegrex / VFPU instructions.
Undo: remove `Ghidra/Extensions/ghidra-allegrex`.

## D4. Proposed (owner to confirm): read the heap images by class layout

What: no relocation; a `HeapImage` reader maps each chunk at its original
address and reads objects through per-class layouts identified by vtable
address, written from the EBOOT, starting with the classes the first boot
needs (song data, gem lists, tempo map).
Why: the STR files are raw RAM with absolute pointers (FORMATS.md). The
alternative, running the PSP code (an emulator / recompiler), is against the
project's clean-room approach.
Undo: the owner prefers another route (OPEN_QUESTIONS Q1).
