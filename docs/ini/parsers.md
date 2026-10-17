# How INI files are read

Freelancer reads configuration through two separate INI parsers: `INI_Reader` in Common (used for all game data, `freelancer.ini`, save games), and the DACOM `IProfileParser` built into `dacom.dll` (used only for `DACOM.INI` / `dacomsrv.ini`). This page describes both parsers, how file paths are resolved, and the conventions the other pages in this folder use to describe keys.

The project never calls the Win32 profile API (`GetPrivateProfileString` and friends); there are exactly two parser implementations. `luaprofile.dll` implements the same `IProfileParser` COM interface on top of Lua globals, but it is not an INI reader.

## Which parser reads what

| Parser | Used for | Implemented in |
|---|---|---|
| `INI_Reader` (text and BINI) | `freelancer.ini`, every file listed in its `[Data]` section, every file found by name inside another INI, `InitialWorld.ini`, `.fl` save and template files (text only) | [common_ini.cpp](../../src/Common/common_ini.cpp), [common_textfile.cpp](../../src/Common/common_textfile.cpp), declared in [common_ini.h](../../src/include/common_ini.h) |
| DACOM `IProfileParser` | `DACOM.INI` (client) and `DACOMsrv.ini` (server), plus the engine modules that query it for their own sections (see [engine-dacom.md](engine-dacom.md)) | `FIniLexer` / `FIniDatabase` in [dacom.cpp](../../src/dacom/dacom.cpp) |

Modules that never read INI files at all: ximage, x86math, ReadFile, RemoteClient, RemoteServer, RemoteCommon, rpclocal, Editor, Thorn, IVP, qhull, zlib, MemoryDiagnostics, SoundStreamer, flmaterials, imeui, movie (no `INI_Reader` or `IProfileParser` use in their sources).

Two more INI-shaped inputs are *not* read by either parser:

* `flconfigdatabase.txt` (RP8 hardware-quirk list, path from `[RenderPipeline] MGSDB` in `DACOM.INI`) has its own private parser in `src/RP8` and its own syntax.
* Output-only files: the client writes `PerfOptions.ini`, the server writes `.fl` saves, and Content generates three path tables (see [data-driven-files.md](data-driven-files.md) and [saves-and-initial-world.md](saves-and-initial-world.md)).

## `INI_Reader`

### Opening a file and detecting the format

* `open(path, bFatal)` maps the file into memory. If the first 12 bytes form a valid `BINI` header (magic `BINI`, and `StringTableOffset` smaller than the file size) the reader switches to the binary half; anything else is read as text. There is no extension check and no separate "open as text" call. The `Version` field is not validated (retail data files carry `1`).
* A file that cannot be opened returns `false`; with `bFatal = true` the reader first calls the host's fatal-error hook (error code 200). Every call site in the port passes `false` except the key-map loader in the client.
* `open_memory(buffer, bytes)` opens a copy of a memory block and is **always text**; BINI data is never recognised there. It is used for `.fl` save/template contents after they have been decoded (see [saves-and-initial-world.md](saves-and-initial-world.md)). A null buffer or zero length returns `false`.
* One reader object can be rewound with `reset()`, searched with `find_header(name)` (which rewinds first), and its position can be saved and restored with `get_state` / `set_state`. Server code uses the latter to scan a save for `[Time]` and then continue from where it was.
* In the whole data set, 1251 of the 1252 INI files in the retail `DATA` folder are BINI; only `InitialWorld.ini` is text. `DATA` is not part of this repository.

### Iteration model

Every reader function in the project follows the same shape:

```
INI_Reader Reader;
if (Reader.open(Path, false))
    while (Reader.read_header())          // next [Section]
        if (Reader.is_header("Name"))
            while (Reader.read_value())   // next key = value line of this section
                if (Reader.is_value("key"))
                    ... Reader.get_value_*(index) ...
```

* `read_header()` advances to the next section and returns `false` at end of file. Values of the previous section that were not consumed are skipped.
* `read_value()` advances to the next key of the current section and returns `false` when the section ends. In text mode the section ends at the next line whose first non-blank character is `[`, which is left for the following `read_header()`.
* Some readers do not test the section name or key name at all and read **positionally**: "first value line is X, second is Y". Those places are called out in the per-file pages (for example the `[Group]` blocks of `InitialWorld.ini`).
* Matching with `is_header` / `is_value` is **case-insensitive** (DACOM's `stricmp`). Repeated sections and repeated keys are all delivered, in file order; whether a later one overrides an earlier one or they accumulate is decided by each reader and noted on the page for that file. There is no built-in default mechanism: "default when missing" means whatever the destination variable was initialised to.
* Keys that no reader recognises are silently ignored in most readers (a few log "unknown ini parameter", which is compiled out in the retail binary and in the port).

### Text grammar

| Element | Rule |
|---|---|
| Line ending | `\n`, `\r`, `\r\n` or `\n\r`. A line is read in a window of at most 1023 bytes; a longer line is split and the rest is treated as the next line. |
| Blank characters | Only space and tab are skipped or trimmed. |
| Section | A line whose first non-blank character is `[`. The name is everything up to the first `]` (trailing blanks trimmed); anything after `]` on the line is ignored. A section whose name starts with `;` (for example `[;TextureLibrary]`) is treated as commented out and skipped. Lines before the first section are ignored. |
| Key / value | `name = params`. The name runs up to the first `=` or `;` and is trimmed, so names may contain spaces (`data path`). The parameter string is everything after the `=` up to the first `;`, with leading and trailing blanks trimmed. A line without `=` is a key with empty parameters (this is how the dev `freelancer.ini` writes `bases`, and how `.fl` templates write `%%PACKAGE%%`). |
| Comments | `;` starts a comment: at the start of a line (after blanks), or after a value. Because `;` ends the parameter string, a value can never contain `;`. There is no `//` or `/* */` comment syntax in the parser (the `.fl` loaders add a pseudo block comment of their own, see the saves page). |
| Quoting, escapes, continuation | None. Quotes are ordinary characters (the one exception is the char literal in number parsing, below). |
| `@include` | **Not supported** by `INI_Reader`. Only the DACOM parser has it. |
| Maximum sizes | Section name, key name and parameter string are each held in a 0x400-byte buffer. |

### Parameters and getters

Parameters are comma separated; the getters take a zero-based position. Leading and trailing blanks of each field are trimmed. A missing field gives `""` / `0` / `0.0` (text) — in BINI mode a missing field additionally pops up a `MessageBox` titled "ERROR" ("header [..] value .. missing parameter N") and then returns the same neutral value.

| Call | Text file | BINI file |
|---|---|---|
| `get_num_parameters()` | 0 for an empty parameter string, otherwise 1 + number of commas | stored parameter count |
| `is_value_empty(i)` | field `i` is empty or absent | no value or `i` out of range |
| `get_value_string(i)` | field `i` as text | parameter `i` formatted as text (ints as `%d`, floats as `%g`) |
| `get_value_string()` | the whole parameter string | all parameters joined with `", "` |
| `get_value_int(i)` | MSVCRT `atoi` of the field | byte/int as is, float truncated, string through `atoi` |
| `get_value_float(i)` / `value_num(i)` | `ParseNumber` of the field (below), returned as `double` | byte/int/float converted, string through `atof` |
| `get_value_bool(i)` | see below | byte/int non-zero, float non-zero, string `true`/`false` else `atoi != 0` |
| `get_value_wstring()` | whole parameter string widened with `mbstowcs` | joined parameters widened |
| `get_vector()` | `get_value_float(0..2)` | same |
| `get_line_ptr()` | `"key = params"` | `"key = joined params"` |

Notes:

* `get_value_string` returns a pointer into a shared static buffer; callers must copy before the next getter call. Several readers rely on this (for example the `[Error] log` reader copies the name first).
* `get_value_int` uses an `atoi` that **wraps modulo 2^32** like the 2003 MSVCRT, so hashed IDs written as large decimals (for example `2926089285`) round-trip into an `unsigned int`. It does not understand hex (`0x10` reads as 0) and ignores a fractional part (`1.5` reads as 1).
* The BINI file header lets typed values round-trip: integer, float and string parameters are stored with their type, so none of the text-number rules below apply to BINI data.

### Number parsing (text)

`get_value_float` / `value_num` run the field through `ParseNumber`:

* An optional `+`, then an optional `-`.
* `0x`/`0X` prefix: hexadecimal, optionally with a `.` fraction.
* `'c'`: a character literal, the value is the character code (the sign is ignored).
* A trailing `d` (decimal) or `b` (binary) after a run of digits, or a single hex digit followed by a lowercase `h`, selects another base. Otherwise the field is parsed as a decimal with the C `atof`, so `.5`, `-2` and `1e3` work.
* Out-of-range digits for a chosen base produce a warning and stop the parse.

The standalone function `ValueNum(text, index)` in the same file is different: it uses plain `atof` (so `0x10` reads as 0). Reader code that needs `ParseNumber` behaviour goes through `INI_Reader::get_value_float`.

### Boolean parsing (text)

`get_value_bool` accepts exactly `true` or `1` (true) and `false` or `0` (false), case-insensitively. Any other word (`yes`, `on`, empty) logs "Invalid bool value ... in .ini file" through the error handler and returns `false`. This is why a boolean key with no value is `false` plus an error line.

### Known hazards

* Retail's shared buffers are fixed-size (0x100 bytes for the BINI join and line buffers, 0x400 for text names); retail overruns them on very long values. The port enlarges or bounds some of them, so extremely long values are an unsupported area rather than a contract.
* On a BINI reader, `is_value`, `get_name_ptr` and `get_num_parameters` called while no value is current (before the first `read_value`, or after it returned `false`) dereference null, in retail and in the port. The typed getters (`get_value_*`) return a neutral value instead.

## DACOM `IProfileParser`

The DACOM component manager owns a built-in INI database. It is loaded exactly once at start-up and then queried through the COM interface `IProfileParser` (obtained with `QueryInterface` on the manager). It is used by the engine DLLs to read their own sections of `DACOM.INI`.

| Aspect | Behaviour |
|---|---|
| Tokens | Words are separated by space, tab, CR, LF and the characters `[ ] ; =`. `;` starts a comment that runs to the end of the line. `[`, `]`, `=` are single-character tokens. |
| Sections | `[` words `]`; the words are joined with single spaces. Sections are kept sorted case-insensitively. **Duplicate sections are kept**; a lookup returns the first one that was added. |
| Lines | `key = value`. Keys and values are phrases: the words are re-joined with single spaces, so tabs and repeated blanks collapse. If several words precede the `=`, the key is "first word + space + remaining words". A line without `=` is stored as a bare key with no value (this is how the `[Libraries]` DLL names and the `[System]` component names are stored). `=` and `[` inside a value are kept as text. |
| Lookup | `OpenSection(name, 3)` returns a handle (only flag value 3 is answered). `GetString(handle, key, buf, n)` returns the **first** key in that section that compares equal case-insensitively and gives its length (0 when missing or empty). `ReadLine(handle, i, buf, n)` returns the i-th line (0-based) as `key = value` or the bare key, so repeated keys and bare lines are only reachable this way. |
| `@include <file>` | Parsed inline. A relative path is resolved against the directory of the file that contains the directive; an include that cannot be opened is silently ignored. |
| Errors | Malformed input does not produce messages. A line that begins with `=` or `]`, or an over-long phrase, hits a `__debugbreak()` as in retail. A `key = value` line before the first `[section]` dereferences a null section and crashes, as in retail. |
| Value types | The parser only stores text. Each module converts values itself. Typical copies of the helper accept `true`/`yes` and `false`/`no`, treat text containing `x` as hexadecimal, and otherwise use `atoi`; see [engine-dacom.md](engine-dacom.md) for what each module accepts. |

Loading order of the configuration file (`DALib::Startup(hwnd, iniPath)`, see [DALib.cpp](../../src/DALib/DALib.cpp)):

1. An explicit path wins: the client passes none, the server passes `DACOMsrv.ini`.
2. Otherwise `DACOM.INI` in the current working directory if it is readable.
3. Otherwise `DACOM.INI` in the directory of `DALib.dll`.

If loading fails, start-up stops with "ERROR: creating DACOM with INI file". After parsing, the DACOM manager reads `[DACOM]` (`DllPath`, `IgnoreDACOMEnv`) and loads every line of `[Libraries]`. A `DllPath` that is not absolute is resolved against the **executable's** directory (leading `.\` and `..\` are handled). Unless `IgnoreDACOMEnv` is true (`true` or any non-zero number; `false` or `0` keeps the environment variable active), the environment variable `DACOM` replaces `DllPath` if it is set. The full key list is in [engine-dacom.md](engine-dacom.md).

## How file paths are found

| Mechanism | What it does |
|---|---|
| Config file location | Client: `Freelancer.ini` in the current working directory (compiled-in name, no command-line override in the code). Server (`FLServer.exe`): `freelancer.ini` in the directory of the server executable, and a second file `SrvLocal-freelancer.ini` next to it that is read with the same reader. The single-player server (loaded into the client process) is handed the client's path. |
| Data path | `[Freelancer] data path` of `freelancer.ini` (client default `..\Data`, the server key is spelled `Data path`; keys are case-insensitive). `DALib::OpenData(path)` creates the *Data file system*, a DACOM file system rooted at that directory. The string is used as given (a trailing `\` is ensured); a relative value is therefore interpreted by the operating system against the working directory of the process. |
| `GetFileSysPath(out, rel, fs)` | If `rel` is absolute (starts with `\`, or has `:` as its second character) it is copied unchanged. Otherwise `rel` is joined to the root of `fs`; `fs == nullptr` means the Data file system. This is how every file named in `[Data]` is found. |
| `GetFullFilename(rel)` | Same join against the Data file system, then expanded to a full path. Used by the shape/texture-panel loaders. |
| `OpenContentDataIni(rel, reader)` | Content module helper: root of the Data file system + `rel`, then `INI_Reader::open(path, false)`. Used by the mission and AI loaders (38 call sites in `src/Content`). |
| Universe-relative files | `Universe::Startup` creates a second file system rooted at the directory of `universe.ini`; system files named by `[System] file` are joined to that root (`Systems\<Sys>\<Sys>.ini`). Other references (base files, room files, asteroid fields ...) are written relative to the Data root. See [systems-and-bases.md](systems-and-bases.md) and [data-driven-files.md](data-driven-files.md). |
| User data | Saves live under `<My Documents>\My Games\Freelancer` (`GetUserDataPath`), local per-user data under the local application-data folder (`GetUserLocalDataPath`). |
| Beside the executable | Some files are read from the directory of the running executable rather than from Data: the new-character templates (`NewCharacter.fl`, `MPNewCharacter.fl`, `NewPlayer.fl`) and `flconfigdatabase.txt`. |

Relevant sources: [common_misc.cpp](../../src/Common/common_misc.cpp), [ContentDataPath.cpp](../../src/Content/ContentDataPath.cpp), [DALib.cpp](../../src/DALib/DALib.cpp), [startup_config.cpp](../../src/freelancer/Client/startup_config.cpp), [ServerControl.cpp](../../src/FLServer/ServerControl.cpp).

## Conventions used in the other pages

* A **key table** has the columns `Key | Type | Default | Meaning / allowed values | Source`. *Type* is one of `string`, `int`, `float`, `bool`, `id` (a nickname string that the reader hashes to a 32-bit ID with `CreateID`, or a number that is used as the ID directly), `path` (file name relative to the Data path unless stated), or a list in brackets such as `[float, float, float]` for comma-separated positional parameters (parameter 0, 1, 2 ...).
* *Default* is the value the destination variable holds when the key is absent. "none" means the reader has no initial value of its own, "not validated" means the code accepts anything and does not range-check or enumerate it.
* "Allowed values" are only listed when the code compares against a fixed set (`stricmp` chains, tables, enums). Where the code does not validate, the page says "not validated" rather than guessing.
* **Repeated** means the key may appear several times in one section and every occurrence is used; **last wins** or **first wins** is written explicitly when a later or earlier occurrence replaces the rest.
* **Source** is a relative link to the file that contains the reader. Names of functions are given when a file has several readers.
* Examples are short and synthetic. The retail `DATA` folder is copyrighted and is not part of the repository, so the pages describe formats and keys instead of reproducing data.
* All INI keys and section names are case-insensitive unless a page says otherwise. Nickname strings are lower-cased by `CreateID` before they are hashed (names longer than 255 bytes are truncated), so IDs derived from nicknames are case-insensitive too. Other value comparisons (`stricmp` or `strcmp`) are noted where they matter.
