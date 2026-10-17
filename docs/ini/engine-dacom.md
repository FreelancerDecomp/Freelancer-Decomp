# DACOM.INI / dacomsrv.ini: engine configuration

This document is the full reference for the engine configuration file that the DACOM component manager loads at start-up (`DACOM.INI` for the client, `DACOMsrv.ini` for the server): the file grammar, how the file is found, which DLLs it loads, and every `[Section]` and key that the engine modules read through the built-in `IProfileParser`. It also compares the development files in `bin\` with what the code actually reads.

**File location / how it is found.** The client calls `DALib::Startup(window, nullptr)`, which uses `DACOM.INI` in the current working directory if it is readable, and otherwise `DACOM.INI` in the directory of `DALib.dll`. The server (`FLServer.exe`) calls `DALib::Startup(NULL, "DACOMsrv.ini")`, a path relative to the working directory. An explicit path is passed straight to `SetINIConfig`. If the file cannot be loaded `DALib::Startup` fails. In this repository the development copies live in `bin\` (`DACOM.INI`, `dacomsrv.ini`); the `bin_debug\` and `binasan\` copies are generated from them by `src/copy_dev_ini.py`, which rewrites the absolute `D:\Freelancer\bin\` prefix to the output directory, so only `bin\*.ini` should be edited. Game data configuration (`freelancer.ini`, `[Data]`) is a different file and a different parser, see [freelancer-ini.md](freelancer-ini.md) and [parsers.md](parsers.md).

Source files: [dacom.cpp](../../src/dacom/dacom.cpp), [DALib.cpp](../../src/DALib/DALib.cpp), and the consumers listed per section below. "Not validated" means the code applies no range or enumeration check to the value.

---

## 1. File grammar (`IProfileParser`, implemented in `dacom.dll`)

The parser is the engine's own: `FIniLexer` ([dacom.cpp](../../src/dacom/dacom.cpp#L331)) tokenises, `FIniDatabase::Parse` ([dacom.cpp](../../src/dacom/dacom.cpp#L653)) builds a database of sections, and `FProfileParserObject` ([dacom.cpp](../../src/dacom/dacom.cpp#L792)) exposes it. The whole file is read into memory first (`fopen` with `"rb"`). It is **not** the `INI_Reader` used for game data and it is **not** the Win32 `GetPrivateProfile*` API (the project never calls the latter).

**Tokens.**

* Space, tab and carriage return separate tokens and are otherwise ignored; a line feed ends a line.
* `;` starts a comment that runs to the end of the line (also after a value).
* `[`, `]`, `=` are single-character tokens. Any other run of characters up to one of ` \r\n\t[];=` is a "word". There is no quoting.
* The word `@include` (case-insensitive) is the include directive.

**Statements.**

* `[Name]` starts a section. Everything between `[` and `]` is read as words joined by single spaces (so `[ Libraries ]` is `Libraries`). Section names are compared case-insensitively. A header line whose first token after `[` is a comment (`[;Name]`) turns the rest of the line into a comment: **no new section starts and the following keys stay in the previous section.**
* `key = value`: the first word is the key, any further words before `=` are appended to it with single spaces (the key is the whole left-hand phrase), and the value is the right-hand phrase to the end of the line. In both phrases consecutive words are joined with exactly **one space**, so runs of blanks or tabs collapse; a second `=` in the value is kept as the text `=`. Phrases are limited to about 1 KB.
* A line without `=` is a bare key with no value (the form used for the DLL names in `[Libraries]` and the component names in `[System]`, `[Engine]`, `[RenderManager]`).
* `@include <file>`: parses another file at that point. A path without drive and without a leading `\` is taken relative to the directory of the **including file** (as written on the command line, so relative to the working directory when the including file was given without a directory). A missing include is ignored silently (the result of the nested parse is discarded).
* A key before the first section header dereferences a null section and crashes (retail behaviour). A stray `]` or unexpected token triggers a debug break.

**Lookup rules.**

* Sections are kept sorted by name; duplicate sections are **kept**, a new one landing behind equal ones. `OpenSection` returns the first match, i.e. the earliest in parse order, so a second `[Name]` block in the same file is never seen by consumers (an `@include`d section that is parsed earlier wins over a later one in the including file).
* `ReadLine(section, index, buffer, size)` returns the n-th line of the section (0-based, in file order, duplicates included) as `key = value` or as the bare key. `GetString(section, key, buffer, size)` returns the value of the **first** line whose key matches (case-insensitive) and its length as the result; the result is 0 when the key is absent **and when it is present with an empty value** (`Key =`), so consumers cannot tell the two apart. A bare key (no `=`) found by `GetString` yields an empty string with a result of 0 as well.
* `OpenSection` only answers when its flags argument is 3 (every consumer passes 3).
* A second, private interface `IProfileParser2` (load from memory, enumerate sections and keys) exists inside `dacom.dll`; it is used only by `SetINIConfig` with flag 2 (the argument is then the INI text itself). The repository's `luaprofile.dll` is an alternative `IProfileParser` implementation (section 14).

### 1.1 The "numeric value" parser shared by the engine modules

Most modules copy the same routine to turn a value into a number (here called the **retail integer**). It is applied to the text returned by `GetString`:

1. Take the longest prefix made only of the characters `0123456789truefalsynoxTRUEFALSYNOX+-` and cut the text there (if the prefix is empty nothing is cut).
2. If the result equals (case-insensitive) `true` or `yes` the value is 1; `false` or `no` gives 0.
3. Otherwise, if the text contains `x` or `X`, it is parsed with `strtoul(text, 0, 16)` (so `0x10` is read as hexadecimal 16).
4. Otherwise it is parsed with the MSVCRT `atoi` (which wraps modulo 2^32 on overflow and returns 0 for text such as `Light`).

There is no floating-point syntax: a value such as `1.5` reads as 1. Where a D3D state is float-typed, the value must be given as the raw IEEE-754 bit pattern in hexadecimal (the built-in defaults, e.g. `0x3F800000` for 1.0, are written that way). The sound reverb keys use a separate float parser (section 8.3). Implementations: [RP8.cpp](../../src/RP8/RP8.cpp#L319), [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L211), [TextureLibrary.cpp](../../src/Shading/TextureLibrary.cpp#L38), [MaterialBatcher.cpp](../../src/Shading/MaterialBatcher.cpp#L341), [Plugins.cpp](../../src/RendComp/Plugins.cpp#L210), [alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1707), [Deformable2.cpp](../../src/Deformable2/Deformable2.cpp#L4658).

---

## 2. Start-up sequence and the sections that drive it

1. `DALib::Startup` loads the file (above) with `SetINIConfig(path, 0)`.
2. `FCOManager::LoadConfiguration` ([dacom.cpp](../../src/dacom/dacom.cpp#L1463)) reads `[DACOM]` and then loads the DLLs in `[Libraries]`. It runs once per process.
3. `DALib::Startup` creates the `SystemContainer`, which reads `[System]` and creates the listed components (section 5).
4. The engine (`EngBase`) reads `[Engine]`; the render manager (`RendComp`) reads `[RenderManager]` (sections 6, 7). Each component reads its own section when it initialises (sections 8 to 13).

Failures in a component are logged and skipped rather than aborting, unless noted.

---

## 3. `[DACOM]`

Read by `FCOManager::LoadConfiguration` ([dacom.cpp](../../src/dacom/dacom.cpp#L1463)).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `DllPath` | path | none (empty: `SearchPath` uses its default order: exe directory, system directories, `PATH`) | Directory searched for the DLLs named in `[Libraries]`. An absolute value (first character `\` or `/`, or second character `:`) is used as is. Any other value is relative to the **directory of the executable**: leading `.\` (or `./`) segments are skipped and each leading `..\` (or `../`) climbs one directory, then the remainder is appended (`.\` gives the exe directory itself). A relative value that starts with `.` but is not one of those forms (for example `.x` or a lone `.`) never advances and **hangs** (retail behaviour). | [dacom.cpp](../../src/dacom/dacom.cpp#L1479) |
| `IgnoreDACOMEnv` | bool | false | Controls whether the environment variable `DACOM` may replace `DllPath`. A value whose first four characters are `true` (case-insensitive, so `trueish` also counts) ignores the variable. A value that does not start with `false` and whose `atoi` is non-zero (for example `1`) also ignores it. Everything else, including `yes` and an empty value, leaves the variable active. | [dacom.cpp](../../src/dacom/dacom.cpp#L1545) |

**Environment variable `DACOM`.** Unless ignored as above, if the process environment contains `DACOM`, its text **replaces** `DllPath` entirely (it is used verbatim as the search directory, whatever the INI said). It is read with `getenv("DACOM")` ([dacom.cpp](../../src/dacom/dacom.cpp#L1556)); there is no other environment variable.

---

## 4. `[Libraries]`

Loaded by `FCOManager::LoadLibraries` ([dacom.cpp](../../src/dacom/dacom.cpp#L1577)) and `AddLibrary` ([dacom.cpp](../../src/dacom/dacom.cpp#L1292)).

| Entry | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| one DLL per line | bare word | none | Repeatable and ordered: the DLLs are loaded in file order, and the order is observable (a module's factories are registered from its `DllMain`; later sections create components by name from them). The first word of the line is the name; anything after the first blank, tab or `;` is cut. The name must carry its extension (`ReadFile.dll`) because no default extension is added, and cannot contain spaces. It is resolved with `SearchPathA(DllPath, name)`: a bare name is searched in `DllPath`; a name given as an absolute path (such as the `D:\Freelancer\bin\...` entries of the dev file) is used as is, which is how the dev file keeps `DllPath` from picking up the retail DLLs. A name that is not found logs "unable to find dll" and is skipped; a DLL that fails to load logs "unable to load dll" and is skipped. The component manager is considered initialised when at least one DLL loaded. Lines are read into a 260-byte buffer. | [dacom.cpp](../../src/dacom/dacom.cpp#L1580) |

If the section is missing the manager logs "[Libraries] group not found" and stays uninitialised (`SetINIConfig` still reports success, so `DALib::Startup` goes on without any library). **No fallback**: a name that cannot be resolved through `DllPath` is never looked up anywhere else (in particular not in `EXE\`).

---

## 5. `[System]`

Read by `FSystemContainer::LoadSystemComponents` ([system.cpp](../../src/system/system.cpp#L332)).

| Entry | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `Name` or `Name = extra` | component name + optional text | none | One system component per line, in order; an absent section means no components. Each name is passed to `CreateInstance` as an aggregate description together with the optional text after `=`. A line that is empty or whose first character is `;` is skipped; the name is cut at the first blank or tab. The result of every line is logged as a notice (`OK` or `FAILED`); a failed component is dropped and the next line is still processed. After the last line all components are initialised (`Initialize`), and a component whose initialisation fails is removed. The line buffer is 256 characters. Names are the component names registered by the loaded libraries and are not validated. | [system.cpp](../../src/system/system.cpp#L332) |

The `extra` text is consumed by the component. The consumer documented here is the render pipeline `DirectX8` (RP8): `DirectX8 = MySection` makes RP8 read its settings from `[MySection]`; without it the section is `RenderPipeline` ([RP8.cpp](../../src/RP8/RP8.cpp#L1137)).

Component names used in the shipped/dev files: `DirectX8`, `TextureLibrary`, `MaterialLibrary`, `MaterialBatcher`, `SoundManager`, `SoundStreamer`, `Alchemy`, `VMeshLibrary`, `MaterialAnimation`, `FxRuntime`.

---

## 6. `[Engine]`

Read by `EngineLoadEngineComponents` ([EngBase.cpp](../../src/EngBase/EngBase.cpp#L3296)); line parser `ParseEngineProfileLine` ([EngBase.cpp](../../src/EngBase/EngBase.cpp#L3262)).

| Entry | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `Name` or `Name = value` | component name + optional token | none | One engine sub-component per line, in order (`Animation`, `Hardpoint`, `Deformable`, `RenderManager`, `FLAppearance`, `VMeshWire` in the shipped files). The line is split on blanks and tabs: the first token is the name; a token `=` followed by one more token supplies the value handed to the component (only a single token, no spaces); a token starting with `;` ends the line. Each component is created by name and aggregated into the engine; a creation failure is logged and the line skipped. In a second pass every created component is initialised; one whose initialisation fails is removed again (warning). | [EngBase.cpp](../../src/EngBase/EngBase.cpp#L3296) |

If `[Engine]` is missing the engine silently loads no sub-components. If the profile parser cannot be acquired, loading fails.

---

## 7. `[RenderManager]` and `[TriMesh]`

### 7.1 `[RenderManager]`

Read by `FRenderManager::LoadPlugins` ([RenderManager.cpp](../../src/RendComp/RenderManager.cpp#L313)).

| Entry | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `PluginName` | bare word | none | One render plug-in per line, created in the listed order (`VMesh`, `TriMesh`, `SphereMesh`). Lines beginning with `;` are skipped; the name is cut at the first blank, tab or `;`. The order is significant: the shipped file notes that `TriMesh` must come before `SphereMesh` because "subsystems attempt to create objects in order listed". A plug-in that cannot be created is skipped silently. | [RenderManager.cpp](../../src/RendComp/RenderManager.cpp#L338) |

A missing section logs "[RenderManager] section not found!" and the manager fails to initialise.

### 7.2 `[TriMesh]`

Read by `TriMeshPluginReadProfile` ([Plugins.cpp](../../src/RendComp/Plugins.cpp#L210)).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `Tristrips` | bool / retail integer | 1 | Non-zero keeps triangle-strip rendering enabled, 0 disables it. Value parsed as in 1.1. The key spelling in the code is `Tristrips`; the dev files write `tristrips` (matching is case-insensitive). | [Plugins.cpp](../../src/RendComp/Plugins.cpp#L235) |

---

## 8. `[RenderPipeline]` (RP8, the Direct3D 8 pipeline)

The section name is `RenderPipeline` unless `[System]` says `DirectX8 = OtherName` (section 5). All keys are read once in `FDirectX8::Startup` ([RP8.cpp](../../src/RP8/RP8.cpp#L2569)) with the retail integer parser (1.1) unless stated. `Pipeline->Startup(nullptr)` is how the client starts it.

### 8.1 Device creation and flags

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `DeviceId` | GUID text | `{00000000-0000-0000-0000-000000000000}` | Read (up to 127 characters) when the section name differs from the previously started one; the value is **not used afterwards** in the port. | [RP8.cpp](../../src/RP8/RP8.cpp#L2585) |
| `DeviceType` | string | `Hardware` | Read like `DeviceId`; **not used afterwards**. | same |
| `DeviceTypeFallback` | int | 0 | Read like `DeviceId`; **not used afterwards**. | same |
| `MGSDB` | path | empty | Hardware-quirk database (e.g. `FLConfigDatabase.txt`); up to 127 characters, used as written. An empty value skips the database. The file uses RP8's own syntax, not INI. | [RP8.cpp](../../src/RP8/RP8.cpp#L2596) |
| `FPU_PRESERVE` | bool | 0 | Non-zero adds `D3DCREATE_FPU_PRESERVE` (0x2). | [RP8.cpp](../../src/RP8/RP8.cpp#L2605) |
| `MULTITHREADED` | bool | 0 | Non-zero adds `D3DCREATE_MULTITHREADED` (0x4). | same |
| `PUREDEVICE` | bool | 0 | Non-zero adds `D3DCREATE_PUREDEVICE` (0x10). | same |
| `SOFTWARE_VERTEXPROCESSING` | bool | 0 | Checked first: non-zero selects software vertex processing (0x20). | [RP8.cpp](../../src/RP8/RP8.cpp#L2613) |
| `MIXED_VERTEXPROCESSING` | bool | 0 | Checked only if the software key is 0: non-zero selects mixed (0x80). | same |
| `HARDWARE_VERTEXPROCESSING` | bool | 0 | Read, but **hardware processing (0x40) is chosen whenever neither of the two keys above is non-zero, whatever this key says.** The vertex-processing keys are mutually exclusive in this priority order. | same |
| `LOCKABLE_BACKBUFFER` | bool | 0 | Allow locking the back buffer. (The retail `DACOM.INI` comment misspells it `LOCAKABLE_BACKBUFFER`; the misspelling is never read.) | [RP8.cpp](../../src/RP8/RP8.cpp#L2629) |
| `USE_SYSLOCK` | bool | 0 | Take the Windows-wide lock while resources are in use. | same |
| `HANDLE_SWAPLOSS` | bool | **1** | Handle device loss inside the pipeline. | same |

### 8.2 Pipeline properties (read for every entry, default applies when absent)

| Key (pipeline-state id) | Default | Key (pipeline-state id) | Default |
|---|---|---|---|
| `TEXTURE` (0) | 0x1 | `CLEAR_COLOR` (8) | 0x0 |
| `LIGHTING` (1) | 0x1 | `CLEAR_ZDEPTH` (9) | 0x3F800000 (1.0f bits) |
| `TEXTURE_LOD` (2) | 0x1 | `CLEAR_STENCIL` (10) | 0xFFFFFFFF |
| `TEXTURE_ALLOW_8BIT` (3) | 0x0 | `STATE_CACHE` (11) | 0x1 |
| `TEXTURE_ALLOW_32BIT` (4) | 0x1 | `BROKEN_MOD2X` (12) | 0x0 |
| `TEXTURE_ALLOW_DXT` (5) | 0x1 | `BROKEN_FLIP` (13) | 0x0 |
| `BROKEN_MULTITEXTURE` (6) | 0x1 | `VIEWSPACE_LIGHTS` (7) | 0x1 |

Source: table [RP8.cpp](../../src/RP8/RP8.cpp#L103), read in [RP8.cpp](../../src/RP8/RP8.cpp#L2640). Each value is stored in the pipeline-state slot of that id. When `TEXTURE_ALLOW_8BIT`, `TEXTURE_ALLOW_32BIT` or `TEXTURE_ALLOW_DXT` is 0, the texture-format check rejects 8-bit palettised, 32-bit-per-pixel or DXT formats respectively.

### 8.3 Device ability overrides (honoured only when the key is present)

These keys override the capabilities derived from the Direct3D device caps (and from the MGSDB database). Each is read **only if the key exists** in the section; absent keys leave the detected value untouched. The value is a retail integer. Table and ability index: [RP8.cpp](../../src/RP8/RP8.cpp#L121).

`DEVICE_GEOMETRY` (4), `TEXTURE_SQUARE_ONLY` (5), `TEXTURE_MAX_WIDTH` (6), `TEXTURE_MAX_HEIGHT` (7), `TEXTURE_CUBEMAPS` (8), `DEPTH_BIAS` (9), `FOG_VERTEX` (10), `FOG_PIXEL` (11), `FOG_RANGE` (12), `FOG_W` (13), `RASTER_ANTIALIASEDGES` (14), `DEVICE_SUPPORT_LEVEL` (15), `TEXTURE_TRILINEAR` (16), `DEVICE_BAD_MODE` (17), `DEVICE_BAD_4444` (18). Meaning of the numbers beyond "0 = off / non-zero = on or a limit" is not validated.

### 8.4 Direct3D render states (default applies when absent)

Every name below is read and applied as the initial value of the Direct3D render state of the same name; the value is the raw DWORD handed to the device (no validation; float-typed states take the float's bit pattern in hex). Source: table [RP8.cpp](../../src/RP8/RP8.cpp#L140), read in [RP8.cpp](../../src/RP8/RP8.cpp#L2687). Defaults in parentheses.

`ZENABLE` (0x1), `FILLMODE` (0x3), `SHADEMODE` (0x2), `LINEPATTERN` (0x0), `ZWRITEENABLE` (0x1), `ALPHATESTENABLE` (0x0), `LASTPIXEL` (0x1), `SRCBLEND` (0x2), `DESTBLEND` (0x1), `CULLMODE` (0x1), `ZFUNC` (0x2), `ALPHAREF` (0x0), `ALPHAFUNC` (0x8), `DITHERENABLE` (0x0), `ALPHABLENDENABLE` (0x0), `FOGENABLE` (0x0), `SPECULARENABLE` (0x0), `ZVISIBLE` (0x1), `FOGCOLOR` (0x0), `FOGTABLEMODE` (0x1), `FOGSTART` (0x0), `FOGEND` (0x0), `FOGDENSITY` (0x0), `EDGEANTIALIAS` (0x0), `ZBIAS` (0x0), `RANGEFOGENABLE` (0x0), `STENCILENABLE` (0x0), `STENCILFAIL` (0x1), `STENCILZFAIL` (0x1), `STENCILPASS` (0x1), `STENCILFUNC` (0x8), `STENCILREF` (0xFFFFFFFF), `STENCILMASK` (0xFFFFFFFF), `STENCILWRITEMASK` (0xFFFFFFFF), `TEXTUREFACTOR` (0x0), `WRAP0` to `WRAP7` (0x0), `CLIPPING` (0x1), `LIGHTING` (0x0), `AMBIENT` (0x0), `FOGVERTEXMODE` (0x0), `COLORVERTEX` (0x0), `LOCALVIEWER` (0x0), `NORMALIZENORMALS` (0x0), `DIFFUSEMATERIALSOURCE` (0x1), `SPECULARMATERIALSOURCE` (0x2), `AMBIENTMATERIALSOURCE` (0x2), `EMISSIVEMATERIALSOURCE` (0x0), `VERTEXBLEND` (0x0), `CLIPPLANEENABLE` (0x0), `SOFTWAREVERTEXPROCESSING` (0x0), `POINTSIZE` (0x3F800000), `POINTSIZE_MIN` (0x3F800000), `POINTSPRITEENABLE` (0x0), `POINTSCALEENABLE` (0x0), `POINTSCALE_A` (0x3F800000), `POINTSCALE_B` (0x0), `POINTSCALE_C` (0x0), `MULTISAMPLEANTIALIAS` (0x1), `MULTISAMPLEMASK` (0xFFFFFFFF), `PATCHEDGESTYLE` (0x0), `PATCHSEGMENTS` (0x0), `DEBUGMONITORTOKEN` (0x0), `POINTSIZE_MAX` (0x3F800000), `INDEXEDVERTEXBLENDENABLE` (0x0), `COLORWRITEENABLE` (0xF), `TWEENFACTOR` (0x0), `BLENDOP` (0x1), `POSITIONORDER` (0x1), `NORMALORDER` (0x1).

Notes:

* `LIGHTING` is **read twice by name**: once as a pipeline property (8.2, default 1) and once as the render state (default 0). A single `LIGHTING =` line therefore sets both. `TEXTURE`, `TEXTURE_LOD` etc. are not render-state names and appear only once.
* Per-texture-stage states (`COLOROP`, `ALPHAOP`, `COLORARG1/2`, `ADDRESSU/V`, ...) have built-in defaults in the port but are **not read from the INI** (the stage maps are filled only with their defaults).
* Keys that are listed in the retail `DACOM.INI` as comments, for example `DEVICE_GAMMA`, are not read by this code.

### 8.5 `[TextureFormatClasses]` (optional)

Read by `PopulateFormatCatalog` ([RP8.cpp](../../src/RP8/RP8.cpp#L1628)); the section is optional and not present in the dev files. It adjusts which pixel formats are tried, in order, for each texture class. Built-in classes are keyed by a four-character code: `DAOP`, `DAOT`, `DAA1`, `DAA4`, `DAA8`, `DXT1` to `DXT5`.

| Line | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `CLASS = fmt, fmt, ...` | text line, repeatable | built-in lists | The first four bytes of the left-hand token are the class key (any four characters; short tokens read stale bytes). The right-hand side is a comma-separated candidate list; each candidate is `bits:red:green:blue:alpha` (five integers, `sscanf "%d:%d:%d:%d:%d"`). Candidates that match no known pixel format are dropped. A line for an existing class **replaces** its candidate list. `;` starts a comment. | [RP8.cpp](../../src/RP8/RP8.cpp#L1696) |
| `clear` | bare line | n/a | A line starting with `cle` (the code compares only the first three characters, case-insensitive) empties the whole catalogue, including the built-in classes. | same |

---

## 9. `[SoundManager]` and `[AudioProperties]`

Read in `FSoundManager::StartupDevice` ([SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3186)). Many defaults are the *current value of a flag bit*, which comes from the start-up flags the caller passes: `DALib::Startup` calls `StartupDevice(hwnd, 0x113)`, i.e. software algorithm `LIGHT`, right-handed coordinates, 3D hardware and 2D hardware enabled, and no `useEAX` / `createAll2DInSoftware` / `useNoHW`; the defaults below are computed for that call. Integer keys use the retail integer parser (1.1).

### 9.1 `[SoundManager]`

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `deviceID` | GUID text | `{00000000-0000-0000-0000-000000000000}` | DirectSound device GUID in braces (at most 40 characters). The all-zero GUID selects the default device. A malformed text is not validated. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3314) |
| `3D_SW_Algorithm` | enum | `LIGHT` | `FULL`, `LIGHT`, `BASIC`, `USER`, `NONE` (case-insensitive, first 32 characters read). `FULL`, `LIGHT` and `BASIC` select the DirectSound 3D algorithms HRTF-full, HRTF-light and no-virtualisation; `USER` selects the null algorithm GUID; `NONE` also uses the null GUID and changes the DirectSound3D creation flags (clears 0x10, sets 0x40). Any other text clears the algorithm bits and selects none. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3329) |
| `useNoHW` | bool | 0 | Non-zero forces both hardware buffer kinds off (clears `use2DHW` and `use3DHW` before they are read, so they then default to 0). | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3389) |
| `rightHandedCoordinates` | bool | 1 | Right-handed coordinate system; when set the manager also uses a coordinate scale of -1. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3408) |
| `createAll2DInSoftware` | bool | 0 | Create every 2D sound as a software buffer. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3418) |
| `speakerConfiguration` | int | not applied | Applied after the device is created **only if the key is present with a non-empty value**. The retail comment lists `HEADPHONES 1, STEREO 2, QUAD 7, SURROUND 8` (the DirectSound speaker configuration values). Not validated. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3435) |
| `maxSoundChannels` | int | 0 (automatic) | Maximum simultaneous sounds. 0 means "derive from the hardware": the larger of the free hardware 3D and mixing buffers, but at least 8, and unchanged when the card reports none. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3454) |
| `use2DHW` | bool | 1 | Use hardware 2D buffers. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3457) |
| `use3DHW` | bool | 1 | Use hardware 3D buffers. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3463) |
| `ALLOW_HW_TO_SW` | bool | 0 | Allow a hardware sound to be re-created in software; effective only if `use2DHW` or `use3DHW` is on. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3469) |
| `ALLOW_SW_TO_HW` | bool | 0 | The reverse; same condition. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3475) |
| `FORCE_FREQ_CONTROL_TO_SW` | bool | 1 | Force frequency-controlled sounds into software buffers. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3481) |
| `NEVER_SET_START_POS` | bool | 0 | Never set the start position of a buffer. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3483) |
| `DISPLAY_EXTRA_DEBUG_INFO` | bool | 0 | Extra sound debug notices. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3485) |
| `max3DPan` | int | 100 (0x64) | Stored as the float `value * 100` (3D pan range). Not validated. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3490) |

### 9.2 `[AudioProperties]` (EAX reverb)

Float keys use a separate parser: the longest prefix of `0123456789eE.+-` is taken and converted with `atof`.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `useEAX` | bool | 0 | Enable the EAX reverb flag. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3211) |
| `reverbBaseEnvironment` | int | 0 | EAX base environment preset. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3231) |
| `reverbMasterVolume` | float | 0.0 | Reverb master volume. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3247) |
| `reverbDecayTime` | float | 0.1 | Reverb decay time. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3263) |
| `reverbDamping` | float | 1.0 | Reverb damping. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3279) |
| `reverbMix` | float, stored as int | 0 | Truncated to an integer. | [SoundManager.cpp](../../src/SoundManager/SoundManager.cpp#L3295) |

None of the numeric ranges are validated.

---

## 10. `[Alchemy]` (particle effects)

Read by `ApplyAlchemyIniAndPools` ([alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1774)) with the retail integer parser. The section is read when the Alchemy component starts and sizes the appearance pools.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `FxBasicAppearance.poolSize` | int | 1000 | Maximum live basic particles (each uses 4 vertices and 6 indices). Not validated; 0 allocates an empty pool. | [alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1780) |
| `FxParticleAppearance.poolSize` | int | 1000 | Maximum live particle-effect objects ("generates effects, not particles"). | [alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1782) |
| `FxRectAppearance.poolSize` | int | 1000 | Rectangle particles. | [alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1786) |
| `FxPerpAppearance.poolSize` | int | 1000 | Perpendicular-quad particles. | [alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1788) |
| `FxMeshAppearance.poolSize` | int | 1000 | Mesh appearances (the retail comment: enough for all tradelane rings of a system). | [alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1790) |
| `FxOrientedAppearance.poolSize` | int | 1000 | Oriented particles. | [alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1792) |
| `FLDustAppearance.poolSize` | int | 1000 | Dust particles (custom appearance). | [alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1794) |
| `FLBeamAppearance.poolSize` | int | 1000 | Beam segments (5 vertices and 24 indices each). | [alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1796) |
| `Alchemy.useMaterialBatcher` | bool | 1 (read, then discarded) | The key is read but its value has no effect (as in the retail code). | [alchemy_common.cpp](../../src/alchemy/alchemy_common.cpp#L1799) |

Retail comments mention `Alchemy.maxVertices` / `Alchemy.maxIndices`; they are only commented-out hints and are not read.

---

## 11. `[Deformable]`

Read by `ApplyCreateInstanceInit` ([Deformable2.cpp](../../src/Deformable2/Deformable2.cpp#L4690)) with the retail integer parser.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `DeformRate` | int | 0 | `N` gives a per-instance deformation rate of `1/N` as a float; 0 (or the key absent) gives rate 0. Each new instance then gets a random phase scaled by that rate. Not validated. | [Deformable2.cpp](../../src/Deformable2/Deformable2.cpp#L4720) |

---

## 12. Shading: `[MaterialBatcher]`, `[BatchedMaterials]`, `[MaterialMap]`, `[TextureLibrary]`, `[TextureFormatMaps]`

### 12.1 `[MaterialBatcher]`

Read by `FMaterialBatcher::StartupFromOuter` ([MaterialBatcher.cpp](../../src/Shading/MaterialBatcher.cpp#L393)).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `BufferSize` | int | 0x100000 (1 MiB) | Size in bytes of the batcher's scratch buffer. Not validated. | [MaterialBatcher.cpp](../../src/Shading/MaterialBatcher.cpp#L439) |
| `Bin1SortOrder` | int | 2 | Sort mode of the second bin. Values 2 to 5 are processed; 6 and 7 print "sort order unimplemented" and draw nothing; 1 prints a warning ("sort state not well implemented"); any other value does nothing. | [MaterialBatcher.cpp](../../src/Shading/MaterialBatcher.cpp#L449) |

### 12.2 `[BatchedMaterials]`

Read by `ParseBatchedMaterials` ([MaterialBatcher.cpp](../../src/Shading/MaterialBatcher.cpp#L474)); repeatable lines, evaluated in file order.

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `Type` | regular expression | none | Repeatable. A material whose (remapped) type string matches the expression is created as a batched material. Key compared with `_stricmp`. | [MaterialBatcher.cpp](../../src/Shading/MaterialBatcher.cpp#L507) |
| `Name` | regular expression | none | Repeatable. Same, matched against the lower-cased material name. | [MaterialBatcher.cpp](../../src/Shading/MaterialBatcher.cpp#L509) |

Any other key is ignored. A line must contain `=`. The expression language is the small Henry-Spencer-style engine in `Shading/MaterialRegex.cpp` (see the shipped example `Type = .*OcOt`). The first rule that matches wins.

### 12.3 `[MaterialMap]`

Read by `FMaterialLibrary::ParseMaterialMap` ([MaterialLibrary.cpp](../../src/Shading/MaterialLibrary.cpp#L1081)). Each line is one rewrite rule; rules are inserted at the front of the list, so **later lines are evaluated first** ("put more specific last").

| Line form | Meaning | Source |
|---|---|---|
| `TypePattern = NewType` | One `=`: a *type* rule. When the material's type string matches `TypePattern`, its type becomes `NewType`. | [MaterialLibrary.cpp](../../src/Shading/MaterialLibrary.cpp#L1096) |
| `name = NamePattern = NewType` | Two `=`: the word before the first `=` selects the kind. The selector `name` (case-insensitive) makes it a *name* rule (the pattern is matched against the material name); any other selector makes it a type rule. The pattern is the text between the first and last `=` (so a pattern cannot contain `=`). | same |

`NewType` is cut to 63 characters. Each rule logs an "Adding material map" notice. Example (synthetic): `name = ^glass_.* = SomeGlassMaterial`.

### 12.4 `[TextureLibrary]`

Read in `FTextureLibrary` start-up ([TextureLibrary.cpp](../../src/Shading/TextureLibrary.cpp#L746)) with the retail integer parser. A retail comment in `DACOM.INI` states that the client's `PerfOptions.ini` overrides these settings (see [client-audio-misc.md](client-audio-misc.md)).

| Key | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `TEXTURE_LOD_LOAD` | int | 1 | Texture level-of-detail load mode (generic slot 0). | [TextureLibrary.cpp](../../src/Shading/TextureLibrary.cpp#L767) |
| `TEXTURE_LOD_LOAD_MIN` | int | 0 | Smallest LOD edge to load (generic slot 4), e.g. 128. | [TextureLibrary.cpp](../../src/Shading/TextureLibrary.cpp#L775) |

### 12.5 `[TextureFormatMaps]` (optional)

Read by `FormatMapLoadFromParser` ([TextureLibrary.cpp](../../src/Shading/TextureLibrary.cpp#L174)); not present in the dev files. The library first installs eight built-in rows, then appends one row per line. A line with a key beginning `cle` (three characters compared) empties the table, built-in rows included.

| Line | Type | Default | Meaning / allowed values | Source |
|---|---|---|---|---|
| `f0:f1:f2:f3:f4:f5 = FOUR` | text line, repeatable | built-in rows | Left of `=`: six fields separated by blanks or `:`; each is a non-negative integer or `*` (match anything). Fields 0 to 3 are compared with the source format's bits per pixel, red bits, green bits and blue bits; fields 4 and 5 are both compared with its alpha bits, exactly as the code does. Right of `=`: the first four characters are the FourCC of the target format class (for example `DAOP`, `DAA1`, `DAA4`, `DAA8`, `DAOT`); it must be a known format FourCC, otherwise the row maps to "no format". `;` starts a comment. Rows are tried in order, the first match wins; DXT source formats map to themselves. A line with an empty right-hand side is ignored. | [TextureLibrary.cpp](../../src/Shading/TextureLibrary.cpp#L139) |

---

## 13. `dacomsrv.ini` (server)

The server loads the same parser and component manager, so every section above applies to it. The development file `bin\dacomsrv.ini`:

```ini
@include FL_Dev.ini
[DACOM]        IgnoreDACOMEnv = true, DllPath = .\
[Libraries]    readfile.dll engbase.dll x86math.dll rendcomp.dll system.dll
               Thorn.dll DebugLib.dll rp8.dll shading.dll
[System]       DirectX8, MaterialLibrary, TextureLibrary
[Engine]       Animation, Hardpoint, RenderManager
[RenderManager] SphereMesh, TriMesh
```

* `@include FL_Dev.ini`: no `FL_Dev.ini` exists in `bin\` (nor does `EXE\` have one for the retail files); a missing include is ignored.
* `DebugLib.dll` is not built by this repository and is not in `bin\`; `AddLibrary` logs "unable to find dll" and skips it. Initialisation still succeeds because other entries load.
* No other sections are present, so the server runs every RP8, Shading and sound default of the sections above.

---

## 14. `luaprofile.dll` (alternative `IProfileParser`)

`luaprofile.dll` registers the component name `ILua` and implements `IProfileParser` on top of a private Lua 3.2 VM instead of an INI text file ([luaprofile.cpp](../../src/luaprofile/luaprofile.cpp)): `LoadFile(path, 0)` runs the file with `lua_dofile`; `OpenSection(name, 3)` opens the **global Lua table** of that name (the special name `$` opens the Lua globals themselves); `ReadLine` enumerates the table's string and number entries; `GetString(section, key)` returns the entry as text. No retail module creates `ILua`, `luaprofile.dll` is not in the dev `[Libraries]`, and it does not read any INI file; it is documented here only because it shares the interface.

---

## 15. Dev `bin\DACOM.INI` against the code

**Present and read** (all correct as far as the code is concerned): `[DACOM] IgnoreDACOMEnv` and `DllPath`; `[Libraries]` (absolute paths for `ReadFile`, `x86math`, `EngBase`, `system`, `RP8`, `SoundStreamer`, `SoundManager`, `Deformable2`, `Thorn`, `Shading`, `RendComp`, `alchemy`, `ximage`, `flmaterials`); `[System]` (`VMeshLibrary`, `DirectX8`, `SoundManager`, `SoundStreamer`, `TextureLibrary`, `MaterialAnimation`, `MaterialBatcher`, `Alchemy`, `FxRuntime`); `[Engine]` (`Animation`, `Hardpoint`, `Deformable`, `RenderManager`, `FLAppearance`, `VMeshWire`); `[RenderManager]` (`VMesh`, `TriMesh`, `SphereMesh`); `[TriMesh] tristrips`; `[RenderPipeline]` (`MGSDB`, `ALPHAREF`, `ALPHATESTENABLE`, `ALPHAFUNC`, `LOCALVIEWER`, `zfunc`, `HARDWARE_VERTEXPROCESSING`, `USE_SYSLOCK`, `HANDLE_SWAPLOSS`); `[SoundManager]` (`createAll2dInSoftware`, `3D_SW_Algorithm`, `use2DHW`, `use3DHW`, `maxSoundChannels`, `FORCE_FREQ_CONTROL_TO_SW`, `max3DPan`); all nine `[Alchemy]` keys; `[BatchedMaterials]` and `[MaterialMap]`.

**Note on the dev values.** `HARDWARE_VERTEXPROCESSING = true` has no effect beyond the default (hardware is chosen unless the software/mixed keys are set). `HANDLE_SWAPLOSS = false` and `USE_SYSLOCK = false` override the defaults 1 and 0 respectively. The `[Alchemy]` pool sizes override the 1000 default for six of the eight pools; `FxOrientedAppearance.poolSize = 0` removes that pool.

**Read by the code but absent from the dev file** (the defaults in this document apply): `[Deformable] DeformRate`; `[MaterialBatcher] BufferSize`, `Bin1SortOrder`; `[TextureLibrary]`; `[TextureFormatMaps]`; `[TextureFormatClasses]`; the whole `[AudioProperties]` section; in `[SoundManager]`: `deviceID`, `useNoHW`, `rightHandedCoordinates`, `speakerConfiguration`, `ALLOW_HW_TO_SW`, `ALLOW_SW_TO_HW`, `NEVER_SET_START_POS`, `DISPLAY_EXTRA_DEBUG_INFO`; in `[RenderPipeline]` every key not listed above (device flags, pipeline properties, ability overrides and the remaining render states).

**Present in the dev file but never read** by this code: nothing at section level. Of the retail file's commented lines, `DEVICE_GAMMA`, `LOCAKABLE_BACKBUFFER` (misspelt) and `Alchemy.maxVertices` / `Alchemy.maxIndices` are not read even if uncommented. The components named in `[System]` (`VMeshLibrary`, `MaterialAnimation`, `FxRuntime` among them) are only requested by name; if no loaded library registers a name, a "FAILED" notice is logged and the line is skipped.

**Not read from DACOM.INI at all.** The performance options of the client (`PerfOptions.ini`), `freelancer.ini` and all game data do not go through this parser; `JFLP.dll` and `HudShift.dll` must not be added to the dev `[Libraries]` (they patch fixed addresses of the retail binaries).

## See also

* [parsers.md](parsers.md): the two parsers (`IProfileParser` here, `INI_Reader` for game data).
* [freelancer-ini.md](freelancer-ini.md): `freelancer.ini`, whose `[Freelancer]` data path is the root of all game data.
* [client-audio-misc.md](client-audio-misc.md), [client-world-fx.md](client-world-fx.md), [server.md](server.md).
