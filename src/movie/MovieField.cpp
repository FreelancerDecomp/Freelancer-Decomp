// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieField.h"
#include "MovieWindow.h"

#include <windows.h>

// DAT_06ae097c. Defined in MovieGraph.cpp.
extern int GMovieGraphCode;

// DAT_06ae0980. Allocation counter. Only 0x06ac4f80 and 0x06ac4fb0 touch it.
int GMovieAllocCount = 0;

// Callees outside this file. Names stay Movie_<addr> until those lanes name them.
// 0x06ac45a0. Defined in MovieExp.cpp. RET 0x18.
long __stdcall ReduceLimbsModulo(unsigned int* Source, unsigned int Count, unsigned int Mode, unsigned int* Dest, unsigned int* Modulus, void* Scratch);

// 0x06ac85c0. Limb add, borrow/carry in EAX. RET 0x10.
long __stdcall Movie_AddLimbsCarry(unsigned int* Left, unsigned int* Right, int Dest, int Count);

// 0x06ac89d0. Limb subtract, borrow in EAX. RET 0x10.
long __stdcall SubtractLimbs(unsigned int* Left, unsigned int* Right, int Dest, int Count);

// 0x06ac8bd0. In-place limb shift. RET 0x10.
unsigned int __stdcall ShiftLimbs(unsigned int* Source, unsigned int BitCount, unsigned int* Dest, int Count);

// 0x06ac8630. Multiply a limb by the modulus and subtract. RET 0x10.
unsigned int __stdcall Movie_SubtractScaledLimbs(int Modulus, unsigned int Digit, unsigned int* Buffer, int Count);

// 0x06ac8490. RET 0x10.
unsigned int __stdcall Movie_AddScaledLimbs(int Modulus, unsigned int Scale, unsigned int* Buffer, int Count);

// 0x06ac8a90. Signed compare of two limb integers. RET 0x10.
long __stdcall CompareLimbs(int Left, unsigned int LeftCount, int Right, int RightCount);

// 0x06aca7d0. RET 0x4.
void __stdcall FillWindowTable(unsigned int Size);

// 0x06ac52f0. Single-limb divisor. RET 0x14. Body in MovieFieldMid.cpp.
unsigned int __stdcall Movie_DivideByLimb(unsigned int* Dividend, unsigned int DivisorLimb, unsigned int* Reciprocal, unsigned int Quotient, int DividendCount);

// 0x06ac54d0. Writes two dwords: reciprocal, then shift. RET 0xC.
void __stdcall StoreLeadingReciprocal(int Limbs, unsigned int Count, unsigned int* Reciprocal);

static void Movie_CopyLimbs(unsigned int* Dest, unsigned int* Source, unsigned int Count)
{
    unsigned int Index;
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        Dest[Index] = Source[Index];
    }
}

static void Movie_ZeroLimbs(unsigned int* Dest, unsigned int Count)
{
    unsigned int Index;
    for (Index = 0u; Index < Count; Index = Index + 1u)
    {
        Dest[Index] = 0u;
    }
}

// Walk from the high limb. Returns 0 when every limb is 0.
static unsigned int Movie_SignificantCount(unsigned int* Limbs, unsigned int Count)
{
    unsigned int Index = Count;
    while (Index != 0u)
    {
        unsigned int Limb = Limbs[Index - 1u];
        Index = Index - 1u;
        if (Limb != 0u)
        {
            return Index + 1u;
        }
    }
    return 0u;
}

// High limb first. -1 when Left < Right, 0 when equal, 1 when Left > Right.
// SBB EAX,EAX / OR EAX,1 after the first differing limb.
static int Movie_CompareDescending(unsigned int* Left, unsigned int* Right, unsigned int Count)
{
    unsigned int Index = Count;
    while (Index != 0u)
    {
        unsigned int LeftLimb = Left[Index - 1u];
        unsigned int RightLimb = Right[Index - 1u];
        Index = Index - 1u;
        if (LeftLimb != RightLimb)
        {
            unsigned int Borrow = (unsigned int)(LeftLimb < RightLimb);
            int Sign = (int)(0u - Borrow);
            return Sign | 1;
        }
    }
    return 0;
}

// SAR / NOT / AND 1, then the slot plus that bit. Listing 06ac419d and 06ac4272.
static unsigned int* Movie_AlignDword(unsigned int* Slot, unsigned char* Reference)
{
    int Delta = (int)(Reference - (unsigned char*)Slot);
    int Bit = (~(Delta >> 2)) & 1;
    return Slot + Bit;
}

// Shared by 0x06ac4fd0 and the inlined copy at 0x06ac5206.
// Shift counts are masked to 5 bits, matching SHR/SHL by CL.
static unsigned int Movie_ReciprocalHigh(unsigned int Carry, unsigned int Top, unsigned int Second, unsigned int* Reciprocal)
{
    unsigned int Shift = Reciprocal[1] & 0x1fu;
    unsigned int InvShift = (0x1fu - Reciprocal[1]) & 0x1fu;
    unsigned int Hi = ((Top >> 1) >> InvShift) | (Carry << Shift);
    unsigned int Lo = ((Second >> 1) >> InvShift) | (Top << Shift);
    unsigned int Inverse = Reciprocal[0];
    unsigned long long Product = (unsigned long long)Inverse * (unsigned long long)Hi;
    unsigned long long Sum = Product + (unsigned long long)Lo + ((unsigned long long)Hi << 32);
    if ((Lo & 0x80000000u) != 0u)
    {
        Sum = Sum + (unsigned long long)(Inverse >> 1);
    }
    return (unsigned int)(Sum >> 32);
}

// 0x06ac4080
// Field_0x110 == 0 copies LimbCount limbs. Otherwise reduces modulo (mode 1).
// Returns the significant limb count, or 0 when the reduction fails.
unsigned int __stdcall Movie_CopyOrReduceField(unsigned int* Source, unsigned int* Dest, unsigned int* Modulus)
{
    unsigned int LimbCount = Modulus[0x40];
    if (Modulus[0x44] == 0u)
    {
        Movie_CopyLimbs(Dest, Source, LimbCount);
    }
    else if (ReduceLimbsModulo(Source, LimbCount, 1u, Dest, Modulus, 0) == 0)
    {
        return 0u;
    }
    return Movie_SignificantCount(Dest, LimbCount);
}

// 0x06ac40e0
// Sum = Left + Right. Subtracts Modulus unless the sum produced no carry and is below it.
// Status 0x19 when the subtract's borrow disagrees with the add's carry.
void __stdcall Movie_AddModulo(unsigned int* Left, unsigned int* Right, unsigned int* Sum, unsigned int* Modulus)
{
    unsigned int LimbCount = Modulus[0x40];
    long Carry = Movie_AddLimbsCarry(Left, Right, (int)Sum, (int)LimbCount);
    if (Carry == 0)
    {
        if (Movie_CompareDescending(Sum, Modulus, LimbCount) < 0)
        {
            return;
        }
    }
    long Borrow = SubtractLimbs(Sum, Modulus, (int)Sum, (int)LimbCount);
    if (Carry - Borrow != 0)
    {
        GMovieGraphCode = 0x19;
    }
}

// 0x06ac4160
// Product of Left and Right (one expand when they alias), then modular reduction.
// Allocates a 0x231C scratch when Scratch is null and frees it on the way out.
void __stdcall Movie_MultiplyModulo(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* Modulus, unsigned int* Scratch)
{
    unsigned int LimbCount = Modulus[0x40];
    unsigned int* Dispatch = (unsigned int*)Modulus[0x42];
    unsigned int* Work = Scratch;
    unsigned char* Base;
    unsigned int* At510;
    unsigned int* AtA74;
    unsigned int* Aligned;
    if (Work == 0)
    {
        Work = (unsigned int*)Movie_AllocateBlock(0x231c);
        if (Work == 0)
        {
            return;
        }
    }
    Base = (unsigned char*)Work;
    At510 = (unsigned int*)(Base + 0x510);
    AtA74 = (unsigned int*)(Base + 0xa74);
    Aligned = Movie_AlignDword((unsigned int*)(Base + 0x538), Base);
    Movie_EvaluateWindow(Left, LimbCount, Work, (unsigned char*)At510, Dispatch);
    if (Left == Right)
    {
        Movie_MultiplyWindows((unsigned int*)Work, (unsigned int*)At510, (unsigned int*)Work, (unsigned int*)At510, AtA74, (unsigned int*)Dispatch);
    }
    else
    {
        unsigned int* AtA4c = (unsigned int*)(Base + 0xa4c);
        Movie_EvaluateWindow(Right, LimbCount, Aligned, (unsigned char*)AtA4c, Dispatch);
        Movie_MultiplyWindows((unsigned int*)Work, (unsigned int*)At510, (unsigned int*)Aligned, (unsigned int*)AtA4c, AtA74, (unsigned int*)Dispatch);
    }
    ReduceLimbsModulo(AtA74, LimbCount + LimbCount, Modulus[0x44], Dest, Modulus, Work);
    if (Scratch == 0)
    {
        Movie_FreeBlock(Work);
    }
}

// 0x06ac4250
// Expands Source, multiplies it by the (Factor, FactorTail) pair, then reduces.
void __stdcall Movie_MultiplyFactorModulo(unsigned int* Source, unsigned int* Factor, unsigned int* FactorTail, unsigned int* Dest, unsigned int* Modulus, unsigned int* Scratch)
{
    unsigned char* Base = (unsigned char*)Scratch;
    unsigned int* At538 = (unsigned int*)(Base + 0x538);
    unsigned int* AtA4c = (unsigned int*)(Base + 0xa4c);
    unsigned int* AtA74 = (unsigned int*)(Base + 0xa74);
    unsigned int* Aligned = Movie_AlignDword(At538, (unsigned char*)Factor);
    unsigned int* Dispatch = (unsigned int*)Modulus[0x42];
    unsigned int LimbCount = Modulus[0x40];
    Movie_EvaluateWindow(Source, LimbCount, Aligned, (unsigned char*)AtA4c, Dispatch);
    Movie_MultiplyWindows((unsigned int*)Aligned, (unsigned int*)AtA4c, (unsigned int*)Factor, (unsigned int*)FactorTail, AtA74, (unsigned int*)Dispatch);
    ReduceLimbsModulo(AtA74, LimbCount << 1, Modulus[0x44], Dest, Modulus, Scratch);
}

// 0x06ac42e0
// Multiplies the four terms the caller already laid out, then reduces.
void __stdcall Movie_MultiplyTermsModulo(unsigned int* TermA, unsigned int* TermB, unsigned int* TermC, unsigned int* TermD, unsigned int* Dest, unsigned int* Modulus, unsigned int* Scratch)
{
    unsigned int* Product = (unsigned int*)((unsigned char*)Scratch + 0xa74);
    unsigned int LimbCount = Modulus[0x40];
    Movie_MultiplyWindows((unsigned int*)TermA, (unsigned int*)TermB, (unsigned int*)TermC, (unsigned int*)TermD, Product, (unsigned int*)Modulus[0x42]);
    ReduceLimbsModulo(Product, LimbCount << 1, Modulus[0x44], Dest, Modulus, Scratch);
}

// 0x06ac4340
// Dest = -Source modulo Modulus. A zero source stays zero; otherwise Dest = Modulus - Dest.
void __stdcall Movie_NegateModulo(unsigned int* Source, unsigned int* Dest, unsigned int* Modulus)
{
    unsigned int LimbCount = Modulus[0x40];
    unsigned int Accum = 0u;
    unsigned int Index;
    if (LimbCount == 0u)
    {
        return;
    }
    for (Index = 0u; Index < LimbCount; Index = Index + 1u)
    {
        unsigned int Limb = Source[Index];
        Dest[Index] = Limb;
        Accum = Accum | Limb;
    }
    if (Accum != 0u)
    {
        SubtractLimbs(Modulus, Dest, (int)Dest, (int)LimbCount);
    }
}

// 0x06ac4390
// Shifts Dest left (BitCount > 0) or right (BitCount < 0), reducing modulo Modulus.
// A right shift of an even modulus stores status 0x1c. A failed correction stores 0x1a or 0x1b.
void __stdcall Movie_ShiftModulo(unsigned int* Source, int BitCount, unsigned int* Dest, unsigned int* Modulus)
{
    unsigned int LimbCount = Modulus[0x40];
    int Remaining = BitCount;
    if (LimbCount == 0u)
    {
        return;
    }
    if (Source != Dest)
    {
        Movie_CopyLimbs(Dest, Source, LimbCount);
    }
    if (Remaining < 0 && (Modulus[0] & 1u) == 0u)
    {
        GMovieGraphCode = 0x1c;
        return;
    }
    while (Remaining > 0)
    {
        int Chunk = 0x20;
        unsigned int Carry;
        unsigned int Second = 0u;
        unsigned int Estimate;
        unsigned int Product;
        unsigned int Diff;
        if (Remaining <= 0x20)
        {
            Chunk = Remaining;
        }
        Carry = ShiftLimbs(Dest, (unsigned int)Chunk, Dest, (int)LimbCount);
        if (LimbCount >= 2u)
        {
            Second = Dest[LimbCount - 2u];
        }
        Estimate = Movie_EstimateQuotient(Carry, Dest[LimbCount - 1u], Second, Modulus + 0x45);
        Product = Movie_SubtractScaledLimbs((int)Modulus, Estimate, Dest, (int)LimbCount);
        Diff = Carry - Product;
        if (Diff == 0u && Movie_CompareDescending(Dest, Modulus, LimbCount) < 0)
        {
            Remaining = Remaining - Chunk;
            continue;
        }
        long Borrow = SubtractLimbs(Dest, Modulus, (int)Dest, (int)LimbCount);
        if (Diff - (unsigned int)Borrow != 0u)
        {
            GMovieGraphCode = 0x1a;
            return;
        }
        Remaining = Remaining - Chunk;
    }
    if (Remaining >= 0)
    {
        return;
    }
    while (Remaining < 0)
    {
        int Amount = -Remaining;
        int Chunk = 0x20;
        unsigned int Mask;
        unsigned int Scaled;
        unsigned int Mixed;
        unsigned int Carry;
        unsigned int Keep;
        if (Amount <= 0x20)
        {
            Chunk = Amount;
        }
        Keep = (unsigned int)(0x20 - Chunk);
        Mask = 0xffffffffu >> Keep;
        Scaled = (0u - (Modulus[0x47] * Dest[0])) & Mask;
        Mixed = Movie_AddScaledLimbs((int)Modulus, Scaled, Dest, (int)LimbCount);
        Carry = ShiftLimbs(Dest, (unsigned int)(-Chunk), Dest, (int)LimbCount);
        Dest[LimbCount - 1u] = Dest[LimbCount - 1u] | (Mixed << Keep);
        if (Carry != 0u)
        {
            GMovieGraphCode = 0x1b;
            return;
        }
        Remaining = Remaining + Chunk;
    }
}

// 0x06ac4550
// Dest = Left - Right. On borrow, adds Modulus back.
// Status 0x1d when that add's carry disagrees with the borrow.
void __stdcall Movie_SubtractModulo(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* Modulus)
{
    unsigned int LimbCount = Modulus[0x40];
    long Borrow = SubtractLimbs(Left, Right, (int)Dest, (int)LimbCount);
    if (Borrow != 0)
    {
        long Carry = Movie_AddLimbsCarry(Dest, Modulus, (int)Dest, (int)LimbCount);
        if (Borrow - Carry != 0)
        {
            GMovieGraphCode = 0x1d;
        }
    }
}

// 0x06ac4db0
// Splits Source at min(Count, Bound[0]) into Dest+0x1B4 and Dest+0x1C4.
// Status 0x38 when the tail is longer than Bound[0].
void __stdcall Movie_SplitLimbs(unsigned int* Source, unsigned int Count, unsigned int* Dest, unsigned int* Bound)
{
    unsigned int Limit = Bound[0];
    unsigned int Head = Limit;
    unsigned int Tail;
    if (Count <= Limit)
    {
        Head = Count;
    }
    Tail = Count - Head;
    if (Tail > Limit)
    {
        GMovieGraphCode = 0x38;
        return;
    }
    Movie_EvaluateWindow(Source, Head, Dest, (unsigned char*)(Dest + 0x6d), Bound);
    Movie_EvaluateWindow(Source + Head, Tail, Dest + 0x71, (unsigned char*)(Dest + 0xde), Bound);
}

// 0x06ac4e20
// Reduces Source[0..Count) into Dest. Field_0x110 == 0 copies and zero-pads.
// Otherwise the value is staged at Workspace+0x100 and reduced.
// Returns 0 after clearing Dest when the 0x261C workspace cannot be allocated.
long __stdcall Movie_ReduceModulo(unsigned int* Source, unsigned int Count, unsigned int* Dest, unsigned int* Modulus)
{
    unsigned int LimbCount = Modulus[0x40];
    unsigned int* Workspace = (unsigned int*)Movie_AllocateBlock(0x261c);
    unsigned int* From;
    unsigned int Length;
    if (Workspace == 0)
    {
        Movie_ZeroLimbs(Dest, LimbCount);
        return 0;
    }
    if (CompareLimbs((int)Source, Count, (int)Modulus, (int)LimbCount) < 0)
    {
        From = Source;
        Length = Movie_SignificantCount(Source, Count);
    }
    else
    {
        Length = (unsigned int)Movie_DivideLimbs(Source, Count, Modulus, (int)LimbCount, Modulus + 0x45, 0u, Workspace);
        From = Workspace;
    }
    if (Modulus[0x44] == 0u)
    {
        Movie_CopyLimbs(Dest, From, Length);
        Movie_ZeroLimbs(Dest + Length, LimbCount - Length);
    }
    else
    {
        unsigned int Prefix = Modulus[0x41];
        Movie_CopyLimbs(Workspace + 0x40 + Prefix, From, Length);
        Movie_ZeroLimbs(Workspace + 0x40, Prefix);
        ReduceLimbsModulo(Workspace + 0x40, Prefix + Length, 0u, Dest, Modulus, Workspace + 0xc0);
    }
    Movie_FreeBlock(Workspace);
    return 1;
}

// 0x06ac4f40
// 386 (0x182) and 486 (0x1E6), and anything whose processor level is below 6, pass 4.
// Every other processor passes 0xC.
void __cdecl Movie_InitForProcessor()
{
    SYSTEM_INFO Info;
    GetSystemInfo(&Info);
    if (Info.dwProcessorType != 0x182u && Info.dwProcessorType != 0x1e6u && Info.wProcessorLevel >= 6)
    {
        FillWindowTable(0xcu);
    }
    else
    {
        FillWindowTable(4u);
    }
}

// 0x06ac4f80
// LocalAlloc(LMEM_FIXED). Status 0x2b on failure. Otherwise increments DAT_06ae0980.
void* __stdcall Movie_AllocateBlock(unsigned int Size)
{
    void* Block = LocalAlloc(0, Size);
    if (Block == 0)
    {
        GMovieGraphCode = 0x2b;
        return 0;
    }
    GMovieAllocCount = GMovieAllocCount + 1;
    return Block;
}

// 0x06ac4fb0
void __stdcall Movie_FreeBlock(void* Block)
{
    if (Block != 0)
    {
        GMovieAllocCount = GMovieAllocCount - 1;
        LocalFree(Block);
    }
}

// 0x06ac4fd0
// The unread store of 0 at [ESP+0x14] has no later read.
unsigned int __stdcall Movie_EstimateQuotient(unsigned int Carry, unsigned int Top, unsigned int Second, unsigned int* Reciprocal)
{
    return Movie_ReciprocalHigh(Carry, Top, Second, Reciprocal);
}

// 0x06ac5050
// Quotient and remainder of a 64-bit dividend by a 32-bit divisor.
// Status 9 when High >= Divisor, which also covers a zero divisor.
void __stdcall Movie_DivideDoubleWord(unsigned int Low, unsigned int High, unsigned int Divisor, int* Quotient, int* Remainder)
{
    unsigned long long Dividend;
    unsigned int Quot;
    if (High >= Divisor)
    {
        GMovieGraphCode = 9;
        return;
    }
    Dividend = ((unsigned long long)High << 32) | Low;
    Quot = (unsigned int)(Dividend / Divisor);
    *Quotient = (int)Quot;
    *Remainder = (int)(Low - Quot * Divisor);
}

// 0x06ac5090
// Divides Dividend by Divisor. Remainder lands in Remainder.
// Quotient digits are stored at Quotient when that address is nonzero, high digit first.
// Returns the remainder's significant length. Status 0xB, 0xC, or 0xA, and 0, on the error paths.
int __stdcall Movie_DivideLimbs(unsigned int* Dividend, unsigned int DividendCount, unsigned int* Divisor, int DivisorCount, unsigned int* Reciprocal, unsigned int Quotient, unsigned int* Remainder)
{
    if (DivisorCount == 0)
    {
        GMovieGraphCode = 0xb;
        return 0;
    }
    if (DividendCount < (unsigned int)DivisorCount)
    {
        unsigned int Length = Movie_SignificantCount(Dividend, DividendCount);
        Movie_CopyLimbs(Remainder, Dividend, DividendCount);
        return (int)Length;
    }
    if (Divisor[DivisorCount - 1] == 0u)
    {
        GMovieGraphCode = 0xc;
        return 0;
    }
    if (DivisorCount == 1)
    {
        Remainder[0] = Movie_DivideByLimb(Dividend, Divisor[0], Reciprocal, Quotient, (int)DividendCount);
    }
    else
    {
        unsigned int LocalReciprocal[2];
        unsigned int* Inverse = Reciprocal;
        int Index = (int)(DividendCount - (unsigned int)DivisorCount);
        if (Inverse == 0)
        {
            StoreLeadingReciprocal((int)Divisor, (unsigned int)DivisorCount, LocalReciprocal);
            Inverse = LocalReciprocal;
        }
        Remainder[DivisorCount - 1] = 0u;
        Movie_CopyLimbs(Remainder, Dividend + Index + 1, (unsigned int)(DivisorCount - 1));
        if (Index != -1)
        {
            unsigned int QuotientAt = Quotient + (unsigned int)Index * 4u;
            do
            {
                unsigned int SavedHigh = Remainder[DivisorCount - 1];
                int Cursor = DivisorCount - 1;
                unsigned int Digit = 0u;
                int bRunEstimate = 1;
                if (Cursor != 0)
                {
                    while (Cursor != 0)
                    {
                        Remainder[Cursor] = Remainder[Cursor - 1];
                        Cursor = Cursor - 1;
                    }
                }
                Remainder[0] = Dividend[Index];
                if (SavedHigh == 0u && Movie_CompareDescending(Remainder, Divisor, (unsigned int)DivisorCount) < 0)
                {
                    bRunEstimate = 0;
                }
                if (bRunEstimate != 0)
                {
                    unsigned int Top = Remainder[DivisorCount - 1];
                    unsigned int Second = Remainder[DivisorCount - 2];
                    unsigned int High = Movie_ReciprocalHigh(SavedHigh, Top, Second, Inverse);
                    unsigned int Mul;
                    if (High == 0xffffffffu)
                    {
                        Digit = 0xffffffffu;
                    }
                    else
                    {
                        Digit = High + 1u;
                    }
                    Mul = Movie_SubtractScaledLimbs((int)Divisor, Digit, Remainder, DivisorCount);
                    if (Mul > SavedHigh)
                    {
                        Digit = Digit - 1u;
                        long Carry = Movie_AddLimbsCarry(Remainder, Divisor, (int)Remainder, DivisorCount);
                        Mul = Mul - (unsigned int)Carry;
                    }
                    if (Mul != SavedHigh)
                    {
                        GMovieGraphCode = 0xa;
                        return 0;
                    }
                }
                if (Quotient != 0u)
                {
                    *(unsigned int*)QuotientAt = Digit;
                }
                Index = Index - 1;
                QuotientAt = QuotientAt - 4u;
            }
            while (Index != -1);
        }
    }
    return (int)Movie_SignificantCount(Remainder, (unsigned int)DivisorCount);
}
