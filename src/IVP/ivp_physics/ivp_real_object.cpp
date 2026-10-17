// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_real_object.h"
#include "ivp_core.h"
#include "ivp_environment.h"
#include "ivp_mindist.h" // IVP_Synapse_Real / IVP_Ledge_Mindist (synapse list teardown)
#include "ivp_phantom.h" // IVP_Controller_Phantom (first_child), cmn-ivp3d
#include "ivp_ov_element.h" // IVP_Mindist_Manager / IVP_OV_Element (enable_collision_detection), cmn-ivp3d
#include "ivp_calc_next_psi_solver.h" // cmn-phy6 block: set_transformation / calc_next_PSI_matrix
#include "ivp_simulation_unit.h"      // cmn-phy6 block: disable_simulation
#include "ivp_controller.h"           // cmn-phy6 block: IVP_Event_Sim
#include "../../include/common_physys.h" // cmn-phy6 block: PhySys::BeamBodyToWorldPose (06343cc0)
#include "ivp_cluster_manager.h"  // IVP_Cluster_Manager (add_object / remove_object / fire_event_*)
#include <new>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cassert>
#include "../ivp_utility/ivp_core_trap.h"   // IVP_CoreTrap (retail's `MOV dword ptr [0],0`)
#include "../ivp_utility/ivp_malloc.h"      // ivp_malloc_aligned / ivp_free_aligned (06343fc0 / 06343ff0)
#include "../ivp_utility/ivp_u_bigvector.h" // IVP_U_BigVector_Grow (0635d3e0)

// Forward declaration

void IVP_Core_Remove_Object(IVP_Core* Core, IVP_Real_Object* Object); // ivp_core.cpp, decomp-tracker item 2852

namespace
{
    // Anchor teardown notification; the unused outer argument is retained.
    void FUN_06356910(IVP_Anchor* Anchor, IVP_Real_Object* /*Outer, unused*/)
    {
        Anchor->l_actuator->AnchorWillBeDeleted(Anchor);
    }

    // Reinterprets raw IEEE-754 bits as a float without UB / strict-aliasing
    // risk (memcpy is the standard-sanctioned way pre-C++20 std::bit_cast).
    // Matches the convention already established in
    // ivp_mindist_limiter.cpp/ivp_surface_builder_ledge_soup.cpp's own
    // BitsToFloat.
    inline float BitsToFloat(uint32_t Bits)
    {
        float Result;
        std::memcpy(&Result, &Bits, sizeof(Result));
        return Result;
    }
}

// ============================================================================
// IVP_Hull_Manager (decomp-tracker item 3166). See the class comment above
// its declaration in ivp_real_object.h for the full outer-class-resolution
// and pool-layout evidence trail.
// ============================================================================

// ----------------------------------------------------------------------------
// FUN_06362ef0 (Common.dll.txt:06362ef0-06362f63) -- the shared pool ctor.
// Called on `IVP_Hull_Manager::this+0x20` with capacity 8 (Common.dll.txt:
// 0635217e) and on the standalone `operator new(0x14)` pool with capacity 0x10
// (:063608fb) inside IVP_SimUnitManager_Construct. Body extracted here
// 2026-09-17 (lane claude:w26-simunit) from IVP_Hull_Manager's ctor, unchanged.
// ----------------------------------------------------------------------------
void IVP_Priority_Pool::Init(uint16_t InitialCapacity)
{
    PoolCapacity = InitialCapacity;
    PoolEntries = static_cast<Entry*>(std::malloc(sizeof(Entry) * PoolCapacity));
    PoolCount = 0;    // Common.dll.txt:06362f06 (`MOV word[ESI+0x10],0`, done early, before the alloc).
    PoolFreeHead = 0; // Common.dll.txt:06362f1a (`MOV word[ESI+2],0`) -- set unconditionally, even for a hypothetical zero-capacity pool; matches retail exactly.
    // Thread the free list through every slot's Next field: :06362f17-:06362f38
    // writes Next = I + 1 for every I < Capacity (no null test on the
    // allocation), then :06362f3a-:06362f46 overwrites the last slot's Next
    // with 0xffff (`[Entries + Capacity*0x10 - 0xc]`). Both callers pass a
    // non-zero capacity (8 at :06352179, 0x10 at :063608fb).
    for (uint32_t I = 0; I < PoolCapacity; ++I)
        PoolEntries[I].Next = static_cast<uint16_t>(I + 1);
    PoolEntries[PoolCapacity - 1].Next = 0xffff;
    PoolKeyThreshold = BitsToFloat(0x501502f9); // Common.dll.txt:06362f59 -- literal retail bit pattern, purpose unconfirmed.
    PoolLiveIndex = 0xffff;  // Common.dll.txt:06362f4d (`MOV word [ESI+0xe],0xffff`)
    PoolSortedHead = 0xffff; // Common.dll.txt:06362f53 (`MOV word [ESI+0xc],0xffff`)
}

// FUN_06362f70 -- frees Entries if non-null.
void IVP_Priority_Pool::Destroy()
{
    if (PoolEntries != nullptr)
    {
        std::free(PoolEntries);
        PoolEntries = nullptr;
    }
}

IVP_Hull_Manager::IVP_Hull_Manager()
{
    // FUN_06352150 (Common.dll.txt:06352150-063521a7). The eight dwords at
    // +0x00..+0x1c (LastEditTimeHi/Lo, Outer/InnerGradient, Outer/InnerValue-
    // AtLastEdit, AccumulatedDeltaA, TimeOfNextReset)
    // are zeroed via the default member initializers above (matching
    // retail's `this[0x00..0x1c] = 0`, Common.dll.txt:06352170-06352196).
    // Then FUN_06362ef0 on `this+0x20` with capacity 8 (Common.dll.txt:0635217e).
    Pool().Init(static_cast<uint16_t>(8));
}

IVP_Hull_Manager::~IVP_Hull_Manager()
{
    // FUN_063521b0 (Common.dll.txt:063521b0-063521e8), which calls
    // FUN_06362f70 (the shared pool dtor) on `this+0x20`.
    Pool().Destroy();
}

void IVP_Hull_Manager::WalkActiveEntries()
{
    // FUN_063521f0 (Common.dll.txt:063521f0-06352218). See the method's own
    // header comment for the full behavior writeup, including the
    // "listener must remove itself or this spins forever" caveat.
    // .text:063521f3-06352215: `CMP word [+0x30],0; JBE` / loop `JA`; the
    // payload of the minimum entry gets slot 2 with this manager. Retail has
    // no null test on the entries pointer here (the guard an earlier pass
    // added was copied from FUN_06352220 and is removed, cmn-phy5 2026-10-08).
    while (PoolCount > 0)
    {
        IVP_Listener_Hull* Listener = static_cast<IVP_Listener_Hull*>(PoolEntries[PoolLiveIndex].Payload);
        Listener->hull_manager_is_going_to_be_deleted_event(this);
    }
}

void IVP_Hull_Manager::DrainAndShift()
{
    // FUN_06352220 (Common.dll.txt:06352220-0635229c). See the
    // OuterValueAtLastEdit/InnerValueAtLastEdit/AccumulatedDeltaA field
    // comments in the header for the full per-entry and tail-flush behavior.
    // This is a REBASE of the whole hull-value coordinate system back to zero
    // (the two values it reads used to be called PendingDeltaA/PendingDeltaB
    // on the strength of this function alone -- renamed 2026-09-17, lane
    // claude:common-ivp5, once FUN_06350050 and IVP_InitCoreRestStateAndDamping
    // showed both are absolute values the hull extrapolation reads back).
    const float NegDeltaA = -OuterValueAtLastEdit;
    const float NegDeltaB = -InnerValueAtLastEdit;

    uint16_t Index = PoolLiveIndex;
    while (Index != 0xffff)
    {
        // Retail's `TEST EAX,EAX; JZ` on &Entries[Index] (:0635225b/:06352261)
        // follows the read of its +4 (:0635225d), so it can never stop the
        // loop with a null pointer; not reproduced (cmn-phy5 2026-10-08).
        Entry& E = PoolEntries[Index];
        void* Payload = E.Payload;
        const uint16_t NextIndex = E.Next; // fetched before mutating E.Key, matching retail's own instruction order (Common.dll.txt:0635225d precedes 0635226a).

        E.Key += NegDeltaA; // Common.dll.txt:0635226a-0635226f.

        // :0635226d-:06352274 -- `PUSH EBP (NegDeltaB); PUSH EBX (NegDeltaA);
        // CALL [EAX+0xc]`: NegDeltaA is the first argument (pushed last).
        static_cast<IVP_Listener_Hull*>(Payload)->hull_manager_is_reset(NegDeltaA, NegDeltaB);

        Index = NextIndex;
    }

    // Tail (Common.dll.txt:06352279-06352292): reset the pending pair, fold
    // NegDeltaA into PoolKeyThreshold and AccumulatedDeltaA.
    PoolKeyThreshold += NegDeltaA;
    OuterValueAtLastEdit = 0.0f;
    InnerValueAtLastEdit = 0.0f;
    AccumulatedDeltaA += NegDeltaA;
}

// ----------------------------------------------------------------------------
// FUN_06362f90 (decomp-tracker item 3506, Common.dll.txt:06362f90-0636327b,
// 247 insns). Moved 2026-09-17 (lane claude:w26-simunit) from
// IVP_Hull_Manager::PoolInsert onto the extracted IVP_Priority_Pool, with the
// body unchanged; IVP_Hull_Manager::PoolInsert is now a one-line forwarder.
// Byte-faithful port, 2026-09-17, lane claude:w18-ivp -- the
// address range verified instruction by instruction is 0x06362f90-0x0636327b
// in full, plus the ctor (FUN_06362ef0, :06362ef0-06362f63) for the free-list
// invariant this relies on.
//
// The pool keeps every live entry on ONE ascending-by-Key intrusive doubly
// linked list (Entry::Next/Prev, head = PoolLiveIndex, so the minimum key is
// always O(1)), and additionally promotes occasional entries onto a second
// "express" list (Entry::ExpressNext/ExpressPrev, head = PoolSortedHead) that
// the insert walk uses as a skip list. PoolKeyThreshold caches the head's key.
// ----------------------------------------------------------------------------
uint16_t IVP_Priority_Pool::Insert(void* Payload, float Key)
{
    ++PoolCount; // Common.dll.txt:06362f98 -- retail bumps the count first, before it even knows whether it has to grow.

    uint32_t NewIndex = 0;
    Entry* NewEntry = nullptr;

    if (PoolFreeHead != 0xffff) // :06362fa2
    {
        // Pop the head of the free chain (:06362fa9-:06362fc2).
        NewIndex = PoolFreeHead;
        NewEntry = &PoolEntries[NewIndex];
        PoolFreeHead = NewEntry->Next;
    }
    else
    {
        // Grow (:06362fcb-:06363085). Note retail's asymmetry, reproduced
        // exactly: it ALLOCATES (Capacity+1)*2 entries but PUBLISHES a
        // capacity of only Capacity*2+1, so the buffer always has one spare
        // entry past the declared capacity.
        const uint16_t OldCapacity = PoolCapacity;
        Entry* NewEntries = static_cast<Entry*>(std::malloc((static_cast<size_t>(OldCapacity) + 1u) * 32u)); // :06362fcd-:06362fd5

        for (uint16_t I = 0; I < OldCapacity; ++I)
            NewEntries[I] = PoolEntries[I]; // :06362fe8-:06363013 (retail copies the four raw dwords per entry).

        PoolCapacity = static_cast<uint16_t>(OldCapacity * 2 + 1); // :0636301d-:06363023
        if (PoolEntries != nullptr)                                // :06363021/:06363026
        {
            std::free(PoolEntries); // :06363029
            PoolEntries = nullptr;  // :06363031
        }

        NewIndex = OldCapacity;               // :0636303d/:06363044 -- the first brand-new slot is the one handed out.
        NewEntry = &NewEntries[OldCapacity];  // :0636303f-:06363042
        PoolEntries = NewEntries;             // :06363049
        PoolFreeHead = static_cast<uint16_t>(OldCapacity + 1); // :0636304e

        for (uint32_t I = static_cast<uint32_t>(OldCapacity) + 1u; I < static_cast<uint32_t>(PoolCapacity); ++I)
            NewEntries[I].Next = static_cast<uint16_t>(I + 1); // :06363059-:0636306f

        // :06363080 -- `word[Capacity*0x10 + Entries - 0xc] = 0xffff`, i.e. the
        // LAST declared slot terminates the free chain. Unconditional in
        // retail, even in the degenerate OldCapacity==0 case where it lands on
        // the entry just handed out.
        NewEntries[PoolCapacity - 1].Next = 0xffff;
    }

    // :06363087-:06363099 -- fill the entry. ExpressNext is seeded with the
    // literal 0xfffe "not in the express list" tag.
    NewEntry->Key = Key;
    NewEntry->Payload = Payload;
    NewEntry->ExpressNext = 0xfffe;

    // :0636309e `FCOMP [ESI+8]` / :063630a3 `TEST AH,0x41` / :063630a6 `JZ`.
    // C3(0x40)=equal, C0(0x01)=st0<mem, so (AH & 0x41) == 0 means strictly
    // Key > PoolKeyThreshold and the JZ goes to the sorted-walk path; the
    // fall-through (taken for Key <= threshold AND for unordered/NaN) makes
    // the new entry the new list head.
    if (!(Key > PoolKeyThreshold))
    {
        PoolKeyThreshold = Key;         // :063630b0
        NewEntry->Next = PoolLiveIndex; // :063630b3
        if (PoolLiveIndex != 0xffff)    // :063630bd
            PoolEntries[PoolLiveIndex].Prev = static_cast<uint16_t>(NewIndex); // :063630ce
        PoolLiveIndex = static_cast<uint16_t>(NewIndex); // :063630d4
        NewEntry->Prev = 0xffff;                        // :063630da
        return static_cast<uint16_t>(NewIndex);         // :063630e0
    }

    // ---- sorted-insert path (:063630e9) ------------------------------------
    // First hop along the express list for as long as its keys are still
    // strictly below the new key, remembering the last express node reached
    // ("Anchor"). StepBudget starts at 3 and grows by one per express hop; it
    // is the number of ascending-list hops that are considered "cheap enough"
    // not to warrant promoting a new express node at the end.
    uint32_t ExpressCur = PoolSortedHead; // :063630eb
    uint32_t Anchor = PoolLiveIndex;      // :063630f1
    int32_t StepBudget = 3;               // :063630f5

    while (ExpressCur != 0xffff) // :063630fd / :0636312f
    {
        // :0636310d `FLD Entries[cur].Key` / :06363113 `FCOMP Key` /
        // :06363119 `TEST AH,1` / :0636311c `JZ` -- C0 alone, so the loop
        // continues while Entries[cur].Key < Key or the pair is unordered.
        if (PoolEntries[ExpressCur].Key >= Key)
            break;
        ++StepBudget;  // :0636311e
        Anchor = ExpressCur;                               // :06363122
        ExpressCur = PoolEntries[ExpressCur].ExpressNext;  // :06363126
    }

    // Then walk the ascending list from Anchor (:06363131-:0636318a).
    uint32_t Prev = Anchor;                     // local_8, :06363145
    uint32_t Cur = PoolEntries[Anchor].Next;    // :06363141
    int32_t Hops = 0;                           // local_c, :06363149
    bool FoundPosition = false;

    while (Cur != 0xffff) // :06363151 / :0636318a
    {
        ++Hops; // :0636316c -- retail increments BEFORE testing the comparison result.
        // :06363166 `FCOMP Entries[cur].Key` with st0 == Key / :06363173
        // `TEST AH,0x41` / :06363176 `JNZ` -- (AH & 0x41) != 0 means
        // Key <= Entries[cur].Key (or unordered), which is the insertion point.
        if (!(Key > PoolEntries[Cur].Key))
        {
            FoundPosition = true;
            break;
        }
        Prev = Cur;                       // :0636317c
        Cur = PoolEntries[Cur].Next;      // :06363180
    }

    if (FoundPosition)
    {
        // LAB_06363219 -- splice between Prev and Cur.
        NewEntry->Next = PoolEntries[Prev].Next;                      // :0636321e/:06363229 (== Cur)
        PoolEntries[Cur].Prev = static_cast<uint16_t>(NewIndex);       // :06363233
        NewEntry->Prev = static_cast<uint16_t>(Prev);                  // :06363238
        PoolEntries[Prev].Next = static_cast<uint16_t>(NewIndex);      // :0636323c
    }
    else
    {
        // LAB_0636318c -- ran off the tail; Cur is 0xffff either way.
        NewEntry->Next = 0xffff;                                  // :06363195
        NewEntry->Prev = static_cast<uint16_t>(Prev);             // :06363199
        PoolEntries[Prev].Next = static_cast<uint16_t>(NewIndex); // :0636319d
    }

    // LAB_063631a1 -- if the ascending walk cost more hops than the budget,
    // promote one node onto the express list so the next insert can skip it.
    if (Hops <= StepBudget) // :063631a5/:063631a9 (JLE -> plain return)
        return static_cast<uint16_t>(NewIndex);

    uint32_t Promote = Anchor; // :063631b2 (local_8)
    if (StepBudget > 2)        // :063631af/:063631b6
    {
        int32_t Remaining = StepBudget - 2; // :063631bb
        do
        {
            Promote = PoolEntries[Promote].Next; // :063631be-:063631c9
            --Remaining;
        } while (Remaining != 0); // :063631cb
    }

    // LAB_063631cd.
    const uint16_t AnchorExpressNext = PoolEntries[Anchor].ExpressNext; // :063631d6
    Entry& PromotedEntry = PoolEntries[Promote];

    if (AnchorExpressNext == 0xfffe) // :063631e1 -- the anchor is not itself an express node.
    {
        // LAB_06363245 -- push the promoted node onto the front of the express list.
        PromotedEntry.ExpressNext = PoolSortedHead; // :06363249
        PromotedEntry.ExpressPrev = 0xffff;         // :0636324e
        if (PoolSortedHead != 0xffff)               // :06363258
            PoolEntries[PoolSortedHead].ExpressPrev = static_cast<uint16_t>(Promote); // :06363269
        PoolSortedHead = static_cast<uint16_t>(Promote); // :0636326e
    }
    else
    {
        // :063631ed-:0636320a -- splice it in right after the anchor.
        PromotedEntry.ExpressNext = AnchorExpressNext;
        PromotedEntry.ExpressPrev = static_cast<uint16_t>(Anchor);
        if (AnchorExpressNext != 0xffff) // :063631e8/:063631f9
            PoolEntries[AnchorExpressNext].ExpressPrev = static_cast<uint16_t>(Promote); // :06363201
        PoolEntries[Anchor].ExpressNext = static_cast<uint16_t>(Promote); // :0636320a
    }

    return static_cast<uint16_t>(NewIndex); // :0636320f/:06363274
}

// ----------------------------------------------------------------------------
// FUN_06363280 (decomp-tracker item 3507, Common.dll.txt:06363280-0636333f,
// 62 insns). Byte-faithful port; the whole range was verified this pass.
// ----------------------------------------------------------------------------
void IVP_Priority_Pool::Remove(uint16_t Slot)
{
    Entry& E = PoolEntries[Slot]; // :0636328a-:06363290

    const uint16_t PrevIndex = E.Prev; // :06363294
    const uint16_t NextIndex = E.Next; // :0636329a

    if (PrevIndex != 0xffff) // :0636329e
    {
        PoolEntries[PrevIndex].Next = NextIndex; // :063632b0
        if (NextIndex != 0xffff)                 // :063632ab/:063632b5
            PoolEntries[NextIndex].Prev = PrevIndex; // :063632bd
    }
    else
    {
        // LAB_063632c4 -- the removed entry was the list head, so the cached
        // minimum key has to be refreshed from the new head.
        PoolLiveIndex = NextIndex; // :063632c9
        if (NextIndex != 0xffff)   // :063632c4/:063632cd
        {
            PoolEntries[NextIndex].Prev = 0xffff;            // :063632d2
            PoolKeyThreshold = PoolEntries[NextIndex].Key;   // :063632dc-:063632e0
        }
        else
        {
            // LAB_063632e5 -- empty pool: back to the same +1e10f sentinel the
            // constructor installs (raw bits 0x501502f9).
            PoolKeyThreshold = BitsToFloat(0x501502f9);
        }
    }

    // LAB_063632ec -- unlink from the express list, unless this entry was
    // never promoted onto it (the 0xfffe tag Insert seeds).
    const uint16_t ExpressNext = E.ExpressNext; // :063632ee
    if (ExpressNext != 0xfffe)                  // :063632f1
    {
        const uint16_t ExpressPrev = E.ExpressPrev; // :06363301
        if (ExpressNext != 0xffff)                  // :063632fb/:06363305
            PoolEntries[ExpressNext].ExpressPrev = ExpressPrev; // :0636330f
        if (ExpressPrev == 0xffff)                  // :06363314/:06363319
            PoolSortedHead = ExpressNext;           // :0636331b
        else
            PoolEntries[ExpressPrev].ExpressNext = ExpressNext; // :06363327
    }

    // LAB_0636332b -- push the slot back onto the free chain.
    E.Next = PoolFreeHead; // :06363335
    --PoolCount;           // :0636332f
    PoolFreeHead = Slot;   // :0636333a
}

// ============================================================================
// IVP_Object / IVP_Cluster (.text:06345610-06345846). cmn-phy7 2026-10-08.
// ============================================================================
namespace
{
    // FUN_06356920 (.text:06356920-0635695a): null for a null string, else
    // FUN_06343f70 (malloc) of strlen + 1 and a copy including the terminator.
    char* IvpStrdup(const char* Source)
    {
        if (Source == nullptr)
        {
            return nullptr;
        }
        const size_t Length = std::strlen(Source) + 1;
        char* const Copy = static_cast<char*>(std::malloc(Length));
        std::memcpy(Copy, Source, Length);
        return Copy;
    }
}

// FUN_06345610. Father and Template are dereferenced unconditionally.
IVP_Object::IVP_Object(IVP_Cluster* Father, const IVP_RealObjectConstructionTemplate* Template)
{
    init_object_core(Father->environment);       // :0634561e-:06345622
    Father->add_object(this);                    // :06345627-:0634562a
    object_name = IvpStrdup(Template->LongNamePtr); // :0634562f-:0634563e
}

// FUN_06345670.
IVP_Object::IVP_Object(IVP_Environment* Environment)
{
    init_object_core(Environment); // :0634567e
}

// FUN_06345690: +0x10, +0x0c, +0x08, +0x18 (= Environment), +0x14, +0x04.
void IVP_Object::init_object_core(IVP_Environment* Environment)
{
    parent = nullptr;
    prev_object = nullptr;
    next_object = nullptr;
    environment = Environment;
    object_name = nullptr;
    object_type = 0;
}

// FUN_063456b0.
IVP_Object::~IVP_Object()
{
    // :063456b3-:063456c1
    if (parent != nullptr)
    {
        parent->remove_object(this);
    }
    // :063456c6-:063456d6 -- FUN_06343fb0 is free().
    if (object_name != nullptr)
    {
        std::free(object_name);
        object_name = nullptr;
    }
    environment = nullptr; // :063456dd
}

// FUN_06345760: IVP_Object(Environment), vptr 063a5454, +0x1c = 0, +0x04 = 1.
IVP_Cluster::IVP_Cluster(IVP_Environment* Environment)
    : IVP_Object(Environment)
{
    first_object = nullptr;
    object_type = 1;
}

// FUN_06345790 (.text:063457b3-063457cd): `MOV EAX,[ECX]; PUSH 1; CALL [EAX]`
// on first_object until it reads null.
IVP_Cluster::~IVP_Cluster()
{
    while (first_object != nullptr)
    {
        delete first_object;
    }
}

// FUN_063457f0.
void IVP_Cluster::add_object(IVP_Object* Object)
{
    Object->next_object = first_object; // :063457f7
    Object->prev_object = nullptr;      // :063457fa
    if (first_object != nullptr)
    {
        first_object->prev_object = Object; // :06345808
    }
    first_object = Object;              // :0634580b
    Object->parent = this;              // :0634580e
}

// FUN_06345820.
void IVP_Cluster::remove_object(IVP_Object* Object)
{
    if (Object->prev_object == nullptr)
    {
        first_object = Object->next_object; // :0634582b-:0634582e
    }
    else
    {
        Object->prev_object->next_object = Object->next_object; // :06345833-:06345836
    }
    if (Object->next_object != nullptr)
    {
        Object->next_object->prev_object = Object->prev_object; // :06345840-:06345843
    }
}

// IVP_Real_Object_Fast_Static's ctor, inlined into 06345100
// (.text:0634512c-06345140): IVP_Object(Father, Template), vptr 063a544c, then
// the hull manager member (06352150).
IVP_Real_Object_Fast_Static::IVP_Real_Object_Fast_Static(IVP_Cluster* Father, const IVP_RealObjectConstructionTemplate* Template)
    : IVP_Object(Father, Template)
{
}

// ============================================================================
// IVP_Real_Object::IVP_Real_Object (FUN_06345100, .text:06345100-06345259,
// RET 0x14). Arguments: Father (the cluster, forwarded to IVP_Object),
// SurfaceManager (+0x80), Parms (EBP: +0x00 long name, +0x04 short name,
// +0x0c / +0x10 the core flags, +0x18 material, +0x54 client data), an
// optional orientation (null -> a stack identity quaternion) and the position
// (5th stack argument, forwarded to the IVP_Core ctor 06347660).
// ============================================================================
IVP_Real_Object::IVP_Real_Object(IVP_Cluster* Father, IVP_SurfaceManager* SurfaceManager,
                                  const IVP_RealObjectConstructionTemplate* Parms,
                                  const IVP_U_Quat* InitialOrientationOrNull,
                                  const IVP_U_Point* InitialPosition)
    : IVP_Real_Object_Fast_Static(Father, Parms) // :06345124-:06345140 (vptrs 063a5450, 063a544c; hull manager 06352150)
{
    // :06345153 -- vptr 063a5440 (the compiler's).

    // :06345145-:0634516b -- a null orientation is replaced by a stack
    // identity quaternion (x = y = z = 0, w = 1.0f); only the IVP_Core ctor
    // below reads it.
    IVP_U_Quat IdentityOrientation;
    const IVP_U_Quat* Orientation = InitialOrientationOrNull;
    if (Orientation == nullptr)
    {
        IdentityOrientation.x = 0.0f;
        IdentityOrientation.y = 0.0f;
        IdentityOrientation.z = 0.0f;
        IdentityOrientation.w = 1.0f;
        Orientation = &IdentityOrientation;
    }

    // :0634517c-:063451ae, in retail's store order.
    synapse_list_head = nullptr;         // +0x20
    invalid_synapse_list_head = nullptr; // +0x24
    mindist_list = nullptr;              // +0x28
    surface_manager = SurfaceManager;    // +0x80
    anchors = nullptr;                   // +0x7c
    ov_element = nullptr;                // +0x90
    flags = 0;                           // +0x78
    q_core_f_object = nullptr;           // +0x2c
    shift_core_f_object.x = 0.0f;        // +0x30
    shift_core_f_object.y = 0.0f;        // +0x34
    shift_core_f_object.z = 0.0f;        // +0x38
    cache_object = nullptr;              // +0x40
    first_child = nullptr;               // +0x1c

    // :06345173-:063451b1 -- strncpy(+0x84, Parms + 4, 8).
    std::strncpy(name, Parms->ShortNameTag, sizeof(name));

    // :063451b7-:063451f7 -- operator new(0x1bc) + 06347660(this, Orientation,
    // Position, Parms+0x0c, Parms+0x10); the result goes to +0x98, +0x9c, +0xa0.
    IVP_Core* const NewCore = new IVP_Core(this, Orientation, InitialPosition, Parms->FlagsHi, Parms->FlagsLo);
    core = NewCore;          // +0x98
    original_core = NewCore; // +0x9c
    field_a0 = NewCore;      // +0xa0

    // :063451fd-:06345224 -- applied to whatever +0x78 holds after the core ctor.
    if (Parms->FlagsHi != 0)
    {
        flags = (flags & 0xffffff10u) | 0x10u;
    }
    else
    {
        flags = (flags & 0xffffff08u) | 0x8u;
    }

    // :06345227-:0634522a
    material = Parms->Material; // +0x8c

    // :06345230-:06345237 -- core->environment->cluster_manager->add_object(this)
    // (0634b3d0), no null tests.
    NewCore->Environment->cluster_manager->add_object(this);

    // :0634523c-:06345240
    client_data = Parms->ClientData; // +0xa4
}

// ============================================================================
// Narrow-phase per-object cache slot pool (IVP_Environment::vector_cores,
// +0x94) -- FUN_0634d530 / FUN_06353510 / FUN_0634d640.
//
// FUN_0634d530 claims a cache slot, FUN_06353510 resolves it lazily (refcount
// + conditional refresh; IVP_Real_Object::ResolveNarrowPhaseCacheSlot below)
// and FUN_0634d640 is the refresh itself (IVP_RefreshNarrowPhaseCacheSlot).
// The consumers are the IVP_Mindist_Minimize_Solver handlers
// (ivp_collision/ivp_mindist_minimize.cpp), which resolve both sides' slots
// and release them again after the walk.
// ============================================================================

// The pool is IVP_Cache_Object_Manager (IVP_Environment+0x94, ivp_environment.h):
// all 18 retail call sites of FUN_0634d530 load ECX from [env + 0x94].

// IVP_NarrowPhaseCacheSlot (one slot of the pool above) is declared in
// ivp_real_object.h since 2026-10-08 (cmn-phy5; the ray solver reads it).

// FUN_0634d640 (decomp-tracker item 3073, Common.dll.txt:0634d640-0634d854,
// 172 insns / 533 bytes). d237:common-port pass (2026-09-10): PORTED FOR
// REAL. This item's own 3 previously-blocking callees are now all closed:
// FUN_06350df0 == IVP_U_Quat::set_interpolate_quat (decomp-tracker item
// 3152), FUN_06350bc0 == IVP_U_Quat::set_matrix (item 3150), FUN_063484e0
// == IVP_U_Matrix3::vmult (item 2894, `skipped`, already covered by this
// port's existing generic ivp_math.h implementation).
//
// `this` (ECX) is the IVP_NarrowPhaseCacheSlot; Owner = Slot->Owner (+0x8),
// Core = Owner->core (+0x98), Env = Owner->environment (+0x18). Computes
// the same reinterpret-as-float PSI time-delta idiom used throughout this
// port (e.g. IVP_Core::recalc_interpolated_frame, ivp_core.cpp:849-883):
//   DTime = (Env->current_time.time_base - Core->TimeOfLastPsi.time_base) +
//           (Env->current_time.time_offset - Core->TimeOfLastPsi.time_offset)
// Stamps Slot->LastRefreshTick = Env->sim_step_count (Common.dll.txt:
// 0634d663-0634d669), then branches on DTime <=> 0.0f (DAT_063a53d4,
// confirmed exact-zero sentinel elsewhere in this port):
//   - DTime == 0.0f (Common.dll.txt:0634d68f-0634d6f1): no elapsed time, so
//     the cached quat is Core->QWorldFCore copied verbatim (no slerp
//     needed) and the extrapolated position is Core->PosWorldFCore verbatim
//     (DTime * DeltaWorldFCore == 0 either way).
//   - DTime != 0.0f (Common.dll.txt:0634d6f6-0634d787): interpolated quat =
//     Core->QWorldFCore.set_interpolate_quat(&QWorldFCore, &QWorldFCoreNext,
//     DTime * Core->IDeltaTime) (same Alpha formula as
//     recalc_interpolated_frame); extrapolated position = PosWorldFCore +
//     DTime (NOT the normalized Alpha -- confirmed distinct: the FMUL at
//     0634d73c/0634d74a/0634d760 multiplies Core->DeltaWorldFCore directly
//     by the pre-IDeltaTime-scaled DTime still on the FPU stack, not by the
//     Alpha computed a few instructions earlier for the quat slerp) *
//     DeltaWorldFCore.
// Both branches write the SAME extrapolated-position triple into BOTH
// Slot->WorldTransform.vv (+0x4c) and Slot->Secondary (+0x5c) -- confirmed
// identical values in both places by the disassembly. The quat is then
// turned into Slot->WorldTransform's rotation-only rows via
// Quat.set_matrix() (which, per item 3150's own confirmed 0x30-byte write
// range, never touches vv -- consistent with vv being set by hand above).
//
// cmn-phy6 2026-10-08: re-traced against .text:0634d640-0634d854 and
// completed. Corrections to the description above:
//   * DTime is ((now.base - core.base) + now.offset) - core.offset on the
//     x87 stack (:0634d659-:0634d67f), not (a - b) + (c - d) in float.
//   * The compare is FCOMP 0.0f + `TEST AH,0x40; JZ` (:0634d682-:0634d68d):
//     equal OR unordered takes the copy branch; only a non-zero, ordered
//     DTime interpolates.
//   * The interpolation branch re-evaluates DTime on the x87 stack
//     (:0634d720-:0634d737) and uses the unrounded value for the position;
//     Alpha = float(DTime * IDeltaTime) is the stored float argument.
// The two tail blocks are now ported:
//   (a) :0634d795-:0634d7a7 unless Owner->flags & 0xc00 (`TEST CH,0xc` --
//       CH is bits 8-15): WorldTransform.vmult4(&Owner->shift_core_f_object
//       (+0x30), &WorldTransform.vv) (063484e0; it reads every input before
//       storing, so the in-place output is safe). Secondary (+0x5c) keeps the
//       core position.
//   (b) :0634d7ac-:0634d848 Owner->q_core_f_object (+0x2c) set:
//       Quat = Quat * q_core_f_object in place (x = Qx*qw + Qy*qz + Qw*qx -
//       Qz*qy, y = Qy*qw + qy*Qw + Qz*qx - Qx*qz, z = qy*Qx + Qz*qw + Qw*qz -
//       Qy*qx, w = Qw*qw - Qx*qx - Qy*qy - Qz*qz), then set_matrix again
//       (rows only).
// The quaternion product is inlined in retail's summation order (cmn-s2-ivp4).
void IVP_RefreshNarrowPhaseCacheSlot(IVP_NarrowPhaseCacheSlot* Slot)
{
    IVP_Real_Object* Owner = Slot->Owner;
    IVP_Environment* Env = Owner->environment;
    IVP_Core* Core = Owner->core;

    const double DTime = (((double)(Env->current_time.time_base) - Core->TimeOfLastPsi.time_base)
                          + Env->current_time.time_offset) - Core->TimeOfLastPsi.time_offset;

    Slot->LastRefreshTick = Env->sim_step_count;

    if (DTime == 0.0 || std::isnan(DTime)) // DAT_063a53d4, `TEST AH,0x40; JZ`
    {
        Slot->Quat = Core->QWorldFCore;

        Slot->WorldTransform.vv.x = Core->PosWorldFCore.x;
        Slot->WorldTransform.vv.y = Core->PosWorldFCore.y;
        Slot->WorldTransform.vv.z = Core->PosWorldFCore.z;
    }
    else
    {
        const float Alpha = (float)(DTime * Core->IDeltaTime);
        Slot->Quat.set_interpolate_quat(&Core->QWorldFCore, &Core->QWorldFCoreNext, Alpha);

        Slot->WorldTransform.vv.x = (float)(DTime * Core->DeltaWorldFCore.x + Core->PosWorldFCore.x);
        Slot->WorldTransform.vv.y = (float)(DTime * Core->DeltaWorldFCore.y + Core->PosWorldFCore.y);
        Slot->WorldTransform.vv.z = (float)(DTime * Core->DeltaWorldFCore.z + Core->PosWorldFCore.z);
    }

    Slot->Secondary.x = Slot->WorldTransform.vv.x;
    Slot->Secondary.y = Slot->WorldTransform.vv.y;
    Slot->Secondary.z = Slot->WorldTransform.vv.z;

    Slot->Quat.set_matrix(&Slot->WorldTransform);

    if ((Owner->flags & 0xc00u) == 0) // (a)
        Slot->WorldTransform.vmult4(&Owner->shift_core_f_object, &Slot->WorldTransform.vv);

    if (Owner->q_core_f_object != nullptr) // (b)
    {
        // :0634d7ba-:0634d845 -- Quat = Quat * q in place, inlined: every
        // term is read before the first store; y and x stay on the FPU, z and
        // w are spilled as floats; stores go z, w, x, y.
        IVP_U_Quat& Q = Slot->Quat;
        const IVP_U_Quat* R = Owner->q_core_f_object;
        const double Y = (((double)Q.y * R->w + (double)R->y * Q.w) + (double)Q.z * R->x) - (double)Q.x * R->z;
        const float Z = (float)((((double)R->y * Q.x + (double)Q.z * R->w) + (double)Q.w * R->z) - (double)Q.y * R->x);
        const float W = (float)((((double)Q.w * R->w - (double)Q.x * R->x) - (double)Q.y * R->y) - (double)Q.z * R->z);
        const double X = (((double)Q.x * R->w + (double)Q.y * R->z) + (double)Q.w * R->x) - (double)R->y * Q.z;
        Q.z = Z;
        Q.w = W;
        Q.x = (float)X;
        Q.y = (float)Y;
        Q.set_matrix(&Slot->WorldTransform); // :0634d848 FUN_06350bc0
    }
}

static void IVP_DEFERRED_RefreshNarrowPhaseCacheSlot(IVP_NarrowPhaseCacheSlot* Slot) { IVP_RefreshNarrowPhaseCacheSlot(Slot); }

// FUN_0634d530 (.text:0634d530-0634d5b5), thiscall on the IVP_Cache_Object_Manager
// (ECX = [env + 0x94] at all 18 call sites), (object), RET 4:
//   slot = &cache_objects[next_free_index] (index*0x70); while slot->RefCount
//   (+4) != 0: next_free_index = (next_free_index + 1) & (n - 1), stored on every
//   step, slot = that entry. next_free_index = (n - 1) & (next_free_index + 1).
//   slot->Owner (+8) non-null -> FUN_0634d510(old owner). slot->Owner = object;
//   object->+0x40 = slot; slot->+0 = 0; `SHL [object+0x78],0x18; CMP 0x8000000;
//   JL skip` -> signed low flag byte >= 8 refreshes the slot (FUN_0634d640).
//   Returns the slot.
IVP_NarrowPhaseCacheSlot* IVP_Cache_Object_Manager::get_cache_object(IVP_Real_Object* Owner)
{
    int32_t Index = next_free_index;
    IVP_NarrowPhaseCacheSlot* Slot = &cache_objects[Index];

    if (Slot->RefCount != 0)
    {
        const int32_t Mask = number_of_cache_objects - 1;
        do
        {
            Index = (Index + 1) & Mask;
            next_free_index = Index; // retail persists the cursor every iteration, not just at the end.
            Slot = &cache_objects[Index];
        } while (Slot->RefCount != 0);
    }

    // Advance the cursor one past the slot just claimed.
    next_free_index = (number_of_cache_objects - 1) & (Index + 1);

    // :0634d57e-:0634d586 FUN_0634d510(Slot->Owner): the previous owner drops
    // the slot (its +0x40 and the slot's +0x08 are cleared). cmn-phy6 2026-10-08.
    if (Slot->Owner != nullptr)
        IVP_Real_Object_Free_Cache_Object(Slot->Owner);

    Slot->Owner = Owner;
    Owner->cache_object = Slot;
    Slot->LastRefreshTick = 0;

    if (int8_t(Owner->flags & 0xff) >= 8)
        IVP_DEFERRED_RefreshNarrowPhaseCacheSlot(Slot);

    return Slot;
}

// FUN_06353510 (decomp-tracker item 3196). See the header comment above
// IVP_Real_Object::ResolveNarrowPhaseCacheSlot's declaration for the
// evidence trail.
IVP_NarrowPhaseCacheSlot* IVP_Real_Object::ResolveNarrowPhaseCacheSlot()
{
    if (cache_object == nullptr)
        cache_object = environment->cache_object_manager->get_cache_object(this); // FUN_0634d530

    IVP_NarrowPhaseCacheSlot* Slot = cache_object;
    ++Slot->RefCount;

    if (int8_t(flags & 0xff) < 8 && environment->sim_step_count > Slot->LastRefreshTick)
        IVP_DEFERRED_RefreshNarrowPhaseCacheSlot(Slot);

    return Slot;
}

// get_cache_object_no_lock (cmn-ivp2b, 2026-10-08), inlined in FUN_0634e0b0
// (.text:0634e18d-0634e1d1 for object 0, :0634e2a8-0634e2ec for object 1):
// `if (!+0x40) +0x40 = FUN_0634d530(env->+0x94, this)`, then
// `SHL ECX(flags),0x18; CMP ECX,0x8000000; JGE skip` (signed byte < 8) and
// `CMP env->+0x110,[slot]; JLE skip` before FUN_0634d640(slot). No refcount.
IVP_NarrowPhaseCacheSlot* IVP_Real_Object::get_cache_object_no_lock()
{
    if (cache_object == nullptr)
        cache_object = environment->cache_object_manager->get_cache_object(this); // FUN_0634d530

    IVP_NarrowPhaseCacheSlot* Slot = cache_object;
    if (int8_t(flags & 0xff) < 8 && environment->sim_step_count > Slot->LastRefreshTick)
        IVP_DEFERRED_RefreshNarrowPhaseCacheSlot(Slot);

    return Slot;
}

// D-item319x pass (2026-09-10, decomp-tracker items 3195/3197/3200): the
// matching decrement for ResolveNarrowPhaseCacheSlot's refcount bump above.
// Not its own retail function -- confirmed inline in the shared tail of all
// 3 real DAT_0658ab4c narrow-phase dispatch handlers, e.g. FUN_063536d0
// (Common.dll.txt:06353826 `DEC dword ptr [ESI+0x4]` / 0635382c-0635382e
// `MOV EAX,[ECX+0x4]; DEC EAX; MOV [ECX+0x4],EAX`, where ESI/ECX are this
// object's and its partner's resolved slots). The minimize handlers in
// ivp_mindist_minimize.cpp write that decrement inline as retail does.
void IVP_Real_Object::ReleaseNarrowPhaseCacheSlot(IVP_NarrowPhaseCacheSlot* Slot)
{
    --Slot->RefCount;
}

// The four slot transforms below (ECX = the cache slot, RET 8) are retail's own
// out-of-line copies of the IVP_U_Matrix operations applied to the slot's
// WorldTransform at +0x1c. Each body is instruction-for-instruction the same
// x87 sequence as the matrix function it forwards to (cmn-phy8 2026-10-08,
// capstone on the raw PE), only with every operand offset shifted by +0x1c:
//   0634d860 == 06348470 vimult4, 0634d8d0 == 063484e0 vmult4,
//   0634d970 == 06366010 vimult,  0634d9d0 == 06348580 vmult.
// The summation order and rounding points are documented on those methods in
// ivp_math.h.

// FUN_0634d860 (.text:0634d860-0634d8c0): world position -> slot space,
// out = M^T * (in - vv).
void IVP_TransformWorldToSlotSpace(IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* WorldPos, IVP_U_Point* OutLocalPos)
{
    Slot->WorldTransform.vimult4(WorldPos, OutLocalPos);
}

// FUN_0634d9d0 (.text:0634d9d0-0634da27): slot direction -> world, out = M * in
// (no translation read). Alias-safe; Leave_PK 06365280 calls it in place.
void IVP_RotateSlotToWorldSpace(IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* LocalDir, IVP_U_Point* OutWorldDir)
{
    Slot->WorldTransform.vmult(LocalDir, OutWorldDir);
}

// FUN_0634d970 (.text:0634d970-0634d9c7): world direction -> slot space,
// out = M^T * in (no translation read). Alias-safe (called in place).
void IVP_RotateWorldToSlotSpace(IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* WorldDir, IVP_U_Point* OutLocalDir)
{
    Slot->WorldTransform.vimult(WorldDir, OutLocalDir);
}

// FUN_0634d8d0 (.text:0634d8d0-0634d964): slot position -> world,
// out = M * in + vv, with vmult4's float spills of the x / y partial sums.
void IVP_TransformSlotToWorldSpace(IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* LocalPos, IVP_U_Point* OutWorldPos)
{
    Slot->WorldTransform.vmult4(LocalPos, OutWorldPos);
}

// FUN_063454e0 (.text:063454e0-0634552c), thiscall, no arguments:
//   EDI = core->MovementState (+0x98, +0x60); core->MovementState =
//   (state & 0xffffff21) | 0x21; EDI = sign-extended low byte of the old state;
//   FUN_0634ebd0(this = core->Environment->mindist_manager (+0x98, +0x0c, +0x10),
//   this object); then core is re-read and its state becomes
//   ((cur ^ old) & 0xff) ^ cur, i.e. the old low byte back under the current
//   upper bits. Only the low byte of the sign-extended old value survives the
//   mask, so the plain old state is equivalent. Caller: 06347170 per object.
void IVP_Real_Object::recheck_ov_element_as_moving()
{
    IVP_Core* const CoreBefore = core;
    const uint32_t OldState = CoreBefore->MovementState;
    CoreBefore->MovementState = (OldState & 0xFFFFFF21u) | 0x21u;
    core->Environment->mindist_manager->RecheckOvElement(this);
    IVP_Core* const CoreAfter = core;
    const uint32_t CurrentState = CoreAfter->MovementState;
    CoreAfter->MovementState = ((CurrentState ^ OldState) & 0xFFu) ^ CurrentState;
}

// ============================================================================
// claude-code:cmn-ivp3d block (2026-10-08): contact points, collision
// detection and phantoms. Declarations and summaries in ivp_real_object.h.
// ============================================================================

// FUN_06345550 (.text:06345550-063455b8, RET 4), unlink_contact_points.
// mindist_list (+0x28) heads the contact-point records of this object,
// threaded through the record's per-object node (record +0x08 for Object1,
// +0x1c for Object2); the node's int16 at +0x0c is record - node
// (IVP_Contact_Point::Delta1 / Delta2). The head is re-read on every
// pass: RemoveDistFromSystem unlinks the record.
void IVP_Real_Object::unlink_contact_points(bool bSilently)
{
    for (;;)
    {
        char* const Node = static_cast<char*>(mindist_list); // :0634555d-:06345566
        if (Node == nullptr)
        {
            break;
        }
        IVP_Contact_Point* const Record =
            reinterpret_cast<IVP_Contact_Point*>(Node + *reinterpret_cast<int16_t*>(Node + 0xc)); // :06345568 / :0634557a
        IVP_ListManager* const FrictionSystem = Record->ListManagerOwner; // :06345570 (+0x70)
        IVP_Core* const Core2 = Record->Object2->original_core;          // :0634556c-:06345574 (+0x24 -> +0x9c)

        // :0634557c-:06345590: `TEST EBP,EBP; JNZ` -- not silent wakes both
        // cores, Object1's first (FUN_06347d80).
        if (!bSilently)
        {
            Record->Object1->original_core->wake_or_ensure_simulation();
            Core2->wake_or_ensure_simulation();
        }

        FrictionSystem->RemoveDistFromSystem(Record); // :06345595-:06345598 FUN_06355490

        // :0634559d-:063455af: an emptied friction system (CtxListCount
        // +0x3e == 0) is deleted through its slot 6 (scalar deleting dtor).
        if (FrictionSystem->CtxListCount == 0)
        {
            delete FrictionSystem;
        }
    }
}

// FUN_06344d50 (.text:06344d50-06344db6, RET 4), enable_collision_detection.
void IVP_Real_Object::enable_collision_detection(bool bEnable)
{
    if (bEnable)
    {
        // :06344d5e-:06344d82: already on (flags & 0x300) -> nothing.
        if ((flags & 0x300u) != 0)
        {
            return;
        }
        environment->mindist_manager->RebuildOvElement(this); // FUN_0634f180
        flags = (flags & 0xfffffdffu) | 0x100u;                // re-read after the call
        return;
    }

    // :06344d85-:06344db0: already off -> nothing; else the OV element goes
    // (slot 4 with 1, the scalar deleting dtor), bits 8-9 are cleared, the
    // pointer is nulled and the contact points are unlinked silently.
    if ((flags & 0x300u) == 0)
    {
        return;
    }
    if (ov_element != nullptr)
    {
        delete ov_element;
    }
    flags &= 0xfffffcffu;
    ov_element = nullptr;
    unlink_contact_points(true);
}

// FUN_06345f10 (.text:06345f10-06345f86, RET 4), convert_to_phantom. The old
// phantom (FUN_063442c0 + operator delete) goes first and first_child is
// nulled before operator new(0x44); a failed allocation stores null.
IVP_Controller_Phantom* IVP_Real_Object::convert_to_phantom(const IVP_Template_Phantom* Template)
{
    delete static_cast<IVP_Controller_Phantom*>(first_child); // :06345f2a-:06345f3e
    first_child = nullptr;                                     // :06345f43
    IVP_Controller_Phantom* const Phantom = new IVP_Controller_Phantom(this, Template); // :06345f41-:06345f6a
    first_child = Phantom;                                     // :06345f77
    return Phantom;
}

// FUN_06345eb0 (.text:06345eb0-06345f0a), convert_phantom_to_object.
int32_t IVP_Real_Object::convert_phantom_to_object()
{
    // :06345eb5-:06345ebd: bits 8-9 of the flags taken BEFORE anything else,
    // sign-extended (`SHL 0x16; SAR 0x1e`): 0x100 -> 1, 0x200 -> -2, 0x300 -> -1.
    const int32_t CollisionBits = static_cast<int32_t>(flags << 22) >> 30;

    enable_collision_detection(false); // :06345ebb-:06345ec0

    delete static_cast<IVP_Controller_Phantom*>(first_child); // :06345ec5-:06345ed9
    first_child = nullptr;                                     // :06345ee1

    // :06345edc-:06345ef2: movement byte 9 -> (f & 0xffffff10) | 0x10.
    if (static_cast<uint8_t>(flags) == 0x9)
    {
        flags = (flags & 0xffffff10u) | 0x10u;
    }

    // :06345ef5-:06345efd: `CMP EDI,1; JNZ` -- only the exact value 1.
    if (CollisionBits == 1)
    {
        enable_collision_detection(true);
    }
    return 1; // :06345f04
}

// FUN_063454b0 (.text:063454b0-063454d8), recalc_exact_mindists. The mindist
// manager (env +0x10) is read once up front; each synapse's next link is read
// before the call (:063454c3).
void IVP_Real_Object::recalc_exact_mindists()
{
    IVP_Mindist_Manager* const MindistManager = environment->mindist_manager; // :063454b0-:063454b4
    IVP_Synapse_Real* Synapse = synapse_list_head;                           // :063454b7
    while (Synapse != nullptr)
    {
        IVP_Synapse_Real* const Next = Synapse->next;
        MindistManager->recalc_exact_mindist(Synapse->get_mindist()); // FUN_06350460
        Synapse = Next;
    }
}

// FUN_06345460 (.text:06345460-063454ac), recalc_invalid_mindists. Next link
// read before the calls (:06345471); the mindist manager is re-read from the
// environment for every mindist that moves (:0634548c-:0634548f).
void IVP_Real_Object::recalc_invalid_mindists()
{
    IVP_Synapse_Real* Synapse = invalid_synapse_list_head; // :06345463
    while (Synapse != nullptr)
    {
        IVP_Synapse_Real* const Next = Synapse->next;
        IVP_Ledge_Mindist* const Mindist = Synapse->get_mindist();
        Mindist->recalc_mindist(); // FUN_06352b90, result unused
        // :0634547d-:0634548a: `AND 0xc000; CMP 0x4000; JZ skip`.
        if ((Mindist->StateFlags & 0xc000u) != 0x4000u)
        {
            IVP_Mindist_Manager* const MindistManager = environment->mindist_manager;
            MindistManager->remove_invalid_mindist(Mindist);         // FUN_0634ffa0
            MindistManager->insert_exact_mindist_no_recalc(Mindist); // FUN_0634e8a0
        }
        Synapse = Next;
    }
}

// FUN_063455c0 (decomp-tracker item 2802, Common.dll.txt:063455c0-0634560c,
// 33 insns). D-cluster-ivp-adjacent pass (2026-09-10): fully traced. Sole
// caller is ~IVP_Real_Object (FUN_06345300/item 2795) below. See this
// method's own header comment (ivp_real_object.h) for the behavior summary.
void IVP_Real_Object::TeardownMindistListenersAndCandidates()
{
    // Common.dll.txt:063455c7-063455e6. Drain anchors's own listener chain.
    // Retail expects each listener's own notify handler (invoked through
    // FUN_06356910, already ported) to unlink itself from anchors as a
    // side effect of being notified; the "did the head actually change"
    // compare below is retail's own infinite-loop safety net for a listener
    // that doesn't (same assert-fail-on-stuck-head idiom already used by
    // IVP_Hull_Manager::WalkActiveEntries's own retail sibling).
    // .text:063455c5-063455e6: EBX (previous head) starts at 0; a head equal
    // to the previous one hits the CORE trap at 063455da and the loop goes
    // on (no exit after the trap).
    {
        IVP_Anchor* PreviousHead = nullptr;
        for (;;)
        {
            IVP_Anchor* Head = anchors;
            if (Head == nullptr)
                break;

            FUN_06356910(Head, this);

            if (Head == PreviousHead)
            {
                IVP_CoreTrap(); // 063455da
            }
            PreviousHead = Head;
        }
    }

    // Common.dll.txt:063455e8-063455ec: `PUSH 1; CALL FUN_06345550`.
    unlink_contact_points(true);

    // Common.dll.txt:063455f1-06345609: while the object's synapse list
    // (+0x20) is non-empty, take the head synapse's mindist (`MOVSX ECX,
    // [EAX+0x18]; ADD ECX,EAX`; a zero address loops back, :063455fe) and
    // call its slot 4 with 1 -- the scalar deleting destructor, whose body
    // (0634dd60) unlinks both synapses. Typed 2026-10-08 (cmn-phy5; was a raw
    // vtable[4] call on an unidentified node).
    for (;;)
    {
        IVP_Synapse_Real* const Head = synapse_list_head;
        if (Head == nullptr)
            break;

        IVP_Ledge_Mindist* const Mindist = Head->get_mindist();
        if (Mindist == nullptr)
            continue;

        delete Mindist;
    }
}

// FUN_06345300 (.text:06345300-06345447). The deleting dtor in slot 0 of
// 063a5440 is FUN_063452e0. After this body the compiler runs the hull
// manager's dtor (063521b0, :06345423) and ~IVP_Object (063456b0, :06345432),
// exactly retail's tail.
IVP_Real_Object::~IVP_Real_Object()
{
    // :06345320 -- vptr 063a5440 (the compiler's).

    // :06345326-:06345345 -- the phantom (+0x1c): 063442c0 + operator delete.
    IVP_Controller_Phantom* const Phantom = static_cast<IVP_Controller_Phantom*>(first_child);
    if (Phantom != nullptr)
    {
        delete Phantom;
    }
    first_child = nullptr;

    // :06345345-:06345351 -- 063521f0 on the hull manager.
    hull_manager.WalkActiveEntries();

    // :06345356-:06345358 -- 063455c0.
    TeardownMindistListenersAndCandidates();

    // :0634535d-:0634536d -- 0634ced0 (environment object listeners, slot 0)
    // with a stack {environment, this}.
    {
        IVP_Environment_Object_Notify_Event Event;
        Event.Env = environment;
        Event.Object = this;
        IVP_Environment_Notify_Object_Listeners_Slot0(environment, &Event);
    }

    // :06345372-:06345381 -- `TEST AH,0x20`: flags bit 0x2000.
    if ((flags & 0x2000u) != 0)
    {
        environment->cluster_manager->fire_event_collision_object_deleted(this); // 0634b090
    }

    // :06345386-:063453a3 -- `TEST AH,0x10`: flags bit 0x1000, with a fresh
    // stack {environment, this}.
    if ((flags & 0x1000u) != 0)
    {
        IVP_Environment_Object_Notify_Event Event;
        Event.Env = environment;
        Event.Object = this;
        environment->cluster_manager->fire_event_object_deleted(&Event); // 0634ae90
    }

    // :063453a8-:063453b5 -- core->environment->cluster_manager (0634b3e0).
    core->Environment->cluster_manager->remove_object(this);

    // :063453ba-:063453c7 -- 0634d510 (cdecl) only when +0x40 is set.
    if (cache_object != nullptr)
    {
        IVP_Real_Object_Free_Cache_Object(this);
    }

    // :063453ca-:06345401 -- 06346f20 on core, then on field_a0 unless it is
    // core, then on original_core unless it is field_a0 or core. No null tests.
    IVP_Core_Remove_Object(core, this);
    if (field_a0 != core)
    {
        IVP_Core_Remove_Object(field_a0, this);
    }
    if (original_core != field_a0 && original_core != core)
    {
        IVP_Core_Remove_Object(original_core, this);
    }

    // :06345406-:06345412 -- operator delete (06391d5a) and null.
    ::operator delete(q_core_f_object);
    q_core_f_object = nullptr;
}

// ============================================================================
// FUN_06345850: IVP_Real_Object::get_m_world_f_object_at_transform
// ============================================================================
void IVP_Real_Object::get_m_world_f_object_at_transform(IVP_Time Time, IVP_U_Matrix* OutMatrix)
{
    IVP_Core* CorePtr = core;

    // 06345853-0634587d: DTime = ((T.base - psi.base) + T.offset) - psi.offset stays on the
    // FPU; only Alpha = DTime * IDeltaTime is rounded (FSTP float argument).
    const float Alpha = (float)(((((double)Time.time_base - CorePtr->TimeOfLastPsi.time_base) + Time.time_offset) -
                                 CorePtr->TimeOfLastPsi.time_offset) * CorePtr->IDeltaTime);
    IVP_U_Quat InterpQuat;
    InterpQuat.set_interpolate_quat(&CorePtr->QWorldFCore, &CorePtr->QWorldFCoreNext, Alpha);

    // 0634588b-063458dc: DTime recomputed on the FPU; each vv component is
    // float(DTime * delta + pos), rounded once; stores go z, x, y.
    const double DTime = (((double)Time.time_base - CorePtr->TimeOfLastPsi.time_base) + Time.time_offset) -
                         CorePtr->TimeOfLastPsi.time_offset;
    const double PosY = DTime * CorePtr->DeltaWorldFCore.y + CorePtr->PosWorldFCore.y;
    const float PosZ = (float)(DTime * CorePtr->DeltaWorldFCore.z + CorePtr->PosWorldFCore.z);
    const double PosX = DTime * CorePtr->DeltaWorldFCore.x + CorePtr->PosWorldFCore.x;
    OutMatrix->vv.z = PosZ;
    OutMatrix->vv.x = (float)PosX;
    OutMatrix->vv.y = (float)PosY;

    // 063458df: rotation rows from the interpolated quaternion.
    InterpQuat.set_matrix(OutMatrix);

    // 063458e4-063458f3: flags & 0xC00 clear -> vv = M * shift_core_f_object + vv.
    if ((flags & 0x0C00) == 0)
    {
        OutMatrix->vmult4(&shift_core_f_object, &OutMatrix->vv);
    }

    // 063458f8-063459ac: InterpQuat = InterpQuat * q_core_f_object, inlined with its own
    // summation order (not set_mult_quat 063510f0's); y and x stay on the FPU, z and w are
    // spilled as floats; stores go z, w, x, y; then the rotation rows are rebuilt.
    if (q_core_f_object != nullptr)
    {
        const IVP_U_Quat* Q2 = q_core_f_object;
        const double Ry = (((double)InterpQuat.z * Q2->x + (double)InterpQuat.y * Q2->w) +
                           (double)InterpQuat.w * Q2->y) -
                          (double)InterpQuat.x * Q2->z;
        const float Rz = (float)((((double)InterpQuat.w * Q2->z + (double)InterpQuat.z * Q2->w) +
                                  (double)InterpQuat.x * Q2->y) -
                                 (double)InterpQuat.y * Q2->x);
        const float Rw = (float)((((double)InterpQuat.w * Q2->w - (double)InterpQuat.x * Q2->x) -
                                  (double)InterpQuat.y * Q2->y) -
                                 (double)InterpQuat.z * Q2->z);
        const double Rx = (((double)InterpQuat.y * Q2->z + (double)InterpQuat.w * Q2->x) +
                           (double)InterpQuat.x * Q2->w) -
                          (double)InterpQuat.z * Q2->y;
        InterpQuat.z = Rz;
        InterpQuat.w = Rw;
        InterpQuat.x = (float)Rx;
        InterpQuat.y = (float)Ry;
        InterpQuat.set_matrix(OutMatrix);
    }
}

// ============================================================================
// FUN_063456f0: IVP_Real_Object::get_m_world_f_object_AT
// Convenience wrapper: uses the owning environment's current simulation time
// (IVP_Environment::current_time at +0xF8) instead of a caller-supplied IVP_Time.
// .text:063456f0-0634570b: environment (+0x18) is read without a test.
// ============================================================================
void IVP_Real_Object::get_m_world_f_object_AT(IVP_U_Matrix* OutMatrix)
{
    get_m_world_f_object_at_transform(environment->current_time, OutMatrix);
}

// ============================================================================
// FUN_063459c0: IVP_Real_Object::calc_at_time
// Same PSI-interpolation math as get_m_world_f_object_at_transform, but the
// caller wants orientation and position as separate outputs instead of a
// packed IVP_U_Matrix.
// ============================================================================
void IVP_Real_Object::calc_at_time(IVP_Time Time, IVP_U_Quat* OutQuat, IVP_U_Point* OutPosition)
{
    IVP_Core* CorePtr = core;

    // 063459c3-063459f7: DTime = ((T.base - psi.base) + T.offset) - psi.offset stays on the
    // FPU; only Alpha = DTime * IDeltaTime is rounded; the interpolation writes OutQuat.
    const float Alpha = (float)(((((double)Time.time_base - CorePtr->TimeOfLastPsi.time_base) + Time.time_offset) -
                                 CorePtr->TimeOfLastPsi.time_offset) * CorePtr->IDeltaTime);
    OutQuat->set_interpolate_quat(&CorePtr->QWorldFCore, &CorePtr->QWorldFCoreNext, Alpha);

    // 063459fc-06345a45: DTime recomputed on the FPU; each component is
    // float(DTime * delta + pos), rounded once; stores go z, x, y.
    const double DTime = (((double)Time.time_base - CorePtr->TimeOfLastPsi.time_base) + Time.time_offset) -
                         CorePtr->TimeOfLastPsi.time_offset;
    const double PosY = DTime * CorePtr->DeltaWorldFCore.y + CorePtr->PosWorldFCore.y;
    const float PosZ = (float)(DTime * CorePtr->DeltaWorldFCore.z + CorePtr->PosWorldFCore.z);
    const double PosX = DTime * CorePtr->DeltaWorldFCore.x + CorePtr->PosWorldFCore.x;
    OutPosition->z = PosZ;
    OutPosition->x = (float)PosX;
    OutPosition->y = (float)PosY;

    // 06345a48-06345a79: flags & 0xC00 clear -> a stack matrix (rows from OutQuat, vv = the
    // position just written) maps shift_core_f_object into OutPosition.
    if ((flags & 0x0C00) == 0)
    {
        IVP_U_Matrix TempMatrix;
        OutQuat->set_matrix(&TempMatrix);
        TempMatrix.vv.set(OutPosition);
        TempMatrix.vmult4(&shift_core_f_object, OutPosition);
    }

    // 06345a7e-06345b11: OutQuat = OutQuat * q_core_f_object, inlined with its own summation
    // order (not set_mult_quat 063510f0's); y and x stay on the FPU, z and w are spilled as
    // floats; every read precedes the first store; stores go z, w, x, y.
    if (q_core_f_object != nullptr)
    {
        const IVP_U_Quat* Q2 = q_core_f_object;
        const double Ry = (((double)OutQuat->w * Q2->y + (double)OutQuat->z * Q2->x) +
                           (double)OutQuat->y * Q2->w) -
                          (double)OutQuat->x * Q2->z;
        const float Rz = (float)((((double)OutQuat->w * Q2->z + (double)Q2->w * OutQuat->z) +
                                  (double)Q2->y * OutQuat->x) -
                                 (double)OutQuat->y * Q2->x);
        const float Rw = (float)((((double)OutQuat->w * Q2->w - (double)OutQuat->x * Q2->x) -
                                  (double)Q2->y * OutQuat->y) -
                                 (double)OutQuat->z * Q2->z);
        const double Rx = (((double)OutQuat->x * Q2->w + (double)OutQuat->y * Q2->z) +
                           (double)OutQuat->w * Q2->x) -
                          (double)Q2->y * OutQuat->z;
        OutQuat->z = Rz;
        OutQuat->w = Rw;
        OutQuat->x = (float)Rx;
        OutQuat->y = (float)Ry;
    }
}

// ============================================================================
// FUN_06345710: IVP_Real_Object::calc_at_time_AT
// Convenience wrapper: uses the owning environment's current simulation time.
// .text:06345710-06345731: environment (+0x18) is read without a test.
// ============================================================================
void IVP_Real_Object::calc_at_time_AT(IVP_U_Quat* OutQuat, IVP_U_Point* OutPosition)
{
    calc_at_time(environment->current_time, OutQuat, OutPosition);
}

// ============================================================================
// FUN_06346170 (.text:06346170-06346190): IVP_Real_Object::EnsureInSimulation.
//   06346170-06346174  `CMP byte [ECX+0x78],8; JZ 06346181`.
//   != 8: 06346176-0634617c  ECX = core (+0x98); tail JMP FUN_063470e0
//         (IVP_Core::update_time_of_last_motion).
//   == 8: 06346181-0634618b  environment (+0x18)->FUN_0634bb90(original_core
//         (+0x9c)) -- IVP_Environment_Enqueue_Core.
// Neither pointer is tested. Callers: 06346080 / 063460c0 / 06346100 below,
// PhySys 06285627, 06347dc3.
// ============================================================================
void IVP_Real_Object::EnsureInSimulation()
{
    if (static_cast<uint8_t>(flags) == 8)
    {
        IVP_Environment_Enqueue_Core(environment, original_core);
        return;
    }
    core->update_time_of_last_motion();
}

// ============================================================================
// cmn-physys4 block (2026-10-08): the IVP_Real_Object methods PhySys calls.
// No method tests a pointer; retail has no test in any of them.
// ============================================================================

// FUN_06344d30: `EAX = NewMass; ECX = [ECX+0x98]; PUSH EAX; CALL FUN_06347c10; RET 4`.
void IVP_Real_Object::change_mass(float NewMass)
{
    core->set_mass(NewMass);
}

// FUN_06345530:
//   06345530-06345538  `MOV EAX,[ECX+0x90]; TEST EAX,EAX; JZ ret` -- no OV element, nothing.
//   0634553a-06345547  ECX = [[[this+0x98]+0xc]+0x10] = core->Environment->mindist_manager;
//                      PUSH this; CALL FUN_0634ebd0. The environment is the CORE's, not
//                      this->environment (+0x18).
void IVP_Real_Object::recheck_collision_filter()
{
    if (ov_element == nullptr)
    {
        return;
    }
    core->Environment->mindist_manager->RecheckOvElement(this);
}

// FUN_06345e60: EAX = core (+0x98). `FLD [EAX+0x128]` (y), `MOV ECX,[EAX+0x12c]` (z, a dword
// move), `FLD [EAX+0x124]` (x); FSTP to Out+0 pops x, FSTP to Out+4 pops y, Out+8 = z. Plain
// float copies, Out+0xc (hesse) is not written.
void IVP_Real_Object::get_core_position_ws(IVP_U_Point* Out) const
{
    const IVP_U_Point& Position = core->MWorldFCore.vv;
    Out->k[0] = Position.k[0];
    Out->k[1] = Position.k[1];
    Out->k[2] = Position.k[2];
}

// FUN_06345ea0: `MOV EAX,[ECX+0x98]; FLD float [EAX+0x4]; RET` -- a float load, so the value
// in ST0 is exactly the stored float.
float IVP_Real_Object::get_upper_limit_radius() const
{
    return core->UpperLimitRadius;
}

// FUN_06346080: CALL 06346170, then EAX = &core->SpeedChange (+0x84):
//   06346092-063460a4  y = Delta.y + SpeedChange.y; z = Delta.z + SpeedChange.z, z stored first
//   063460a8-063460b5  x = SpeedChange.x + Delta.x; then the stores of z, x, y.
// Each sum is two floats added on the x87 stack and stored as float, which is the float sum.
void IVP_Real_Object::async_add_speed_object_ws(const IVP_U_Point* SpeedWs)
{
    EnsureInSimulation();
    IVP_Core* const Core = core;   // 06346088: re-read after the call
    float const Y = SpeedWs->k[1] + Core->SpeedChange.y;
    float const Z = SpeedWs->k[2] + Core->SpeedChange.z;
    float const X = Core->SpeedChange.x + SpeedWs->k[0];
    Core->SpeedChange.z = Z;
    Core->SpeedChange.x = X;
    Core->SpeedChange.y = Y;
}

// FUN_063460c0: the same shape on core->RotSpeedChange (+0x74).
void IVP_Real_Object::async_add_rot_speed_object_cs(const IVP_U_Point* RotSpeedCs)
{
    EnsureInSimulation();
    IVP_Core* const Core = core;   // 063460c8
    float const Y = RotSpeedCs->k[1] + Core->RotSpeedChange.y;
    float const Z = RotSpeedCs->k[2] + Core->RotSpeedChange.z;
    float const X = Core->RotSpeedChange.x + RotSpeedCs->k[0];
    Core->RotSpeedChange.z = Z;
    Core->RotSpeedChange.x = X;
    Core->RotSpeedChange.y = Y;
}

// FUN_06346100:
//   06346107           CALL 06346170 (EnsureInSimulation).
//   0634610c-0634612a  core->FUN_06346890(environment->current_time.time_base (+0xf8),
//                      .time_offset (+0xfc), &Matrix) -- calc_interpolated_transform.
//   0634612f-0634613d  Matrix.FUN_06348470(PositionWs, &PositionCs) -- vimult4.
//   06346142-06346150  Matrix.FUN_06366010(ImpulseWs, &ImpulseCs) -- IVP_U_Matrix3::vimult.
//   06346155-06346162  core->FUN_06346c00(&PositionCs, &ImpulseCs, ImpulseWs).
void IVP_Real_Object::async_push_object_ws(const IVP_U_Point* PositionWs, const IVP_U_Point* ImpulseWs)
{
    EnsureInSimulation();
    IVP_Core* const Core = core;   // 06346115

    IVP_U_Matrix Matrix;           // local_40
    Core->calc_interpolated_transform(environment->current_time.time_base,
                                      environment->current_time.time_offset, &Matrix);

    IVP_U_Point PositionCs;        // local_50
    Matrix.vimult4(PositionWs, &PositionCs);

    IVP_U_Point ImpulseCs;         // local_60
    Matrix.vimult(ImpulseWs, &ImpulseCs);

    Core->apply_impulse_at_point(reinterpret_cast<const Vector*>(&PositionCs),
                                 reinterpret_cast<const Vector*>(&ImpulseCs),
                                 reinterpret_cast<const Vector*>(ImpulseWs));
}
// ---- end cmn-physys4 block ----

// FUN_063450e0 (Common.dll.txt:406987-406999, decomp-tracker item 2789, 8
// insns). RE-ATTRIBUTED 2026-09-09: a prior lane (D108) found call-site
// evidence that this function's `this` is IVP_Real_Object*, not IVP_Core* as
// item 2789 was previously attributed (pointing at
// IVP_Core::revive_sleeping_core). This pass re-verified from the function's
// OWN raw bytes rather than trusting either side:
//   CMP byte ptr [ECX+0x78],0x8    ; this->flags low byte == 8 ?
//   JNZ done
//   EAX = [ECX+0x9c]                ; EAX = this->original_core
//   ECX2 = [EAX+0xc]                 ; push this Core's Environment (+0xC)
//   PUSH ECX2
//   ECX = [EAX+0x15c]                ; ECX = this Core's OwningSimUnit (+0x15C)
//   CALL FUN_06349490(ECX=SimUnit, stack=Environment)
// this->flags(+0x78) and this->original_core(+0x9C) are exact matches for
// IVP_Real_Object's own fields above -- not IVP_Core's (whose analogous state
// field, MovementState, lives at +0x60, not +0x78). Confirmed independently
// at both of this function's other 2 call sites too: FUN_06344dc0
// (Common.dll.txt:06344dc0-06344dcc) reads `[ECX+0x98]->+0x60` (== this->core
// ->MovementState, per IVP_Real_Object::core at +0x98 and
// IVP_Core::MovementState at +0x60) before tail-calling here with the SAME
// ECX, and the call site inside the (separate, unexamined) function at
// Common.dll.txt:0x0634380f tests `[[either ESI or EBX]+0x78]` (flags again)
// before calling here with that same object as ECX. So item 2789's old
// ported_name was a genuine misattribution, now fixed (see decomp-tracker).
//
// FUN_06349490 (decomp-tracker item 2942) is itself a SimUnit-scoped wake
// pass: ECX=SimUnit (own +0xE/+0x10 = n_elems/elems over an IVP_Core array,
// same layout IVP_Real_Object::EnsureInSimulation and IVP_Core::
// ensure_in_simulation already document), stack arg=Environment (only used
// in FUN_06349490's own FUN_06349460/FUN_06349400 tail, see IVP_Core::
// revive_sleeping_core below -- both of those are now real, ready_unverified
// functions, D-target-2942 pass 2026-09-09). The existing IVP_Core::
// revive_sleeping_core() (this=Core, derives SimUnitPtr = this->OwningSimUnit
// internally) is really this port's model of FUN_06349490 itself, NOT of
// FUN_063450e0 -- reusing it here is correct because it derives the exact
// same SimUnitPtr retail passes as FUN_06349490's ECX (Core->OwningSimUnit).
// It doesn't thread FUN_06349490's Environment stack argument through as an
// explicit parameter, but revive_sleeping_core now reaches the same value via
// this->Environment for its manager lookup (the only place that stack arg is
// actually used), so this is no longer an observable gap -- the sole
// remaining stub in that call graph is FUN_0634b480 (decomp-tracker item
// 2989), which retail calls with no arguments at all.
void IVP_Real_Object::ReviveOriginalCoreIfSleeping()
{
    if ((flags & 0xFF) == 8)
    {
        original_core->revive_sleeping_core();
    }
}

// ============================================================================
// IVP_Real_Object::ApplyLocalCsys (LAB_06344de0, Common.dll.txt:06344de0-
// 06344f06, decomp-tracker item 2785, 100 insns / 297 bytes). D-item2785-wire
// pass (2026-09-10): IVP_Real_Object's real, concrete vtable slot 2,
// identified by a prior pass's full .rdata vtable dump (see item 2817's own
// notes) as the target of the unconditional `Self->vtable[2](&LocalCsys)`
// call inside FUN_06345b20/IVP_InitCoreRestStateAndDamping below. This pass
// settles the ONE open puzzle a prior pass left (the exact shape/type of this
// function's own stack argument, `NewLocalCsys`) and ports the whole function.
//
// NewLocalCsys LAYOUT -- SETTLED. `EBX` (this function's own single stack
// argument) is read at three places:
//   (a) Common.dll.txt:06344e00-06344e05, as `Source` to FUN_063483d0 =
//       IVP_U_Matrix::set_inverse(Dest=local scratch, Source=EBX) -- that
//       method's own parameter type is already a confirmed `const
//       IVP_U_Matrix*` (ivp_math.h), so EBX must be one too.
//   (b) Common.dll.txt:06344e8a/06344e99/06344ea9, as `EBX[0]`/`EBX[0x14]`/
//       `EBX[0x28]` -- EXACTLY IVP_U_Matrix3::rows[0].k[0] / rows[1].k[1] /
//       rows[2].k[2] (the 3x3 rotation's diagonal): each IVP_U_Point row is
//       0x10 bytes, so row i's diagonal element sits at i*0x10 + i*4 = i*0x14
//       -- 0x00/0x14/0x28 for i=0/1/2. This is NOT a stride mismatch against
//       Freelancer's own tightly-packed `class Csys` (what a prior pass
//       suspected) -- it is IVP_U_Matrix's own already-established row
//       layout (ivp_math.h), used identically by every narrow-phase leaf
//       solver in this file (e.g. IVP_TransformWorldToSlotSpace above).
// Independent cross-check via the prior pass's own lead, FUN_063481b0/item
// 2889: it zero-fills a 0x40-byte buffer, then stamps the literal 1.0f
// (0x3f800000) at these SAME three offsets (Common.dll.txt:063481b3-063481c8)
// -- i.e. it builds an IDENTITY IVP_U_Matrix (rotation=identity, vv=0), using
// this exact 0x14-stride diagonal pattern. And DAT_063a53d0 (compared against
// EBX[0]/[0x14]/[0x28] below) decodes byte-for-byte to 0x3f800000 = 1.0f
// (Common.dll.txt:568653-568656, `.rdata:063a53d0 00 00 80 3f`) -- so the
// comparison is a bit-EXACT equality test against FUN_063481b0's own identity
// sentinel, not a numeric epsilon (confirmed via the single `TEST AH,0x40`,
// which isolates only FCOMP's "equal" flag C3 -- no C0/C2 bits are tested).
//
// CONCLUSION: NewLocalCsys is a `const IVP_U_Matrix*` representing
// "Object-relative-to-Core" (this object's local frame, expressed inside its
// owning IVP_Core's own frame). Its INVERSE (computed once, up front, via
// set_inverse) is "Core-relative-to-Object" -- shift_core_f_object is that
// inverse's translation (vv), and q_core_f_object is a quaternion rebuilt
// from that SAME inverse's rotation part (Common.dll.txt:06344edf, `LEA
// EDX,[ESP+0x10]` resolves to the identical local-scratch address set_inverse
// wrote into, NOT NewLocalCsys itself -- traced ESP-relative, offset by
// offset). Testing NewLocalCsys's own (pre-inversion) diagonal for the
// identity fast path instead of the inverse's is safe/equivalent specifically
// because the transpose of an exact identity matrix is bit-identical to
// itself (no rounding introduced) -- retail's choice to test the cheaper,
// already-live EBX pointer instead of the freshly-computed inverse is just an
// optimization, not a different check.
// ============================================================================
void IVP_Real_Object::ApplyLocalCsys(const IVP_U_Matrix* NewLocalCsys)
{
    // Common.dll.txt:06344de7-06344dfa/06344e00-06344e05. Step 1 (the
    // Csys::Csys EH-array-construct call on a local temp) is a confirmed
    // no-op -- Csys::Csys itself writes nothing (Common.dll.txt:06261260,
    // `MOV EAX,this; RET`) -- its stack region is simply reused as raw
    // scratch for set_inverse's own output. `CoreFObject` models that reused
    // scratch directly, skipping the pointless intermediate ctor call.
    IVP_U_Matrix CoreFObject; // "Core, expressed from Object's frame" = inverse(NewLocalCsys)
    CoreFObject.set_inverse(NewLocalCsys);

    // :06344e0a-:06344e28 -- every anchor's core-space point is rebuilt from
    // its object-space point through the new matrix (out = M * in + vv).
    for (IVP_Anchor* Anchor = anchors; Anchor != nullptr; Anchor = Anchor->anchor_next_in_object)
    {
        CoreFObject.vmult4(&Anchor->object_pos, &Anchor->core_pos); // FUN_063484e0
    }

    // Common.dll.txt:06344e2a-06344e8a. shift_core_f_object <-
    // CoreFObject.vv, collapsed to an exact zero (plus a flags bit) when its
    // squared length is below a small epsilon (DAT_063a5438, decoded below --
    // a genuine ~9e-6 epsilon, unlike DAT_063a53d0's exact-1.0f sentinel
    // below).
    const float ShiftEpsilon = BitsToFloat(0x3716feb5); // DAT_063a5438, Common.dll.txt:568740-568743 (B5 FE 16 37)
    // :06344e2a-:06344e51 -- (z*z + y*y) + x*x stays on the FPU; FCOMP eps;
    // TEST AH,0x1; JZ -> the zero branch runs on C0 (less or unordered).
    const double ShiftQuadLen = ((double)CoreFObject.vv.k[2] * CoreFObject.vv.k[2]
                               + (double)CoreFObject.vv.k[1] * CoreFObject.vv.k[1])
                              + (double)CoreFObject.vv.k[0] * CoreFObject.vv.k[0];
    if (!(ShiftQuadLen >= ShiftEpsilon))
    {
        // Common.dll.txt:06344e53-06344e6c. Retail names for these two
        // `flags` (+0x78) bits were not recovered -- kept as raw literals
        // with this citation, per this project's placeholder-field policy.
        flags = (flags & ~0x800u) | 0x400u;
        shift_core_f_object.x = 0.0f;
        shift_core_f_object.y = 0.0f;
        shift_core_f_object.z = 0.0f;
    }
    else
    {
        // Common.dll.txt:06344e6e-06344e83.
        shift_core_f_object.x = CoreFObject.vv.k[0];
        shift_core_f_object.y = CoreFObject.vv.k[1];
        shift_core_f_object.z = CoreFObject.vv.k[2];
        flags &= ~0xc00u;
    }

    // Common.dll.txt:06344e8a-06344ef1. q_core_f_object <- freed/nulled when
    // NewLocalCsys's rotation diagonal is bit-exactly the FUN_063481b0
    // identity sentinel (1.0f at each of rows[0].k[0]/rows[1].k[1]/
    // rows[2].k[2]); otherwise rebuilt from CoreFObject's (the INVERSE's)
    // rotation part and normalized -- see this function's own header comment
    // for why testing NewLocalCsys's pre-inversion diagonal here is safe.
    // FCOMP 1.0f; TEST AH,0x40; JZ -> each test passes on C3, i.e. equal OR
    // unordered (a NaN diagonal counts as identity).
    const float IdentitySentinel = BitsToFloat(0x3f800000); // DAT_063a53d0, exact 1.0f
    if (!(NewLocalCsys->rows[0].k[0] < IdentitySentinel) && !(NewLocalCsys->rows[0].k[0] > IdentitySentinel) &&
        !(NewLocalCsys->rows[1].k[1] < IdentitySentinel) && !(NewLocalCsys->rows[1].k[1] > IdentitySentinel) &&
        !(NewLocalCsys->rows[2].k[2] < IdentitySentinel) && !(NewLocalCsys->rows[2].k[2] > IdentitySentinel))
    {
        ::operator delete(q_core_f_object); // FUN_06391d5a (operator delete; the else branch allocates with operator new)
        q_core_f_object = nullptr;
    }
    else
    {
        if (q_core_f_object == nullptr)
            q_core_f_object = static_cast<IVP_U_Quat*>(::operator new(sizeof(IVP_U_Quat)));
        q_core_f_object->set_from_matrix(&CoreFObject); // IVP_U_Matrix3 base of the INVERSE, Common.dll.txt:06344edf
        q_core_f_object->normize(); // FUN_06350fb0, confirmed IVP_U_Quat::normize (decomp-tracker item 3153)
    }

    // Common.dll.txt:06344ef1-06344eff. Invalidate the narrow-phase cache
    // slot if one was ever resolved for this object.
    if (cache_object != nullptr)
    {
        IVP_Real_Object_Free_Cache_Object(this); // FUN_0634d510
    }
}

// LAB_06344f10 (decomp-tracker item 2786, Common.dll.txt:06344f10-06344f4c,
// 22 insns, kind=vtable_method). D-cluster-ivp-adjacent pass (2026-09-10):
// fully traced. IVP_Real_Object's real, concrete vtable slot 1 -- confirmed
// by its own .rdata XREF triple (PTR_FUN_063a5444/PTR_FUN_063a5828/
// PTR_FUN_063a5860), the SAME 3-entry pattern (transitional-ctor-vtable /
// object_type==2-ctor-chain / object_type==3-ctor-chain) already confirmed
// for ApplyLocalCsys's own slot 2 (item 2785, see that method's header
// comment above). See this method's own header comment (ivp_real_object.h)
// for the declaration-level summary.
void IVP_Real_Object::SetLocalCsysFromQuatAndPosition(const IVP_U_Quat* Orientation, const IVP_U_Point* Position)
{
    // Common.dll.txt:06344f14-06344f24 (`FUN_06350bc0(Orientation,
    // &LocalCsys)` -- IVP_U_Quat::set_matrix, already generic/ported).
    IVP_U_Matrix LocalCsys;
    Orientation->set_matrix(&LocalCsys);

    // Common.dll.txt:06344f24-06344f40. Overwrite the translation row with
    // Position verbatim (set_matrix above only ever fills in the rotation
    // rows).
    LocalCsys.vv.x = Position->x;
    LocalCsys.vv.y = Position->y;
    LocalCsys.vv.z = Position->z;

    // Common.dll.txt:06344f44-06344f45 (`this->vtable[2](&LocalCsys)`): the
    // virtual ApplyLocalCsys (slot 2).
    ApplyLocalCsys(&LocalCsys);
}

// ============================================================================
// FUN_06345b20 (decomp-tracker item 2817, Common.dll.txt:408190-408445, 221
// insns / 786 bytes). Csys/inertia-tensor mass-properties step of
// IVP_Real_Object's real construction chain. D-item2817 dedicated pass
// (2026-09-10) -- full instruction-by-instruction trace of the whole
// function; see below for exactly what is and is not ported.
//
// CALLER CORRECTION: item 2817's own prior notes (and item 3487's own
// wording) call this "called from FUN_06345100" -- WRONG. This function's own
// XREF list (Common.dll.txt:408208: `XREF[3] 06343b07(c),063622e6(c),
// 063625e3(c)`) does not include any address inside FUN_06345100's own
// 0x06345100-0x06345259 range. Cross-checked against FUN_06362570/item 3487's
// body instead (Common.dll.txt:457095-457101, `PUSH EDI; MOV ECX,ESI; CALL
// FUN_06345b20` at address 0x063625e3, squarely inside FUN_06362570's own
// 0x06362570-0x0636261c byte range): the real caller is FUN_06362570 (item
// 3487), calling this as a SIBLING step immediately after its own earlier
// call to FUN_06345100 (item 2790, at 0x063625a9) -- not a child call FROM
// FUN_06345100. param_2 (EDI here) is FUN_06362570's own param_3, ultimately
// forwarded from IVP_Environment::static_object's original caller through
// FUN_0634cdf0/item 3050 -- a construction-time "mass/damping template" this
// port has no independently-recovered retail type for (see
// IVP_RealObjectConstructionTemplate below). The other 2 XREF callers
// (0x06343b07, 0x063622e6) were not traced this pass.
//
// `this` (EBX) is Self, the same IVP_Real_Object FUN_06345100 just built.
// ESI is bound ONCE (Common.dll.txt:408262, `MOV ESI,[EBX+0x98]` = Self->core)
// and stays that IVP_Core* for the rest of the function -- every `[ESI+...]`
// access from that point on targets IVP_Core fields, not IVP_Real_Object's
// own. See ivp_real_object.h's field_94 comment for a related misattribution
// this pass corrected (that field is READ here, at Common.dll.txt:408284, not
// zeroed -- the +0x94/+0x98/+0x9c/+0xa4/+0xa8/+0xac zero-fill at
// Common.dll.txt:408392-408399 is Core->RotSpeed/Speed, a different object).
//
// CONFIRMED AND PORTED THIS PASS (IVP_InitCoreRestStateAndDamping below):
//   - Core->RotSpeed = {0,0,0}, Core->Speed = {0,0,0} (Common.dll.txt:
//     408392-408399, plain XOR/MOV zero-fill of both Vectors -- "a freshly
//     built rigid body starts at rest").
//   - Core->RotSpeedDamp = {Template+0x3c, Template+0x40, Template+0x44},
//     Core->SpeedDamp = Template+0x38 (Common.dll.txt:408377-408390,
//     confirmed against ivp_core.h's own already-documented RotSpeedDamp/
//     SpeedDamp fields at those SAME +0x24/+0x44 offsets).
//   - Core->UpperLimitRadius = Self->field_94 + <switch(Self->object_type)>,
//     Core->SpeedLimit = <switch(Self->object_type)> (Common.dll.txt:
//     408224-408293, via FUN_06346870/item 2837). This identifies FUN_06346870
//     as a trivial Core->UpperLimitRadius(+0x4)/SpeedLimit(+0x8) 2-field
//     setter, resolving that item's own "owning class not identified" open
//     question -- the owner IS IVP_Core (`*(int*)(outer+0x98)` in that item's
//     notes was always Self->core). The object_type==3 branch is
//     independently confirmed LIVE for the traced call chain (FUN_06362570
//     unconditionally sets Self->object_type = 3 right before calling this
//     function, Common.dll.txt:457091 `MOV [ESI+4],0x3`).
//
//     object_type==2 (D-surfacemanager-dynamic-chain pass, 2026-09-10): NOW
//     WIRED for real. This branch needs a virtual call through Self->
//     surface_manager's own vtable slot 2 (Common.dll.txt:408274-408283) --
//     surface_manager's concrete CLASS is still not named, but this pass
//     traced its full CONSTRUCTION chain on the object_type==2/"dynamic real
//     object" path (the twin chain a prior D-surfacemanager-vtable pass
//     flagged but could not close, see item 2899's own notes) far enough to
//     resolve everything this call site needs:
//
//       CONSTRUCTION CHAIN (outside the 0x0634xxxx-0636xxxx IVP cluster
//       proper, in the higher-level PhySys/DACOM object-factory region --
//       read-only tracing here, no edits made to that region, consistent
//       with this project's forbidden-zone guidance): at each of the 3
//       call sites that build a "dynamic real object" (Common.dll.txt XREF
//       0x0628af7e/0x0628b367/0x0628c838, all `CALL FUN_0634cd80`), a small
//       8-byte object is built just beforehand via `operator_new(8)`
//       (Common.dll.txt:0628aed9-0628aef9): `[obj+0x0] = &PTR_LAB_0639c2dc`
//       (a real, multi-slot .rdata vtable -- see below), `[obj+0x4] =
//       <outer object>->+0x8` (an opaque back-pointer this pass calls
//       "Inner", never itself resolved to a named class). FUN_0634cd80 is
//       structurally IDENTICAL to FUN_0634cdf0/item 3050 (same operator_new
//       (0xA8)+FUN_0634cb90-id-lookup+delegate-to-a-"FUN_06362570-twin"
//       shape), confirming item 3166's own "twin of FUN_06362570" finding
//       for its own delegate, FUN_06362280 (a twin of FUN_06362570/item
//       3487, vtable PTR_FUN_063a5824, Self->object_type=2). Re-deriving
//       BOTH functions' own stack-argument-to-callee-parameter mapping
//       (the exact "stack-slot-index chain" a prior pass could not close --
//       the key insight was that FUN_0634cb90 TAIL-JUMPS into BaseData::
//       get_base_id, popping none of its own caller's stack args, which
//       silently leaves 4 dwords on the stack for the SUBSEQUENT call to
//       reuse as ITS OWN trailing stack params) shows the 8-byte object
//       built above flows through unchanged as FUN_0634cd80's own param_1 ->
//       FUN_06362280's own param_2 -> FUN_06345100's own param_2, i.e.
//       EXACTLY the `SurfaceManager` constructor argument item 2790 already
//       stores verbatim into IVP_Real_Object::surface_manager (+0x80,
//       Common.dll.txt:06345185). This settles, with a concrete construction
//       site, that surface_manager on this path is genuinely that 8-byte
//       vtable-bearing adapter (or null, if its own operator_new(8) failed --
//       matches this function's own existing null-checked dispatch pattern).
//
//       VTABLE (PTR_LAB_0639c2dc, Common.dll.txt:558690, dumped straight
//       from .rdata): slot0=LAB_0634ad40, slot1=LAB_0634ac70,
//       slot2=LAB_0634acb0, slot3=LAB_0634ac90, slot4=FUN_0634ab90,
//       slot5=FUN_0634ac50, slot6=LAB_0634ab60 (7 real function targets, all
//       read this pass). Every one of these begins `MOV EAX,[ECX+4]` (i.e.
//       reads the adapter's own "Inner" field first) -- slot1 (LAB_0634ac70,
//       Common.dll.txt:0634ac70-0634ac87) copies `*(Vector*)(Inner+0x0)` to
//       the caller's output pointer (this is ALREADY the confirmed target of
//       this function's own earlier LocalCsys.vv translation branch above,
//       cross-checking clean); slot3 (LAB_0634ac90, Common.dll.txt:0634ac90-
//       0634acab) copies `*(Vector*)(Inner+0xc)` (a SECOND, separate 3-float
//       field on Inner) the same way -- used by the RotInertia "box" shape
//       sub-case (see the RotInertia/Mass writeup below, now also resolved
//       and wired); slot2 (LAB_0634acb0, Common.dll.txt:0634acb0-
//       0634ad2f) is THIS branch's own target, decoded in full below. Slots
//       0/4/5/6 (LAB_0634ad40/FUN_0634ab90/FUN_0634ac50/LAB_0634ab60) do more
//       involved Inner-relative geometry work (nested-list walks, point/box
//       queries via FUN_0634a9b0/FUN_0634a500/FUN_0634a4e0/FUN_0635abf0) but
//       are not called anywhere in FUN_06345b20/this function, so were not
//       traced further -- Inner's own concrete class remains unidentified,
//       consistent with this project's placeholder-field policy (only the
//       vtable INDEX and calling convention are load-bearing here, not
//       Inner's own layout).
//
//     See IVP_InitCoreRestStateAndDamping's own object_type==2 branch below
//     for how slot 2's exact calling convention (this=surface_manager,
//     in=&LocalCsys.vv, out1/out2=&MaxAngularSpeedAddend/&SpeedLimitValue)
//     was pinned down and wired -- this ALSO required moving the LocalCsys
//     construction + ApplyLocalCsys call ahead of this switch (retail's own
//     real order, Common.dll.txt:06345b2a-06345bd9 precede 06345be2), since
//     LocalCsys.vv is a live input to this branch, not just an output
//     consumed later.
//
// Core->RotInertia / Core->Mass (Common.dll.txt:408294-408377): D-rotinertia-
// selector pass (2026-09-10) -- NOW FULLY RESOLVED AND PORTED. This was the
// item's last remaining gap; see below for the resolution, then the "NOW
// WIRED" world-transform writeup further down for the rest of this function.
//
// PRIOR PASSES had already narrowed this down to exactly one open question:
// the origin of the shape-selector value read from `[ESP+0x1c]`
// (Common.dll.txt:408307), used to pick between the "box" and "sphere"
// RotInertia sub-formulas. No instruction between the function's start and
// 408307 was seen writing to that physical stack slot under the spot-checks
// prior passes ran, so it was left an open TODO calling for "a full
// single-step stack simulation of this function's first ~100 instructions."
//
// RESOLVED this pass by doing exactly that -- a literal instruction-by-
// instruction physical-ESP simulation from the prologue (Common.dll.txt:
// 06345b20, `SUB ESP,0xd8` + 4 PUSHes establishing baseline PB) all the way
// to 408307, tracking every PUSH/POP/CALL(+RET N)/SUB ESP/ADD ESP and
// confirming every intervening CALL is self-cleaning (each one's own stack
// args are popped by its own RET N, restoring ESP to PB before the next
// instruction) -- the SAME constant-baseline method prior passes already
// validated for the rest of this function, just extended further back and
// checked exhaustively rather than spot-checked. Finding: `[ESP+0x1c]`
// (physical PB+0x1c) IS written, at Common.dll.txt:06345b6e (`MOV
// [ESP+0x1c],EBP`) -- MUCH earlier in the function than either the earlier
// UpperLimitRadius/SpeedLimit switch (408263, `MOV EAX,EBP; SUB EAX,2; ...`)
// or this RotInertia switch (408307) themselves. EBP is loaded once, at
// Common.dll.txt:06345b4b (`MOV EBP,[EBX+4]`) == Self->object_type, and nothing
// between 06345b4b and 06345b6e touches EBP, so this stack write really does
// store object_type to PB+0x1c. No instruction anywhere between 06345b6e and
// 408307 writes to PB+0x1c again (independently re-checked, both switch arms
// -- object_type==2 and object_type==3 -- and their shared tail at
// LAB_06345c24). So `[ESP+0x1c]` at 408307 is simply a STACK COPY of
// Self->object_type, stashed early specifically so it survives EBP being
// repurposed: at Common.dll.txt:06345c67 (`LEA EBP,[ESI+0x14]`, 3 instructions
// before the 408307 read), EBP stops being object_type and becomes
// `&Core->RotInertia` for the rest of the function -- retail needed
// object_type a second time (for this RotInertia switch) AFTER already
// giving EBP a new job, so it reads its own earlier stack stash instead of
// re-deriving `Self->object_type` from memory a second time. This also
// explains why this selector's own switch shape (`SUB EAX,2; JZ box; DEC
// EAX; JZ sphere; else trap`) is byte-identical to the earlier object_type
// switch at 408263-268 -- it is testing the exact same 2-valued (2 or 3)
// discriminator, just read from a different location.
//
// With the selector resolved, the whole block was traced and ported
// (IVP_InitCoreRestStateAndDamping below), including the normalize/clamp
// tail (Common.dll.txt:408354-408377, previously left as a separate "NEXT
// LANE" item) since it is a direct, mechanical continuation once RotInertia
// itself is known:
//   - MASS (Common.dll.txt:408294-408301): `MassValue = (Template->MassInput
//     < DAT_063a545c) ? 1.0f : Template->MassInput` -- clamp to a minimum of
//     1.0f. DAT_063a545c decodes to a small positive epsilon (bytes A6 9B 44
//     3B, Common.dll.txt:568764-568769), not independently named. Stored to
//     Core->Mass (Core+0x20) at the very end of the block (408377, `MOV
//     [EBP+0xc],EAX` where EBP==&Core->RotInertia==Core+0x14).
//   - ROTINERTIA SHAPE SWITCH (Common.dll.txt:408303-408353): if
//     `Template->RotInertiaShapePtr == nullptr` (only nullness is load-
//     bearing -- the pointee is never dereferenced in this function, so its
//     own layout stays unidentified, per this project's placeholder-field
//     policy), RotInertia is copied directly, UNSCALED, from
//     Template->InertiaBoxVector (Common.dll.txt:408344-408352, LAB_06345ce4).
//     Otherwise, both remaining sub-cases fill a 3-float `scratch` buffer and
//     then fall into a SHARED tail (Common.dll.txt:408328-408342,
//     LAB_06345cb0) that componentwise-multiplies `scratch` by
//     Template->InertiaBoxVector and by MassValue:
//       - selector==2 ("box"): `scratch = Self->surface_manager->vtable[3]
//         (&scratch)`, i.e. `*(Vector*)(Inner+0xc)` per this function's own
//         header-comment vtable dump (slot3=LAB_0634ac90) -- same
//         surface_manager-adapter class already confirmed live for the
//         object_type==2 UpperLimitRadius/SpeedLimit branch above.
//       - selector==3 ("sphere"): `scratch = {r*r*0.4f, r*r*0.4f, r*r*0.4f}`
//         (Common.dll.txt:408315-320) where `r` is read from PB+0x10 -- which,
//         tracing that SAME physical slot back through 408286-293, is
//         EXACTLY the value FUN_06346870 (item 2837) just stored into
//         Core->UpperLimitRadius a few instructions earlier in this same
//         function (re-derived: PB+0x10 is written once, at 408289 `FSTP
//         [ESP+0x14]` after the `PUSH ECX` at 408287 shifts the physical
//         offset from 0x24 to 0x14... i.e. physical PB+0x10, matching the
//         `[ESP+0x14]` display at that push depth -- holding `Self->field_94
//         + MaxAngularSpeedAddend`, the exact sum FUN_06346870 receives as
//         its own UpperLimitRadius argument, and untouched between that call
//         and 408315). DAT_063a5458 decodes to EXACTLY 0.4f (bytes CD CC CC
//         3E, Common.dll.txt:568759-568763) -- i.e. the classic solid-sphere
//         moment-of-inertia coefficient 2/5 -- strong independent
//         confirmation this sub-case really is "isotropic sphere-like
//         inertia from an effective radius", corroborating the selector
//         resolution above rather than just being consistent with it.
//       - anything else traps (408312, same `MOV [DAT_00000000],0`
//         assert-fail idiom as the object_type switch's own default case --
//         unreachable given object_type is already asserted to be 2 or 3
//         earlier in this same function).
//   - NORMALIZE/CLAMP TAIL (Common.dll.txt:408354-408377): if
//     `Template->RotInertiaClampFactor == 0.0f` (DAT_063a53d4, the confirmed
//     exact-zero sentinel already used elsewhere in this port), skip
//     straight to the Mass store. Otherwise: `Threshold = |Core->RotInertia|
//     * RotInertiaClampFactor` (magnitude via the already-ported
//     FUN_06347e60/VectorLength math, item 2879, inlined here matching this
//     port's existing convention of inlining that one-line formula rather
//     than calling a cross-file static helper -- see
//     ivp_impact_solver.cpp's own VectorLength), then each of RotInertia's 3
//     components is raised to Threshold if currently smaller (Common.dll.txt:
//     06345d1d-2c, a 3-iteration loop over &RotInertia.x/.y/.z) -- an
//     "inertia floor" stability clamp.
//
// New IVP_RealObjectConstructionTemplate fields added for this (ivp_real_
// object.h): MassInput (+0x1c), RotInertiaShapePtr (+0x20), InertiaBoxVector
// (+0x24..+0x2c), RotInertiaClampFactor (+0x34) -- see that header's own
// field comments for the full citation trail. +0x30 remains a genuine,
// unread (by this function) gap, left unmodeled per this project's
// placeholder-field policy.
//   - Core->PosWorldFCore / Core->MWorldFCore / Core->QWorldFCore /
//     Core->QWorldFCoreNext (Common.dll.txt:408400-408439, via FUN_063474d0/
//     item 2861, 35 insns). UPDATE (D-worldtransform pass, 2026-09-10): the
//     whole leaf chain is now ported and real, no longer a gap in itself --
//     FUN_063474d0 is IVP_Core::init_world_transform_from_local_csys
//     (ivp_core.h/.cpp), FUN_063481d0/item 2890 is
//     IVP_U_Matrix::mmult4 (ivp_math.h, a real 3x3-matrix-times-
//     matrix concatenation, NOT "set rotation part leaving vv untouched" as
//     an earlier pass's notes guessed), and FUN_06350ca0/item 3151 turned out
//     to be a byte-exact match for the ALREADY-existing generic
//     IVP_U_Quat::set_from_matrix (ivp_math.h) -- no new code needed for that
//     one. See ivp_core.cpp's own comment above
//     init_world_transform_from_local_csys for the full field-by-field trace.
//
//     NOW WIRED this pass (D-item2785-wire, 2026-09-10): Self->vtable[2] is
//     confirmed to be LAB_06344de0 = IVP_Real_Object::ApplyLocalCsys (see that
//     method's own header comment above, decomp-tracker item 2785), so the
//     virtual call is no longer a blocker. Retail's LocalCsys argument
//     (`local_80`) is built at Common.dll.txt:06345b2a-06345bd7 -- re-traced
//     precisely this pass, and it is NOT a bare "hardcoded identity
//     quaternion" as an earlier pass's notes assumed:
//       - ROTATION: genuinely always identity -- a local IVP_U_Quat{0,0,0,1}
//         (Common.dll.txt:06345b4e-06345b6e, unconditional zero/one field
//         init) is run through FUN_06350bc0/IVP_U_Quat::set_matrix (item
//         3150) into local_80's rotation rows (Common.dll.txt:06345ba1-baa).
//       - TRANSLATION (vv): NOT always zero -- Common.dll.txt:06345b46-b72
//         branches on `Template->field_0x50` (now IVP_RealObjectConstruction
//         Template::OptionalComOverride, ivp_real_object.h): if non-null, its
//         own +0x30/+0x34/+0x38 float triple is copied straight into
//         local_80.vv (Common.dll.txt:06345b74-b8b, no surface_manager
//         involved); if null, `Self->surface_manager->vtable[1](&local_80.vv)`
//         is called instead (Common.dll.txt:06345b91-b9e, thiscall
//         this=surface_manager, one stack arg = the 3-float output pointer --
//         confirmed byte-exact from the raw instructions, even though
//         surface_manager's own concrete class is still unidentified -- same
//         as the RotInertia/Mass block's own vtable slot 3 dispatch below
//         (unidentified class, but concretely wired). Ported both branches:
//         the non-null one directly, the null
//         one via a raw opaque-vtable dispatch matching this port's existing
//         convention for unidentified interface candidates (e.g.
//         IVP_Hull_Manager::WalkActiveEntries above) -- the vtable INDEX and
//         calling convention are concretely confirmed from the disassembly
//         even though surface_manager's class name/full layout is not, so
//         this is not a layout guess.
//     After Self->ApplyLocalCsys(&LocalCsys), retail immediately calls
//     `Core->init_world_transform_from_local_csys(&LocalCsys)` with the SAME
//     (pre-inversion) LocalCsys (Common.dll.txt:06345d93-9a, `PUSH &local_80`
//     -- confirmed the identical local, not a different buffer, per
//     IVP_Core::init_world_transform_from_local_csys's own header comment,
//     ivp_core.cpp) -- both now wired below. Structurally decoupled from the
//     RotInertia/Mass block (only reads the local Csys and Core->MWorldFCore's
//     own prior value, never Core->RotInertia/Mass).
//
// With RotInertia/Mass ALSO now resolved and wired (D-rotinertia-selector
// pass, 2026-09-10 -- see above), the ONLY remaining gap in this function is:
//   - The Common.dll.txt:408400-408439 tail also computes a {DeltaTime,
//     InvDeltaTime} pair from an Environment-shaped struct at +0xf8/+0xfc/
//     +0x100/+0x104 (NOT Template itself -- ECX is reloaded at 408405 `MOV
//     ECX,[ESP+0xec]` from the original param_2/EDI value spilled at
//     function entry, Common.dll.txt:408221) and calls the already-ported
//     Core::synchronize_simulation_time_snapshot (FUN_06346e70/item 2850)
//     with it; the divide/fallback logic (Common.dll.txt:408405-408436)
//     itself was not re-derived precisely enough to commit safely this pass.
//     This is the sole reason this item's overall status stays `partial`
//     rather than `ready_unverified`.
//
// See decomp-tracker item 2817 for status and item 3050's own notes for the
// wider IVP_Environment::static_object chain this sits in.
// ============================================================================

// FUN_06362570's own param_3 (this function's param_2, EDI) -- a
// construction-time "mass/damping template" struct passed down from
// IVP_Environment::static_object's original caller through FUN_0634cdf0/item
// 3050. NOT a retail type name (no IVP reference source in this port).
// D-hullmanager-outer-wire pass (2026-09-10): this is the SAME struct
// IVP_Real_Object's own constructor (FUN_06345100/item 2790) reads its own
// +0x00/+0x0c/+0x10/+0x18/+0x54 fields from (confirmed same pointer -- see
// FUN_06345b20's own "CALLER CORRECTION" comment above) -- now declared once,
// as IVP_RealObjectConstructionTemplate, in ivp_real_object.h so both
// constructors can share it. The +0x38/+0x3c/+0x40/+0x44 fields
// IVP_InitCoreRestStateAndDamping below reads are that same shared struct's
// SpeedDampInput/RotSpeedDampInputX/Y/Z members, PLUS (D-rotinertia-selector
// pass, 2026-09-10) MassInput/RotInertiaShapePtr/InertiaBoxVector/
// RotInertiaClampFactor at +0x1c/+0x20/+0x24/+0x34 -- see FUN_06345b20's own
// comment above for the full offset inventory. +0x50 (OptionalComOverride)
// is likewise already modeled. Only +0x30 remains a real, unmodeled gap.

// FUN_06345b20's ported body -- see the header comment block above
// IVP_RealObjectConstructionTemplate for the full evidence trail. D-core-
// init-tail pass (2026-09-10): the function's LAST remaining gap (the
// DeltaTime/InvDeltaTime tail, Common.dll.txt:06345d9f-06345e20) is now
// closed -- see that block's own comment, right before the
// Core->synchronize_simulation_time_snapshot() call at the end of this
// function, for the full resolution (the mystery ECX reload there is
// Self->environment, not a Template reload -- confirmed against all 3 of
// this function's own call sites). Every field/step of this function is now
// real: Core->RotSpeed/Speed/RotSpeedDamp/SpeedDamp, UpperLimitRadius/
// SpeedLimit, LocalCsys/ApplyLocalCsys, RotInertia/Mass/RecomputeInverseInertia,
// the world-transform call, and now DeltaTime/InvDeltaTime. Still NOT wired
// into any caller yet -- that is item 3487/FUN_06362570's own job (see this
// file's IVP_Real_Object::IVP_Real_Object comment above and
// IVP_Ball::IVP_Ball further down this file for that wiring).
// ============================================================================
// IVP_SurfaceManager / IVP_SurfaceManager_Default -- real implementations.
//
// D-claude-common-ivp pass (2026-09-17). REPLACES the hand-rolled
// {VTable, Inner} pair with a 10-entry fake vtable that used to live here in
// an anonymous namespace: IVP_SurfaceManager is a real class now (see
// ivp_surface_manager.h for the complete, byte-extent-checked geometry of
// both tables and for the per-slot evidence). Everything below is defined at
// file scope, NOT inside an anonymous namespace -- defining a header-declared
// class's members in one produces C2888.
//
// The bodies that hold retail's deliberate `MOV dword ptr [0],0` call
// IVP_CoreTrap (ivp_utility/ivp_core_trap.h), a real null store.
// ============================================================================

// The five-instruction append both IVP_SurfaceManager_Default slots 4 and 5
// open-code (.text:06362433-0636245b / 0636246f-06362492): count >= capacity
// (signed, `CMP EAX,[ESI]; JL`) -> FUN_0635d3e0 (IVP_U_BigVector_Grow); then
// elems[count] = Element and count + 1. No null tests.
static void IVP_SurfaceManagerOutListAppend(IVP_SurfaceManager_OutList* OutList, void* Element)
{
    if (OutList->Count >= OutList->Capacity)
        IVP_U_BigVector_Grow(OutList);

    OutList->Elems[OutList->Count] = Element;
    ++OutList->Count;
}

// The abstract base's own destructor. Pure virtual, but with a real body --
// FUN_0635d440 (Common.dll.txt:0635d440-0635d446) does nothing but reinstall
// the base vptr and return, which is what a compiler-generated base
// destructor epilogue looks like and is reproduced implicitly here.
IVP_SurfaceManager::~IVP_SurfaceManager()
{
}

// Slots 7 and 8 -- the one shared body at 062ed730 (Common.dll.txt:253999),
// a bare `RET 4`. Present in BOTH tables at the same address, i.e. a
// base-provided default that IVP_SurfaceManager_Default inherits unchanged.
void IVP_SurfaceManager::add_reference_to_ledge(void* /*Param1*/)
{
}

void IVP_SurfaceManager::remove_reference_to_ledge(void* /*Param1*/)
{
}

// FUN_06362370 (Common.dll.txt:456769-456808). Installs the derived vtable
// (done by the compiler here), then allocates the 0x20-byte block at 16-byte
// alignment through FUN_06343fc0(0x20, 0x10) and zero-fills it with
// `XOR EAX,EAX; MOV ECX,8; REP STOSD`.
// The REP STOSD runs on the returned pointer with no null test (063623a5-063623b4).
IVP_SurfaceManager_Default::IVP_SurfaceManager_Default()
{
    GeometryBlock = ivp_malloc_aligned(kIVPSurfaceManagerGeometryBlockSize,
                                       kIVPSurfaceManagerGeometryBlockAlign); // 063623a0 FUN_06343fc0
    std::memset(GeometryBlock, 0, kIVPSurfaceManagerGeometryBlockSize);
}

// FUN_063624e0 (Common.dll.txt:456984-457010). Frees the block through
// FUN_06343ff0 and nulls the field, then the base destructor runs.
// Retail's scalar-deleting wrapper FUN_063624c0 (Common.dll.txt:456962-
// 456973) is slot 9 itself and is what the compiler emits for a virtual
// destructor, so it needs no separate body here.
IVP_SurfaceManager_Default::~IVP_SurfaceManager_Default()
{
    if (GeometryBlock != nullptr) // 06362503-06362510
    {
        ivp_free_aligned(GeometryBlock); // 06362513 FUN_06343ff0
        GeometryBlock = nullptr;
    }
}

// Slot 0 (Common.dll.txt:209992-209993, `MOV EAX,[this+4]; RET`).
void* IVP_SurfaceManager_Default::get_single_convex()
{
    return GeometryBlock;
}

// Slot 1 (Common.dll.txt:063623d0-063623de). Zeroes three consecutive floats
// through the hidden return pointer and returns that pointer. This is the one
// slot this port's own callers dispatch through today
// (IVP_InitCoreRestStateAndDamping below, for the "no explicit
// centre-of-mass override" path).
float* IVP_SurfaceManager_Default::get_mass_center(float* OutVec3)
{
    OutVec3[0] = 0.0f;
    OutVec3[1] = 0.0f;
    OutVec3[2] = 0.0f;
    return OutVec3;
}

// Slot 2 (LAB_063623f0, Common.dll.txt:456848-456850): nothing but the
// null-store and `RET 0xc`.
void IVP_SurfaceManager_Default::get_radius_and_radius_dev_to_given_center(void* /*Param1*/, void* /*Param2*/, void* /*Param3*/)
{
    IVP_CoreTrap(); // 063623f0
}

// Slot 3 (LAB_0634dc00, Common.dll.txt:423084-423086): null-store, `RET 4`.
// NOTE the 4 XREFs on that label -- it is shared by several unrelated vtables
// (063a55a4, 063a55b8, 063a583c, 063a58e0), i.e. ICF folding again, so it
// proves nothing beyond "this slot traps".
void IVP_SurfaceManager_Default::get_rotation_inertia(void* /*Param1*/)
{
    IVP_CoreTrap(); // 0634dc00
}

// Slot 4 (LAB_06362400, .text:06362400-0636245c, RET 0x18). |c|^2 =
// (x*x + y*y) + z*z and Radius*Radius both stay on the x87 stack (double
// here); FCOMPP compares R^2 (ST0) with |c|^2, TEST AH,0x1; JNZ skips the
// append on less OR unordered. The appended element is this manager's own
// GeometryBlock (`MOV EDI,[ECX+4]` at 0636243c) onto the list in argument 6.
void IVP_SurfaceManager_Default::get_all_ledges_within_radius(const float* Center3, float Radius,
                                                           void* /*Param3*/, void* /*Param4*/,
                                                           void* /*Param5*/,
                                                           IVP_SurfaceManager_OutList* OutList)
{
    const double DistanceSq = ((double)(Center3[0]) * Center3[0] +
                               (double)(Center3[1]) * Center3[1]) +
                              (double)(Center3[2]) * Center3[2];
    const double RadiusSq = (double)(Radius) * Radius;
    if (!(RadiusSq >= DistanceSq))
        return;

    IVP_SurfaceManagerOutListAppend(OutList, GeometryBlock);
}

// Slot 5 (FUN_06362460, Common.dll.txt:456899-456924). Retail runs the
// null-store FIRST and then still performs the same unconditional append slot
// 4 does -- i.e. the trap is reached before the useful work, so this slot is
// effectively dead in retail. Both halves are reproduced, in retail's order.
void IVP_SurfaceManager_Default::get_all_terminal_ledges(IVP_SurfaceManager_OutList* OutList)
{
    IVP_CoreTrap(); // 06362465
    IVP_SurfaceManagerOutListAppend(OutList, GeometryBlock);
}

// Slot 6 (LAB_063624a0, Common.dll.txt:456938-456939): null-store, `RET 8`.
void IVP_SurfaceManager_Default::insert_all_ledges_hitting_ray(void* /*Param1*/, void* /*Param2*/)
{
    IVP_CoreTrap(); // 063624a0
}

// Slot 10 (Common.dll.txt:063624b0-063624b5, `MOV EAX,1; RET`). ICF-folded
// under the name "Archetype::EqObj::get_class_type"; the body is the
// constant 1.
int32_t IVP_SurfaceManager_Default::get_type()
{
    return 1;
}

// DAT_0658afe0 (Common.dll.txt:2283120). Retail constructs it from a CRT
// static initializer and destroys it from an atexit thunk; a file-scope
// object with a constructor and destructor is exactly that.
IVP_SurfaceManager_Default g_IVP_DefaultSurfaceManager_0658afe0;

// Port-only accessor (retail has no getter -- see ivp_surface_manager.h).
IVP_SurfaceManager* IVP_GetDefaultSurfaceManager()
{
    return &g_IVP_DefaultSurfaceManager_0658afe0;
}

// ============================================================================
// The two IVP_Real_Object leaves -- IVP_Polygon (object_type 2) and IVP_Ball
// (object_type 3). D-claude-common-ivp pass (2026-09-17); class-shape defect
// 3b. See their declarations in ivp_real_object.h for the vtable geometry,
// the naming caveat and the reason both are non-polymorphic.
//
// Both constructors have the SAME five-step skeleton in retail:
//   1. CALL FUN_06345100 (the shared base constructor) with five arguments
//   2. store their own vtable at [this+0]
//   3. store their own object_type at [this+4]
//   4. write [this+0x94], then CALL FUN_06345b20
//      (IVP_InitCoreRestStateAndDamping)
//   5. build an {Environment*, IVP_Real_Object*} record on the stack and
//      CALL FUN_0634cea0 on Core->Environment (object_listeners slot 1)
// Only steps 2/3 and the arithmetic inside step 4 differ.
//
// STEP 5's ARGUMENT, in detail, because it is easy to misread. Retail
// (Common.dll.txt:063622eb-06362304 for IVP_Polygon,
// 063625e8-06362601 for IVP_Ball):
//     MOV EAX,[ESI+0x98]     ; EAX = this->core
//     MOV EDX,[EAX+0xc]      ; EDX = Core->Environment (IVP_Core+0x0C)
//     LEA ECX,[ESP+0x10]
//     MOV [ESP+0x10],EDX     ; record field 0 = Environment
//     MOV [ESP+0x14],ESI     ; record field 1 = this
//     PUSH ECX
//     MOV ECX,[EAX+0xc]      ; receiver = the same Environment
//     CALL FUN_0634cea0
// That two-dword record is byte-for-byte the one FUN_0634b480 (item 2989)
// builds for the slot-2 sibling, already modeled here as
// IVP_Environment_Object_Notify_Event (ivp_environment.h) -- confirmed by
// comparing against that function's own identical
// `LEA EAX,[ESP+0x10]; MOV [ESP+0x10],ECX; MOV [ESP+0x14],EDI` sequence at
// Common.dll.txt:0634b566-0634b56f. Note [EAX+0xc] here is
// IVP_Core::Environment, NOT IVP_Environment::controller_manager (which also
// lives at +0x0C of its own class) -- EAX is loaded from this->core at +0x98
// one instruction earlier.
// ============================================================================

// FUN_06362280 (.text:06362280-0636231c, RET 0x14).
IVP_Polygon::IVP_Polygon(IVP_Cluster* Father, IVP_SurfaceManager* SurfaceManager,
                         const IVP_RealObjectConstructionTemplate* Parms,
                         const IVP_U_Quat* InitialOrientationOrNull,
                         const IVP_U_Point* InitialPosition)
    : IVP_Real_Object(Father, SurfaceManager, Parms, InitialOrientationOrNull, InitialPosition) // :063622b1-:063622bc
{
    // :063622c1 -- vptr 063a5824 (the compiler's).

    // Common.dll.txt:063622c7.
    object_type = 2;

    // Common.dll.txt:063622ce-063622d1 (`MOV EAX,[EDI+0x4c]; MOV
    // [ESI+0x94],EAX`). A plain dword copy here -- unlike IVP_Ball below,
    // which adds a radius to the same source value.
    field_94 = Parms->Field_0x4c;

    // Common.dll.txt:063622d7-063622e6. Retail pushes TWO stack arguments,
    // `[Father+0x18]` (the father cluster's environment) then the template;
    // the callee reads the template at :06345b3f and the environment at
    // :06345d9f.
    IVP_InitCoreRestStateAndDamping(this, Father->environment, Parms);

    // :063622eb-:06362304 -- 0634cea0 on core->environment with a stack
    // {core->environment, this}; no null tests.
    IVP_Environment_Object_Notify_Event Event;
    Event.Env = core->Environment;
    Event.Object = this;
    IVP_Environment_Notify_Object_Listeners_Slot1(core->Environment, &Event);
}

// FUN_06362570 (.text:06362570-06362619, RET 0x14).
IVP_Ball::IVP_Ball(IVP_Cluster* Father, const float* Radius,
                   const IVP_RealObjectConstructionTemplate* Parms,
                   const IVP_U_Quat* InitialOrientationOrNull,
                   const IVP_U_Point* InitialPosition)
    // Common.dll.txt:063625a0 (`PUSH 0x0`): retail passes a NULL surface
    // manager to the base constructor and overwrites the field a few
    // instructions later, below.
    : IVP_Real_Object(Father, nullptr, Parms, InitialOrientationOrNull, InitialPosition)
{
    // :063625b2 -- vptr 063a585c (the compiler's).

    // Common.dll.txt:063625b8.
    object_type = 3;

    // Common.dll.txt:063625c4. THE reason IVP_SurfaceManager had to become a
    // real class: this is the only consumer of the module's default instance
    // anywhere in Common.dll.
    surface_manager = &g_IVP_DefaultSurfaceManager_0658afe0; // +0x80

    // Common.dll.txt:063625ae/063625bf-063625d1 (`MOV EDX,[ESP+param_2]; FLD
    // [EDI+0x4c]; FADD [EDX]; FSTP [ESI+0x94]`). Real float addition here,
    // unlike IVP_Polygon's plain dword copy above.
    field_94 = Parms->Field_0x4c + *Radius;

    // Common.dll.txt:063625d7-063625e3. Same two-argument call as
    // IVP_Polygon's; see that comment.
    IVP_InitCoreRestStateAndDamping(this, Father->environment, Parms);

    // :063625e8-:06362601 -- as in IVP_Polygon; no null tests.
    IVP_Environment_Object_Notify_Event Event;
    Event.Env = core->Environment;
    Event.Object = this;
    IVP_Environment_Notify_Object_Listeners_Slot1(core->Environment, &Event);
}

// FUN_06345b20 (.text:06345b20-06345e2f, thiscall on Self, RET 8). Stack
// arguments (Environment, Template): Template is reloaded at :06345b3f, the
// environment at :06345d9f for the time tail. The 2026-10-14 x87 pass
// (claude-code:cmn-s2-ivp2) restored retail's statement order -- the damping
// copies and the RotSpeed/Speed zeroing come after the Mass store
// (:06345d37-:06345d88) -- and the double intermediates noted below.
void IVP_InitCoreRestStateAndDamping(IVP_Real_Object* Self, IVP_Environment* Environment,
                                     const IVP_RealObjectConstructionTemplate* Template)
{
    // ------------------------------------------------------------------
    // Common.dll.txt:06345b2a-06345bd9. LocalCsys construction + the
    // Self->vtable[2]/ApplyLocalCsys call. D-surfacemanager-dynamic-chain
    // pass (2026-09-10): MOVED ahead of the UpperLimitRadius/SpeedLimit
    // switch below, to match retail's own real instruction order. An
    // earlier pass's restructuring (grouping "independent" pieces together
    // for readability) had put the switch BEFORE LocalCsys existed in this
    // port -- harmless while the object_type==2 branch was still an
    // all-zero TODO, but WRONG now that this pass wires that branch to a
    // real surface_manager call that needs LocalCsys.vv as a live input
    // (see the object_type==2 branch's own comment below for the evidence
    // that ties the two together). See this function's own header comment
    // ("NOW WIRED this pass") for the rest of the LocalCsys evidence trail.
    // ------------------------------------------------------------------
    IVP_U_Matrix LocalCsys; // :06345b3a, the rows' trivial constructor (eh vector ctor, 3 x 0x10)
    const int32_t ObjectType = Self->object_type; // :06345b4b EBP, kept in [ESP+0x1c] for both switches

    // Rotation: identity (:06345b4e-:06345b66, a local IVP_U_Quat{0,0,0,1}),
    // run through IVP_U_Quat::set_matrix at :06345baa -- after the translation
    // below is fetched. set_matrix writes the three rows only.
    IVP_U_Quat IdentityLocalQuat{0.0f, 0.0f, 0.0f, 1.0f};

    // Translation: Template->OptionalComOverride's own +0x30/+0x34/+0x38
    // triple when present, else Self->surface_manager->vtable[1].
    if (Template->OptionalComOverride != nullptr)
    {
        // Common.dll.txt:06345b74-b8b.
        const IVP_U_Point* Override = reinterpret_cast<const IVP_U_Point*>(
            static_cast<const uint8_t*>(Template->OptionalComOverride) + 0x30);
        LocalCsys.vv.x = Override->k[0];
        LocalCsys.vv.y = Override->k[1];
        LocalCsys.vv.z = Override->k[2];
    }
    else
    {
        // Common.dll.txt:06345b91-b9e (`MOV ECX,[EBX+0x80]; MOV EAX,[ECX];
        // CALL [EAX+4]`), i.e. surface_manager's vtable slot 1.
        //
        // D-claude-common-ivp pass (2026-09-17): this used to be a raw
        // opaque-vtable dispatch through a reinterpret_cast function
        // pointer, because IVP_SurfaceManager was only forward-declared.
        // It is a real virtual call now. Slot 1 writes exactly three floats
        // through its argument (see ivp_surface_manager.h), so the argument
        // is the address of LocalCsys.vv's first component rather than the
        // whole 4-float IVP_U_Point -- matching retail, which never touches
        // the fourth.
        // 2026-10-14 (claude-code:cmn-s2-ivp): the port-only
        // `surface_manager != nullptr` test and its "fail soft" else branch
        // are gone -- retail (.text:06345b72 JZ -> 06345b91-06345b9e) calls
        // slot 1 unconditionally whenever there is no override.
        Self->surface_manager->get_mass_center(&LocalCsys.vv.k[0]);
    }
    IdentityLocalQuat.set_matrix(&LocalCsys); // :06345baa FUN_06350bc0

    // Common.dll.txt:06345bd7-06345bd9 (`Self->vtable[2](&LocalCsys)`, now
    // resolved to IVP_Real_Object::ApplyLocalCsys/item 2785).
    Self->ApplyLocalCsys(&LocalCsys);

    IVP_Core* const Core = Self->core; // :06345bdc, loaded after the call

    // :06345be2-:06345c21 (switch on the stashed object_type, feeding
    // FUN_06346870/item 2837 -- see this function's header comment).
    float MaxAngularSpeedAddend = 0.0f;
    float SpeedLimitValue = 0.0f;
    if (ObjectType == 3)
    {
        // :06345bf8-:06345c00: both terms are a flat 0.0f.
    }
    else if (ObjectType == 2)
    {
        // Common.dll.txt:06345c0a-06345c21 (`Self->surface_manager->
        // vtable[2](&LocalCsys.vv, &MaxAngularSpeedAddend,
        // &SpeedLimitValue)`). D-surfacemanager-dynamic-chain pass
        // (2026-09-10): this call's full calling convention is now resolved
        // by tracing every pushed stack-arg address back to its physical
        // ESP-relative slot. This whole function keeps a SINGLE constant
        // physical ESP baseline from right after its own prologue (SUB
        // ESP,0xd8 plus 4 saved registers = the "+0xe8" correction seen on
        // Ghidra's own early-function local references) all the way to its
        // epilogue -- every intervening CALL in the function is confirmed
        // self-cleaning/stack-neutral (RET N callees), so raw "[ESP+N]"
        // literals at different points in the function are directly
        // comparable without a decompiler. The FIRST-pushed/LAST-stack-arg
        // pointer here (Common.dll.txt:06345c1c, `LEA EAX,[ESP+0x30]` at
        // that exact program point, after the 2 earlier pushes in this same
        // call sequence) resolves to physical ESP+0x28 -- the SAME address
        // `LEA EDX=>local_c0,[ESP+0x28]` (Common.dll.txt:06345b99) used
        // moments earlier as the OUTPUT of the surface_manager->vtable[1]
        // call the LocalCsys.vv translation branch above already ports.
        // I.e. this vtable[2] call's first (thiscall-adjacent) argument IS
        // `&LocalCsys.vv` itself (the object's already-computed local
        // center-of-mass/shift point), not a separate unidentified buffer.
        //
        // The other two pushed pointers (Common.dll.txt:06345c12/06345c17,
        // their own `LEA EAX,[ESP+0x24]`/`[ESP+0x14]`) are genuine OUTPUTS
        // -- confirmed by reading the callee's own body directly (this
        // vtable's slot 2 target, LAB_0634acb0, Common.dll.txt:0634acb0-
        // 0634ad2f -- see this function's own header comment for the full
        // surface_manager-adapter-class writeup this pass established,
        // including how its vtable was dumped from .rdata and slot 1/3 were
        // independently confirmed). LAB_0634acb0 computes `D = |Inner.center
        // - *InPoint|` (a 3-float Euclidean distance against the adapter's
        // own opaque "Inner" sub-object), writes `D + Inner.radius` to the
        // SECOND-pushed pointer, and `D + (Inner.kind_byte) * Inner.radius *
        // DAT_063a54b4` to the FIRST-pushed pointer -- matching this call
        // site's own consumption immediately below (MaxAngularSpeedAddend
        // <- second-pushed slot, SpeedLimitValue <- first-pushed slot,
        // Common.dll.txt:408285-293, unchanged by this pass). Since the
        // callee is reached purely through the confirmed vtable INDEX and
        // calling convention -- not through any assumption about
        // surface_manager's own class NAME or Inner's own field semantics
        // -- this is safe to wire as a raw vtable dispatch, matching the
        // identical convention already used for slot 1 just above.
        // Typed slot 2 call; the port-only null-surface_manager guard is gone
        // (cmn-phy5 2026-10-08): retail has no null check at :06345c0a.
        Self->surface_manager->get_radius_and_radius_dev_to_given_center(
            const_cast<IVP_U_Point*>(&LocalCsys.vv), &MaxAngularSpeedAddend, &SpeedLimitValue);
    }
    else
    {
        // Common.dll.txt:408268/408312: retail intentionally null-derefs
        // here (`MOV dword ptr [DAT_00000000],0x0`) -- a deliberate
        // assert-fail trap, not a real code path.
        IVP_CoreTrap(); // 06345bec, then the shared tail at 06345c24
    }

    // :06345c24-:06345c3e: FLD field_94, FADD addend, FSTP float into the
    // addend's own slot (re-read as the sphere radius below), then
    // FUN_06346870(core, that sum, limit).
    const float Radius = Self->field_94 + MaxAngularSpeedAddend;
    Core->SetSpeedLimits(Radius, SpeedLimitValue);

    // ------------------------------------------------------------------
    // Common.dll.txt:408294-408377. Mass/RotInertia block. D-rotinertia-
    // selector pass (2026-09-10): this was this item's sole remaining gap,
    // now closed -- see this function's own header comment above for the
    // full instruction-by-instruction resolution of the `[ESP+0x1c]` shape
    // selector (it is simply Self->object_type, stashed to the stack early
    // at Common.dll.txt:06345b6e specifically because EBP -- which originally
    // held it -- is repurposed to `&Core->RotInertia` before this block needs
    // the value a second time).
    // ------------------------------------------------------------------
    {
        // MASS (Common.dll.txt:408294-408301). DAT_063a545c is a small
        // positive epsilon (bytes A6 9B 44 3B, Common.dll.txt:568764-568769),
        // not independently named -- clamp Template->MassInput to a minimum
        // of 1.0f whenever it falls below that epsilon.
        // :06345c43-:06345c5a `FCOMP eps` / `TEST AH,0x1` / `JZ`: C0 set (less, or
        // unordered) -> 1.0f.
        const float MassEpsilon = BitsToFloat(0x3b449ba6); // DAT_063a545c
        const float MassValue = !(Template->MassInput >= MassEpsilon) ? 1.0f : Template->MassInput;

        // ROTINERTIA SHAPE SWITCH (:06345c62-:06345cfa). Both scaled shapes
        // fill a three-float scratch and share the tail at :06345cb0, where
        // x and y keep s*T on the FPU before the FMUL by the mass, while z is
        // stored as a float first (FSTP [ESP+0x1c] at :06345cbe).
        float ScaleScratch[3];
        bool bScaled = true;
        if (Template->AutoCheckRotInertia == 0)
        {
            // Common.dll.txt:408344-408352 (LAB_06345ce4). Direct, UNSCALED
            // copy -- no surface_manager or MassValue involved on this path.
            Core->RotInertia.x = Template->InertiaBoxVector.x;
            Core->RotInertia.y = Template->InertiaBoxVector.y;
            Core->RotInertia.z = Template->InertiaBoxVector.z;
            bScaled = false;
        }
        else if (ObjectType == 2)
        {
            // Common.dll.txt:408307-408313/408322-408342 (LAB_06345ca0 falls
            // into the shared tail LAB_06345cb0). "Box" shape: Scratch <-
            // Self->surface_manager->vtable[3](&Scratch), i.e. `*(Vector*)
            // (Inner+0xc)` per this function's own header-comment vtable dump
            // (slot3=LAB_0634ac90, same surface_manager-adapter class already
            // confirmed live for the UpperLimitRadius/SpeedLimit branch
            // above), then componentwise-scaled by Template->InertiaBoxVector
            // and MassValue.
            IVP_U_Point Scratch;
            // Typed slot 3 call, no port-only null guard (cmn-phy5 2026-10-08).
            Self->surface_manager->get_rotation_inertia(&Scratch);
            ScaleScratch[0] = Scratch.x;
            ScaleScratch[1] = Scratch.y;
            ScaleScratch[2] = Scratch.z;
        }
        else if (ObjectType == 3)
        {
            // Common.dll.txt:408307-408311/408314-408342 (LAB_06345c84 ->
            // LAB_06345cb0). "Sphere" shape: Scratch <- isotropic
            // r*r*0.4f (DAT_063a5458 decodes to EXACTLY 0.4f, bytes CD CC CC
            // 3E, Common.dll.txt:568759-568763 -- the classic solid-sphere
            // moment-of-inertia coefficient 2/5), where `r` is the SAME
            // physical stack slot (PB+0x10) FUN_06346870 just consumed a few
            // instructions earlier as Core->UpperLimitRadius's own new value
            // (re-traced this pass -- see header comment), i.e. r ==
            // Core->UpperLimitRadius at this exact point in the function. Then
            // componentwise-scaled by Template->InertiaBoxVector and
            // MassValue, matching the box branch's own shared tail formula.
            // :06345c84-:06345c9a: r*r*0.4f as one x87 product, then FST /
            // FST / FSTP float into all three slots.
            const float SphereCoefficient = BitsToFloat(0x3ecccccd); // DAT_063a5458 == 0.4f
            const float Scratch = (float)((double)(Radius) * Radius * SphereCoefficient);
            ScaleScratch[0] = Scratch;
            ScaleScratch[1] = Scratch;
            ScaleScratch[2] = Scratch;
        }
        else
        {
            // :06345c78 `MOV dword ptr [0],0`, then the shared tail on the
            // never-written scratch (retail reads uninitialised stack).
            IVP_CoreTrap();
            ScaleScratch[0] = ScaleScratch[1] = ScaleScratch[2] = 0.0f;
        }

        if (bScaled)
        {
            // :06345cb0-:06345cdf.
            const float ScaledZ = (float)((double)(ScaleScratch[2]) * Template->InertiaBoxVector.z);
            Core->RotInertia.x = (float)((double)(ScaleScratch[0]) * Template->InertiaBoxVector.x * MassValue);
            Core->RotInertia.y = (float)((double)(ScaleScratch[1]) * Template->InertiaBoxVector.y * MassValue);
            Core->RotInertia.z = (float)((double)(ScaledZ) * MassValue);
        }

        // :06345cfc-:06345d0a `FCOMP 0.0f` / `TEST AH,0x40` / `JNZ`: C3 set (equal,
        // or unordered) skips the clamp.
        if (Template->RotInertiaClampFactor < 0.0f || Template->RotInertiaClampFactor > 0.0f)
        {
            // :06345d0c-:06345d13 FUN_06347e60 (sqrt((x*x + y*y) + z*z), unrounded)
            // times the factor stays on the FPU.
            const double X = Core->RotInertia.x;
            const double Y = Core->RotInertia.y;
            const double Z = Core->RotInertia.z;
            const double Threshold = std::sqrt((X * X + Y * Y) + Z * Z) * Template->RotInertiaClampFactor;

            // :06345d1d-:06345d2c, three passes: `FCOM [c]` / `TEST AH,0x41` /
            // `JNZ` -- only Threshold > c (ordered) stores, FST float.
            if (Threshold > Core->RotInertia.x) Core->RotInertia.x = (float)(Threshold);
            if (Threshold > Core->RotInertia.y) Core->RotInertia.y = (float)(Threshold);
            if (Threshold > Core->RotInertia.z) Core->RotInertia.z = (float)(Threshold);
        }

        // :06345d30-:06345d34 (`MOV [EBP+0xc],EAX`, EBP = &Core->RotInertia).
        Core->Mass = MassValue;
    }

    // :06345d37-:06345d5e.
    Core->SpeedDamp = Template->SpeedDampInput;
    Core->RotSpeedDamp.x = Template->RotSpeedDampInputX;
    Core->RotSpeedDamp.y = Template->RotSpeedDampInputY;
    Core->RotSpeedDamp.z = Template->RotSpeedDampInputZ;

    // :06345d61 FUN_063483d0 inverts LocalCsys into a stack matrix that nothing
    // reads afterwards; no other effect.

    // :06345d66-:06345d88.
    Core->RotSpeed.x = Core->RotSpeed.y = Core->RotSpeed.z = 0.0f;
    Core->Speed.x = Core->Speed.y = Core->Speed.z = 0.0f;

    // Common.dll.txt:06345d8e (`CALL FUN_06347410`, immediately after
    // Core->Mass is stored). Recompute InvRotInertia/InvMass/the isotropic
    // flag bits/DAngularLimitFactor from the RotInertia/Mass/UpperLimitRadius
    // values just set.
    Core->RecomputeInverseInertia();

    // Common.dll.txt:06345d93-06345d9a (`Core->init_world_transform_from_
    // local_csys(&local_80)`, the SAME pre-inversion LocalCsys).
    Core->init_world_transform_from_local_csys(&LocalCsys);

    // ------------------------------------------------------------------
    // :06345d9f-:06345e20, the time tail on the Environment argument
    // ([ESP+0xec]; every caller passes Self's environment: 06343b07 from
    // Object+0x18, 063622e6 / 063625e3 from Father+0x18).
    // Alpha = ((next_psi_time_base - time_base) + next_psi_time_offset) -
    // time_offset stays on the FPU; `FCOM 1e-10f` (DAT_063a53d8) / `TEST
    // AH,0x41` / `JNZ`: only Alpha > eps (ordered) takes {float(Alpha),
    // float(1.0f / Alpha)} -- the divide uses the unrounded Alpha -- else
    // {delta_time, inv_delta_time}.
    // ------------------------------------------------------------------
    {
        float DeltaTime;
        float InvDeltaTime;
        const double Alpha = (((double)(Environment->next_psi_time_base) - Environment->current_time.time_base) +
                              Environment->next_psi_time_offset) - Environment->current_time.time_offset;
        if (Alpha > (double)(1.0e-10f))
        {
            DeltaTime = (float)(Alpha);           // :06345def FST
            InvDeltaTime = (float)(1.0 / Alpha);  // :06345df3-:06345dfb (DAT_063a53d0 == 1.0f)
        }
        else
        {
            DeltaTime = Environment->delta_time;             // :06345e05 +0xac
            InvDeltaTime = Environment->inv_delta_time;      // :06345e0f +0xb0
        }

        // Common.dll.txt:06345e19-06345e20 (`Core->synchronize_simulation_
        // time_snapshot(&{DeltaTime, InvDeltaTime})`, item 2850).
        Core->synchronize_simulation_time_snapshot(DeltaTime, InvDeltaTime);
    }
}

// ============================================================================
// IVP_RealObjectConstructionTemplate members (2026-10-08, claude-code:cmn-phy3;
// moved from the former duplicate class IVP_Material, ivp_material.cpp).
// ============================================================================

// FUN_063486c0 (.text:063486c0-0634870f). FUN_06348660 first zeroes +0x00
// twice (an inlined member constructor), then `REP STOSD` clears 0x16
// dwords, then the defaults below are stored in retail's order, +0x14 last.
IVP_RealObjectConstructionTemplate::IVP_RealObjectConstructionTemplate()
{
    std::memset(this, 0, sizeof(IVP_RealObjectConstructionTemplate));
    MassInput = 1.0f;                                // 063486d9
    AutoCheckRotInertia = 1;                         // 063486dc
    InertiaBoxVector.x = 1.0f;                       // 063486e3
    InertiaBoxVector.y = 1.0f;                       // 063486e6
    InertiaBoxVector.z = 1.0f;                       // 063486e9
    SpeedDampInput = BitsToFloat(0x3c23d70a);        // 063486f1
    RotInertiaClampFactor = BitsToFloat(0x3cf5c28f); // 063486f4
    RotSpeedDampInputX = BitsToFloat(0x3c23d70a);    // 063486fb
    RotSpeedDampInputY = BitsToFloat(0x3c23d70a);    // 063486fe
    RotSpeedDampInputZ = BitsToFloat(0x3c23d70a);    // 06348701
    std::memset(_unknown_0x14, 0, sizeof(_unknown_0x14)); // 06348705, second zeroing
}

// FUN_06348670 (thunk 06348710): `if ([0]) { FUN_06343fb0([0]); [0] = 0; }`.
IVP_RealObjectConstructionTemplate::~IVP_RealObjectConstructionTemplate()
{
    if (LongNamePtr != nullptr)
    {
        std::free(LongNamePtr);
        LongNamePtr = nullptr;
    }
}

// FUN_06348720 (.text:06348720-0634875b). A null Name stores its low byte (0)
// into +0x04 and returns. Otherwise strlen(Name) > 8 hits retail's assert
// stand-in `MOV [0],EAX` (.text:06348745), reproduced literally; then
// strncpy(ShortNameTag, Name, 8), which does not terminate an 8-char name.
void IVP_RealObjectConstructionTemplate::set_short_name(const char* Name)
{
    if (Name == nullptr)
    {
        ShortNameTag[0] = 0;
        return;
    }
    if (std::strlen(Name) > 8)
    {
        volatile int32_t* Trap = nullptr;
        *Trap = 0;
    }
    std::strncpy(ShortNameTag, Name, 8);
}

// FUN_06348690 (.text:06348690-063486b8): free the old name (and null the
// field), then store FUN_06356920(Name) -- IVP's strdup, which returns null
// for a null Name (`TEST ESI,ESI; JZ`, .text:06356925).
void IVP_RealObjectConstructionTemplate::set_long_name(const char* Name)
{
    if (LongNamePtr != nullptr)
    {
        std::free(LongNamePtr);
        LongNamePtr = nullptr;
    }
    LongNamePtr = (Name != nullptr) ? _strdup(Name) : nullptr;
}

// ============================================================================
// cmn-phy6 block (2026-10-08): ensure_in_simulation 06344dc0,
// disable_simulation 06344fb0, recheck_mindists_of_other_cores 06344f50,
// beam_object_to_new_position 06345f90, core merge 06343700, core split
// 06343940.
// ============================================================================

void IVP_Core_Add_Object(IVP_Core* Core, IVP_Real_Object* Object); // ivp_core.cpp, FUN_06346ef0
void __fastcall FUN_063465a0(IVP_Core* Core);                      // ivp_core.cpp
void IVP_RescheduleCollisionEvent(IVP_Ledge_Mindist* This, int32_t RearmWhenFar, int32_t EventMode); // ivp_mindist_limiter.cpp, FUN_0634f320

// FUN_06344dc0: `MOV EAX,[ECX+0x98]; CMP byte ptr [EAX+0x60],0x8; JNZ ret;
// JMP FUN_063450e0`.
void IVP_Real_Object::ensure_in_simulation()
{
    if (static_cast<uint8_t>(core->MovementState & 0xffu) == 8)
        ReviveOriginalCoreIfSleeping();
}

// FUN_06344fb0 (.text:06344fb0-063450cb):
//   :06344fb4-:06344fbe Core = this->core; (Core->Flags & 0xc) -> return.
//   :06344fc4-:06344fcc (Core->Flags & 0x30) -> Core->Environment->dequeue (0634bb20).
//   :06344fd1-:06344fdc signed movement byte >= 8 -> return.
//   :06344fe2-:06344ffa every object of the core, last first: unlink_contact_points(1).
//   :06344ffc-:0634504c QLastMotion (+0x170) = QWorldFCoreNext (+0xe4),
//     PosLastMotion (+0x190) = PosWorldFCore (+0xb4), raw copies.
//   :06345052-:063450aa T = (now.base - 20.0f (DAT_063a543c)) + now.offset on the
//     x87 stack; base = ftol(T) (truncation); TimeOfLastMotion = {float(base),
//     float(T - base)}.
//   :063450b2-:063450c1 OwningSimUnit (+0x15c): 063495c0, then 063495e0(Environment).
void IVP_Real_Object::disable_simulation()
{
    IVP_Core* const Core = core;
    if ((Core->Flags & 0xcu) != 0)
        return;
    if ((Core->Flags & 0x30u) != 0)
        IVP_Environment_Dequeue_Core(Core->Environment, Core);
    if (static_cast<int8_t>(Core->MovementState & 0xffu) >= 8)
        return;

    for (int32_t Index = static_cast<int32_t>(Core->Objects.n_elems) - 1; Index >= 0; --Index)
        Core->Objects.elems[Index]->unlink_contact_points(true);

    Core->QLastMotion = Core->QWorldFCoreNext;
    Core->PosLastMotion.x = Core->PosWorldFCore.x;
    Core->PosLastMotion.y = Core->PosWorldFCore.y;
    Core->PosLastMotion.z = Core->PosWorldFCore.z;

    const IVP_Environment* const Env = Core->Environment;
    const double Now = ((double)(Env->current_time.time_base) - 20.0f) + Env->current_time.time_offset;
    const int32_t Base = static_cast<int32_t>(Now);
    Core->TimeOfLastMotion.time_base = (float)(Base);
    Core->TimeOfLastMotion.time_offset = (float)(Now - (double)(Base));

    IVP_Simulation_Unit_Rebalance_And_Clear_Topology_Flag(Core->OwningSimUnit);           // 063495c0
    IVP_Simulation_Unit_Update_Motion_And_Maybe_Sleep(Core->OwningSimUnit, Core->Environment); // 063495e0
}

// FUN_06344f50 (.text:06344f50-06344fa5). The next synapse (+0x08) is read
// before the mindist is touched.
void IVP_Real_Object::recheck_mindists_of_other_cores()
{
    IVP_Synapse_Real* Synapse = synapse_list_head;
    while (Synapse != nullptr)
    {
        IVP_Ledge_Mindist* const Mindist = Synapse->get_mindist();
        IVP_Synapse_Real* const Next = Synapse->next;
        if (Mindist->synapse[0].l_obj->core->Unconfirmed_0x1B8 != Mindist->synapse[1].l_obj->core->Unconfirmed_0x1B8)
        {
            Mindist->DispatchNarrowPhaseSolve(); // :06344f85 FUN_06352cf0
            if ((Mindist->StateFlags & 0xc000u) == 0)
                IVP_RescheduleCollisionEvent(Mindist, 0, 2); // :06344f98 FUN_0634f320
        }
        Synapse = Next;
    }
}

// FUN_06345f90 (.text:06345f90-06346075, RET 0xc):
//   :06345f9c Core = this->core.
//   :06345fa2-:06345fe6 QWorldFCore = *RotationWs, PosWorldFCore = *PositionWs
//     (raw 16-byte copies).
//   :06345fdc-:06345fea unless flags (+0x78) & 0xc00:
//     RotationWs->set_matrix(&M) (06350bc0); M.vv = *PositionWs (FLD/FSTP);
//     Shift = -shift_core_f_object (+0x30, FCHS); M.vmult4(&Shift, &PosWorldFCore)
//     (063484e0).
//   :0634603d-:0634604a q_core_f_object (+0x2c) set: QWorldFCore =
//     RotationWs * inverse(q_core_f_object) (06351190 over 063510c0).
//   :0634604f-:06346069 IVP_Calc_Next_PSI_Solver{Core}.set_transformation(
//     &QWorldFCore, &PosWorldFCore, bOptimizeForRepeatedCalls) (06351b90).
// IVP_U_Quat::set_mult_inv_quat (ivp_math.h) reproduces 06351190's own term order
// and x87 spills.
void IVP_Real_Object::beam_object_to_new_position(const IVP_U_Quat* RotationWs, const IVP_U_Point* PositionWs, int32_t bOptimizeForRepeatedCalls)
{
    IVP_Core* const Core = core;
    IVP_U_Quat QWorldFCore = *RotationWs;
    IVP_U_Point PosWorldFCore = *PositionWs;

    if ((flags & 0xc00u) == 0)
    {
        IVP_U_Matrix MWorldFObject;
        RotationWs->set_matrix(&MWorldFObject);
        MWorldFObject.vv.x = PositionWs->x;
        MWorldFObject.vv.y = PositionWs->y;
        MWorldFObject.vv.z = PositionWs->z;
        IVP_U_Point ShiftObjectFCore;
        ShiftObjectFCore.x = -shift_core_f_object.x;
        ShiftObjectFCore.y = -shift_core_f_object.y;
        ShiftObjectFCore.z = -shift_core_f_object.z;
        MWorldFObject.vmult4(&ShiftObjectFCore, &PosWorldFCore);
    }

    if (q_core_f_object != nullptr)
        QWorldFCore.set_mult_inv_quat(RotationWs, static_cast<const IVP_U_Quat*>(q_core_f_object));

    IVP_Calc_Next_PSI_Solver Solver(Core);
    Solver.set_transformation(&QWorldFCore, &PosWorldFCore, bOptimizeForRepeatedCalls);
}

// FUN_06343700 (.text:06343700-0634393c, cdecl):
//   :06343700-:0634371e Distance vs 0.0f (FCOMP, `TEST AH,0x1; JZ` skips when
//     not below): Distance < 0 or unordered -> measure. Both cores' positions
//     are extrapolated with FUN_0628be60 (Dst = Pos + Delta * dt, x87 per
//     component) to Host->environment's current time, dt = ((now.base -
//     core.TimeOfLastPsi.base) + now.offset) - core.TimeOfLastPsi.offset
//     stored as float; Distance = float(sqrt((dz*dz + dy*dy) + dx*dx)).
//   :063437e0-:0634380f exactly one of the two objects has a signed movement
//     byte >= 8: that one is revived (063450e0).
//   :06343814-:06343850 OldCore = Object->core; Object->get_cache_object_no_lock
//     (inlined 0634d530 / 0634d640); its quaternion (+0xc) and position
//     (+0x4c) are copied.
//   :06343872-:063438cf NewCore = Host->core; UpperLimitRadius (+0x04) and
//     +0x08 each become float(Distance + OldCore's value) when that is strictly
//     greater (FCOM, `TEST AH,0x41; JNZ` keeps the old value).
//   :063438d1-:063438f7 Object->unlink_contact_points(1); OldCore->remove (06346f20,
//     deletes an emptied core); core / original_core / +0xa0 = NewCore;
//     NewCore->add (06346ef0).
//   :063438fc-:06343923 Object's movement byte = Host's.
//   :06343926 06343cc0(0, Object, &Quat, &Position, 0) = PhySys::BeamBodyToWorldPose.
//   :06343930 FUN_063465a0(NewCore).
void IVP_Merge_Object_Into_Core(IVP_Real_Object* Host, IVP_Real_Object* Object, float Distance)
{
    if (!(Distance >= 0.0f))
    {
        const IVP_Environment* const Env = Host->environment;
        const float NowBase = Env->current_time.time_base;
        const float NowOffset = Env->current_time.time_offset;

        double Position[2][3];
        IVP_Real_Object* const Objects[2] = { Host, Object };
        for (int32_t Side = 0; Side < 2; ++Side)
        {
            const IVP_Core* const Core = Objects[Side]->core;
            const float Dt = (float)((((double)(NowBase) - Core->TimeOfLastPsi.time_base) + NowOffset) - Core->TimeOfLastPsi.time_offset);
            Position[Side][0] = (float)((double)(Core->PosWorldFCore.x) + (double)(Core->DeltaWorldFCore.x) * Dt);
            Position[Side][1] = (float)((double)(Core->PosWorldFCore.y) + (double)(Core->DeltaWorldFCore.y) * Dt);
            Position[Side][2] = (float)((double)(Core->PosWorldFCore.z) + (double)(Core->DeltaWorldFCore.z) * Dt);
        }
        const double Dx = Position[0][0] - Position[1][0];
        const double Dy = Position[0][1] - Position[1][1];
        const double Dz = Position[0][2] - Position[1][2];
        Distance = (float)(std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx));
    }

    const bool bHostSimulated = static_cast<int8_t>(Host->flags & 0xffu) < 8;
    const bool bObjectSimulated = static_cast<int8_t>(Object->flags & 0xffu) < 8;
    if (bHostSimulated && !bObjectSimulated)
        Object->ReviveOriginalCoreIfSleeping();
    else if (!bHostSimulated && bObjectSimulated)
        Host->ReviveOriginalCoreIfSleeping();

    IVP_Core* const OldCore = Object->core;
    const IVP_NarrowPhaseCacheSlot* const Slot = Object->get_cache_object_no_lock();
    const IVP_U_Quat Quat = Slot->Quat;
    Vector Position;
    Position.x = Slot->WorldTransform.vv.x;
    Position.y = Slot->WorldTransform.vv.y;
    Position.z = Slot->WorldTransform.vv.z;

    IVP_Core* const NewCore = Host->core;
    const double Radius = (double)(Distance) + OldCore->UpperLimitRadius;
    if (Radius > NewCore->UpperLimitRadius)
        NewCore->UpperLimitRadius = (float)(Radius);
    const double Limit = (double)(Distance) + OldCore->SpeedLimit;
    if (Limit > NewCore->SpeedLimit)
        NewCore->SpeedLimit = (float)(Limit);

    Object->unlink_contact_points(true);
    IVP_Core_Remove_Object(OldCore, Object);
    Object->core = NewCore;
    Object->original_core = NewCore;
    Object->field_a0 = NewCore;
    IVP_Core_Add_Object(NewCore, Object);

    Object->flags = (Object->flags & ~0xffu) | (Host->flags & 0xffu);

    PhySys::BeamBodyToWorldPose(nullptr, Object, &Quat.x, Position);
    FUN_063465a0(NewCore);
}

// FUN_06343940 (.text:06343940-06343cb3, cdecl):
//   :06343966-:0634396a Object->unlink_contact_points(1).
//   :0634396f-:063439fa Env = Object->environment, OldCore = Object->core;
//     the cache object (inlined get_cache_object_no_lock) gives the quaternion
//     (+0xc) and position (+0x4c).
//   :063439ee-:063439fe OldCore->get_surface_speed(&Position, &SurfaceSpeed) (06346290).
//   :06343a03-:06343a20 OldCore->RotSpeed copied.
//   :06343a19-:06343a29 ShiftLength = float(|shift_core_f_object|) (06347e60).
//   :06343a2d-:06343a4e Template->ShortNameTag[0] != 0 -> strncpy(name, tag, 8).
//   :06343a51-:06343a74 count = OldCore->Objects.n_elems; OldCore->remove (06346f20);
//     count <= 1 (signed JG) -> OldCore = 0 (the core was deleted), else
//     FUN_063465a0(OldCore).
//   :06343a79-:06343acf flags = (flags & 0xffffff08) | 8; NewCore = new IVP_Core(
//     Object, &Quat, &Position, 0, Template->FlagsLo); core / original_core /
//     +0xa0 = NewCore.
//   :06343ad5-:06343b07 Template material (+0x18) / client data (+0x54) when
//     non-null; field_94 = Template->Field_0x4c; 06345b20(Env, Template).
//   :06343b0c-:06343b17 OldCore's signed movement byte >= 8 -> return. Retail
//     reads [0+0x60] here when the old core was deleted: an access violation,
//     so retail only ever splits objects out of cores holding two or more.
//     Reproduced literally (VERY STRANGE, but retail).
//   :06343b1d-:06343b2a NewCore->fire_event_object_frozen (0634b590);
//     Object->ensure_in_simulation (06344dc0).
//   :06343b2f-:06343b79 NewCore->Speed = SurfaceSpeed, NewCore->RotSpeed = old RotSpeed.
//   :06343b7f-:06343bcb DeltaPsi = float(((Env->next_psi.base - now.base) +
//     next_psi.offset) - now.offset); a local hull-manager vector with a 0x80
//     slot inline buffer.
//   :06343bcf-:06343be4 (Object->core->Flags & 0xc) -> skip to the vector cleanup.
//   :06343bea-:06343bfe hull_manager: OuterGradient = InnerGradient = 0;
//     AccumulatedDeltaA = OuterValueAtLastEdit = float(ShiftLength + AccumulatedDeltaA).
//   :06343c01-:06343c3a event sim: DeltaPsi > 1e-10f (DAT_063a53d8, `TEST AH,0x41;
//     JNZ` takes the else on not-greater) -> {DeltaPsi, float(1.0f / DeltaPsi)},
//     else {Env->delta_time, Env->inv_delta_time}; environment = Env.
//   :06343c3e-:06343c50 IVP_Calc_Next_PSI_Solver{NewCore}.calc_next_PSI_matrix.
//   :06343c5b NewCore->FastSimUnitData (+0x1b4) = 0.
//   :06343c61 commit_all_hull_managers(Env, &Vector) (06351f80).
//   :06343c69-:06343c72 recalc_exact_mindists (063454b0), recheck_mindists_of_other_cores (06344f50).
//   :06343c7b-:06343c93 the vector frees a heap buffer.
void IVP_Split_Object_From_Core(IVP_Real_Object* Object, const IVP_RealObjectConstructionTemplate* Template)
{
    Object->unlink_contact_points(true);
    IVP_Environment* const Env = Object->environment;
    IVP_Core* OldCore = Object->core;

    const IVP_NarrowPhaseCacheSlot* const Slot = Object->get_cache_object_no_lock();
    IVP_U_Quat Quat = Slot->Quat;
    IVP_U_Point Position = {};
    Position.x = Slot->WorldTransform.vv.x;
    Position.y = Slot->WorldTransform.vv.y;
    Position.z = Slot->WorldTransform.vv.z;

    IVP_U_Point SurfaceSpeed = {};
    OldCore->get_surface_speed(&Position, &SurfaceSpeed);
    const float OldRotSpeedX = OldCore->RotSpeed.x;
    const float OldRotSpeedY = OldCore->RotSpeed.y;
    const float OldRotSpeedZ = OldCore->RotSpeed.z;
    const float ShiftLength = (float)(std::sqrt(
        (double)(Object->shift_core_f_object.x) * Object->shift_core_f_object.x
        + (double)(Object->shift_core_f_object.y) * Object->shift_core_f_object.y
        + (double)(Object->shift_core_f_object.z) * Object->shift_core_f_object.z));

    if (Template->ShortNameTag[0] != 0)
        std::strncpy(Object->name, Template->ShortNameTag, 8);

    const int32_t OldObjectCount = OldCore->Objects.n_elems;
    IVP_Core_Remove_Object(OldCore, Object);
    if (OldObjectCount <= 1)
        OldCore = nullptr;
    else if (OldCore != nullptr)
        FUN_063465a0(OldCore);

    Object->flags = (Object->flags & 0xffffff08u) | 8u;
    IVP_Core* NewCore = new IVP_Core(Object, &Quat, &Position, 0, Template->FlagsLo);
    Object->core = NewCore;
    Object->original_core = NewCore;
    Object->field_a0 = NewCore;
    if (Template->Material != nullptr)
        Object->material = Template->Material;
    if (Template->ClientData != nullptr)
        Object->client_data = Template->ClientData;
    Object->field_94 = Template->Field_0x4c;
    IVP_InitCoreRestStateAndDamping(Object, Env, Template); // 06345b20(Env, Template)

    // VERY STRANGE, but retail: OldCore is null here when the old core held
    // only this object (:06343a6a), and :06343b0c still reads its +0x60.
    if (static_cast<int8_t>(OldCore->MovementState & 0xffu) >= 8)
        return;

    // :06343b1d / :06343b3b / :06343b59 re-read Object->core (+0x98) each time.
    Object->core->fire_event_object_frozen(); // 0634b590
    Object->ensure_in_simulation();           // 06344dc0
    IVP_Core* SpeedCore = Object->core;
    SpeedCore->Speed.x = SurfaceSpeed.x;
    SpeedCore->Speed.y = SurfaceSpeed.y;
    SpeedCore->Speed.z = SurfaceSpeed.z;
    SpeedCore = Object->core;
    SpeedCore->RotSpeed.x = OldRotSpeedX;
    SpeedCore->RotSpeed.y = OldRotSpeedY;
    SpeedCore->RotSpeed.z = OldRotSpeedZ;

    const float DeltaPsi = (float)((((double)(Env->next_psi_time_base) - Env->current_time.time_base) + Env->next_psi_time_offset) - Env->current_time.time_offset);
    IVP_U_Vector_Fixed<IVP_Hull_Manager, 0x80> ActiveHullManagers;

    NewCore = Object->core;
    if ((NewCore->Flags & 0xcu) == 0)
    {
        IVP_Hull_Manager& Hull = Object->hull_manager;
        Hull.OuterGradient = 0.0f;
        Hull.InnerGradient = 0.0f;
        const float Value = (float)((double)(ShiftLength) + Hull.AccumulatedDeltaA);
        Hull.AccumulatedDeltaA = Value;
        Hull.OuterValueAtLastEdit = Value;

        IVP_Event_Sim EventSim;
        if (DeltaPsi > 1e-10f) // DAT_063a53d8
        {
            EventSim.delta_time = DeltaPsi;
            EventSim.i_delta_time = (float)(1.0 / (double)(DeltaPsi)); // DAT_063a53d0 = 1.0f
        }
        else
        {
            EventSim.delta_time = Env->delta_time;
            EventSim.i_delta_time = Env->inv_delta_time;
        }
        EventSim.environment = Env;
        // +0x0c (sim_unit) is not written, as in retail; calc_next_PSI_matrix reads only
        // +0x00/+0x04 of the event (06351458-06351466).

        IVP_Calc_Next_PSI_Solver Solver(NewCore);
        Solver.calc_next_PSI_matrix(&EventSim, &ActiveHullManagers);
        NewCore->FastSimUnitData = nullptr;
        IVP_Calc_Next_PSI_Solver::commit_all_hull_managers(Env, &ActiveHullManagers);
        Object->recalc_exact_mindists();
        Object->recheck_mindists_of_other_cores();
    }
}
// ---- end cmn-phy6 block ----------------------------------------------------
