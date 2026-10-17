// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_effectarchetype.h"

#include "alchemy_common.h"

#include <cstring>
#include <new>

// alchemy.dll — DACOM particle FX scaffolding.
// Out-of-line method bodies for FFxEffectArchetype.
namespace AlchemyImpl
{
    GENRESULT DACOM_API FFxEffectArchetype::QueryInterface(const char* Iid, void** Out)
    {
        if (IidEqual(Iid, Iids::FxEffectArchetype) || IidEqual(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IFxEffectArchetype*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FFxEffectArchetype::Release()
    {
        // FUN_06222900 (alchemy.dll.txt:71344-71367): retail guards the
        // decrement against underflow (`if (refcount != 0) refcount--;`
        // before the zero-check) rather than decrementing unconditionally --
        // the same underflow-guard pattern already fixed in
        // FComponentFactory::Release (alchemy_factory.cpp) and in
        // FFxRuntime::Release (alchemy_fxruntime.cpp). An extra Release()
        // call past 0 previously underflowed RefCount_ to 0xFFFFFFFF here
        // instead of retail's no-op.
        //
        // [wave61-lane2] fresh re-derivation from raw bytes also found retail
        // stores 0x1 back into [ESI+0x48] (`MOV dword ptr [ESI+0x48],0x1`,
        // 06222919) immediately before calling the member-teardown helper
        // (FUN_06222c50) and operator delete (FUN_062460e0) -- the same
        // teardown reentrancy-guard pattern documented for FFxNodeLibrary
        // (alchemy_nodelibrary.cpp), FFxAnimatedSingle
        // (alchemy_ramp.cpp) and FFxEffectObject (alchemy_effectobject.cpp).
        // It guards against a reentrant AddRef/Release on `this` observing
        // RefCount_==0 while teardown is still in progress. Mirrored here to
        // match retail exactly.
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ == 0)
        {
            RefCount_ = 1;
            delete this;
            return 0;
        }
        return RefCount_;
    }

    GENRESULT DACOM_API FFxEffectArchetype::ResetRecords()
    {
        NextId_ = 0;
        Rec16Count_ = 0;
        Rec8Count_ = 0;
        std::memset(Header16_, 0, sizeof(Header16_));
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::SetName(const char* Name)
    {
        Name_ = Name;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::AddRec16(const FAleRec16* Rec, unsigned long* OutId,
                                                      unsigned long Flag)
    {
        unsigned long Assigned = Flag;
        if (Flag != 0)
        {
            for (unsigned long Index = 0; Index < Rec16Count_; ++Index)
            {
                const FAleRec16* Stored = &Rec16_[Index];
                if (Rec->D == Stored->B && Rec->B == Stored->D && Rec->C == Stored->A &&
                    Rec->A == Stored->C)
                    return GR_GENERIC;
            }
        }

        FAleRec16 Stored;
        Stored.A = Rec->C;
        Stored.B = Rec->D;
        Stored.C = Rec->A;
        Stored.D = Rec->B;
        AppendRec16(Stored);
        if (Flag == 0)
        {
            ++NextId_;
            Assigned = NextId_;
        }
        Rec16_[Rec16Count_ - 1].B = Assigned;

        if (static_cast<long>(NextId_) <= static_cast<long>(Assigned))
            NextId_ = Assigned;
        if (OutId != nullptr)
            *OutId = Assigned;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::RemoveRec16(unsigned long Id)
    {
        unsigned long Found = Rec16Count_;
        for (unsigned long Index = 0; Index < Rec16Count_; ++Index)
        {
            if (Rec16_[Index].B == Id)
            {
                Found = Index;
                break;
            }
        }
        if (Found == Rec16Count_)
            return GR_GENERIC;

        for (unsigned long Index = Found + 1; Index < Rec16Count_; ++Index)
            Rec16_[Index - 1] = Rec16_[Index];
        Rec16Count_ -= 1;

        for (unsigned long Index = 0; Index < Rec8Count_;)
        {
            if (Rec8_[Index].A != Id && Rec8_[Index].B != Id)
            {
                ++Index;
            }
            else
            {
                for (unsigned long ShiftIndex = Index + 1; ShiftIndex < Rec8Count_; ++ShiftIndex)
                    Rec8_[ShiftIndex - 1] = Rec8_[ShiftIndex];
                --Rec8Count_;
            }
        }
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::Slot8(unsigned long Id, unsigned long Value)
    {
        for (unsigned long Index = 0; Index < Rec16Count_; ++Index)
        {
            if (Rec16_[Index].B == Id)
            {
                Rec16_[Index].A = Value;
                return GR_OK;
            }
        }
        return GR_GENERIC;
    }

    GENRESULT DACOM_API FFxEffectArchetype::AddRec8(const FAleRec8* Rec)
    {
        for (unsigned long Index = 0; Index < Rec8Count_; ++Index)
        {
            if (Rec8_[Index].A == Rec->A && Rec8_[Index].B == Rec->B)
                return GR_GENERIC;
        }
        FAleRec8 Stored;
        Stored.A = Rec->A;
        Stored.B = Rec->B;
        AppendRec8(Stored);
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::RemoveRec8(const FAleRec8* Rec)
    {
        unsigned long Found = Rec8Count_;
        for (unsigned long Index = 0; Index < Rec8Count_; ++Index)
        {
            if (Rec8_[Index].A == Rec->A && Rec8_[Index].B == Rec->B)
            {
                Found = Index;
                break;
            }
        }
        if (Found == Rec8Count_)
            return GR_GENERIC;
        for (unsigned long Index = Found + 1; Index < Rec8Count_; ++Index)
            Rec8_[Index - 1] = Rec8_[Index];
        Rec8Count_ -= 1;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::GetRec16(unsigned long Index, FAleRec16* Out)
    {
        const FAleRec16* Stored = &Rec16_[Index];
        Out->D = Stored->B;
        Out->B = Stored->D;
        Out->C = Stored->A;
        Out->A = Stored->C;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::GetRec8(unsigned long Index, FAleRec8* Out)
    {
        Out->A = Rec8_[Index].A;
        Out->B = Rec8_[Index].B;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::GetRec16ById(unsigned long Id, FAleRec16* Out)
    {
        const FAleRec16* Stored = Rec16_;
        const FAleRec16* const End = Rec16Count_ == 0 ? Stored : Stored + Rec16Count_;
        while (Stored != End)
        {
            if (Stored->B == Id)
                break;
            ++Stored;
        }
        // The record loads are unconditional, including an unchecked end cursor.
        Out->D = Stored->B;
        Out->B = Stored->D;
        Out->C = Stored->A;
        Out->A = Stored->C;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::FindRec8Index(const FAleRec8* Rec,
                                                           unsigned long* OutIndex)
    {
        for (unsigned long Index = 0; Index < Rec8Count_; ++Index)
        {
            if (Rec8_[Index].A != Rec->A || Rec8_[Index].B != Rec->B)
                continue;
            if (OutIndex != nullptr)
                *OutIndex = Index;
            return GR_OK;
        }
        return GR_GENERIC;
    }

    GENRESULT DACOM_API FFxEffectArchetype::RemapRec16FieldD(unsigned long OldId,
                                                              unsigned long NewId)
    {
        for (unsigned long Index = 0; Index < Rec16Count_; ++Index)
        {
            if (Rec16_[Index].D == OldId)
                Rec16_[Index].D = NewId;
        }
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::GetHeader16(unsigned char* Out16)
    {
        for (unsigned long Index = 0; Index < 16; Index += 4)
        {
            unsigned long Word;
            std::memcpy(&Word, Header16_ + Index, sizeof(Word));
            std::memcpy(Out16 + Index, &Word, sizeof(Word));
        }
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectArchetype::SetHeader16(const unsigned char* Header16)
    {
        for (unsigned long Index = 0; Index < 16; Index += 4)
        {
            unsigned long Word;
            std::memcpy(&Word, Header16 + Index, sizeof(Word));
            std::memcpy(Header16_ + Index, &Word, sizeof(Word));
        }
        return GR_OK;
    }

    void FFxEffectArchetype::AppendRec16(const FAleRec16& Record)
    {
        const unsigned long Position = Rec16Count_;
        if (Rec16Capacity_ - Rec16Count_ < 1)
        {
            const unsigned long Growth = Rec16Count_ > 1 ? Rec16Count_ : 1;
            const unsigned long Capacity = Rec16Count_ + Growth;
            const unsigned long AllocationCount = static_cast<long>(Capacity) < 0 ? 0 : Capacity;
            FAleRec16* Next = new FAleRec16[AllocationCount];
            for (unsigned long Index = 0; Index < Position; ++Index)
                Next[Index] = Rec16_[Index];
            Next[Position] = Record;
            for (unsigned long Index = Position; Index < Rec16Count_; ++Index)
                Next[Index + 1] = Rec16_[Index];
            delete[] Rec16_;
            Rec16Capacity_ = Capacity;
            const unsigned long CurrentCount = Rec16_ != nullptr ? Rec16Count_ : 0;
            Rec16_ = Next;
            Rec16Count_ = CurrentCount + 1;
            return;
        }
        Rec16_[Position] = Record;
        ++Rec16Count_;
    }

    void FFxEffectArchetype::AppendRec8(const FAleRec8& Record)
    {
        const unsigned long Position = Rec8Count_;
        if (Rec8Capacity_ - Rec8Count_ < 1)
        {
            const unsigned long Growth = Rec8Count_ > 1 ? Rec8Count_ : 1;
            const unsigned long Capacity = Rec8Count_ + Growth;
            const unsigned long AllocationCount = static_cast<long>(Capacity) < 0 ? 0 : Capacity;
            FAleRec8* Next = new FAleRec8[AllocationCount];
            for (unsigned long Index = 0; Index < Position; ++Index)
                Next[Index] = Rec8_[Index];
            Next[Position] = Record;
            for (unsigned long Index = Position; Index < Rec8Count_; ++Index)
                Next[Index + 1] = Rec8_[Index];
            delete[] Rec8_;
            Rec8Capacity_ = Capacity;
            const unsigned long CurrentCount = Rec8_ != nullptr ? Rec8Count_ : 0;
            Rec8_ = Next;
            Rec8Count_ = CurrentCount + 1;
            return;
        }
        Rec8_[Position] = Record;
        ++Rec8Count_;
    }

} // namespace AlchemyImpl
