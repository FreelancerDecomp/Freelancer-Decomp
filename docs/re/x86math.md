# x86math.dll — 3DMathEngine

`x86math.dll` is a small DACOM module with fast 3D operations (matrices, transforms, quaternions, `InvSqrt`) used by EngBase, Common, RP8, RendComp, Shading, Thorn, alchemy, Deformable2, flmaterials, Content and Server. This document explains how to obtain the object, what types it has, and which numeric peculiarities the port must preserve. The port is `src/x86math/`, the public ABI is [src/include/x86math.h](../../src/include/x86math.h) (`I3DMathEngine`, `FVector`/`FMatrix`/`FTransform`/`FQuaternion`, `FMathDesc`); function slots and statuses live in decomp-tracker (listing `EXE/Disasm/x86math.dll.txt`). Rules for calling foreign interfaces — [cross_dll_protocol.md](cross_dll_protocol.md).

## How to obtain the object

There are no PE exports (only `DllMain`); other modules do not link against x86math but create the object through `ICOManager::CreateInstance` with the descriptor `{size = 0xC, interface_name = "3DMathEngine", implementation = NULL}` (field `+8`: NULL or `"x86"`). The factory is registered with priority `0x40000000`. Codes: success — `GR_OK`, `*out` is the object with AddRef; `desc == 0` or no name — `0xFFFFFFFE`; wrong size/name/implementation — `0xFFFFFFFD`. The name is compared bytewise with `"3DMathEngine"` (13 bytes). `QueryInterface` on the object itself always returns `*out = 0` and `GR_GENERIC`: the IID `"1.11_I3DMathEngine"` is not used in retail, the only path to the object is `CreateInstance`. `AddRef`/`Release` always return 1 (singleton). If `CreateInstance` is broken, EngBase gets null math pointers and crashes on the first call.

`DllMain` (attach): creates the object with two vfptrs (math interface + factory, 8 bytes), builds the `InvSqrt` table and registers the component. If `new` fails it returns TRUE without building the table.

## Types

`Vector` — 12 bytes `{X,Y,Z}`; `Matrix` — 36 bytes, row-major `d[row][col]`, `out = M * v` gives `out.x = row0·v`; `Transform` — 48 bytes, `Matrix` + translation at `+0x24`; `Quaternion` — 16 bytes `{W,X,Y,Z}` (scalar first). The slot order of `I3DMathEngine` does not match the Liberty `3dMath.h` reconstruction; the source of truth is the retail vtable. The port added a `QuatSlerpValue` slot with `const float* T` at the end of the interface — a contract between our modules, not retail compatibility (the former `QuatSlerp(float)` delegates to it).

## Numeric peculiarities that must not be "simplified"

- Most methods are x87 with intermediate values in 80-bit precision; the order of additions, early rounding to `float`, and reading the source after writing the destination (input/output aliasing) affect the result and are reproduced literally. Only a listing comparison is acceptable; an "equivalent" `float` expression rounds differently.
- `MIN_DET = 1e-8f`: when `|det| <= MIN_DET`, `inverse` and `general_inverse` write the adjugate without dividing and return `GR_INVALID_PARMS`.
- **`InvSqrt`/`Sqrt`**: a table with 10 mantissa bits (`0x400` bytes) + two Newton iterations `(3 - y²x) * y * 0.5`; `Sqrt` = the same result × x. In the PE the table is zeros (BSS) and is built in `DllMain` by an x87 generator (`FLD (i|0xFC00)<<14`, `FSQRT`, `FDIVR 1.0`, `(bits+0x2000)>>15`), after which `table[0x200] = 0xFF` (the mantissa slot of 1.0, where the computed byte overflows). Comparing against the PE file is meaningless; compare against the live table after `DllMain`; isolated discrepancies are possible with a different x87 control word.
- **`transform_list` (Matrix)** — "FastDot": each matrix row is classified as `==0` / `==1` / other (27 kernel variants, FPU order: Z·C, then Y·B, then X·A); if the sum of "saved operations" over the three rows is `< 4`, the common triple MAD is used, otherwise three indirect kernel calls. Not SIMD. The `Transform` variant adds the translation on x87 before the final `float` store (with zero translation — a tail call into the Matrix version).
- **`quat_slerp`** — not Shoemake slerp but nlerp with normalization: for `dot >= 0` lerp `A + T*(B-A)`, otherwise `A - T*(A+B)` (lerp towards `-B`); normalization via `FSQRT` + `FDIVR 1.0`, **not** via the table-based `InvSqrt`; zero length is not checked.
- `matrix_to_quaternion` / `quaternion_to_matrix` — standard formulas (trace / largest diagonal; for `matrix_to_quaternion` the branch is chosen by an x87 comparison, including unordered), with rounding to `float` only on the result writes.

## Port deviations

1. No CRT `_initterm`/onexit: the table is built explicitly in `DllMain` after a successful `new`.
2. The FastDot kernels are naked thiscall members of `FFastDot` (result in ST0), because MSVC cannot express a free `__thiscall`; thresholds and layout are the same. Slots 16/17 remain `DACOM_API` stdcall.
3. The object is created with `::operator new(8, nothrow)` + placement new (MSVC writes the vfptr itself, retail does it manually; `sizeof == 8`).
4. The port's `QueryInterface` and `CreateInstance` check the output pointer for NULL; retail writes without a check.
5. The status of individual methods (some remain incompletely verified with respect to FP-stack argument preparation) is in the tracker, not here.
