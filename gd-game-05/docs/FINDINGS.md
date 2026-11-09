# Rock Band (Xbox 360): gameplay rules and findings

Every rule with the address in `default.xex` where it was read ([C]) or
marked [I] (inferred; blocks "done" for gameplay, lesson L2). Addresses here
are always Rock Band's.

## The executable [C]
- XEX2, title ID 45410829, image base 0x82000000, entry 0x824F9000.
- Ghidra (XEXLoaderWV + FixXexFunctions): 41,175 functions, 13,449 strings,
  77,980 data references.
