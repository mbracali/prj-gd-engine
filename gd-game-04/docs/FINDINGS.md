# Guitar Hero III (Xbox 360): findings

Gameplay rules and engine behaviour as read in the executable and the data,
each with **[C]** (confirmed, with the address or data) or **[I]**
(inferred). Formats: `formats.md`. Tooling: `../agents/CONTEXT.md`.

## The executable

- XEX2, title ID 415607F7, media ID 43048167, base 0x82000000, entry
  0x82136AC8 [C: XEX optional headers 0x00040006, 0x00010201, 0x00010100].
- Ghidra (XEXLoaderWV): 32,998 functions after `FixXexFunctions.java`.
- Paths the code builds: `%s%s.pak.%s`, `%s%s/%s%s.pak.%s` (0x82182310),
  `pak/qb.pak`, `pak/dbgq.pak` (0x820D2200), `P\Compressed\compress.toc.xen`
  [C: strings and their referencing functions].
- 0x82188C50 rewrites `.pak` / `.pab` paths under `cutscenes`, `missions`,
  `pak`, `zones` (lower-cases the path, moves the file name after the
  folder) [C].

## Gameplay

Nothing yet (M2).
