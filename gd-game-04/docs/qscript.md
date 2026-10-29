# QScript in gd-game-04: the bytecode and the VM

GH3's game flow, menus, HUD wiring and much tuning are compiled QScript in
QB files (`formats.md` "QB"). Following lesson L1 we run those scripts with
our own VM (`engine/src/script/`) and reimplement the C functions they call
("natives"). **[C]** = confirmed in the data or the XEX, **[I]** = inferred.

## Bytecode

`tools/qbscript.py` (and `script/bytecode.cpp`) tokenizes all 16,552 scripts
on the disc with every byte accounted for [C data]; the opcode table is in
`formats.md` "QScript bytecode". Jump operands [C data, every jump on the
disc lands on a token]:

| Token | Operand | Target |
|---|---|---|
| fast-if `47` | u16 | operand position + offset: just past the matching `else` / `endif`, or onto the next `elseif` |
| fast-else `48` | u16 | just past the `endif` |
| elseif `27` | u16, u16 | first: the next `elseif` / else body; second (operand + 2): past the `endif` |
| short jump `49` | u16 | the next `case` (after a `case`), or past `endswitch` (ending a case body) |

`switch <expr>` is followed by `case <short jump> <value>` blocks and
optionally `default`.

## Semantics the VM implements

- A line is a call (`Name params...`), an assignment to a local (`<arg> x =
  v` or `x = v` at the start of a line), control flow, or a struct alone (the
  script's default parameters, kept where the caller gave none [I]).
- Parameters: `name = value`, bare names (flags: unnamed keys), `<...>`
  (all the caller's parameters), inline structs (merged; `$global` items in
  them resolve at call time [I]), positional values.
- Values: literals; a bare name is a key; `<arg> x` a local; `$x` a global
  (`$<arg> x`: the global named by a local's key); `.field`, `[index]`;
  `( expr )` with `* /` over `+ -` over comparisons over `and` / `or`;
  inside parentheses `=` compares [C data: `( <arg> player = 1 )`].
- An `if` condition is a call (`if not GuitarInputLogicPerFrame player =
  <arg> player`) or an expression. Scripts called in a condition run to
  their end at once [I].
- `begin ... repeat [n]` (a non-number count ends the loop [I]), `break`,
  `return x = v` (merged into the caller's parameters).
- Spawned scripts run once per game frame (60 Hz) until they end; `Wait n
  [seconds | gameframes]` suspends them.
- `random_range pair:(a, b)` (0x30, 0x38 the same with the other generator):
  whole-number ends give an integer in [min, max], else a float in [min,
  max) [C 0x82190940] (glow_menu_element's `Wait random_range ...`).
- `<arg> pos [ 0 ]` indexes a pair or vector [I: TileSpriteLoop's tiles];
  `. ( expr )` reads the field a key expression names [C data:
  `$ store_camera_props . ( $ target_store_camera_prop )`].
- A called script's unnamed struct parameters give their fields, later ones
  replacing earlier [C data: ui_flow_manager_respond_to_action's
  `<create_func> player = .. <create_params> <state_create_params>`, the
  pause menu's `for_options`].
- An event handler runs with its element as the object (a plain `gettags`
  in options_menu_hilight is that element's); it outlives the element (a
  pad_choose that destroys its own menu goes on) [I].
- `random(...)`: each branch offset counts from just after its own 4 bytes
  in the table (not from the table's end) [C data: every random on the disc
  then lands on a token start; `random:((1,1,1),(8,24,40))` branches are 20
  bytes apart]. Before, most randoms took the first branch (or none).
- Inside parentheses an expression goes on over line ends (`( $ game_mode
  = p2_career or` / `$ game_mode = p2_coop )`); a default-parameter struct
  may be written out over lines (`{` ... `}`, add_text_to_column) [C data].
- A key naming a global (a FormatText checksumName such as
  'left_column_x'): in arithmetic it is the global's number / pair; `[ i ]`
  indexes the global array; `( <arg> key )` alone is the global array /
  pair; FormatText `%d` prints the global number [I: the detailed stats'
  columns rely on all four].
- FormatText `usecommas` (thousands separators) [I: the name; scores,
  cash]; a float prints as `%.<decimalplaces>f`, 3 places by default [C
  0x82264F88 `li r26,3`, 0x822649F0 `"%%.%df"`] (the detailed stats' "1.478 X").
- A struct field that points at a global (`marker =
  <slowride_markers_text_e5fa3adf>`) reads that global's string [I:
  get_section_stats' section names].

## Natives

`gd04 --audittest <disc> --out docs/audit.md` lists every name the scripts
call that is neither a script nor a native yet (most-called first). The
C functions' addresses: `ghidra/analysis/cfuncs.tsv` (`DumpCFuncs.java`).
Each native in `script/natives_*.cpp` names the behaviour it reimplements
and its source; stubs are logged once with `GD04_UILOG=1` (L1).

## Status

`gd04 --scripttest <disc>`: every script tokenizes; `guitar_startup` runs to
its end against the natives there are (the rest logged).
