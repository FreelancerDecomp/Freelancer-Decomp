// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieMod.h"
#include "MovieField.h"
#include "MovieGraph.h"
#include "MovieWindow.h"

#include <intrin.h>

// Callees this file does not define. Signatures are the pushes at the call
// sites (stdcall: the caller does not adjust ESP).

// 0x06ac3110. RET 0xC. Zeros Groups * Field[0] dwords and returns 1.
long __stdcall Movie_ZeroFieldGroups(unsigned int* Dest, int Groups, int* Field);

// 0x06ac3140. RET 0x10. Copies Groups * Field[0] dwords from First to Second and returns 1.
long __stdcall Movie_CopyFieldGroups(unsigned int* First, unsigned int* Second, int Groups, int* Field);

// 0x06ac7e60. RET 0x8. 06ac69a0 takes the nonzero EAX as success and skips the slot write.
long __stdcall Movie_FieldElementIsUnit(int Limbs, int* FieldHolder);

// 0x06ac7f20. RET 0x10. Pushes are Sign, FieldHolder, SlotLimbs, Limbs.
long __stdcall Movie_CopyOrNegatePair(unsigned int* Limbs, unsigned int* SlotLimbs, int* FieldHolder, int Sign);

// 0x06ac7760. RET 0x14. Pushes are Sign, Context, Slot, Limbs, Saved. EAX is ignored here.
void __stdcall Movie_AssignWindowSlot(unsigned int* Saved, unsigned int* Limbs, int Slot, int* Context, int Sign);

// 0x06ac8190. RET 0x8. Defined in MovieFieldHigh.cpp.
long __stdcall StoreFieldUnit(unsigned int* Dest, int* FieldHolder);

// 0x06ac6bb0. RET 0x10. Defined in MovieFieldHigh.cpp. Fourth push is the scratch base.
long __stdcall SeedFieldWork(unsigned int* Value, unsigned int* Work, int* FieldHolder, unsigned int* Upper);

// 0x06ac7210. Defined in MovieFieldHigh.cpp.
long __stdcall MultiplyFieldLimbs(unsigned int* Left, unsigned int* Right, int* FieldHolder, unsigned int* Scratch);

// 0x06ac7570. RET 0xC. The third argument is popped and not read.
long __stdcall Movie_LoadFieldOne(unsigned int* Work, int* FieldHolder, unsigned int* Scratch);

// Slot stride is 0x14f dwords (0x53c bytes). Work+0x53c0 is the scratch passed to the products.
static unsigned int* Movie_ExponentSlot(unsigned int* Work, unsigned int Index)
{
    return Work + Index * 0x14fu;
}

// 0x06ac61a0. Walks Context[2] records of stride 0x410. The state dword is at
// byte +0x881c and the 0x100-byte buffers start at +0xe958. A zero flag at
// *Context returns immediately. The second pass clears Context[2], then for
// each record applies the state formula and may call 0x06ac69a0.
void __stdcall Movie_AdvanceFieldRecordStates(int* Context)
{
    unsigned char* Bytes = (unsigned char*)Context;
    unsigned int* Outer = (unsigned int*)Context[4];
    int Mode = Context[1];
    int* Field = (int*)Outer[0];
    int LimbCount = Field[0];
    int RecordCount = Context[2];
    if (Context[0] == 0)
    {
        return;
    }

    // Local400, Local300, Local200. Retail lays these out as 0x100, 0x100, 0x200 bytes.
    unsigned int Work[64 + 64 + 128];
    unsigned int* Local400 = Work;
    unsigned int* Local300 = Work + 64;
    unsigned int* Local200 = Work + 128;
    int LiveCount = 0;
    if (RecordCount != 0)
    {
        int RecordByte = 0;
        int BufferByte = 0;
        int Remaining = RecordCount;
        while (Remaining != 0)
        {
            unsigned int* Buffer = (unsigned int*)(Bytes + BufferByte + 0xe958);
            int State = *(int*)(Bytes + RecordByte + 0x881c);
            unsigned int* Record = (unsigned int*)(Bytes + RecordByte + 0x8418);
            unsigned int* AtN = Record + LimbCount;
            unsigned int* At2N = AtN + LimbCount;
            unsigned int* At3N = At2N + LimbCount;
            int StepOk = 0;
            // 06ac622b. State 1 copies the second limb group. State 2 subtracts.
            // Any other state zeros the buffer; that call's EAX is ignored.
            if (State == 1)
            {
                if (Context[0] != 0 && Movie_CopyFieldGroups(At2N, Buffer, 1, Field) != 0)
                {
                    StepOk = 1;
                }
            }
            else if (State == 2)
            {
                if (Context[0] != 0 && Movie_SubtractFieldLimbs(Record, At2N, Buffer, Field) != 0)
                {
                    StepOk = 1;
                }
                if (Movie_FieldLimbsAreZero((int)Buffer, Field) != 0 &&
                    Movie_FieldLimbsEqual((int)AtN, (int)At3N, Field) != 0)
                {
                    State = 3;
                    if (Mode == 0)
                    {
                        if (StepOk != 0 && Movie_AddFieldLimbs(AtN, AtN, Buffer, Field) != 0)
                        {
                            StepOk = 1;
                        }
                        else
                        {
                            StepOk = 0;
                        }
                    }
                    else if (StepOk != 0 && Movie_CopyFieldGroups(Record, Buffer, 1, Field) != 0)
                    {
                        StepOk = 1;
                    }
                    else
                    {
                        StepOk = 0;
                    }
                }
            }
            else
            {
                StepOk = 0;
                Movie_ZeroFieldGroups(Buffer, 1, Field);
            }

            int StoredIndex = 0;
            // 06ac632b. A zero buffer becomes state 4. A repeat of the previous
            // buffer reuses that slot. Otherwise the buffer is appended.
            if (Movie_FieldLimbsAreZero((int)Buffer, Field) != 0)
            {
                State = 4;
                StoredIndex = 0;
            }
            else if (BufferByte == 0 ||
                Movie_FieldLimbsEqual((int)(Bytes + BufferByte + 0xe858), (int)Buffer, Field) == 0)
            {
                StoredIndex = LiveCount;
                LiveCount = LiveCount + 1;
                BufferByte = BufferByte + 0x100;
            }
            else
            {
                StoredIndex = LiveCount - 1;
            }

            int Combined = 0;
            if (Context[0] != 0 && StepOk != 0)
            {
                Combined = 1;
            }
            Context[0] = Combined;
            *(int*)(Bytes + RecordByte + 0x8824) = StoredIndex;
            *(int*)(Bytes + RecordByte + 0x881c) = State;
            RecordByte = RecordByte + 0x410;
            Remaining = Remaining - 1;
        }

        if (LiveCount != 0)
        {
            int Copied = 0;
            if (Context[0] != 0 &&
                Movie_CopyFieldGroups((unsigned int*)(Bytes + 0xe958), (unsigned int*)(Bytes + 0xfd58), 1, Field) != 0)
            {
                Copied = 1;
            }
            Context[0] = Copied;
            if (LiveCount != 1)
            {
                int Byte = 0x100;
                int Left = LiveCount - 1;
                while (Left != 0)
                {
                    int Step = 0;
                    if (Context[0] != 0 &&
                        Movie_MultiplyFieldLimbs(
                            (unsigned int*)(Bytes + Byte + 0xe958),
                            (unsigned int*)(Bytes + Byte + 0xfc58),
                            (unsigned int*)(Bytes + Byte + 0xfd58),
                            (unsigned int*)Field) != 0)
                    {
                        Step = 1;
                    }
                    Byte = Byte + 0x100;
                    Left = Left - 1;
                    Context[0] = Step;
                }
            }

            int Unary = 0;
            if (Context[0] != 0 &&
                Movie_FieldUnaryByKind(
                    (unsigned int*)(Bytes + ((unsigned int)LiveCount << 8) + 0xfc58),
                    Local300,
                    (unsigned int*)Field) != 0)
            {
                Unary = 1;
            }
            Context[0] = Unary;

            int Down = LiveCount - 1;
            if (Down != 0)
            {
                unsigned int Byte = (unsigned int)Down << 8;
                while (Down != 0)
                {
                    int Step = 0;
                    if (Context[0] != 0 &&
                        Movie_MultiplyFieldLimbs(
                            Local300,
                            (unsigned int*)(Bytes + Byte + 0xfc58),
                            (unsigned int*)(Bytes + Byte + 0xfd58),
                            (unsigned int*)Field) != 0 &&
                        Movie_MultiplyFieldLimbs(
                            Local300,
                            (unsigned int*)(Bytes + Byte + 0xe958),
                            Local300,
                            (unsigned int*)Field) != 0)
                    {
                        Step = 1;
                    }
                    Byte = Byte - 0x100u;
                    Down = Down - 1;
                    Context[0] = Step;
                }
            }

            int Stored = 0;
            if (Context[0] != 0 &&
                Movie_CopyFieldGroups(Local300, (unsigned int*)(Bytes + 0xfd58), 1, Field) != 0)
            {
                Stored = 1;
            }
            Context[0] = Stored;
        }
    }

    // 06ac6518. The saved record count still bounds the second pass.
    Context[2] = 0;
    if (RecordCount == 0)
    {
        return;
    }

    int RecordByte = 0;
    int Index = 0;
    while (Index != RecordCount)
    {
        int Result = Context[0];
        if (Result == 0)
        {
            return;
        }
        int Slot = *(int*)(Bytes + RecordByte + 0x8820);
        int State = *(int*)(Bytes + RecordByte + 0x881c);
        int Sign = *(int*)(Bytes + RecordByte + 0x8818);
        int SavedIndex = *(int*)(Bytes + RecordByte + 0x8824);
        unsigned int* Record = (unsigned int*)(Bytes + RecordByte + 0x8418);
        unsigned int* Selected = (unsigned int*)(Bytes + SavedIndex * 0x100 + 0xfd58);
        unsigned int* AtN = Record + LimbCount;
        unsigned int* At2N = AtN + LimbCount;
        unsigned int* At3N = At2N + LimbCount;
        unsigned int StateSlot = (unsigned int)(State - 1);
        int EnterSlot = 0;
        // 06ac6594. Table at 06ac698c: state 1, state 2, state 3, state 4.
        // State 0 and states above 4 take the JA at 06ac658e.
        if (StateSlot > 3u)
        {
            Result = 0;
        }
        else if (StateSlot == 0u)
        {
            if (Result != 0 &&
                Movie_MultiplyFieldLimbs(Selected, Selected, Local400, (unsigned int*)Field) != 0 &&
                Movie_MultiplyFieldLimbs(Record, Local400, Local200, (unsigned int*)Field) != 0 &&
                Movie_MultiplyFieldLimbs(Selected, Local400, Local400, (unsigned int*)Field) != 0 &&
                Movie_MultiplyFieldLimbs(AtN, Local400, Local200 + LimbCount, (unsigned int*)Field) != 0)
            {
                Result = 1;
                EnterSlot = 1;
            }
            else
            {
                Result = 0;
            }
        }
        else if (StateSlot == 3u)
        {
            if (Result != 0)
            {
                EnterSlot = 1;
            }
        }
        else
        {
            int MathOk = 0;
            if (State == 2)
            {
                if (Result != 0 && Movie_SubtractFieldLimbs(AtN, At3N, Local400, Field) != 0)
                {
                    MathOk = Movie_MultiplyFieldLimbs(Selected, Local400, Local400, (unsigned int*)Field);
                }
            }
            else if (Mode != 0)
            {
                if (Result != 0 &&
                    Movie_MultiplyFieldLimbs(AtN, Selected, Local400, (unsigned int*)Field) != 0)
                {
                    MathOk = Movie_AddFieldLimbs(Record, Local400, Local400, Field);
                }
            }
            else if (Result != 0 &&
                Movie_MultiplyFieldLimbs(Record, Record, Local400, (unsigned int*)Field) != 0 &&
                Movie_AddFieldLimbs(Local400, Local400, Local300, Field) != 0 &&
                Movie_AddFieldLimbs(Local400, Local300, Local400, Field) != 0 &&
                Movie_AddFieldLimbs(Local400, Outer + 1, Local400, Field) != 0 &&
                Movie_MultiplyFieldLimbs(Local400, Selected, Local400, (unsigned int*)Field) != 0)
            {
                MathOk = 1;
            }
            if (MathOk != 0)
            {
                MathOk = Movie_MultiplyFieldLimbs(Local400, Local400, Local300, (unsigned int*)Field);
            }
            if (Mode != 0)
            {
                if (MathOk != 0 &&
                    Movie_AddFieldLimbs(Local400, Local300, Local300, Field) != 0 &&
                    Movie_SubtractFieldLimbs(Local300, Outer + 1, Local300, Field) != 0 &&
                    Movie_SubtractFieldLimbs(Local300, Record, Local300, Field) != 0 &&
                    Movie_SubtractFieldLimbs(Local300, At2N, Local200, Field) != 0)
                {
                    MathOk = 1;
                }
                else
                {
                    MathOk = 0;
                }
            }
            else if (MathOk != 0 &&
                Movie_SubtractFieldLimbs(Local300, Record, Local300, Field) != 0 &&
                Movie_SubtractFieldLimbs(Local300, At2N, Local200, Field) != 0)
            {
                MathOk = 1;
            }
            else
            {
                MathOk = 0;
            }
            if (MathOk != 0 &&
                Movie_SubtractFieldLimbs(Local200, Record, Local300, Field) != 0 &&
                Movie_MultiplyFieldLimbs(Local400, Local300, Local300, (unsigned int*)Field) != 0 &&
                Movie_AddFieldLimbs(AtN, Local300, Local200 + LimbCount, Field) != 0)
            {
                Result = 1;
            }
            else
            {
                Result = 0;
            }
            Sign = (int)(0u - (unsigned int)Sign);
            if (Result != 0)
            {
                EnterSlot = 1;
            }
        }

        // 06ac6903. Slot is the dword at +0x8820. The counter stride is 0x204.
        if (EnterSlot != 0)
        {
            if (Slot > 0 && Slot <= Context[3])
            {
                unsigned int Scaled = (unsigned int)Slot;
                Scaled = (Scaled << 7) + Scaled;
                int* Counter = (int*)(Bytes + Scaled * 4u + 0x314u);
                if (*Counter > 0)
                {
                    *Counter = *Counter - 1;
                    if (Result != 0 && State != 4)
                    {
                        Movie_CommitFieldSlot(Local200, Slot, (unsigned int*)Context, Sign);
                    }
                }
                else
                {
                    Result = 0;
                }
            }
            else
            {
                Result = 0;
            }
        }

        int Combined = 0;
        if (Context[0] != 0 && Result != 0)
        {
            Combined = 1;
        }
        Context[0] = Combined;
        RecordByte = RecordByte + 0x410;
        Index = Index + 1;
    }
}

// 0x06ac69a0. Depth is the dword at byte +0x11158. Slot limbs are at +0x114 + Slot*0x204.
// 06ac69c4 leaves EAX = Slot when *Context is 0. Every other path increments the depth,
// merges the step into *Context as 0 or 1 (06ac6a65), then decrements the depth (06ac6a81).
long __stdcall Movie_CommitFieldSlot(unsigned int* Limbs, int Slot, unsigned int* Context, int Sign)
{
    unsigned int Flag = Context[0];
    unsigned int* FieldHolder = (unsigned int*)Context[4];
    unsigned int SlotIndex = (unsigned int)Slot;
    unsigned int* SlotLimbs = Context + SlotIndex * 0x81u + 0x45u;
    if (Flag == 0u)
    {
        return Slot;
    }

    unsigned int Depth = Context[0x4456];
    Context[0x4456] = Depth + 1u;
    unsigned int Step = Flag;
    // 06ac69dc and 06ac69e1. Depths other than 0 and 1 fail the step.
    if (Depth != 0u && Depth != 1u)
    {
        Step = 0u;
    }
    else if (Movie_FieldElementIsUnit((int)Limbs, (int*)FieldHolder) == 0)
    {
        if (Movie_FieldElementIsUnit((int)SlotLimbs, (int*)FieldHolder) != 0)
        {
            // 06ac6a16. NEG / SBB / NEG collapses the callee's EAX to 0 or 1.
            long Blended = Movie_CopyOrNegatePair(Limbs, SlotLimbs, (int*)FieldHolder, Sign);
            if (Blended != 0)
            {
                Step = 1u;
            }
            else
            {
                Step = 0u;
            }
        }
        else
        {
            int* Field = (int*)FieldHolder[0];
            unsigned int* Saved = (unsigned int*)((unsigned char*)Context + Depth * 0x200u + 0x1115cu);
            if (Movie_CopyFieldGroups(SlotLimbs, Saved, 2, Field) != 0 &&
                StoreFieldUnit(SlotLimbs, (int*)FieldHolder) != 0)
            {
                Step = 1u;
            }
            else
            {
                Step = 0u;
            }
            // 06ac6a4f falls through to 0x06ac7760 from both the success and the failure side.
            Movie_AssignWindowSlot(Saved, Limbs, Slot, (int*)Context, Sign);
        }
    }

    unsigned int Result = 0u;
    if (Context[0] != 0u && Step != 0u)
    {
        Result = 1u;
    }
    Context[0] = Result;
    Context[0x4456] = Context[0x4456] - 1u;
    return (long)Result;
}

// 0x06ac6b70. 06ac6b7a calls 0x06ac61a0 while *Context is live and Context[2] is 0x14.
// The returned index is the count before the increment at 06ac6b93.
int __stdcall Movie_AllocateFieldRecord(int* Context)
{
    if (Context[0] == 0)
    {
        return 0;
    }
    while (Context[2] == 0x14)
    {
        Movie_AdvanceFieldRecordStates(Context);
        if (Context[0] == 0)
        {
            break;
        }
    }
    if (Context[0] == 0)
    {
        return 0;
    }
    int Index = Context[2];
    Context[2] = Index + 1;
    return Index;
}

// 0x06ac6a90. High is Limbs + 2 * Field[0] (LEA [edx+ecx*8] at 06ac6aa7).
// A nonzero zero-test returns that EAX (06ac6ab9). Slot 0 returns 0.
// Field+0x12c matching the high half tail-calls 0x06ac69a0. Otherwise the
// record at Index * 0x410 gets state 1, the slot, and the sign, and three
// limb groups are copied to +0x8418.
long __stdcall Movie_QueueOrCommitFieldLimbs(unsigned int* Limbs, unsigned int Slot, unsigned int* Context, unsigned int Sign)
{
    unsigned int* FieldHolder = (unsigned int*)Context[4];
    int* Field = (int*)FieldHolder[0];
    int LimbCount = Field[0];
    unsigned int* High = Limbs + LimbCount * 2;
    if (Context[0] == 0u)
    {
        return 0;
    }
    long Zero = Movie_FieldLimbsAreZero((int)High, Field);
    if (Zero != 0)
    {
        return Zero;
    }
    if (Slot == 0u)
    {
        return 0;
    }
    unsigned int* Constant = (unsigned int*)((unsigned char*)Field + 0x12c);
    if (Movie_FieldLimbsEqual((int)High, (int)Constant, Field) != 0)
    {
        return Movie_CommitFieldSlot(Limbs, (int)Slot, Context, (int)Sign);
    }

    int Index = Movie_AllocateFieldRecord((int*)Context);
    unsigned char* Record = (unsigned char*)Context + (unsigned int)Index * 0x410u;
    *reinterpret_cast<unsigned int*>(Record + 0x881c) = 1u;
    *reinterpret_cast<unsigned int*>(Record + 0x8820) = Slot;
    *reinterpret_cast<unsigned int*>(Record + 0x8818) = Sign;
    unsigned int* Counter = Context + Slot * 0x81u + 0xc5u;
    *Counter = *Counter + 1u;
    // 06ac6b3e. A dead flag skips the copy. Both failure sides store 0 at 06ac6b67.
    if (Context[0] != 0u &&
        Movie_CopyFieldGroups(Limbs, (unsigned int*)(Record + 0x8418), 3, Field) != 0)
    {
        Context[0] = 1u;
        return 1;
    }
    Context[0] = 0u;
    return 0;
}

// 0x06ac6c90. Multiplies two paired values. A zero high half copies the other
// operand (5 groups) and returns that copy's EAX. Otherwise the halves are
// squared or copied into Scratch at n..6n, and Field[2] >= 2 selects the
// product formula (CMP [field+8], 2 / SETGE at 06ac6cc3).
long __stdcall Movie_MultiplyPairedLimbs(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* FieldHolder, unsigned int* Scratch)
{
    int* Field = (int*)FieldHolder[0];
    int LimbCount = Field[0];
    unsigned int* Constant = (unsigned int*)((unsigned char*)Field + 0x12c);
    unsigned int* LeftHalf = Left + LimbCount;
    unsigned int* LeftTail = LeftHalf + LimbCount;
    unsigned int* RightHalf = Right + LimbCount;
    unsigned int* RightTail = RightHalf + LimbCount;
    unsigned int* DestHalf = Dest + LimbCount;
    unsigned int* DestTail = DestHalf + LimbCount;
    int ModeAtLeast2 = 0;
    if (Field[2] >= 2)
    {
        ModeAtLeast2 = 1;
    }

    // 06ac6cd9 and 06ac6ce5. Both results are overwritten before they are tested.
    Movie_FieldLimbsEqual((int)LeftTail, (int)Constant, Field);
    Movie_FieldLimbsEqual((int)RightTail, (int)Constant, Field);
    if (Movie_FieldLimbsAreZero((int)LeftTail, Field) != 0)
    {
        return Movie_CopyFieldGroups(Right, Dest, 5, Field);
    }
    if (Movie_FieldLimbsAreZero((int)RightTail, Field) != 0)
    {
        return Movie_CopyFieldGroups(Left, Dest, 5, Field);
    }

    int LeftMatches = Movie_FieldLimbsEqual((int)LeftTail, (int)Constant, Field);
    int RightMatches = Movie_FieldLimbsEqual((int)RightTail, (int)Constant, Field);
    unsigned int* Scratch0 = Scratch;
    unsigned int* Scratch1 = Scratch + LimbCount;
    unsigned int* Scratch2 = Scratch + LimbCount * 2;
    unsigned int* Scratch3 = Scratch + LimbCount * 3;
    unsigned int* Scratch4 = Scratch + LimbCount * 4;
    unsigned int* Scratch5 = Scratch + LimbCount * 5;
    unsigned int* Scratch6 = Scratch + LimbCount * 6;
    int LeftOk = 0;
    if (LeftMatches != 0)
    {
        if (Movie_CopyFieldGroups(Right, Scratch2, 1, Field) != 0 &&
            Movie_CopyFieldGroups(RightHalf, Scratch4, 1, Field) != 0)
        {
            LeftOk = 1;
        }
    }
    else if (Movie_MultiplyFieldLimbs(LeftTail, LeftTail, Scratch0, (unsigned int*)Field) != 0 &&
        Movie_MultiplyFieldLimbs(Right, Scratch0, Scratch2, (unsigned int*)Field) != 0 &&
        Movie_MultiplyFieldLimbs(LeftTail, Scratch0, Scratch0, (unsigned int*)Field) != 0 &&
        Movie_MultiplyFieldLimbs(RightHalf, Scratch0, Scratch4, (unsigned int*)Field) != 0)
    {
        LeftOk = 1;
    }

    int StepOk = 0;
    if (RightMatches != 0)
    {
        if (LeftOk != 0 &&
            Movie_CopyFieldGroups(Left, Scratch1, 1, Field) != 0 &&
            Movie_CopyFieldGroups(LeftHalf, Scratch3, 1, Field) != 0)
        {
            StepOk = 1;
        }
    }
    else if (LeftOk != 0 &&
        Movie_MultiplyFieldLimbs(RightTail, RightTail, Scratch0, (unsigned int*)Field) != 0 &&
        Movie_MultiplyFieldLimbs(Left, Scratch0, Scratch1, (unsigned int*)Field) != 0 &&
        Movie_MultiplyFieldLimbs(RightTail, Scratch0, Scratch0, (unsigned int*)Field) != 0 &&
        Movie_MultiplyFieldLimbs(LeftHalf, Scratch0, Scratch3, (unsigned int*)Field) != 0)
    {
        StepOk = 1;
    }

    if (Movie_FieldLimbsEqual((int)Scratch1, (int)Scratch2, Field) != 0)
    {
        if (Movie_FieldLimbsEqual((int)Scratch3, (int)Scratch4, Field) != 0)
        {
            if (StepOk != 0 && MultiplyFieldLimbs(Right, Dest, (int*)FieldHolder, Scratch) != 0)
            {
                return 1;
            }
            return 0;
        }
        if (StepOk != 0 && Movie_LoadFieldOne(Dest, (int*)FieldHolder, Scratch) != 0)
        {
            return 1;
        }
        return 0;
    }

    int DiffOk = 0;
    if (StepOk != 0 &&
        Movie_SubtractFieldLimbs(Scratch1, Scratch2, Scratch5, Field) != 0 &&
        Movie_SubtractFieldLimbs(Scratch3, Scratch4, Scratch6, Field) != 0)
    {
        DiffOk = 1;
    }

    int Mixed = 0;
    if (LeftMatches != 0)
    {
        if (DiffOk != 0 && Movie_CopyFieldGroups(Scratch5, Scratch0, 1, Field) != 0)
        {
            Mixed = 1;
        }
    }
    else if (DiffOk != 0 &&
        Movie_MultiplyFieldLimbs(Scratch5, LeftTail, Scratch0, (unsigned int*)Field) != 0)
    {
        Mixed = 1;
    }

    int Ready = 0;
    if (RightMatches != 0)
    {
        if (Mixed != 0 && Movie_CopyFieldGroups(Scratch0, DestTail, 1, Field) != 0)
        {
            Ready = 1;
        }
    }
    else if (Mixed != 0 &&
        Movie_MultiplyFieldLimbs(Scratch0, RightTail, DestTail, (unsigned int*)Field) != 0)
    {
        Ready = 1;
    }
    if (Ready != 0 &&
        Movie_MultiplyFieldLimbs(Scratch5, Scratch5, Scratch0, (unsigned int*)Field) != 0 &&
        Movie_MultiplyFieldLimbs(Scratch1, Scratch0, Scratch1, (unsigned int*)Field) != 0 &&
        Movie_MultiplyFieldLimbs(Scratch2, Scratch0, Scratch2, (unsigned int*)Field) != 0 &&
        Movie_AddFieldLimbs(Scratch1, Scratch2, Scratch0, Field) != 0)
    {
        Ready = 1;
    }
    else
    {
        Ready = 0;
    }
    if (Ready == 0)
    {
        return 0;
    }

    // 06ac705f. A failed step above arrives here with EAX clear and returns 0
    // before either formula's first call.
    if (ModeAtLeast2 != 0)
    {
        if (Movie_MultiplyFieldLimbs(Scratch4, Scratch1, Scratch1, (unsigned int*)Field) != 0 &&
            Movie_MultiplyFieldLimbs(Scratch3, Scratch2, Scratch2, (unsigned int*)Field) != 0 &&
            Movie_SubtractFieldLimbs(Scratch2, Scratch1, Scratch2, Field) != 0 &&
            Movie_MultiplyFieldLimbs(DestTail, DestTail, Dest, (unsigned int*)Field) != 0 &&
            Movie_MultiplyFieldLimbs(FieldHolder + 1, Dest, Dest, (unsigned int*)Field) != 0 &&
            Movie_AddFieldLimbs(Dest, Scratch0, Dest, Field) != 0 &&
            Movie_AddFieldLimbs(Scratch6, DestTail, Scratch0, Field) != 0 &&
            Movie_MultiplyFieldLimbs(Scratch0, Scratch6, Scratch6, (unsigned int*)Field) != 0 &&
            Movie_SubtractFieldLimbs(Scratch6, Dest, Dest, Field) != 0 &&
            Movie_MultiplyFieldLimbs(Scratch0, Dest, Scratch0, (unsigned int*)Field) != 0 &&
            Movie_SubtractFieldLimbs(Scratch2, Scratch0, DestHalf, Field) != 0)
        {
            return 1;
        }
        return 0;
    }
    if (Movie_MultiplyFieldLimbs(Scratch6, Scratch6, Dest, (unsigned int*)Field) != 0 &&
        Movie_SubtractFieldLimbs(Dest, Scratch0, Dest, Field) != 0 &&
        Movie_SubtractFieldLimbs(Scratch2, Scratch1, Scratch2, Field) != 0 &&
        Movie_MultiplyFieldLimbs(Scratch3, Scratch2, Scratch2, (unsigned int*)Field) != 0 &&
        Movie_SubtractFieldLimbs(Dest, Scratch1, Scratch0, Field) != 0 &&
        Movie_MultiplyFieldLimbs(Scratch0, Scratch6, Scratch0, (unsigned int*)Field) != 0 &&
        Movie_SubtractFieldLimbs(Scratch2, Scratch0, DestHalf, Field) != 0)
    {
        return 1;
    }
    return 0;
}

// 0x06ac6c40. 06ac6c55 is limb count * 5. Both callees must return nonzero; success is EAX = 1.
long __stdcall Movie_SeedAndMultiplyLimbs(unsigned int* Left, unsigned int* Value, unsigned int* Dest, unsigned int* FieldHolder, unsigned int* Scratch)
{
    int* Field = (int*)FieldHolder[0];
    int LimbCount = Field[0];
    unsigned int* Work = Scratch + LimbCount * 5;
    if (SeedFieldWork(Value, Work, (int*)FieldHolder, Scratch) == 0)
    {
        return 0;
    }
    if (Movie_MultiplyPairedLimbs(Left, Work, Dest, FieldHolder, Scratch) == 0)
    {
        return 0;
    }
    return 1;
}

// 0x06ac94c0. Strips leading zero limbs. Fewer than two limbs tail-calls
// 0x06ac9a30. Otherwise widths 3..7 are costed, and width 8 with a non-null
// Dest exponentiates at the cheapest width. Return is squares + multiplies.
int __stdcall Movie_ExponentiateByWindow(unsigned int* Base, unsigned int* Exponent, unsigned int ExponentLimbs, unsigned int* Dest, unsigned int* Context)
{
    unsigned int Significant = ExponentLimbs;
    while (Significant != 0u)
    {
        unsigned int Limb = Exponent[Significant - 1u];
        Significant = Significant - 1u;
        if (Limb != 0u)
        {
            Significant = Significant + 1u;
            break;
        }
    }
    if (Significant <= 1u)
    {
        unsigned int Limb = 0u;
        if (Significant != 0u)
        {
            Limb = Exponent[0];
        }
        return Movie_ExponentiateLimb(Base, Limb, Dest, Context);
    }

    unsigned int* Dispatch = (unsigned int*)Context[0x42];
    unsigned int Top = Exponent[Significant - 1u];
    unsigned long BitIndex = 0;
    _BitScanReverse(&BitIndex, Top);
    unsigned int BitLength = BitIndex + 1u;
    unsigned int Second = Exponent[Significant - 2u];
    unsigned int TotalBits = (Significant + 0x07ffffffu) << 5;
    TotalBits = TotalBits + BitLength;
    unsigned int Packed = (Second >> 1) >> (BitLength - 1u);
    Packed = Packed | (Top << (32u - BitLength));
    int BestWidth = -1;
    int BestMultiplies = 0;
    int BestSquares = 0;
    unsigned int* Work = (unsigned int*)Movie_AllocateBlock(0x1721cu);
    if (Work == 0)
    {
        if (Dest != 0)
        {
            unsigned int ByteCount = Context[0x40] << 2;
            unsigned int Words = ByteCount >> 2;
            unsigned int Tail = ByteCount & 3u;
            unsigned int Index;
            for (Index = 0u; Index < Words; Index = Index + 1u)
            {
                Dest[Index] = 0u;
            }
            unsigned char* TailBytes = (unsigned char*)(Dest + Words);
            unsigned int TailIndex;
            for (TailIndex = 0u; TailIndex < Tail; TailIndex = TailIndex + 1u)
            {
                TailBytes[TailIndex] = 0;
            }
        }
        return 0;
    }

    int Width = 3;
    for (;;)
    {
        int UsedWidth = Width;
        int DoReal = 0;
        if (Width == 8 && Dest != 0)
        {
            DoReal = 1;
            UsedWidth = BestWidth;
        }
        unsigned int Span = 1u << (unsigned int)UsedWidth;
        unsigned int Window = Packed >> (0x1eu - (unsigned int)UsedWidth);
        unsigned int Half = Span >> 1;
        unsigned int BitCursor = TotalBits - (unsigned int)UsedWidth - 2u;
        int SquareCount = 1;
        int MultiplyCount = (int)(Half - 1u);
        int Extra = 0;
        unsigned int Accum = 0u;
        if (DoReal != 0)
        {
            unsigned int* High = Movie_ExponentSlot(Work, Half - 1u);
            Movie_EvaluateWindow(Base, Context[0x40], Work, (unsigned char*)(Work + 0x145), Dispatch);
            Movie_MultiplyTermsModulo(Work, Work + 0x145, Work, Work + 0x145, Dest, Context, Work + 0x53c0);
            Movie_EvaluateWindow(Dest, Context[0x40], High + 1, (unsigned char*)(High + 0x145), Dispatch);
            if (Half != 1u)
            {
                unsigned int* Cursor = Work;
                unsigned int Left = Half - 1u;
                while (Left != 0u)
                {
                    unsigned int* Next = Cursor + 0x14f;
                    Movie_MultiplyTermsModulo(Cursor, Cursor + 0x145, High + 1, High + 0x145, Dest, Context, Work + 0x53c0);
                    Movie_EvaluateWindow(Dest, Context[0x40], Next, (unsigned char*)(Next + 0x145), Dispatch);
                    Cursor = Next;
                    Left = Left - 1u;
                }
            }
        }

        // 06ac970c. JA is unsigned. The switch at 06ac9729 is Window & 3;
        // 0, 1 and 3 share 06ac9730, and 2 is 06ac9746.
        if (Window > Span * 3u - 2u)
        {
            unsigned int Quarter = Window >> 2;
            if ((Quarter & 1u) == 0u)
            {
                if (DoReal != 0)
                {
                    unsigned int* Slot = Movie_ExponentSlot(Work, Quarter - Half);
                    unsigned int* High = Movie_ExponentSlot(Work, Half - 1u);
                    Movie_MultiplyTermsModulo(High, High + 0x145, Slot, Slot + 0x145, Dest, Context, Work + 0x53c0);
                }
                SquareCount = 2;
            }
            else
            {
                if (DoReal != 0)
                {
                    unsigned int* Slot = Movie_ExponentSlot(Work, Quarter >> 1);
                    Movie_MultiplyTermsModulo(Slot, Slot + 0x145, Slot, Slot + 0x145, Dest, Context, Work + 0x53c0);
                }
                MultiplyCount = MultiplyCount + 1;
            }
            Extra = 1;
            Accum = Window - Quarter * 4u;
        }
        else if ((Window & 3u) == 2u)
        {
            if (DoReal != 0)
            {
                unsigned int* High = Movie_ExponentSlot(Work, Half - 1u);
                unsigned int* Slot = Movie_ExponentSlot(Work, (Window >> 1) - Span);
                Movie_MultiplyTermsModulo(High, High + 0x145, High, High + 0x145, Dest, Context, Work + 0x53c0);
                Movie_MultiplyFactorModulo(Dest, Slot, Slot + 0x145, Dest, Context, Work + 0x53c0);
            }
            SquareCount = 2;
            MultiplyCount = MultiplyCount + 1;
            Extra = 0;
            Accum = 1u;
        }
        else
        {
            Extra = 1;
            Accum = Window - Span * 2u + 2u;
        }

        for (;;)
        {
            while (Accum < Half && BitCursor != 0u)
            {
                BitCursor = BitCursor - 1u;
                Extra = Extra + 1;
                unsigned int Bit = (Exponent[BitCursor >> 5] >> (BitCursor & 0x1fu)) & 1u;
                Accum = Bit + Accum * 2u;
            }
            if (Accum != 0u)
            {
                unsigned long Zeros = 0;
                _BitScanForward(&Zeros, Accum);
                if ((unsigned int)Extra != Zeros)
                {
                    unsigned int Steps = (unsigned int)Extra - Zeros;
                    SquareCount = (int)((unsigned int)SquareCount + Steps);
                    while (Steps != 0u)
                    {
                        if (DoReal != 0)
                        {
                            Movie_MultiplyModulo(Dest, Dest, Dest, Context, Work + 0x53c0);
                        }
                        Extra = (int)((unsigned int)Extra - 1u);
                        Steps = Steps - 1u;
                    }
                }
                if (DoReal != 0)
                {
                    unsigned int Index = Accum >> (Zeros + 1u);
                    unsigned int* Slot = Movie_ExponentSlot(Work, Index);
                    Movie_MultiplyFactorModulo(Dest, Slot, Slot + 0x145, Dest, Context, Work + 0x53c0);
                }
                MultiplyCount = MultiplyCount + 1;
                Accum = 0u;
            }
            if (Extra != 0)
            {
                SquareCount = SquareCount + Extra;
                while (Extra != 0)
                {
                    if (DoReal != 0)
                    {
                        Movie_MultiplyModulo(Dest, Dest, Dest, Context, Work + 0x53c0);
                    }
                    Extra = Extra - 1;
                }
            }
            if (BitCursor == 0u)
            {
                break;
            }
        }

        int SquareDelta = SquareCount - BestSquares;
        int MultiplyDelta = MultiplyCount - BestMultiplies;
        int Delta = SquareDelta + SquareDelta * 2 + MultiplyDelta * 4;
        if (BestWidth < 0 || Delta < 0)
        {
            BestWidth = UsedWidth;
            BestMultiplies = MultiplyCount;
            BestSquares = SquareCount;
        }
        Width = Width + 1;
        if (Width > 8)
        {
            break;
        }
    }

    Movie_FreeBlock(Work);
    return BestSquares + BestMultiplies;
}
