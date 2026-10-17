# PE resources (string / HTML / VERSIONINFO)

Client texts (IDS, infocard HTML, versions) are stored in code-free resource DLLs that we rebuild from retail dumps. This document explains where the sources come from, how the encoding is handled and what counts as a resource project in the solution. The implementation is in `src/Resources/` (`import_decompiled_rc.py`, `ResourceDll.props`).

Client IDS live in separate code-free DLLs: `LoadLibrary("resources.dll")` from WinMain, then `[Resources]` in `freelancer.ini` (`LoadResourcesFromIni`). Lookup: `FindResourceExA(RT_STRING / RT_HTML)`.

## Where the files come from

Ghidra dump: `EXE/Disasm/DecompiledResources/<module>/<module>.rc` (+ sidecar HTML/ICO/BMP). **Ignore** the root `.rc` files in that folder (the dump overwrote same-named files).

Import into `src/`: `src/Resources/import_decompiled_rc.py` converts Resource Hacker VERSIONINFO to `rc.exe` form and copies the sidecars.

## Encoding

The importer reads the exported `.rc` honouring the BOM and writes our script in
UTF-8. `ResourceDll.props` sets `rc.exe /c65001` for all resource DLLs and all
configurations. The generated scripts also contain `#pragma code_page(65001)`,
because the code projects do not include this property sheet.

Non-ASCII Resource Hacker escape sequences denote character values:
for example, `\xFC` in the name Saarbrücken is U+00FC. The importer writes such
characters directly in UTF-8 instead of replacing them with single-byte octal escapes:
`rc.exe` decodes the latter through the code page, which under UTF-8 gives U+FFFD.

In the built PE, `RT_STRING` stores length-prefixed UTF-16. The client reads these
words directly; there is no UTF-8 conversion on this path. `RT_HTML` is taken
from UTF-16LE sidecar files with a BOM; the compiler includes them as binary data,
without changing the encoding.

The exported data also contain differences in wording, XML markup and language identifiers
from the retail PE files: a match with the sources verifies that the encoding is preserved,
not that the data is fully equivalent to retail.

## Solution

The solution is flat (folders in VS are created manually; do not name a folder `Resources` — VS confuses it with the `resources` project).

- **Resource DLLs** (`src/Resources/`, `/NOENTRY`) → `bin\`: `resources`, `InfoCards`, `MiscText`, `NameResources`, `EquipResources`, `OfferBribeResources`, `MiscTextInfo2`, `serverresources`.
- **Code:** DLLs + `Common.lib` + the exe. Code modules that had a dump have their own `.rc` (VERSIONINFO; Common also has a debug MENU/DIALOGEX, its VERSIONINFO is removed to avoid duplicating the exe's).
- **freelancer** — the client exe; `resources/freelancer.rc` (ICON 103 + VERSIONINFO).

`Content.dll` has no dump — VERSIONINFO was not added.

`serverresources`: the binary `RT_TOOLBAR` (type 241 + `.bin`) is not accepted by rc.exe as a TOOLBAR script — the resource is skipped, the rest is in the DLL.

The set of imported modules is defined by `CODE_MAP`, `RESOURCE_ONLY` and `SKIP_DUMP` in
the importer; the presence of a project in the solution does not by itself mean resources are imported.

## Responsibility boundary

Resource DLLs provide STRINGTABLE/HTML/VERSIONINFO. Loading goes from
`bin\`; the retail PE files in `EXE\` serve only as the RE reference, falling back to them is forbidden.
Interpretation of RDL, construction of NeuroNet controls and text display belong
to the client and Common; the presence of texts in a resource DLL does not prove that these
consumers are correct.
