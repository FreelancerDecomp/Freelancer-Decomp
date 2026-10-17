# luaprofile.dll

`luaprofile.dll` is a DACOM plugin with an embedded Lua 3.2 that registers a single component, `"ILua"`. **In retail it takes no part in the game path**: it is not in `[Libraries]`, no retail module creates `"ILua"`, and INI loading goes through dacom's built-in parser. This document exists so that nobody confuses this module with `IProfileParser` or with Lua in Thorn. The port is `src/luaprofile/` (a separate private Lua VM in `src/luaprofile/lua32/`), the public ABI is `src/include/luaprofile.h` (`IProfileParser`, `ILua`, `FLuaTaggedValue`, `FSectionEnumCallback`, `AcquireProfileParser()`); function addresses and statuses are in decomp-tracker (listing `EXE/Disasm/luaprofile.dll.txt`).

## Role and boundaries

- `DllMain` registers the `"ILua"` factory (priority `0x40000000`) and nothing else; there are no PE exports. The string `"ILua"` exists only in `luaprofile.dll` itself (checked across all retail PEs and INIs); nobody calls `CreateInstance("ILua")`. Our `bin/DACOM.INI` does not load the library either.
- `IProfileParser` is provided by dacom's built-in parser (`ProfileParser2`, aggregated into `COManager`), see [dacom.md](dacom.md). `CreateInstance` selects a factory by the CRC of the name, so `"ILua"` is reachable only under its own name; the `"ProfileParser"`/`"ProfileParser2"` factories (priority `0x80000000`) are separate dacom components.
- This VM is **not** Thorn (`src/Thorn/lua32/` is a different copy of Lua 3.2) and not a shared `lua_State`: every `ILua` instance creates its own `lua_State` (`lua_setstate(0)` + `lua_open`). A shared public header must not expose internals (`kRegisterPriority` conflicts with a constant in Shading).

## Object creation

`CreateInstance` requires an AGGDESC of size `0x14` and a name byte-for-byte equal to `"ILua"`. Fields: `+8` is the **outer `IUnknown`** (controlling unknown, object `+0x18`), `+0xC` is the `IUnknown**` for the inner one (object `+0x1C`), `+0x10` is not read. This is the same layout as `SystemContainer`; the comment in `dacom.h` about the "IID at `+8`" refers to a different descriptor. The result is the object base (the `ILua` facet at +0), not the parser facet; the parser is obtained through QI on the inner one (the `IProfileParser` facet at `+4`, `IAggregateComponent` at `+8`). OOM gives `0xFFFFFFFC`, a wrong size/name gives `0xFFFFFFFD`.

The object is `calloc(0x28)`. The constructor prepares the VM: the Lua helpers `__get_profile_line(i)` and `__get_table_line(table, i)` (`next`/`nextvar`, **0-based** index, only strings and numbers are counted), and registers the C callback `_ERRORMESSAGE`, which writes the string `"%s(%d) : ERROR:General:%s"` through `FDUMP`. The last `Release` closes the VM.

## `IProfileParser` slot contracts (ILua facet)

| Slot | Behaviour |
|------|-----------|
| 3 | `flags != 0` → `-1`; otherwise `lua_dofile(path)`, the result is discarded, always `GR_OK`. The slot has no name (the binary has no method strings) — do not name it |
| 4 | walk of Lua global **tables** via `lua_nextvar`, callback `cb(parser, name, ctx)`; `0` — stop (the method returns 0), end of the walk — 1 |
| 5 `OpenSection` | allows only `flags == 3`; name `"$"` → ref `-1`, otherwise `getglobal` + `istable` + `lua_ref` |
| 6 `CloseSection` | release the ref and free the handle (8 bytes: next, ref) |
| 7 `ReadLine` | `key=value`; lines are taken via `__get_table_line`; on the `"$"` handle the path is **dead** (in the PE at `06801413` there is `MOV EAX, lua_callfunction` instead of `CALL`, so the branch below is unreachable) |
| 8 `GetString` | length/0; on `"$"` — `getglobal(key)` |

Dacom's built-in parser has the same contract for slots 5–8 (EngBase, system, RP8, Shading, alchemy, Deformable2, SoundManager, RendComp open a section with `flags == 3`); its slots 3–4 differ (INI file loading and a walk over section names), hence "do not name". There is no converter from INI sections to Lua tables in this DLL: on a fresh `ILua`, `OpenSection("Engine")` returns 0 until slot 3 executes Lua with an `Engine` table.

The `ILua` facet (slot 3): call a global Lua function `(name, out_rec*, argc, argv**)`; not a function → `0xFFFFFFFE`, and the previous `lua_State` is **not** restored; arguments are `tag` + payload records (tag 9 — string, 5 — float, 6 — double, bit `0x1000` — integer). Retail has no callers. In the port `InvokeGlobal` sets only the active `Payload.String` (retail copies an uninitialized word into the inactive bytes of the union — the port does not claim byte-level compatibility with this garbage).

## Known deviations

- `luaV_tostring` prints a number via the UCRT `sprintf("%.16g")`, retail uses MSVCRT: for `1e20` this yields `"1e+20"` and `"1e+020"`. The result is interned and visible to Lua; the deviation is recorded in [crt_compat.md](crt_compat.md).
- The port knows nothing about `FL_DevOnlyLibs.ini` (it is not in the repository); what retail-dev loaded through it is not proven.
