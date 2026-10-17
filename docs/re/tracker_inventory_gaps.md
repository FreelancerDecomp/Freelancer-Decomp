# Tracker inventory gaps: freelancer.exe `.text`

Part of `freelancer.exe`'s `.text` is not covered by any decomp-tracker item. This is not an ingest
error: in the `EXE/Disasm/freelancer.exe.txt` listing these bytes are marked `??` (Ghidra did not create
a function), although a disassembler working from the PE decodes them as ordinary x86. `refresh_module` on the same
export will add nothing. The fix is "Create Function" in Ghidra, then re-export
`.txt` and `.c` and run `refresh_module`.

Almost all uncovered blocks are alignment `INT3`/`NOP` padding between functions. Blocks larger than 32 bytes are an
addition to the port completeness criterion: "everything in the tracker, plus these blocks". They cannot be inserted through the tracker API
without a new Ghidra export, so they are listed here. The porting status
of these blocks is kept in the `ported_*` fields of neighbouring tracker functions and in `deviations.md`, not here.

Measurement (a local script, not published): (the PE `.text` range minus the union of tracker item intervals, then classification of blocks
>= 32 bytes by Capstone, vtable xrefs from `.rdata` and neighbour notes). Re-run after every
new Ghidra listing export; put the numbers in the wave report, not in this file.

## Blocks >= 32 bytes (at the time of writing)

| Address | What it is |
|---|---|
| `0x00412e41` (body `0x00412e50`) | FontString paint/draw (vtable `0x5c8cdc`) |
| `0x00574380` (`EnterState` `0x005743a0`) | menu EnterState, a 19-case switch, table `0x575360` |
| `0x00573eae` (body `0x00573eb0`) | menu state Update/EnterState driver (vtable `0x5e306c`) |
| `0x0059a69e` (body `0x0059a6a0`) | Win3D / WinControl draw prologue (vtable `0x5e5ce8`) |
| `0x005b8220` | not code: remainder of the PE Debug Directory / CodeView (NB10 + path to `FreeLancer.pdb`) |
| `0x005a1cb8` (body `0x005a1cc0`) | NeuroNet control style application (`FUN_004177b0` + id/scale) |
| `0x005753ac` (`0x5753b0`, `0x5753d0`) | caption flags and IDS caption + style |
| `0x00534ed3` | mid-function fragment (Ghidra did not determine the parent's boundary), `vtable+0x150`, masks `0x103`/`0x803` |
| `0x0051f773` (body `0x0051f780`) | view mapper `[ecx+8]+0x1c` -> 0/1/2 (tradelane, vtable `0x5dc368`) |
| `0x00556ba9` (body `0x00556bb0`) | `rep movsd` x12 into `dst+0x5c` (hotspot, vtable `0x5e12f8`) |
| `0x004c715f` (body `0x004c7160`) | callback trampoline `CALL [this+0x18](arg)` in the sensitivity settings (vtable `0x5d6d54`) |
