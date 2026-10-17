# zlib.dll

`zlib.dll` is a third-party compression library (zlib + MiniZip), not a DACOM component. In retail only `DALib.dll` uses it (compression of DirectPlay network packets). This document explains why our port is a vendored zlib 1.3.1 with a special ABI, and what must not be changed in `src/zlib/`. The port is `src/zlib/` → `bin\zlib.dll`; the zlib and MiniZip sources are in `src/zlib/vendor/`.

## What the retail DLL is

Standard zlib 1.1.4 (the Windows build by Gilles Vollant / Alessandro Iacopetti, 2002) plus MiniZip (`unz*`/`zip*`), without Digital Anvil wrappers. It imports the CRT and KERNEL32; `DllMain` is the ordinary CRT one, with no game initialization. `zlibVersion()` returns `"1.1.4"`, and `deflateInit2_`/`inflateInit_` accept only a version that starts with `'1'` and `stream_size == 0x38` (56 bytes).

**The calling convention is `__stdcall` (WINAPI), not cdecl**, unlike the standard `zlib1.dll`. Export names are undecorated (set by the `.def`); `gzprintf` is varargs (cdecl). Retail `DALib` calls `deflateInit_` with four pushes and no `ADD ESP` — a cdecl build would corrupt the stack.

## Export surface

Ordinal base 1, 87 slots, 65 named exports; the empty (RVA 0) ordinals 42–60, 69, 79, 85 are reserved and **are not filled**. The zlib 1.1.4 core is ordinals 1–41, MiniZip is 61–87 (2002 signatures: `unzGetCurrentFileInfo` takes 8 arguments, `zipOpenNewFileInZip` 10, `zipOpenNewFileInZip2` 11). No extra APIs (`inflateBack`, `compressBound`, `*64`) are exported. Check: `dumpbin /EXPORTS EXE\zlib.dll` against `bin\zlib.dll` must give identical names and ordinals.

`DALib` imports functions **by ordinal** (4 `deflate`, 6 `deflateEnd`, 8 `deflateInit_`, 10 `deflateReset`, 19 `inflate`, 20 `inflateEnd`, 22 `inflateInit_`, 23 `inflateReset`); the ordinals must not change. The retail consumer is the network codec class (`FUN_065c1000`): two consecutive `z_stream`s (deflate at level 6 and inflate), one-shot calls with `Z_FINISH` followed by `Reset`; compression rejects input shorter than `0x20` bytes.

## Porting principle

Do not rewrite inflate/deflate; vendor a compatible C implementation instead:

- zlib 1.3.1 + MiniZip from the same archive: the bit stream is compatible with 1.1.4, `z_stream` is still 56 bytes on Win32, `inflateInit_` accepts `"1.1.4"`.
- Built with `ZLIB_WINAPI` (`ZEXPORT` = `WINAPI`), without `ZLIB_DLL` (so that `__declspec(dllexport)` does not conflict with the `.def`).
- `src/zlib/zlib.def` sets the undecorated names and exact ordinals; `zlibVersion` is exported as an alias of `FreelancerZlibVersion` (`src/zlib/zlib_version_compat.c`), which returns `"1.1.4"`.
- Known discrepancies: `FILEVERSION` in `zlib.rc` is `1.3.1.0` (retail has `1.1.4.0`; nobody reads it); `gzFile` is the opaque 1.3.1 pointer, not `FILE*`; MiniZip internally has Zip64 types, but the exported 32-bit structures keep the 2002 layout.

Our `DALib` (`src/DALib/CDPNetCodec.cpp`) is built with `ZLIB_WINAPI`, includes `vendor/zlib.h` and links against `zlib.lib`. The DLL is not loaded through `[Libraries]` — only as a PE dependency of `DALib`. Do not put it on the search path of applications that expect a cdecl `zlib1.dll`.
