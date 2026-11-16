# Study: Arson, an independent DTA implementation (vs our script engines in gd01, gd02, gd03, gd05)

Study, 2026-11-13 (backlog GDALL-052). Read-only: the repository was cloned shallowly, its Rust source and tests
read, and deleted; nothing was built, run or imported. Every section ends with an empty **Owner's take:** line
for you.

## Executive summary

| Project | Who | Licence | What it is | Language | Status | We take | Make it ours (1-10) |
|---|---|---|---|---|---|---|---|
| hmxmilohax/arson | MiloHax (Nathan and others), 2024- | **LGPL-3.0-or-later**; the `examples/` folder MIT or Apache-2.0 | A **from-scratch implementation of Harmonix's DTA language**: a text lexer / parser that matches the games' own tokenizer, a DTB reader / writer (Milo and Forge layouts, both encryptions), an evaluator with ~170 builtins, a formatter, a compiler (`arsonc`), a REPL | Rust (1.86), ~24,000 lines | v0.3.0, last commit 2026-07 | **Knowledge**: lexer edge cases, preprocessor rules, DTB layout details; a checklist for our text parser and our builtins | 7 (different language; LGPL forbids copying into our tree) |

**The key finding.** Arson is clean, well-tested and independent (not decompiled), so it is a safe and useful
reference, but **only as facts**: LGPL code cannot be copied into a PolyForm Noncommercial project without
putting those files under LGPL, and it is Rust while we are C++. Compared with our engines:

- **Binary side (DTB, preprocessor): we agree.** The node type numbers, `#ifdef` / `#ifndef` / `#else` / `#endif`,
  `#define` / `#undef`, `#include`, `#autorun`, and **`#merge` (recursive; keys already present win)** behave the
  same as our `ScriptLoader::preprocess` / `mergeArrays` in gd02 / gd03 / gd05.
- **Text side: ours is simpler than the games'.** Our `parseDtaText` (used for text `.dta` such as song lists)
  has no hex integers, no `/* */` block comments (with the original's odd rules), no `#directives`, treats `;`
  inside a word as a comment start (the games read `a;symbol` as one symbol), and rejects floats such as `.`,
  `+.`, `-.E1` (the games read 0). This matters once we read **text DTA we did not ship**: custom / DLC
  `songs.dta` for GDALL-042.
- **Builtins: no single truth here.** Arson implements a superset (string, math, file, bit helpers) designed by
  its authors; it is not proof of what a given game has. One difference to check: Arson's `/` on two integers is
  **integer division**; ours (gd02 / gd03 / gd05) returns an integer only when the division is exact and a float
  otherwise, and gd01 always a float. Neither is marked [C] in our code: the games' rule must be read in our XEX.
- **`mod` is wrong in gd02, gd03 and gd05 (a likely live bug).** Arson and our gd02 / gd03 / gd05 keep the
  dividend's sign (`{mod -1 5}` = -1). gd01 learned today (GD01-319, #100) that Harmonix's `mod` wraps into
  `[0, n)`: the store's `{mod {+ itemIdx -1} n}` gave -1 and showed `_shop_desc` / $0 going up from the first item.
  **The same store line is in GH2's `ui/career.dta` and Encore's `ui/career.dta`**, and the tutorials step back
  with `{mod {+ $old $inc} ...}` in GH1 / GH2 / Encore and `{mod {- $old $inc} ...}` in RB1. So the #100 bug very
  likely exists in gd02 and gd03 (store, Up on the first item) and in the tutorials' "previous" in gd02 / gd03 / gd05.

Owner's take:

## Licensing

| Part | Licence | Copy into our repo? | Use |
|---|---|---|---|
| `crates/`, `tools/` | LGPL-3.0-or-later | **No** (would bring LGPL obligations to those files; our licence is PolyForm NC) | Read for facts and test ideas |
| `crates/arson-parse/tests/test_files/*.dta` | LGPL-3.0-or-later (SPDX header) | **No** | Write our own cases from the behaviours they show |
| `examples/` | MIT or Apache-2.0 | Possible with the notice, but nothing there we need | - |
| Running `arsonc` as a separate developer tool (never linked, never shipped) | LGPL allows use | Allowed, optional | Only if you want a second DTB writer to cross-check ours |

Owner's take:

## 1. What it is and how it works

| Crate / tool | Does |
|---|---|
| `arson-parse` | Lexer whose regexes follow the original Flex tokenizer "as closely as possible", parser to an AST with diagnostics; directives `#define #undef #include #include_opt #merge #autorun #ifdef #ifndef #else #endif` |
| `arson-dtb` | DTB read / write: node kinds 0-9, 16-20, 32-37; array header **Milo** (i16 size, i16 line, i16 "deprecated" id) and **Forge** (i32 id, i32 size, i16 line); old (pre-GH2) and new (GH2 on) encryption; tolerates arrays that claim more nodes than the file holds when the file ends on a node boundary |
| `arson-core` | Context, symbols, variables, objects (`{obj handler ...}`), the loader (include stack, recursion check, `#include_opt`), builtins: operators, flow (`if`, `if_else`, `unless`, `while`, `foreach`, `foreach_int`, `do`, `with`), arrays (`elem`, `find`, `insert_elems`, `sort`, `sort_by` ...), strings (`search_replace`, `substr`, `strcat`, `tolower` ...), numbers |
| `arson-stdlib` | Math, random, file system |
| `arson-fmtlib`, `arson-fmt` | A formatter for `.dta` text |
| `arsonc`, `arson-run`, `arson-repl` | DTA -> DTB compiler, script runner, REPL |

Owner's take:

## 2. Links

| What | Link |
|---|---|
| Repository | https://github.com/hmxmilohax/arson |
| Script corpus from the same group | https://github.com/hmxmilohax/milo-script-library (`study_milo_script_library.md`) |

Owner's take:

## 3. Side by side with our engines

| Topic | Arson | Ours | Action |
|---|---|---|---|
| DTB node types | 0 int, 1 float, 2 var, 3 func, 4 object, 5 symbol, 6 unhandled, 7-9 ifdef / else / endif, 16 array, 17 command, 18 string, 19 property, 20 glob, 32-37 define / include / merge / ifndef / autorun / undef | Same set (`data/dta.h`); gd01's enum stops at IfNDef | gd01: check its discs never hold 36 / 37 (GH1 has none today) |
| `#merge` | Recursive, existing tags win, only non-empty arrays | Same (`mergeArrays`) | None |
| `#include_opt` | Text-only directive: missing file is not an error | Not supported (we only read DTB, where it is resolved) | Needed only if we ever preprocess text DTA |
| Array header | Milo i16 x3; Forge i32, i32, i16 | Milo layout (GH1 / GH2 / RB1) | Forge only matters for RB4-era (gd19) |
| Short arrays at EOF | Tolerated on a node boundary | Unknown | Add a test |
| Text lexer | Hex `0x..`, odd floats, `/* */` rules, `;` only at a token start, quoted symbols `'..'`, `$var`, `kDataUnhandled`, directives | No hex, no block comments, `;` anywhere, strict floats, no directives | Bring `parseDtaText` up to the games' rules before GDALL-042 reads foreign / custom `songs.dta` |
| `/` int / int | Integer division | gd02 / 03 / 05: int if exact, else float; gd01: float | Read the games' rule in our XEX, mark [C] |
| `mod` | Remainder with the dividend's sign; float fmod; error on 0 | gd01: wrapped into [0, n) (GD01-319); gd02 / gd03 / gd05: dividend's sign; 0 on 0 | **Port gd01's rule to gd02 / gd03 / gd05** (Arson is not the reference here); check the zero case |
| Integers | 64-bit | 32-bit (as the games) | Ours is right for the games |
| `random_int a b` | `a <= n < b` | Same | None |
| `with`, `sort`, `search_replace`, `\|=`, `&=`, `'~'` | Present | Missing in gd05 (see `study_milo_script_library.md`) | Add to gd05, semantics from RB's XEX |

Owner's take:

## 4. Feature ideas

- A **script console** (developer option): type a DTA expression and run it in the running game (Arson's REPL is
  the idea, our interpreter does the work). Good for debugging gd02 / gd05 menus without rebuilding.
- A **DTA formatter / printer** for our logs: print any `DataNode` back as readable text (we print bits today);
  also the base of the parser check in `study_milo_script_library.md` NEW-3.

Owner's take:

## 5. Things to avoid

- Copying source, tests or test files (LGPL).
- Linking Arson or calling it from our builds (a Rust toolchain and LGPL in every package).
- Adopting Arson's extra builtins (file I/O, `exit`, `panic` ...) in our games: the games do not have them, and
  file access from scripts would be a safety risk with foreign content.

Owner's take:

## 6. Does it explain any open bug?

Yes, one: `mod` (section 3 and NEW-0): the #100 store bug fixed in gd01 today very likely exists in gd02 and gd03,
and the tutorials' "previous lesson" may wrap wrongly in gd02 / gd03 / gd05. Also, the missing gd05 builtins found with the script library (`|=`, `&=`, `'~'` in the characters' arm
flags) are a lead for **GD05-044 (the band members' hands)**.

Owner's take:

## Suggested backlog items

| ID | Game | Class | Description | Effort (agent h) | Value |
|---|---|---|---|---|---|
| NEW-0 | gd02 / gd03 / gd05 | Bugfix | `{mod a n}` wraps into [0, n) as in gd01 (GD01-319); check the store's Up on the first item (GH2 / Encore `career.dta`) and the tutorials' previous lesson; scripttest cases. Priority: high (same bug as #100) | 0.5-1 | High |
| NEW-1 | gd01 / gd02 / gd03 / gd05 | Original | Read the games' `/` and `mod` rules (int / int, by zero) in each XEX / ELF, mark [C], align all four interpreters, scripttest cases | 1-2 | Medium |
| NEW-2 | gd02 / gd05 (shared code) | Enhancement | Text DTA parser up to the games' rules: hex, block comments, `;` only at a token start, odd floats, quoted symbols, directives and `#include_opt`; our own tests written from the behaviours (no copied files). Needed for GDALL-042 foreign / custom song lists | 2-3 | High |
| NEW-3 | gd02 / gd05 | Original | DTB reader: tolerate arrays that claim more nodes than the file holds when it ends on a node boundary; test | 0.5 | Low |
| NEW-4 | gd02 / gd05 | Enhancement | Developer script console (run a DTA expression in the live game) and a `DataNode` text printer for logs | 2-3 | Medium |

Owner's take:
