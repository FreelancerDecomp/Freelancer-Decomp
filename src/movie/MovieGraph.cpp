// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieGraph.h"
#include "MovieWide.h"
#include "MovieWindow.h"

#include "MediaPlayer.h"

#include <intrin.h>

// Callees outside 0x06ac2690..0x06ac3d40. Signatures are the pushes at the
// call sites in movie.dll.txt (stdcall: no ADD ESP after the CALL).

void __cdecl Movie_InitForProcessor();
void* __cdecl MovieOperatorNew(unsigned int Size);
void __cdecl MovieOperatorDelete(void* Block);
long __stdcall Movie_ReduceModulo(unsigned int* Source, unsigned int Count, unsigned int* Dest, unsigned int* Modulus);
long __stdcall Movie_CommitFieldPair(unsigned int* Left, unsigned int* Right, unsigned int* FieldWords, unsigned int* Record);
long __stdcall MultiplyByFieldFactors(unsigned int* Value, int* FieldHolder);
long __stdcall CollectSquaredRows(unsigned int* Value, int SquareSteps, int RowCount, void* Table, void* FieldHolder);
void __stdcall StoreLeadingReciprocal(int Limbs, unsigned int Count, unsigned int* Reciprocal);
// 0x06ac5610. Defined in MovieExp.cpp. RET 0x1C.
long __stdcall ExponentiateSlidingWindow(void* Base, int Window, int Span, void* Exponent, int ExponentCount, void* Extra, void* FieldHolder);
void __stdcall Movie_FillRandomBelow(int Source, void* Dest, unsigned int Count);
long __stdcall Movie_AddFieldElements(unsigned int* Left, unsigned int* Right, unsigned int* Dest, int Sign, unsigned int* FieldHolder);
unsigned int __stdcall Movie_CopyOrReduceField(unsigned int* Source, unsigned int* Dest, unsigned int* Modulus);
void __stdcall Movie_AddModulo(unsigned int* Left, unsigned int* Right, unsigned int* Sum, unsigned int* Modulus);
void __stdcall Movie_MultiplyModulo(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* Modulus, unsigned int* Scratch);
void __stdcall Movie_NegateModulo(unsigned int* Source, unsigned int* Dest, unsigned int* Modulus);
void __stdcall Movie_SubtractModulo(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* Modulus);
long __stdcall BinaryGcdLimbs(unsigned int* Left, unsigned int* Context, unsigned int* Coefficient, unsigned int* Result);
long __stdcall CompareLimbs(int Left, unsigned int LeftCount, int Right, int RightCount);
long __stdcall ReduceLimbsShiftXor(unsigned int* Left, unsigned int LeftBits, unsigned int* Right, unsigned int RightBits, unsigned int* Coefficient, unsigned int* Remainder);
long __stdcall MultiplyLimbsShiftXor(int Left, int Right, unsigned int* Product, int Modulus, unsigned int ModulusBits);
unsigned int __stdcall SquareLimbByKind(unsigned int* Input, unsigned int* Output, unsigned int* Context);
void __stdcall ShiftLimbsByBits(unsigned int* Source, unsigned int BitCount, unsigned int* Dest, unsigned int Count);
unsigned int __stdcall ShiftLimbs(unsigned int* Source, unsigned int BitCount, unsigned int* Dest, int Count);
void* __stdcall Movie_AllocateBlock(unsigned int Size);
unsigned int* __stdcall FindWindowSlot(unsigned int LimbCount);
unsigned int __stdcall Movie_AddScaledLimbs(int Source, unsigned int Scale, unsigned int* Buffer, int Count);
void __stdcall Movie_SplitLimbs(unsigned int* Source, unsigned int Count, unsigned int* Dest, unsigned int* Bound);
int __stdcall Movie_DivideLimbs(unsigned int* Dividend, unsigned int DividendCount, unsigned int* Divisor, int DivisorCount, unsigned int* Reciprocal, unsigned int Quotient, unsigned int* Remainder);
long __stdcall AddLimbs(unsigned int* Left, int LeftCount, unsigned int* Right, int RightCount, int Sum);
long __stdcall SubtractLimbs(unsigned int* Minuend, unsigned int* Subtrahend, int Difference, int Count);
void __stdcall Movie_FreeBlock(void* Block);

CRITICAL_SECTION GMovieGraphLock;
int GMovieGraphLockInit = 0;
int GMovieGraphCode = 0;

// Five dwords at .rdata:06ada788, 06ada79c, 06ada7b0, 06ada7c4, 06ada7d8, 06ada7ec.
// 06ada7ec is 20 bytes; Ghidra split the tail into DAT_06ada7f8 / DAT_06ada7fc.
static const unsigned int GLimbs788[5] = { 0x141424F7u, 0x31415926u, 0x27182818u, 0x01234567u, 0x89ABCDEFu };
static const unsigned int GLimbs79c[5] = { 0x28C2A675u, 0xEC149044u, 0x2716B26Eu, 0x01234567u, 0x89ABCDEFu };
static const unsigned int GLimbs7b0[5] = { 0xEBE41497u, 0x4780C009u, 0x7632FF3Du, 0xD277BCE8u, 0x37A5ABCCu };
static const unsigned int GLimbs7c4[5] = { 0x9328239Eu, 0xD78FDEDFu, 0x28E85F1Au, 0x725E2F32u, 0x0DD8DABFu };
static const unsigned int GLimbs7d8[5] = { 0x109FA120u, 0xBA38DAF0u, 0x3510C07Du, 0xD6A3A1E5u, 0x8723947Fu };
static const unsigned int GLimbs7ec[5] = { 0xA379936Fu, 0xD4ED7ACDu, 0x8C3C5856u, 0x1075522Du, 0x44574491u };

// NEG / SBB / AND 0x7fffbffb / ADD 0x80004005. Nonzero -> 0, zero -> 0x80004005.
static long Movie_CodeFromFlag(unsigned int Flag)
{
    unsigned int Mask = (Flag != 0u) ? 0x7fffbffbu : 0u;
    return (long)(Mask + 0x80004005u);
}

// REP MOVSD + REP MOVSB. ByteCount from `SHL count,2` makes the MOVSB trip count 0.
static void Movie_MoveBytes(void* Dst, const void* Src, unsigned int ByteCount)
{
    unsigned int DwordCount = ByteCount >> 2;
    unsigned int Tail = ByteCount & 3u;
    unsigned int* DstDwords = (unsigned int*)Dst;
    const unsigned int* SrcDwords = (const unsigned int*)Src;
    unsigned int Index;
    for (Index = 0; Index < DwordCount; Index = Index + 1)
    {
        DstDwords[Index] = SrcDwords[Index];
    }
    unsigned char* DstBytes = (unsigned char*)Dst + (DwordCount * 4u);
    const unsigned char* SrcBytes = (const unsigned char*)Src + (DwordCount * 4u);
    for (Index = 0; Index < Tail; Index = Index + 1)
    {
        DstBytes[Index] = SrcBytes[Index];
    }
}

// REP STOSD of 0 + REP STOSB. Same byte-count split as Movie_MoveBytes.
static void Movie_ZeroBytes(void* Dst, unsigned int ByteCount)
{
    unsigned int DwordCount = ByteCount >> 2;
    unsigned int Tail = ByteCount & 3u;
    unsigned int* DstDwords = (unsigned int*)Dst;
    unsigned int Index;
    for (Index = 0; Index < DwordCount; Index = Index + 1)
    {
        DstDwords[Index] = 0;
    }
    unsigned char* DstBytes = (unsigned char*)Dst + (DwordCount * 4u);
    for (Index = 0; Index < Tail; Index = Index + 1)
    {
        DstBytes[Index] = 0;
    }
}

// IMUL by Groups, then SHL 2. FUN_06ac3110 / FUN_06ac3140.
static unsigned int Movie_GroupBytes(const int* Field, int Groups)
{
    return (Field[0] * (unsigned int)Groups) << 2;
}

long __stdcall Movie_ZeroFieldGroups(unsigned int* Dest, int Groups, int* Field)
{
    unsigned int Dwords = Movie_GroupBytes(Field, Groups) >> 2;
    unsigned int Index;
    for (Index = 0; Index < Dwords; Index = Index + 1)
    {
        Dest[Index] = 0;
    }
    return 1;
}

long __stdcall Movie_CopyFieldGroups(unsigned int* Source, unsigned int* Dest, int Groups, int* Field)
{
    unsigned int Dwords = Movie_GroupBytes(Field, Groups) >> 2;
    unsigned int Index;
    for (Index = 0; Index < Dwords; Index = Index + 1)
    {
        Dest[Index] = Source[Index];
    }
    return 1;
}

static void Movie_Copy5(unsigned int* Dst, const unsigned int* Src)
{
    unsigned int Index;
    for (Index = 0; Index < 5u; Index = Index + 1)
    {
        Dst[Index] = Src[Index];
    }
}

// x86 SHL/SHR mask the count to 5 bits.
static unsigned int Movie_Shl(unsigned int Value, unsigned int Count)
{
    return Value << (Count & 31u);
}

static unsigned int Movie_Shr(unsigned int Value, unsigned int Count)
{
    return Value >> (Count & 31u);
}

long __stdcall Movie_SearchFieldWitness(unsigned int* Seed, unsigned int* Element, unsigned int* FieldBlock)
{
    unsigned int* Field = (unsigned int*)FieldBlock[0];
    unsigned int Block[5];
    unsigned int Work[64];
    unsigned int* Past = Element + Field[0];
    long Ok = 1;
    unsigned int Found = 0;
    int Round;
    int Kind = (int)Field[2];

    Block[0] = Seed[0];
    Block[1] = Seed[1];
    Block[2] = Seed[2];
    Block[3] = Seed[3];
    if (Kind <= 0 || Kind > 3)
    {
        return 0;
    }
    for (Round = 0; Round < 100; Round = Round + 1)
    {
        long Step;
        if (Ok == 0)
        {
            return Ok;
        }
        if (Found != 0)
        {
            break;
        }
        Block[4] = (unsigned int)Round;
        Movie_CopyAndReduceLimbs(Block, 5u, Element, (unsigned int*)Field[4]);
        if (Field[2] == 1u || Movie_FieldLimbsAreZero((int)Element, (int*)Field) != 0)
        {
            Step = Movie_MultiplyThenAddField(Element, Element, FieldBlock + 1, Work, Field);
            if (Step == 0 || Movie_MultiplyThenAddField(Element, Work, FieldBlock + 0x41, Work, Field) == 0)
            {
                Ok = 0;
            }
            else
            {
                Ok = 1;
                Found = Movie_SquareFieldLimbs(Work, Past, Field);
            }
        }
        else
        {
            if (Movie_MultiplyFieldLimbs(Element, Element, Work, Field) == 0 ||
                Movie_MultiplyAfterUnary(FieldBlock + 0x41, Work, Work, Field) == 0 ||
                Movie_AddFieldLimbs(Work, Element, Work, (int*)Field) == 0 ||
                Movie_AddFieldLimbs(Work, FieldBlock + 1, Work, (int*)Field) == 0)
            {
                Ok = 0;
            }
            else
            {
                Ok = 1;
                if (Movie_TraceFieldLimbs(Work, Past, Field) != 0)
                {
                    Ok = Movie_MultiplyFieldLimbs(Element, Past, Past, Field) != 0 ? 1 : 0;
                    Found = 1;
                }
            }
        }
    }
    if (Ok == 0 || Found != 0)
    {
        return Ok;
    }
    return 0;
}

long __stdcall Movie_CopyAndReduceLimbs(unsigned int* Src, unsigned int Count, unsigned int* Dst, unsigned int* Modulus)
{
    unsigned int Local[5];
    unsigned int Index;
    if (Count > 5u)
    {
        return 0;
    }
    for (Index = 0; Index < Count; Index = Index + 1)
    {
        Local[Index] = Src[Index];
    }
    Movie_ReduceModulo(Local, Count, Dst, Modulus);
    return 1;
}

long  Movie_HresultIfNonZero(int* Value)
{
    return Movie_CodeFromFlag((unsigned int)(*Value != 0));
}

long  Movie_HresultIfNonZeroLocked(int* Value)
{
    long Result;
    EnterCriticalSection(&GMovieGraphLock);
    Result = Movie_HresultIfNonZero(Value);
    LeaveCriticalSection(&GMovieGraphLock);
    return Result;
}

void  Movie_AllocateFieldContext(void** Holder)
{
    unsigned int* Object;
    Movie_InitForProcessor();
    Object = (unsigned int*)MovieOperatorNew(0x439c);
    *Holder = Object;
    if (Object != 0)
    {
        long Status = Movie_BuildFieldContext(Object, (unsigned int*)GLimbs788, (unsigned int*)GLimbs79c, (unsigned int*)GLimbs7b0, (unsigned int*)GLimbs7c4, 0x06ada7d8, 0x06ada7ec);
        if (Status != 0)
        {
            MovieOperatorDelete(*Holder);
            *Holder = 0;
        }
    }
}

long __stdcall Movie_BuildFieldContext(unsigned int* Object, unsigned int* LimbsA, unsigned int* LimbsB, unsigned int* LimbsC, unsigned int* LimbsD, int NonNullA, int NonNullB)
{
    long Built;
    long Ok;
    unsigned int* Modulus;
    if (Object == 0 || LimbsA == 0 || LimbsB == 0 || LimbsC == 0 || LimbsD == 0 || NonNullA == 0 || NonNullB == 0)
    {
        return (long)0x80070057;
    }
    Movie_Copy5(Object, LimbsA);
    Movie_Copy5(Object + 0x19, LimbsB);
    Modulus = Object + 0x1e;
    Built = Movie_BuildModulusRecord(Object, 5u, 1u, Modulus);
    Movie_CopyAndReduceLimbs(LimbsC, 5u, Object + 5, Modulus);
    Movie_CopyAndReduceLimbs(LimbsD, 5u, Object + 10, Modulus);
    Ok = 0;
    if (Built != 0)
    {
        if (Movie_BindFieldHeader((int)Modulus, (int)(Object + 0x4af), (int*)(Object + 0xdf7)) != 0)
        {
            if (Movie_CommitFieldPair(Object + 5, Object + 10, Object + 0xdf7, Object + 0xd76) != 0)
            {
                Ok = 1;
            }
        }
    }
    Movie_CopyAndReduceLimbs((unsigned int*)GLimbs7d8, 5u, Object + 0xf, Modulus);
    Movie_CopyAndReduceLimbs((unsigned int*)GLimbs7ec, 5u, Object + 0x14, Modulus);
    if (Ok != 0)
    {
        if (MultiplyByFieldFactors(Object + 0xf, (int*)(Object + 0xd76)) != 0)
        {
            if (CollectSquaredRows(Object + 0xf, 4, 0x3c, Object + 0xe85, Object + 0xd76) != 0)
            {
                Object[0xe82] = 5u;
                StoreLeadingReciprocal((int)(Object + 0x19), 5u, Object + 0xe83);
                return 0;
            }
        }
    }
    return (long)0x80004005;
}

void*  Movie_CreateFieldContext(void** Holder)
{
    if (GMovieGraphLockInit == 0)
    {
        InitializeCriticalSection(&GMovieGraphLock);
        GMovieGraphLockInit = 1;
    }
    EnterCriticalSection(&GMovieGraphLock);
    Movie_AllocateFieldContext(Holder);
    LeaveCriticalSection(&GMovieGraphLock);
    return Holder;
}

void  Movie_ReleaseFieldContext(void** Holder)
{
    void* Block;
    EnterCriticalSection(&GMovieGraphLock);
    Block = *Holder;
    if (Block != 0)
    {
        MovieOperatorDelete(Block);
    }
    LeaveCriticalSection(&GMovieGraphLock);
}

long __stdcall Movie_ReduceWithFixedWidth(void* Arg1, void* Arg2, int Arg3, void* Arg4, void* Arg5)
{
    return ExponentiateSlidingWindow(Arg1, 4, 0x3c, Arg2, Arg3, Arg4, Arg5);
}

long  Movie_MapFieldLimbs(void* This, unsigned int* InLimbs, unsigned int* Seed, unsigned int* OutLimbs)
{
    // Stack image of SUB ESP,0xDC. Adjacent 5-dword slots so a 10-dword
    // write through Local50 / Local78 lands in Local3c / Local64.
    struct FFrame
    {
        unsigned int LocalDc[10];
        unsigned int LocalB4[5];
        unsigned int LocalA0[5];
        unsigned int Local8c[5];
        unsigned int Local78[5];
        unsigned int Local64[5];
        unsigned int Local50[5];
        unsigned int Local3c[5];
        unsigned int Local28[10];
    };
    FFrame Frame;
    unsigned int* Context;
    unsigned int Index;
    if (*(int*)This == 0)
    {
        return (long)0x80004005;
    }
    if (InLimbs == 0 || Seed == 0 || OutLimbs == 0)
    {
        return (long)0x80070057;
    }
    Context = *(unsigned int**)This;
    Movie_CopyAndReduceLimbs(InLimbs, 5u, Frame.LocalA0, (unsigned int*)((unsigned char*)Context + 0x78));
    Movie_CopyAndReduceLimbs(InLimbs + 5, 5u, Frame.Local8c, (unsigned int*)((unsigned char*)Context + 0x78));
    if (MultiplyByFieldFactors(Frame.LocalA0, (int*)((unsigned char*)Context + 0x35d8)) == 0)
    {
        return (long)0x80004005;
    }
    if (Movie_SearchFieldWitness(Seed, Frame.Local28, (unsigned int*)((unsigned char*)Context + 0x35d8)) == 0)
    {
        return (long)0x80004005;
    }
    Movie_FillRandomBelow((int)Context, Frame.LocalB4, 5);
    if (Movie_ReduceWithFixedWidth((unsigned char*)Context + 0x3a14, Frame.LocalB4, 5, Frame.Local50, (unsigned char*)Context + 0x35d8) == 0)
    {
        return (long)0x80004005;
    }
    if (MultiplyByFieldFactors(Frame.LocalA0, (int*)((unsigned char*)Context + 0x35d8)) == 0)
    {
        return (long)0x80004005;
    }
    if (Movie_ReduceExpandedRows(Frame.LocalA0, Frame.LocalB4, 5, Frame.LocalDc, (unsigned char*)Context + 0x35d8) == 0)
    {
        return (long)0x80004005;
    }
    if (Movie_AddFieldElements(Frame.LocalDc, Frame.Local28, Frame.Local78, 1, (unsigned int*)((unsigned char*)Context + 0x35d8)) == 0)
    {
        return (long)0x80004005;
    }
    Movie_CopyOrReduceField(Frame.Local50, Frame.LocalDc, (unsigned int*)((unsigned char*)Context + 0x78));
    for (Index = 0; Index < 5u; Index = Index + 1)
    {
        OutLimbs[Index] = Frame.LocalDc[Index];
    }
    Movie_CopyOrReduceField(Frame.Local3c, Frame.LocalDc, (unsigned int*)((unsigned char*)Context + 0x78));
    for (Index = 0; Index < 5u; Index = Index + 1)
    {
        OutLimbs[5 + Index] = Frame.LocalDc[Index];
    }
    Movie_CopyOrReduceField(Frame.Local78, Frame.LocalDc, (unsigned int*)((unsigned char*)Context + 0x78));
    for (Index = 0; Index < 5u; Index = Index + 1)
    {
        OutLimbs[10 + Index] = Frame.LocalDc[Index];
    }
    Movie_CopyOrReduceField(Frame.Local64, Frame.LocalDc, (unsigned int*)((unsigned char*)Context + 0x78));
    for (Index = 0; Index < 5u; Index = Index + 1)
    {
        OutLimbs[0xf + Index] = Frame.LocalDc[Index];
    }
    return 0;
}

long __stdcall Movie_ReduceExpandedRows(unsigned int* Arg1, void* Arg2, int Arg3, void* Arg4, void* Arg5)
{
    unsigned char Local[0x988];
    CollectSquaredRows(Arg1, 4, 0x3c, Local, Arg5);
    return ExponentiateSlidingWindow(Local, 4, 0x3c, Arg2, Arg3, Arg4, Arg5);
}

long  Movie_MapFieldLimbsLocked(void* This, unsigned int* InLimbs, unsigned int* Seed, unsigned int* OutLimbs)
{
    long Result;
    EnterCriticalSection(&GMovieGraphLock);
    Result = Movie_MapFieldLimbs(This, InLimbs, Seed, OutLimbs);
    LeaveCriticalSection(&GMovieGraphLock);
    return Result;
}

long  Movie_RecoverFieldLimbs(void* This, unsigned int* BaseLimbs, unsigned int* Groups, unsigned int* OutWords)
{
    struct FFrame
    {
        unsigned int LocalB4[5];
        unsigned int LocalA0[5];
        unsigned int Local8c[5];
        unsigned int Local78[5];
        unsigned int Local64[5];
        unsigned int Local50[10];
        unsigned int Local28[10];
    };
    FFrame Frame;
    unsigned int* Context;
    unsigned int Index;
    Context = *(unsigned int**)This;
    if (Context == 0)
    {
        return (long)0x80004005;
    }
    if (BaseLimbs == 0 || Groups == 0 || OutWords == 0)
    {
        return (long)0x80070057;
    }
    for (Index = 0; Index < 5u; Index = Index + 1)
    {
        Frame.LocalB4[Index] = BaseLimbs[Index];
    }
    Movie_CopyAndReduceLimbs(Groups, 5u, Frame.LocalA0, (unsigned int*)((unsigned char*)Context + 0x78));
    Movie_CopyAndReduceLimbs(Groups + 5, 5u, Frame.Local8c, (unsigned int*)((unsigned char*)Context + 0x78));
    Movie_CopyAndReduceLimbs(Groups + 10, 5u, Frame.Local78, (unsigned int*)((unsigned char*)Context + 0x78));
    Movie_CopyAndReduceLimbs(Groups + 0xf, 5u, Frame.Local64, (unsigned int*)((unsigned char*)Context + 0x78));
    if (MultiplyByFieldFactors(Frame.LocalA0, (int*)((unsigned char*)Context + 0x35d8)) == 0)
    {
        return (long)0x80004005;
    }
    if (Movie_ReduceExpandedRows(Frame.LocalA0, Frame.LocalB4, 5, Frame.Local50, (unsigned char*)Context + 0x35d8) == 0)
    {
        return (long)0x80004005;
    }
    if (Movie_AddFieldElements(Frame.Local78, Frame.Local50, Frame.Local28, -1, (unsigned int*)((unsigned char*)Context + 0x35d8)) == 0)
    {
        return (long)0x80004005;
    }
    return Movie_CodeFromFlag((unsigned int)Movie_StoreLowFourLimbs(Frame.Local28, OutWords, (int*)((unsigned char*)Context + 0x35d8)));
}

long __stdcall Movie_StoreLowFourLimbs(unsigned int* Source, unsigned int* Dest, int* Holder)
{
    unsigned int Local[5];
    unsigned int* Modulus = *(unsigned int**)Holder;
    Movie_CopyOrReduceField(Source, Local, (unsigned int*)Modulus[4]);
    // 06ac301e REP MOVSD copies the five local dwords onto themselves. EAX from 4080 is ignored.
    Dest[0] = Local[0];
    Dest[1] = Local[1];
    Dest[2] = Local[2];
    Dest[3] = Local[3];
    return 1;
}

long  Movie_RecoverFieldLimbsLocked(void* This, unsigned int* BaseLimbs, unsigned int* Groups, unsigned int* OutWords)
{
    long Result;
    EnterCriticalSection(&GMovieGraphLock);
    Result = Movie_RecoverFieldLimbs(This, BaseLimbs, Groups, OutWords);
    LeaveCriticalSection(&GMovieGraphLock);
    return Result;
}

long __stdcall Movie_AddFieldLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Sum, int* Field)
{
    int Kind = Field[2];
    int Count;
    if (Kind == 1)
    {
        Movie_AddModulo(Left, Right, Sum, (unsigned int*)Field[4]);
        return 1;
    }
    if (Kind > 1 && Kind < 4)
    {
        Count = Field[0];
        while (Count != 0)
        {
            *Sum = *Left ^ *Right;
            Left = Left + 1;
            Right = Right + 1;
            Sum = Sum + 1;
            Count = Count - 1;
        }
        return 1;
    }
    return 0;
}

long __stdcall Movie_MultiplyAfterUnary(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* Field)
{
    unsigned int Work[64];
    if (Movie_FieldUnaryByKind(Right, Work, Field) != 0)
    {
        if (Movie_MultiplyFieldLimbs(Left, Work, Dest, Field) != 0)
        {
            return 1;
        }
    }
    return 0;
}

long __stdcall Movie_FieldLimbsEqual(int LeftAddr, int RightAddr, int* Field)
{
    int Count;
    unsigned int Left;
    unsigned int Right;
    int Kind = Field[2];
    if (Kind <= 0 || Kind > 3)
    {
        return 0;
    }
    Count = Field[0];
    for (;;)
    {
        if (Count == 0)
        {
            return 1;
        }
        Left = *(unsigned int*)(LeftAddr + Count * 4 - 4);
        Right = *(unsigned int*)(RightAddr + Count * 4 - 4);
        Count = Count - 1;
        if (Left != Right)
        {
            return 0;
        }
    }
}

long __stdcall Movie_SetFieldFromSigned(unsigned int Value, unsigned int* Dest, int* Field)
{
    int Signed = (int)Value;
    int Sign = Signed >> 31;
    unsigned int Absolute = ((unsigned int)Signed ^ (unsigned int)Sign) - (unsigned int)Sign;
    int Kind = Field[2];
    if (Kind == 1)
    {
        Movie_ReduceModulo(&Absolute, 1u, Dest, (unsigned int*)Field[4]);
        if (Signed < 0)
        {
            Movie_NegateFieldLimbs(Dest, Dest, Field);
        }
        return 1;
    }
    if (Kind > 1 && Kind < 4)
    {
        if ((Absolute & 1u) != 0u)
        {
            Movie_MoveBytes(Dest, (unsigned char*)Field + 0x12c, Movie_GroupBytes(Field, 1));
            return 1;
        }
        Movie_ZeroBytes(Dest, Movie_GroupBytes(Field, 1));
        return 1;
    }
    return 0;
}

long __stdcall Movie_BindFieldHeader(int Modulus, int Stored, int* Field)
{
    int Count = *(int*)(Modulus + 0x100);
    int* Src = (int*)(Modulus + 0x1144);
    int* Dst = (int*)((unsigned char*)Field + 0x12c);
    Field[3] = Stored;
    Field[7] = 0;
    Field[8] = 0;
    Field[4] = Modulus;
    Field[0] = Count;
    Field[1] = 1;
    Field[2] = 1;
    while (Count != 0)
    {
        *Dst = *Src;
        Src = Src + 1;
        Dst = Dst + 1;
        Count = Count - 1;
    }
    return 1;
}

long __stdcall Movie_FieldUnaryByKind(unsigned int* Source, unsigned int* Dest, unsigned int* Field)
{
    unsigned int WorkLow[64];
    unsigned int WorkHigh[64];
    int Flag = 1;
    unsigned int Count = Field[0];
    unsigned int Second = Field[1];
    unsigned int Kind = Field[2];
    long Step;
    if (Kind == 1u)
    {
        unsigned int* Modulus = (unsigned int*)Field[4];
        if (Modulus[0x44] == 1u)
        {
            Step = BinaryGcdLimbs(Source, Modulus, Dest, 0);
            return Step != 0 ? 1 : 0;
        }
        {
            unsigned int Produced = Movie_ExtendedEuclidLimbs(Source, Count, Modulus, Count, Dest, 0, WorkHigh, 0);
            Flag = 1;
            Step = CompareLimbs((int)WorkHigh, Produced, (int)&Flag, 1);
            return Step == 0 ? 1 : 0;
        }
    }
    if (Kind == 2u)
    {
        Step = Movie_PowFieldByBitScan(Source, (int)Dest, Field);
        return Step != 0 ? 1 : 0;
    }
    if (Kind == 3u)
    {
        Step = ReduceLimbsShiftXor(Source, Second, Field + 9, Second + 1u, WorkLow, WorkHigh);
        if (Step != 0)
        {
            Flag = 1;
            Step = CompareLimbs((int)WorkHigh, Count, (int)&Flag, 1);
            if (Step == 0)
            {
                Movie_MoveBytes(Dest, WorkLow, Movie_GroupBytes((int*)Field, 1));
                return 1;
            }
        }
    }
    return 0;
}

long __stdcall Movie_FieldLimbsAreZero(int Element, int* Field)
{
    int Count;
    int Kind = Field[2];
    if (Kind <= 0 || Kind > 3)
    {
        return 0;
    }
    Count = Field[0];
    while (Count != 0)
    {
        int Word = *(int*)(Element + Count * 4 - 4);
        Count = Count - 1;
        if (Word != 0)
        {
            return 0;
        }
    }
    return 1;
}

long __stdcall Movie_MultiplyFieldLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int* Field)
{
    unsigned int Kind = Field[2];
    if (Kind == 1u)
    {
        Movie_MultiplyModulo(Left, Right, Product, (unsigned int*)Field[4], (unsigned int*)Field[3]);
        return 1;
    }
    if (Kind == 2u)
    {
        return Movie_MultiplyBinaryField(Left, Right, Product, Field);
    }
    if (Kind == 3u)
    {
        return MultiplyLimbsShiftXor((int)Left, (int)Right, Product, (int)(Field + 9), Field[1]);
    }
    return 0;
}

long __stdcall Movie_MultiplyThenAddField(unsigned int* Left, unsigned int* Right, unsigned int* Addend, unsigned int* Dest, unsigned int* Field)
{
    unsigned int Work[64];
    if (Movie_MultiplyFieldLimbs(Left, Right, Work, Field) != 0)
    {
        if (Movie_AddFieldLimbs(Work, Addend, Dest, (int*)Field) != 0)
        {
            return 1;
        }
    }
    return 0;
}

long __stdcall Movie_NegateFieldLimbs(unsigned int* Source, unsigned int* Dest, int* Field)
{
    int Kind = Field[2];
    if (Kind == 1)
    {
        Movie_NegateModulo(Source, Dest, (unsigned int*)Field[4]);
        return 1;
    }
    if (Kind > 1 && Kind < 4)
    {
        Movie_MoveBytes(Dest, Source, Movie_GroupBytes(Field, 1));
        return 1;
    }
    return 0;
}

unsigned int __stdcall Movie_SquareFieldLimbs(unsigned int* Source, unsigned int* Dest, unsigned int* Field)
{
    unsigned int Kind = Field[2];
    unsigned int Second = Field[1];
    if (Kind == 1u)
    {
        return SquareLimbByKind(Source, Dest, (unsigned int*)Field[4]);
    }
    if (Kind == 2u)
    {
        return (unsigned int)Movie_ShiftFieldLimbs(Source, Second - 1u, (int)Dest, Field);
    }
    if (Kind == 3u)
    {
        unsigned int Ok = 1;
        unsigned int Left = Second;
        Movie_MoveBytes(Dest, Source, Field[0] << 2);
        if (Second != 1u)
        {
            Left = Second - 1u;
            do
            {
                if (Ok == 0 || Movie_MultiplyFieldLimbs(Dest, Dest, Dest, Field) == 0)
                {
                    Ok = 0;
                }
                else
                {
                    Ok = 1;
                }
                Left = Left - 1u;
            } while (Left != 0u);
        }
        return Ok;
    }
    return 0;
}

long __stdcall Movie_SubtractFieldLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Difference, int* Field)
{
    int Kind = Field[2];
    int Count;
    if (Kind == 1)
    {
        Movie_SubtractModulo(Left, Right, Difference, (unsigned int*)Field[4]);
        return 1;
    }
    if (Kind > 1 && Kind < 4)
    {
        Count = Field[0];
        while (Count != 0)
        {
            *Difference = *Left ^ *Right;
            Left = Left + 1;
            Right = Right + 1;
            Difference = Difference + 1;
            Count = Count - 1;
        }
        return 1;
    }
    return 0;
}

long __stdcall Movie_ShiftFieldLimbs(unsigned int* Source, unsigned int Shift, int Dest, unsigned int* Field)
{
    unsigned int Low[64];
    unsigned int High[64];
    long Flag = 1;
    unsigned int Words = Field[0];
    unsigned int Limit = Field[1];
    unsigned int Index;
    unsigned int* Out = (unsigned int*)Dest;
    if (Field[2] != 2u)
    {
        return 0;
    }
    if ((int)Shift < 0 || (int)Shift > (int)Limit)
    {
        Flag = 0;
    }
    ShiftLimbsByBits(Source, Shift, Low, Words);
    ShiftLimbsByBits(Source, Shift - Limit, High, Words);
    for (Index = 0; Index < Words; Index = Index + 1)
    {
        Out[Index] = High[Index] | Low[Index];
    }
    *(unsigned int*)((unsigned char*)Out + Words * 4u - 4u) &= Field[0x4a];
    return Flag;
}

long __stdcall Movie_PowFieldByBitScan(unsigned int* Base, int Dest, unsigned int* Field)
{
    unsigned int Buf100[64];
    unsigned int Buf200[64];
    unsigned int Esi = Field[1] - 1u;
    unsigned long Bit = 0;
    int Ebx;
    long Eax;
    _BitScanReverse(&Bit, Esi | 1u);
    Ebx = (int)Bit;
    if (Movie_FieldLimbsAreZero((int)Base, (int*)Field) != 0)
    {
        return 0;
    }
    // FUN_06ac3140 (06ac315a) returns 1; the JZ at 06ac37dd is not taken.
    Movie_MoveBytes(Buf100, Base, Movie_GroupBytes((int*)Field, 1));
    for (;;)
    {
        Eax = 1;
        for (;;)
        {
            if (Ebx == 0)
            {
                if (Eax == 0)
                {
                    return 0;
                }
                if (Movie_ShiftFieldLimbs(Buf100, 1u, Dest, Field) == 0)
                {
                    return 0;
                }
                return 1;
            }
            if (Eax != 0)
            {
                unsigned int Shifted = Esi >> (unsigned int)Ebx;
                if (Movie_ShiftFieldLimbs(Buf100, Shifted, (int)Buf200, Field) != 0 &&
                    Movie_MultiplyBinaryField(Buf100, Buf200, Buf100, Field) != 0)
                {
                    Eax = 1;
                }
                else
                {
                    Eax = 0;
                }
            }
            else
            {
                Eax = 0;
            }
            Ebx = Ebx - 1;
            if (((Esi >> (unsigned int)Ebx) & 1u) != 0u)
            {
                break;
            }
            if (Eax == 0)
            {
                return 0;
            }
        }
        if (Eax == 0)
        {
            return 0;
        }
        if (Movie_ShiftFieldLimbs(Buf100, 1u, (int)Buf100, Field) == 0)
        {
            return 0;
        }
        if (Movie_MultiplyBinaryField(Buf100, Base, Buf100, Field) == 0)
        {
            return 0;
        }
    }
}

long __stdcall Movie_MultiplyBinaryField(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int* Field)
{
    unsigned int Copy2Raw[257];
    unsigned int Copy1Raw[257];
    unsigned int* Copy2 = Copy2Raw + 1;
    unsigned int* Copy1 = Copy1Raw + 1;
    unsigned int Shift = Field[0x49];
    unsigned int Count = Field[0];
    unsigned short* Pairs = (unsigned short*)Field[7];
    unsigned int PairCount = Field[6];
    unsigned int Complement = 0x20u - Shift;
    long Square;
    unsigned int Index;
    if (Field[2] != 2u)
    {
        return 0;
    }
    for (Index = 0; Index < Count; Index = Index + 1)
    {
        unsigned int Word2 = Right[Index];
        unsigned int Word1 = Left[Index];
        Copy2[Index] = Word2;
        Copy1[Index] = Word1;
        Product[Index] = Word2 & Word1;
    }
    Square = Movie_ShiftFieldLimbs(Product, 1u, (int)Product, Field);
    {
        unsigned int Ret1 = ShiftLimbs(Copy1, 0u - Shift, Copy1 + Count, (int)Count);
        unsigned int Ret2 = ShiftLimbs(Copy2, 0u - Shift, Copy2 + Count, (int)Count);
        if (Shift != 0u)
        {
            // 06ac39ec/06ac39f7: OR into the last copied dword (base + count*4 - 4), not [count].
            Copy1[(int)Count - 1] |= Movie_Shl(Ret1, Complement);
            Copy2[(int)Count - 1] |= Movie_Shl(Ret2, Complement);
        }
    }
    if (Left != Right)
    {
        unsigned int Pair;
        for (Pair = 0; Pair < PairCount; Pair = Pair + 1)
        {
            unsigned int BitsA = Pairs[0];
            unsigned int BitsB = Pairs[1];
            unsigned int ShiftA = BitsA & 0x1fu;
            unsigned int ShiftB = BitsB & 0x1fu;
            unsigned int BaseA = BitsA >> 5;
            unsigned int BaseB = BitsB >> 5;
            unsigned int InvA = 0x1fu - ShiftA;
            unsigned int InvB = 0x1fu - ShiftB;
            unsigned int Limb;
            Pairs = Pairs + 2;
            for (Limb = 0; Limb < Count; Limb = Limb + 1)
            {
                unsigned int Ia = BaseA + Limb;
                unsigned int Ib = BaseB + Limb;
                unsigned int Left1 = Movie_Shl(Copy1[Ia + 1u] << 1, InvA) | Movie_Shr(Copy1[Ia], ShiftA);
                unsigned int Right1 = Movie_Shl(Copy2[Ib + 1u] << 1, InvB) | Movie_Shr(Copy2[Ib], ShiftB);
                unsigned int Left2 = Movie_Shl(Copy2[Ia + 1u] << 1, InvA) | Movie_Shr(Copy2[Ia], ShiftA);
                unsigned int Right2 = Movie_Shl(Copy1[Ib + 1u] << 1, InvB) | Movie_Shr(Copy1[Ib], ShiftB);
                Product[Limb] ^= (Left1 & Right1) ^ (Left2 & Right2);
            }
        }
    }
    *(unsigned int*)((unsigned char*)Product + Count * 4u - 4u) &= Field[0x4a];
    return Square != 0 ? 1 : 0;
}

long __stdcall Movie_TraceFieldLimbs(unsigned int* Source, unsigned int* Dest, unsigned int* Field)
{
    unsigned int Kind = Field[2];
    unsigned int Words = Field[0];
    unsigned int Bits = Field[1];
    unsigned int Shift = Field[0x49];
    if (Kind == 2u)
    {
        unsigned int Parity = 0;
        unsigned int Index = 0;
        unsigned int* Out = Dest;
        if (Bits != 0u)
        {
            do
            {
                unsigned int WordIndex = Index >> 5;
                unsigned int Bit = Index & 0x1fu;
                unsigned int Taken = Movie_Shr(Source[WordIndex], Bit) & 1u;
                unsigned int Cleared;
                unsigned int Written;
                Parity = Parity ^ Taken;
                Cleared = ~(Movie_Shl((~Parity) & 1u, Bit));
                Written = Movie_Shl(Parity & 1u, Bit);
                Out[WordIndex] = (Cleared & Out[WordIndex]) | Written;
                Index = Index + 1u;
            } while (Index != Bits);
        }
        {
            unsigned int* Last = (unsigned int*)((unsigned char*)Out + Words * 4u - 4u);
            unsigned int Masked = Movie_Shr(Movie_Shl(*Last, Shift), Shift);
            *Last = Masked;
        }
        return Parity == 0u ? 1 : 0;
    }
    if (Kind == 3u)
    {
        unsigned int Local[64];
        unsigned int BitPlace = Field[8];
        unsigned int BitWord = BitPlace >> 5;
        unsigned int BitMask = 1u << (BitPlace & 0x1fu);
        long Ok;
        unsigned int* Slot;
        Movie_ZeroBytes(Local, Movie_GroupBytes((int*)Field, 1));
        Ok = 1;
        Movie_ZeroBytes(Dest, Movie_GroupBytes((int*)Field, 1));
        Slot = Local + BitWord;
        *Slot = BitMask;
        if (Bits != 1u)
        {
            unsigned int Left = Bits - 1u;
            do
            {
                if (Ok != 0 &&
                    Movie_MultiplyFieldLimbs(Local, Local, Local, Field) != 0 &&
                    Movie_MultiplyFieldLimbs(Dest, Dest, Dest, Field) != 0 &&
                    Movie_MultiplyThenAddField(Local, Source, Dest, Dest, Field) != 0)
                {
                    Ok = 1;
                }
                else
                {
                    Ok = 0;
                }
                *Slot = *Slot ^ BitMask;
                Left = Left - 1u;
            } while (Left != 0u);
        }
        if (Ok != 0 &&
            Movie_MultiplyThenAddField(Dest, Dest, Dest, Local, Field) != 0 &&
            Movie_FieldLimbsEqual((int)Local, (int)Source, (int*)Field) != 0)
        {
            return 1;
        }
    }
    return 0;
}

long __stdcall Movie_BuildModulusRecord(unsigned int* Limbs, unsigned int Count, unsigned int Flag, unsigned int* Record)
{
    unsigned int* Block = (unsigned int*)Movie_AllocateBlock(0x304);
    long Result = 0;
    unsigned int* Wide;
    unsigned int* Half;
    unsigned int HalfWord;
    unsigned int Inv;
    unsigned int ByteCount;
    if (Block == 0)
    {
        return 0;
    }
    if (Count == 0u || Limbs[Count - 1u] == 0u)
    {
        GMovieGraphCode = 1;
    }
    else
    {
        Wide = FindWindowSlot(Count);
        Half = FindWindowSlot((Count + 1u) >> 1);
        if (Wide == 0 || Half == 0 || Count > 0x40u)
        {
            GMovieGraphCode = 3;
        }
        else
        {
            HalfWord = *Half;
            Record[0x44] = Flag;
            Record[0x41] = HalfWord * 2u;
            Record[0x42] = (unsigned int)Wide;
            Record[0x40] = Count;
            Record[0x43] = (unsigned int)Half;
            ByteCount = Count << 2;
            Movie_MoveBytes(Record, Limbs, ByteCount);
            Movie_MoveBytes((unsigned char*)Record + 0x1040, Limbs, ByteCount);
            Record[0x450] = Count;
            StoreLeadingReciprocal((int)Limbs, Count, Record + 0x45);
            if ((~Limbs[0] & 1u) == 0u)
            {
                unsigned int Step = 5u;
                unsigned int Limb = Limbs[0];
                Inv = Limb + (((Limb + 1u) >> 2) * 8u);
                do
                {
                    Step = Step + Step;
                    Inv = Inv * (2u - Inv * Limb);
                } while ((int)Step < 0x20);
            }
            else
            {
                Inv = 0;
            }
            Record[0x47] = Inv;
            if (Flag == 1u)
            {
                unsigned int* Cursor;
                unsigned int* High;
                unsigned int* Table;
                unsigned int Left;
                if (Inv == 0u)
                {
                    GMovieGraphCode = 2;
                    goto Done;
                }
                Record[0x37f] = Inv;
                Block[Count] = Movie_StoreScaledLimbs((int)Record, Inv, Block, (int)Count);
                if (HalfWord != 1u)
                {
                    Left = HalfWord - 1u;
                    Cursor = Block + 1;
                    High = Block + Count + 1u;
                    Table = Record + 0x380;
                    do
                    {
                        unsigned int Mixed = 0u - (Cursor[0] * Inv);
                        unsigned int Produced;
                        *Table = Mixed;
                        Produced = Movie_AddScaledLimbs((int)Record, Mixed, Cursor, (int)Count);
                        Cursor = Cursor + 1;
                        *High = Produced;
                        High = High + 1;
                        Table = Table + 1;
                        Left = Left - 1u;
                    } while (Left != 0u);
                }
                Movie_EvaluateWindow(Record + 0x37f, HalfWord, Record + 0x39f, (unsigned char*)(Record + 0x40c), Half);
                Movie_SplitLimbs(Block + HalfWord, Count, Record + 0x29d, Half);
            }
            {
                unsigned int Span = HalfWord + Count;
                unsigned int Shift = Record[0x46];
                unsigned int* Ones = Block;
                unsigned int Fill = Span;
                unsigned int* At180;
                unsigned int* At204;
                unsigned int One = 1u;
                if (Span != 0u)
                {
                    while (Fill != 0u)
                    {
                        *Ones = 0xffffffffu;
                        Ones = Ones + 1;
                        Fill = Fill - 1u;
                    }
                }
                *(unsigned int*)((unsigned char*)Block + Span * 4u - 4u) = Movie_Shr(0xffffffffu, Shift);
                At180 = (unsigned int*)((unsigned char*)Block + 0x180);
                At204 = (unsigned int*)((unsigned char*)Block + 0x204);
                Movie_DivideLimbs(Block, Span, Limbs, (int)Count, Record + 0x45, (unsigned int)At180, At204);
                Movie_EvaluateWindow(At180, HalfWord, Record + 0x20c, (unsigned char*)(Record + 0x279), Half);
                ShiftLimbs(At180, 0xffffffffu, Record + 0x27d, (int)HalfWord);
                if (*At204 == 0xffffffffu)
                {
                    AddLimbs(At204, (int)Count, &One, 1, (int)At204);
                }
                else
                {
                    *At204 = *At204 + 1u;
                }
                SubtractLimbs(Record, At204, (int)At204, (int)Count);
                Movie_SplitLimbs(At204, Count, Record + 0x12a, Half);
                Movie_SplitLimbs(Record, Count, Record + 0x48, Half);
                One = 1u;
                Movie_ReduceModulo(&One, 1u, Record + 0x451, Record);
                Result = 1;
            }
        }
    }
Done:
    Movie_FreeBlock(Block);
    return Result;
}
