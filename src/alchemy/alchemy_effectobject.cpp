// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_effectobject.h"

#include "alchemy_common.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

// alchemy.dll — DACOM particle FX scaffolding.
// Out-of-line method bodies for FFxEffectObject.
namespace AlchemyImpl
{
    // Every node this object holds is an IGenericFx. Retail reaches their slots straight
    // off the stored pointer -- 0x0621f537 calls [node+0x2c] with no QueryInterface -- so
    // this is a cast, not a query. It exists because the nodes are stored as IDAComponent*.
    static IAlchemy* AsAlchemy()
    {
        return G_EffectAlchemy;
    }

    static IGenericFx* AsFx(IDAComponent* Node)
    {
        return static_cast<IGenericFx*>(Node);
    }

    struct FFxTemporaryInterface
    {
        FFxTemporaryInterface() : Value(nullptr) {}
        ~FFxTemporaryInterface() noexcept(false) { Reset(); }

        void Reset()
        {
            if (Value != nullptr)
            {
                Value->Release();
                Value = nullptr;
            }
        }

        IDAComponent* Value;
    };

    namespace
    {
        unsigned long EffectRecordDistance(const FFxEffectNodeRec* First,
                                           const FFxEffectNodeRec* Last)
        {
            return First == Last ? 0 : static_cast<unsigned long>(Last - First);
        }

        FFxEffectNodeRec* CopyEffectRecords(const FFxEffectNodeRec* First,
                                           const FFxEffectNodeRec* Last,
                                           FFxEffectNodeRec* Destination)
        {
            while (First != Last)
            {
                new (Destination) FFxEffectNodeRec(*First);
                ++First;
                ++Destination;
            }
            return Destination;
        }

        void ConstructEffectRecords(FFxEffectNodeRec* Destination, unsigned long Count,
                                    const FFxEffectNodeRec& Record)
        {
            while (Count != 0)
            {
                new (Destination) FFxEffectNodeRec(Record);
                ++Destination;
                --Count;
            }
        }

        void AssignEffectRecords(FFxEffectNodeRec* First, FFxEffectNodeRec* Last,
                                 const FFxEffectNodeRec& Record)
        {
            while (First != Last)
            {
                *First = Record;
                ++First;
            }
        }

        void CopyEffectRecordsBackward(const FFxEffectNodeRec* First,
                                        const FFxEffectNodeRec* Last,
                                        FFxEffectNodeRec* DestinationEnd)
        {
            while (First != Last)
            {
                --Last;
                --DestinationEnd;
                *DestinationEnd = *Last;
            }
        }

        void DestroyEffectRecords(FFxEffectNodeRec* First, FFxEffectNodeRec* Last)
        {
            while (First != Last)
            {
                First->~FFxEffectNodeRec();
                ++First;
            }
        }
    }

    FFxEffectObject::~FFxEffectObject() noexcept(false)
    {
        struct FNodeStorageOwner
        {
            FFxEffectObject* Owner;

            ~FNodeStorageOwner() noexcept(false)
            {
                FFxEffectNodeRec* Record = Owner->Nodes_;
                FFxEffectNodeRec* const End = Owner->NodeCount_ == 0 ?
                    Owner->Nodes_ : Owner->Nodes_ + Owner->NodeCount_;
                DestroyEffectRecords(Record, End);
                ::operator delete(Owner->Nodes_);
                Owner->Nodes_ = nullptr;
                Owner->NodeCount_ = 0;
                Owner->NodeCapacity_ = 0;
            }
        } Storage{this};

        if (Archetype_ != nullptr)
        {
            Archetype_->Release();
            Archetype_ = nullptr;
        }
    }

    GENRESULT DACOM_API FFxEffectObject::QueryInterface(const char* Iid, void** Out)
    {
        if (IidEqual(Iid, Iids::FxEffect) || IidEqual(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IFxEffect*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FFxEffectObject::Release()
    {
        // LAB_06222810 (alchemy.dll.txt:71219-71236): retail also guards the
        // decrement itself against underflow (`if (refcount != 0)
        // refcount--;` before the zero-check), not just the teardown step
        // below -- the same underflow-guard pattern already fixed in
        // FComponentFactory::Release, FFxRuntime::Release, and
        // FFxEffectArchetype::Release. A stray extra Release() call past 0
        // previously underflowed RefCount_ to 0xFFFFFFFF here instead of
        // retail's no-op. Fixed as part of the wave-42 targeted COM-refcount
        // sweep.
        if (RefCount_ != 0)
            --RefCount_;
        const unsigned long Next = RefCount_;
        if (Next == 0)
        {
            // retail resets the refcount to 1 before tearing the object down
            // (CALL [vtable+0x44](this,1) == destructor+delete), guarding against
            // a re-entrant Release() reaching zero again while teardown is
            // still in progress. Mirror that guard here.
            //
            // wave63 (sonnet:alchemy-wave63-lane1, 2026-09-11): re-verified
            // byte-for-byte against LAB_06222810 (alchemy.dll.txt:71226-
            // 71233: `MOV dword ptr [ECX+0x20],0x1` immediately before
            // `CALL dword ptr [EAX+0x44]`) as part of a module-wide sweep of
            // this reentrancy-guard idiom -- confirmed already correctly
            // ported here, no change needed.
            RefCount_ = 1;
            this->~FFxEffectObject();
            ::operator delete(this);
            return 0;
        }
        return Next;
    }

    GENRESULT DACOM_API FFxEffectObject::Slot3(void* A, void* B)
    {
        // FUN_0621f610 (alchemy.dll.txt:64408-64466):
        //   if (this+0x18 != 0)  →  JMP (*(+0x18))->vtable+0x3C(node, A, B)
        //   else walk Nodes_: Pad7 skip, then node+0x3C(A,B); FDUMP on fail.
        // Initialize selects this+0x18 as an alias to the override-folder record. A and B
        // are only forwarded to the nodes' vt+0x3c, which on an appearance
        // is the this+0xc thunk (e.g. FUN_06226f80) into FUN_062400f0: A is the slot index
        // (0 composed +0x4c, 1 raw +0x1c, else -2) and B the 12-float source. Callers:
        // gf_thorn_psys.cpp (1, Xform), particle_effect.cpp (0, &World). A port-only copy of A
        // as 16 floats into a Transform_ cache was removed 2026-09-26 -- A is an index.

        // 0621f611..0621f625: the override node's vt+0x3c takes the call alone (tail JMP),
        // with no null test on the node.
        const unsigned long SlotIndex =
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(A));
        const float* const Source12 = static_cast<const float*>(B);
        if (TransformExtra_ != nullptr)
        {
            IDAComponent* const Node = TransformExtra_->Node;
            return AsFx(Node)->SetNodeTransform(SlotIndex, Source12);
        }

        // 0621f628..0621f693: every root-prefix record (count +4), no null test on the
        // node. vt+0x1c answering non-zero in AL skips it (0621f646); otherwise vt+0x3c(A, B),
        // and a non-zero result is reported.
        FFxEffectNodeRec* Record = Nodes_;
        for (long Index = 0; Index < RootNodeCount_; ++Index, ++Record)
        {
            IGenericFx* const Fx = AsFx(Record->Node);
            if (static_cast<unsigned char>(Fx->IsNodeDead()) != 0)
                continue;
            if (Fx->SetNodeTransform(SlotIndex, Source12) == GR_OK)
                continue;
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                  "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxEffect.cpp",
                  0x19e, "Failed to set node transform. Continuing.");
        }
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectObject::Slot4(void* A, void* B)
    {
        // LAB_0621f6a0: this+0x18 (TransformExtra_) override target takes
        // priority over the node list when set (previously not ported -- Slot4
        // always fell through to Nodes_[0].Node regardless of TransformExtra_).
        // 0621f6a0..0621f6b3: tail JMP to the override node's vt+0x40, no null test.
        // A = slot index, B = float** out (FUN_062400b0 behind the node's vt+0x40).
        const unsigned long SlotIndex =
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(A));
        float** const OutSlot = static_cast<float**>(B);
        if (TransformExtra_ != nullptr)
        {
            IDAComponent* const Node = TransformExtra_->Node;
            return AsFx(Node)->GetNodeTransform(SlotIndex, OutSlot);
        }
        // 0621f6b6..0621f6c1: otherwise the first record's node (+0xc -> [0]) takes it, with
        // neither a count nor a null test (the port-only Transform_ fallback is gone).
        return AsFx(Nodes_[0].Node)->GetNodeTransform(SlotIndex, OutSlot);
    }

    GENRESULT DACOM_API FFxEffectObject::Slot7(void* Arg)
    {
        // FUN_0621f6d0, re-verified: self-contained WalkNodesAlchemy(0x3C)
        // only. No 0x64 call anywhere in this function's body.
        return WalkNodesAlchemy(&IAlchemy::Slot15, Arg);
    }

    GENRESULT DACOM_API FFxEffectObject::Slot8(void* Arg)
    {
        // FUN_0621f730, re-verified: self-contained WalkNodesAlchemy(0x40)
        // only. No 0x64 call anywhere in this function's body.
        return WalkNodesAlchemy(&IAlchemy::Slot16, Arg);
    }

    GENRESULT DACOM_API FFxEffectObject::Slot9(void* Arg)
    {
        // FUN_0621f790, re-verified: self-contained WalkNodesAlchemy(0x44)
        // only. No 0x64 call anywhere in this function's body.
        return WalkNodesAlchemy(&IAlchemy::DrawNodes, Arg);
    }

    GENRESULT DACOM_API FFxEffectObject::Slot10(void* IdRaw, void* Out)
    {
        const unsigned long Id =
            static_cast<unsigned long>(reinterpret_cast<unsigned int>(IdRaw));
        for (unsigned long Index = 0; Index < NodeCount_; ++Index)
        {
            if (Nodes_[Index].Id != Id)
                continue;
            *static_cast<void**>(Out) = Nodes_[Index].Node;
            return GR_OK;
        }
        return GR_GENERIC;
    }

    GENRESULT DACOM_API FFxEffectObject::Slot11(void* Arg)
    {
        return WalkNodesFlagged(static_cast<unsigned long>(reinterpret_cast<unsigned int>(Arg)));
    }

    GENRESULT DACOM_API FFxEffectObject::Slot12(void* A, void* B)
    {
        return WalkNodesEnumerate(A, B);
    }

    GENRESULT DACOM_API FFxEffectObject::Slot13(void* Arg)
    {
        return DispatchFlagsToAlchemy(static_cast<unsigned long>(reinterpret_cast<unsigned int>(Arg)));
    }

    GENRESULT DACOM_API FFxEffectObject::Slot15(void* Arg)
    {
        return MergeNodeBoundsSphere(Arg);
    }

    GENRESULT DACOM_API FFxEffectObject::GetBoundComponent(void** Out)
    {
        // 0621fd00: publish the captured archetype, then acquire it unconditionally.
        IFxEffectArchetype* const Bound = Archetype_;
        *Out = Bound;
        Bound->AddRef();
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectObject::Slot17()
    {
        Unbind();
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectObject::Initialize(AGGDESC* Desc)
    {
        // FUN_0621eed0 reads desc+8 unconditionally (no size or null test); CreateEffect
        // passes {8, "FxEffect", archetype, Param1}.
        FFxEffectCreateDesc* Extended = reinterpret_cast<FFxEffectCreateDesc*>(Desc);
        IFxEffectArchetype* const InitialArchetype = Extended->Archetype;
        BindArchetype(InitialArchetype);
        return InstantiateNodesFromArchetype(InitialArchetype, Extended);
    }

    void FFxEffectObject::BindArchetype(IFxEffectArchetype* Archetype)
    {
        // FUN_0621eed0's own inline rebind (alchemy.dll.c:18690-18697,
        // alchemy.dll.txt:63762-...): `if (Archetype_ != New) { Release(old
        // via FUN_062212e0); Archetype_ = New; if (New) AddRef(New); }` --
        // re-verified this wave: no name-copy call of any kind appears here
        // (FUN_062212e0 itself is a plain release-and-null helper, confirmed
        // by its own bytes at alchemy.dll.txt:68030-68044). A pre-wave-61
        // version of this port also cached the archetype's name into an
        // unused Name_[256] buffer via CopyNameFromBound() -- that field had
        // no retail counterpart (the class's real vtable, dumped in full at
        // alchemy.dll.txt:162302-162323, has exactly 19 slots and none of
        // them expose a name) and nothing in this class ever read it back.
        // Removed along with the same-Archetype early-out's dead call.
        if (Archetype_ == Archetype)
            return;
        if (Archetype_ != nullptr)
            Archetype_->Release();
        Archetype_ = Archetype;
        if (Archetype_ != nullptr)
            Archetype_->AddRef();
    }

    void FFxEffectObject::Unbind()
    {
        ClearNodes();
        if (Archetype_ != nullptr)
        {
            Archetype_->Release();
            Archetype_ = nullptr;
        }
    }

    // Was WalkNodesAlchemy(Off, Arg): IAlchemy's slot number as a runtime argument, for
    // three different slots. IAlchemy has been public in src/include/alchemy.h all along.
    GENRESULT FFxEffectObject::WalkNodesAlchemy(
        GENRESULT (DACOM_API IAlchemy::*Method)(void*, void*), void* Arg)
    {
        GENRESULT Result = GR_OK;
        FFxEffectNodeRec* Record = Nodes_;
        for (long Index = 0; Index < RootNodeCount_; ++Index, ++Record)
        {
            IDAComponent* Node = Record->Node;
            if (static_cast<unsigned char>(AsFx(Node)->IsNodeDead()) != 0)
                continue;
            Result = (G_EffectAlchemy->*Method)(Arg, Node);
        }
        return Result;
    }

    unsigned char FFxEffectObject::CallNodeFlagSlot(
        IDAComponent* Node, GENRESULT (DACOM_API IGenericFx::*Query)(unsigned char*))
    {
        unsigned char OutFlag = 0;
        (AsFx(Node)->*Query)(&OutFlag);
        return OutFlag;
    }

    GENRESULT FFxEffectObject::WalkNodesFlagged(unsigned long Flags)
    {
        const unsigned long Count = NodeCount_;
        FFxEffectNodeRec* Record = Nodes_;
        for (unsigned long Index = 0; Index < Count; ++Index, ++Record)
        {
            if (TransformExtra_ != nullptr &&
                Record->Node == TransformExtra_->Node)
                continue;
            if (static_cast<unsigned char>(AsFx(Record->Node)->IsNodeDead()) != 0)
                continue;

            if (Flags == 7)
                return static_cast<GENRESULT>(0);
            if ((Flags & 0x1) != 0 && CallNodeFlagSlot(Record->Node, &IGenericFx::GetFlagBit1) != 0)
                return static_cast<GENRESULT>(0);
            if ((Flags & 0x2) != 0 && CallNodeFlagSlot(Record->Node, &IGenericFx::GetFlagBit2) != 0)
                return static_cast<GENRESULT>(0);
            if ((Flags & 0x4) != 0 && CallNodeFlagSlot(Record->Node, &IGenericFx::GetFlagBit4) != 0)
                return static_cast<GENRESULT>(0);
        }
        return static_cast<GENRESULT>(1);
    }

    GENRESULT FFxEffectObject::DispatchFlagsToAlchemy(unsigned long Flags)
    {
        FFxEffectNodeRec* Record = Nodes_;
        if (Flags == 7)
        {
            const unsigned long Count = NodeCount_;
            for (long Index = 0; Index < RootNodeCount_ &&
                                  static_cast<unsigned long>(Index) < Count; ++Index, ++Record)
            {
                IDAComponent* Node = Record->Node;
                if (static_cast<unsigned char>(AsFx(Node)->IsNodeDead()) == 0)
                    G_EffectAlchemy->SubmitNode(Node);
            }
            DestroyNodeRecords();
            return GR_OK;
        }
        if (NodeCount_ == 0)
            return GR_OK;
        do
        {
            IDAComponent* Node = Record->Node;
            if (static_cast<unsigned char>(AsFx(Node)->IsNodeDead()) == 0)
            {
                if ((Flags & 0x1) != 0 &&
                    CallNodeFlagSlot(Record->Node, &IGenericFx::GetFlagBit1) != 0)
                    G_EffectAlchemy->SubmitNode(Node);
                if ((Flags & 0x2) != 0 &&
                    CallNodeFlagSlot(Record->Node, &IGenericFx::GetFlagBit2) != 0)
                    G_EffectAlchemy->SubmitNode(Node);
                if ((Flags & 0x4) != 0 &&
                    CallNodeFlagSlot(Record->Node, &IGenericFx::GetFlagBit4) != 0)
                    G_EffectAlchemy->SubmitNode(Node);
            }
            ++Record;
        } while (Record != Nodes_ + NodeCount_);
        return GR_OK;
    }

    GENRESULT FFxEffectObject::WalkNodesEnumerate(void* Callback, void* Context)
    {
        using FnEnum = unsigned long(__cdecl*)(const FNodeIdPair*, void*);
        FnEnum Enum = reinterpret_cast<FnEnum>(Callback);
        const unsigned long Count = NodeCount_;
        FFxEffectNodeRec* Record = Nodes_;
        for (unsigned long Index = 0; Index < Count; ++Index, ++Record)
        {
            FNodeIdPair Pair;
            Pair.Id = Record->Id;
            Pair.Node = Record->Node;
            if (Enum(&Pair, Context) != 0)
                break;
        }
        return GR_OK;
    }

    GENRESULT FFxEffectObject::MergeNodeBoundsSphere(void* Arg)
    {
        float* Sphere = static_cast<float*>(Arg);
        unsigned long SphereCount = 0;
        FFxEffectNodeRec* Record = Nodes_;
        FFxEffectNodeRec* const End = NodeCount_ == 0 ? Nodes_ : Nodes_ + NodeCount_;
        for (; Record != End; ++Record)
        {
            IGenericFx* Fx = AsFx(Record->Node);
            if (SphereCount == 0)
            {
                if (Fx->GetBoundingSphere(Sphere) == GR_OK)
                    SphereCount = 1;
                continue;
            }

            float NodeSphere[4];
            if (Fx->GetBoundingSphere(NodeSphere) != GR_OK)
                continue;
            ++SphereCount;

            // Volatile float stores retain the retail FST/FSTP rounding boundaries.
            const volatile float Dx = static_cast<float>(static_cast<double>(NodeSphere[0]) - Sphere[0]);
            const volatile float Dy = static_cast<float>(static_cast<double>(NodeSphere[1]) - Sphere[1]);
            const double DzWide = static_cast<double>(NodeSphere[2]) - Sphere[2];
            const volatile float Dz = static_cast<float>(DzWide);
            const double Dot = (DzWide * Dz + static_cast<double>(Dy) * Dy) + static_cast<double>(Dx) * Dx;
            const volatile float DotStored = static_cast<float>(Dot);
            const float DotForBits = DotStored;
            std::uint32_t DotBits;
            std::memcpy(&DotBits, &DotForBits, sizeof(DotBits));
            const std::uint32_t SeedBits = 0x5f3759dfu - static_cast<std::uint32_t>(static_cast<std::int32_t>(DotBits) >> 1);
            float Seed;
            std::memcpy(&Seed, &SeedBits, sizeof(Seed));
            const double SeedSquared = static_cast<double>(Seed) * Seed;
            const double Correction = 1.5 - (SeedSquared * Dot) * 0.5;
            const volatile float Distance = static_cast<float>((Correction * Seed) * Dot);

            if (static_cast<double>(Distance) + Sphere[3] < NodeSphere[3])
            {
                Sphere[0] = NodeSphere[0];
                Sphere[1] = NodeSphere[1];
                Sphere[2] = NodeSphere[2];
                Sphere[3] = NodeSphere[3];
            }
            else if (!(static_cast<double>(NodeSphere[3]) + Distance < Sphere[3]))
            {
                const double NewRadius = ((static_cast<double>(NodeSphere[3]) + Sphere[3]) + Distance) * 0.5;
                // TEST AH,0x41 / JNP skips <= epsilon; unordered continues.
                if (!(static_cast<double>(Distance) <= 1e-6))
                {
                    const double Reciprocal = 1.0 / Distance;
                    const double NormalX = static_cast<double>(Dx) * Reciprocal;
                    const volatile float NormalY = static_cast<float>(Reciprocal * Dy);
                    const volatile float NormalZ = static_cast<float>(static_cast<double>(Dz) * Reciprocal);
                    const double Offset = NewRadius - Sphere[3];
                    const volatile float OffsetStored = static_cast<float>(Offset);
                    const double ShiftX = Offset * NormalX;
                    const double ShiftY = static_cast<double>(NormalY) * OffsetStored;
                    const volatile float ShiftZ = static_cast<float>(static_cast<double>(NormalZ) * OffsetStored);
                    Sphere[0] = static_cast<float>(ShiftX + Sphere[0]);
                    Sphere[1] = static_cast<float>(ShiftY + Sphere[1]);
                    Sphere[2] = static_cast<float>(static_cast<double>(ShiftZ) + Sphere[2]);
                }
                Sphere[3] = static_cast<float>(NewRadius);
            }
        }
        return SphereCount != 0 ? GR_OK : GR_GENERIC;
    }

    float FFxEffectObject::ComputeMaxSubtreeDuration(IDAComponent* FirstSibling)
    {
        float MaxDuration = 0.0f;
        for (IDAComponent* Sibling = FirstSibling; Sibling != nullptr;)
        {
            IGenericFx* Fx = AsFx(Sibling);

            float OwnDuration = 0.0f;
            Fx->GetSubtreeDuration(&OwnDuration);
            if (OwnDuration > MaxDuration)
                MaxDuration = OwnDuration;

            IDAComponent* Child = Fx->GetFirstChild();
            if (Child != nullptr)
            {
                const float SubtreeMax = ComputeMaxSubtreeDuration(Child);
                if (!(SubtreeMax < MaxDuration))
                    MaxDuration = ComputeMaxSubtreeDuration(Child);
            }

            Sibling = Fx->GetNextSibling();
        }
        return MaxDuration;
    }

    FFxEffectNodeRec* FFxEffectObject::NodeRecordsEnd() const
    {
        return NodeCount_ == 0 ? Nodes_ : Nodes_ + NodeCount_;
    }

    // 06220ef0: copy ranges retain captured cursors; container endpoints are
    // reloaded only where the listing reloads them after reference callbacks.
    void FFxEffectObject::InsertNodeRecords(unsigned long InsertIndex,
                                           unsigned long InsertCount,
                                           const FFxEffectNodeRec& Record)
    {
        FFxEffectNodeRec* const Position = InsertIndex == 0 ? Nodes_ : Nodes_ + InsertIndex;
        FFxEffectNodeRec* const EntryEnd = NodeRecordsEnd();
        if (NodeCapacity_ - NodeCount_ < InsertCount)
        {
            const unsigned long Growth = NodeCount_ > InsertCount ? NodeCount_ : InsertCount;
            const unsigned long Capacity = NodeCount_ + Growth;
            const unsigned long AllocationCount = static_cast<long>(Capacity) < 0 ? 0 : Capacity;
            FFxEffectNodeRec* Next = static_cast<FFxEffectNodeRec*>(
                ::operator new(sizeof(FFxEffectNodeRec) * AllocationCount));
            FFxEffectNodeRec* const NewPosition = CopyEffectRecords(Nodes_, Position, Next);
            ConstructEffectRecords(NewPosition, InsertCount, Record);
            CopyEffectRecords(Position, NodeRecordsEnd(), NewPosition + InsertCount);
            DestroyEffectRecords(Nodes_, NodeRecordsEnd());
            ::operator delete(Nodes_);
            NodeCapacity_ = Capacity;
            const unsigned long CurrentCount = Nodes_ != nullptr ? NodeCount_ : 0;
            Nodes_ = Next;
            NodeCount_ = CurrentCount + InsertCount;
            return;
        }

        if (EffectRecordDistance(Position, EntryEnd) < InsertCount)
        {
            CopyEffectRecords(Position, EntryEnd, Position + InsertCount);
            FFxEffectNodeRec* const ConstructAt = NodeRecordsEnd();
            ConstructEffectRecords(ConstructAt,
                InsertCount - EffectRecordDistance(Position, ConstructAt), Record);
            AssignEffectRecords(Position, NodeRecordsEnd(), Record);
        }
        else if (InsertCount != 0)
        {
            CopyEffectRecords(EntryEnd - InsertCount, EntryEnd, EntryEnd);
            FFxEffectNodeRec* const ShiftEnd = NodeRecordsEnd();
            CopyEffectRecordsBackward(Position, ShiftEnd - InsertCount, ShiftEnd);
            AssignEffectRecords(Position, Position + InsertCount, Record);
        }
        NodeCount_ += InsertCount;
    }

    // 06220ac0: single-element overload, including its own returned position.
    FFxEffectNodeRec* FFxEffectObject::InsertSingleNodeRecord(unsigned long InsertIndex,
                                                            const FFxEffectNodeRec& Record)
    {
        FFxEffectNodeRec* const Position = InsertIndex == 0 ? Nodes_ : Nodes_ + InsertIndex;
        FFxEffectNodeRec* const EntryEnd = NodeRecordsEnd();
        if (NodeCapacity_ - NodeCount_ < 1)
        {
            const unsigned long Growth = NodeCount_ > 1 ? NodeCount_ : 1;
            const unsigned long Capacity = NodeCount_ + Growth;
            const unsigned long AllocationCount = static_cast<long>(Capacity) < 0 ? 0 : Capacity;
            FFxEffectNodeRec* Next = static_cast<FFxEffectNodeRec*>(
                ::operator new(sizeof(FFxEffectNodeRec) * AllocationCount));
            FFxEffectNodeRec* const NewPosition = CopyEffectRecords(Nodes_, Position, Next);
            new (NewPosition) FFxEffectNodeRec(Record);
            CopyEffectRecords(Position, NodeRecordsEnd(), NewPosition + 1);
            DestroyEffectRecords(Nodes_, NodeRecordsEnd());
            ::operator delete(Nodes_);
            NodeCapacity_ = Capacity;
            const unsigned long CurrentCount = Nodes_ != nullptr ? NodeCount_ : 0;
            Nodes_ = Next;
            NodeCount_ = CurrentCount + 1;
            return InsertIndex == 0 ? Next : Next + InsertIndex;
        }
        if (EffectRecordDistance(Position, EntryEnd) < 1)
        {
            CopyEffectRecords(Position, EntryEnd, EntryEnd);
            FFxEffectNodeRec* const ConstructAt = NodeRecordsEnd();
            ConstructEffectRecords(ConstructAt,
                1 - EffectRecordDistance(Position, ConstructAt), Record);
            AssignEffectRecords(Position, NodeRecordsEnd(), Record);
        }
        else
        {
            CopyEffectRecords(EntryEnd - 1, EntryEnd, EntryEnd);
            FFxEffectNodeRec* const ShiftEnd = NodeRecordsEnd();
            CopyEffectRecordsBackward(Position, ShiftEnd - 1, ShiftEnd);
            AssignEffectRecords(Position, Position + 1, Record);
        }
        FFxEffectNodeRec* const ResultBase = Nodes_;
        ++NodeCount_;
        return InsertIndex == 0 ? ResultBase : ResultBase + InsertIndex;
    }

    GENRESULT FFxEffectObject::InstantiateNodesFromArchetype(
        IFxEffectArchetype* InitialArchetype, const FFxEffectCreateDesc* Desc)
    {
        const unsigned long RecCount = InitialArchetype->GetRec16Count();
        DestroyNodeRecords();
        if (NodeCapacity_ < RecCount)
        {
            const unsigned long AllocationCount = static_cast<long>(RecCount) < 0 ? 0 : RecCount;
            FFxEffectNodeRec* Next = static_cast<FFxEffectNodeRec*>(
                ::operator new(sizeof(FFxEffectNodeRec) * AllocationCount));
            CopyEffectRecords(Nodes_, NodeRecordsEnd(), Next);
            DestroyEffectRecords(Nodes_, NodeRecordsEnd());
            ::operator delete(Nodes_);
            NodeCapacity_ = RecCount;
            const unsigned long CurrentCount = Nodes_ != nullptr ? NodeCount_ : 0;
            Nodes_ = Next;
            NodeCount_ = CurrentCount;
        }
        unsigned long SortedRootCount = 0;
        unsigned long OverrideId = kNodeParentRoot;
        for (long Index = 0; Index < static_cast<long>(RecCount); ++Index)
        {
            FAleRec16 Rec = {};
            InitialArchetype->GetRec16(static_cast<unsigned long>(Index), &Rec);

            FFxTemporaryInterface Created;
            IDAComponent*& Node = Created.Value;
            if (Rec.A == 0)
            {
                // FUN_0621eed0 @ 0621eff7-000: after PUSH OutNode, [ESP+0x40]
                // aliases GetRec16 Out.B (hash), not Out.C. Out.C is the
                // 0x8000 / small tag used later for list-insert kind.
                // Rec.B = HashNameCrc(instance name) as inserted by
                // LoadNodeLibrary → InsertNodeByName(InsertName).
                const GENRESULT Lookup =
                    G_EffectNodeLibrary->NodeLibSlot11(Rec.B, &Node);
                if (Lookup < 0)
                {
                    FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                          "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxEffect.cpp",
                          0x113, "couldn't create instace of node\n");
                    return GR_GENERIC;
                }
            }
            else
            {
                AGGDESC Desc = {};
                Desc.size = 8;
                Desc.interface_name = kFxNodeFactory;
                if (DACOM_Acquire()->CreateInstance(&Desc, reinterpret_cast<void**>(&Node)) < 0)
                {
                    FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                          "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxEffect.cpp",
                          0x11d, "couldn't create instace of folder\n");
                    return GR_GENERIC;
                }
                if (Rec.B == 0xEE223B51u)
                    OverrideId = Rec.D;
            }

            const bool bRoot = Rec.C == kNodeParentRoot;
            unsigned long InsertIndex = NodeCount_;
            if (bRoot)
            {
                InsertIndex = 0;
                if (SortedRootCount != 0)
                {
                    const int SortKey = AsFx(Node)->GetSortKey();
                    while (InsertIndex < SortedRootCount &&
                           AsFx(Nodes_[InsertIndex].Node)->GetSortKey() <= SortKey)
                        ++InsertIndex;
                }
            }
            {
                FFxEffectNodeRec Inserted(Node, Rec.A, Rec.D, Rec.C);
                if (bRoot)
                    InsertNodeRecords(InsertIndex, 1, Inserted);
                else
                    InsertSingleNodeRecord(NodeCount_, Inserted);
            }
            if (bRoot)
                ++SortedRootCount;
        }
        TransformExtra_ = nullptr;
        if (OverrideId != kNodeParentRoot)
        {
            unsigned long Index = 0;
            while (Index < NodeCount_ && Nodes_[Index].Id != OverrideId)
                ++Index;
            TransformExtra_ = &Nodes_[Index];
        }
        FFxEffectNodeRec* const AttachedEnd = AttachAlchemyTree(Desc);
        BindRec8Associates(InitialArchetype);

        // FUN_0621eed0 tail 0621f517..0621f5d1: for every record whose kind
        // (record+4 = rec16 Rec.A, stored by the FUN_06220ef0 insert) is 1 --
        // the CreateNamedFx("FxNode") groups -- Max = 0, walk the child chain
        // (+0x2C first child, +0x34 next) taking the max of each child's
        // Duration (+0x68) and of FUN_0621ee50 over its own children, then
        // store it with +0x6C (SetNodeDuration, node+0x88). A group with no
        // children gets 0. Until 2026-09-26 the port ran this on every record
        // and +0x6C was a stub; now that +0x6C writes Duration, the kind gate
        // is what keeps leaf nodes at their own 100.0 default.
        FFxEffectNodeRec* DurationRecord = Nodes_;
        for (; DurationRecord != AttachedEnd; ++DurationRecord)
        {
            if (DurationRecord->Kind != 1)
                continue;
            IDAComponent* Child = AsFx(DurationRecord->Node)->GetFirstChild();
            const float MaxDuration = (Child != nullptr) ? ComputeMaxSubtreeDuration(Child) : 0.0f;
            AsFx(DurationRecord->Node)->SetNodeDuration(MaxDuration);
        }

        return GR_OK;
    }

    IDAComponent* FFxEffectObject::FindNodeById(unsigned long Id) const
    {
        const FFxEffectNodeRec* const End = NodeRecordsEnd();
        const FFxEffectNodeRec* Record = Nodes_;
        while (Record != End)
        {
            if (Record->Id == Id)
                break;
            ++Record;
        }
        return Record->Node;
    }

    FFxEffectNodeRec* FFxEffectObject::AttachAlchemyTree(const FFxEffectCreateDesc* Desc)
    {
        FFxEffectNodeRec* const End = NodeRecordsEnd();
        FFxEffectNodeRec* Record = Nodes_;
        RootNodeCount_ = 0;
        for (; Record != End; ++Record)
        {
            IDAComponent* Parent = Desc->RootParent;
            if (Record->Parent != kNodeParentRoot)
                Parent = FindNodeById(Record->Parent);
            else
                ++RootNodeCount_;
            G_EffectAlchemy->AttachNode(Parent, Record->Node);
        }
        return End;
    }

    void FFxEffectObject::BindRec8Associates(IFxEffectArchetype* InitialArchetype)
    {
        const unsigned long Rec8Count = InitialArchetype->GetRec8Count();
        for (long Index = 0; Index < static_cast<long>(Rec8Count); ++Index)
        {
            FAleRec8 Rec8 = {};
            InitialArchetype->GetRec8(static_cast<unsigned long>(Index), &Rec8);
            IDAComponent* NodeA = FindNodeById(Rec8.A);
            FFxTemporaryInterface Emitter;
            FFxTemporaryInterface Appearance;
            if (NodeA->QueryInterface(Iids::FxEmitter,
                                     reinterpret_cast<void**>(&Emitter.Value)) >= 0)
            {
                Appearance.Reset();
                IDAComponent* NodeB = FindNodeById(Rec8.B);
                NodeB->QueryInterface(Iids::FxAppearance,
                                      reinterpret_cast<void**>(&Appearance.Value));
                AsFx(Emitter.Value)->AssociateAppearance(Appearance.Value);
                continue;
            }
            Appearance.Reset();
            if (NodeA->QueryInterface(Iids::FxAppearance,
                                     reinterpret_cast<void**>(&Appearance.Value)) >= 0)
            {
                FFxTemporaryInterface Field;
                IDAComponent* NodeB = FindNodeById(Rec8.B);
                NodeB->QueryInterface(Iids::FxField, reinterpret_cast<void**>(&Field.Value));
                AsFx(Appearance.Value)->AssociateField(Field.Value);
                continue;
            }

            FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                  "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxEffect.cpp",
                  0x17b, "Unknown associate combination\n");
        }
    }

    void FFxEffectObject::ClearNodes()
    {
        DestroyNodeRecords();
        ::operator delete(Nodes_);
        Nodes_ = nullptr;
        NodeCapacity_ = 0;
    }

    void FFxEffectObject::DestroyNodeRecords()
    {
        const unsigned long Count = NodeCount_;
        FFxEffectNodeRec* Record = Nodes_;
        for (unsigned long Index = 0; Index < Count; ++Index, ++Record)
        {
            Record->~FFxEffectNodeRec();
        }
        NodeCount_ = 0;
    }

} // namespace AlchemyImpl
