# GH2 (Xbox 360) `.dtb` script format

`.dtb` files are compiled `.dta` scripts: a tree of arrays holding numbers,
symbols, strings, nested arrays and preprocessor directives. The ark has 177
of them (config, UI, locale, venues, character/world object definitions).
`tools/dtb.py` decodes them back to `.dta` text; all 177 parse with every byte
consumed and nothing left over. Decoded copies: `extracted/dta/`.

## Layers

1. **Encryption.** The first 4 bytes are a little-endian seed, stored in
   clear. Every later byte is XORed with the low byte of a Park-Miller
   generator (`key = 16807 * key mod (2^31 - 1)`, advanced before each byte).
   This is the same cipher as `gen/main.hdr`; `tools/ark.py` `crypt()`
   implements it. The output is 4 bytes shorter than the file.
2. **Plaintext.** One `u8` that is always `0x01` ("an array follows"), then
   the root array, then end of data. All values are little-endian.

## Array

```
u16 count   number of nodes
u16 line    source line of the opening bracket in the original .dta
u16 id      always 0 in all 38,348 arrays in the game
count x node
```

The root array holds the file's top-level nodes (it has no brackets in the
text). Line numbers go up to 3980, so 16 bits are enough. 350 arrays are
empty (`count` = 0, e.g. `()` or `{}`).

## Node

```
u32 type
value (layout depends on type)
```

| Type | Name | Value | Text form | Count in the game |
|---|---|---|---|---|
| `0x00` | int | `s32` | `42` | 7,328 |
| `0x01` | float | `f32` | `-3.5` | 3,903 |
| `0x02` | variable | `u32 len` + bytes | `$name` | 3,579 |
| `0x05` | symbol | `u32 len` + bytes | `name`, or `'odd name'` | 50,859 |
| `0x06` | "unhandled" marker | `u32` (always 0) | `kDataUnhandled` | 122 |
| `0x07` | `#ifdef` | `u32 len` + macro name | `#ifdef HX_XBOX` | 37 |
| `0x08` | `#else` | `u32` (always 0) | `#else` | 17 |
| `0x09` | `#endif` | `u32` (always 0) | `#endif` | 42 |
| `0x10` | array | nested array | `( ... )` | 27,976 |
| `0x11` | command | nested array | `{ ... }` | 9,099 |
| `0x12` | string | `u32 len` + bytes | `"text"` | 12,604 |
| `0x13` | property | nested array | `[ ... ]` | 1,096 |
| `0x20` | `#define` | `u32 len` + macro name | `#define NAME` | 984 |
| `0x21` | `#include` | `u32 len` + file name | `#include macros.dta` | 165 |
| `0x22` | `#merge` | `u32 len` + file name | `#merge default.dta` | 61 |
| `0x23` | `#ifndef` | `u32 len` + macro name | `#ifndef _SHIP` | 5 |

Not seen in GH2's files but handled by `dtb.py` in case they appear: `0x24`
`#autorun` and `0x25` `#undef` (both taking a name). Types `0x03` and `0x04`
never occur. An unknown type is an error.

Details:

- Text lengths are byte counts with no terminating NUL. Strings are Latin-1
  (accented letters in the French, German, Italian and Spanish locales, plus
  ©/®). `dtb.py` writes the `.dta` output as UTF-8. In the text form a `"`
  inside a string is written `\q`, the Harmonix escape.
- `#define NAME` has no value of its own: its value is the **next sibling
  node** (usually an array, e.g. `#define kTempoFast (2)`). `dtb.py` prints the
  two on one line.
- Directives are **kept in the binary, not expanded at compile time**. Both
  branches of an `#ifdef HX_EE` / `#ifdef HX_XBOX` stay in the file (so the
  360 data still has PS2 branches), and `#include` / `#merge` are stored as
  file names. The game's loader must evaluate them when it reads the file
  (inference: the 360 build defines `HX_XBOX`, and a retail build `_SHIP`).
- Directives can be anywhere in an array, not only at the top level
  (e.g. inside a `store.dtb` video entry, or around test songs in
  `songs.dtb`).
- `kDataUnhandled` is used as the return value of a handler script (venue
  `.dtb` files: `(chorus {...} kDataUnhandled)`), so that the message keeps
  going to other handlers.

## Example (start of `config/gen/songs.dtb`, decrypted)

```
01                   root follows
5d 00 01 00 00 00    root: 93 nodes, line 1, id 0
10 00 00 00          node: array
0b 00 0b 00 00 00    11 nodes, line 11, id 0
05 00 00 00          node: symbol
0d 00 00 00 "badreputation"
10 00 00 00          node: array
02 00 0c 00 00 00    2 nodes, line 12, id 0
05 00 00 00 04 00 00 00 "name"
12 00 00 00 0e 00 00 00 "Bad Reputation"
...
```

The "extra u32 per array" that this byte stream seems to show is really the
`u16 line` + `u16 id` pair followed by the next node's `u32 type`. Reading the
array header as `u16, u16, u16` (6 bytes) is the only reading that makes all
177 files end exactly.

## Differences from GH1 (PS2)

- **Only the encryption is different.** GH1 uses a 256-word table seeded with
  the C `rand()` LCG and an XOR-feedback generator (see
  `gd-game-01/tools/dtb.py`). GH2 uses the Park-Miller generator above, the
  same as its archive header.
- The plaintext layout is the same: the `0x01` byte, `u16 count / u16 line /
  u16 id` array headers, and the same node type numbers. GH1's parser, given
  GH2's cipher, reads all 177 GH2 files exactly.
- GH2 files carry `#ifdef HX_XBOX` / `HX_EE` / `HX_PC` / `HX_PS3` branches and
  references to shared engine scripts under `../../../system/run/config/`
  (also in the ark as `system/run/.../*.dtb`).
