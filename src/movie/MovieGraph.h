// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <windows.h>

// Field-arithmetic helpers for the COM object at vtable PTR_FUN_06ada770.
// FUN_06ac1e50 (PlayMovie, approach 0) installs an IServiceProvider at
// g_serviceProvider. QueryService (FUN_06ac1b60) builds that object, and its
// vtable slot +0x14 (FUN_06ac2549) calls CreateFieldContext / HresultIfNonZeroLocked /
// RecoverFieldLimbsLocked / MapFieldLimbsLocked / ReleaseFieldContext
// (FUN_06ac2ae0 / 28e0 / 3060 / 2e40 / 2b30).
// The rest of this file is that cluster calling itself. Callees past 0x06ac3d40
// stay declared in MovieGraph.cpp; this lane does not define them.

// DAT_06ae0960, the CRITICAL_SECTION these helpers take. DAT_06ae0978 is the
// one-shot init flag. DAT_06ae097c is the status dword FUN_06ac3d40 writes.
extern CRITICAL_SECTION GMovieGraphLock;
extern int GMovieGraphLockInit;
extern int GMovieGraphCode;

// FUN_06ac2690. stdcall, RET 0xC. Up to 100 rounds; 1 only when a witness is found.
long __stdcall Movie_SearchFieldWitness(unsigned int* Seed, unsigned int* Element, unsigned int* FieldBlock);

// FUN_06ac2870. stdcall, RET 0x10. Copies Count dwords (Count <= 5) and calls 0x06ac4e20.
long __stdcall Movie_CopyAndReduceLimbs(unsigned int* Src, unsigned int Count, unsigned int* Dst, unsigned int* Modulus);

// FUN_06ac28c0. Retail thiscall, ECX = the pointer. The port takes that pointer as the
// argument: MSVC rejects __thiscall on a free function (C3865). 0 -> 0x80004005, nonzero -> 0.
long  Movie_HresultIfNonZero(int* Value);

// FUN_06ac28e0. thiscall. EnterCriticalSection, HresultIfNonZero, Leave.
long  Movie_HresultIfNonZeroLocked(int* Value);

// FUN_06ac2910. thiscall. Allocates 0x439C and fills it, or stores 0.
void  Movie_AllocateFieldContext(void** Holder);

// FUN_06ac2970. stdcall, RET 0x1C. 0 on success, 0x80070057 or 0x80004005 otherwise.
long __stdcall Movie_BuildFieldContext(unsigned int* Object, unsigned int* LimbsA, unsigned int* LimbsB, unsigned int* LimbsC, unsigned int* LimbsD, int NonNullA, int NonNullB);

// FUN_06ac2ae0. thiscall. Lazy-inits the lock, then AllocateFieldContext. Returns This.
void*  Movie_CreateFieldContext(void** Holder);

// FUN_06ac2b30. thiscall. Under the lock, frees *Holder when it is nonzero.
void  Movie_ReleaseFieldContext(void** Holder);

// FUN_06ac2b60. stdcall, RET 0x14. Forwards to FUN_06ac5610 with 4 and 0x3C inserted.
long __stdcall Movie_ReduceWithFixedWidth(void* Arg1, void* Arg2, int Arg3, void* Arg4, void* Arg5);

// FUN_06ac2b90. thiscall, RET 0xC. Four groups of five limbs into OutLimbs.
long  Movie_MapFieldLimbs(void* This, unsigned int* InLimbs, unsigned int* Seed, unsigned int* OutLimbs);

// FUN_06ac2de0. stdcall, RET 0x14. Five stack arguments; the .c signature dropped four.
long __stdcall Movie_ReduceExpandedRows(unsigned int* Arg1, void* Arg2, int Arg3, void* Arg4, void* Arg5);

// FUN_06ac2e40. thiscall, RET 0xC. MapFieldLimbs under the lock.
long  Movie_MapFieldLimbsLocked(void* This, unsigned int* InLimbs, unsigned int* Seed, unsigned int* OutLimbs);

// FUN_06ac2e80. thiscall, RET 0xC. Inverse of the map; HRESULT from the low-half store.
long  Movie_RecoverFieldLimbs(void* This, unsigned int* BaseLimbs, unsigned int* Groups, unsigned int* OutWords);

// FUN_06ac3000. stdcall, RET 0xC. Always returns 1. Copies four dwords out.
long __stdcall Movie_StoreLowFourLimbs(unsigned int* Source, unsigned int* Dest, int* Holder);

// FUN_06ac3060. thiscall, RET 0xC. RecoverFieldLimbs under the lock.
long  Movie_RecoverFieldLimbsLocked(void* This, unsigned int* BaseLimbs, unsigned int* Groups, unsigned int* OutWords);

// FUN_06ac30a0. stdcall, RET 0x10. Kind 1 adds modulo; kinds 2 and 3 XOR.
long __stdcall Movie_AddFieldLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Sum, int* Field);

// FUN_06ac3170. stdcall, RET 0x10. Unary-by-kind on Right, then multiply by Left.
long __stdcall Movie_MultiplyAfterUnary(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* Field);

// FUN_06ac31d0. stdcall, RET 0xC. 1 when the Count-dword spans compare equal.
long __stdcall Movie_FieldLimbsEqual(int Left, int Right, int* Field);

// FUN_06ac3220. stdcall, RET 0xC. Embeds a signed integer; negative values are negated.
long __stdcall Movie_SetFieldFromSigned(unsigned int Value, unsigned int* Dest, int* Field);

// FUN_06ac32b0. stdcall, RET 0xC. Returns 1. EAX is set before REP MOVSD.
long __stdcall Movie_BindFieldHeader(int Modulus, int Stored, int* Field);

// FUN_06ac3300. stdcall, RET 0xC. Kind 1 binary gcd or compare-to-1; kind 2 power; kind 3 shift-xor.
long __stdcall Movie_FieldUnaryByKind(unsigned int* Source, unsigned int* Dest, unsigned int* Field);

// FUN_06ac3450. stdcall, RET 0x8. 1 when every Count dword is 0.
long __stdcall Movie_FieldLimbsAreZero(int Element, int* Field);

// FUN_06ac3490. stdcall, RET 0x10. Kind 1 multiply-modulo; kind 2 binary multiply; kind 3 shift-xor.
long __stdcall Movie_MultiplyFieldLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int* Field);

// FUN_06ac3500. stdcall, RET 0x14. Product of Left and Right, then add Addend.
long __stdcall Movie_MultiplyThenAddField(unsigned int* Left, unsigned int* Right, unsigned int* Addend, unsigned int* Dest, unsigned int* Field);

// FUN_06ac3570. stdcall, RET 0xC. Kind 1 negates modulo; kinds 2 and 3 copy.
long __stdcall Movie_NegateFieldLimbs(unsigned int* Source, unsigned int* Dest, int* Field);

// FUN_06ac35c0. stdcall, RET 0xC. Kind 1 square; kind 2 shift; kind 3 repeated self-multiply.
unsigned int __stdcall Movie_SquareFieldLimbs(unsigned int* Source, unsigned int* Dest, unsigned int* Field);

// FUN_06ac3660. stdcall, RET 0x10. Kind 1 subtracts modulo; kinds 2 and 3 XOR.
long __stdcall Movie_SubtractFieldLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Difference, int* Field);

// FUN_06ac36d0. stdcall, RET 0x10. Two shifts, OR, mask of the last limb. Kind 2 only.
long __stdcall Movie_ShiftFieldLimbs(unsigned int* Source, unsigned int Shift, int Dest, unsigned int* Field);

// FUN_06ac3790. stdcall, RET 0xC. Binary exponentiation from the high bit of (field[1] - 1).
long __stdcall Movie_PowFieldByBitScan(unsigned int* Base, int Dest, unsigned int* Field);

// FUN_06ac38d0. stdcall, RET 0x10. Kind-2 product: AND plus the pair cross terms.
long __stdcall Movie_MultiplyBinaryField(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int* Field);

// FUN_06ac3b80. stdcall, RET 0xC. Kind 2 writes bit parity; kind 3 checks the iterated product.
long __stdcall Movie_TraceFieldLimbs(unsigned int* Source, unsigned int* Dest, unsigned int* Field);

// FUN_06ac3d40. stdcall, RET 0x10. Count and Flag are the immediates pushed
// by FUN_06ac2970 (5 and 1); the body uses them as integers, not pointers.
// Builds the modulus record. 1 on success, else 0.
long __stdcall Movie_BuildModulusRecord(unsigned int* Limbs, unsigned int Count, unsigned int Flag, unsigned int* Record);

// 0x06ac3110. RET 0xC. Zeros Groups * Field[0] dwords and returns 1.
long __stdcall Movie_ZeroFieldGroups(unsigned int* Dest, int Groups, int* Field);

// 0x06ac3140. RET 0x10. Copies Groups * Field[0] dwords from Source to Dest and returns 1.
long __stdcall Movie_CopyFieldGroups(unsigned int* Source, unsigned int* Dest, int Groups, int* Field);
