// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Field arithmetic from movie.dll. Each body is in MovieField.cpp; the retail
// address is on the definition. Calling conventions match the listing RET n.

// 0x06ac4080. RET 0xC.
unsigned int __stdcall Movie_CopyOrReduceField(unsigned int* Source, unsigned int* Dest, unsigned int* Modulus);

// 0x06ac40e0. RET 0x10.
void __stdcall Movie_AddModulo(unsigned int* Left, unsigned int* Right, unsigned int* Sum, unsigned int* Modulus);

// 0x06ac4160. RET 0x14. Scratch may be null; this function then allocates it.
void __stdcall Movie_MultiplyModulo(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* Modulus, unsigned int* Scratch);

// 0x06ac4250. RET 0x18. Scratch is the caller's buffer.
void __stdcall Movie_MultiplyFactorModulo(unsigned int* Source, unsigned int* Factor, unsigned int* FactorTail, unsigned int* Dest, unsigned int* Modulus, unsigned int* Scratch);

// 0x06ac42e0. RET 0x1C. The four terms are forwarded to the product kernel.
void __stdcall Movie_MultiplyTermsModulo(unsigned int* TermA, unsigned int* TermB, unsigned int* TermC, unsigned int* TermD, unsigned int* Dest, unsigned int* Modulus, unsigned int* Scratch);

// 0x06ac4340. RET 0xC.
void __stdcall Movie_NegateModulo(unsigned int* Source, unsigned int* Dest, unsigned int* Modulus);

// 0x06ac4390. RET 0x10. BitCount is a signed shift.
void __stdcall Movie_ShiftModulo(unsigned int* Source, int BitCount, unsigned int* Dest, unsigned int* Modulus);

// 0x06ac4550. RET 0x10.
void __stdcall Movie_SubtractModulo(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* Modulus);

// 0x06ac4db0. RET 0x10. Bound[0] is the split limit; Bound is also passed through.
void __stdcall Movie_SplitLimbs(unsigned int* Source, unsigned int Count, unsigned int* Dest, unsigned int* Bound);

// 0x06ac4e20. RET 0x10. 0 if the workspace allocation fails, 1 otherwise.
long __stdcall Movie_ReduceModulo(unsigned int* Source, unsigned int Count, unsigned int* Dest, unsigned int* Modulus);

// 0x06ac4f40. cdecl.
void __cdecl Movie_InitForProcessor();

// 0x06ac4f80. RET 0x4.
void* __stdcall Movie_AllocateBlock(unsigned int Size);

// 0x06ac4fb0. RET 0x4.
void __stdcall Movie_FreeBlock(void* Block);

// 0x06ac4fd0. RET 0x10. High half of the reciprocal product.
unsigned int __stdcall Movie_EstimateQuotient(unsigned int Carry, unsigned int Top, unsigned int Second, unsigned int* Reciprocal);

// 0x06ac54d0. Normalize the leading limbs and store the reciprocal plus the shift.
void __stdcall StoreLeadingReciprocal(int Limbs, unsigned int Count, unsigned int* Reciprocal);

// 0x06ac5050. RET 0x14. Dividend is (High << 32) | Low.
void __stdcall Movie_DivideDoubleWord(unsigned int Low, unsigned int High, unsigned int Divisor, int* Quotient, int* Remainder);

// 0x06ac5090. RET 0x1C. Quotient may be 0. Returns the remainder's significant length, or 0 on error.
int __stdcall Movie_DivideLimbs(unsigned int* Dividend, unsigned int DividendCount, unsigned int* Divisor, int DivisorCount, unsigned int* Reciprocal, unsigned int Quotient, unsigned int* Remainder);
