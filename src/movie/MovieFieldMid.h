// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Field helpers from movie.dll whose bodies are in MovieFieldMid.cpp.
// The retail address is on each definition. Calling conventions match the listing RET n.

// 0x06ac52f0. RET 0x14. Divide Dividend by one limb. Quotient may be 0.
// Returns the remainder. The listing types the fifth argument int*; the body uses it as the length.
unsigned int __stdcall Movie_DivideByLimb(unsigned int* Dividend, unsigned int DivisorLimb, unsigned int* Reciprocal, unsigned int Quotient, int DividendCount);

// 0x06ac7570. RET 0xC. Copies the constant at Field+0x12c into the first two groups and zeros the next three.
// Scratch is the third argument the RET pops; the body never reads it.
long __stdcall Movie_LoadFieldOne(unsigned int* Work, int* FieldHolder, unsigned int* Scratch);

// 0x06ac75d0. RET 0x10. A zero tail at First+2*Count stores the field unit into Second.
// Otherwise Second's halves are First * u^2 and (First+Count) * u^3, where u is the unary of the tail.
long __stdcall Movie_ScalePairByTail(unsigned int* First, unsigned int* Second, int* FieldHolder, unsigned int* Scratch);

// 0x06ac7680. RET 0x14. Decrements the two window counters, then assigns the pair at +1 and -1.
void __stdcall Movie_RecordWindowSumDiff(unsigned int* SumSlot, unsigned int* DiffSlot, int First, int Second, int* State);

// 0x06ac7760. RET 0x14. Binds (First, Second) to a window slot. Sign selects copy or negate.
void __stdcall Movie_AssignWindowSlot(unsigned int* First, unsigned int* Second, int Slot, int* State, int Sign);

// 0x06ac7e60. RET 0x8. Kind 1: both halves equal Field[4]. Otherwise the 2*Count limbs are zero.
long __stdcall Movie_FieldElementIsUnit(int Limbs, int* FieldHolder);

// 0x06ac7f20. RET 0x10. Sign 1 copies two groups. Sign -1 runs 0x06ac7f80. Any other sign returns 0.
long __stdcall Movie_CopyOrNegatePair(unsigned int* Source, unsigned int* Dest, int* FieldHolder, int Sign);

// 0x06ac78a0. RET 0x14. Sign is +1 or -1. Kinds 2 and 3 finish by XOR-mixing the stack buffers.
long __stdcall Movie_AddFieldElements(unsigned int* Left, unsigned int* Right, unsigned int* Dest, int Sign, unsigned int* FieldHolder);

// 0x06ac7f80. RET 0xC. Copies the low half. The high half is copied, negated, or added to the low half.
long __stdcall Movie_NegateFieldPair(unsigned int* Source, unsigned int* Dest, int* FieldHolder);

// 0x06ac8200. RET 0x10. Kind 1 rejects a zero witness. Kinds 2 and 3 reject a zero Right.
long __stdcall Movie_CommitFieldPair(unsigned int* Left, unsigned int* Right, unsigned int* FieldWords, unsigned int* Record);

// 0x06ac8300. RET 0x8. Status 0x2D when Subtrahend < Minuend. Otherwise a value in that inclusive span.
unsigned int __stdcall Movie_DrawBoundedLimb(unsigned int Minuend, unsigned int Subtrahend);

// 0x06ac8410. RET 0xC. Status 0x30 when Source is not greater than 1 or its top limb is 0.
void __stdcall Movie_FillRandomBelow(int Source, void* Dest, unsigned int Count);

// 0x06ac8490. RET 0x10. Buffer[i] += Source[i] * Scale. Returns the carry.
unsigned int __stdcall Movie_AddScaledLimbs(int Source, unsigned int Scale, unsigned int* Buffer, int Count);

// 0x06ac85c0. RET 0x10. Equal-length add. The listing pairs limbs; the carry out is 0 or 1.
long __stdcall Movie_AddLimbsCarry(unsigned int* Left, unsigned int* Right, int Dest, int Count);

// 0x06ac8630. RET 0x10. Buffer[i] -= Source[i] * Digit. Returns the borrow.
unsigned int __stdcall Movie_SubtractScaledLimbs(int Source, unsigned int Digit, unsigned int* Buffer, int Count);
