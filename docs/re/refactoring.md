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
