# Readability refactoring

Rules for tasks that improve the readability of already ported code (RP8, RendComp, Shading,
flmaterials, etc.) without changing behaviour.

## Names and comments

- Replace `SlotNN`, `FUN_...` and uninformative names with meaningful ones only after reading the
  implementations, the consumers and the retail listing. A name describes a proven contract (copy
  or ownership transfer, bytes or elements, requested or applied state); do not guess unknown
  semantics.
- Keep the previous name next to the renamed declaration (`// formerly Slot3`), and update the
  implementations, the consumers and the name/signature in the tracker (`ported_name`, `ported_file`).
- Comments are short: contract, ownership, lifetime, units, non-trivial ordering.
  Long proofs and address-level analyses go into tracker items (`add_comment`).

## Types and casts

- Replace `void*` and byte pointers with concrete types when all producers and consumers
  confirm it. A type change does not change ownership: no `AddRef`/`Release`, smart pointers, or new
  lifetime policy.
- Eliminate casts by fixing the types of fields, arguments and return values; mechanical replacement of
  `static_cast`/`reinterpret_cast` is forbidden. Do not lose the base-class address adjustment under
  multiple inheritance, do not strip `const`, and distinguish a numeric conversion from carrying over the
  bit representation of `float`, colors and packed data.
- A type crossing a DLL boundary is declared once in `src/include/<module>.h`; all consumers
  are changed in the same task, and the order of virtual methods and `DACOM_API` are preserved.

## Memory and initialization

- Replace raw `operator new`/`delete` with `new`, `new[]`, `delete`, `delete[]` when the type
  and the number of objects are known; change the allocation and all release paths together.
- Preserve initialization, construction and destruction order, exception handling and ownership.
  Verify separately the move from a raw block to objects with constructors. Example:
  four `FRpVertexStream` zeroed by hand are equivalent to `new FRpVertexStream[4]{}` with `delete[]`.

## Constants and methods

- Replace magic numbers with existing enums and named constants when the meaning is unambiguous
  (the state identifier in `SetRenderState`/`GetRenderState` is `D3DRENDERSTATETYPE`; the state
  value stays a number, since it holds different kinds of data). Preserve width, signedness and bit
  semantics; do not merge heterogeneous values into one enum.
- Return free functions to real C++ objects according to the retail listing (constructor chain,
  `this`, calls, RTTI). Replace `Base + Offset` accesses with named fields only after converting
  all consumers of the numeric offsets; do not change the field layout before that.
- Do not reorder globals with complex initialization (for example RP8) until their contracts
  are recovered from the full inventory: initializers, writers, readers.

## Change boundaries

Refactoring preserves algorithms, order of actions, branching, errors, side effects and object
lifetimes. Defensive checks and incidental modernization are not added. A discovered behavioural defect
is immediately recorded by lowering the status in the tracker, and is fixed in a separate change.

Renaming an exported symbol changes the export: check the export surface
(`dumpbin -exports`) and the imports of all consumers.

Building and improving names do not raise a status to `ready_verified`: that requires a full comparison
of behaviour against the listing.

## Port-only code

Code with no retail counterpart carries one tag, so that a single grep finds it:

```cpp
// PORT(<feature>): <why>
```

- The tag starts the comment that introduces a port-only block, declaration, include or file
  header. A single port-only line gets it as a trailing comment:
  `G_InstanceKeys.Remove(Node); // PORT(perf-index)`.
- A file that is port-only as a whole puts the tag in its header comment, next to the macro that
  compiles it (`// PORT(walk): compiled only with FREELANCER_WALK_MODE`).
- Code under a feature macro (`#ifdef FREELANCER_WIDESCREEN_SUPPORT` and the like) is found
  through the macro too; tag the comment that explains the block, not every `#ifdef`.
- A comment that only records port-only code that was removed stays plain prose: nothing is
  left to find.
- `grep -rn "PORT(" src` lists every tag; `grep -rn "PORT(perf-index)" src` one feature.
  The features, their switches and their effect on retail compatibility are indexed in
  [deviations.md](deviations.md#port-only-features).

| Tag | What |
|---|---|
| `walk` | Walk mode in base rooms (`FREELANCER_WALK_MODE`) |
| `widescreen` | Widescreen resolutions and Hor+ cameras (`FREELANCER_WIDESCREEN_SUPPORT`) |
| `rp9` | RP9.dll and the client's Direct3D 9 hooks, including the volumetric nebula (`FREELANCER_RP9`) |
| `rpgpu` | RPGPU.dll, the SDL3 + SDL_GPU pipeline (`src/RPGPU`), and TexConvert (`src/TexConvert`). The HLSL shaders are not tagged: CI compares their compiled bytecode with the committed copy. |
| `diag` | The owner's diagnostics: Beta label, tick profiler, FDUMP debugger echo |
| `perf-index` | Side indexes that answer a retail walk without changing its result (docs/scaling.md) |
| `x64` | The x64 build (`_WIN64` / not `_M_IX86`) |
| `dpnet` | DPNet, the port's DirectPlay 8 (`FREELANCER_DPNET`), and DPNetCheck |
| `flregress` | FLRegress.exe and its hooks |
| `dedicated` | FLDedicated.exe |
| `tools` | Other port-only programs (RpcCodecTest, MemoryDiagnostics, TexConvertTest) |
| `api` | A name, accessor, interface slot or helper retail has no counterpart for, with no behaviour of its own |
| `deviation` | A behaviour difference outside the features above, recorded in [deviations.md](deviations.md) |

A new feature gets a tag here, a row in the deviations.md index and, if it can be switched off,
a switch in `cmake/Freelancer.cmake` ([building.md](../building.md#switches)).

Inside a retail function body, keep the retail code written once. Put the port-only part in one
guarded call, or in a small helper that makes exactly the retail call when the feature is off,
instead of copying the retail body to both sides of an `#else` (see `gameflow.cpp` for walk
mode). The body then still reads, and diffs, like the listing.
