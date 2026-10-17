# DACOM (dacom.dll)

`dacom.dll` is the root of the whole architecture: the component manager `COManager` (`ICOManager`), the factory registry, the library loader driven by `DACOM.INI`, the built-in INI parser (`IProfileParser`) and the emergency output `FDUMP`. This document is for whoever touches module loading or parses INI files. The port is `src/dacom/dacom.cpp`, the public ABI is `src/include/dacom.h`; addresses and slots of individual functions are in decomp-tracker (listing: `EXE/Disasm/dacom.dll.txt`).

## Conventions

- The exports `DACOM_Acquire` / `DACOM_GetDllVersion` / `DACOM_GetVersion` are `__cdecl`; interface methods are `__stdcall`, `this` on the stack. 15 exports in total (including `DACOM_CRC::*`, `LogStream::*`, the `FDUMP` data, `stricmp`); `src/dacom/dacom.def` lists all of them.
- IIDs are strings such as `"1.11_ICOManager"`, `"1.11_IProfileParser"`; in code they are `Iids::` from `src/include/dacom_iids.h`.
- `AddRef`/`Release` of `COManager` always return 1 (a singleton that lives as long as the process).

## Who answers `IProfileParser`

**Dacom's built-in parser, not `luaprofile.dll`.** `COManager::COManager` (`065b1f80`) registers the `"ProfileParser"` and `"ProfileParser2"` factories with priority `0x80000000` and creates `ProfileParser2` as an aggregated object (outer = `COManager`): `COManager+0x28` is its `IProfileParser*`, `+0x2c` is the inner `IUnknown`. `COManager::QueryInterface` (`065b2090`) answers `"1.11_ICOManager"` itself and hands everything else to the inner `IUnknown`, which knows `"1.11_IProfileParser"` and `"1.11_IProfileParser2"`. Consumers (EngBase, system, alchemy, rendcomp, rp8, shading, sound, deformable2) obtain the parser through `DACOM_Acquire()->QueryInterface`, not `CreateInstance`. The parser factories (`065b4750`) accept only the extended descriptor `{0x14, name, outer, …}`; a plain 8-byte AGGDESC yields `-3`.

`luaprofile.dll` registers only `"ILua"`; no retail module creates such a component, and it is not loaded in retail (not in `[Libraries]`). It is not in our `bin/DACOM.INI` either.

## Library loading

- `DllPath` (from `[DACOM]` in the INI): an absolute value (`\`, `/`, `X:`) is taken as is; a relative one is built from the exe directory, leading `..\` go up. `DllPath = .\` yields the exe directory. If `IgnoreDACOMEnv` is not `true`, the `DACOM` environment variable overrides `DllPath`.
- `AddLibrary`: the only lookup is `SearchPathA(DllPath, name, …)`. There are no fallback paths (`..\EXE` and the like): a name that is not found produces an error via `FDUMP` and `-1`; a `LoadLibrary` error gives `-2`. On success `{HMODULE, name}` is written to the library list; while the DLL is loading, its name (without extension) sits in `COManager+0x30` and tags the components it registers from `DllMain`.
- Hence in our build a name that is not found in `bin\` must fail loudly rather than be picked up from `EXE\`.

## Component registry

- `RegisterComponent(factory, name, priority)`: the reference is inserted **before the first reference with priority <= the new one** (unsigned) — among equals the last registered wins. The registry is `map<CRC32(name), list<reference>>` in ascending CRC order (in the port a `std::map`, same iteration order); the CRC folds case using the retail table (the index is signed, bytes >= 0x80 read the table tail).
- `CreateInstance(desc, out)`: `*out = 0`; `desc == NULL`, no name, or the name `"COManager"` → the manager itself; otherwise the bucket by the CRC of the name, no bucket → `-3`, factories are tried in order, the first success wins, otherwise the code of the last one.
- `SetINIConfig(text, flags)`: `flags & 2` — `text` is an in-memory INI (`IProfileParser2::LoadFromMemory`), otherwise a file path; after loading, `LoadConfiguration` runs (`[DACOM]` and `[Libraries]`); load errors of individual libraries do not affect the result.

## `FDUMP`

`DllMain` installs the default handler: `vsprintf` into a 0x1000 buffer, `OutputDebugStringA`; levels 0, 1 and 2 (until Ignore is pressed) show a `MessageBoxA` "DACOM::STANDARD_DUMP" (Abort/Retry/Ignore): Abort → `PostQuitMessage(-1)` + `abort()`, Retry → `INT3`, Ignore at level 2 → show no more warnings. Common.dll (`ErrorHandler::activate`) then replaces `FDUMP` with its own. Known deviation: our default handler returns `void`, retail returns `int` 0.

## The `RegExp` component

Registered from `DllMain` (hence `COManager` is already constructed at dacom.dll attach): `CreateInstance` requires an 8-byte AGGDESC `{8, "RegExp"}`; interfaces `IRegExp` and `IDAComponent`. Internally it is Henry Spencer's regexp with extensions (`\<`, `\>`, `\` escaping in literals and classes, `\d \n \r \s \t`). The compiler and the matcher share global cursors, so the engine is not reentrant; the expression and source strings are borrowed, the program and the capture buffer belong to the object. Known port deviation: `isalnum` in the matcher is applied to an unsigned byte.

## Built-in INI parser (format)

The parser object contains the `IProfileParser` facet (+0), `IProfileParser2` (+4, visible only inside dacom), the section base and the counter of the inner `IUnknown`; QI/AddRef/Release of both facets go to the outer.

- `IProfileParser` slots: file load (`flags & 1` → `-1`), section walk `cb(this, name, ctx)` (0 — stop), `OpenSection` (`flags != 3` → NULL), `CloseSection` → 1, `ReadLine`/`GetString` without a buffer return the required length. `IProfileParser2` adds `LoadFromMemory`, `EnumerateSections` and `EnumerateKeys` (a visitor with a `Visit` method).
- Lexer: separators `\t \r ' '`; `;` is a comment to the end of the line; tokens `\n`, `[`, `]`, `=`; a word ends at `" \r\n\t[];="`; the word `@include` (case-insensitive) is a separate token. A `key = value` line: the key is the words up to `=`, the value is the words up to the end of the line (a repeated `=` is part of the value). `@include file` — the path is relative to the directory of the current file if the name has no drive and does not start with `\`.
- Sections are stored sorted by `_stricmp`, duplicates are kept (the new one after the equal ones), lookup returns the first.
- A key outside a section dereferences NULL (a crash in retail), an unexpected token is `INT3`: the port reproduces this behaviour.
