# Cross-DLL protocol: how to analyse a foreign vtable call

The Freelancer client is ~30 modules that talk to each other through COM-like DACOM interfaces.
A listing of any module constantly contains calls such as
`(**(code **)(*(int *)param_1 + 0x1c))(...)` on an object from a neighbouring module. This document is the
**method**: how to find out which class and slot that is, and how to record the result so the work
does not have to be repeated. The facts about specific interfaces and slots (names, arity, calling
convention, vtable binding) live in decomp-tracker, not here.

## 1. How to analyse a foreign vtable call

From cheap to expensive; do not jump straight to the last steps.

1. **Whose address it is.** The module address ranges, their listings and our port directories are in
   [abi/modules.md](abi/modules.md).
2. **Whether we already have a declaration.** [abi/symbols.md](abi/symbols.md) lists every type and where
   it is declared (public/private); the declaration in `src/include/<module>.h` gives the slot order
   (taking base classes into account).
3. **The interface catalogue in decomp-tracker** (`list_interfaces` / `get_interface`,
   `list_vtables` / `get_vtable` for the *foreign* module).   `get_vtable` can undercount slots — check the table size against the `.txt`.
4. **The foreign module's listing** (`EXE/Disasm/<module>.txt`): the whole vtable block, by byte
   extent. Remember: retail DLLs are not loaded by us, the listings are the reference for *behaviour*, not
   for ABI; headers only have to agree between our caller and our callee.
5. **Live debugging** — only when 1–4 did not help, and only with the owner's consent.

## 2. What the person who worked it out must do

- **Named a slot** — record it in the catalogue (`update_interface_slot`, `upsert_interface`). If the
  name is unknown, leave `SlotNN` and a `// +0xNN` comment; names are not invented.
- **A type crosses a DLL boundary** — it is declared **once** in the flat
  `src/include/<module>.h` (models: `src/include/dacom.h`, `src/include/readfile.h`);
  `src/include` is already on every project's include path via `Directory.Build.props`.
  Do not create `src/<Mod>/include/`.
- **Never declare a foreign interface with a body of your own** — only `#include` it. Two bodies of one
  interface mean a slot order that has drifted apart and a call into the wrong method.
- **Do not call a slot by number** (`Vtable[5]`) if you can call a method. While raw and typed calls
  live on the same table, the indices in the header must match the numbers in the raw
  lines; convert an interface **all at once**.
- The public ABI is interfaces, descriptor structures and IIDs, not the whole internal
  header: names needed only by the module are not exported (constant collisions between DLLs).
- Signatures in headers use modern types (`std::string`, `wchar_t`), `DACOM_API`/`__stdcall` on every interface
  method, no `virtual ~IFoo()`. There is no need to bend types toward VC6 mangling.
- A module is not finished while a virtual type that other modules reference is still declared
  privately inside `src/<Module>/`, or while one type has two declarations with different slot
  counts. This complements the tracker statuses, it does not replace them.

## 3. IID strings — only `Iids::`

The string `"1.11_IFoo"` must not be written in code: all of them live in `src/include/dacom_iids.h`
(namespace `Iids`, visible everywhere through `dacom.h`). The constant name = the IID without `1.11_` and without
the leading `I`: `Object->QueryInterface(Iids::FileSystem, &Out)`. A new IID is added **only**
after the string has been read in a listing (`.rdata` or a push before `QueryInterface`):
a QI refusal at runtime is not caught by the build. Factory registration names (`"VMeshLibrary"`,
`"DirectX8"`) are a different set of strings, they are in `src/include/render/factory_names.h`
(`Render::Factory`). In comments an IID is written as a string — it is a quotation from the listing.

## 4. Rules for checking arity and calling convention

These are traps the port has actually fallen into (details of each case are in git history
and in the tracker items):

- **`RET imm16` is the frame width, not the arity.** `__thiscall` (object in `ECX`) is also
  callee-cleanup. Explicit `this` on the stack: arguments = `imm/4 - 1`; `__thiscall`: arguments =
  `imm/4`. The calling side decides, and within one interface the conventions may differ
  per slot. A prologue-based classifier is not proof.
- **A balanced stack does not mean the call is right.** `__stdcall(a, b)` and `__thiscall(a, b)` pop
  the same 8 bytes; the receiver in `ECX` is invisible to the argument count. If retail reloads
  `ECX` right before `CALL [vtable+N]` and reads it nowhere else, it is `__thiscall`.
  MSVC cannot do an indirect `__thiscall` through a function pointer, but can through a
  typed virtual declared `__thiscall`; naked-asm helpers are not needed.
- **A slot declared `DACOM_API` rather than `__thiscall`** makes callers push `this`
  on the stack on top of the arguments — four bytes of extra stack per call, silently, as long as both sides are
  `void*`.
- **The type of the holder, not the variable name.** A local `Engine` may hold an `IScriptEngine`, not an
  `IEngine`; in a mass conversion, check the declared type and the arity of every call.
- **Null checks on a slot stay raw** (`Vtable[N] == nullptr` asks whether the
  slot is wired; a typed call cannot say that). [abi/RAW_CALLS.md](abi/RAW_CALLS.md) counts calls, not
  reads.
- **The origin of a sub-object is proven by the constructor of the enclosing class** (`LEA ECX,[this+N];
  CALL <ctor>`) together with the RTTI COL at `vftable[-4]`; the address of a method is not: `/OPT:ICF` folds
  identical bodies of different classes.
- **Out-parameters and the hidden return pointer** change the arity: a returned structure
  adds a hidden argument; a handle passed "by address" (`destroy_vb`) must not be
  replaced by a value.
- **Read literals as bit patterns** (`float` versus an integer enumeration versus an angle in
  degrees) — by how retail uses them.
- **`ADD ECX,4` is the address of a sub-object, not a field load**: do not confuse `LEA`/`ADD` with `MOV`
  when deriving a nested object.
- The [abi/RAW_CALLS.md](abi/RAW_CALLS.md) counters are a lower bound: only what the generator managed to
  recognise is counted as a holder. "Zero raw calls" does not prove the absence of defects.

## 5. Files

| File | What is in it |
|------|-----------|
| `src/include/dacom_iids.h` | all IID strings in namespace `Iids` |
| [abi/modules.md](abi/modules.md), [abi/symbols.md](abi/symbols.md), [abi/AUDIT.md](abi/AUDIT.md), [abi/RAW_CALLS.md](abi/RAW_CALLS.md) | a generated snapshot of the cross-DLL interface index (the generator is not published; do not edit by hand) |
