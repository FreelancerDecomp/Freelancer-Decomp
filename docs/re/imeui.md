# imeui.dll

`imeui.dll` is the IME (Input Method Editor) user-interface layer for CJK and multilingual text in the game: caret, input buffer, composition/reading/candidate windows, colours, fonts and the input-focus manager on top of Windows IMM32/TSF. This document is for anyone touching input fields (chat, menu fields) or IME message handling. The port lives in `src/imeui/`, the public classes in `src/include/imeui.h` (whole classes are exported through `IMEUI_API`, no `.def` is needed); function addresses and statuses are in decomp-tracker (listing `EXE/Disasm/imeui.dll.txt`). Retail exports are checked with `dumpbin /exports EXE\imeui.dll` (85 names: constructors, destructors, assignment operators, vftables and class methods); the comparison is by class, method and arity, without VC6 decoration or ordinals.

## Contents

- `CCaret` — blinking caret. Field `+4` is a DWORD `BlinkTimestamp`, not an HWND; the constructor ignores the input HWND. `UpdateCaret` accounts for composition, show/blink, tick overflow and a strict excess of the blink interval.
- `CInputBox` — text buffer, DBCS/UTF-16 input, navigation (`MoveCaret*`, `AddChar`, `DeleteChar`), character filter, caption, margins.
- `IMEInputBox : CInputBox` — positioning of composition/reading/candidate windows, font (`LOGFONT`/`HFONT`), IME state and toolbar.
- `IMEFocus` — singleton: list of registered fields, message dispatcher (`WndProc`, `OnChar`, `OnKeyDown`, `OnLButtonDown`, `OnKillFocus`), colours (`_IMEUI_APPEARANCE`), `Draw`.
- `CImeUiFont_Base` / `CImeUiFont` — GDI font and string metrics; the base class has five slots (`SetHeight`, `SetColor`, `SetPosition`, `GetTextExtent`, `Draw`) and no virtual destructor. The separate `FSymbolFont` adds a sixth, `SetSymbol`; its empty height/position/draw and zero metrics are real retail behaviour.
- Internal engine (`ImeUiCore.cpp`): IMM32 (`ImmGetCandidateList`, `ImmGetCompositionString`, `ImmNotifyIME`, `GetReadingString`), reading of legacy Chinese IMEs, composition, candidates, TSF and hiding/restoring the language bar (`ITfLangBarMgr`); the status bar and `DrawFan` geometry are in `ImeUiToolbar.cpp`.

The virtual method tables of `IMEFocus`, `CInputBox` and `IMEInputBox` contain 12, 11 and 14 slots; the abstract draw/caret slots remain pure virtual. All public classes and the appearance structure are declared once in `src/include/imeui.h`; `CCaret.h`, `CInputBox.h`, `IMEInputBox.h` and `IMEFocus.h` forward to it.

## Contracts that are easy to break

- `_IMEUI_APPEARANCE` occupies `0x3c` bytes: three symbol colours, byte height/alpha/position, a font pointer, candidate colours (`+0x1c` is `CandidateColor`), five composition-attribute background colours, a byte alpha and the composition text colour (`+0x38`). String resources 1..16 and numeric parsing preserve the record widths; the resource ring is four buffers of `0x800` bytes each, and the `LoadStringW` limit of 2048 characters is kept as in the listing.
- The `IMEFocus` constructor stores only the **low byte** of the HWND in field `+4`; the copy ctor copies that byte, assignment leaves the previous one. List nodes are copied separately and pointers to fields are preserved. Constructor/destructor EH is implemented by an RAII list guard; the destructor allows an outgoing exception (`noexcept(false)`), as retail does.
- `IMEFocus::Draw` calls the virtual `Draw(Context)` of every registered field between the IME render passes. `WndProc` calls the core only when the list is non-empty, then performs size/activate/focus dispatch; `WM_ACTIVATE` uses `ImeUi_SetActive`.
- Resource initialisation starts with `LoadLibraryW` and falls back to `LoadLibraryA` on failure. The Windows 9x/MSLU loader is not carried over: the W/A branches of IMM32 and the legacy IME reading variants are restored, while compiler EH/deleting-destructor thunks, PE imports/exports and the NT loader are replaced by modern infrastructure.

## Preserved retail quirks

Shallow copying of buffers and the font pointer in `CInputBox`, fixed array limits and the `DeleteObject` call on a DC are not replaced by defensive checks. Operation with installed CJK IMEs has not been verified; the presence of code and a successful build does not prove it.
