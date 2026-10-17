// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_simulation_unit.h"
#include "ivp_environment.h"
#include "ivp_controller.h"
#include "ivp_real_object.h"
#include "../ivp_utility/ivp_random.h"
#include "../ivp_utility/ivp_u_memory.h"

#include <cstdlib>
#include <cmath>
#include <cstring>

// ============================================================================
// FUN_06348e10: IVP_Simulation_Unit's real constructor. See the header's own
// comment above the declaration for the full evidence trail (D-ivpenv-
// staticobject pass, 2026-09-10).
// ============================================================================
IVP_Simulation_Unit::IVP_Simulation_Unit()
    : State(8)
    , Prev(nullptr) // retail leaves this genuinely uninitialized; see header comment
    , Next(nullptr) // ditto
{
    // Cores: the Fixed<2> ctor -- elems = inline buffer (+0x14), memsize = 2, n_elems = 0
    // (.text:06348e27-06348e35). ControllerBuckets: the default ctor -- {0, 0, null}
    // (.text:06348e39-06348e41).
}

// ============================================================================
// FUN_06348ef0: IVP_Simulation_Unit::remove_core. Common.dll.txt:414099-414138.
// Backward linear search of Unit->Cores for `Core`, then shift-remove --
// byte-for-byte the same algorithm as IVP_U_Vector<T>::RemoveElement, so
// reused directly rather than hand-duplicated.
// ============================================================================
static void IVP_Simulation_Unit_Remove_Core(IVP_Simulation_Unit* Unit, IVP_Core* Core)
{
    Unit->Cores.RemoveElement(Core);
}

// ============================================================================
// FUN_06348c80: IVP_Simulation_Unit::find_controller_bucket_index.
// Common.dll.txt:413799-413820. Backward linear search of ControllerBuckets
// for the bucket keyed by `Controller` (compares bucket->Controller, i.e.
// *(elems[i]), not the slot pointer itself -- confirmed via
// `CMP dword ptr [ESI],EDX` where ESI=elems[i] and EDX=search key).
// ============================================================================
static int IVP_Simulation_Unit_Find_Controller_Bucket_Index(IVP_Simulation_Unit* Unit, IVP_Controller* Controller)
{
    for (int i = static_cast<int>(Unit->ControllerBuckets.n_elems) - 1; i >= 0; --i)
    {
        if (Unit->ControllerBuckets.ElementAt(i)->Controller == Controller)
        {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// FUN_06348e60: IVP_Simulation_Unit::free_controller_bucket.
// Common.dll.txt:414020-414062. Re-resolves the bucket index for `Controller`
// (retail really does call FUN_06348c80 again here rather than reusing the
// caller's index -- faithfully reproduced, not an accidental double search),
// frees the bucket's own Cores vector buffer and the bucket record itself,
// then removes the now-dangling slot from ControllerBuckets.
//
// Index<0 is defensive-only: every real call site (see
// IVP_Simulation_Unit_Detach_Core below) only calls this after confirming a
// matching bucket exists, so retail itself never null/underflow-guards this
// lookup here.
// ============================================================================
static void IVP_Simulation_Unit_Free_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller)
{
    // cmn-ivp3d 2026-10-08: no not-found guard in retail (.text:06348e60-
    // 06348eb5); an absent controller (index -1) reads elems[-1] and shifts
    // from -1 -- VERY STRANGE, REPRODUCED ON PURPOSE (the invented
    // `Index < 0 -> return` is gone).
    const int32_t Index = IVP_Simulation_Unit_Find_Controller_Bucket_Index(Unit, Controller);

    IVP_Sim_Unit_Controller_Bucket* const Bucket = Unit->ControllerBuckets.elems[Index];
    delete Bucket; // :06348e77-:06348e83 null test, FUN_06348be0 (the Cores vector's Clear = member dtor), operator delete

    // :06348e8b-:06348eae: shift down while i < n-1 (n re-read), then n--.
    for (int32_t I = Index; I < static_cast<int32_t>(Unit->ControllerBuckets.n_elems) - 1; ++I)
    {
        Unit->ControllerBuckets.elems[I] = Unit->ControllerBuckets.elems[I + 1];
    }
    --Unit->ControllerBuckets.n_elems;
}

// ============================================================================
// FUN_06349430: raw intrusive doubly-linked-list unlink. Common.dll.txt:
// 414798-414817. Structural twin of FUN_063493e0 (push-front, ported nowhere
// yet -- not needed for the destructor path this pass covers). `HeadSlot` is
// the address of whichever of the manager's two list-head pointers (+0x14 or
// +0x1A4) currently owns `Node`.
// ============================================================================
static void IVP_Simulation_Unit_List_Unlink(IVP_Simulation_Unit* Node, IVP_Simulation_Unit** HeadSlot)
{
    IVP_Simulation_Unit* Prev = Node->Prev;
    IVP_Simulation_Unit* Next = Node->Next;

    if (Prev != nullptr)
    {
        Prev->Next = Next;
    }
    else
    {
        *HeadSlot = Next;
    }

    if (Next != nullptr)
    {
        Next->Prev = Prev;
    }
}

// ============================================================================
// FUN_06349380 (.text:06349380-063493d5, thiscall, RET 4):
// IVP_Sim_Units_Manager's constructor, cmn-phy8 2026-10-08. Store order as
// retail: env (:06349389), +0x14 = 0 (:06349399), +0x1a4 = 0 (:0634939c),
// +0x04 = 9.0f (:063493ae), +0x08 = 0x3f3ae140 (:063493bd), +0x0c = 0.0f
// (:063493cc), +0x10 = 0x3e99999a = 0.3f (:063493cf). +0x18..+0x1a3 stay as
// operator_new left them.
// ============================================================================
IVP_Sim_Units_Manager::IVP_Sim_Units_Manager(IVP_Environment* Environment)
{
    environment = Environment;
    sim_units_head = nullptr;
    still_units_head = nullptr;
    Field_0x04 = 9.0f;
    Field_0x08 = 0.729999542f; // 0x3f3ae140 exactly
    Field_0x0c = 0.0f;
    Field_0x10 = 0.3f; // 0x3e99999a
}

// ============================================================================
// FUN_06349460 (.text:06349460-06349480, thiscall, RET 4):
// IVP_Sim_Units_Manager::rem_sim_unit_from_manager. `SHL [Unit] 0x18; CMP
// 0x8000000; JL` -- State low byte < 8 (signed) picks the +0x14 head, else
// +0x1a4; then FUN_06349430(Unit, head). Callers 06348c4b, 06348d57,
// 063494cc, 06349667.
// ============================================================================
void IVP_Sim_Units_Manager::rem_sim_unit_from_manager(IVP_Simulation_Unit* Unit)
{
    IVP_Simulation_Unit** HeadSlot = (static_cast<int8_t>(Unit->State & 0xFF) < 8)
        ? &sim_units_head
        : &still_units_head;

    IVP_Simulation_Unit_List_Unlink(Unit, HeadSlot);
}

// ============================================================================
// FUN_063493e0: raw intrusive doubly-linked-list push-front. Common.dll.txt:
// 414749-414763. Structural twin of FUN_06349430 (unlink, ported above as
// IVP_Simulation_Unit_List_Unlink) -- confirmed byte-for-byte inverse of it:
// `HeadSlot` is the address of whichever of the manager's two list-head
// pointers (+0x14 / +0x1A4) `Node` is being pushed onto as the new head.
// ============================================================================
static void IVP_Simulation_Unit_List_Push_Front(IVP_Simulation_Unit* Node, IVP_Simulation_Unit** HeadSlot)
{
    IVP_Simulation_Unit* OldHead = *HeadSlot;

    Node->Next = OldHead;
    if (OldHead != nullptr)
    {
        OldHead->Prev = Node;
    }
    Node->Prev = nullptr;
    *HeadSlot = Node;
}

// ============================================================================
// FUN_06349400 (.text:06349400-06349420, thiscall, RET 4):
// IVP_Sim_Units_Manager::add_sim_unit_to_manager. Same head selection as
// rem_sim_unit_from_manager (`SHL 0x18; CMP 0x8000000; JL` -> +0x14, else
// +0x1a4), then FUN_063493e0(Unit, head). Callers 063476d0, 063489cc,
// 063494d8 (revive, State already 1), 0634967c.
// ============================================================================
void IVP_Sim_Units_Manager::add_sim_unit_to_manager(IVP_Simulation_Unit* Unit)
{
    IVP_Simulation_Unit** HeadSlot = (static_cast<int8_t>(Unit->State & 0xFF) < 8)
        ? &sim_units_head
        : &still_units_head;

    IVP_Simulation_Unit_List_Push_Front(Unit, HeadSlot);
}

// ============================================================================
// FUN_06348d80: IVP_Simulation_Unit::free_vector_buffers. Common.dll.txt:
// 413933-413986 (SEH-framed in retail; dropped here per this codebase's
// established convention for leaf cleanup with no throwing sub-calls). Frees
// ControllerBuckets.elems first, then Cores.elems (matching retail's exact
// order), each only if heap-allocated (not the inline small-buffer) --
// byte-for-byte IVP_U_Vector<T>::Clear()'s own logic, so reused directly.
// ============================================================================
// cmn-ivp3d 2026-10-08: retail first runs FUN_06348b80 (clear the controller
// buckets, :06348da6) -- that call was missing. This is the unit's destructor
// body (~IVP_Simulation_Unit); callers free the unit afterwards.
void IVP_Simulation_Unit_Free_Vector_Buffers(IVP_Simulation_Unit* Unit)
{
    IVP_Simulation_Unit_Clear_Controller_Buckets(Unit); // :06348da6 FUN_06348b80
    Unit->ControllerBuckets.Clear();                    // :06348dab-:06348dcf
    Unit->Cores.Clear();                                // :06348dd3-:06348dfd
}

// ============================================================================
// FUN_06348cb0: IVP_Simulation_Unit::detach_core (orchestrator). See
// ivp_simulation_unit.h for the full evidence chain and offsets. Called from
// IVP_Core::~IVP_Core (FUN_063477e0, Common.dll.txt:411302-411306) via
// IVP_Core_Remove_Object's real-destructor wiring in ivp_core.cpp.
// ============================================================================
void IVP_Simulation_Unit_Detach_Core(IVP_Simulation_Unit* Unit, IVP_Core* Core)
{
    // 1. Remove Core from this unit's own Cores vector.
    IVP_Simulation_Unit_Remove_Core(Unit, Core);

    // 2. For every controller still attached to Core, remove Core from that
    //    controller's per-unit bucket; if the bucket's own Cores list becomes
    //    empty, free the bucket itself.
    for (int i = static_cast<int>(Core->Controllers.n_elems) - 1; i >= 0; --i)
    {
        IVP_Controller* Controller = Core->Controllers.ElementAt(i);

        // :06348cdc-:06348d37 (cmn-ivp3d 2026-10-08: the invented
        // `BucketIndex < 0 -> continue` is gone; retail indexes elems[-1]).
        const int32_t BucketIndex = IVP_Simulation_Unit_Find_Controller_Bucket_Index(Unit, Controller);
        IVP_Sim_Unit_Controller_Bucket* const Bucket = Unit->ControllerBuckets.elems[BucketIndex];
        Bucket->Cores.RemoveElement(Core); // inlined generic remove

        if (Bucket->Cores.n_elems == 0)
        {
            IVP_Simulation_Unit_Free_Controller_Bucket(Unit, Controller);
        }
    }

    // 3. If this was the last Core attached to the unit, tear the unit down:
    //    unlink it from the owning manager's active/sleeping list, free its
    //    two vector buffers, then free the unit itself.
    if (Unit->Cores.n_elems == 0)
    {
        Core->Environment->sim_units_manager->rem_sim_unit_from_manager(Unit); // Environment+0x08, FUN_06349460
        IVP_Simulation_Unit_Free_Vector_Buffers(Unit);
        ::operator delete(Unit); // FUN_06391d5a (the unit comes from operator new)
    }
}

// ============================================================================
// D-cluster-06347f50 pass (2026-09-10): controller-bucket build/merge/split
// side. See ivp_simulation_unit.h's own comment above each declaration for
// the full evidence trail; this section just implements them in the same
// order they're declared there.
// ============================================================================

// FUN_06348860 (Common.dll.txt:413260-413283, decomp-tracker item 2908).
bool IVP_Simulation_Unit_Has_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller)
{
    for (int i = static_cast<int>(Unit->ControllerBuckets.n_elems) - 1; i >= 0; --i)
    {
        if (Unit->ControllerBuckets.ElementAt(i)->Controller == Controller)
        {
            return true;
        }
    }
    return false;
}

// FUN_06348900 (Common.dll.txt:413297-413338, decomp-tracker item 2910).
void IVP_Simulation_Unit_Create_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller)
{
    IVP_Sim_Unit_Controller_Bucket* Bucket = new IVP_Sim_Unit_Controller_Bucket();
    Bucket->Controller = Controller;
    Unit->ControllerBuckets.Add(Bucket);
}

// FUN_063488a0 (Common.dll.txt:413351-413387, decomp-tracker item 2909).
void IVP_Simulation_Unit_Add_Core_To_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller, IVP_Core* Core)
{
    // .text:063488a0-063488f2: inlined search (index -1 when absent, no
    // test -- retail indexes elems[-1]; the invented guard is gone,
    // cmn-ivp3d 2026-10-08), then IVP_U_Vector::add on the bucket's Cores.
    const int32_t Index = IVP_Simulation_Unit_Find_Controller_Bucket_Index(Unit, Controller);
    IVP_Sim_Unit_Controller_Bucket* const Bucket = Unit->ControllerBuckets.elems[Index];
    Bucket->Cores.Add(Core);
}

// FUN_063487f0 (Common.dll.txt:413186-413260, decomp-tracker item 2907).
void IVP_Simulation_Unit_Rebuild_Controller_Buckets(IVP_Simulation_Unit* Unit)
{
    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        IVP_Core* Core = Unit->Cores.ElementAt(i);
        for (int j = static_cast<int>(Core->Controllers.n_elems) - 1; j >= 0; --j)
        {
            IVP_Controller* Controller = Core->Controllers.ElementAt(j);

            if (!IVP_Simulation_Unit_Has_Controller_Bucket(Unit, Controller))
            {
                IVP_Simulation_Unit_Create_Controller_Bucket(Unit, Controller);
            }
            IVP_Simulation_Unit_Add_Core_To_Controller_Bucket(Unit, Controller, Core);
        }
    }

    // FUN_063492c0 (decomp-tracker item 2935), now ported below as
    // IVP_Simulation_Unit_Sort_Controller_Buckets_By_Priority -- wired in per
    // the TODO the concurrent lane that authored this function left here
    // (D-cluster-06347f50 pass, 2026-09-10; closed by D-cluster-06348f60,
    // same day).
    IVP_Simulation_Unit_Sort_Controller_Buckets_By_Priority(Unit);
}

// FUN_06348b80 (Common.dll.txt:413433-413457, decomp-tracker item 2914).
void IVP_Simulation_Unit_Clear_Controller_Buckets(IVP_Simulation_Unit* Unit)
{
    for (int i = static_cast<int>(Unit->ControllerBuckets.n_elems) - 1; i >= 0; --i)
    {
        // :06348b94-:06348bab: the null test is delete's; FUN_06348be0 (the
        // Cores vector's Clear) is the member destructor, then operator
        // delete -- the bucket comes from `new` (06348900), so `delete`, not
        // free() (cmn-ivp3d 2026-10-08).
        delete Unit->ControllerBuckets.elems[i];
    }
    Unit->ControllerBuckets.Clear();
}

// FUN_06348ad0 (Common.dll.txt:413560-413644, decomp-tracker item 2913). See
// ivp_simulation_unit.h's own comment above the declaration for the vtable
// slot-index caveat and why this branch is currently unreachable in practice.
IVP_Core* IVP_Simulation_Unit_Find_Disconnected_Core(IVP_Simulation_Unit* Unit)
{
    // Step 1: reset every attached Core's union-find link.
    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        Unit->Cores.ElementAt(i)->MergedCoreWhichReplacedIt = nullptr;
    }

    // Step 2 (:06348af3-:06348b46, count read once): every bucket's
    // controller, slot 2 get_associated_controlled_cores (`CALL [EDX+0x8]`,
    // typed since cmn-ivp3d 2026-10-08, was a raw vtable[2] cast). An empty
    // list is skipped (`DEC ESI; JS`) before elems[0] is read.
    for (int32_t B = static_cast<int32_t>(Unit->ControllerBuckets.n_elems) - 1; B >= 0; --B)
    {
        IVP_U_Vector<IVP_Core>* const Cores = Unit->ControllerBuckets.elems[B]->Controller->get_associated_controlled_cores();
        const int32_t Count = static_cast<int32_t>(Cores->n_elems);
        if (Count - 1 < 0)
        {
            continue;
        }

        IVP_Core* const FirstMaster = FUN_063557c0(Cores->elems[0]);
        for (int32_t J = Count - 1; J >= 0; --J)
        {
            IVP_Core* const Master = FUN_063557c0(Cores->elems[J]);
            if (Master != FirstMaster)
            {
                Master->MergedCoreWhichReplacedIt = FirstMaster;
            }
        }
    }

    // Step 3 (:06348b48-:06348b77): the first core's master is taken with no
    // count test (an empty unit reads elems[0], as retail -- the port's old
    // `n == 0 -> return nullptr` was invented), then the first core whose
    // master differs is returned.
    IVP_Core* const FirstMaster = FUN_063557c0(Unit->Cores.elems[0]);
    for (int32_t I = 0; I < static_cast<int32_t>(Unit->Cores.n_elems); ++I)
    {
        IVP_Core* const Master = FUN_063557c0(Unit->Cores.elems[I]);
        if (Master != FirstMaster)
        {
            return Master;
        }
    }
    return nullptr;
}

// FUN_06348960 (Common.dll.txt:413401-413527, decomp-tracker item 2911).
// Faithfully reproduces retail's own multi-pass control flow (see
// ivp_simulation_unit.h's header comment) rather than a "cleaned up"
// re-derivation, since the peel-one-group-per-pass algorithm is genuinely
// stateful across passes (the PendingMaster/NeedsAnotherPass carry).
void IVP_Simulation_Unit_Split_Off_Core_Group(IVP_Simulation_Unit* Unit, IVP_Core* TargetMaster)
{
    for (;;)
    {
        IVP_Core* PendingMaster = nullptr; // ESI / local_18: first non-matching core's master seen this pass
        bool NeedsAnotherPass = false;     // local_14 (as a bool -- retail's 0/1 dword)

        IVP_Simulation_Unit* NewUnit = new IVP_Simulation_Unit(); // operator_new(0x24) + FUN_06348e10
        NewUnit->State = (NewUnit->State & 0xFFFFFF01u) | 1u;     // force low byte to 1 ("awake")

        TargetMaster->Environment->sim_units_manager->add_sim_unit_to_manager(NewUnit); // Environment+0x08, FUN_06349400

        int Index = 0;
        while (Index < static_cast<int>(Unit->Cores.n_elems))
        {
            IVP_Core* Core = Unit->Cores.ElementAt(Index);
            IVP_Core* CurrentMaster = FUN_063557c0(Core);

            if (CurrentMaster == TargetMaster)
            {
                // Move Core out of Unit and into NewUnit.
                Unit->Cores.RemoveAt(Index);
                --Index; // compensate: the array just shifted down under us
                NewUnit->Cores.Add(Core);
                Core->OwningSimUnit = NewUnit;
            }
            else if (PendingMaster == nullptr)
            {
                PendingMaster = CurrentMaster;
            }
            else if (CurrentMaster != PendingMaster)
            {
                // A THIRD distinct connected group exists among the cores
                // left behind -- one more outer pass will be needed.
                NeedsAnotherPass = true;
            }

            ++Index;
        }

        IVP_Simulation_Unit_Rebuild_Controller_Buckets(NewUnit);

        if (!NeedsAnotherPass)
        {
            break;
        }
        TargetMaster = PendingMaster;
    }
}

// FUN_06348aa0 (Common.dll.txt:413529-413551, decomp-tracker item 2912).
void IVP_Simulation_Unit_Rebalance(IVP_Simulation_Unit* Unit)
{
    IVP_Core* Disconnected = IVP_Simulation_Unit_Find_Disconnected_Core(Unit);
    if (Disconnected != nullptr)
    {
        IVP_Simulation_Unit_Clear_Controller_Buckets(Unit);
        IVP_Simulation_Unit_Split_Off_Core_Group(Unit, Disconnected);
        IVP_Simulation_Unit_Rebuild_Controller_Buckets(Unit);
    }
}

// FUN_06348c10 (Common.dll.txt:413731-413767, decomp-tracker item 2916).
void IVP_Simulation_Unit_Absorb_Cores_From(IVP_Simulation_Unit* Unit, IVP_Simulation_Unit* Source)
{
    // EAX starts at 0 (:06348c19) and holds the last moved core's Environment
    // (+0xc, :06348c32) after the loop; the count is re-read every pass.
    IVP_Environment* Environment = nullptr;
    for (int32_t I = 0; I < static_cast<int32_t>(Source->Cores.n_elems); ++I)
    {
        IVP_Core* const Core = Source->Cores.elems[I];
        Unit->Cores.Add(Core); // FUN_06348ec0
        Environment = Core->Environment;
        Core->OwningSimUnit = Unit;
    }

    // VERY STRANGE, REPRODUCED ON PURPOSE (:06348c47, cmn-ivp3d 2026-10-08):
    // the sim-unit manager is read off that environment with no test, so an
    // empty Source dereferences null exactly as retail does. (The port used to
    // skip the unlink in that case -- an invented guard.)
    Environment->sim_units_manager->rem_sim_unit_from_manager(Source); // FUN_06349460
}

// FUN_06348c60 (Common.dll.txt:413779-413794, decomp-tracker item 2917).
void IVP_Simulation_Unit_Merge_From(IVP_Simulation_Unit* Unit, IVP_Simulation_Unit* Source)
{
    IVP_Simulation_Unit_Clear_Controller_Buckets(Unit);
    IVP_Simulation_Unit_Absorb_Cores_From(Unit, Source);
    IVP_Simulation_Unit_Rebuild_Controller_Buckets(Unit);
}

// ============================================================================
// D-cluster-06348f60 pass (2026-09-10): controller-detach / per-unit wake /
// motion-classification family. See ivp_simulation_unit.h's own comment
// above each declaration for the full evidence trail; this section just
// implements them in the same order they're declared there.
// ============================================================================

// FUN_06349210 (Common.dll.txt:414508-414590, decomp-tracker item 2934, 69
// insns). Full raw trace: resolves the bucket for `Controller` (FUN_06348c80,
// same helper as IVP_Simulation_Unit_Find_Controller_Bucket_Index above),
// then removes `Core` from that bucket's own Cores vector via the identical
// backward-search-then-shift algorithm IVP_U_Vector<T>::RemoveElement already
// implements (`Bucket->Cores.RemoveElement(Core)` reaches byte-identical
// memory to retail's inlined removal here); if the bucket's Cores list is
// left empty, frees the bucket via the SAME helper
// IVP_Simulation_Unit_Free_Controller_Bucket already uses for the
// destructor path (re-resolves the bucket index rather than reusing a
// caller-held one -- faithfully reproduced double search, matching that
// helper's own documented precedent). This is, byte-for-byte, the same
// per-controller body IVP_Simulation_Unit_Detach_Core already runs inline
// in its own loop (above) -- provided here as the independently-callable,
// correctly-named standalone form retail itself exposes.
void IVP_Simulation_Unit_Detach_Core_From_Controller_Bucket(IVP_Simulation_Unit* Unit,
                                                              IVP_Controller* Controller,
                                                              IVP_Core* Core)
{
    // cmn-ivp3d 2026-10-08: .text:06349210 has no not-found test (index -1
    // reads elems[-1]); the invented guard is gone. The emptied bucket is
    // freed inline with the SAME index (:06349274-:063492b6), not through a
    // second search.
    const int32_t BucketIndex = IVP_Simulation_Unit_Find_Controller_Bucket_Index(Unit, Controller);
    IVP_Sim_Unit_Controller_Bucket* const Bucket = Unit->ControllerBuckets.elems[BucketIndex];
    Bucket->Cores.RemoveElement(Core);

    if (Bucket->Cores.n_elems == 0) // `CMP word [ESI+6],0; JA` -- unsigned, i.e. == 0
    {
        delete Bucket; // :0634927b-:06349287 (null test, Clear 06348be0, operator delete)
        for (int32_t I = BucketIndex; I < static_cast<int32_t>(Unit->ControllerBuckets.n_elems) - 1; ++I) // :0634928f-:063492b0
        {
            Unit->ControllerBuckets.elems[I] = Unit->ControllerBuckets.elems[I + 1];
        }
        --Unit->ControllerBuckets.n_elems;
    }
}

// FUN_063491d0 (Common.dll.txt:414468-414493, decomp-tracker item 2933, 21
// insns). See ivp_simulation_unit.h's own comment above the declaration.
void IVP_Simulation_Unit_Register_Core_With_Controller(IVP_Simulation_Unit* Unit,
                                                        IVP_Controller* Controller,
                                                        IVP_Core* Core)
{
    if (!IVP_Simulation_Unit_Has_Controller_Bucket(Unit, Controller))
    {
        IVP_Simulation_Unit_Create_Controller_Bucket(Unit, Controller);
    }
    IVP_Simulation_Unit_Add_Core_To_Controller_Bucket(Unit, Controller, Core);
    IVP_Simulation_Unit_Sort_Controller_Buckets_By_Priority(Unit);
}

// FUN_06349350 (Common.dll.txt:414675-414695, decomp-tracker item 2936, 15
// insns, tiny). Swaps ControllerBuckets.elems[i] and ControllerBuckets.elems[j]
// in place. Trivial helper for the insertion sort below -- defined ahead of
// it since it's the sort's own callee.
void IVP_Simulation_Unit_Swap_Controller_Buckets(IVP_Simulation_Unit* Unit, int i, int j)
{
    IVP_Sim_Unit_Controller_Bucket* Bi = Unit->ControllerBuckets.ElementAt(i);
    IVP_Sim_Unit_Controller_Bucket* Bj = Unit->ControllerBuckets.ElementAt(j);
    Unit->ControllerBuckets.elems[i] = Bj;
    Unit->ControllerBuckets.elems[j] = Bi;
}

// FUN_063492c0 (Common.dll.txt:414595-414661, decomp-tracker item 2935, 57
// insns). Insertion-sort of Unit->ControllerBuckets by each bucket's own
// Controller's get_controller_priority() virtual (signed enum return).
// Preserves retail's exact
// control flow: outer index walks forward one step at a time; whenever
// adjacent buckets [i],[i+1] compare out of order, swap them, then bubble the
// moved element back toward index 0 while it's still greater than its new
// left neighbor.
void IVP_Simulation_Unit_Sort_Controller_Buckets_By_Priority(IVP_Simulation_Unit* Unit)
{
    int Count = static_cast<int>(Unit->ControllerBuckets.n_elems);
    for (int i = 0; i + 1 < Count; ++i)
    {
        int j = i + 1;
        int PriorityJ = Unit->ControllerBuckets.ElementAt(j)->Controller->get_controller_priority();
        int PriorityI = Unit->ControllerBuckets.ElementAt(i)->Controller->get_controller_priority();

        if (PriorityI <= PriorityJ)
        {
            continue;
        }

        IVP_Simulation_Unit_Swap_Controller_Buckets(Unit, i, j);

        for (int k = i; k > 0; --k)
        {
            int PriorityPrev = Unit->ControllerBuckets.ElementAt(k - 1)->Controller->get_controller_priority();
            if (PriorityPrev <= PriorityJ)
            {
                break;
            }
            IVP_Simulation_Unit_Swap_Controller_Buckets(Unit, k - 1, k);
        }
    }
}

// FUN_063494f0 (Common.dll.txt:414913-414933, decomp-tracker item 2943, 15
// insns, tiny). See ivp_simulation_unit.h's own comment above the
// declaration -- byte-identical twin of IVP_Core::ensure_in_simulation()'s
// own inline loop (ivp_core.cpp), provided here as the independently
// callable, correctly-named standalone form for this cluster's other
// callers.
void IVP_Simulation_Unit_Update_Cores_Last_Motion_Time(IVP_Simulation_Unit* Unit)
{
    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        Unit->Cores.ElementAt(i)->update_time_of_last_motion(); // FUN_063470e0
    }
}

// FUN_06349490's raw retail shape (ECX=SimUnit, stack=Environment*). See
// ivp_simulation_unit.h's own comment above the declaration.
// cmn-ivp3d 2026-10-08: re-traced .text:06349490-063494e8 (the library's
// sim_unit_revive_for_simulation); the invented `Unit == nullptr` and
// `Environment != nullptr` guards are gone and the sleep test is the low
// BYTE of State (`CMP byte [EDI],8`), not the whole dword. This is the one
// port of 06349490; IVP_Core::revive_sleeping_core forwards here.
void IVP_Simulation_Unit_Revive_If_Sleeping(IVP_Simulation_Unit* Unit, IVP_Environment* Environment)
{
    // :06349494-:063494bd: scan the cores backwards (count re-read only when
    // the scan restarts); a core whose movement byte is >= 8 (signed) gets
    // revive_pass (0634b480); a return of exactly 1 restarts the scan.
    int32_t Index = static_cast<int32_t>(Unit->Cores.n_elems);
    for (;;)
    {
        --Index;
        if (Index < 0)
        {
            break;
        }
        IVP_Core* const Core = Unit->Cores.elems[Index];
        if (static_cast<int32_t>(Core->MovementState << 24) < 0x08000000)
        {
            continue;
        }
        if (Core->revive_pass() == 1)
        {
            Index = static_cast<int32_t>(Unit->Cores.n_elems);
        }
    }

    // :063494bf-:063494e1.
    if (static_cast<uint8_t>(Unit->State) == 0x08)
    {
        Environment->sim_units_manager->rem_sim_unit_from_manager(Unit); // 06349460
        Unit->State = (Unit->State & 0xFFFFFF01u) | 0x1u;
        Environment->sim_units_manager->add_sim_unit_to_manager(Unit);   // 06349400
    }
}


// FUN_063495e0 (Common.dll.txt:415037-415117, decomp-tracker item 2947, 71
// insns). See ivp_simulation_unit.h's own comment above the declaration for
// the full behavior summary. Full raw trace: Environment's current_time
// (IVP_Environment+0xF8/+0xFC, ivp_environment.h) is read once up front and
// fed to every Core's classify_motion_state(); each core's 1/2/3 result is
// folded into that core's MovementState low byte via the same masked-merge
// idiom classify_motion_state's own header already documents, and AND-
// accumulated into Verdict (starts at 3, the "long idle" value -- so Verdict
// only survives at 3 if EVERY core reported 3). If the whole unit's Cores
// list is empty, the scan loop never executes and Verdict is left at its
// initial 3 -- retail genuinely puts an empty unit to sleep unconditionally,
// reproduced here as-is (matches the raw JS-to-sleep-section fallthrough at
// Common.dll.txt:0634960d).
int IVP_Simulation_Unit_Update_Motion_And_Maybe_Sleep(IVP_Simulation_Unit* Unit, IVP_Environment* Environment)
{
    const float TimeBase = Environment->current_time.time_base;
    const float TimeOffset = Environment->current_time.time_offset;

    int Verdict = 3;
    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        IVP_Core* Core = Unit->Cores.ElementAt(i);
        int Result = Core->classify_motion_state(TimeBase, TimeOffset); // FUN_063478f0
        Core->MovementState = (Core->MovementState & ~0xFFu) | (static_cast<uint32_t>(Result) & 0xFFu);
        Verdict &= Result;
    }

    if (Verdict != 3)
    {
        return 0;
    }

    // Every core reported "long idle" (3) -- transition the whole unit to sleep.
    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        Unit->Cores.ElementAt(i)->freeze_and_fire_object_frozen(); // FUN_0634b5e0 (:06349655-:0634965b)
    }

    IVP_Sim_Units_Manager* const Manager = Environment->sim_units_manager; // Environment+0x08
    Manager->rem_sim_unit_from_manager(Unit); // 06349460
    Unit->State = (Unit->State & 0xFFFFFF08u) | 0x08u;
    Manager->add_sim_unit_to_manager(Unit);   // 06349400

    return 1;
}

// FUN_063495c0 (Common.dll.txt:415020-415031, decomp-tracker item 2946, 10
// insns, tiny). NOT one of this pass's own 19 assigned items, but closed
// here as a direct dependency of item 2948 below -- its own 3 callees
// (Clear/Rebuild_Controller_Buckets, Rebalance) are all ready as of this
// pass; item 2946 was previously `blocked` on exactly those 3.
void IVP_Simulation_Unit_Rebalance_And_Clear_Topology_Flag(IVP_Simulation_Unit* Unit)
{
    IVP_Simulation_Unit_Clear_Controller_Buckets(Unit);  // FUN_06348b80
    IVP_Simulation_Unit_Rebuild_Controller_Buckets(Unit); // FUN_063487f0
    IVP_Simulation_Unit_Rebalance(Unit);                   // FUN_06348aa0
    Unit->State &= 0xFFFFFCFFu; // clears bits 0x300 (the pair items 2926/2930 above set)
}

// FUN_063496a0 (.text:063496a0-063499af, thiscall on the unit, RET 8), the
// library's simulate_single_sim_unit_psi. Re-traced in full by cmn-ivp3d
// (2026-10-08); the x87 terms are kept in double where retail keeps them on
// the FPU stack and rounded only at the FSTP dword.
//   - EventSim->sim_unit = Unit; Env->vector_cores_frozen (+0xa4) busy word
//     (+0x10) incremented; the current time halves (+0xf8 / +0xfc) and the
//     controller bucket count (+0x1e) are read once up front.
//   - Cores: retail runs n-1 .. 1 in the loop (`DEC EBX; CMP EBX,1; JL`)
//     and then index 0 unconditionally (:06349792) -- reproduced literally,
//     so an empty unit reads elems[0] as retail does.
//   - Verdict = OR of the float bits of (1 - |speed|^2); `OR EDX,EBP; JGE`
//     takes the non-negative arm.
//   - Controller buckets, last first: slot 4 do_simulation_controller(
//     EventSim, &Bucket->Cores) (:063498fa-:0634990d).
//   - Every core appended to OutputCoreList, last first.
//   - Timer expired (local flag 1) -> 063495e0(Unit, Env).
//   - Every object of every core, last first: recalc_invalid_mindists
//     (06345460, :06349979).
//   - State & 0x300 -> 063495c0; busy word decremented (re-read through
//     EventSim->environment); the arena is rewound (06344070, reset_chunks).
// The per-core body FUN_063496a0 runs twice (:063496f0-:0634978c in the loop,
// :06349792-:06349828 for index 0). Returns the float bits of 1 - |speed|^2.
static uint32_t IVP_Simulation_Unit_Psi_Core_Step(IVP_Core* Core, float TimeBase, float TimeOffset)
{
    // `FLD now.base; FSUB core+0x68; FADD now.offset; FSUB core+0x6c; FSTP dword`.
    const float Alpha = (float)((((double)(TimeBase)
        - *reinterpret_cast<const float*>(&Core->TimeOfLastPsi.time_base)) + TimeOffset)
        - *reinterpret_cast<const float*>(&Core->TimeOfLastPsi.time_offset));

    Core->QWorldFCoreNext.set_matrix(&Core->MWorldFCore); // FUN_06350bc0 (+0xe4 -> +0xf4)

    // FUN_0628be60(this = +0x124, +0xb4, +0xc4, Alpha): each component is
    // `FLD s; FMUL dir; FADD base; FSTP` -- one rounding per component.
    Core->MWorldFCore.vv.set(
        (float)((double)(Alpha) * Core->DeltaWorldFCore.x + Core->PosWorldFCore.x),
        (float)((double)(Alpha) * Core->DeltaWorldFCore.y + Core->PosWorldFCore.y),
        (float)((double)(Alpha) * Core->DeltaWorldFCore.z + Core->PosWorldFCore.z));

    Core->commit_speed_changes(); // FUN_06346dd0
    Core->MovementState &= 0xFFFF00FFu; // +0x60
    Core->PendingImpactCount = 0;       // +0x64 (word)

    // x*x + y*y, then + z*z, then FSUBR 1.0f (DAT_063a53d0), FSTP dword.
    const double SpeedMagSq = ((double)(Core->Speed.x) * Core->Speed.x
        + (double)(Core->Speed.y) * Core->Speed.y)
        + (double)(Core->Speed.z) * Core->Speed.z;
    const float SignTerm = (float)(1.0 - SpeedMagSq);
    uint32_t SignBits;
    std::memcpy(&SignBits, &SignTerm, sizeof(SignBits));
    return SignBits;
}

void IVP_Simulation_Unit_Process_One_Psi_Step(IVP_Event_Sim* EventSim, IVP_Simulation_Unit* Unit, IVP_U_Vector<IVP_Core>* OutputCoreList)
{
    EventSim->sim_unit = Unit; // :063496b0

    IVP_Environment* Env = EventSim->environment;
    // CORRECTED 2026-09-17: these two counters live on DIFFERENT objects, and the port had
    // both hanging off the pool. Retail:
    //     .text:063496b3  MOV EAX,[EAX + 0xa4]        ; Env->vector_cores_frozen
    //     .text:063496b9  INC word ptr [EAX + 0x10]   ; <- the arena's own +0x10 counter
    //     .text:0634983e  MOV ESI,[ECX + 0x8]         ; ESI = the ENVIRONMENT
    //     .text:06349841  DEC word ptr [ESI + 0x118]  ; <- an Environment field, not the pool's
    // vector_cores_frozen is a 0x14-byte IVP_U_Memory arena (see ivp_environment.h, proven
    // from the constructor's init_mem pair), so +0x118 on it was 0x104 bytes past the end of
    // the object. That mis-binding only became visible when the field stopped being `void*`.
    uint16_t* BusyCounter = &Env->vector_cores_frozen->State;
    int16_t* CooldownTimer = &Env->next_movement_check;

    ++(*BusyCounter);

    const float TimeBase = Env->current_time.time_base;
    const float TimeOffset = Env->current_time.time_offset;

    // :063496c8 -- the controller-bucket count is captured here, before anything below runs.
    const int32_t ControllerBucketCount = static_cast<int32_t>(Unit->ControllerBuckets.n_elems);

    uint32_t Verdict = 0;
    for (int32_t I = static_cast<int32_t>(Unit->Cores.n_elems) - 1; I >= 1; --I)
    {
        Verdict |= IVP_Simulation_Unit_Psi_Core_Step(Unit->Cores.elems[I], TimeBase, TimeOffset);
    }
    // Index 0 unconditionally (an empty unit reads elems[0], as retail).
    Verdict |= IVP_Simulation_Unit_Psi_Core_Step(Unit->Cores.elems[0], TimeBase, TimeOffset);

    bool CooldownExpired;
    if (static_cast<int32_t>(Verdict) < 0)
    {
        Unit->State = (Unit->State & 0xFFFFF7FFu) | 0x400u;
        --(*CooldownTimer);
        if (*CooldownTimer == 0)
        {
            // :06349855-:06349874 -- FMUL -5.0f (DAT_063a549c) on the unrounded value, then
            // MSVCRT ftol (truncation toward zero), 0xf - result into the word.
            const double RandomValue = (double)(IVP_FastRandomFloat()) * -5.0f;
            const int32_t RandomInt = static_cast<int32_t>(RandomValue);
            *CooldownTimer = static_cast<int16_t>(0xF - RandomInt);
            CooldownExpired = true;
        }
        else
        {
            CooldownExpired = false;
        }
    }
    else
    {
        uint32_t StateVerdict = Unit->State;
        const uint32_t Shifted = static_cast<uint32_t>(static_cast<int32_t>(StateVerdict << 0x14) >> 0x12);
        StateVerdict = ((Shifted ^ StateVerdict) & 0x3000u) ^ StateVerdict;
        Unit->State = StateVerdict;
        if ((StateVerdict & 0x3000u) != 0)
        {
            IVP_Simulation_Unit_Update_Cores_Last_Motion_Time(Unit); // FUN_063494f0
        }
        Unit->State &= 0xFFFFF3FFu;

        --(*CooldownTimer);
        if (*CooldownTimer == 0)
        {
            // :063498bf-:063498de -- same reseed as above (ftol truncates).
            const double RandomValue = (double)(IVP_FastRandomFloat()) * -5.0f;
            const int32_t RandomInt = static_cast<int32_t>(RandomValue);
            *CooldownTimer = static_cast<int16_t>(0xF - RandomInt);
            CooldownExpired = true;
        }
        else
        {
            CooldownExpired = false;
        }
    }

    // :063498eb-:0634990d: every controller bucket (count read up front), last
    // first, slot 4 (`CALL [EDX+0x10]`) with (EventSim, &Bucket->Cores).
    for (int32_t B = ControllerBucketCount - 1; B >= 0; --B)
    {
        IVP_Sim_Unit_Controller_Bucket* const Bucket = Unit->ControllerBuckets.elems[B];
        Bucket->Controller->do_simulation_controller(EventSim, &Bucket->Cores);
    }

    // Step 6: append every core into OutputCoreList. Reinterpreted as the
    // real IVP_U_Vector<IVP_Core> template (same memsize@0/n_elems@2/
    // elems@4 header every other Cores list in this cluster already uses)
    // so Add() handles the Realloc-if-full growth exactly like retail's own
    // FUN_063441a0 call, rather than a hand-duplicated partial copy.
    for (int i = static_cast<int>(Unit->Cores.n_elems) - 1; i >= 0; --i)
    {
        OutputCoreList->Add(Unit->Cores.ElementAt(i));
    }

    if (CooldownExpired)
    {
        IVP_Simulation_Unit_Update_Motion_And_Maybe_Sleep(Unit, Env); // FUN_063495e0
    }

    // :0634995b-:06349982: every object (+0x50 vector) of every core, last
    // first: recalc_invalid_mindists (FUN_06345460).
    for (int32_t I = static_cast<int32_t>(Unit->Cores.n_elems) - 1; I >= 0; --I)
    {
        IVP_Core* const Core = Unit->Cores.elems[I];
        for (int32_t J = static_cast<int32_t>(Core->Objects.n_elems) - 1; J >= 0; --J)
        {
            Core->Objects.elems[J]->recalc_invalid_mindists();
        }
    }

    if ((Unit->State & 0x300u) != 0)
    {
        IVP_Simulation_Unit_Rebalance_And_Clear_Topology_Flag(Unit); // FUN_063495c0, item 2946
    }

    // :06349992-:063499a3 -- leave the arena and rewind it (FUN_06344070).
    --(*BusyCounter);
    Env->vector_cores_frozen->reset_chunks();
}

// ============================================================================
// FUN_063499c0 (.text:063499c0-06349a57, RET 8): IVP_Sim_Units_Manager::
// simulate_sim_units_psi. `SimUnitsManager` is IVP_Environment+0x08; its active
// list head is +0x14. Builds the IVP_Event_Sim (delta_time, i_delta_time,
// environment; the sim_unit dword is written per unit by FUN_063496a0) and runs
// FUN_063496a0 over the active list exactly in retail's register pipeline: the
// next three units are fetched before each call, the loop body processes the
// oldest, and the last three are processed newest first (:06349a1d / :06349a2f /
// :06349a41) -- for units A,B,C,D,E the order is A, B, E, D, C.
// ============================================================================
void IVP_Sim_Units_Manager::simulate_sim_units_psi(IVP_Environment* Environment,
                                                   IVP_U_Vector<IVP_Core>* CoresWhichNeedCalcNextPsi)
{
    IVP_Event_Sim EventSim;
    EventSim.delta_time = Environment->delta_time;      // :063499c7 (+0xac)
    EventSim.i_delta_time = Environment->inv_delta_time; // :063499d7 (+0xb0)
    EventSim.environment = Environment;                  // :063499dd

    IVP_Simulation_Unit* Unit0 = sim_units_head; // EBP, this+0x14
    if (Unit0 == nullptr)
    {
        return;
    }
    IVP_Simulation_Unit* Unit1 = Unit0->Next; // EBX
    if (Unit1 != nullptr)
    {
        IVP_Simulation_Unit* Unit2 = Unit1->Next; // EDI
        if (Unit2 != nullptr)
        {
            IVP_Simulation_Unit* Unit3 = Unit2->Next; // ESI
            if (Unit3 != nullptr)
            {
                do
                {
                    IVP_Simulation_Unit_Process_One_Psi_Step(&EventSim, Unit0, CoresWhichNeedCalcNextPsi);
                    Unit0 = Unit1;
                    Unit1 = Unit2;
                    Unit2 = Unit3;
                    Unit3 = Unit3->Next;
                } while (Unit3 != nullptr);
            }
            IVP_Simulation_Unit_Process_One_Psi_Step(&EventSim, Unit2, CoresWhichNeedCalcNextPsi);
        }
        IVP_Simulation_Unit_Process_One_Psi_Step(&EventSim, Unit1, CoresWhichNeedCalcNextPsi);
    }
    IVP_Simulation_Unit_Process_One_Psi_Step(&EventSim, Unit0, CoresWhichNeedCalcNextPsi);
}
