# CRT/STL: behavioural compatibility with retail

The 2003 retail binaries link against the system `MSVCRT.DLL`, `MSVCP60.DLL`,
`CRTDLL.DLL` (VC6) or contain a statically compiled VC6 STL. The port is built with
modern MSVC and uses the UCRT and the modern STL. Calls of the same name
do not always behave the same way: number parsing and printing, rounding, container
capacity, the `operator new` failure path, argument validation and locale tables all differ.
Where a game value (a hash ID, text on the HUD, a Lua string key) depends on
such behaviour, the port reproduces the retail contract by **behaviour**, not by ABI.

This is not a return to the VC6 ABI. Binary compatibility with the retail DLLs is not a goal:
the `std::string` layouts, stream vtables, COW counters and private MSVCP60 methods
are not reproduced; our DLLs use the modern STL between them.

## Modules

| What | Where |
|---|---|
| `MsvcrtAtoi`, `MsvcrtWtoi` (+ `MsvcrtWideCharToDigit`, `MsvcrtIsWideSpace`) | `src/include/msvcrt_compat.h` |
| `MsvcrtAtof` (decimal branch, integer mantissa) | `src/Common/msvcrt_atof.h/.cpp` |
| `MsvcrtLegacyVsnprintf`, `MsvcrtFormatLuaNumber`, `MsvcrtNumericVsnprintf` | `src/include/msvcrt_numeric_format.h` |

Consumers: `MsvcrtAtoi` — every direct decimal integer parse in the game sources
(project rule: `atoi`/`std::atoi` are forbidden); `MsvcrtAtof` — Common `ValueNum`,
the decimal branch of `ParseNumber`, BINI string → float; `MsvcrtFormatLuaNumber` — `luaV_tostring`
in Thorn and luaprofile (`lvm.c`); `MsvcrtNumericVsnprintf` — `NumberStringChild::Printf` /
`FormatFloat` (`font_string.cpp`).

## Implemented contracts

- **`atoi`/`atol`/`_wtoi`**: the loop `total = 10*total + digit` with no overflow check,
  the result wraps modulo 2^32; UCRT saturates to `INT_MAX`/`INT_MIN`.
  The data stores 32-bit hash IDs in decimal notation (`ship_archetype = 2806013121`,
  mission target hash IDs in saves via `%u`). Saturation breaks them, and even the
  "zero / non-zero" checks. Whitespace for `atoi` is exactly `0x09..0x0d`, `0x20`; for `_wtoi` it is
  the set from `MsvcrtIsWideSpace`; the `_wtoi` digits are 18 Unicode decimal blocks
  (`_wchartodigit`). The tables are explicit and do not depend on the process locale.
- **`atof` (Common)**: the old grammar — `0x10`, `0x1p+2`, `inf`, `nan` yield 0 and
  stop parsing; modern parsing accepts hex and infinities. The implementation:
  up to 24 input digits, decimal powers with an extended mantissa, rounding before
  `double`, sign of zero, range and `errno`; independent of x87 precision. The power tables
  and the subnormal conversion are checked against `_ld12tod` of the system MSVCRT.
  `ParseNumber` has its own base-N parse — it cannot be replaced wholesale with `strtod`.
- **Lua number → string** (`sprintf("%.16g")`): the old CRT gives a three-digit exponent
  (`1e+020`), `1.#INF`/`1.#QNAN`, and the former 17 → 16 digit rounding. The legacy stdio options alone
  are not enough (14,888 differences on a random sample), so the formatter builds
  17 digits through `_ecvt_s`, rounds manually and prints by the `%g` rules.
  The Lua lexer and `luaO_str2d` parse literals with their own loop: the `strtod` risk
  does not apply to them.
- **HUD `%.Nf` with a literal suffix**: half values round "away from zero"
  (`sprintf("%.0f", 2.5)` = `"3"`, UCRT gives `"2"`; `%.1f` of 1.25 = `1.3` against `1.2`),
  significant digits and zero padding are unchanged. A single `%.Nf` with N <= 100 is covered;
  other formats go through the local UCRT legacy policy (`_CRT_INTERNAL_PRINTF_LEGACY_*`)
  with no promise of full compatibility of an arbitrary `printf`.

Measured coverage (`crt_numeric_regression`, `Release|Win32`): 20,025 `atof` comparisons
(`double` bits and `errno`), 300,009 Lua formatter cases, 2,988,419 HUD formatting cases,
41 bounded-buffer cases; 0 differences. These are finite samples, not a proof
for all inputs.

## Known UCRT / MSVCRT differences (measured)

| Case | MSVCRT (retail) | UCRT |
|---|---|---|
| `atoi("2806013121")` | -1488954175 | 2147483647 |
| `atof("0x10")`, `atof("inf")` | 0 | 16, Inf |
| `sscanf("1e+", "%lf", &d)` | 1, writes 1.0 | EOF, writes nothing |
| `sprintf("%.0f", 2.5)`, `"%.1f"` of 1.25 | `3`, `1.3` | `2`, `1.2` |
| `sprintf("%.16g", 1e20)` | `1e+020` | `1e+20` |
| Inf/NaN in `%g` | `1.#INF` / `1.#QNAN` | `inf` / `nan` |
| `strtoul("-4294967297", 0, 10)` | 1, ERANGE | 4294967295, ERANGE |
| `operator new(SIZE_MAX)` | null | `std::bad_alloc` |
| `_stricmp("a","z")` | -1 | -25 (sign matches) |
| `iswspace(U+0085)`, `iswcntrl(U+00AD)` | false | true |
| `iswctype` | mask differences at U+00AA/B5/BA, U+0085, U+00AD, U+0009/A0 | — |
| `strftime("%c")` in the C locale | `06/06/24 08:02:03` | `Thu Jun  6 08:02:03 2024` |
| `acos(0.5)` (scalar export) | differs by 1 ULP | — |
| `pow(NaN, 0)` | NaN, `EDOM` | 1, `errno` = 0 |
| `localtime` < 0 | NULL, `errno` not set | NULL, `EINVAL` |
| `_ftol` / out-of-range conversion | low 32 bits (4294967296 → 0) | `0x80000000` |
| `clearfp` | does not return SSE flags | returns a normalised status |

Matches (no differences found, must not be changed): `rand`/`srand` (4 seeds x 100,000 values),
`strtol` for valid strings, `isalnum`/`isalpha`/`isdigit`/`isspace`/`isxdigit`/`tolower`/`toupper`
in the C locale, `iswspace` except U+0085, `memcmp`/`strcmp` by sign, `qsort` (127 arrays),
`calloc` (failure on overflow on both sides), `_snprintf` (return and bytes for count 0..6).
Replacing `rand` with `mt19937` or `sscanf("%d")` with `strtol` is already a different mechanism.

## STL and internal contracts

For the statically compiled STL and MSVCP60, selected bodies were compared against the raw PE and
modern `std::`. Conclusions that matter for maintenance:

- **`vector<T>` growth**: retail takes `size + max(size, 1)`, the modern STL takes `max(new, cap + cap/2)`.
  Contents and self-alias insertion match; capacity and the size of the allocator request do not.
  Capacity on its own does not change game logic; code that depends on the `c_str()`/`data()` address
  after growth must be checked separately.
- **Allocation failure**: retail `operator new` returns null (MSVCRT), the modern one throws
  `bad_alloc`. On an exception VC6 `string::_Grow` repeats the allocation with `(request+2)*width`.
  A retail vector in the empty state with a null from the allocator skips the store and leaves wrong
  bounds; this corrupted state should not be copied as a "contract". Any other
  failure policy is a deliberate deviation, not a silent substitution.
- **`filebuf::setbuf`**: `setbuf(nullptr, 0)` in retail returns null with `EINVAL` (`_IOFBF` mode),
  the modern one returns `this` (unbuffered); `setbuf(buf, 16)` in retail keeps ownership of the borrowed
  `FILE`, the modern one closes it in the destructor;
  `setbuf(buf, 1)` invokes the invalid-parameter handler (process termination by default).
- **Imported data**: `npos`, `_Nullstr` — the numeric sentinel matches, the address of the empty string
  differs in the modern STL (this is string storage, not a defect).
- **Exceptions**: constructors, copying and destructors of `logic_error`/`runtime_error`/`out_of_range`,
  MSVCRT `exception`, `_Xran`/`_Xlen`, RTTI casts and `type_info`, `CxxFrameHandler`,
  `except_handler3`, `_EH_prolog`, `_CxxThrowException` — no behavioural differences were found
  in the selected areas.
- **Streams**: the virtual streambuf/filebuf slots (`overflow`, `underflow`, `xsgetn`, `xsputn`,
  `sync`...) matched in the selected scenarios; printing `float`/`double` through `ostream` changes
  only the text dump, not the binary network packets. The vbtable layouts differ
  (offset 88 against 104) — this has no game significance, no ABI is assumed.
- **Time**: `time`, `_ftime`, `clock`, `_strdate`, `_strtime` matched; `_daylight` after `_tzset`
  on the old side is a zone code, not a boolean (the boolean semantics match).

## Open consumer defects at the time of the audit

The list is a hint for cross-checking against the tracker, not a source of statuses; the current state
is in `decomp-tracker` and `deviations.md`.

- `RichTextEditWin::OnChar` (`0057ceb0`): UCRT `iswcntrl(U+00AD)` is true, retail goes on to the filter and the insertion.
- `Thorn::io_date` (`06f55f30`): `strftime("%c")` gives a different format by default.
- Common `get_value_int` (`063107c0`): retail takes the low 32 bits of `_ftol`, the port does `static_cast<int>`.
- Common `FUN_06328f20` / `SplitTextAtSize`: `iswspace(U+0085)` changes the wrap boundary.
- Allocation failure in the rpclocal decoders (`06da5550`, `06da7b90`); `setlocale` in Thorn accepts
  modern aliases (`en-US`, `.UTF8`), the old side refuses.
- The reachability of each difference in the shipped data and scripts is not proven.

## How it was measured and is re-checked

**Two current runtimes on the same machine** were compared, not the 2003 CRT: the system 32-bit
`C:\Windows\SysWOW64\msvcrt.dll` (7.0.19041.x), `msvcp60.dll`, `crtdll.dll` against MSVC 19.44 /
UCRT (`/MD`, `/arch:IA32`, `/fp:precise`). The retail PE and listings only say *which* CRT interface
was called and *which* values reach it; the system MSVCRT was updated together with Windows
and is not declared an unchanged original. For inline bodies the raw retail PE was used, not the decompilation:
CRT calls in `EXE\Disasm\*.txt` are not visible if Ghidra gave no name (indirect, computed).

The method for a new entry (a CRT/STL function imported by a retail module):

1. Find the imports in the PE (`dumpbin -imports EXE\<mod>.dll`) and the calls in the listing `EXE\Disasm\<mod>.txt`;
   a missing `XREF` in the listing does not prove the call is absent — scan the raw bytes for the IAT slot and thunk.
2. Write a separate Win32 exe (a real MSBuild, not `cl /Zs`) that loads the system
   MSVCRT/MSVCP60 only as a reference and compares it with the modern side on vectors: boundary values,
   `errno`, `endptr`, state. Do not pass objects of one side to the other and do not mix their `FILE`/heap.
3. Analyse the specific consumer in the retail listing (the CALL address, what happens to the result).
   A difference without a reachable consumer is not called a game defect.
4. If the consumer depends on the difference, add a helper in `src/include/msvcrt_*.h` (or `src/Common`),
   lower the function's status in the tracker
   for the duration of the work, then raise it by the listing.

The numeric regression test, the one-off probe programs and the CSV/JSON registries of the 2026-10 audit
are not part of the published repository.

## Unresolved

- Coverage is not exhaustive: unnamed, inline and computed CRT/STL calls, ~170 local STL bodies,
  the full locale/DBCS surface, behaviour under `new_handler` and real out-of-memory.
- Invalid-parameter handler policy: the modern CRT may terminate the process on inputs
  that MSVCRT silently accepted; checks cannot be disabled globally nor everything moved to `_s` variants.
- `clock` became monotonic (QPC), `time_t` is 64-bit: units and serialisation must be checked at the consumer,
  not by restoring the VC6 layout.
