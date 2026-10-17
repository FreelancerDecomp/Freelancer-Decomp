// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FUN_0634e0b0 -- IVP_Create_Exact_Mindists (the library's
// IVP_Mindist_Manager::create_exact_mindists) and its private helpers
// FUN_0634e7d0 / FUN_0634e7f0 / FUN_0634e820 / FUN_0634e860 / FUN_063505a0.
// cmn-ivp2b, 2026-10-08. See ivp_mindist_create.h.

#include "ivp_mindist_create.h"

#include "ivp_collision.h"                            // IVP_Collision, IVP_Collision_Delegator
#include "../ivp_compact_builder/ivp_compact_ledge.h" // IVP_Compact_Ledge
#include "../ivp_physics/ivp_mindist.h"               // IVP_Ledge_Mindist, IVP_Complex_Mindist
#include "../ivp_physics/ivp_real_object.h"           // IVP_Real_Object, IVP_TransformWorldToSlotSpace
#include "../ivp_physics/ivp_surface_manager.h"       // IVP_SurfaceManager, IVP_SurfaceManager_OutList
#include "../ivp_physics/ivp_environment.h"           // IVP_Environment
#include "../ivp_physics/ivp_core.h"                  // IVP_Core

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "../ivp_utility/ivp_u_bigvector.h" // IVP_U_BigVector_Grow (FUN_0635d3e0): grow a
                                            // {capacity, count, elems, inline buffer @+0xc}
                                            // list to 2*capacity+1

namespace
{
    // The two ledge lists FUN_0634e0b0 keeps on its stack: a 32-bit
    // {capacity, count, elems} header (FUN_06350540: elems = buffer, capacity =
    // 0x100, count = 0) immediately followed by its 0x100-entry inline buffer,
    // which is what the grow helper's `elems != this+0xc` test relies on.
    struct FLedgeList
    {
        IVP_SurfaceManager_OutList List;
        void* InlineBuffer[0x100];

        FLedgeList()
        {
            List.Elems = InlineBuffer; // FUN_06350540 :0635054a
            List.Capacity = 0x100;     // :0635054d
            List.Count = 0;            // :0635054f
        }

        // :0634e748-0634e785 (object 1's list) / :0634e787-0634e7b5 (object
        // 0's list): free the buffer unless it is the inline one or null.
        ~FLedgeList()
        {
            if (List.Elems != InlineBuffer && List.Elems != nullptr)
                std::free(List.Elems); // FUN_06343fb0
        }

        // FUN_06350590 + the store at :0634e21c-0634e248, and the inlined
        // copy at :0634e340-0634e375: grow when Count >= Capacity, then append.
        void Add(const IVP_Compact_Ledge* Ledge)
        {
            if (List.Count >= List.Capacity)
                IVP_U_BigVector_Grow(&List); // FUN_0635d3e0
            List.Elems[List.Count] = const_cast<IVP_Compact_Ledge*>(Ledge);
            ++List.Count;
        }

        const IVP_Compact_Ledge* At(int32_t Index) const
        {
            return static_cast<const IVP_Compact_Ledge*>(List.Elems[Index]);
        }
    };
    static_assert(offsetof(FLedgeList, InlineBuffer) == 0x0c, "inline buffer at list+0xc (FUN_0635d3e0's test)");

    // The local IVP_U_Vector with a 0x80-entry inline buffer right after its
    // header (FUN_06347dd0(this, buffer, 0x80) at :0634e45f); its destructor
    // is the vector's inline-buffer-aware clear (:0634e6f8-0634e726).
    struct FNewMindistVector
    {
        IVP_U_Vector<IVP_Collision> Vector;
        IVP_Collision* InlineBuffer[0x80];

        FNewMindistVector()
            : Vector(InlineBuffer, 0x80)
        {
        }
    };

    // The pair key FUN_0634e0b0 builds at its local +0x48 and hands to
    // FUN_0634e7f0 / FUN_0634e860. Only the two ledges are read.
    struct FMindistPairKey
    {
        IVP_Real_Object* Object0;        // +0x00
        IVP_Real_Object* Object1;        // +0x04
        const IVP_Compact_Ledge* Ledge0; // +0x08
        const IVP_Compact_Ledge* Ledge1; // +0x0c
    };

    // The mix both hashes share:
    // `ECX = (L1 * 75) ^ L0; EDX = ECX SAR 8; EAX = (EDX << 10) - EDX; EAX += ECX`.
    uint32_t HashLedgePair(const IVP_Compact_Ledge* Ledge0, const IVP_Compact_Ledge* Ledge1)
    {
        const uint32_t Mixed = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Ledge1)) * 75u)
                               ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Ledge0));
        const int32_t Shifted = static_cast<int32_t>(Mixed) >> 8;
        return static_cast<uint32_t>(Shifted) * 0x3ffu + Mixed;
    }

    // FUN_0634e7f0 (.text:0634e7f0-0634e811, cdecl): hash of a pair key.
    uint32_t HashPairKey(const FMindistPairKey* Key)
    {
        return HashLedgePair(Key->Ledge0, Key->Ledge1);
    }

    // FUN_0634e820 (.text:0634e820-0634e855, cdecl): hash of a collision's
    // own ledge pair (slot 2, get_ledges). The second stack argument the
    // caller pushes (0) is never read.
    uint32_t HashCollision(IVP_Collision* Collision)
    {
        const IVP_Compact_Ledge* Ledges[2];
        Collision->get_ledges(Ledges); // :0634e82e
        return HashLedgePair(Ledges[0], Ledges[1]);
    }

    // FUN_0634e860 (.text:0634e860-0634e895, cdecl): true when the
    // collision's ledges are exactly the key's (first, then second).
    bool CollisionMatchesKey(IVP_Collision* Collision, const FMindistPairKey* Key)
    {
        const IVP_Compact_Ledge* Ledges[2];
        Collision->get_ledges(Ledges);     // :0634e86e
        return Ledges[0] == Key->Ledge0 && // :0634e879
               Ledges[1] == Key->Ledge1;   // :0634e882
    }

    // FUN_0634e7d0 (.text:0634e7d0-0634e7ea, thiscall on the collision,
    // RET 8): replace the back-index that equals OldIndex (+0x0c first, else
    // +0x10 unconditionally).
    void SetFVectorIndex(IVP_Collision* Collision, int32_t OldIndex, int32_t NewIndex)
    {
        if (Collision->IndexInElement0 == OldIndex)
            Collision->IndexInElement0 = NewIndex;
        else
            Collision->IndexInElement1 = NewIndex;
    }

    // FUN_0628be60 (Dst = A + B * Scale) as the x87 evaluates it: per
    // component FLD Scale / FMUL B / FADD A / FSTP float.
    void AddMultiple(IVP_U_Point* Out, const Vector& A, const Vector& B, float Scale)
    {
        Out->x = (float)((double)(Scale) * B.x + A.x);
        Out->y = (float)((double)(Scale) * B.y + A.y);
        Out->z = (float)((double)(Scale) * B.z + A.z);
    }

    // One side's ledge collection: .text:0634e13c-0634e21a for object 0
    // (Object = EBP = obj0, Other = ESI = obj1) and :0634e257-0634e337 for
    // object 1 (Object = ESI = obj1, Other = EBP = obj0). The search centre is
    // the OTHER object's core position extrapolated to now, taken into this
    // object's space.
    void CollectLedgesWithinRadius(IVP_Real_Object* Object, IVP_Real_Object* Other, IVP_Environment* Environment,
                                   float ScanRadius, const IVP_Compact_Ledge* RootLedge,
                                   const IVP_Compact_Ledge* OtherSingleLedge, FLedgeList& Result)
    {
        IVP_Core* OtherCore = Other->core; // :0634e142 / :0634e25d (+0x98)

        // FLD env+0xf8; FSUB core+0x68; FADD env+0xfc; FSUB core+0x6c; FSTP float.
        const float DeltaTime = (float)(
            (((double)(Environment->current_time.time_base) - OtherCore->TimeOfLastPsi.time_base)
             + Environment->current_time.time_offset) - OtherCore->TimeOfLastPsi.time_offset);

        // FUN_0628be60(&centre, core+0xb4, core+0xc4, DeltaTime).
        IVP_U_Point CentreWorld;
        AddMultiple(&CentreWorld, OtherCore->PosWorldFCore, OtherCore->DeltaWorldFCore, DeltaTime);

        // FLD obj+0x94; FADD othercore+0x04; FADD ScanRadius; FSTP float.
        const float SearchRadius = (float)(
            (double)(Object->field_94) + OtherCore->UpperLimitRadius + ScanRadius);

        // get_cache_object_no_lock (inlined), then FUN_06348470 on cache+0x1c.
        IVP_NarrowPhaseCacheSlot* Cache = Object->get_cache_object_no_lock();
        IVP_U_Point CentreObject;
        IVP_TransformWorldToSlotSpace(Cache, &CentreWorld, &CentreObject);

        // Surface manager slot 4 (RET 0x18): (centre in object space, radius,
        // root ledge, other object = 0, the other side's single ledge, result).
        Object->surface_manager->get_all_ledges_within_radius(
            &CentreObject.x, SearchRadius,
            const_cast<IVP_Compact_Ledge*>(RootLedge), nullptr,
            const_cast<IVP_Compact_Ledge*>(OtherSingleLedge), &Result.List);
    }
}

// FUN_06350560 (.text:06350560-06350580).
FCollisionIndexTable::~FCollisionIndexTable()
{
    if (bHeap != 0 && Table != nullptr) // :06350563-:0635056e
    {
        std::free(Table);               // :06350571 FUN_06343fb0
        Table = nullptr;                // :06350579
    }
}

// FUN_063505a0 (.text:063505a0-063505de, RET 8).
void IVP_SwapCollisionSlots(IVP_U_Vector<IVP_Collision>* Vector, int32_t IndexA, int32_t IndexB)
{
    IVP_Collision* A = Vector->elems[IndexA]; // :063505a9
    IVP_Collision* B = Vector->elems[IndexB]; // :063505b1
    Vector->elems[IndexA] = B;                // :063505b4
    Vector->elems[IndexB] = A;                // :063505ba

    if (A->IndexInElement0 == IndexA)  // :063505bd
        A->IndexInElement0 = IndexB;   // :063505c2
    else
        A->IndexInElement1 = IndexB;   // :063505c7

    if (B->IndexInElement0 == IndexB)  // :063505ca
        B->IndexInElement0 = IndexA;   // :063505d0
    else
        B->IndexInElement1 = IndexA;   // :063505d9
}

// FUN_0634e0b0 (.text:0634e0b0-0634e7cc).
void IVP_Create_Exact_Mindists(IVP_Real_Object* Object0, IVP_Real_Object* Object1, float ScanRadius,
                               IVP_U_Vector<IVP_Collision>* Mindists,
                               const IVP_Compact_Ledge* SingleLedge0, const IVP_Compact_Ledge* SingleLedge1,
                               const IVP_Compact_Ledge* RootLedge0, const IVP_Compact_Ledge* RootLedge1,
                               IVP_Collision_Delegator* Delegator)
{
    IVP_Environment* Environment = Object0->environment; // :0634e0da

    FLedgeList Ledges0; // local +0x66c: object 0's ledges (FUN_06350540, :0634e0f5)
    FLedgeList Ledges1; // local +0x260: object 1's ledges (FUN_06350540, :0634e119)

    // :0634e11e-0634e248 -- object 0's side.
    if (SingleLedge0 == nullptr)
        CollectLedgesWithinRadius(Object0, Object1, Environment, ScanRadius, RootLedge0, SingleLedge1, Ledges0);
    else
        Ledges0.Add(SingleLedge0);

    // :0634e24f-0634e375 -- object 1's side.
    if (SingleLedge1 == nullptr)
        CollectLedgesWithinRadius(Object1, Object0, Environment, ScanRadius, RootLedge1, SingleLedge0, Ledges1);
    else
        Ledges1.Add(SingleLedge1);

    // :0634e37c-0634e3e4 -- size the open-addressing table of int16 indices.
    // Needed = (n1 * n0 + old) * 2 + 2. The 0x400-entry stack table is used,
    // halved while still above Needed, when that size exceeds the old count;
    // otherwise a heap table is allocated, doubled until above twice the old
    // count.
    const int32_t OldCount = static_cast<int32_t>(Mindists->n_elems);
    const int32_t Needed = (Ledges1.List.Count * Ledges0.List.Count + OldCount) * 2 + 2;
    int32_t TableSize = 0x400;
    if (Needed < TableSize)           // :0634e39c `CMP ECX,EBX; JGE`
    {
        do
        {
            TableSize >>= 1;          // :0634e3a0 SAR
        } while (TableSize > Needed); // :0634e3a2-:0634e3a4 `JG`
    }

    int16_t InlineTable[0x400]; // local +0xa78
    FCollisionIndexTable Hash;  // local +0x2c / +0x30 / +0x34
    if (TableSize > OldCount)   // :0634e3a6-:0634e3a8 `JG`
    {
        Hash.Mask = TableSize - 1; // :0634e3d7/:0634e3e0
        Hash.bHeap = 0;            // :0634e3d8
        Hash.Table = InlineTable;  // :0634e3e4/:0634e3ef
    }
    else
    {
        const int32_t Twice = OldCount * 2; // :0634e3aa
        Hash.bHeap = 1;                     // :0634e3ae
        int32_t HeapSize = TableSize;       // :0634e3b6
        if (!(TableSize > Twice))           // :0634e3ac/:0634e3b8 `JG` skips the loop
        {
            do
            {
                HeapSize <<= 1;             // :0634e3ba
            } while (HeapSize <= Twice);    // :0634e3bc-:0634e3be `JLE`
        }
        Hash.Mask = HeapSize - 1;                                                           // :0634e3c3/:0634e3c7
        Hash.Table = static_cast<int16_t*>(std::malloc(static_cast<size_t>(HeapSize) * 2)); // :0634e3cb FUN_06343f70
    }
    int16_t* const Table = Hash.Table;
    const int32_t Mask = Hash.Mask;

    // :0634e3eb-0634e410 -- every entry := -1, written as (Mask >> 1) + 1
    // dwords from the top down.
    for (int32_t Dword = Mask >> 1; Dword >= 0; --Dword)
        reinterpret_cast<int32_t*>(Table)[Dword] = -1;

    // :0634e412-0634e447 -- file every existing record under its own hash,
    // from the top down, with linear probing.
    for (int32_t Index = OldCount - 1; Index >= 0; --Index)
    {
        uint32_t Slot = HashCollision(Mindists->elems[Index]) & static_cast<uint32_t>(Mask); // :0634e41b-:0634e429
        while (Table[Slot] != -1)                                                             // :0634e42e
            Slot = (Slot + 1) & static_cast<uint32_t>(Mask);                                  // :0634e436-:0634e437
        Table[Slot] = static_cast<int16_t>(Index);                                            // :0634e441
    }

    int32_t Kept = 0; // local +0x3c (:0634e3f3): records [0, Kept) are wanted again
    {
        FNewMindistVector NewMindists; // local +0x58 (:0634e45f)

        FMindistPairKey Key;                    // local +0x48
        IVP_Collision* LastCandidate = nullptr; // local +0x14: written by every probe (:0634e50c)

        for (int32_t I = Ledges0.List.Count - 1; I >= 0; --I) // :0634e464-:0634e47c, :0634e67b-:0634e67f
        {
            const IVP_Compact_Ledge* Ledge0 = Ledges0.At(I); // :0634e488-:0634e48f
            Key.Ledge0 = Ledge0;                               // :0634e49d
            Key.Object0 = Object0;                             // :0634e4ab

            for (int32_t J = Ledges1.List.Count - 1; J >= 0; --J) // :0634e4a1-:0634e4b3, :0634e671-:0634e675
            {
                const IVP_Compact_Ledge* Ledge1 = Ledges1.At(J); // :0634e4b9-:0634e4c4
                Key.Ledge1 = Ledge1;                               // :0634e4cb
                Key.Object1 = Object1;                             // :0634e4db

                uint32_t Slot = HashPairKey(&Key) & static_cast<uint32_t>(Mask); // :0634e4df-:0634e4e6
                int32_t Found = Table[Slot];                                       // :0634e4e8 MOVSX
                bool bCreate = true;
                if (Found != -1)                                                   // :0634e4f0
                {
                    bool bMatched = false;
                    for (;;)
                    {
                        LastCandidate = Mindists->elems[Found];                    // :0634e4f9-:0634e50c
                        if (CollisionMatchesKey(LastCandidate, &Key))              // :0634e510-:0634e51a
                        {
                            bMatched = true;
                            break;
                        }
                        Slot = (Slot + 1) & static_cast<uint32_t>(Mask);           // :0634e51c-:0634e51d
                        Found = Table[Slot];                                       // :0634e51f
                        if (Found == -1)                                           // :0634e524-:0634e529
                            break;
                    }

                    if (bMatched)
                    {
                        // :0634e52b-0634e57f -- move the matched record down to
                        // index Kept: re-point the table entry of the record
                        // now at Kept, then swap the two records.
                        if (Found > Kept) // `JLE` skips
                        {
                            uint32_t KeptSlot = HashCollision(Mindists->elems[Kept]) & static_cast<uint32_t>(Mask); // :0634e531-:0634e54e
                            while (static_cast<int32_t>(Table[KeptSlot]) != Kept)                                    // :0634e550-:0634e566
                                KeptSlot = (KeptSlot + 1) & static_cast<uint32_t>(Mask);
                            Table[KeptSlot] = static_cast<int16_t>(Found); // :0634e56f
                            Table[Slot] = static_cast<int16_t>(Kept);      // :0634e57a
                            IVP_SwapCollisionSlots(Mindists, Kept, Found); // :0634e57f
                        }
                        ++Kept; // :0634e584-:0634e58f
                        // :0634e588/:0634e58d/:0634e593 -- `TEST EAX,EAX; JNZ`
                        // on the candidate the probe read: a live record is
                        // kept and no new mindist is made for this pair.
                        bCreate = (LastCandidate == nullptr);
                    }
                }

                if (!bCreate)
                    continue;

                // LAB_0634e599 -- a new exact mindist for this ledge pair.
                // `TEST byte [ledge+8],3` on both: if either is set, the
                // recursive variant (0x94), else the plain one (0x80); both
                // get (object 0's environment, Delegator).
                IVP_Ledge_Mindist* Mindist;
                if ((Ledge0->SizeAndFlags & 3) == 0 &&
                    (Ledge1->SizeAndFlags & 3) == 0)
                    Mindist = new IVP_Ledge_Mindist(Environment, Delegator);   // :0634e5ad-:0634e5d9 FUN_0634dd10
                else
                    Mindist = new IVP_Complex_Mindist(Environment, Delegator); // :0634e5e0-:0634e60c FUN_06362ab0

                // :0634e615-:0634e641 -- FUN_0634def0(obj0, obj1, ledge0+0x14,
                // ledge1+0x14): each ledge's first triangle's first edge.
                Mindist->init_mindist(Object0, Object1,
                                      const_cast<IVP_Compact_Ledge*>(Ledge0)->GetFirstEdge(),
                                      const_cast<IVP_Compact_Ledge*>(Ledge1)->GetFirstEdge());

                NewMindists.Vector.Add(Mindist); // :0634e646-:0634e66c (FUN_063441a0 when full)
            }
        }

        // :0634e68c-0634e6af -- delete every old record that was not wanted
        // again, from the top down (slot 4 with 1, null entries skipped). Each
        // deletion reports to its delegator, which takes it out of Mindists.
        for (int32_t Index = static_cast<int32_t>(Mindists->n_elems) - 1; Index >= Kept; --Index)
        {
            IVP_Collision* Old = Mindists->elems[Index]; // :0634e69b-:0634e69e (elems re-read)
            if (Old != nullptr)
                delete Old;                              // :0634e6a5-:0634e6a9
        }

        // :0634e6b1-0634e6f6 -- append the new records from the top down;
        // each gets its back-index -1 -> its slot (FUN_0634e7d0).
        for (int32_t Index = static_cast<int32_t>(NewMindists.Vector.n_elems) - 1; Index >= 0; --Index)
        {
            IVP_Collision* New = NewMindists.Vector.elems[Index]; // :0634e6c5-:0634e6c9
            const int32_t NewSlot = static_cast<int32_t>(Mindists->n_elems);
            Mindists->Add(New);                                   // :0634e6be-:0634e6e0, :0634e6f1 (FUN_063441a0 when full)
            SetFVectorIndex(New, -1, NewSlot);                    // :0634e6e3-:0634e6ec
        }
    } // :0634e6f8-0634e726 -- NewMindists' inline-buffer-aware clear

    // Hash's destructor (:0634e728-0634e745, the inlined FUN_06350560), then
    // Ledges1 / Ledges0 (:0634e748-0634e7b5).
}
