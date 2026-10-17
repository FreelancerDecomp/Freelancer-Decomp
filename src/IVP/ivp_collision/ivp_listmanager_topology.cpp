// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// d229:cluster-06355200 pass (2026-09-10). Implementation for
// ivp_listmanager_topology.h -- see that header's own top-of-file comment
// for the full class-identity evidence trail. This TU only needs PUBLIC API
// (IVP_ListManager/IVP_ListManager_CorePairEntry/IVP_Contact_Point's
// already-declared fields and methods, IVP_Core's already-declared fields
// and methods, the already-ported FUN_063557c0/IVP_Contact_Point::ShiftTimeStamp) --
// items 3231/3243 are NOT here because they need the anonymous-namespace-
// local helpers (IVP_PopulatePairRecordIntoOwner/
// IVP_ResolvePairRecordSideMaterialSlots) declared inside
// ivp_mindist_limiter.cpp itself; see that file's own tail for those two.
#include "ivp_listmanager_topology.h"
#include "../ivp_physics/ivp_real_object.h"
#include "../ivp_physics/ivp_environment.h"
#include "../ivp_utility/ivp_math.h"    // IVP_U_Matrix3::vmult/vimult
#include "../ivp_compact_builder/ivp_compact_ledge.h" // IVP_Compact_Edge::GetCompactLedge
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "../ivp_physics/ivp_cluster_manager.h" // IVP_Cluster_Manager::fire_event_friction_slot3 (0634b140)

namespace
{
    // The event FUN_06355200 builds on its own stack (frame offset +0x10 after
    // the four pushes) and hands to the collision listeners. Only these fields
    // are written (06355261-06355284); retail leaves the rest as stack
    // contents, the port zeroes them. The block at +0x0c is what +0x04 points
    // at. PORT-LOCAL name (Ipion: an IVP_Event_Friction with its contact
    // situation embedded).
    struct FIVP_ContactPointDeletedEvent
    {
        IVP_Environment* Environment;           // +0x00
        void* Situation;                        // +0x04: = &SituationBlock
        IVP_Contact_Point* ContactPoint;   // +0x08
        uint8_t SituationBlock[0x30];           // +0x0c: not written here
        IVP_Real_Object* Objects[2];            // +0x3c / +0x40
        void* CompactEdges[2];                  // +0x44 / +0x48: Feature1 / Feature2
    };
    static_assert(offsetof(FIVP_ContactPointDeletedEvent, Objects) == 0x3c, "event +0x3c");
    static_assert(offsetof(FIVP_ContactPointDeletedEvent, CompactEdges) == 0x44, "event +0x44");
}

// ---------------------------------------------------------------------
// Item 3216: FUN_06355200 (.text:06355200-0635530e), the contact point's
// destructor body (called from FUN_06355490 before operator delete).
// 2026-10-08 (claude-code:cmn-phy4): the prologue is ported.
//   06355208-06355232: Object1->surface_manager (+0x80)->slot 8 (+0x20) with
//     the compact ledge of Feature1 (edge & ~0xf = triangle, minus
//     (tri_index + 1) * 16);
//   06355232-06355255: the same for Object2 / Feature2;
//   06355255-06355288: the stack event (above), Environment = Object1's
//     environment (+0x18), then FUN_0634cfe0 on that environment
//     (IVP_Environment_Notify_Listeners_C8_Slot3);
//   0635528d-063552b4: for Object1 then Object2, when flags (+0x78) has 0x2000
//     (`TEST AH,0x20`): FUN_0634b140(environment->controller_manager (+0xc),
//     object, &event) -- the environment of Object1 for both.
// The tail (063552b9-0635530e) unlinks both of the record's embedded list
// nodes, undoing IVP_Contact_Point::IVP_Contact_Point's insert (ivp_contact_point.cpp).
// ---------------------------------------------------------------------
void IVP_UnlinkPairRecordFromObjects(IVP_Contact_Point* Record)
{
    IVP_Real_Object* Object1 = Record->Object1;
    IVP_Real_Object* Object2 = Record->Object2;
    IVP_Environment* Environment = Object1->environment;

    Object1->surface_manager->remove_reference_to_ledge(
        reinterpret_cast<IVP_Compact_Edge*>(Record->Feature1)->GetCompactLedge());
    Object2->surface_manager->remove_reference_to_ledge(
        reinterpret_cast<IVP_Compact_Edge*>(Record->Feature2)->GetCompactLedge());

    FIVP_ContactPointDeletedEvent Event;
    std::memset(&Event, 0, sizeof(Event));
    Event.Environment = Environment;
    Event.Situation = Event.SituationBlock;
    Event.ContactPoint = Record;
    Event.Objects[0] = Object1;
    Event.Objects[1] = Object2;
    Event.CompactEdges[0] = Record->Feature1;
    Event.CompactEdges[1] = Record->Feature2;

    IVP_Environment_Notify_Listeners_C8_Slot3(Environment, &Event);

    if ((Object1->flags & 0x2000u) != 0)
    {
        Environment->cluster_manager->fire_event_friction_slot3(Object1, &Event); // 0634b140
    }
    if ((Object2->flags & 0x2000u) != 0)
    {
        Environment->cluster_manager->fire_event_friction_slot3(Object2, &Event); // 0634b140
    }

    // Side 1 (Common.dll.txt:063552b9-063552dd): unlink node #1
    // (Next1/Prev1, +0x08/+0x0c) from Object1->mindist_list.
    if (Record->Next1 != nullptr)
        *reinterpret_cast<void**>(static_cast<char*>(Record->Next1) + 0x4) = Record->Prev1;

    if (Record->Prev1 != nullptr)
        *reinterpret_cast<void**>(Record->Prev1) = Record->Next1;
    else
        Record->Object1->mindist_list = Record->Next1;

    // Side 2 (Common.dll.txt:063552dd-0635530e): exact mirror, node #2
    // (Next2/Prev2, +0x1c/+0x20) / Object2->mindist_list.
    if (Record->Next2 != nullptr)
        *reinterpret_cast<void**>(static_cast<char*>(Record->Next2) + 0x4) = Record->Prev2;

    if (Record->Prev2 != nullptr)
        *reinterpret_cast<void**>(Record->Prev2) = Record->Next2;
    else
        Record->Object2->mindist_list = Record->Next2;
}

// ---------------------------------------------------------------------------
// Item 3228: FUN_063557e0 (Common.dll.txt:063557e0-06355887, 69 insns).
// ---------------------------------------------------------------------------
// 4-phase union-find scan, disasm-traced fully this pass. Mirrors
// IVP_Simulation_Unit_Find_Disconnected_Core (ivp_simulation_unit.cpp)
// exactly in shape (reset -> rebuild-from-edges -> reference-master ->
// scan-for-mismatch), keyed by CorePairEntries instead of ControllerBuckets,
// and reproducing retail's own backward-iterate-with-unconditional-overwrite
// idiom in phases 3/4 (NOT an early-exit "return on first difference" loop --
// retail keeps scanning and overwrites its running result on every match, so
// with 3+ disconnected groups the returned Core can differ from a naive
// forward-first-mismatch implementation).
IVP_Core* IVP_ListManager_FindDisconnectedCore(IVP_ListManager* Manager)
{
    // Phase 1 (06355803-06355801... i.e. 063557e3-06355801): reset union-find
    // over every registered Core.
    for (int i = Manager->RegisteredCores.Len() - 1; i >= 0; --i)
        Manager->RegisteredCores.ElementAt(i)->MergedCoreWhichReplacedIt = nullptr;

    // Phase 2 (06355803-0635583d): rebuild union-find groups from
    // CorePairEntries, skipping any entry with a kinematic-ish side
    // (Flags&0xc, the same gate used throughout this cluster).
    for (int i = Manager->CorePairEntries.Len() - 1; i >= 0; --i)
    {
        IVP_ListManager_CorePairEntry* Entry = Manager->CorePairEntries.ElementAt(i);
        IVP_Core* CoreA = Entry->CoreA;
        IVP_Core* CoreB = Entry->CoreB;
        if ((CoreA->Flags & 0xc) != 0)
            continue;
        if ((CoreB->Flags & 0xc) != 0)
            continue;

        IVP_Core* MasterA = FUN_063557c0(CoreA);
        IVP_Core* MasterB = FUN_063557c0(CoreB);
        if (MasterA != MasterB)
            MasterB->MergedCoreWhichReplacedIt = MasterA;
    }

    // Phase 3 (0635583d-0635585f): reference master = master of the
    // lowest-index non-kinematic registered Core (retail's own backward
    // overwrite-loop lands there, see comment above).
    IVP_Core* ReferenceMaster = nullptr;
    for (int i = Manager->RegisteredCores.Len() - 1; i >= 0; --i)
    {
        IVP_Core* Core = Manager->RegisteredCores.ElementAt(i);
        if ((Core->Flags & 0xc) == 0)
            ReferenceMaster = FUN_063557c0(Core);
    }

    // Phase 4 (0635585f-06355881): scan every non-kinematic Core for a
    // master that differs from ReferenceMaster.
    IVP_Core* Disconnected = nullptr;
    for (int i = Manager->RegisteredCores.Len() - 1; i >= 0; --i)
    {
        IVP_Core* Core = Manager->RegisteredCores.ElementAt(i);
        if ((Core->Flags & 0xc) != 0)
            continue;
        IVP_Core* Master = FUN_063557c0(Core);
        if (Master != ReferenceMaster)
            Disconnected = Master;
    }
    return Disconnected;
}

namespace
{
    // The per-core record of a friction system is IVP_Friction_Info_For_Core
    // (ivp_mindist.h): operator_new(0xc) at .text:063558fd, FrDists zeroed
    // at 06355909-06355910, FrictionSystem written at 06355922. The
    // file-local IVP_Core_ObjectHashEntry that stood here was a second
    // declaration of it; merged 2026-10-08 (claude-code:cmn-phy3).
    IVP_Friction_Info_For_Core* IVP_FindFrictionInfo(IVP_Core* Core, IVP_ListManager* FrictionSystem)
    {
        return static_cast<IVP_Friction_Info_For_Core*>(
            Core->find_object_hash_entry(static_cast<unsigned>(reinterpret_cast<uintptr_t>(FrictionSystem))));
    }
}

// ---------------------------------------------------------------------------
// Item 3229: FUN_06355890 (Common.dll.txt:06355890-06355ff4wait... actually
// 06355890-06355b9b, 278 insns, large).
// ---------------------------------------------------------------------------
// Disasm-traced in full this pass. The "ListManager-side twin" of
// IVP_Simulation_Unit_Split_Off_Core_Group (ivp_simulation_unit.cpp), reusing
// the SAME already-ported IVP_Core::find/register/unregister/
// release_object_hash_entry quartet IVP_MergeRegisteredCores
// (ivp_mindist_limiter.cpp) already relies on for the analogous "per-Core
// per-ListManager membership" bookkeeping.
//
// Shape (retail's own outer goto-loop, `JMP LAB_063558ae` back to the top --
// reproduced here as a C++ for(;;) with the SAME "TargetMaster reassigned,
// loop again" carry):
//   1. Allocate NewManager = new IVP_ListManager(Manager->Environment) (the
//      ORIGINAL manager's own +0x04 field, not TargetMaster).
//   2. For every Core in Manager->RegisteredCores: if kinematic-ish, give it
//      a fresh IVP_Core_ObjectHashEntry keyed on NewManager and register it
//      on NewManager too (a kinematic Core can be tracked by BOTH managers
//      at once); else if get_master_core(Core)==TargetMaster, move it from
//      Manager to NewManager (Unregister/Register) and re-key its existing
//      Manager-side hash entry in place to NewManager.
//   3. For every CorePairEntry whose (post-kinematic-normalized) side
//      resolves to TargetMaster's group: move the entry itself from
//      Manager->CorePairEntries to NewManager->CorePairEntries, and for each
//      of its AttachedCtx pair-records, move it from Manager's Ctx list to
//      NewManager's (RemoveCtx/InsertCtx) and, if the pair straddles a
//      kinematic Core, move the record between that Core's two hash
//      entries too.
//   4. Cleanup pass: any kinematic Core left with an EMPTY hash entry under
//      either manager gets that entry released and is unregistered from
//      that specific manager.
//   5. If both managers still have >=2 RegisterCore calls recorded, look for
//      a further disconnected group inside the (now smaller) Manager and,
//      if found, loop again with that as the new TargetMaster; otherwise the
//      underfull manager's sole core releases its record for that manager
//      and the underfull manager is DELETED (`vtbl[6](1)` at
//      .text:06355b39-06355b3d for NewManager, 06355b8a-06355b90 for
//      Manager -- the caller's own friction system).
//
// RETAIL UB, made deterministic here (owner's ruling 2026-10-08): both
// underfull branches (.text:06355aea-06355b31 and 06355b3f-06355b86) call
// IVP_Core::release_object_hash_entry(H) (FUN_06347d50 = unregister, Clear,
// operator delete) and then, when H is non-null, run the same Clear +
// operator delete on H a SECOND time -- a double free of freed memory. The
// port performs the first release only.
void IVP_ListManager_SplitOffCoreGroup(IVP_ListManager* Manager, IVP_Core* TargetMaster)
{
    for (;;)
    {
        // Step 1 (.text:063558ae-063558db): operator_new(0x4c) + FUN_0634b600
        // with the ORIGINAL manager's environment ([EDI+4]).
        IVP_ListManager* NewManager = new IVP_ListManager(Manager->Environment);

        // Step 2 (063558db-06355963): move/register Cores.
        for (int i = Manager->RegisteredCores.Len() - 1; i >= 0; --i)
        {
            IVP_Core* Core = Manager->RegisteredCores.ElementAt(i);

            if ((Core->Flags & 0xc) != 0)
            {
                // Kinematic-ish: track it on NewManager too, independent of
                // whether it belongs to TargetMaster's group (a kinematic
                // Core has no meaningful "master" -- it never enters the
                // union-find graph, Phase 2 of IVP_ListManager_FindDisconnectedCore
                // above already skips it for the same reason).
                IVP_Friction_Info_For_Core* Handle = new IVP_Friction_Info_For_Core(); // 063558fb-06355913
                Handle->FrictionSystem = NewManager;  // 06355922
                NewManager->RegisterCore(Core);       // 06355925
                Core->register_object_hash_entry(Handle); // 0635592d: FUN_06347c80
            }
            else if (FUN_063557c0(Core) == TargetMaster)
            {
                Manager->UnregisterCore(Core);
                NewManager->RegisterCore(Core);

                // Re-key Core's existing Manager-side hash entry in place
                // (Common.dll.txt:06355958-0635595d: `MOV [EAX+8],EBX` --
                // direct Key overwrite, NOT the unregister+rewrite+register
                // dance IVP_MergeRegisteredCores' own re-keying branch uses;
                // reproduced literally).
                IVP_FindFrictionInfo(Core, Manager)->FrictionSystem = NewManager; // 06355955-0635595d
            }
        }

        // Step 3 (06355963-06355a4a): move CorePairEntries (and their
        // AttachedCtx pair-records) whose group matches TargetMaster.
        for (int i = Manager->CorePairEntries.Len() - 1; i >= 0; --i)
        {
            IVP_ListManager_CorePairEntry* Entry = Manager->CorePairEntries.ElementAt(i);
            IVP_Core* PickA = Entry->CoreA;
            IVP_Core* PickB = Entry->CoreB;

            // If CoreA is kinematic-ish, use CoreB as the union-find probe
            // (swap so PickA is always the kinematic side when exactly one
            // is); if CoreA is not, but CoreB is, PickB is already the
            // kinematic side, PickA the probe -- Common.dll.txt:06355986-
            // 063559a1's own swap-or-not dance.
            bool EitherKinematic = false;
            IVP_Core* KinematicSide = nullptr;
            if ((PickA->Flags & 0xc) != 0)
            {
                EitherKinematic = true;
                KinematicSide = PickA;
                std::swap(PickA, PickB); // PickA (probe) = old PickB
            }
            else if ((PickB->Flags & 0xc) != 0)
            {
                EitherKinematic = true;
                KinematicSide = PickB;
            }

            IVP_Friction_Info_For_Core* KinHandleUnderManager = nullptr;     // local_14, 06355988
            IVP_Friction_Info_For_Core* KinHandleUnderNewManager = nullptr;  // local_10, 0635598c
            if (EitherKinematic)
            {
                KinHandleUnderManager = IVP_FindFrictionInfo(KinematicSide, Manager);       // 063559a3-063559b2
                KinHandleUnderNewManager = IVP_FindFrictionInfo(KinematicSide, NewManager); // 063559ab-063559bb
            }

            if (FUN_063557c0(PickA) != TargetMaster)
                continue;

            Manager->CorePairEntries.RemoveElement(Entry);
            NewManager->CorePairEntries.Add(Entry);

            for (int j = Entry->AttachedCtx.Len() - 1; j >= 0; --j)
            {
                IVP_Contact_Point* Node = Entry->AttachedCtx.ElementAt(j);

                // .text:063559ed-06355a09: retail's assert -- walk Manager's
                // dist list for Node (the whole list, no early exit) and trap
                // with `MOV [0],0` when it is not there. Cheap literal UB,
                // reproduced (owner's ruling 2026-10-08).
                bool bFound = false;
                for (IVP_Contact_Point* Walk = Manager->CtxListHead; Walk != nullptr; Walk = Walk->ListManagerListNext)
                {
                    if (Walk == Node)
                        bFound = true;
                }
                if (!bFound)
                {
                    volatile int* Trap = nullptr; // 06355a09
                    *Trap = 0;
                }

                Manager->RemoveCtx(Node);    // 06355a16: FUN_0634b8d0
                NewManager->InsertCtx(Node); // 06355a20: FUN_0634b9a0

                if (KinHandleUnderManager != nullptr) // 06355a25-06355a2b
                {
                    KinHandleUnderManager->FrDists.RemoveElement(Node); // 06355a2e: FUN_06355310
                    KinHandleUnderNewManager->FrDists.Add(Node);        // 06355a38: FUN_063556b0
                }
            }
        }

        // Step 4 (06355a4a-06355abd): release now-empty kinematic hash
        // entries and drop that Core's registration from whichever manager
        // it emptied out under.
        for (int i = Manager->RegisteredCores.Len() - 1; i >= 0; --i)
        {
            IVP_Core* Core = Manager->RegisteredCores.ElementAt(i);
            if ((Core->Flags & 0xc) == 0)
                continue;

            IVP_Friction_Info_For_Core* NewManagerHandle = IVP_FindFrictionInfo(Core, NewManager); // 06355a62-06355a69
            IVP_Friction_Info_For_Core* ManagerHandle = IVP_FindFrictionInfo(Core, Manager);       // 06355a6e-06355a73

            if (NewManagerHandle->FrDists.n_elems == 0) // 06355a7c: FUN_06355370
            {
                Core->release_object_hash_entry(NewManagerHandle); // 06355a88
                NewManager->UnregisterCore(Core);                  // 06355a92
            }
            if (ManagerHandle->FrDists.n_elems == 0) // 06355a99
            {
                Core->release_object_hash_entry(ManagerHandle); // 06355aa5
                Manager->UnregisterCore(Core);                  // 06355aad
            }
        }

        // Step 5 (.text:06355abd-06355b93). Both counts are compared as signed
        // words (`CMP word ptr [reg+0x3c],AX(2); JL`).
        if (static_cast<int16_t>(NewManager->RegisterCoreCallCount) < 2)
        {
            // 06355aea-06355b3d. RegisteredCores.elems[0] is read without a
            // count check; the record is released once (see RETAIL UB above),
            // then NewManager is deleted (06355b31-06355b3d, null-tested).
            IVP_Core* SoleCore = NewManager->RegisteredCores.elems[0];
            SoleCore->release_object_hash_entry(IVP_FindFrictionInfo(SoleCore, NewManager));
            delete NewManager;
            return;
        }
        if (static_cast<int16_t>(Manager->RegisterCoreCallCount) < 2)
        {
            // 06355b3f-06355b90: the same on Manager -- the caller's system.
            IVP_Core* SoleCore = Manager->RegisteredCores.elems[0];
            SoleCore->release_object_hash_entry(IVP_FindFrictionInfo(SoleCore, Manager));
            delete Manager;
            return;
        }

        // 06355ad2-06355ae5: FUN_063557e0 on Manager; none -> done, else loop.
        IVP_Core* NextDisconnected = IVP_ListManager_FindDisconnectedCore(Manager);
        if (NextDisconnected == nullptr)
            return;
        TargetMaster = NextDisconnected;
    }
}

// LAB_06355bb0 (item 3230) is slot 3 of the friction system's own vtable
// (.rdata:063a54d4 = 063a54c8+0x0c): IVP_ListManager::reset_time in
// ivp_friction_solver.cpp since 2026-10-08 (claude-code:cmn-phy3).

// ---------------------------------------------------------------------------
// Item 3232: FUN_06355c80 (Common.dll.txt:06355c80-06355c9d, 15 insns, tiny).
// ---------------------------------------------------------------------------
void IVP_ListManager_TickAllCorePairBudgets(IVP_ListManager* Manager)
{
    for (int i = Manager->CorePairEntries.Len() - 1; i >= 0; --i)
        IVP_ListManager_TickCorePairBudget(Manager->CorePairEntries.ElementAt(i));
}

// ---------------------------------------------------------------------------
// Item 3233: FUN_06355ca0 (Common.dll.txt:06355ca0-06355cba, 13 insns, tiny).
// ---------------------------------------------------------------------------
// Clears IVP_ListManager_CorePairEntry::_field24 (the SAME "budget" field
// IVP_ListManager_TickCorePairBudget below consumes/scales) for every entry.
void IVP_ListManager_ClearAllCorePairBudgets(IVP_ListManager* Manager)
{
    for (int i = Manager->CorePairEntries.Len() - 1; i >= 0; --i)
        Manager->CorePairEntries.ElementAt(i)->_field24 = 0;
}

// ---------------------------------------------------------------------------
// Item 3234: FUN_06355cc0 (Common.dll.txt:06355cc0-06355d1e, 36 insns).
// ---------------------------------------------------------------------------
// Disasm-traced in full this pass (Common.dll.txt:06355cc0-06355d1e):
// scales Entry->_field24 by Entry->CoreA->Environment->field_120 (both
// already-declared fields), and -- only while the scaled budget is
// non-negative (DAT_063a53d4 confirmed == 0.0f from the raw .rdata bytes)
// AND both CoreA/CoreB have a null AxisLock (+0x10) -- consumes some of it via
// IVP_ComputeAndApplyContactAxisImpulse (item 3240, below), accumulating the
// consumed amount into a float at IVP_Environment+0x68 and subtracting it back
// out of Entry->_field24. (2026-09-17, D-ivp-anomaly pass: the old 84-byte
// `manager_context` blob this used to index as `manager_context[0x34]` was
// split into named spans in ivp_environment.h to expose three impact counters;
// +0x68 is now the base of `manager_context_68`, so the arithmetic below is
// unchanged in address terms -- see that header for the new layout.)
void IVP_ListManager_TickCorePairBudget(IVP_ListManager_CorePairEntry* Entry)
{
    IVP_Environment* Env = Entry->CoreA->Environment;
    // 06355cca-06355cd7: the product stays on the x87 stack; FST to the local
    // (the argument below) and FST to +0x24.
    const double ScaledBudget = (double)(Env->field_120) * Entry->_field24;
    const float ScaledBudgetF = (float)(ScaledBudget);
    Entry->_field24 = ScaledBudgetF;

    // 06355cda-06355ce5: FCOMP 0.0f (DAT_063a53d4) on the unrounded product;
    // TEST AH,0x1; JNZ -> return on less OR unordered.
    if (!(ScaledBudget >= 0.0))
        return;
    // (Field renamed 2026-09-17: IVP_Core+0x10 is IVP_Core_AxisLock*, not
    // the IVP_Real_Object back-pointer it was declared as -- see that
    // struct's comment in ivp_core.h. This site only null-tests it, so the
    // behaviour is unchanged.)
    if (Entry->CoreA->AxisLock != nullptr)
        return;
    if (Entry->CoreB->AxisLock != nullptr)
        return;

    // 06355cf8-06355d1a: the result is a float (063561c0 ends in FLD float), so
    // the two one-operation float updates below round exactly like the x87.
    const float Consumed = IVP_ComputeAndApplyContactAxisImpulse(Entry, ScaledBudgetF);

    float* EnvTickAccumulator = reinterpret_cast<float*>(&Env->manager_context_68[0]); // IVP_Environment+0x68
    *EnvTickAccumulator += Consumed;
    Entry->_field24 -= Consumed;
}

// ---------------------------------------------------------------------------
// Item 3235: FUN_06355d20 (Common.dll.txt:06355d20-06355d4e, 16 insns, tiny).
// ---------------------------------------------------------------------------
// Pure scalar math, everything on the x87 stack (double here):
//   06355d20-06355d24 S = B + C
//   06355d28-06355d3c sqrt(|A*A - (D+D)*S|)   (FSUBP: A*A minus the product)
//   06355d3e          A - root                (FSUBR)
//   06355d42-06355d4a * (1.0f / S)            (DAT_063a53d0 = 1.0f; FDIV, FMULP)
double IVP_SolveContactAxisRoot(float A, float B, float C, float D)
{
    const double Sum = (double)(B) + C;
    const double Inner = (double)(A) * A - ((double)(D) + D) * Sum;
    const double Root = A - std::sqrt(std::fabs(Inner));
    return Root * (1.0 / Sum);
}

// ---------------------------------------------------------------------------
// Item 3236: FUN_06355d60 (Common.dll.txt:06355d60-06355daa, 23 insns).
// ---------------------------------------------------------------------------
// cdecl, five floats, result on the x87 stack (double here):
//   06355d60-06355d68 T = A / (D + E)                    (FDIVR)
//   06355d6c-06355d72 X = A - T*E                        (FSUBR)
//   06355d76-06355d7e float(X*X*C) spilled into E's own argument slot,
//                     which 06355d9e reads back (FSUB float [param_5])
//   06355d84          Y = T*D
//   06355d88-06355d9c (A*A*C) - (Y*Y*B)                  (FSUBP)
//   06355d9e-06355da2 (that - float(X*X*C)) * 0.5f       (DAT_063a5460)
double IVP_SolveContactAxisEnergySplit(float A, float B, float C, float D, float E)
{
    const double T = A / ((double)(D) + E);
    const double X = A - T * E;
    const float XSquaredC = (float)(X * X * C);
    const double Y = T * D;
    const double Difference = (double)(A) * A * C - Y * Y * B;
    return (Difference - XSquaredC) * 0.5f;
}

// ---------------------------------------------------------------------------
// Item 3237: FUN_06355db0 (Common.dll.txt:06355db0-06355ff4, 175 insns,
// medium-large).
// ---------------------------------------------------------------------------
// Thiscall on the 0x6c-byte scratch, RET 8. Re-traced 2026-10-14
// (claude-code:cmn-s2-ivp); the earlier port read RotSpeed where retail reads
// Speed, used a wrong epsilon, missed the 1.0f overwrite of the length slots
// and half of the kinematic test, and computed in float. Shape:
//   1. 06355db6-06355ddb: CoreB-argument kinematic ((Flags and 0xc) or
//      (Flags and 0x300)) -> slot +0x60 = CoreB argument, +0x64 = CoreA
//      argument; else +0x60 = CoreA, +0x64 = CoreB.
//   2. 06355ddb-06355e1e: Field[0..2] = float(CY.Speed - CX.Speed) (IVP_Core
//      +0xa4), normalised in place by FUN_06347e90 (real_length_plus_normize),
//      Field[0x0c] (+0x30) = float(returned length).
//   3. 06355e21-06355e4d: world spin of each core = MWorldFCore (+0xf4) vmult
//      RotSpeed (+0x94), FUN_06348580.
//   4. 06355e52-06355e7f: spin delta (WorldB - WorldA, float per component),
//      normalised in place by FUN_06347e90, Field[0x11] (+0x44) = float(length).
//   5. 06355e82-06355edd: Field[4..6] (+0x10) = CX matrix vimult delta;
//      the delta is negated (DAT_063a5498 = -1.0f) and Field[8..10] (+0x20) =
//      CY matrix vimult it (FUN_06366010).
//   6. 06355ee2-06355f8b: per side, w = float(RotInertia (+0x14) * axis) per
//      component, L = FUN_06347e60 (unrounded); +0x48/+0x4c = float(L) (FST);
//      FCOM 1e-10f (DAT_063a53d8), TEST AH,0x1: less or unordered -> both the
//      length slot and the inverse slot (+0x50/+0x54) become 1.0f; otherwise
//      inverse = float(1.0f / L).
//   7. 06355f8d-06355ff4: +0x38 = CY.Mass, +0x40 = CY.InvMass; CX kinematic
//      (same two-mask test) -> +0x34 = +0x38 * 10000.0f (DAT_063a565c),
//      +0x3c = +0x40 * 1e-4f (DAT_063a547c), +0x48 = +0x4c * 10000.0f,
//      +0x50 = +0x54 * 1e-4f; otherwise +0x34 = CX.Mass, +0x3c = CX.InvMass.
void IVP_PopulateContactAxisSetup(IVP_PairContactAxisSetup* Setup, IVP_Core* CoreA, IVP_Core* CoreB)
{
    constexpr float kLengthEpsilon = 1e-10f; // DAT_063a53d8 (0x2edbe6ff)
    constexpr float kNegOne = -1.0f;         // DAT_063a5498
    constexpr float kTenThousand = 10000.0f; // DAT_063a565c
    constexpr float kSmallScale = 1e-4f;     // DAT_063a547c (0x38d1b717)

    // Step 1: pick sides (Common.dll.txt:06355db6-06355ddb). Retail tests
    // `AL & 0xc` (Flags & 0xc) and, separately, `AH & 0x3` (the byte at
    // Flags+1, i.e. Flags & 0x300) -- combined here as a single mask.
    if ((CoreB->Flags & 0xc) != 0 || (CoreB->Flags & 0x300) != 0)
    {
        Setup->CoreA() = CoreB;
        Setup->CoreB() = CoreA;
    }
    else
    {
        Setup->CoreA() = CoreA;
        Setup->CoreB() = CoreB;
    }
    IVP_Core* CX = Setup->CoreA();
    IVP_Core* CY = Setup->CoreB();

    // Step 2 (06355ddb-06355e1e): y, z, x differences, each FSTP float; then
    // FUN_06347e90 on the three slots, return value FSTP to +0x30. Slot +0x0c
    // (Field[3]) is not written.
    IVP_U_Point SpeedDelta{};
    SpeedDelta.k[0] = CY->Speed.x - CX->Speed.x;
    SpeedDelta.k[1] = CY->Speed.y - CX->Speed.y;
    SpeedDelta.k[2] = CY->Speed.z - CX->Speed.z;
    const double SpeedDeltaLength = SpeedDelta.real_length_plus_normize();
    Setup->Field[0] = SpeedDelta.k[0];
    Setup->Field[1] = SpeedDelta.k[1];
    Setup->Field[2] = SpeedDelta.k[2];
    Setup->Field[0x0c] = (float)(SpeedDeltaLength);

    // Step 3 (06355e21-06355e4d): FUN_06348580(this = Core+0xf4, &Core->RotSpeed, out).
    IVP_U_Point RotSpeedX{};
    RotSpeedX.set(CX->RotSpeed.x, CX->RotSpeed.y, CX->RotSpeed.z);
    IVP_U_Point RotSpeedY{};
    RotSpeedY.set(CY->RotSpeed.x, CY->RotSpeed.y, CY->RotSpeed.z);
    IVP_U_Point WorldSpinX{};
    IVP_U_Point WorldSpinY{};
    CX->MWorldFCore.vmult(&RotSpeedX, &WorldSpinX);
    CY->MWorldFCore.vmult(&RotSpeedY, &WorldSpinY);

    // Step 4 (06355e52-06355e7f): x, y, z differences FSTP float; FUN_06347e90
    // on them, return value FSTP to +0x44.
    IVP_U_Point SpinDelta{};
    SpinDelta.k[0] = WorldSpinY.k[0] - WorldSpinX.k[0];
    SpinDelta.k[1] = WorldSpinY.k[1] - WorldSpinX.k[1];
    SpinDelta.k[2] = WorldSpinY.k[2] - WorldSpinX.k[2];
    const double SpinDeltaLength = SpinDelta.real_length_plus_normize();
    Setup->Field[0x11] = (float)(SpinDeltaLength);

    // Step 5 (06355e82-06355edd): FUN_06366010(this = CX+0xf4, &delta, Setup+0x10);
    // delta *= -1.0f per component (FSTP float); FUN_06366010(this = CY+0xf4,
    // &delta, Setup+0x20). vimult writes only k[0..2].
    IVP_U_Point SpinAxisX{};
    CX->MWorldFCore.vimult(&SpinDelta, &SpinAxisX);
    SpinDelta.k[0] = SpinDelta.k[0] * kNegOne;
    SpinDelta.k[1] = SpinDelta.k[1] * kNegOne;
    SpinDelta.k[2] = SpinDelta.k[2] * kNegOne;
    IVP_U_Point SpinAxisY{};
    CY->MWorldFCore.vimult(&SpinDelta, &SpinAxisY);
    Setup->Field[4] = SpinAxisX.k[0]; Setup->Field[5] = SpinAxisX.k[1]; Setup->Field[6] = SpinAxisX.k[2];
    Setup->Field[8] = SpinAxisY.k[0]; Setup->Field[9] = SpinAxisY.k[1]; Setup->Field[10] = SpinAxisY.k[2];

    // Step 6 (06355ee2-06355f8b). IVP_Core+0x14 is RotInertia. Each weighted
    // component is FSTP float; FUN_06347e60 leaves the length unrounded; FST
    // stores float(L); TEST AH,0x1 after FCOM 1e-10f: less or unordered ->
    // 1.0f (EDI = 0x3f800000) into the length slot AND the inverse slot,
    // else inverse = float(1.0f / L) with L unrounded.
    IVP_U_Point WeightedX{};
    WeightedX.k[0] = CX->RotInertia.x * Setup->Field[4];
    WeightedX.k[1] = CX->RotInertia.y * Setup->Field[5];
    WeightedX.k[2] = CX->RotInertia.z * Setup->Field[6];
    const double LengthX = WeightedX.real_length();
    Setup->Field[0x12] = (float)(LengthX);
    if (!(LengthX >= kLengthEpsilon))
    {
        Setup->Field[0x12] = 1.0f; // 06355f23
        Setup->Field[0x14] = 1.0f; // 06355f28
    }
    else
    {
        Setup->Field[0x14] = (float)(1.0 / LengthX); // 06355f2d-06355f35
    }

    IVP_U_Point WeightedY{};
    WeightedY.k[0] = Setup->Field[8] * CY->RotInertia.x;
    WeightedY.k[1] = CY->RotInertia.y * Setup->Field[9];
    WeightedY.k[2] = CY->RotInertia.z * Setup->Field[10];
    const double LengthY = WeightedY.real_length();
    Setup->Field[0x13] = (float)(LengthY);
    if (!(LengthY >= kLengthEpsilon))
    {
        Setup->Field[0x13] = 1.0f; // 06355f76
        Setup->Field[0x15] = 1.0f; // 06355f7b
    }
    else
    {
        Setup->Field[0x15] = (float)(1.0 / LengthY); // 06355f80-06355f88
    }

    // Step 7 (06355f8d-06355ff4). IVP_Core+0x20 is Mass, +0x40 InvMass. The
    // kinematic test is TEST CL,0xc / TEST CH,0x3 on CX's flags.
    Setup->Field[0x0e] = CY->Mass;
    Setup->Field[0x10] = CY->InvMass;
    if ((CX->Flags & 0xc) == 0 && (CX->Flags & 0x300) == 0)
    {
        Setup->Field[0x0d] = CX->Mass;
        Setup->Field[0x0f] = CX->InvMass;
    }
    else
    {
        Setup->Field[0x0d] = Setup->Field[0x0e] * kTenThousand;
        Setup->Field[0x0f] = Setup->Field[0x10] * kSmallScale;
        Setup->Field[0x12] = Setup->Field[0x13] * kTenThousand;
        Setup->Field[0x14] = Setup->Field[0x15] * kSmallScale;
    }
}

// ---------------------------------------------------------------------------
// Item 3238: FUN_06356000 (Common.dll.txt:06356000-06356045, 31 insns).
// ---------------------------------------------------------------------------
// 06356003-0635601c: the first result FSTP float to +0x58; 0635601f-06356041:
// the second FST float to +0x5c, then the unrounded second result plus the
// float at +0x58 is FSTP'd to +0x68.
void IVP_ComputeContactAxisEnergies(IVP_PairContactAxisSetup* Setup)
{
    Setup->Field[0x16] = (float)(IVP_SolveContactAxisEnergySplit(
        Setup->Field[0x11], Setup->Field[0x12], Setup->Field[0x13], Setup->Field[0x14], Setup->Field[0x15]));

    const double Result2 = IVP_SolveContactAxisEnergySplit(
        Setup->Field[0x0c], Setup->Field[0x0d], Setup->Field[0x0e], Setup->Field[0x0f], Setup->Field[0x10]);
    Setup->Field[0x17] = (float)(Result2);

    Setup->Field[0x1a] = (float)(Result2 + Setup->Field[0x16]);
}

// ---------------------------------------------------------------------------
// Item 3239: FUN_06356050 (Common.dll.txt:06356050-063561b2, 115 insns,
// medium).
// ---------------------------------------------------------------------------
// Thiscall on the scratch, RET 4. Adds the impulse of the two roots onto the
// cores' SpeedChange (+0x84) and RotSpeedChange (+0x74). Only CX (+0x60) is
// gated on (Flags and 0xc); the CY block always runs.
void IVP_ApplyContactAxisImpulses(IVP_PairContactAxisSetup* Setup, float Scale)
{
    // 06356053-06356096: each root's fourth argument is FSTP float, each
    // result FSTP float (local_8 / the Scale slot).
    const float Temp1 = (float)(IVP_SolveContactAxisRoot(
        Setup->Field[0x11], Setup->Field[0x14], Setup->Field[0x15], Scale * Setup->Field[0x16]));
    const float Temp2 = (float)(IVP_SolveContactAxisRoot(
        Setup->Field[0x0c], Setup->Field[0x0f], Setup->Field[0x10], Scale * Setup->Field[0x17]));

    IVP_Core* CoreA = Setup->CoreA();
    if ((CoreA->Flags & 0xc) == 0) // 0635609d TEST byte [CX],0xc; JNZ 06356117
    {
        // 063560a2-063560dd: X = Temp2 * +0x3c stays on the stack; each
        // component is float(X * axis + accumulator).
        const double X = (double)(Temp2) * Setup->Field[0x0f];
        CoreA->SpeedChange.z = (float)(X * Setup->Field[2] + CoreA->SpeedChange.z);
        CoreA->SpeedChange.x = (float)(X * Setup->Field[0] + CoreA->SpeedChange.x);
        CoreA->SpeedChange.y = (float)(X * Setup->Field[1] + CoreA->SpeedChange.y);

        // 063560e3-06356114: Y = Temp1 * +0x50, the same over +0x10..+0x18.
        const double Y = (double)(Temp1) * Setup->Field[0x14];
        CoreA->RotSpeedChange.z = (float)(Y * Setup->Field[6] + CoreA->RotSpeedChange.z);
        CoreA->RotSpeedChange.x = (float)(Y * Setup->Field[4] + CoreA->RotSpeedChange.x);
        CoreA->RotSpeedChange.y = (float)(Y * Setup->Field[5] + CoreA->RotSpeedChange.y);
    }

    // 06356117-06356142: the linear axis is negated in place (DAT_063a5498 = -1.0f).
    Setup->Field[0] = Setup->Field[0] * -1.0f;
    Setup->Field[1] = Setup->Field[1] * -1.0f;
    Setup->Field[2] = Setup->Field[2] * -1.0f;

    IVP_Core* CoreB = Setup->CoreB();
    // 06356145-06356177: X2 = Temp2 * +0x40 is spilled as float (0635614c).
    const float X2 = Temp2 * Setup->Field[0x10];
    CoreB->SpeedChange.z = (float)((double)(Setup->Field[2]) * X2 + CoreB->SpeedChange.z);
    CoreB->SpeedChange.x = (float)((double)(Setup->Field[0]) * X2 + CoreB->SpeedChange.x);
    CoreB->SpeedChange.y = (float)((double)(Setup->Field[1]) * X2 + CoreB->SpeedChange.y);

    // 0635617a-063561ac: Y2 = Temp1 * +0x54 stays on the stack.
    const double Y2 = (double)(Temp1) * Setup->Field[0x15];
    CoreB->RotSpeedChange.z = (float)(Y2 * Setup->Field[10] + CoreB->RotSpeedChange.z);
    CoreB->RotSpeedChange.x = (float)(Y2 * Setup->Field[8] + CoreB->RotSpeedChange.x);
    CoreB->RotSpeedChange.y = (float)(Y2 * Setup->Field[9] + CoreB->RotSpeedChange.y);
}

// ---------------------------------------------------------------------------
// Item 3240: FUN_063561c0 (Common.dll.txt:063561c0-06356235, 27 insns).
// ---------------------------------------------------------------------------
float IVP_ComputeAndApplyContactAxisImpulse(IVP_ListManager_CorePairEntry* Entry, float Budget)
{
    constexpr float kEnergyEpsilon = 1e-10f; // DAT_063a53d8, see IVP_PopulateContactAxisSetup's own citation
    constexpr float kBudgetCapFraction = 0.1f; // DAT_063a5464, confirmed bit-exact (CD CC CC 3D)

    IVP_PairContactAxisSetup Setup{};
    IVP_PopulateContactAxisSetup(&Setup, Entry->CoreA, Entry->CoreB);
    IVP_ComputeContactAxisEnergies(&Setup);

    // 063561dd-063561fc: Cap = +0x68 * 0.1f stays on the stack; FCOMP of the
    // budget against it, TEST AH,0x41; JNZ keeps the budget on not-greater or
    // unordered, otherwise the budget slot gets float(Cap).
    const float EnergySum = Setup.Field[0x1a];
    const double Cap = (double)(EnergySum) * kBudgetCapFraction;
    if (Budget > Cap)
        Budget = (float)(Cap);

    // 063561fc-0635620b: FCOMP 1e-10f, TEST AH,0x1: less or unordered -> 0.0f.
    if (!(EnergySum >= kEnergyEpsilon))
        return 0.0f; // DAT_063a53d4

    IVP_ApplyContactAxisImpulses(&Setup, Budget / EnergySum); // 06356219-06356229, FSTP float argument
    return Budget; // 0635622e FLD float
}
