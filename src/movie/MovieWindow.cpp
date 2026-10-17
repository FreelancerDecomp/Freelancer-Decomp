// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieWindow.h"
#include "MovieCca.h"
#include "MovieD4.h"
#include "MovieField.h"
#include "MovieGraph.h"

#include <intrin.h>

// Flag helpers match the x86 instructions used by 0x06aca920 and 0x06acb0a0.
// Carry is unchanged by INC, DEC, LEA, and MOV. Shift counts are masked to 5 bits;
// a masked count of 0 leaves the flags alone.

static unsigned int LoadDword(unsigned int Address)
{
    return *(unsigned int*)Address;
}

static void StoreDword(unsigned int Address, unsigned int Value)
{
    *(unsigned int*)Address = Value;
}

static unsigned char LoadByte(unsigned int Address)
{
    return *(unsigned char*)Address;
}

static void StoreByte(unsigned int Address, unsigned char Value)
{
    *(unsigned char*)Address = Value;
}

static void Push32(unsigned int& Esp, unsigned int Value)
{
    Esp = Esp - 4u;
    StoreDword(Esp, Value);
}

static unsigned int Pop32(unsigned int& Esp)
{
    unsigned int Value = LoadDword(Esp);
    Esp = Esp + 4u;
    return Value;
}

static void SetFlags32(unsigned int Result, unsigned int& Zero, unsigned int& SignFlag)
{
    Zero = (Result == 0u) ? 1u : 0u;
    SignFlag = Result >> 31;
}

static unsigned int Add32(unsigned int Left, unsigned int Right, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned long long Sum = (unsigned long long)Left + Right;
    unsigned int Result = (unsigned int)Sum;
    long long Signed = (long long)(int)Left + (long long)(int)Right;
    Carry = (unsigned int)(Sum >> 32);
    Overflow = (Signed < -2147483647LL - 1 || Signed > 2147483647LL) ? 1u : 0u;
    SetFlags32(Result, Zero, SignFlag);
    return Result;
}

static unsigned int Adc32(unsigned int Left, unsigned int Right, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned long long Sum = (unsigned long long)Left + Right + Carry;
    unsigned int Result = (unsigned int)Sum;
    long long Signed = (long long)(int)Left + (long long)(int)Right + (long long)Carry;
    Carry = (unsigned int)(Sum >> 32);
    Overflow = (Signed < -2147483647LL - 1 || Signed > 2147483647LL) ? 1u : 0u;
    SetFlags32(Result, Zero, SignFlag);
    return Result;
}

static unsigned int Sub32(unsigned int Left, unsigned int Right, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned long long Need = (unsigned long long)Right;
    unsigned int Result = Left - Right;
    long long Signed = (long long)(int)Left - (long long)(int)Right;
    Carry = (Left < Need) ? 1u : 0u;
    Overflow = (Signed < -2147483647LL - 1 || Signed > 2147483647LL) ? 1u : 0u;
    SetFlags32(Result, Zero, SignFlag);
    return Result;
}

static unsigned int Sbb32(unsigned int Left, unsigned int Right, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned long long Need = (unsigned long long)Right + Carry;
    unsigned int Result = (unsigned int)((unsigned long long)Left - Need);
    long long Signed = (long long)(int)Left - (long long)(int)Right - (long long)Carry;
    Carry = (Left < Need) ? 1u : 0u;
    Overflow = (Signed < -2147483647LL - 1 || Signed > 2147483647LL) ? 1u : 0u;
    SetFlags32(Result, Zero, SignFlag);
    return Result;
}

static unsigned int And32(unsigned int Left, unsigned int Right, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned int Result = Left & Right;
    Carry = 0u;
    Overflow = 0u;
    SetFlags32(Result, Zero, SignFlag);
    return Result;
}

static void Cmp32(unsigned int Left, unsigned int Right, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    Sub32(Left, Right, Carry, Zero, SignFlag, Overflow);
}

static unsigned int Neg32(unsigned int Value, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned int Result = 0u - Value;
    Carry = (Value != 0u) ? 1u : 0u;
    Overflow = (Value == 0x80000000u) ? 1u : 0u;
    SetFlags32(Result, Zero, SignFlag);
    return Result;
}

static unsigned int Inc32(unsigned int Value, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned int Result = Value + 1u;
    Overflow = (Value == 0x7fffffffu) ? 1u : 0u;
    SetFlags32(Result, Zero, SignFlag);
    return Result;
}

static unsigned int Dec32(unsigned int Value, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned int Result = Value - 1u;
    Overflow = (Value == 0x80000000u) ? 1u : 0u;
    SetFlags32(Result, Zero, SignFlag);
    return Result;
}

static unsigned int Shl32(unsigned int Value, unsigned int Count, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    Count = Count & 31u;
    if (Count == 0u)
    {
        return Value;
    }
    Carry = (Value >> (32u - Count)) & 1u;
    unsigned int Result = Value << Count;
    if (Count == 1u)
    {
        Overflow = ((Value >> 31) ^ (Value >> 30)) & 1u;
    }
    else
    {
        Overflow = 0u;
    }
    SetFlags32(Result, Zero, SignFlag);
    return Result;
}

static unsigned int Shr32(unsigned int Value, unsigned int Count, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    Count = Count & 31u;
    if (Count == 0u)
    {
        return Value;
    }
    Carry = (Value >> (Count - 1u)) & 1u;
    unsigned int Result = Value >> Count;
    if (Count == 1u)
    {
        Overflow = Value >> 31;
    }
    else
    {
        Overflow = 0u;
    }
    SetFlags32(Result, Zero, SignFlag);
    return Result;
}

static void CmpByte(unsigned int Left, unsigned int Right, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned int Diff = (Left - Right) & 0xffu;
    int Signed = (int)(signed char)Left - (int)(signed char)Right;
    Carry = (Left < Right) ? 1u : 0u;
    Zero = (Diff == 0u) ? 1u : 0u;
    SignFlag = (Diff >> 7) & 1u;
    Overflow = (Signed < -128 || Signed > 127) ? 1u : 0u;
}

static unsigned int XorByte(unsigned int Left, unsigned int Right, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned int Result = (Left ^ Right) & 0xffu;
    Carry = 0u;
    Overflow = 0u;
    Zero = (Result == 0u) ? 1u : 0u;
    SignFlag = (Result >> 7) & 1u;
    return Result;
}

static unsigned int SbbByte(unsigned int Left, unsigned int Right, unsigned int& Carry, unsigned int& Zero, unsigned int& SignFlag, unsigned int& Overflow)
{
    unsigned int Need = Right + Carry;
    unsigned int Result = (Left - Need) & 0xffu;
    int Signed = (int)(signed char)Left - (int)(signed char)Right - (int)Carry;
    Carry = (Left < Need) ? 1u : 0u;
    Zero = (Result == 0u) ? 1u : 0u;
    SignFlag = (Result >> 7) & 1u;
    Overflow = (Signed < -128 || Signed > 127) ? 1u : 0u;
    return Result;
}

// DAT_06add4cc. Index is Dispatch[2]. Empty slots are the retail null dwords.
// Slots 1..4, 6, 8 and 12 are Multiply*LimbPairs.
typedef void (__stdcall *FWidthProduct)(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Scale);

static FWidthProduct const GWidthProducts[13] =
{
    0,
    MultiplyOneLimbPairs,
    MultiplyTwoLimbPairs,
    MultiplyThreeLimbPairs,
    MultiplyFourLimbPairs,
    0,
    MultiplySixLimbPairs,
    0,
    MultiplyEightLimbPairs,
    0,
    0,
    0,
    MultiplyTwelveLimbPairs
};

// DAT_06ada800. 3^0 through 3^15. The dword before it (0x06ada7fc) is not read
// by the loops below: they start at index Halvings-1 and walk down to index 0.
static unsigned int const GPowersOfThree[16] =
{
    0x00000001u, 0x00000003u, 0x00000009u, 0x0000001bu,
    0x00000051u, 0x000000f3u, 0x000002d9u, 0x0000088bu,
    0x000019a1u, 0x00004ce3u, 0x0000e6a9u, 0x0002b3fbu,
    0x00081bf1u, 0x001857d3u, 0x0048fb79u, 0x00daf26bu
};

// 0x06ac8910. RET 0x14. Same arity as AddLimbs. Chosen when the mark bytes match.
long __stdcall Movie_SubtractLimbsWide(unsigned int* Left, int LeftCount, unsigned int* Right, int RightCount, int Sum);

// 0x06ac8500.
long __stdcall AddLimbs(unsigned int* Left, int LeftCount, unsigned int* Right, int RightCount, int Sum);

static void CopyOrZeroBytes(unsigned int* Dest, unsigned int* Source, unsigned int ByteCount, int FillZero)
{
    unsigned int Words = ByteCount >> 2;
    unsigned int Tail = ByteCount & 3u;
    unsigned int Index;
    unsigned char* DestBytes = (unsigned char*)(Dest + Words);
    unsigned int TailIndex;
    if (FillZero == 0)
    {
        unsigned char* SourceBytes = (unsigned char*)(Source + Words);
        for (Index = 0u; Index < Words; Index = Index + 1u)
        {
            Dest[Index] = Source[Index];
        }
        for (TailIndex = 0u; TailIndex < Tail; TailIndex = TailIndex + 1u)
        {
            DestBytes[TailIndex] = SourceBytes[TailIndex];
        }
    }
    else
    {
        for (Index = 0u; Index < Words; Index = Index + 1u)
        {
            Dest[Index] = 0u;
        }
        for (TailIndex = 0u; TailIndex < Tail; TailIndex = TailIndex + 1u)
        {
            DestBytes[TailIndex] = 0;
        }
    }
}

// 0x06ac9a30. Exponent is one limb. Below 2, Dest is replaced by the field one
// (Context+0x451) or by Base, and the return is 0. Otherwise the bits below the
// top pair walk a 2-bit window. The return is squares plus multiplies, including
// when Dest is null and the limb calls are skipped.
int __stdcall Movie_ExponentiateLimb(unsigned int* Base, unsigned int Exponent, unsigned int* Dest, unsigned int* Context)
{
    unsigned int MultiplyCount = 0u;
    unsigned int SquareCount = 0u;
    unsigned int ByteCount;
    unsigned int* Source;
    if (Exponent <= 1u)
    {
        if (Dest == 0)
        {
            return 0;
        }
        Source = Context + 0x451;
        if (Exponent != 0u)
        {
            Source = Base;
        }
        ByteCount = Context[0x40] << 2;
        CopyOrZeroBytes(Dest, Source, ByteCount, 0);
        return 0;
    }

    unsigned long BitIndex = 0;
    _BitScanReverse(&BitIndex, Exponent);
    unsigned int Shift = BitIndex + 1u - 2u;
    unsigned int Top = Exponent >> Shift;
    unsigned int TopIsThree = (Top == 3u) ? 1u : 0u;
    unsigned int* Work = (unsigned int*)Movie_AllocateBlock(0x2e94u);
    if (Work == 0)
    {
        if (Dest != 0)
        {
            ByteCount = Context[0x40] << 2;
            CopyOrZeroBytes(Dest, 0, ByteCount, 1);
        }
        return 0;
    }

    unsigned char* Raw = (unsigned char*)Work;
    unsigned int* Low = Work;
    unsigned int* LowTail = (unsigned int*)(Raw + 0x514);
    unsigned int* Second = (unsigned int*)(Raw + 0x53c);
    unsigned int* SecondTail = (unsigned int*)(Raw + 0xa50);
    unsigned int* Temp = (unsigned int*)(Raw + 0xa78);
    unsigned int* Scratch = (unsigned int*)(Raw + 0xb78);
    unsigned int* Dispatch = (unsigned int*)Context[0x42];
    unsigned int LimbCount = Context[0x40];
    if (Dest != 0)
    {
        Movie_EvaluateWindow(Base, LimbCount, Low, (unsigned char*)LowTail, Dispatch);
        Movie_MultiplyTermsModulo(Low, LowTail, Low, LowTail, Dest, Context, Scratch);
    }
    SquareCount = 1u;
    if (TopIsThree != 0u)
    {
        if (Dest != 0)
        {
            Movie_MultiplyFactorModulo(Dest, Low, LowTail, Dest, Context, Scratch);
        }
        MultiplyCount = 1u;
    }
    if (Shift != 0u)
    {
        unsigned int Window = 0u;
        if (Dest != 0)
        {
            Movie_EvaluateWindow(Dest, LimbCount, Second, (unsigned char*)SecondTail, Dispatch);
        }
        while (Shift != 0u)
        {
            Shift = Shift - 1u;
            unsigned int Bit = (Exponent >> Shift) & 1u;
            Window = Bit + Window * 2u;
            // 06ac9ba0. Above 3 the switch is skipped and the shift is tested again.
            if (Window > 3u)
            {
            }
            else if (Window <= 1u)
            {
                if (Dest != 0)
                {
                    Movie_MultiplyModulo(Dest, Dest, Dest, Context, Scratch);
                }
                SquareCount = SquareCount + 1u;
            }
            else if (Window == 2u)
            {
                MultiplyCount = MultiplyCount + 1u;
                SquareCount = SquareCount + 1u;
                Window = 0u;
                if (Dest != 0)
                {
                    Movie_MultiplyFactorModulo(Dest, Low, LowTail, Dest, Context, Scratch);
                    Movie_MultiplyModulo(Dest, Dest, Dest, Context, Scratch);
                }
            }
            else
            {
                if (TopIsThree == 0u)
                {
                    if (Dest != 0)
                    {
                        Movie_MultiplyTermsModulo(Second, SecondTail, Low, LowTail, Temp, Context, Scratch);
                        Movie_EvaluateWindow(Temp, LimbCount, Second, (unsigned char*)SecondTail, Dispatch);
                    }
                    TopIsThree = 1u;
                    MultiplyCount = MultiplyCount + 1u;
                }
                if (Dest != 0)
                {
                    Movie_MultiplyModulo(Dest, Dest, Dest, Context, Scratch);
                    Movie_MultiplyFactorModulo(Dest, Second, SecondTail, Dest, Context, Scratch);
                }
                MultiplyCount = MultiplyCount + 1u;
                SquareCount = SquareCount + 1u;
                Window = 0u;
            }
        }
        if (Window != 0u)
        {
            if (Dest != 0)
            {
                Movie_MultiplyFactorModulo(Dest, Low, LowTail, Dest, Context, Scratch);
            }
            MultiplyCount = MultiplyCount + 1u;
        }
    }
    Movie_FreeBlock(Work);
    return (int)(MultiplyCount + SquareCount);
}

// 0x06aca450.
void __stdcall Movie_FillByteBuffer(unsigned char* Buffer, unsigned int ByteCount)
{
    GenerateRngBytes(0, 0, Buffer, ByteCount);
}

// 0x06aca890. Dispatch[0] is the width, [1] the halving count, [2] the width-product
// index. The first call uses 3^Halvings. Each later step folds Product with one
// power, walking Dispatch[2] upward and the power table downward.
void __stdcall Movie_MultiplyWindows(unsigned int* Left, unsigned int* LeftTail, unsigned int* Right, unsigned int* RightTail, unsigned int* Product, unsigned int* Dispatch)
{
    unsigned int Halvings = Dispatch[1];
    unsigned int Width = Dispatch[0];
    unsigned int Selector = Dispatch[2];
    GWidthProducts[Selector](Left, Right, Product, GPowersOfThree[Halvings]);
    if (Halvings == 0u)
    {
        return;
    }
    unsigned int Shift = Halvings;
    unsigned int* Factor = Dispatch + 2;
    unsigned int const* Power = GPowersOfThree + (Halvings - 1u);
    while (Shift != 0u)
    {
        unsigned int Scale = *Power;
        unsigned int Half = Scale >> 1;
        Movie_FoldPartialProducts(
            Product,
            Width >> (Shift & 31u),
            Scale,
            (*Factor) << 1,
            (unsigned char*)LeftTail + Half,
            (unsigned char*)RightTail + Half);
        Power = Power - 1;
        Factor = Factor + 1;
        Shift = Shift - 1u;
    }
}

// 0x06acae10. Halvings == 0 calls the width product with scale 1 and returns.
// Otherwise the slot is Dispatch[Halvings+1]. Mark bytes choose add versus subtract
// at the end: equal selects 0x06ac8910, different selects 0x06ac8500.
void __stdcall Movie_MultiplyWindowCross(unsigned int* Left, unsigned char* LeftMark, unsigned int* Right, unsigned char* RightMark, unsigned int* Middle, unsigned int* Product, unsigned int* Dispatch)
{
    unsigned int Selector = Dispatch[2];
    unsigned int Halvings = Dispatch[1];
    FWidthProduct ProductFn = GWidthProducts[Selector];
    if (Halvings == 0u)
    {
        ProductFn(Left, Right, Product, 1u);
        return;
    }

    unsigned int Slot = Dispatch[Halvings + 1u];
    unsigned int Width = Dispatch[0];
    unsigned int Mask = (LeftMark[0] == RightMark[0]) ? 0u : 0xffffffffu;
    ProductFn(Left + Slot, Right + Slot, Product + (Slot * 2u), GPowersOfThree[Halvings - 1u] << 1);
    if (Halvings != 1u)
    {
        unsigned int Shift = Halvings;
        unsigned int LeftTrips = Halvings - 1u;
        unsigned int const* Power = GPowersOfThree + (Halvings - 2u);
        unsigned int* Factor = Dispatch + 2;
        while (LeftTrips != 0u)
        {
            unsigned int Current = Power[0];
            unsigned int Next = Power[1];
            unsigned int Sum = (Next >> 1) + Current;
            Movie_FoldPartialProducts(
                Product + (Slot * 2u),
                Width >> (Shift & 31u),
                Current * 2u,
                (*Factor) << 1,
                LeftMark + Sum,
                RightMark + Sum);
            Power = Power - 1;
            Factor = Factor + 1;
            Shift = Shift - 1u;
            LeftTrips = LeftTrips - 1u;
        }
    }

    unsigned int Half = Width >> 1;
    unsigned int CarryOut = 0u;
    unsigned int Adjust = 2u - Mask;
    unsigned int Index;
    if (Half != 0u)
    {
        for (Index = 0u; Index < Half; Index = Index + 1u)
        {
            unsigned int FromMiddle = Middle[Index];
            unsigned int FromSlot16 = Product[(Slot * 4u) + Index];
            unsigned int FromSlot8 = Product[(Slot * 2u) + Index];
            unsigned int FromHalf = Product[Half + Index];
            unsigned int Carry = 0u;
            unsigned int Zero = 0u;
            unsigned int SignFlag = 0u;
            unsigned int Overflow = 0u;
            unsigned int Mixed = FromSlot16 ^ Mask;
            unsigned int High = 0u;
            Mixed = Add32(Mixed, FromHalf, Carry, Zero, SignFlag, Overflow);
            High = Adc32(High, 0u, Carry, Zero, SignFlag, Overflow);
            Mixed = Add32(Mixed, ~FromSlot8, Carry, Zero, SignFlag, Overflow);
            High = Adc32(High, 0u, Carry, Zero, SignFlag, Overflow);
            Mixed = Add32(Mixed, ~FromMiddle, Carry, Zero, SignFlag, Overflow);
            High = Adc32(High, 0u, Carry, Zero, SignFlag, Overflow);
            Mixed = Add32(Mixed, Adjust, Carry, Zero, SignFlag, Overflow);
            High = Adc32(High, 0u, Carry, Zero, SignFlag, Overflow);
            Adjust = High;
            Mixed = Mixed + CarryOut;
            unsigned int Sum = FromSlot8 + Mixed;
            Product[Index] = FromMiddle;
            Product[Half + Index] = FromMiddle;
            Product[(Half * 2u) + Index] = Sum;
            if (Mixed != 0u)
            {
                CarryOut = (Sum < Mixed) ? 1u : 0u;
            }
        }
    }

    unsigned int* AtHalf = Product + Half;
    unsigned int* AtTwice = Product + (Half * 2u);
    unsigned int* AtTriple = Product + (Half * 3u);
    unsigned int* AtPair = Product + (Half + Slot * 2u);
    if (Half != 0u)
    {
        if (AtPair == AtTriple)
        {
            unsigned int Limb = AtTriple[0];
            unsigned int Limit = 0xffffffffu - CarryOut;
            if (Limb > Limit)
            {
                AddLimbs(AtPair, (int)Half, &CarryOut, 1, (int)AtTriple);
            }
            else
            {
                AtTriple[0] = Limb + CarryOut;
            }
        }
        else
        {
            AddLimbs(AtPair, (int)Half, &CarryOut, 1, (int)AtTriple);
        }
    }
    AddLimbs(AtHalf, (int)(Half * 3u), AtTwice, (int)(Half * 2u), (int)AtHalf);
    if (Mask == 0u)
    {
        Movie_SubtractLimbsWide(AtHalf, (int)(Half * 3u), Product + (Slot * 4u), (int)(Half * 2u), (int)AtHalf);
    }
    else
    {
        AddLimbs(AtHalf, (int)(Half * 3u), Product + (Slot * 4u), (int)(Half * 2u), (int)AtHalf);
    }
}

// 0x06acb290. Count above Dispatch[0] sets status 0x37 and returns.
// Otherwise the source is copied, the gap to the width is zeroed, and each
// halving calls 0x06acb0a0. DestTail is a byte address: the sign base adds Power>>1.
void __stdcall Movie_EvaluateWindow(unsigned int* Source, unsigned int Count, unsigned int* Dest, unsigned char* DestTail, unsigned int* Dispatch)
{
    unsigned int Depth = Dispatch[1];
    unsigned int Width = Dispatch[0];
    unsigned int WordIndex = Dispatch[Depth + 2u];
    if (Count > Width)
    {
        GMovieGraphCode = 0x37;
        return;
    }

    unsigned int CopyBytes = Count << 2;
    CopyOrZeroBytes(Dest, Source, CopyBytes, 0);
    unsigned int Gap = Width - Count;
    unsigned int GapBytes = Gap << 2;
    CopyOrZeroBytes(Dest + Count, 0, GapBytes, 1);
    if (Depth == 0u)
    {
        return;
    }

    unsigned int Level = 0u;
    unsigned int* Factor = Dispatch + Depth + 1u;
    while (Level != Depth)
    {
        unsigned int Power = GPowersOfThree[Level];
        unsigned int Scale = *Factor;
        Movie_SubtractHalves(
            Dest + WordIndex,
            Width >> ((Level + 1u) & 31u),
            Power,
            Scale,
            DestTail + (Power >> 1));
        Level = Level + 1u;
        Factor = Factor - 1;
    }
}

// 0x06aca920. Each statement is one listing instruction. Labels are the
// listing addresses. Groups == 0 returns before the limb work.
void __stdcall Movie_FoldPartialProducts(unsigned int* Base, unsigned int Count, unsigned int Groups, unsigned int Step, unsigned char* SignLeft, unsigned char* SignRight)
{
    unsigned int SpaceWords[40];
    unsigned int Esp = (unsigned int)(SpaceWords + 24);
    StoreDword(Esp + 4u, (unsigned int)Base);
    StoreDword(Esp + 8u, Count);
    StoreDword(Esp + 12u, Groups);
    StoreDword(Esp + 16u, Step);
    StoreDword(Esp + 20u, (unsigned int)SignLeft);
    StoreDword(Esp + 24u, (unsigned int)SignRight);
    unsigned int Eax = 0u;
    unsigned int Ecx = 0u;
    unsigned int Edx = 0u;
    unsigned int Ebx = 0u;
    unsigned int Esi = 0u;
    unsigned int Edi = 0u;
    unsigned int Ebp = 0u;
    unsigned int Carry = 0u;
    unsigned int Zero = 0u;
    unsigned int SignFlag = 0u;
    unsigned int Overflow = 0u;
    Eax = LoadDword((Esp) + 12u); // 06aca920 MOV         EAX,dword ptr [ESP + param_3]
    Esp = Sub32(Esp, 40u, Carry, Zero, SignFlag, Overflow); // 06aca924 SUB         ESP,0x28
    Eax = Neg32(Eax, Carry, Zero, SignFlag, Overflow); // 06aca927 NEG         EAX
    StoreDword((Esp) + 24u, Ebp); // 06aca929 MOV         dword ptr [ESP + local_10+0x28],EBP
    if (Zero != 0u) { goto Lab_06acae05; } // 06aca92d JZ          LAB_06acae05
    StoreDword((Esp) + 28u, Ebx); // 06aca933 MOV         dword ptr [ESP + local_c+0x28],EBX
    StoreDword((Esp) + 32u, Edi); // 06aca937 MOV         dword ptr [ESP + local_8+0x28],EDI
    StoreDword((Esp) + 36u, Esi); // 06aca93b MOV         dword ptr [ESP + local_4+0x28],ESI
    Ebp = Eax; // 06aca93f MOV         EBP,EAX
    Ecx = LoadDword((Esp) + 64u); // 06aca941 MOV         ECX,dword ptr [ESP + param_6+0x28]
    Ebx = LoadDword((Esp) + 60u); // 06aca945 MOV         EBX,dword ptr [ESP + param_5+0x28]
    Edi = LoadDword((Esp) + 56u); // 06aca949 MOV         EDI,dword ptr [ESP + param_4+0x28]
    Ecx = Sub32(Ecx, Ebp, Carry, Zero, SignFlag, Overflow); // 06aca94d SUB         ECX,EBP
    Edx = LoadDword((Esp) + 48u); // 06aca94f MOV         EDX,dword ptr [ESP + param_2+0x28]
    Esi = LoadDword((Esp) + 44u); // 06aca953 MOV         ESI,dword ptr [ESP + param_1+0x28]
    Ebx = Sub32(Ebx, Ebp, Carry, Zero, SignFlag, Overflow); // 06aca957 SUB         EBX,EBP
    StoreDword((Esp) + 12u, Ecx); // 06aca959 MOV         dword ptr [ESP + local_1c+0x28],ECX
    Ecx = Edi + Edi * 2u; // 06aca95d LEA         ECX,[EDI + EDI*0x2]
    Edi = Sub32(Edi, Edx, Carry, Zero, SignFlag, Overflow); // 06aca960 SUB         EDI,EDX
    Esi = Esi + Edx * 4u; // 06aca962 LEA         ESI,[ESI + EDX*0x4]
    Cmp32(Edi, Edx, Carry, Zero, SignFlag, Overflow); // 06aca965 CMP         EDI,EDX
    StoreDword((Esp) + 8u, Ebx); // 06aca967 MOV         dword ptr [ESP + local_20+0x28],EBX
    if (Zero == 0u) { goto Lab_06aca97f; } // 06aca96b JNZ         LAB_06aca97f
    Cmp32(Edx, 4u, Carry, Zero, SignFlag, Overflow); // 06aca96d CMP         EDX,0x4
    if (Zero != 0u) { goto Lab_06acacc4; } // 06aca970 JZ          LAB_06acacc4
    Cmp32(Edx, 3u, Carry, Zero, SignFlag, Overflow); // 06aca976 CMP         EDX,0x3
    if (Zero != 0u) { goto Lab_06acab65; } // 06aca979 JZ          LAB_06acab65
Lab_06aca97f:
    Edx = Shr32(Edx, 1u, Carry, Zero, SignFlag, Overflow); // 06aca97f SHR         EDX,0x1
    StoreDword((Esp) + 20u, Ecx); // 06aca981 MOV         dword ptr [ESP + local_14+0x28],ECX
    StoreDword((Esp) + 16u, Edi); // 06aca985 MOV         dword ptr [ESP + local_18+0x28],EDI
    if (Carry == 0u) { goto Lab_06aca9f0; } // 06aca989 JNC         LAB_06aca9f0
    if (Carry != 0u) { goto Lab_06acadf5; } // 06aca98b JC          LAB_06acadf5
Lab_06aca991:
    Ebx = LoadDword(Esi + Edi * 8u); // 06aca991 MOV         EBX,dword ptr [ESI + EDI*0x8]
    Ecx = LoadDword((Esi + Edi * 8u) + 4u); // 06aca994 MOV         ECX,dword ptr [ESI + EDI*0x8 + 0x4]
    Eax = Sbb32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06aca998 SBB         EAX,EBX
    Ebx = LoadDword((Esi) + 4u); // 06aca99a MOV         EBX,dword ptr [ESI + 0x4]
    StoreDword(Esi, Eax); // 06aca99d MOV         dword ptr [ESI],EAX
    Eax = LoadDword((Esi) + 8u); // 06aca99f MOV         EAX,dword ptr [ESI + 0x8]
    Ebx = Sbb32(Ebx, Ecx, Carry, Zero, SignFlag, Overflow); // 06aca9a2 SBB         EBX,ECX
    Ecx = LoadDword((Esi + Edi * 8u) + 8u); // 06aca9a4 MOV         ECX,dword ptr [ESI + EDI*0x8 + 0x8]
    StoreDword((Esi) + 4u, Ebx); // 06aca9a8 MOV         dword ptr [ESI + 0x4],EBX
    Ebx = LoadDword((Esi) + 12u); // 06aca9ab MOV         EBX,dword ptr [ESI + 0xc]
    Eax = Sbb32(Eax, Ecx, Carry, Zero, SignFlag, Overflow); // 06aca9ae SBB         EAX,ECX
    Ecx = LoadDword((Esi + Edi * 8u) + 12u); // 06aca9b0 MOV         ECX,dword ptr [ESI + EDI*0x8 + 0xc]
    StoreDword((Esi) + 8u, Eax); // 06aca9b4 MOV         dword ptr [ESI + 0x8],EAX
    Esi = (Esi) + 16u; // 06aca9b7 LEA         ESI,[ESI + 0x10]
    Ebx = Sbb32(Ebx, Ecx, Carry, Zero, SignFlag, Overflow); // 06aca9ba SBB         EBX,ECX
    Ebp = Dec32(Ebp, Zero, SignFlag, Overflow); // 06aca9bc DEC         EBP
    Eax = LoadDword(Esi); // 06aca9bd MOV         EAX,dword ptr [ESI]
    StoreDword((Esi) - 4u, Ebx); // 06aca9bf MOV         dword ptr [ESI + -0x4],EBX
    if (Zero == 0u) { goto Lab_06aca991; } // 06aca9c2 JNZ         LAB_06aca991
    Ecx = Edx; // 06aca9c4 MOV         ECX,EDX
    Ebx = LoadDword((Esp) + 4u); // 06aca9c6 MOV         EBX,dword ptr [ESP + local_24+0x28]
Lab_06aca9ca:
    Eax = Sbb32(Eax, Ebp, Carry, Zero, SignFlag, Overflow); // 06aca9ca SBB         EAX,EBP
    Ecx = Dec32(Ecx, Zero, SignFlag, Overflow); // 06aca9cc DEC         ECX
    StoreDword(Esi, Eax); // 06aca9cd MOV         dword ptr [ESI],EAX
    if (Carry == 0u) { goto Lab_06aca9d9; } // 06aca9cf JNC         LAB_06aca9d9
    Eax = LoadDword((Esi) + 4u); // 06aca9d1 MOV         EAX,dword ptr [ESI + 0x4]
    Esi = (Esi) + 4u; // 06aca9d4 LEA         ESI,[ESI + 0x4]
    if (Zero == 0u) { goto Lab_06aca9ca; } // 06aca9d7 JNZ         LAB_06aca9ca
Lab_06aca9d9:
    Ebp = LoadDword((Esp) + 4u); // 06aca9d9 MOV         EBP,dword ptr [ESP + local_24+0x28]
    Ecx = LoadDword((Esp) + 20u); // 06aca9dd MOV         ECX,dword ptr [ESP + local_14+0x28]
    Esi = LoadDword(Esp); // 06aca9e1 MOV         ESI,dword ptr [ESP]=>local_28
    Ebp = Inc32(Ebp, Zero, SignFlag, Overflow); // 06aca9e4 INC         EBP
    if (Zero != 0u) { goto Lab_06acadf5; } // 06aca9e5 JZ          LAB_06acadf5
    Edi = Sub32(Edi, Edx, Carry, Zero, SignFlag, Overflow); // 06aca9eb SUB         EDI,EDX
    Esi = Esi + Ecx * 4u; // 06aca9ed LEA         ESI,[ESI + ECX*0x4]
Lab_06aca9f0:
    StoreDword((Esp) + 4u, Ebp); // 06aca9f0 MOV         dword ptr [ESP + local_24+0x28],EBP
    StoreDword(Esp, Esi); // 06aca9f4 MOV         dword ptr [ESP]=>local_28,ESI
    Ebp = Edx; // 06aca9f7 MOV         EBP,EDX
    Eax = LoadDword(Esi + Edi * 4u); // 06aca9f9 MOV         EAX,dword ptr [ESI + EDI*0x4]
Lab_06aca9fc:
    Ebx = LoadDword(Esi); // 06aca9fc MOV         EBX,dword ptr [ESI]
    Ecx = LoadDword((Esi) + 4u); // 06aca9fe MOV         ECX,dword ptr [ESI + 0x4]
    Ebx = Adc32(Ebx, Eax, Carry, Zero, SignFlag, Overflow); // 06acaa01 ADC         EBX,EAX
    Eax = LoadDword((Esi + Edi * 4u) + 4u); // 06acaa03 MOV         EAX,dword ptr [ESI + EDI*0x4 + 0x4]
    StoreDword(Esi + Edx * 8u, Ebx); // 06acaa07 MOV         dword ptr [ESI + EDX*0x8],EBX
    Esi = (Esi) + 8u; // 06acaa0a LEA         ESI,[ESI + 0x8]
    Ecx = Adc32(Ecx, Eax, Carry, Zero, SignFlag, Overflow); // 06acaa0d ADC         ECX,EAX
    Ebp = Dec32(Ebp, Zero, SignFlag, Overflow); // 06acaa0f DEC         EBP
    Eax = LoadDword(Esi + Edi * 4u); // 06acaa10 MOV         EAX,dword ptr [ESI + EDI*0x4]
    StoreDword((Esi + Edx * 8u) - 4u, Ecx); // 06acaa13 MOV         dword ptr [ESI + EDX*0x8 + -0x4],ECX
    if (Zero == 0u) { goto Lab_06aca9fc; } // 06acaa17 JNZ         LAB_06aca9fc
    Ebp = Edx; // 06acaa19 MOV         EBP,EDX
    Ebx = LoadDword((Esi + Edi * 4u) + 4u); // 06acaa1b MOV         EBX,dword ptr [ESI + EDI*0x4 + 0x4]
Lab_06acaa1f:
    Eax = Adc32(Eax, 0u, Carry, Zero, SignFlag, Overflow); // 06acaa1f ADC         EAX,0x0
    Ecx = LoadDword(Esp); // 06acaa22 MOV         ECX,dword ptr [ESP]=>local_28
    Ebx = Adc32(Ebx, 0u, Carry, Zero, SignFlag, Overflow); // 06acaa25 ADC         EBX,0x0
    /* nop */ // 06acaa28 NOP
Lab_06acaa29:
    StoreDword(Esi + Edx * 8u, Eax); // 06acaa29 MOV         dword ptr [ESI + EDX*0x8],EAX
    StoreDword((Esi + Edx * 8u) + 4u, Ebx); // 06acaa2c MOV         dword ptr [ESI + EDX*0x8 + 0x4],EBX
    Eax = LoadDword((Esi + Edi * 4u) + 8u); // 06acaa30 MOV         EAX,dword ptr [ESI + EDI*0x4 + 0x8]
    Ebx = LoadDword((Esi + Edi * 4u) + 12u); // 06acaa34 MOV         EBX,dword ptr [ESI + EDI*0x4 + 0xc]
    Ebp = Dec32(Ebp, Zero, SignFlag, Overflow); // 06acaa38 DEC         EBP
    Esi = (Esi) + 8u; // 06acaa39 LEA         ESI,[ESI + 0x8]
    if (Carry == 0u && Zero == 0u) { goto Lab_06acaa29; } // 06acaa3c JA          LAB_06acaa29
    if (Zero == 0u) { goto Lab_06acaa1f; } // 06acaa3e JNZ         LAB_06acaa1f
    Ebp = Sub32(Ebp, Edx, Carry, Zero, SignFlag, Overflow); // 06acaa40 SUB         EBP,EDX
    Eax = LoadDword(Ecx + Edx * 8u); // 06acaa42 MOV         EAX,dword ptr [ECX + EDX*0x8]
    Edi = Edx; // 06acaa45 MOV         EDI,EDX
    Edx = And32(Edx, Edx, Carry, Zero, SignFlag, Overflow); // 06acaa47 AND         EDX,EDX
Lab_06acaa49:
    Ebx = LoadDword(Ecx + Ebp * 8u); // 06acaa49 MOV         EBX,dword ptr [ECX + EBP*0x8]
    Esi = LoadDword((Ecx + Ebp * 8u) + 4u); // 06acaa4c MOV         ESI,dword ptr [ECX + EBP*0x8 + 0x4]
    Ebx = Adc32(Ebx, Eax, Carry, Zero, SignFlag, Overflow); // 06acaa50 ADC         EBX,EAX
    Eax = LoadDword((Ecx + Edx * 8u) + 4u); // 06acaa52 MOV         EAX,dword ptr [ECX + EDX*0x8 + 0x4]
    StoreDword(Ecx, Ebx); // 06acaa56 MOV         dword ptr [ECX],EBX
    Ecx = (Ecx) + 8u; // 06acaa58 LEA         ECX,[ECX + 0x8]
    Esi = Adc32(Esi, Eax, Carry, Zero, SignFlag, Overflow); // 06acaa5b ADC         ESI,EAX
    Edi = Dec32(Edi, Zero, SignFlag, Overflow); // 06acaa5d DEC         EDI
    Eax = LoadDword(Ecx + Edx * 8u); // 06acaa5e MOV         EAX,dword ptr [ECX + EDX*0x8]
    StoreDword((Ecx) - 4u, Esi); // 06acaa61 MOV         dword ptr [ECX + -0x4],ESI
    if (Zero == 0u) { goto Lab_06acaa49; } // 06acaa64 JNZ         LAB_06acaa49
    Eax = LoadDword(Ecx); // 06acaa66 MOV         EAX,dword ptr [ECX]
    Ebp = Edx; // 06acaa68 MOV         EBP,EDX
Lab_06acaa6a:
    Ebx = LoadDword(Ecx + Edx * 8u); // 06acaa6a MOV         EBX,dword ptr [ECX + EDX*0x8]
    Esi = LoadDword((Ecx + Edx * 8u) + 4u); // 06acaa6d MOV         ESI,dword ptr [ECX + EDX*0x8 + 0x4]
    Ebx = Adc32(Ebx, Eax, Carry, Zero, SignFlag, Overflow); // 06acaa71 ADC         EBX,EAX
    Eax = LoadDword((Ecx) + 4u); // 06acaa73 MOV         EAX,dword ptr [ECX + 0x4]
    StoreDword(Ecx, Ebx); // 06acaa76 MOV         dword ptr [ECX],EBX
    Ecx = (Ecx) + 8u; // 06acaa78 LEA         ECX,[ECX + 0x8]
    Esi = Adc32(Esi, Eax, Carry, Zero, SignFlag, Overflow); // 06acaa7b ADC         ESI,EAX
    Ebp = Dec32(Ebp, Zero, SignFlag, Overflow); // 06acaa7d DEC         EBP
    Eax = LoadDword(Ecx); // 06acaa7e MOV         EAX,dword ptr [ECX]
    StoreDword((Ecx) - 4u, Esi); // 06acaa80 MOV         dword ptr [ECX + -0x4],ESI
    if (Zero == 0u) { goto Lab_06acaa6a; } // 06acaa83 JNZ         LAB_06acaa6a
    Ebp = Edx; // 06acaa85 MOV         EBP,EDX
    Ebx = LoadDword((Esp) + 4u); // 06acaa87 MOV         EBX,dword ptr [ESP + local_24+0x28]
Lab_06acaa8b:
    Eax = Adc32(Eax, 0u, Carry, Zero, SignFlag, Overflow); // 06acaa8b ADC         EAX,0x0
    Edi = LoadDword((Esp) + 8u); // 06acaa8e MOV         EDI,dword ptr [ESP + local_20+0x28]
    StoreDword(Ecx, Eax); // 06acaa92 MOV         dword ptr [ECX],EAX
    if (Carry == 0u) { goto Lab_06acaa9f; } // 06acaa94 JNC         LAB_06acaa9f
    Eax = LoadDword((Ecx) + 4u); // 06acaa96 MOV         EAX,dword ptr [ECX + 0x4]
    Ecx = (Ecx) + 4u; // 06acaa99 LEA         ECX,[ECX + 0x4]
    Ebp = Dec32(Ebp, Zero, SignFlag, Overflow); // 06acaa9c DEC         EBP
    if (Zero == 0u) { goto Lab_06acaa8b; } // 06acaa9d JNZ         LAB_06acaa8b
Lab_06acaa9f:
    Ecx = LoadDword((Esp) + 12u); // 06acaa9f MOV         ECX,dword ptr [ESP + local_1c+0x28]
    Esi = LoadDword(Esp); // 06acaaa3 MOV         ESI,dword ptr [ESP]=>local_28
    Eax = (Eax & 0xffff00ffu) | ((LoadByte(Edi + Ebx) & 0xffu) << 8); // 06acaaa6 MOV         AH,byte ptr [EDI + EBX*0x1]
    Edi = LoadDword((Esp) + 16u); // 06acaaa9 MOV         EDI,dword ptr [ESP + local_18+0x28]
    Eax = (Eax & 0xffffff00u) | (LoadByte(Ecx + Ebx) & 0xffu); // 06acaaad MOV         AL,byte ptr [ECX + EBX*0x1]
    Ebp = Edx; // 06acaab0 MOV         EBP,EDX
    Edi = Add32(Edi, Edx, Carry, Zero, SignFlag, Overflow); // 06acaab2 ADD         EDI,EDX
    Eax = (Eax & 0xffff00ffu) | ((XorByte(((Eax >> 8) & 0xffu), (Eax & 0xffu), Carry, Zero, SignFlag, Overflow) & 0xffu) << 8); // 06acaab4 XOR         AH,AL
    Eax = LoadDword(Esi); // 06acaab6 MOV         EAX,dword ptr [ESI]
    if (Zero != 0u) { goto Lab_06aca991; } // 06acaab8 JZ          LAB_06aca991
Lab_06acaabe:
    Ebx = LoadDword(Esi + Edi * 8u); // 06acaabe MOV         EBX,dword ptr [ESI + EDI*0x8]
    Ecx = LoadDword((Esi + Edi * 8u) + 4u); // 06acaac1 MOV         ECX,dword ptr [ESI + EDI*0x8 + 0x4]
    Eax = Adc32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acaac5 ADC         EAX,EBX
    Ebx = LoadDword((Esi) + 4u); // 06acaac7 MOV         EBX,dword ptr [ESI + 0x4]
    StoreDword(Esi, Eax); // 06acaaca MOV         dword ptr [ESI],EAX
    Eax = LoadDword((Esi) + 8u); // 06acaacc MOV         EAX,dword ptr [ESI + 0x8]
    Ebx = Adc32(Ebx, Ecx, Carry, Zero, SignFlag, Overflow); // 06acaacf ADC         EBX,ECX
    Ecx = LoadDword((Esi + Edi * 8u) + 8u); // 06acaad1 MOV         ECX,dword ptr [ESI + EDI*0x8 + 0x8]
    StoreDword((Esi) + 4u, Ebx); // 06acaad5 MOV         dword ptr [ESI + 0x4],EBX
    Ebx = LoadDword((Esi) + 12u); // 06acaad8 MOV         EBX,dword ptr [ESI + 0xc]
    Eax = Adc32(Eax, Ecx, Carry, Zero, SignFlag, Overflow); // 06acaadb ADC         EAX,ECX
    Ecx = LoadDword((Esi + Edi * 8u) + 12u); // 06acaadd MOV         ECX,dword ptr [ESI + EDI*0x8 + 0xc]
    StoreDword((Esi) + 8u, Eax); // 06acaae1 MOV         dword ptr [ESI + 0x8],EAX
    Esi = (Esi) + 16u; // 06acaae4 LEA         ESI,[ESI + 0x10]
    Ebx = Adc32(Ebx, Ecx, Carry, Zero, SignFlag, Overflow); // 06acaae7 ADC         EBX,ECX
    Ebp = Dec32(Ebp, Zero, SignFlag, Overflow); // 06acaae9 DEC         EBP
    Eax = LoadDword(Esi); // 06acaaea MOV         EAX,dword ptr [ESI]
    StoreDword((Esi) - 4u, Ebx); // 06acaaec MOV         dword ptr [ESI + -0x4],EBX
    if (Zero == 0u) { goto Lab_06acaabe; } // 06acaaef JNZ         LAB_06acaabe
    Ecx = Edx; // 06acaaf1 MOV         ECX,EDX
    Ebx = LoadDword((Esp) + 4u); // 06acaaf3 MOV         EBX,dword ptr [ESP + local_24+0x28]
Lab_06acaaf7:
    Eax = Adc32(Eax, Ebp, Carry, Zero, SignFlag, Overflow); // 06acaaf7 ADC         EAX,EBP
    Ecx = Dec32(Ecx, Zero, SignFlag, Overflow); // 06acaaf9 DEC         ECX
    StoreDword(Esi, Eax); // 06acaafa MOV         dword ptr [ESI],EAX
    if (Carry == 0u) { goto Lab_06aca9d9; } // 06acaafc JNC         LAB_06aca9d9
    Eax = LoadDword((Esi) + 4u); // 06acab02 MOV         EAX,dword ptr [ESI + 0x4]
    Esi = (Esi) + 4u; // 06acab05 LEA         ESI,[ESI + 0x4]
    if (Zero == 0u) { goto Lab_06acaaf7; } // 06acab08 JNZ         LAB_06acaaf7
    if (Zero != 0u) { goto Lab_06aca9d9; } // 06acab0a JZ          LAB_06aca9d9
Lab_06acab10:
    Ebx = Sub32(Ebx, Edi, Carry, Zero, SignFlag, Overflow); // 06acab10 SUB         EBX,EDI
    Edi = LoadDword((Esi) - 32u); // 06acab12 MOV         EDI,dword ptr [ESI + -0x20]
    StoreDword((Esi) - 72u, Ebx); // 06acab15 MOV         dword ptr [ESI + -0x48],EBX
    Ebx = LoadDword((Esi) - 64u); // 06acab18 MOV         EBX,dword ptr [ESI + -0x40]
    Ecx = Sbb32(Ecx, Edi, Carry, Zero, SignFlag, Overflow); // 06acab1b SBB         ECX,EDI
    Edi = LoadDword((Esi) - 28u); // 06acab1d MOV         EDI,dword ptr [ESI + -0x1c]
    StoreDword((Esi) - 68u, Ecx); // 06acab20 MOV         dword ptr [ESI + -0x44],ECX
    Ecx = LoadDword((Esi) - 60u); // 06acab23 MOV         ECX,dword ptr [ESI + -0x3c]
    Ebx = Sbb32(Ebx, Edi, Carry, Zero, SignFlag, Overflow); // 06acab26 SBB         EBX,EDI
    Edi = LoadDword((Esi) - 24u); // 06acab28 MOV         EDI,dword ptr [ESI + -0x18]
    StoreDword((Esi) - 64u, Ebx); // 06acab2b MOV         dword ptr [ESI + -0x40],EBX
    Ebx = LoadDword((Esi) - 56u); // 06acab2e MOV         EBX,dword ptr [ESI + -0x38]
    Ecx = Sbb32(Ecx, Edi, Carry, Zero, SignFlag, Overflow); // 06acab31 SBB         ECX,EDI
    Edi = LoadDword((Esi) - 20u); // 06acab33 MOV         EDI,dword ptr [ESI + -0x14]
    StoreDword((Esi) - 60u, Ecx); // 06acab36 MOV         dword ptr [ESI + -0x3c],ECX
    Ecx = LoadDword((Esi) - 52u); // 06acab39 MOV         ECX,dword ptr [ESI + -0x34]
    Ebx = Sbb32(Ebx, Edi, Carry, Zero, SignFlag, Overflow); // 06acab3c SBB         EBX,EDI
    Edi = LoadDword((Esi) - 16u); // 06acab3e MOV         EDI,dword ptr [ESI + -0x10]
    Ecx = Sbb32(Ecx, Edi, Carry, Zero, SignFlag, Overflow); // 06acab41 SBB         ECX,EDI
    Edi = LoadDword((Esi) - 48u); // 06acab43 MOV         EDI,dword ptr [ESI + -0x30]
    Edi = Sbb32(Edi, 0u, Carry, Zero, SignFlag, Overflow); // 06acab46 SBB         EDI,0x0
    StoreDword((Esi) - 56u, Ebx); // 06acab49 MOV         dword ptr [ESI + -0x38],EBX
    Edx = Sbb32(Edx, 0u, Carry, Zero, SignFlag, Overflow); // 06acab4c SBB         EDX,0x0
    StoreDword((Esi) - 52u, Ecx); // 06acab4f MOV         dword ptr [ESI + -0x34],ECX
    Eax = Sbb32(Eax, 0u, Carry, Zero, SignFlag, Overflow); // 06acab52 SBB         EAX,0x0
    StoreDword((Esi) - 48u, Edi); // 06acab55 MOV         dword ptr [ESI + -0x30],EDI
    Ebp = Inc32(Ebp, Zero, SignFlag, Overflow); // 06acab58 INC         EBP
    StoreDword((Esi) - 44u, Edx); // 06acab59 MOV         dword ptr [ESI + -0x2c],EDX
    StoreDword((Esi) - 40u, Eax); // 06acab5c MOV         dword ptr [ESI + -0x28],EAX
    if (Zero != 0u) { goto Lab_06acadf5; } // 06acab5f JZ          LAB_06acadf5
Lab_06acab65:
    Eax = LoadDword((Esi) + 12u); // 06acab65 MOV         EAX,dword ptr [ESI + 0xc]
    Edx = LoadDword(Esi); // 06acab68 MOV         EDX,dword ptr [ESI]
    Edi = LoadDword((Esi) + 4u); // 06acab6a MOV         EDI,dword ptr [ESI + 0x4]
    Ebx = LoadDword((Esi) + 16u); // 06acab6d MOV         EBX,dword ptr [ESI + 0x10]
    Eax = Add32(Eax, Edx, Carry, Zero, SignFlag, Overflow); // 06acab70 ADD         EAX,EDX
    Ecx = LoadDword((Esi) + 20u); // 06acab72 MOV         ECX,dword ptr [ESI + 0x14]
    Ebx = Adc32(Ebx, Edi, Carry, Zero, SignFlag, Overflow); // 06acab75 ADC         EBX,EDI
    Edx = LoadDword((Esi) + 8u); // 06acab77 MOV         EDX,dword ptr [ESI + 0x8]
    Ecx = Adc32(Ecx, Edx, Carry, Zero, SignFlag, Overflow); // 06acab7a ADC         ECX,EDX
    Edi = LoadDword((Esi) + 24u); // 06acab7c MOV         EDI,dword ptr [ESI + 0x18]
    Edi = Adc32(Edi, 0u, Carry, Zero, SignFlag, Overflow); // 06acab7f ADC         EDI,0x0
    Edx = LoadDword((Esi) + 28u); // 06acab82 MOV         EDX,dword ptr [ESI + 0x1c]
    Edx = Adc32(Edx, 0u, Carry, Zero, SignFlag, Overflow); // 06acab85 ADC         EDX,0x0
    StoreDword((Esi) + 24u, Edi); // 06acab88 MOV         dword ptr [ESI + 0x18],EDI
    Edi = LoadDword((Esi) + 32u); // 06acab8b MOV         EDI,dword ptr [ESI + 0x20]
    StoreDword((Esi) + 28u, Edx); // 06acab8e MOV         dword ptr [ESI + 0x1c],EDX
    Edi = Adc32(Edi, 0u, Carry, Zero, SignFlag, Overflow); // 06acab91 ADC         EDI,0x0
    Edx = LoadDword((Esi) - 12u); // 06acab94 MOV         EDX,dword ptr [ESI + -0xc]
    Edx = Add32(Edx, Eax, Carry, Zero, SignFlag, Overflow); // 06acab97 ADD         EDX,EAX
    StoreDword((Esi) + 32u, Edi); // 06acab99 MOV         dword ptr [ESI + 0x20],EDI
    Edi = LoadDword((Esi) - 8u); // 06acab9c MOV         EDI,dword ptr [ESI + -0x8]
    StoreDword(Esi, Edx); // 06acab9f MOV         dword ptr [ESI],EDX
    Edi = Adc32(Edi, Ebx, Carry, Zero, SignFlag, Overflow); // 06acaba1 ADC         EDI,EBX
    Edx = LoadDword((Esi) - 4u); // 06acaba3 MOV         EDX,dword ptr [ESI + -0x4]
    Edx = Adc32(Edx, Ecx, Carry, Zero, SignFlag, Overflow); // 06acaba6 ADC         EDX,ECX
    StoreDword((Esi) + 4u, Edi); // 06acaba8 MOV         dword ptr [ESI + 0x4],EDI
    Edi = LoadDword((Esi) + 24u); // 06acabab MOV         EDI,dword ptr [ESI + 0x18]
    StoreDword((Esi) + 8u, Edx); // 06acabae MOV         dword ptr [ESI + 0x8],EDX
    Eax = Adc32(Eax, Edi, Carry, Zero, SignFlag, Overflow); // 06acabb1 ADC         EAX,EDI
    Edx = LoadDword((Esi) + 28u); // 06acabb3 MOV         EDX,dword ptr [ESI + 0x1c]
    Ebx = Adc32(Ebx, Edx, Carry, Zero, SignFlag, Overflow); // 06acabb6 ADC         EBX,EDX
    StoreDword((Esi) + 12u, Eax); // 06acabb8 MOV         dword ptr [ESI + 0xc],EAX
    Eax = LoadDword((Esi) + 32u); // 06acabbb MOV         EAX,dword ptr [ESI + 0x20]
    StoreDword((Esi) + 16u, Ebx); // 06acabbe MOV         dword ptr [ESI + 0x10],EBX
    Ecx = Adc32(Ecx, Eax, Carry, Zero, SignFlag, Overflow); // 06acabc1 ADC         ECX,EAX
    Ebx = LoadDword((Esp) + 8u); // 06acabc3 MOV         EBX,dword ptr [ESP + local_20+0x28]
    Edi = Adc32(Edi, 0u, Carry, Zero, SignFlag, Overflow); // 06acabc7 ADC         EDI,0x0
    StoreDword((Esi) + 20u, Ecx); // 06acabca MOV         dword ptr [ESI + 0x14],ECX
    Edx = Adc32(Edx, 0u, Carry, Zero, SignFlag, Overflow); // 06acabcd ADC         EDX,0x0
    Ecx = LoadDword((Esp) + 12u); // 06acabd0 MOV         ECX,dword ptr [ESP + local_1c+0x28]
    Esi = (Esi) + 72u; // 06acabd4 LEA         ESI,[ESI + 0x48]
    Ebx = (Ebx & 0xffff00ffu) | ((LoadByte(Ebx + Ebp) & 0xffu) << 8); // 06acabd7 MOV         BH,byte ptr [EBX + EBP*0x1]
    Eax = Adc32(Eax, 0u, Carry, Zero, SignFlag, Overflow); // 06acabda ADC         EAX,0x0
    Ebx = (Ebx & 0xffffff00u) | (LoadByte(Ecx + Ebp) & 0xffu); // 06acabdd MOV         BL,byte ptr [ECX + EBP*0x1]
    StoreDword((Esi) - 48u, Edi); // 06acabe0 MOV         dword ptr [ESI + -0x30],EDI
    CmpByte(((Ebx >> 8) & 0xffu), (Ebx & 0xffu), Carry, Zero, SignFlag, Overflow); // 06acabe3 CMP         BH,BL
    Ebx = LoadDword((Esi) - 72u); // 06acabe5 MOV         EBX,dword ptr [ESI + -0x48]
    Ecx = LoadDword((Esi) - 68u); // 06acabe8 MOV         ECX,dword ptr [ESI + -0x44]
    Edi = LoadDword((Esi) - 36u); // 06acabeb MOV         EDI,dword ptr [ESI + -0x24]
    if (Zero != 0u) { goto Lab_06acab10; } // 06acabee JZ          LAB_06acab10
    Ebx = Add32(Ebx, Edi, Carry, Zero, SignFlag, Overflow); // 06acabf4 ADD         EBX,EDI
    Edi = LoadDword((Esi) - 32u); // 06acabf6 MOV         EDI,dword ptr [ESI + -0x20]
    StoreDword((Esi) - 72u, Ebx); // 06acabf9 MOV         dword ptr [ESI + -0x48],EBX
    Ebx = LoadDword((Esi) - 64u); // 06acabfc MOV         EBX,dword ptr [ESI + -0x40]
    Ecx = Adc32(Ecx, Edi, Carry, Zero, SignFlag, Overflow); // 06acabff ADC         ECX,EDI
    Edi = LoadDword((Esi) - 28u); // 06acac01 MOV         EDI,dword ptr [ESI + -0x1c]
    StoreDword((Esi) - 68u, Ecx); // 06acac04 MOV         dword ptr [ESI + -0x44],ECX
    Ecx = LoadDword((Esi) - 60u); // 06acac07 MOV         ECX,dword ptr [ESI + -0x3c]
    Ebx = Adc32(Ebx, Edi, Carry, Zero, SignFlag, Overflow); // 06acac0a ADC         EBX,EDI
    Edi = LoadDword((Esi) - 24u); // 06acac0c MOV         EDI,dword ptr [ESI + -0x18]
    StoreDword((Esi) - 64u, Ebx); // 06acac0f MOV         dword ptr [ESI + -0x40],EBX
    Ebx = LoadDword((Esi) - 56u); // 06acac12 MOV         EBX,dword ptr [ESI + -0x38]
    Ecx = Adc32(Ecx, Edi, Carry, Zero, SignFlag, Overflow); // 06acac15 ADC         ECX,EDI
    Edi = LoadDword((Esi) - 20u); // 06acac17 MOV         EDI,dword ptr [ESI + -0x14]
    StoreDword((Esi) - 60u, Ecx); // 06acac1a MOV         dword ptr [ESI + -0x3c],ECX
    Ecx = LoadDword((Esi) - 52u); // 06acac1d MOV         ECX,dword ptr [ESI + -0x34]
    Ebx = Adc32(Ebx, Edi, Carry, Zero, SignFlag, Overflow); // 06acac20 ADC         EBX,EDI
    Edi = LoadDword((Esi) - 16u); // 06acac22 MOV         EDI,dword ptr [ESI + -0x10]
    Ecx = Adc32(Ecx, Edi, Carry, Zero, SignFlag, Overflow); // 06acac25 ADC         ECX,EDI
    Edi = LoadDword((Esi) - 48u); // 06acac27 MOV         EDI,dword ptr [ESI + -0x30]
    Edi = Adc32(Edi, 0u, Carry, Zero, SignFlag, Overflow); // 06acac2a ADC         EDI,0x0
    StoreDword((Esi) - 56u, Ebx); // 06acac2d MOV         dword ptr [ESI + -0x38],EBX
    Edx = Adc32(Edx, 0u, Carry, Zero, SignFlag, Overflow); // 06acac30 ADC         EDX,0x0
    StoreDword((Esi) - 52u, Ecx); // 06acac33 MOV         dword ptr [ESI + -0x34],ECX
    Eax = Adc32(Eax, 0u, Carry, Zero, SignFlag, Overflow); // 06acac36 ADC         EAX,0x0
    StoreDword((Esi) - 48u, Edi); // 06acac39 MOV         dword ptr [ESI + -0x30],EDI
    Ebp = Inc32(Ebp, Zero, SignFlag, Overflow); // 06acac3c INC         EBP
    StoreDword((Esi) - 44u, Edx); // 06acac3d MOV         dword ptr [ESI + -0x2c],EDX
    StoreDword((Esi) - 40u, Eax); // 06acac40 MOV         dword ptr [ESI + -0x28],EAX
    if (Zero == 0u) { goto Lab_06acab65; } // 06acac43 JNZ         LAB_06acab65
    if (Zero != 0u) { goto Lab_06acadf5; } // 06acac49 JZ          LAB_06acadf5
Lab_06acac4f:
    Ecx = Sub32(Ecx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acac4f SUB         ECX,EBP
    Ebp = LoadDword((Esi) - 44u); // 06acac51 MOV         EBP,dword ptr [ESI + -0x2c]
    StoreDword((Esi) - 96u, Ecx); // 06acac54 MOV         dword ptr [ESI + -0x60],ECX
    Ecx = LoadDword((Esi) - 88u); // 06acac57 MOV         ECX,dword ptr [ESI + -0x58]
    Edx = Sbb32(Edx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acac5a SBB         EDX,EBP
    Ebp = LoadDword((Esi) - 40u); // 06acac5c MOV         EBP,dword ptr [ESI + -0x28]
    StoreDword((Esi) - 92u, Edx); // 06acac5f MOV         dword ptr [ESI + -0x5c],EDX
    Edx = LoadDword((Esi) - 84u); // 06acac62 MOV         EDX,dword ptr [ESI + -0x54]
    Ecx = Sbb32(Ecx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acac65 SBB         ECX,EBP
    Ebp = LoadDword((Esi) - 36u); // 06acac67 MOV         EBP,dword ptr [ESI + -0x24]
    StoreDword((Esi) - 88u, Ecx); // 06acac6a MOV         dword ptr [ESI + -0x58],ECX
    Ecx = LoadDword((Esi) - 80u); // 06acac6d MOV         ECX,dword ptr [ESI + -0x50]
    Edx = Sbb32(Edx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acac70 SBB         EDX,EBP
    Ebp = LoadDword((Esi) - 32u); // 06acac72 MOV         EBP,dword ptr [ESI + -0x20]
    StoreDword((Esi) - 84u, Edx); // 06acac75 MOV         dword ptr [ESI + -0x54],EDX
    Edx = LoadDword((Esi) - 76u); // 06acac78 MOV         EDX,dword ptr [ESI + -0x4c]
    Ecx = Sbb32(Ecx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acac7b SBB         ECX,EBP
    Ebp = LoadDword((Esi) - 28u); // 06acac7d MOV         EBP,dword ptr [ESI + -0x1c]
    StoreDword((Esi) - 80u, Ecx); // 06acac80 MOV         dword ptr [ESI + -0x50],ECX
    Ecx = LoadDword((Esi) - 72u); // 06acac83 MOV         ECX,dword ptr [ESI + -0x48]
    Edx = Sbb32(Edx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acac86 SBB         EDX,EBP
    Ebp = LoadDword((Esi) - 24u); // 06acac88 MOV         EBP,dword ptr [ESI + -0x18]
    StoreDword((Esi) - 76u, Edx); // 06acac8b MOV         dword ptr [ESI + -0x4c],EDX
    Edx = LoadDword((Esi) - 68u); // 06acac8e MOV         EDX,dword ptr [ESI + -0x44]
    Ecx = Sbb32(Ecx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acac91 SBB         ECX,EBP
    Ebp = LoadDword((Esi) - 20u); // 06acac93 MOV         EBP,dword ptr [ESI + -0x14]
    StoreDword((Esi) - 72u, Ecx); // 06acac96 MOV         dword ptr [ESI + -0x48],ECX
    Ecx = LoadDword((Esi) - 64u); // 06acac99 MOV         ECX,dword ptr [ESI + -0x40]
    Edx = Sbb32(Edx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acac9c SBB         EDX,EBP
    Ebp = LoadDword((Esp) + 4u); // 06acac9e MOV         EBP,dword ptr [ESP + local_24+0x28]
    Ecx = Sbb32(Ecx, 0u, Carry, Zero, SignFlag, Overflow); // 06acaca2 SBB         ECX,0x0
    StoreDword((Esi) - 68u, Edx); // 06acaca5 MOV         dword ptr [ESI + -0x44],EDX
    Edi = Sbb32(Edi, 0u, Carry, Zero, SignFlag, Overflow); // 06acaca8 SBB         EDI,0x0
    StoreDword((Esi) - 64u, Ecx); // 06acacab MOV         dword ptr [ESI + -0x40],ECX
    Eax = Sbb32(Eax, 0u, Carry, Zero, SignFlag, Overflow); // 06acacae SBB         EAX,0x0
    StoreDword((Esi) - 60u, Edi); // 06acacb1 MOV         dword ptr [ESI + -0x3c],EDI
    Ebx = Sbb32(Ebx, 0u, Carry, Zero, SignFlag, Overflow); // 06acacb4 SBB         EBX,0x0
    StoreDword((Esi) - 56u, Eax); // 06acacb7 MOV         dword ptr [ESI + -0x38],EAX
    Ebp = Inc32(Ebp, Zero, SignFlag, Overflow); // 06acacba INC         EBP
    StoreDword((Esi) - 52u, Ebx); // 06acacbb MOV         dword ptr [ESI + -0x34],EBX
    if (Zero != 0u) { goto Lab_06acadf5; } // 06acacbe JZ          LAB_06acadf5
Lab_06acacc4:
    Eax = LoadDword((Esi) + 16u); // 06acacc4 MOV         EAX,dword ptr [ESI + 0x10]
    Ebx = LoadDword((Esi) + 20u); // 06acacc7 MOV         EBX,dword ptr [ESI + 0x14]
    Edx = LoadDword(Esi); // 06acacca MOV         EDX,dword ptr [ESI]
    Edi = LoadDword((Esi) + 4u); // 06acaccc MOV         EDI,dword ptr [ESI + 0x4]
    Eax = Add32(Eax, Edx, Carry, Zero, SignFlag, Overflow); // 06acaccf ADD         EAX,EDX
    StoreDword((Esp) + 4u, Ebp); // 06acacd1 MOV         dword ptr [ESP + local_24+0x28],EBP
    Ebx = Adc32(Ebx, Edi, Carry, Zero, SignFlag, Overflow); // 06acacd5 ADC         EBX,EDI
    Ebp = LoadDword((Esi) + 8u); // 06acacd7 MOV         EBP,dword ptr [ESI + 0x8]
    Ecx = LoadDword((Esi) + 24u); // 06acacda MOV         ECX,dword ptr [ESI + 0x18]
    Edx = LoadDword((Esi) + 28u); // 06acacdd MOV         EDX,dword ptr [ESI + 0x1c]
    Ecx = Adc32(Ecx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acace0 ADC         ECX,EBP
    Edi = LoadDword((Esi) + 12u); // 06acace2 MOV         EDI,dword ptr [ESI + 0xc]
    Edx = Adc32(Edx, Edi, Carry, Zero, SignFlag, Overflow); // 06acace5 ADC         EDX,EDI
    Ebp = LoadDword((Esi) + 32u); // 06acace7 MOV         EBP,dword ptr [ESI + 0x20]
    Ebp = Adc32(Ebp, 0u, Carry, Zero, SignFlag, Overflow); // 06acacea ADC         EBP,0x0
    Edi = LoadDword((Esi) + 36u); // 06acaced MOV         EDI,dword ptr [ESI + 0x24]
    Edi = Adc32(Edi, 0u, Carry, Zero, SignFlag, Overflow); // 06acacf0 ADC         EDI,0x0
    StoreDword((Esi) + 32u, Ebp); // 06acacf3 MOV         dword ptr [ESI + 0x20],EBP
    Ebp = LoadDword((Esi) + 40u); // 06acacf6 MOV         EBP,dword ptr [ESI + 0x28]
    StoreDword((Esi) + 36u, Edi); // 06acacf9 MOV         dword ptr [ESI + 0x24],EDI
    Ebp = Adc32(Ebp, 0u, Carry, Zero, SignFlag, Overflow); // 06acacfc ADC         EBP,0x0
    Edi = LoadDword((Esi) + 44u); // 06acacff MOV         EDI,dword ptr [ESI + 0x2c]
    StoreDword((Esi) + 40u, Ebp); // 06acad02 MOV         dword ptr [ESI + 0x28],EBP
    Ebp = LoadDword((Esi) - 16u); // 06acad05 MOV         EBP,dword ptr [ESI + -0x10]
    Edi = Adc32(Edi, 0u, Carry, Zero, SignFlag, Overflow); // 06acad08 ADC         EDI,0x0
    Ebp = Add32(Ebp, Eax, Carry, Zero, SignFlag, Overflow); // 06acad0b ADD         EBP,EAX
    StoreDword((Esi) + 44u, Edi); // 06acad0d MOV         dword ptr [ESI + 0x2c],EDI
    Edi = LoadDword((Esi) - 12u); // 06acad10 MOV         EDI,dword ptr [ESI + -0xc]
    Edi = Adc32(Edi, Ebx, Carry, Zero, SignFlag, Overflow); // 06acad13 ADC         EDI,EBX
    StoreDword(Esi, Ebp); // 06acad15 MOV         dword ptr [ESI],EBP
    Ebp = LoadDword((Esi) - 8u); // 06acad17 MOV         EBP,dword ptr [ESI + -0x8]
    StoreDword((Esi) + 4u, Edi); // 06acad1a MOV         dword ptr [ESI + 0x4],EDI
    Ebp = Adc32(Ebp, Ecx, Carry, Zero, SignFlag, Overflow); // 06acad1d ADC         EBP,ECX
    Edi = LoadDword((Esi) - 4u); // 06acad1f MOV         EDI,dword ptr [ESI + -0x4]
    Edi = Adc32(Edi, Edx, Carry, Zero, SignFlag, Overflow); // 06acad22 ADC         EDI,EDX
    StoreDword((Esi) + 8u, Ebp); // 06acad24 MOV         dword ptr [ESI + 0x8],EBP
    Ebp = LoadDword((Esi) + 32u); // 06acad27 MOV         EBP,dword ptr [ESI + 0x20]
    StoreDword((Esi) + 12u, Edi); // 06acad2a MOV         dword ptr [ESI + 0xc],EDI
    Eax = Adc32(Eax, Ebp, Carry, Zero, SignFlag, Overflow); // 06acad2d ADC         EAX,EBP
    Edi = LoadDword((Esi) + 36u); // 06acad2f MOV         EDI,dword ptr [ESI + 0x24]
    Ebx = Adc32(Ebx, Edi, Carry, Zero, SignFlag, Overflow); // 06acad32 ADC         EBX,EDI
    StoreDword((Esi) + 16u, Eax); // 06acad34 MOV         dword ptr [ESI + 0x10],EAX
    Eax = LoadDword((Esi) + 40u); // 06acad37 MOV         EAX,dword ptr [ESI + 0x28]
    StoreDword((Esi) + 20u, Ebx); // 06acad3a MOV         dword ptr [ESI + 0x14],EBX
    Ecx = Adc32(Ecx, Eax, Carry, Zero, SignFlag, Overflow); // 06acad3d ADC         ECX,EAX
    Ebx = LoadDword((Esi) + 44u); // 06acad3f MOV         EBX,dword ptr [ESI + 0x2c]
    StoreDword((Esi) + 24u, Ecx); // 06acad42 MOV         dword ptr [ESI + 0x18],ECX
    Esi = (Esi) + 96u; // 06acad45 LEA         ESI,[ESI + 0x60]
    Edx = Adc32(Edx, Ebx, Carry, Zero, SignFlag, Overflow); // 06acad48 ADC         EDX,EBX
    Ecx = LoadDword((Esp) + 4u); // 06acad4a MOV         ECX,dword ptr [ESP + local_24+0x28]
    Ebp = Adc32(Ebp, 0u, Carry, Zero, SignFlag, Overflow); // 06acad4e ADC         EBP,0x0
    StoreDword((Esi) - 68u, Edx); // 06acad51 MOV         dword ptr [ESI + -0x44],EDX
    Edi = Adc32(Edi, 0u, Carry, Zero, SignFlag, Overflow); // 06acad54 ADC         EDI,0x0
    Edx = LoadDword((Esp) + 8u); // 06acad57 MOV         EDX,dword ptr [ESP + local_20+0x28]
    StoreDword((Esi) - 64u, Ebp); // 06acad5b MOV         dword ptr [ESI + -0x40],EBP
    Ebp = LoadDword((Esp) + 12u); // 06acad5e MOV         EBP,dword ptr [ESP + local_1c+0x28]
    Eax = Adc32(Eax, 0u, Carry, Zero, SignFlag, Overflow); // 06acad62 ADC         EAX,0x0
    Edx = (Edx & 0xffff00ffu) | ((LoadByte(Edx + Ecx) & 0xffu) << 8); // 06acad65 MOV         DH,byte ptr [EDX + ECX*0x1]
    Ebx = Adc32(Ebx, 0u, Carry, Zero, SignFlag, Overflow); // 06acad68 ADC         EBX,0x0
    Edx = (Edx & 0xffffff00u) | (LoadByte(Ebp + Ecx) & 0xffu); // 06acad6b MOV         DL,byte ptr [EBP + ECX*0x1]
    CmpByte(((Edx >> 8) & 0xffu), (Edx & 0xffu), Carry, Zero, SignFlag, Overflow); // 06acad6f CMP         DH,DL
    Ebp = LoadDword((Esi) - 48u); // 06acad71 MOV         EBP,dword ptr [ESI + -0x30]
    Ecx = LoadDword((Esi) - 96u); // 06acad74 MOV         ECX,dword ptr [ESI + -0x60]
    Edx = LoadDword((Esi) - 92u); // 06acad77 MOV         EDX,dword ptr [ESI + -0x5c]
    if (Zero != 0u) { goto Lab_06acac4f; } // 06acad7a JZ          LAB_06acac4f
    Ecx = Add32(Ecx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acad80 ADD         ECX,EBP
    Ebp = LoadDword((Esi) - 44u); // 06acad82 MOV         EBP,dword ptr [ESI + -0x2c]
    StoreDword((Esi) - 96u, Ecx); // 06acad85 MOV         dword ptr [ESI + -0x60],ECX
    Ecx = LoadDword((Esi) - 88u); // 06acad88 MOV         ECX,dword ptr [ESI + -0x58]
    Edx = Adc32(Edx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acad8b ADC         EDX,EBP
    Ebp = LoadDword((Esi) - 40u); // 06acad8d MOV         EBP,dword ptr [ESI + -0x28]
    StoreDword((Esi) - 92u, Edx); // 06acad90 MOV         dword ptr [ESI + -0x5c],EDX
    Edx = LoadDword((Esi) - 84u); // 06acad93 MOV         EDX,dword ptr [ESI + -0x54]
    Ecx = Adc32(Ecx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acad96 ADC         ECX,EBP
    Ebp = LoadDword((Esi) - 36u); // 06acad98 MOV         EBP,dword ptr [ESI + -0x24]
    StoreDword((Esi) - 88u, Ecx); // 06acad9b MOV         dword ptr [ESI + -0x58],ECX
    Ecx = LoadDword((Esi) - 80u); // 06acad9e MOV         ECX,dword ptr [ESI + -0x50]
    Edx = Adc32(Edx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acada1 ADC         EDX,EBP
    Ebp = LoadDword((Esi) - 32u); // 06acada3 MOV         EBP,dword ptr [ESI + -0x20]
    StoreDword((Esi) - 84u, Edx); // 06acada6 MOV         dword ptr [ESI + -0x54],EDX
    Edx = LoadDword((Esi) - 76u); // 06acada9 MOV         EDX,dword ptr [ESI + -0x4c]
    Ecx = Adc32(Ecx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acadac ADC         ECX,EBP
    Ebp = LoadDword((Esi) - 28u); // 06acadae MOV         EBP,dword ptr [ESI + -0x1c]
    StoreDword((Esi) - 80u, Ecx); // 06acadb1 MOV         dword ptr [ESI + -0x50],ECX
    Ecx = LoadDword((Esi) - 72u); // 06acadb4 MOV         ECX,dword ptr [ESI + -0x48]
    Edx = Adc32(Edx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acadb7 ADC         EDX,EBP
    Ebp = LoadDword((Esi) - 24u); // 06acadb9 MOV         EBP,dword ptr [ESI + -0x18]
    StoreDword((Esi) - 76u, Edx); // 06acadbc MOV         dword ptr [ESI + -0x4c],EDX
    Edx = LoadDword((Esi) - 68u); // 06acadbf MOV         EDX,dword ptr [ESI + -0x44]
    Ecx = Adc32(Ecx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acadc2 ADC         ECX,EBP
    Ebp = LoadDword((Esi) - 20u); // 06acadc4 MOV         EBP,dword ptr [ESI + -0x14]
    StoreDword((Esi) - 72u, Ecx); // 06acadc7 MOV         dword ptr [ESI + -0x48],ECX
    Ecx = LoadDword((Esi) - 64u); // 06acadca MOV         ECX,dword ptr [ESI + -0x40]
    Edx = Adc32(Edx, Ebp, Carry, Zero, SignFlag, Overflow); // 06acadcd ADC         EDX,EBP
    Ebp = LoadDword((Esp) + 4u); // 06acadcf MOV         EBP,dword ptr [ESP + local_24+0x28]
    Ecx = Adc32(Ecx, 0u, Carry, Zero, SignFlag, Overflow); // 06acadd3 ADC         ECX,0x0
    StoreDword((Esi) - 68u, Edx); // 06acadd6 MOV         dword ptr [ESI + -0x44],EDX
    Edi = Adc32(Edi, 0u, Carry, Zero, SignFlag, Overflow); // 06acadd9 ADC         EDI,0x0
    StoreDword((Esi) - 64u, Ecx); // 06acaddc MOV         dword ptr [ESI + -0x40],ECX
    Eax = Adc32(Eax, 0u, Carry, Zero, SignFlag, Overflow); // 06acaddf ADC         EAX,0x0
    StoreDword((Esi) - 60u, Edi); // 06acade2 MOV         dword ptr [ESI + -0x3c],EDI
    Ebx = Adc32(Ebx, 0u, Carry, Zero, SignFlag, Overflow); // 06acade5 ADC         EBX,0x0
    StoreDword((Esi) - 56u, Eax); // 06acade8 MOV         dword ptr [ESI + -0x38],EAX
    Ebp = Inc32(Ebp, Zero, SignFlag, Overflow); // 06acadeb INC         EBP
    StoreDword((Esi) - 52u, Ebx); // 06acadec MOV         dword ptr [ESI + -0x34],EBX
    if (Zero == 0u) { goto Lab_06acacc4; } // 06acadef JNZ         LAB_06acacc4
Lab_06acadf5:
    Ebp = LoadDword((Esp) + 24u); // 06acadf5 MOV         EBP,dword ptr [ESP + local_10+0x28]
    Ebx = LoadDword((Esp) + 28u); // 06acadf9 MOV         EBX,dword ptr [ESP + local_c+0x28]
    Edi = LoadDword((Esp) + 32u); // 06acadfd MOV         EDI,dword ptr [ESP + local_8+0x28]
    Esi = LoadDword((Esp) + 36u); // 06acae01 MOV         ESI,dword ptr [ESP + local_4+0x28]
Lab_06acae05:
    Esp = Add32(Esp, 40u, Carry, Zero, SignFlag, Overflow); // 06acae05 ADD         ESP,0x28
    return; // 06acae08 RET         0x18
}

// 0x06acb0a0. Each statement is one listing instruction.
void __stdcall Movie_SubtractHalves(unsigned int* Limbs, unsigned int Count, unsigned int Repeats, unsigned int Step, unsigned char* Signs)
{
    unsigned int SpaceWords[40];
    unsigned int Esp = (unsigned int)(SpaceWords + 24);
    StoreDword(Esp + 4u, (unsigned int)Limbs);
    StoreDword(Esp + 8u, Count);
    StoreDword(Esp + 12u, Repeats);
    StoreDword(Esp + 16u, Step);
    StoreDword(Esp + 20u, (unsigned int)Signs);
    unsigned int Eax = 0u;
    unsigned int Ecx = 0u;
    unsigned int Edx = 0u;
    unsigned int Ebx = 0u;
    unsigned int Esi = 0u;
    unsigned int Edi = 0u;
    unsigned int Ebp = 0u;
    unsigned int Carry = 0u;
    unsigned int Zero = 0u;
    unsigned int SignFlag = 0u;
    unsigned int Overflow = 0u;
    Ecx = LoadDword((Esp) + 12u); // 06acb0a0 MOV         ECX,dword ptr [ESP + param_3]
    Esp = Sub32(Esp, 16u, Carry, Zero, SignFlag, Overflow); // 06acb0a4 SUB         ESP,0x10
    Cmp32(Ecx, 0u, Carry, Zero, SignFlag, Overflow); // 06acb0a7 CMP         ECX,0x0
    if (Zero != 0u) { goto Lab_06acb289; } // 06acb0aa JZ          LAB_06acb289
    StoreDword((Esp) + 8u, Edi); // 06acb0b0 MOV         dword ptr [ESP + local_8+0x10],EDI
    StoreDword((Esp) + 12u, Esi); // 06acb0b4 MOV         dword ptr [ESP + local_4+0x10],ESI
    Edi = LoadDword((Esp) + 32u); // 06acb0b8 MOV         EDI,dword ptr [ESP + param_4+0x10]
    Edx = LoadDword((Esp) + 24u); // 06acb0bc MOV         EDX,dword ptr [ESP + param_2+0x10]
    StoreDword(Esp, Ebp); // 06acb0c0 MOV         dword ptr [ESP]=>local_10,EBP
    StoreDword((Esp) + 4u, Ebx); // 06acb0c3 MOV         dword ptr [ESP + local_c+0x10],EBX
    Eax = Edi + Edi * 2u; // 06acb0c7 LEA         EAX,[EDI + EDI*0x2]
    Esi = LoadDword((Esp) + 20u); // 06acb0ca MOV         ESI,dword ptr [ESP + param_1+0x10]
    Eax = Shl32(Eax, 2u, Carry, Zero, SignFlag, Overflow); // 06acb0ce SHL         EAX,0x2
    Cmp32(Edx, Edi, Carry, Zero, SignFlag, Overflow); // 06acb0d1 CMP         EDX,EDI
    Ebp = LoadDword((Esp) + 36u); // 06acb0d3 MOV         EBP,dword ptr [ESP + param_5+0x10]
    if (Zero == 0u) { goto Lab_06acb132; } // 06acb0d7 JNZ         LAB_06acb132
    Cmp32(Edx, 4u, Carry, Zero, SignFlag, Overflow); // 06acb0d9 CMP         EDX,0x4
    if (Zero != 0u) { goto Lab_06acb21a; } // 06acb0dc JZ          LAB_06acb21a
    Cmp32(Edx, 3u, Carry, Zero, SignFlag, Overflow); // 06acb0e2 CMP         EDX,0x3
    if (Zero != 0u) { goto Lab_06acb1a0; } // 06acb0e5 JZ          LAB_06acb1a0
    if (Zero == 0u) { goto Lab_06acb132; } // 06acb0eb JNZ         LAB_06acb132
Lab_06acb0ed:
    Ebp = Shr32(Ebp, 1u, Carry, Zero, SignFlag, Overflow); // 06acb0ed SHR         EBP,0x1
    Eax = LoadDword(Esi); // 06acb0ef MOV         EAX,dword ptr [ESI]
Lab_06acb0f1:
    Ebx = LoadDword(Esi + Edx * 4u); // 06acb0f1 MOV         EBX,dword ptr [ESI + EDX*0x4]
    Ecx = LoadDword((Esi + Edx * 4u) + 4u); // 06acb0f4 MOV         ECX,dword ptr [ESI + EDX*0x4 + 0x4]
    Ebx = Sbb32(Ebx, Eax, Carry, Zero, SignFlag, Overflow); // 06acb0f8 SBB         EBX,EAX
    Eax = LoadDword((Esi) + 4u); // 06acb0fa MOV         EAX,dword ptr [ESI + 0x4]
    StoreDword(Esi + Edi * 8u, Ebx); // 06acb0fd MOV         dword ptr [ESI + EDI*0x8],EBX
    Esi = (Esi) + 8u; // 06acb100 LEA         ESI,[ESI + 0x8]
    Ecx = Sbb32(Ecx, Eax, Carry, Zero, SignFlag, Overflow); // 06acb103 SBB         ECX,EAX
    Ebp = Dec32(Ebp, Zero, SignFlag, Overflow); // 06acb105 DEC         EBP
    Eax = LoadDword(Esi); // 06acb106 MOV         EAX,dword ptr [ESI]
    StoreDword((Esi + Edi * 8u) - 4u, Ecx); // 06acb108 MOV         dword ptr [ESI + EDI*0x8 + -0x4],ECX
    if (Zero == 0u) { goto Lab_06acb0f1; } // 06acb10c JNZ         LAB_06acb0f1
    Ebp = Edx; // 06acb10e MOV         EBP,EDX
Lab_06acb110:
    Eax = LoadDword((Esi + Edx * 4u) - 4u); // 06acb110 MOV         EAX,dword ptr [ESI + EDX*0x4 + -0x4]
    Ebx = LoadDword((Esi + Edx * 4u) - 8u); // 06acb114 MOV         EBX,dword ptr [ESI + EDX*0x4 + -0x8]
    Ebp = Sub32(Ebp, 2u, Carry, Zero, SignFlag, Overflow); // 06acb118 SUB         EBP,0x2
    StoreDword((Esi + Edi * 4u) - 4u, Eax); // 06acb11b MOV         dword ptr [ESI + EDI*0x4 + -0x4],EAX
    StoreDword((Esi + Edi * 4u) - 8u, Ebx); // 06acb11f MOV         dword ptr [ESI + EDI*0x4 + -0x8],EBX
    Esi = (Esi) - 8u; // 06acb123 LEA         ESI,[ESI + -0x8]
    if (Zero == 0u) { goto Lab_06acb110; } // 06acb126 JNZ         LAB_06acb110
    Ecx = Pop32(Esp); // 06acb128 POP         ECX
    Ebp = Pop32(Esp); // 06acb129 POP         EBP
    Ecx = Dec32(Ecx, Zero, SignFlag, Overflow); // 06acb12a DEC         ECX
    Eax = Pop32(Esp); // 06acb12b POP         EAX
    if (Zero != 0u) { goto Lab_06acb27a; } // 06acb12c JZ          LAB_06acb27a
Lab_06acb132:
    Ebx = (Esi + Edx * 4u) - 4u; // 06acb132 LEA         EBX,[ESI + EDX*0x4 + -0x4]
    Esi = Sub32(Esi, Eax, Carry, Zero, SignFlag, Overflow); // 06acb136 SUB         ESI,EAX
    Ebx = Sub32(Ebx, Eax, Carry, Zero, SignFlag, Overflow); // 06acb138 SUB         EBX,EAX
    Push32(Esp, Eax); // 06acb13a PUSH        EAX
    Push32(Esp, Ebp); // 06acb13b PUSH        EBP
    Push32(Esp, Ecx); // 06acb13c PUSH        ECX
    Ecx = (Ebp + Ecx) - 1u; // 06acb13d LEA         ECX,[EBP + ECX*0x1 + -0x1]
Lab_06acb141:
    Eax = LoadDword(Ebx); // 06acb141 MOV         EAX,dword ptr [EBX]
    Ebp = LoadDword(Ebx + Edx * 4u); // 06acb143 MOV         EBP,dword ptr [EBX + EDX*0x4]
    Cmp32(Eax, Ebp, Carry, Zero, SignFlag, Overflow); // 06acb146 CMP         EAX,EBP
    if (Zero == 0u) { goto Lab_06acb151; } // 06acb148 JNZ         LAB_06acb151
    Cmp32(Esi, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb14a CMP         ESI,EBX
    Ebx = (Ebx) - 4u; // 06acb14c LEA         EBX,[EBX + -0x4]
    if (Zero == 0u) { goto Lab_06acb141; } // 06acb14f JNZ         LAB_06acb141
Lab_06acb151:
    { // 06acb151 SBB         AL,AL
    unsigned int Alu = SbbByte((Eax & 0xffu), (Eax & 0xffu), Carry, Zero, SignFlag, Overflow);
    Eax = (Eax & 0xffffff00u) | (Alu & 0xffu);
    }
    Ebp = Edx; // 06acb153 MOV         EBP,EDX
    StoreByte(Ecx, (unsigned char)((Eax & 0xffu))); // 06acb155 MOV         byte ptr [ECX],AL
    if (Zero == 0u) { goto Lab_06acb0ed; } // 06acb157 JNZ         LAB_06acb0ed
    Ebp = Shr32(Ebp, 1u, Carry, Zero, SignFlag, Overflow); // 06acb159 SHR         EBP,0x1
    Eax = LoadDword(Esi); // 06acb15b MOV         EAX,dword ptr [ESI]
Lab_06acb15d:
    Ebx = LoadDword(Esi + Edx * 4u); // 06acb15d MOV         EBX,dword ptr [ESI + EDX*0x4]
    Ecx = LoadDword((Esi + Edx * 4u) + 4u); // 06acb160 MOV         ECX,dword ptr [ESI + EDX*0x4 + 0x4]
    Eax = Sbb32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb164 SBB         EAX,EBX
    Ebx = LoadDword((Esi) + 4u); // 06acb166 MOV         EBX,dword ptr [ESI + 0x4]
    StoreDword(Esi + Edi * 8u, Eax); // 06acb169 MOV         dword ptr [ESI + EDI*0x8],EAX
    Esi = (Esi) + 8u; // 06acb16c LEA         ESI,[ESI + 0x8]
    Ebx = Sbb32(Ebx, Ecx, Carry, Zero, SignFlag, Overflow); // 06acb16f SBB         EBX,ECX
    Ebp = Dec32(Ebp, Zero, SignFlag, Overflow); // 06acb171 DEC         EBP
    StoreDword((Esi + Edi * 8u) - 4u, Ebx); // 06acb172 MOV         dword ptr [ESI + EDI*0x8 + -0x4],EBX
    Eax = LoadDword(Esi); // 06acb176 MOV         EAX,dword ptr [ESI]
    if (Zero == 0u) { goto Lab_06acb15d; } // 06acb178 JNZ         LAB_06acb15d
    Ebp = Edx; // 06acb17a MOV         EBP,EDX
    if (Zero != 0u) { goto Lab_06acb110; } // 06acb17c JZ          LAB_06acb110
Lab_06acb17e:
    Ebx = Sub32(Ebx, Eax, Carry, Zero, SignFlag, Overflow); // 06acb17e SUB         EBX,EAX
    Edx = LoadDword((Esi) + 4u); // 06acb180 MOV         EDX,dword ptr [ESI + 0x4]
    Eax = LoadDword((Esi) + 16u); // 06acb183 MOV         EAX,dword ptr [ESI + 0x10]
    StoreDword((Esi) + 24u, Ebx); // 06acb186 MOV         dword ptr [ESI + 0x18],EBX
    Eax = Sbb32(Eax, Edx, Carry, Zero, SignFlag, Overflow); // 06acb189 SBB         EAX,EDX
    Edx = LoadDword((Esi) + 8u); // 06acb18b MOV         EDX,dword ptr [ESI + 0x8]
    Ebx = LoadDword((Esi) + 20u); // 06acb18e MOV         EBX,dword ptr [ESI + 0x14]
    StoreDword((Esi) + 28u, Eax); // 06acb191 MOV         dword ptr [ESI + 0x1c],EAX
    Ebx = Sbb32(Ebx, Edx, Carry, Zero, SignFlag, Overflow); // 06acb194 SBB         EBX,EDX
    Ecx = Dec32(Ecx, Zero, SignFlag, Overflow); // 06acb196 DEC         ECX
    StoreDword((Esi) + 32u, Ebx); // 06acb197 MOV         dword ptr [ESI + 0x20],EBX
    if (Zero != 0u) { goto Lab_06acb27a; } // 06acb19a JZ          LAB_06acb27a
Lab_06acb1a0:
    Eax = LoadDword((Esi) - 28u); // 06acb1a0 MOV         EAX,dword ptr [ESI + -0x1c]
    Ebx = LoadDword((Esi) - 16u); // 06acb1a3 MOV         EBX,dword ptr [ESI + -0x10]
    Cmp32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb1a6 CMP         EAX,EBX
    if (Zero == 0u) { goto Lab_06acb1bc; } // 06acb1a8 JNZ         LAB_06acb1bc
    Eax = LoadDword((Esi) - 32u); // 06acb1aa MOV         EAX,dword ptr [ESI + -0x20]
    Ebx = LoadDword((Esi) - 20u); // 06acb1ad MOV         EBX,dword ptr [ESI + -0x14]
    Cmp32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb1b0 CMP         EAX,EBX
    if (Zero == 0u) { goto Lab_06acb1bc; } // 06acb1b2 JNZ         LAB_06acb1bc
    Eax = LoadDword((Esi) - 36u); // 06acb1b4 MOV         EAX,dword ptr [ESI + -0x24]
    Ebx = LoadDword((Esi) - 24u); // 06acb1b7 MOV         EBX,dword ptr [ESI + -0x18]
    Cmp32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb1ba CMP         EAX,EBX
Lab_06acb1bc:
    { // 06acb1bc SBB         DL,DL
    unsigned int Alu = SbbByte((Edx & 0xffu), (Edx & 0xffu), Carry, Zero, SignFlag, Overflow);
    Edx = (Edx & 0xffffff00u) | (Alu & 0xffu);
    }
    Eax = LoadDword((Esi) - 36u); // 06acb1be MOV         EAX,dword ptr [ESI + -0x24]
    Ebx = LoadDword((Esi) - 24u); // 06acb1c1 MOV         EBX,dword ptr [ESI + -0x18]
    Esi = (Esi) - 36u; // 06acb1c4 LEA         ESI,[ESI + -0x24]
    StoreByte((Ebp + Ecx) - 1u, (unsigned char)((Edx & 0xffu))); // 06acb1c7 MOV         byte ptr [EBP + ECX*0x1 + -0x1],DL
    if (SignFlag != Overflow) { goto Lab_06acb17e; } // 06acb1cb JL          LAB_06acb17e
    Eax = Sub32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb1cd SUB         EAX,EBX
    Edx = LoadDword((Esi) + 4u); // 06acb1cf MOV         EDX,dword ptr [ESI + 0x4]
    Ebx = LoadDword((Esi) + 16u); // 06acb1d2 MOV         EBX,dword ptr [ESI + 0x10]
    StoreDword((Esi) + 24u, Eax); // 06acb1d5 MOV         dword ptr [ESI + 0x18],EAX
    Edx = Sbb32(Edx, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb1d8 SBB         EDX,EBX
    Eax = LoadDword((Esi) + 8u); // 06acb1da MOV         EAX,dword ptr [ESI + 0x8]
    Ebx = LoadDword((Esi) + 20u); // 06acb1dd MOV         EBX,dword ptr [ESI + 0x14]
    StoreDword((Esi) + 28u, Edx); // 06acb1e0 MOV         dword ptr [ESI + 0x1c],EDX
    Eax = Sbb32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb1e3 SBB         EAX,EBX
    Ecx = Dec32(Ecx, Zero, SignFlag, Overflow); // 06acb1e5 DEC         ECX
    StoreDword((Esi) + 32u, Eax); // 06acb1e6 MOV         dword ptr [ESI + 0x20],EAX
    if (Zero == 0u) { goto Lab_06acb1a0; } // 06acb1e9 JNZ         LAB_06acb1a0
    if (Zero != 0u) { goto Lab_06acb27a; } // 06acb1eb JZ          LAB_06acb27a
Lab_06acb1f1:
    Ebx = Sub32(Ebx, Eax, Carry, Zero, SignFlag, Overflow); // 06acb1f1 SUB         EBX,EAX
    Edx = LoadDword((Esi) + 4u); // 06acb1f3 MOV         EDX,dword ptr [ESI + 0x4]
    Eax = LoadDword((Esi) + 20u); // 06acb1f6 MOV         EAX,dword ptr [ESI + 0x14]
    StoreDword((Esi) + 32u, Ebx); // 06acb1f9 MOV         dword ptr [ESI + 0x20],EBX
    Eax = Sbb32(Eax, Edx, Carry, Zero, SignFlag, Overflow); // 06acb1fc SBB         EAX,EDX
    Edx = LoadDword((Esi) + 8u); // 06acb1fe MOV         EDX,dword ptr [ESI + 0x8]
    Ebx = LoadDword((Esi) + 24u); // 06acb201 MOV         EBX,dword ptr [ESI + 0x18]
    StoreDword((Esi) + 36u, Eax); // 06acb204 MOV         dword ptr [ESI + 0x24],EAX
    Ebx = Sbb32(Ebx, Edx, Carry, Zero, SignFlag, Overflow); // 06acb207 SBB         EBX,EDX
    Eax = LoadDword((Esi) + 12u); // 06acb209 MOV         EAX,dword ptr [ESI + 0xc]
    Edx = LoadDword((Esi) + 28u); // 06acb20c MOV         EDX,dword ptr [ESI + 0x1c]
    StoreDword((Esi) + 40u, Ebx); // 06acb20f MOV         dword ptr [ESI + 0x28],EBX
    Edx = Sbb32(Edx, Eax, Carry, Zero, SignFlag, Overflow); // 06acb212 SBB         EDX,EAX
    Ecx = Dec32(Ecx, Zero, SignFlag, Overflow); // 06acb214 DEC         ECX
    StoreDword((Esi) + 44u, Edx); // 06acb215 MOV         dword ptr [ESI + 0x2c],EDX
    if (Zero != 0u) { goto Lab_06acb27a; } // 06acb218 JZ          LAB_06acb27a
Lab_06acb21a:
    Eax = LoadDword((Esi) - 36u); // 06acb21a MOV         EAX,dword ptr [ESI + -0x24]
    Ebx = LoadDword((Esi) - 20u); // 06acb21d MOV         EBX,dword ptr [ESI + -0x14]
    Cmp32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb220 CMP         EAX,EBX
    if (Zero == 0u) { goto Lab_06acb240; } // 06acb222 JNZ         LAB_06acb240
    Eax = LoadDword((Esi) - 40u); // 06acb224 MOV         EAX,dword ptr [ESI + -0x28]
    Ebx = LoadDword((Esi) - 24u); // 06acb227 MOV         EBX,dword ptr [ESI + -0x18]
    Cmp32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb22a CMP         EAX,EBX
    if (Zero == 0u) { goto Lab_06acb240; } // 06acb22c JNZ         LAB_06acb240
    Eax = LoadDword((Esi) - 44u); // 06acb22e MOV         EAX,dword ptr [ESI + -0x2c]
    Ebx = LoadDword((Esi) - 28u); // 06acb231 MOV         EBX,dword ptr [ESI + -0x1c]
    Cmp32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb234 CMP         EAX,EBX
    if (Zero == 0u) { goto Lab_06acb240; } // 06acb236 JNZ         LAB_06acb240
    Eax = LoadDword((Esi) - 48u); // 06acb238 MOV         EAX,dword ptr [ESI + -0x30]
    Ebx = LoadDword((Esi) - 32u); // 06acb23b MOV         EBX,dword ptr [ESI + -0x20]
    Cmp32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb23e CMP         EAX,EBX
Lab_06acb240:
    { // 06acb240 SBB         DL,DL
    unsigned int Alu = SbbByte((Edx & 0xffu), (Edx & 0xffu), Carry, Zero, SignFlag, Overflow);
    Edx = (Edx & 0xffffff00u) | (Alu & 0xffu);
    }
    Eax = LoadDword((Esi) - 48u); // 06acb242 MOV         EAX,dword ptr [ESI + -0x30]
    Ebx = LoadDword((Esi) - 32u); // 06acb245 MOV         EBX,dword ptr [ESI + -0x20]
    Esi = (Esi) - 48u; // 06acb248 LEA         ESI,[ESI + -0x30]
    StoreByte((Ebp + Ecx) - 1u, (unsigned char)((Edx & 0xffu))); // 06acb24b MOV         byte ptr [EBP + ECX*0x1 + -0x1],DL
    if (SignFlag != Overflow) { goto Lab_06acb1f1; } // 06acb24f JL          LAB_06acb1f1
    Eax = Sub32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb251 SUB         EAX,EBX
    Edx = LoadDword((Esi) + 4u); // 06acb253 MOV         EDX,dword ptr [ESI + 0x4]
    Ebx = LoadDword((Esi) + 20u); // 06acb256 MOV         EBX,dword ptr [ESI + 0x14]
    StoreDword((Esi) + 32u, Eax); // 06acb259 MOV         dword ptr [ESI + 0x20],EAX
    Edx = Sbb32(Edx, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb25c SBB         EDX,EBX
    Eax = LoadDword((Esi) + 8u); // 06acb25e MOV         EAX,dword ptr [ESI + 0x8]
    Ebx = LoadDword((Esi) + 24u); // 06acb261 MOV         EBX,dword ptr [ESI + 0x18]
    StoreDword((Esi) + 36u, Edx); // 06acb264 MOV         dword ptr [ESI + 0x24],EDX
    Eax = Sbb32(Eax, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb267 SBB         EAX,EBX
    Edx = LoadDword((Esi) + 12u); // 06acb269 MOV         EDX,dword ptr [ESI + 0xc]
    Ebx = LoadDword((Esi) + 28u); // 06acb26c MOV         EBX,dword ptr [ESI + 0x1c]
    StoreDword((Esi) + 40u, Eax); // 06acb26f MOV         dword ptr [ESI + 0x28],EAX
    Edx = Sbb32(Edx, Ebx, Carry, Zero, SignFlag, Overflow); // 06acb272 SBB         EDX,EBX
    Ecx = Dec32(Ecx, Zero, SignFlag, Overflow); // 06acb274 DEC         ECX
    StoreDword((Esi) + 44u, Edx); // 06acb275 MOV         dword ptr [ESI + 0x2c],EDX
    if (Zero == 0u) { goto Lab_06acb21a; } // 06acb278 JNZ         LAB_06acb21a
Lab_06acb27a:
    Ebp = LoadDword(Esp); // 06acb27a MOV         EBP,dword ptr [ESP]=>local_10
    Ebx = LoadDword((Esp) + 4u); // 06acb27d MOV         EBX,dword ptr [ESP + local_c+0x10]
    Edi = LoadDword((Esp) + 8u); // 06acb281 MOV         EDI,dword ptr [ESP + local_8+0x10]
    Esi = LoadDword((Esp) + 12u); // 06acb285 MOV         ESI,dword ptr [ESP + local_4+0x10]
Lab_06acb289:
    Esp = Add32(Esp, 16u, Carry, Zero, SignFlag, Overflow); // 06acb289 ADD         ESP,0x10
    return; // 06acb28c RET         0x14
}
