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

## Natives

`gd04 --audittest <disc> --out docs/audit.md` lists every name the scripts
call that is neither a script nor a native yet (most-called first). The
C functions' addresses: `ghidra/analysis/cfuncs.tsv` (`DumpCFuncs.java`).
Each native in `script/natives_*.cpp` names the behaviour it reimplements
and its source; stubs are logged once with `GD04_UILOG=1` (L1).

## Status

`gd04 --scripttest <disc>`: every script tokenizes; `guitar_startup` runs to
its end against the natives there are (the rest logged).
