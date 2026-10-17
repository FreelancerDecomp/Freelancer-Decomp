# ReadFile.dll

`ReadFile.dll` is the game's file subsystem: the `FileSystem` component in three implementations (DOS — a Win32 directory/file, UTF — a resource archive `.cmp/.3db/.vms/.txm/.sph…`, MEM — an in-memory buffer) and the `SearchPath` component (a list of roots separated by `;`). Almost all `DATA` reading goes through it, starting with `DALib::OpenData`. This document describes the object creation contracts and the formats; the behaviour of individual functions and their statuses are in decomp-tracker (listing `EXE/Disasm/readfile.dll.txt`). The port is `src/ReadFile/`, the public ABI is [src/include/readfile.h](../../src/include/readfile.h) (`IFileSystem`, `ISearchPath`, `IDataBuffer`, `FFileSystemDesc`); the rules are in [cross_dll_protocol.md](cross_dll_protocol.md). Many `IFileSystem` slots have no official names in the binary: unnamed slots stay `SlotNN`.

## Registration

There are no PE exports, only `DllMain`: on attach it registers four factories (priorities as in retail): the DOS singleton (`"FileSystem"`, `0x40000000`), an empty UTF object (`"FileSystem"`, `0x80000000`), the MEM factory (`"FileSystem"`, `0x80000000`) and the `SearchPath` factory (`"SearchPath"`, `0x80000000`). COManager matches the name from the descriptor and tries the factories in order until one returns `GR_OK`; the implementation is selected by the descriptor's `Implementation` field. The IIDs answered are: `1.11_IFileSystem` (DOS/UTF/MEM), `1.11_IDataBuffer` (the MEM buffer and UTF nodes), `1.11_ISearchPath`, `1.11_IComponentFactory`; IID comparison is bytewise. `DllMain` detach releases the saved `ICOManager` and does not call `UnregisterComponent`.

## `FileSystem` descriptor (`CreateInstance`)

Size `0x30`/`0x34` (DOS, UTF) or `0x40` (MEM); `SearchPath` is `0x8` (size + name).

| Offset | Field |
|----------|------|
| `+0x00` / `+0x04` | size / name (`"FileSystem"`) |
| `+0x08` | implementation: `"DOS"`, `"UTF"`, `"MEM"` (NULL is allowed for UTF) |
| `+0x0C` | path |
| `+0x10…+0x24` | `CreateFile`: access (`GENERIC_READ`), share, security, disposition (`OPEN_EXISTING`), flags, template |
| `+0x28` | parent (IFS), when opening a child |
| `+0x30` | "find" identifier (only with size `0x34`), `0xFFFFFFFF` means none |

`DALib::OpenData` creates a DOS object for the data directory (size `0x34`, `"DOS"`). The DOS factory: a directory → a new DOS object (`0x170` bytes); a file with `"DOS"` → `CreateFileA` (+ memory mapping); a file with any other implementation → a repeated `ICOManager::CreateInstance` with `desc+0x28 = this`, where the UTF factory matches `"UTF"`. An empty path string is not rejected (only NULL is): the path is resolved relative to the root of the DOS object.

MEM (`0x40`): `desc+0x28 == 0`, implementation name `"MEM"`; when `desc+0x3C` bit0 = 0 the buffer is allocated with `VirtualAlloc` of size `desc+0x38`, otherwise the external pointer `desc+0x34` is wrapped. QI returns `IFileSystem` and `IDataBuffer`.

## UTF archive format

The header is `0x38` bytes: `+0x00` magic `"UTF "` (`0x20465455`), `+0x04` version `0x101`, `+0x08` offset of the node tree, `+0x14` node record size (`0x0C` or `0x2C`, otherwise error `0xFFFFFFFA`), `+0x18` offset of the string block, `+0x24` offset of the data section; the offsets must be smaller than the file size, and the file size must be > `0x38`. The remaining header fields are not read.

- **Node `0x2C`** (all 2557 `DATA` files of this installation use exactly this one): `+0x00` offset of the next sibling (a linked list, the terminator is the tree base), `+0x04` name offset in the string block, `+0x08` flags (bit `0x10` — directory), `+0x10` offset of the first child (directory) or of the data (leaf), `+0x1C` data size, `+0x20/+0x24/+0x28` packed DOS date/time (converted with `DosDateTimeToFileTime`).
- **Node `0x0C`**: siblings are consecutive records; `+0x00` bit 31 — directory, string = string block + `(value & 0x7FFFFFFF) + 4`; `+0x04` offset of children/data; `+0x08` child count/size.
- Name lookup is `stricmp`; the path is split on `\` and `/`, a leading separator is skipped; a leaf does not allow descending. Reading: the file offset = `header[+0x24] + node.data_offset + position`.
- Records are read straight from the mapped file; the tree is not materialised in memory. The "current node" descriptor (`OpenNode`) is stored in the object; handle 0 in `Read`/`GetSize`/`Seek` means the object's root handle.
- `OpenNode(".")` looks for a node with the literal name `"."`; if there is none, the call fails and the current node does not change (the parent is not returned). To go up, retail passes `".."` — callers (EngBase hardpoint) must do the same.
- A nested clone (`CreateInstance` on an already opened UTF object) opens a subtree by path; it keeps the parent and the topmost archive with `AddRef` and reads through its mapping.

## General rules

- **Path joining** (`FUN_06b75420`, used by both DOS and UTF `+0xA4`): an absolute path (leading `\`, `/` or `X:`) is copied as is; otherwise a separator is appended to the root, a leading `.\` is dropped, each leading `..` removes the previous component; on buffer overflow it fails. Known retail UB: a root of length `OutChars-1` without a trailing separator writes the terminator past the buffer; the port's callers provide a buffer one byte larger.
- **Masks** for UTF search: `*` — the rest of the pattern, `?` — one character, comparison is case-insensitive, `\` ≡ `/`; a pattern without a dot gets `.*`, a pattern starting with `.` gets a `*` prefix, a name without a dot gets a trailing `.`. The folding table is indexed by a signed byte: characters `0x80…0xFF` read the 128 bytes of `.data` before the table, and the port copies them from the PE. DOS `FindFirst` does **not** go through this matcher: it calls Win32 `FindFirstFileA` on the joined path.
- **DOS `FindFirst` is broken in retail**: the result of the path join is checked "backwards", a successful join yields `-1` / `LastError 0xA1`; the port reproduces this. The working search is UTF (EngBase calls it with the mask `"*"` on the `Cmpnd` node); it returns a 1-based search slot number, `FindNext` fills `WIN32_FIND_DATA` with the node's flags, time, size and name.
- The DOS "write" slots are refusal stubs (retail has no `WriteFile` import); writing is really done by MEM (`Write` + buffer growth rounds the end up to 64 KB).
- Memory-mapping slots `+0x3C/+0x40/+0x44`: for DOS this is not a new `CreateFileMapping`/`MapViewOfFile` but a token over an already existing mapping (`protect` must be `2`, `access` must be `4`, otherwise error `5`); `+0x44` does not call `UnmapViewOfFile`. For a nested UTF the token yields a pointer into the parent's mapping; a top-level UTF refuses.
- **`SearchPath`**: `SetPath` splits the string on `;` (empty segments are skipped) and opens the entries from right to left; each entry is cut at its first component (`\\server\share`, `X:\dir`, or up to the first separator), opened as a DOS object, and the remainder is taken relative to it. `CreateInstance` tries the children in order: the initial result is `-6`, `0` is returned immediately, otherwise the result of the last child. `GetPath` copies the stored string (buffer `0x108`).

## Port deviations and deliberate gaps

- Containers are `std::map`/`std::vector` instead of VC6 trees; MEM uses `FMemDataBuffer` composition instead of MI adaptors.
- The UTF allocator of the `0x0C` walker is not reproduced 1:1 (there are no `0x0C` formats in the installed data).
- Some paths reproduce retail UB deterministically (dangling tokens of closed files, unbounded copies onto the stack, sign extension in `isalpha`); the per-function list is in the tracker notes.
- A lesson worth remembering: changing an interface's calling convention breaks the ABI for **every** caller. A partial rebuild (`Shading`, `freelancer`, `ReadFile` at different times) once produced a crash into a null vtable slot, indistinguishable from "the slot is missing".
