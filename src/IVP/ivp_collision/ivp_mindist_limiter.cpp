// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Complex-ledge mindist spawn limiter: IVP_Ledge_Mindist / IVP_Complex_Mindist.
//
// Retail: Common.dll.c / .txt 0x06362620-0x063632b0 (decomp-tracker items
// 3488-3507) plus the allocator FUN_0634e0b0 and the base ctor/dtor pair
// FUN_0634dd10/FUN_0634dd60. See the large comment block in ivp_mindist.h
// above the class declarations for the full identification writeup (class
// identity, field-offset evidence, relationship to the pre-existing
// IVP_Impact_Solver_Long_Term class, and what is/isn't reproduced byte-exactly).
//
// decomp-tracker agent: claude-common-mindist-limiter, 2026-09-06.
//
// FULLPORT-DEFERRED[IVP]: several retail call targets in this cluster reach
// into IVP internals that are not ported anywhere in this tree yet --
// IVP_Real_Object::environment chain double-dispatch (the "fill two floats"
// calls in FUN_06362840/LAB_06362650/LAB_06362920), the compound-core /
// anomaly-limiter helpers (FUN_0634fec0/ffa0/06350020/0350350/0350310),
// synapse-list linkage (FUN_06391ef0/06391dfc), hesse-vector recompute
// (FUN_06350250), and the base class's own refcount/list bookkeeping in
// FUN_0634dd10/dd60. Each is represented below by a small named stub so the
// *shape* of the call is visible even though the body is a no-op, matching
// the convention already established in common_physys.h.
//
// FUN_06350310/FUN_06350350 (decomp-tracker items 3132/3133) hand a mindist
// to the phantom (IVP_Controller_Phantom, object +0x1c) of each side:
// mindist_entered_volume 06344400 / mindist_left_volume 06344580, ported in
// src/IVP/ivp_physics/ivp_phantom.cpp (cmn-ivp3d, 2026-10-08).
#include "../ivp_physics/ivp_mindist.h"
#include "../ivp_physics/ivp_environment.h"
#include "../ivp_physics/ivp_ov_element.h" // IVP_Mindist_Manager (IVP_Environment::mindist_manager, +0x10) -- DetachMindist is defined in this file.
#include "../ivp_physics/ivp_material.h"
#include "ivp_listmanager_topology.h" // for IVP_ListManager_RefreshAttachedCtxAndCountLive / IVP_ComputePairRecordMassFactor's own declarations (item 3231/3243, d229:cluster-06355200) -- included here so the compiler enforces the signature match against ivp_listmanager_topology.h's declarations instead of relying on link-time agreement alone.
#include "../ivp_utility/ivp_vector_math.h"
#include "../ivp_utility/ivp_core_trap.h" // IVP_CoreTrap (retail's `MOV dword ptr [0],0`)
#include "../../include/common_physys.h"
#include "ivp_ledge_sweep_solver.h" // the collision-time estimator dispatch layer (items 3380-3384) and its eight drivers
#include "ivp_mindist_create.h" // IVP_Create_Exact_Mindists (FUN_0634e0b0), called by SpawnOrDeferChild (06362c70)
#include "../ivp_physics/ivp_phantom.h" // IVP_Controller_Phantom (06350310 / 06350350), cmn-ivp3d
#include "../ivp_physics/ivp_contact_point.h" // IVP_Try_To_Generate_Managed_Friction (06356bc0), cmn-ivp3d
#include "ivp_mindist_minimize.h" // IVP_Mindist_Minimize_Solver: 06352cf0 / 06352b90 build it on the stack
#include "ivp_compact_ledge_solver.h" // IVP_Compact_Ledge_Solver: the contact-frame leaves 06359850 / 06359ae0 and 0635a1e0
#include "../ivp_physics/ivp_impact.h" // IVP_Mindist_Do_Impact (FUN_06358680), called by 06358cb0
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <new>
#include <type_traits>


// ---------------------------------------------------------------------------
// Retail's global mindist tolerance block  (.data:0658aa10 .. 0658ab44)
// ---------------------------------------------------------------------------
// RESOLVED 2026-09-17 (lane claude:w16-ivp, decomp-tracker item 3117). Five
// separate globals in this cluster -- DAT_0658aa10, DAT_0658aa18,
// DAT_0658ab24, DAT_0658ab34 and DAT_0658ab40 -- were carried as "opaque, not
// independently resolved" by several earlier passes (see
// IVP_Ledge_Mindist::UpdatePerTick's history below). They are
// all the SAME object, and that object is already ported.
//
// Proof, from the listing only:
//   1. LAB_0634da30 is `JMP LAB_0634da40` and LAB_0634da40 is
//      `MOV ECX,0x0658aa10; JMP FUN_0634db40` (Common.dll.txt:0634da40 --
//      `b9 10 aa 58 06` is a MOV-immediate, so ECX is the ADDRESS, not a
//      load). LAB_0634da30's only XREF is the static-initializer pointer at
//      .data:063ea60c, i.e. this object is built once at DLL load.
//   2. FUN_0634db40 (decomp-tracker item 3081) and its callee FUN_0634da50
//      (item 3079) are already ported verbatim in src/IVP/ivp_physics/ivp_mindist_settings.cpp
//      and write, in FLOAT-index terms: [0x00], [0x01], [0x02..0x41] (a
//      64-entry ramp), [0x42..0x4a], then `*(float*)(p+0x130) = 1.0f` and
//      `*(int*)(p+0x12c) = 1000`.
//   3. Every consumer's byte offset therefore lands on a named field:
//        DAT_0658aa10 = block[0x00]            (= param*0.1f = 0.001f)
//        DAT_0658aa18 = &block[0x02]           the 64-entry ramp's base, which
//                       is why every user indexes it as `[idx*4 + 0658aa18]`
//        DAT_0658ab24 = block[0x45]            (0x0658ab24-0x0658aa10 = 0x114)
//        DAT_0658ab34 = block[0x49]            (offset 0x124 = block[1]*0.1f)
//        DAT_0658ab3c = block[0x4b]            (offset 0x12c, the int 1000)
//        DAT_0658ab40 = block[0x4c]            (offset 0x130, == 1.0f)
//        DAT_0658ab44 = block[0x4d]            (offset 0x134, an int32 that
//                       FUN_0634db40 does NOT initialize -- see below)
// 2026-10-08 (claude-code:cmn-phy2): the block is now the one shared global
// G_IVP_MindistSettings (ivp_mindist_settings.h), constructed at DLL load like
// retail's .data:063ea60c entry. The lazily-built local copy this file used to
// keep (and the second copy in ivp_ledge_sweep_solver.cpp) are gone; the
// accessors below keep their names and read the global.
#include "../ivp_physics/ivp_mindist_settings.h"

namespace
{
    // DAT_0658aa18[Index] -- retail's unclamped ramp index, see
    // FIVP_MindistSettingsStorage for the out-of-range tail.
    float IVP_ToleranceRamp(uint32_t Index) { return IVP_MindistSettingsRamp(Index); }
    float IVP_ToleranceBase() { return G_IVP_MindistSettings.real_coll_dist; } // DAT_0658aa10
    float IVP_ToleranceMinRate() { return G_IVP_MindistSettings.speed_after_keeper_dist; } // DAT_0658ab24
    float IVP_ToleranceBias() { return G_IVP_MindistSettings.mindist_change_force_dist; } // DAT_0658ab34
    float IVP_ToleranceEpsScale() { return G_IVP_MindistSettings.Field_0x130; } // DAT_0658ab40

    // DAT_0658ab44 (+0x134), only read/written by FUN_0634f320 (:0634f57b /
    // :0634f586 / :0634f5a6) as a plain int counter.
    int32_t& IVP_ToleranceRecomputeCounter()
    {
        return G_IVP_MindistSettings.Field_0x134;
    }
}

// FUN_0634f320 (decomp-tracker item 3117), defined far below next to
// IVP_Ledge_Mindist::UpdatePerTick -- its only in-file caller.
void IVP_RescheduleCollisionEvent(IVP_Ledge_Mindist* This, int32_t RearmWhenFar, int32_t EventMode);

namespace
{
    // ---- FULLPORT-DEFERRED[IVP] stubs -------------------------------------
    // Named after what retail's call site does, not after the retail
    // function address, so the intent reads without cross-referencing the
    // decompilation. None of these are among this task's 21 tracked items.

    // (IVP_DEFERRED_CalcHesseVectorHint removed 2026-10-08, cmn-ivp2c: it was
    // the range_manager->get_coll_range_intra_objects query whose R0 + R1 is
    // FUN_06362c70's radius argument, now made directly by its callers.)

    // (IVP_DEFERRED_DemoteToSimpleTracking / IVP_DEFERRED_LinkAsSimpleSynapse
    // removed 2026-10-08: FUN_06350020 + FUN_0634e8a0 are IVP_Mindist_Manager::
    // remove_hull_mindist + insert_exact_mindist_no_recalc, called directly by
    // IVP_Complex_Mindist::DeactivateToSimple.)

    // (IVP_DEFERRED_LinkAsPendingSynapse removed 2026-10-08: FUN_0634fec0 +
    // FUN_0634e960 are IVP_Mindist_Manager::DetachMindist +
    // insert_invalid_mindist, called directly.)

    // REPLACED BY A REAL PORT (2026-09-17, lane claude:w16-ivp). The stub that
    // used to sit here stood in for FUN_0634f320 (decomp-tracker item 3117);
    // that function is now written out in full as
    // IVP_RescheduleCollisionEvent below, and UpdatePerTick calls it directly.
    // What remains deferred out of that function is ONE named call -- the
    // collision-time estimator dispatch, IVP_DEFERRED_InvokeCollisionTime-
    // Estimator just below -- not the whole function.

    // NO LONGER DEFERRED (2026-09-17, lane claude:w26-simunit). Three stubs
    // used to sit here -- IVP_DEFERRED_DequeueCollisionEvent (FUN_063609f0,
    // item 3403, called at :0634f3a2), IVP_DEFERRED_EnqueueCollisionEvent
    // (FUN_06362f90's inlined call site at :0634f964-:0634f98b) and
    // IVP_DEFERRED_RearmPendingHullCheck (FUN_0634fec0, item 3125, called at
    // :0634f3fe). All three were blocked on the same thing: IVP_Environment's
    // sim_unit_manager (+0x04) and mindist_manager (+0x10) were untyped `void*`.
    // Both now have real classes -- IVP_Sim_Unit_Manager (ivp_environment.h) and
    // IVP_Mindist_Manager (ivp_ov_element.h) -- so IVP_RescheduleCollisionEvent
    // below calls
    //     Env->sim_unit_manager->CancelEvent(This->EventQueueHandle)
    //     Env->mindist_manager->DetachMindist(This)
    //     This->EventQueueHandle = Env->sim_unit_manager->EnqueueEvent(This, Hi, Lo)
    // directly. The event time that function computes is no longer dropped.

    // (IVP_DEFERRED_LedgeMindistVtableSlot8 and
    // IVP_DEFERRED_CheckSeparationWithinTolerance removed 2026-10-08, cmn-phy8:
    // slot 8 is the virtual IVP_Ledge_Mindist::do_impact, and the tolerance
    // test is written out in UpdatePerTick with the threshold kept in double.)

    // (IVP_DEFERRED_RecalcMindist removed 2026-10-08, cmn-phy6: FUN_06352b90 is
    // IVP_Ledge_Mindist::recalc_mindist, defined after DispatchNarrowPhaseSolve.)

    // FUN_06350310 (.text:06350310-0635034b, cdecl, one argument), cmn-phy5
    // 2026-10-08: unless StateFlags & 0xc00 already, set
    // flags = (flags & 0xfffdf7ff) | 0x10400 and hand the mindist to each
    // object's phantom (+0x1c) that exists, object 0 first.
    void IVP_AddMindistToPhantoms(IVP_Ledge_Mindist* M)
    {
        if ((M->StateFlags & 0xc00u) != 0) // :06350318 `TEST AH,0xc; JNZ`
            return;
        M->StateFlags = (M->StateFlags & 0xfffdf7ffu) | 0x10400u;
        for (int32_t I = 0; I < 2; ++I)
        {
            IVP_Controller_Phantom* const Phantom = static_cast<IVP_Controller_Phantom*>(M->synapse[I].l_obj->first_child); // :0635032a / :0635033a
            if (Phantom != nullptr)
                Phantom->mindist_entered_volume(M); // FUN_06344400 (cmn-ivp3d)
        }
    }

    // FUN_06350350 (.text:06350350-06350386, cdecl, one argument), cmn-phy5
    // 2026-10-08: if StateFlags & 0xc00, clear 0x30c00 (`AND 0xfffcf3ff`) and
    // take the mindist out of each object's phantom that exists. (The
    // file-head note's "clears 0x0c0300" is wrong.)
    void IVP_RemoveMindistFromPhantoms(IVP_Ledge_Mindist* M)
    {
        if ((M->StateFlags & 0xc00u) == 0) // :06350358 `TEST AH,0xc; JZ`
            return;
        M->StateFlags &= 0xfffcf3ffu;
        for (int32_t I = 0; I < 2; ++I)
        {
            IVP_Controller_Phantom* const Phantom = static_cast<IVP_Controller_Phantom*>(M->synapse[I].l_obj->first_child); // :06350365 / :06350375
            if (Phantom != nullptr)
                Phantom->mindist_left_volume(M); // FUN_06344580 (cmn-ivp3d)
        }
    }

    // ---------------------------------------------------------------------
    // D-ctx-resolve pass (2026-09-10): real port of the pair-record
    // find-or-create chain FUN_06356bc0/item 3262 dispatches into --
    // FUN_0635a900 (item 3302), FUN_0635a950 (item 3303) and FUN_063539d0
    // (item 3204). This is the piece that gives Ctx (IVP_ResolveCandidatePairRecord's
    // own Ctx out-param, below) a real, non-null value for the first time --
    // see ivp_mindist.h's IVP_Contact_Point struct-level comment for the
    // full field-by-field evidence trail this reproduces. FUN_06356bc0's OTHER
    // half -- the still-unidentified "ListManager" class (items 3003/3004/
    // 3005/3222/2992) that produces out1/out2's ListManager pointer -- remains
    // a separate, larger, still-deferred subsystem; only Ctx/out2 (the pair
    // record and its found-vs-created tag) are real below.
    // ---------------------------------------------------------------------

    // D155-gap-closing pass (2026-09-10): FUN_06356740 (item 3250) and
    // FUN_06356810 (item 3251), the "unconfirmed extra match check" D155's
    // own notes on item 3302 flagged as omitted. Both fully traced this pass
    // (Common.dll.txt:437571-437657 and :437667-437733, 73+60 insns, zero
    // unresolved instructions).
    //
    // FUN_06356740: thiscall(this=one pair-record side's own node address,
    // i.e. Record+8 or Record+0x1c; stack param=pointer to one of Mindist's
    // two embedded 0x1c-byte synapse slots, Mindist+0x18 or Mindist+0x34).
    // Reads the node's own "kind" byte at node+0xE (== Record's _hash1/_hash2
    // LOW byte, node+0xE lands exactly on _hash1 for node1=Record+8, on
    // _hash2 for node2=Record+0x1c) as a SIGNED int8_t (`MOVSX EAX,AX; SAR
    // EAX,8` on a byte loaded into AH is exactly `(int8_t)byte`), compares it
    // against the slot's own FeatureType (slot+0x1a -- which for SlotA/SlotB
    // IS IVP_Ledge_Mindist::ShapeTypeA/ShapeTypeB at their already-declared
    // offsets, +0x32/+0x4e), then dispatches a 4-way switch (0-3, >3 traps)
    // comparing the node's Feature (node+0x10, == Record::Feature1/Feature2)
    // against the slot's own Feature (slot+0x14, == Mindist::FeatureA/
    // FeatureB) using the class's existing IVP_Compact_Edge helpers:
    //   0 (Vertex): same owning IVP_Compact_Ledge (via GetCompactTriangle()->
    //      GetCompactLedge(), exactly matching the disasm's own triangle-base-
    //      minus-(TriIndex+1)*16 arithmetic) AND same GetStartPointIndex().
    //   1 (Edge): the node's own edge pointer equals the slot's, OR its
    //      GetOppositeEdge() does (`(Data<<1)>>17` then `this+offset*4` is
    //      exactly GetOppositeIndex()/GetOppositeEdge()).
    //   2 (Face): same owning IVP_Compact_Triangle (masked-pointer equality).
    //   3: always matches (no further geometry check).
    inline bool IVP_CompareSideFeature(uint16_t RecordSideHash, const void* RecordSideFeature, int16_t SlotShapeType, const void* SlotFeature)
    {
        const int8_t Kind = static_cast<int8_t>(RecordSideHash & 0xFF);
        if (Kind != SlotShapeType)
            return false;

        const IVP_Compact_Edge* NodeEdge = static_cast<const IVP_Compact_Edge*>(RecordSideFeature);
        const IVP_Compact_Edge* TargetEdge = static_cast<const IVP_Compact_Edge*>(SlotFeature);

        switch (Kind)
        {
        case 0:
            return NodeEdge->GetCompactLedge() == TargetEdge->GetCompactLedge()
                && NodeEdge->GetStartPointIndex() == TargetEdge->GetStartPointIndex();
        case 1:
            return NodeEdge == TargetEdge || NodeEdge->GetOppositeEdge() == TargetEdge;
        case 2:
            return NodeEdge->GetCompactTriangle() == TargetEdge->GetCompactTriangle();
        case 3:
            return true;
        default:
            // :063567e7 the library's CORE (`MOV dword ptr [0],0`); then
            // :063567f1 returns 0.
            IVP_CoreTrap();
            return false;
        }
    }

    // FUN_06356810: thiscall(this=Record, stack param=Mindist). Tries BOTH
    // orientations (a record's side1/side2 can be assigned to either physical
    // object depending on the StateFlags-bit-0x100 tag active at construction
    // time, see IVP_Contact_Point::IVP_Contact_Point below) -- Object identity AND, per
    // side, IVP_CompareSideFeature above must both agree.
    inline bool IVP_ConfirmPairRecordMatch(const IVP_Contact_Point* Record, const IVP_Ledge_Mindist* Mindist)
    {
        const IVP_Synapse_Real& SynA = Mindist->synapse[0];
        const IVP_Synapse_Real& SynB = Mindist->synapse[1];
        if (Record->Object1 == SynA.l_obj && Record->Object2 == SynB.l_obj)
        {
            if (IVP_CompareSideFeature(Record->_hash1, Record->Feature1, SynA.status, SynA.edge)
             && IVP_CompareSideFeature(Record->_hash2, Record->Feature2, SynB.status, SynB.edge))
                return true;
        }
        if (Record->Object2 == SynA.l_obj && Record->Object1 == SynB.l_obj)
        {
            if (IVP_CompareSideFeature(Record->_hash2, Record->Feature2, SynA.status, SynA.edge)
             && IVP_CompareSideFeature(Record->_hash1, Record->Feature1, SynB.status, SynB.edge))
                return true;
        }
        return false;
    }

    // DAT_0658ab1c (read by IVP_Contact_Point::IVP_Contact_Point's GapDistance write below and
    // by FUN_06356568/FUN_06362da6/FUN_06368ad9/FUN_06368dc9) is NOT a clock:
    // it is IVP_Mindist_Settings+0x10c (0x0658ab1c - 0x0658aa10), set once by
    // the settings constructor at DLL load. The old `= 0.0f` placeholder that
    // stood here is gone (2026-10-08, claude-code:cmn-phy2); readers use
    // G_IVP_MindistSettings.friction_dist.

    // FUN_0635a900 (item 3302, Common.dll.txt:443852-443893). Walks the
    // intrusive list rooted at Mindist->ObjectA->mindist_list
    // (IVP_Real_Object+0x28, ivp_real_object.h) looking for an existing pair
    // record keyed by (Mindist->ObjectA, Mindist->ObjectB). Retail's raw list
    // entries are NODE pointers -- the embedded Next1/Next2 sub-struct's own
    // address (record+0x08 or record+0x1C), NOT the record's base -- recovered
    // via each node's own self-relative Delta field (int16 at node+0xC, i.e.
    // IVP_Contact_Point::Delta1/Delta2). UPDATE (D155-gap-closing pass,
    // 2026-09-10): the coarse Object1/Object2-vs-ObjectB pre-check is now
    // followed by the real FUN_06356810 confirmation (IVP_ConfirmPairRecordMatch
    // above) -- a candidate whose Object identity matches but whose feature
    // confirmation fails is REJECTED and the walk continues to the next list
    // node, exactly matching retail's own JNZ-back-into-the-loop behavior
    // (Common.dll.txt:0635a930-0635a934), closing item 3302's previously
    // omitted match-confirmation gap for real.
    inline IVP_Contact_Point* IVP_FindExistingPairRecord(IVP_Ledge_Mindist* Mindist)
    {
        IVP_Real_Object* const ObjB = Mindist->synapse[1].l_obj;
        void* NodePtr = Mindist->synapse[0].l_obj->mindist_list;
        while (NodePtr != nullptr)
        {
            char* NodeBytes = static_cast<char*>(NodePtr);
            int16_t Delta = *reinterpret_cast<int16_t*>(NodeBytes + 0x0C); // Delta1/Delta2, self-relative
            IVP_Contact_Point* Record = reinterpret_cast<IVP_Contact_Point*>(NodeBytes + Delta);
            if ((Record->Object1 == ObjB || Record->Object2 == ObjB)
                && IVP_ConfirmPairRecordMatch(Record, Mindist))
                return Record;
            NodePtr = *reinterpret_cast<void**>(NodeBytes); // Next field, node+0x00
        }
        return nullptr;
    }

    // FUN_063539d0 (the pair record's constructor) is IVP_Contact_Point's
    // constructor, ivp_contact_point.cpp (cmn-ivp3f 2026-10-09).

} // namespace -- IVP_FindOrCreatePairRecord has external linkage (cmn-ivp3d 2026-10-08)

    // FUN_0635a950 (item 3303, Common.dll.txt:443903-443951). Plain
    // find-or-create wrapper: tries IVP_FindExistingPairRecord first; on a
    // miss, allocates a fresh IVP_Contact_Point and constructs it via
    // IVP_Contact_Point::IVP_Contact_Point. *OutTag mirrors retail's own out-tag: 0 = found
    // existing, 1 = newly created. Declared in ivp_listmanager_topology.h
    // (try_to_generate_managed_friction 06356bc0 in ivp_contact_point.cpp).
    IVP_Contact_Point* IVP_FindOrCreatePairRecord(IVP_Ledge_Mindist* Mindist, int32_t* OutTag)
    {
        IVP_Contact_Point* Existing = IVP_FindExistingPairRecord(Mindist);
        if (Existing != nullptr)
        {
            *OutTag = 0;
            return Existing;
        }

        IVP_Contact_Point* Fresh = new IVP_Contact_Point(Mindist); // operator_new(0x74) + FUN_063539d0
        *OutTag = 1;
        return Fresh;
    }

namespace
{


    // ---------------------------------------------------------------------
    // D-listmanager pass (2026-09-10): real port of the ListManager class
    // (decomp-tracker items 2992/3002/3003/3004/3005/3226/3241/3222) -- see
    // ivp_mindist.h's own IVP_ListManager class-level comment for the full
    // field-by-field evidence trail.
    //
    // UPDATE (D-target063555b0-followup pass, 2026-09-10): the IVP_Core-side
    // hash-table trio this pass originally left out of reach --
    // FUN_06347c40/FUN_06347c80/FUN_06347d20 -- is now fully traced and
    // ported as IVP_Core::find/register/unregister_object_hash_entry
    // (items 2870-2872, ivp_core.h/.cpp). This pass additionally traces and
    // ports FUN_06347d50 (item 2873, IVP_Core::release_object_hash_entry),
    // FUN_0634ba50 (IVP_ListManager::UnregisterCore, below), and
    // FUN_06355310/FUN_06355710 (confirmed generic IVP_U_Vector<T>::
    // RemoveElement() instantiations, items 3217/3225) -- closing out
    // MergeFrom's Phase 2 (IVP_MergeRegisteredCores) and Phase 1's own
    // per-node teardown (IVP_RetirePairRecordBookkeeping / FUN_0634b850,
    // item 3001) for real. Only FUN_06349180/FUN_06349110 (items 2932/2931,
    // Core's own SEPARATE +0x150/+0x15c tear-off-handle tracking structure)
    // remain FULLPORT-DEFERRED -- a different, still-unexamined subsystem
    // outside this class's own scope, not part of the ObjectHashMap cluster.
    // ---------------------------------------------------------------------
} // namespace -- IVP_ListManager/IVP_ListManager_CorePairEntry are declared
  // in ivp_mindist.h at file scope (not inside this TU's anonymous
  // namespace), so their out-of-line member definitions below must also
  // live outside it (C2888 otherwise). Reopened again below, after
  // MergeFrom, to resume the FULLPORT-DEFERRED-stub anonymous namespace.

// FUN_0634b600 (item 2992), .text:0634b600-0634b680 (RET 4).
//   0634b617 / 0634b61e / 0634b667: the three vtables (static handle 063a5500
//     at +0x08, energy handle 063a54e4 at +0x10, the system's own 063a54c8 at
//     +0x00) -- the base and handle constructions below.
//   0634b670 / 0634b673: the handles' back pointers (+0x0c, +0x14) = this.
//   0634b627-0634b649: the three vectors (+0x24/+0x2c/+0x34) zeroed --
//     IVP_U_Vector's default constructor.
//   0634b64c: +0x04 = the argument.  0634b652: +0x20 = 0.
//   0634b655 / 0634b659: +0x3c / +0x3e = 0.
//   0634b64f-0634b66d: `AND [+0x44],0xffffff00` -- only the low byte is
//     cleared in retail; this port zeroes the whole dword (the upper three
//     bytes are never read: every reader tests or rewrites the low byte only,
//     .text:06356615-06356621, 063554d0-063554db).
//   +0x18..+0x1f, +0x40 and +0x48 are not written.
IVP_ListManager::IVP_ListManager(IVP_Environment* EnvironmentIn)
    : Environment(EnvironmentIn)
    , StaticFsHandle(this)
    , EnergyFsHandle(this)
{
    CtxListHead = nullptr;
    RegisterCoreCallCount = 0;
    CtxListCount = 0;
    union_find_necessary = 0; // 0634b64f-0634b66d: low byte only
}

    // FUN_0634b9a0 (item 3004), Common.dll.txt:0634b9a0-0634b9c1.
    void IVP_ListManager::InsertCtx(IVP_Contact_Point* Node)
    {
        Node->ListManagerOwner = this;
        IVP_Contact_Point* OldHead = CtxListHead;
        Node->ListManagerListPrev = nullptr;
        Node->ListManagerListNext = OldHead;
        CtxListHead = Node;
        if (OldHead != nullptr)
            OldHead->ListManagerListPrev = Node;
        ++CtxListCount;
    }

    // FUN_0634b8d0 (item 3002), Common.dll.txt:0634b8d0-0634b8f4.
    void IVP_ListManager::RemoveCtx(IVP_Contact_Point* Node)
    {
        IVP_Contact_Point* Prev = Node->ListManagerListPrev;
        IVP_Contact_Point* Next = Node->ListManagerListNext;
        if (Next != nullptr)
            Next->ListManagerListPrev = Prev;
        --CtxListCount;
        if (Prev != nullptr)
            Prev->ListManagerListNext = Next;
        else
            CtxListHead = Next;
    }

    // FUN_06355770 (item 3226), Common.dll.txt:06355770-063557ae. Unordered
    // (CoreA,CoreB) match: a candidate entry hits if its own {CoreA,CoreB}
    // equals the search pair in either order.
    IVP_ListManager_CorePairEntry* IVP_ListManager::FindCorePairEntry(IVP_Core* CoreA, IVP_Core* CoreB)
    {
        for (int I = CorePairEntries.Len() - 1; I >= 0; --I)
        {
            IVP_ListManager_CorePairEntry* Entry = CorePairEntries[I];
            const bool HitsA = (Entry->CoreA == CoreA || Entry->CoreB == CoreA);
            const bool HitsB = (Entry->CoreA == CoreB || Entry->CoreB == CoreB);
            if (HitsA && HitsB)
                return Entry;
        }
        return nullptr;
    }

    // FUN_06356280, .text:06356280-063562be: count is the unsigned word at
    // +0x36 (`XOR EDX,EDX; MOV DX,[ECX+0x36]`), walked from the last element down.
    IVP_ListManager_CorePairEntry* IVP_ListManager::FindCorePairEntryExact(IVP_Core* KeyA, IVP_Core* KeyB)
    {
        for (int I = CorePairEntries.Len() - 1; I >= 0; --I)
        {
            IVP_ListManager_CorePairEntry* Entry = CorePairEntries[I];
            if ((Entry->CoreA == KeyA && Entry->CoreB == KeyB) || (Entry->CoreA == KeyB && Entry->CoreB == KeyA))
                return Entry;
        }
        return nullptr;
    }

    // FUN_0634b900 (item 3003), Common.dll.txt:0634b900-0634b998. Retail leaves
    // 063556b0's EAX (= Ctx) as the result; no caller (063555e5, 06356d33,
    // 06356e84) reads it.
    void IVP_ListManager::FindOrAttachCorePairEntry(IVP_Contact_Point* Ctx)
    {
        // Common.dll.txt:0634b916-0634b938: Ctx->0x24/->0x98 and Ctx->0x10/
        // ->0x98 (Object2->core / Object1->core, already-declared fields).
        IVP_Core* CoreA = Ctx->Object1->core;
        IVP_Core* CoreB = Ctx->Object2->core;

        IVP_ListManager_CorePairEntry* Entry = FindCorePairEntry(CoreA, CoreB);
        if (Entry == nullptr)
        {
            // Common.dll.txt:0634b941-0634b975: operator_new(0x30) +
            // FUN_06356240/item 3241, then registered into CorePairEntries
            // via FUN_063556e0/item 3224 (the generic IVP_U_Vector<T>::Add
            // instantiation this port's own Add() already reproduces).
            Entry = new IVP_ListManager_CorePairEntry();
            Entry->CoreA = CoreA;
            Entry->CoreB = CoreB;
            CorePairEntries.Add(Entry);
        }

        // Common.dll.txt:0634b97a-0634b986: FUN_063556b0/item 3223, same
        // generic Add() at a different embedding offset.
        Entry->AttachedCtx.Add(Ctx);
    }

    // FUN_0634b9d0 (item 3005), .text:0634b9d0-0634ba4b (RET 4).
    //   0634b9d4-0634b9f9: RegisteredCores.Add(Core) (FUN_063441a0 grows).
    //   0634b9fd: `TEST byte ptr [EBX],0xc; JNZ` -> kinematic cores stop here.
    //   0634ba02-0634ba26: RegisteredDynamicCores.Add(Core).
    //   0634ba1f-0634ba3f: Core->register_controller (FUN_06349180) with
    //     &StaticFsHandle (ESI+8), this, &EnergyFsHandle (ESI+0x10).
    //   0634ba44: ++RegisterCoreCallCount on both paths.
    void IVP_ListManager::RegisterCore(IVP_Core* Core)
    {
        RegisteredCores.Add(Core);

        if ((Core->Flags & 0xc) == 0)
        {
            RegisteredDynamicCores.Add(Core);
            Core->register_controller(&StaticFsHandle);
            Core->register_controller(this);
            Core->register_controller(&EnergyFsHandle);
        }

        ++RegisterCoreCallCount;
    }

    // FUN_0634ba50, .text:0634ba50-0634bb1a (RET 4).
    //   0634ba56: `TEST byte ptr [EDI],0xc; JNZ` -> kinematic cores skip the
    //     dynamic half.
    //   0634ba5d-0634baa3: RegisteredDynamicCores: backward search, then the
    //     count is decremented and the tail shifted down -- inline, so when
    //     Core is absent retail still decrements and shifts from index -1
    //     (see IVP_U_Vector::RemoveElement's open divergence note).
    //   0634baa5-0634babe: Core->detach_controller (FUN_06349110) with
    //     &EnergyFsHandle, this, &StaticFsHandle -- the reverse of RegisterCore.
    //   0634bac3-0634bb0b: RegisteredCores, same inline removal.
    //   then --RegisterCoreCallCount.
    void IVP_ListManager::UnregisterCore(IVP_Core* Core)
    {
        if ((Core->Flags & 0xc) == 0)
        {
            RegisteredDynamicCores.RemoveElement(Core);
            Core->detach_controller(&EnergyFsHandle);
            Core->detach_controller(this);
            Core->detach_controller(&StaticFsHandle);
        }

        RegisteredCores.RemoveElement(Core);
        --RegisterCoreCallCount;
    }

    // FUN_0634b850 (item 3001), Common.dll.txt:419016-419067. MergeFrom's
    // Phase 1 per-node teardown -- traced fully this pass (D-target063555b0-
    // followup, 2026-09-10). Locates the (CoreA,CoreB) entry Node was
    // attached under on Other (FindCorePairEntry is real, see above; retail
    // traps via a null-pointer write if this ever misses, Common.dll.txt:
    // 0634b878-0634b87a -- the same "should never happen" convention used
    // elsewhere in this cluster, e.g. item 3301's caseD_4 -- not reproduced,
    // this port trusts the invariant that Node's own entry was already
    // attached by FindOrAttachCorePairEntry before Node could ever reach
    // here), removes Node from that entry's AttachedCtx (FUN_06355310, a
    // confirmed generic IVP_U_Vector<T>::RemoveElement() instantiation --
    // disasm-identical in shape to the existing RemoveElement/RemoveAt pair
    // in ivp_u_vector.h at every traced call site where the element is
    // guaranteed present, decomp-tracker item 3217 marked `skipped`), then
    // -- only if that was the LAST attached Ctx (FUN_06355370/item 3218's own
    // AttachedCtx.n_elems getter, now read directly instead) -- removes and
    // destroys the now-empty entry itself (Other->CorePairEntries.
    // RemoveElement, FUN_06355710, same generic template instantiated at a
    // different embedding offset, item 3225 marked `skipped`; then Clear()+
    // free() via FUN_063449d0/FUN_06391d5a, matching
    // IVP_ListManager_CorePairEntry's own already-declared AttachedCtx
    // teardown). Returns true if the entry was destroyed -- MergeFrom's own
    // call site (Common.dll.txt:063555d5) discards this value, kept for
    // signature fidelity only.
    //
    // 2026-10-08 (claude-code:cmn-phy2): hoisted from a free helper into the
    // class (FUN_06355490 = RemoveDistFromSystem calls it too and tests the
    // result), re-traced against .text:0634b850-0634b8be:
    //   - the lookup passes (Object1->core, Object2->core) (0634b855-0634b86f);
    //   - a miss is retail's assert trap `MOV [0],EAX` (0634b87a) -- now
    //     reproduced (cheap literal UB, owner's ruling) instead of skipped;
    //   - the empty entry is cleared (FUN_063449d0) and released with operator
    //     delete (FUN_06391d5a) -- it was allocated with `new` in
    //     FindOrAttachCorePairEntry, so the port's old `free(Entry)` was an
    //     allocator mismatch; now `delete`.
    int IVP_ListManager::RetirePairRecordBookkeeping(IVP_Contact_Point* Dist)
    {
        IVP_Core* CoreA = Dist->Object1->core;
        IVP_Core* CoreB = Dist->Object2->core;
        IVP_ListManager_CorePairEntry* Entry = FindCorePairEntry(CoreA, CoreB);
        if (Entry == nullptr)
        {
            volatile int* Trap = nullptr; // 0634b87a: MOV [DAT_00000000],EAX
            *Trap = 0;
        }

        Entry->AttachedCtx.RemoveElement(Dist);   // FUN_06355310
        if (Entry->AttachedCtx.n_elems != 0)      // FUN_06355370
            return 0;

        CorePairEntries.RemoveElement(Entry);     // FUN_06355710
        if (Entry != nullptr)
        {
            Entry->AttachedCtx.Clear();           // FUN_063449d0
            delete Entry;                         // FUN_06391d5a
        }
        return 1;
    }

    // The per-core record this phase moves is IVP_Friction_Info_For_Core
    // (ivp_mindist.h): FrDists at +0x00 (FUN_063556b0 Add / FUN_06355310
    // RemoveElement operate on it), the owning system at +0x08
    // (`MOV [EBP+8],EBX`, .text:06355676). The file-local
    // IVP_Core_ObjectHashEntry that stood here was a second declaration of the
    // same 0xc-byte record; merged 2026-10-08 (claude-code:cmn-phy3).

    // FUN_063555b0's own Phase 2 (Common.dll.txt:063555f0-0635568e) -- traced
    // fully this pass (D-target063555b0-followup, 2026-09-10; supersedes the
    // D-listmanager pass's own "FULLPORT-DEFERRED" characterization). For
    // every Core in Other->RegisteredCores (walked backward, matching
    // retail's own index-from-count-1 loop): looks up Core's own hash entry
    // under BOTH Other and Target (IVP_Core::find_object_hash_entry x2,
    // Common.dll.txt:06355610/0635561a -- a 3rd, redundant re-lookup of the
    // Other-keyed entry at :0635562a on the merge path is not reproduced,
    // its result is provably identical to the first lookup since
    // find_object_hash_entry never mutates the map).
    //   - If Core already has its own entry under Target (TargetEntry !=
    //     null, i.e. Core is independently registered to both ListManagers
    //     already): transplants every item from OtherEntry into TargetEntry
    //     one at a time, backward (FUN_063556b0/Add then FUN_06355310/
    //     RemoveElement per item, Common.dll.txt:06355633-0635565b), then
    //     destroys the now-empty OtherEntry (release_object_hash_entry).
    //     Core's own RegisteredCores/RegisteredDynamicCores membership on
    //     both ListManagers is left untouched on this path (retail does NOT
    //     call UnregisterCore/RegisterCore here, Common.dll.txt:06355669
    //     `JMP LAB_0635568e` skips straight past that pair).
    //   - Else (no existing Target entry): re-keys OtherEntry in place
    //     instead of allocating a new one -- unregisters it under the old
    //     Other key, overwrites its own Key field to Target, re-registers it
    //     -- then moves Core itself from Other's RegisteredCores/
    //     RegisteredDynamicCores to Target's via UnregisterCore/RegisterCore.
    void IVP_MergeRegisteredCores(IVP_ListManager* Target, IVP_ListManager* Other)
    {
        for (int I = static_cast<int>(Other->RegisteredCores.n_elems) - 1; I >= 0; --I)
        {
            IVP_Core* Core = Other->RegisteredCores[I];

            IVP_Friction_Info_For_Core* OtherEntry = static_cast<IVP_Friction_Info_For_Core*>(
                Core->find_object_hash_entry(static_cast<unsigned>(reinterpret_cast<uintptr_t>(Other))));
            IVP_Friction_Info_For_Core* TargetEntry = static_cast<IVP_Friction_Info_For_Core*>(
                Core->find_object_hash_entry(static_cast<unsigned>(reinterpret_cast<uintptr_t>(Target))));

            if (TargetEntry != nullptr)
            {
                for (int J = static_cast<int>(OtherEntry->FrDists.n_elems) - 1; J >= 0; --J)
                {
                    IVP_Contact_Point* Elem = OtherEntry->FrDists[J];
                    TargetEntry->FrDists.Add(Elem);
                    OtherEntry->FrDists.RemoveElement(Elem);
                }
                Core->release_object_hash_entry(OtherEntry);
            }
            else
            {
                Core->unregister_object_hash_entry(OtherEntry);
                OtherEntry->FrictionSystem = Target;
                Core->register_object_hash_entry(OtherEntry);

                Other->UnregisterCore(Core);
                Target->RegisterCore(Core);
            }
        }
    }

    // FUN_063555b0 (item 3222), .text:063555b0-063556ac (RET 4).
    void IVP_ListManager::MergeFrom(IVP_ListManager* Other)
    {
        // Phase 1 (Common.dll.txt:063555bb-063555ee): walk Other's Ctx list,
        // moving every node onto `this`.
        IVP_Contact_Point* Node = Other->CtxListHead;
        while (Node != nullptr)
        {
            IVP_Contact_Point* NextNode = Node->ListManagerListNext;
            Other->RemoveCtx(Node);
            Other->RetirePairRecordBookkeeping(Node);
            this->InsertCtx(Node);
            this->FindOrAttachCorePairEntry(Node);
            Node = NextNode;
        }

        // Phase 2 -- now real, see IVP_MergeRegisteredCores above.
        IVP_MergeRegisteredCores(this, Other);

        // .text:06355698-063556a2: `TEST EDI,EDI; JZ; MOV EDX,[EDI]; PUSH 1;
        // CALL [EDX+0x18]` -- the deleting destructor (slot 6), i.e. `delete`.
        delete Other;
    }

namespace
{
    // FUN_06356bc0 (item 3262) is ported as IVP_Try_To_Generate_Managed_Friction
    // (ivp_contact_point.cpp, cmn-ivp3d 2026-10-08); the lookalike that used
    // to stand here (a fresh IVP_ListManager per call) is deleted.


    // FUN_06358ee0 (item 3283) is ported in ivp_physics/ivp_impact.cpp. Its
    // two small callees FUN_06358bb0 (item 3278) and FUN_06358c50 (item 3280)
    // follow; both take an IVP_Contact_Point*.

    // FUN_06356730 (decomp-tracker item 3249, already `simplified`): masks a
    // tagged pointer down to its 16-byte-aligned base and extracts a 7-bit
    // tag from bits 24-30 of the dword it points to. This pass (item 3278)
    // confirms it is always called on IVP_Contact_Point::Feature1/
    // Feature2 (the masked, tagged IVP_Compact_Edge* fields) in this
    // cluster -- inlined directly here rather than adding a second wrapper
    // function for a 2-instruction body.
    inline uint32_t IVP_ReadFeatureTag7(void* TaggedFeature)
    {
        uint32_t* Base = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(TaggedFeature) & ~static_cast<uintptr_t>(0xF));
        return (*Base >> 0x18) & 0x7F;
    }

    // FUN_06358bb0 (decomp-tracker item 3278, .text:06358bb0-06358bfb). thiscall
    // (this = Candidate), one stack argument OutArray[2] (RET 4). For each of
    // the record's two sides (Object1/Feature1 at +0x10/+0x18, Object2/Feature2
    // at +0x24/+0x2c, a 0x14 stride):
    //   Tag = FUN_06356730(side) = (*(Feature & ~0xf) >> 24) & 0x7f;
    //   Tag == 0: OutArray[side] = Object[side]->material (+0x8c);
    //   else:     OutArray[side] = Object1->environment->l_material_manager
    //               ->get_material_by_index(nullptr, Tag)   (`PUSH EAX; PUSH 0;
    //               CALL [EDX]`, .text:06358bda-06358bec).
    // The `else` branch always goes through Object1's environment: EBX is set
    // to Candidate+0x10 once before the loop and never advances.
    // Callers: 06353b50, 063562d0, 06358c50, 06359390 -- external linkage
    // (declared in ivp_listmanager_topology.h) so ClampTangentDirection in
    // ivp_contact_point.cpp reaches the same body.
} // namespace

void IVP_ResolvePairRecordSideMaterialSlots(IVP_Contact_Point* Candidate, IVP_Material** OutArray)
    {
        for (int Side = 0; Side < 2; ++Side)
        {
            IVP_Real_Object* SideObject = (Side == 0) ? Candidate->Object1 : Candidate->Object2;
            void* SideFeature = (Side == 0) ? Candidate->Feature1 : Candidate->Feature2;

            const uint32_t Tag = IVP_ReadFeatureTag7(SideFeature);
            if (Tag == 0)
            {
                OutArray[Side] = SideObject->material;
            }
            else
            {
                OutArray[Side] = Candidate->Object1->environment->l_material_manager
                    ->get_material_by_index(nullptr, static_cast<int>(Tag));
            }
        }
    }

    // FUN_06358c50 (decomp-tracker item 3280, .text:06358c50-06358c9e).
    // thiscall (this = Candidate), one stack argument Owner (RET 4).
    // Object1 is loaded into EBX before the FUN_06358bb0 call and its
    // environment read after it (.text:06358c5a, 06358c69); the manager pointer
    // is read once (.text:06358c7e) and used for both dispatches.
    // External linkage (declared in ivp_listmanager_topology.h) since
    // 2026-10-08 (cmn-ivp3d): IVP_Core 063465a0 / 063464a0 call it too.
    void IVP_PopulatePairRecordIntoOwner(IVP_Contact_Point* Candidate, IVP_Impact_Solver_Long_Term* Owner)
    {
        IVP_Real_Object* Object1 = Candidate->Object1;
        IVP_Real_Object* Object2 = Candidate->Object2;

        // .text:06358c60-06358c64.
        IVP_ResolvePairRecordSideMaterialSlots(Candidate, Owner->Materials);

        // .text:06358c69-06358c7b.
        Owner->Objects[0] = Object1;
        Owner->Objects[1] = Object2;
        Owner->Feature0 = Candidate->Feature1;
        Owner->Feature1 = Candidate->Feature2;

        // .text:06358c7e-06358c97: slot 2 (elasticity) into Owner+0x60, then
        // slot 1 (friction) into Candidate+0x44, each stored straight off the
        // FPU as a float.
        IVP_Material_Manager* Manager = Object1->environment->l_material_manager;
        Owner->ContactElasticity = Manager->get_elasticity(Owner);
        Candidate->FrictionCoefficient = Manager->get_friction_factor(Owner);
    }

namespace
{

    // FUN_0635a1e0, .text:0635a1e9-0635a229: Object1->environment+0xa4 owns
    // the temporary contact frame (FUN_06347380, 0xd8 bytes). The callers
    // reset this arena after the impact/recalculation pass; a standalone new
    // would escape that reset every time Candidate->Owner is overwritten.
    // Preserve the reused block: only Status+0x52 = 0 and the dword at +0x54
    // &= 0xfffffc00, then publish Owner and increment environment+0x80.
    inline IVP_Impact_Solver_Long_Term* IVP_AllocatePairRecordOwner(IVP_Contact_Point* Candidate)
    {
        static_assert(std::is_trivially_default_constructible<IVP_Impact_Solver_Long_Term>::value &&
                      std::is_trivially_destructible<IVP_Impact_Solver_Long_Term>::value,
                      "Contact frames must not construct or retain owned resources in the scratch arena");
        // The modern alignas(16) object has tail padding; both sizeof(T) and
        // retail's 0xd8 request consume the same 0xe0-byte arena slot.
        static_assert(((sizeof(IVP_Impact_Solver_Long_Term) + 0x1fu) & ~size_t(0x1f)) == 0xe0u,
                      "Contact frame must retain the listing's rounded arena stride");
        IVP_Environment* const Environment = Candidate->Object1->environment;
        void* const Storage = Environment->vector_cores_frozen->GetMem(
            static_cast<int>(sizeof(IVP_Impact_Solver_Long_Term)));
        IVP_Impact_Solver_Long_Term* const Owner = ::new (Storage) IVP_Impact_Solver_Long_Term;
        uint32_t PackedState;
        std::memcpy(&PackedState, &Owner->StateProcessed, sizeof(PackedState));
        Owner->Status = 0;
        PackedState &= 0xfffffc00u;
        std::memcpy(&Owner->StateProcessed, &PackedState, sizeof(PackedState));
        Candidate->Owner = Owner;
        ++Environment->counter_80;
        return Owner;
    }

    // Offsets this cluster's three contact-frame builders reach through raw
    // casts or read as a wider type than the header declares. Pinned here
    // rather than in ivp_mindist.h because this file is the only consumer.
    static_assert(offsetof(IVP_Contact_Point, _field4c) == 0x4C, "IVP_Contact_Point+0x4C");
    static_assert(offsetof(IVP_Contact_Point, GapDistance) == 0x58, "IVP_Contact_Point+0x58");
    static_assert(offsetof(IVP_Contact_Point, _flags60) == 0x60, "IVP_Contact_Point+0x60");
    static_assert(offsetof(IVP_Impact_Solver_Long_Term, ContactNormalWS) == 0x00, "IVP_Impact_Solver_Long_Term+0x00");
    static_assert(offsetof(IVP_Impact_Solver_Long_Term, StateProcessed) == 0x54, "IVP_Impact_Solver_Long_Term+0x54");

    // Common.dll.txt:063598cc-063598d9 and :06359ceb-06359cfa, byte-identical
    // in both leaves: `EAX = [Owner+0x54]; AND EAX,0xfffffdff; OR EAX,0x100;
    // MOV [Owner+0x54],EAX`. A FULL DWORD read/modify/write at the offset
    // ivp_mindist.h declares as the single byte `StateProcessed` (06358ee0 in
    // ivp_impact.cpp reads the same packed dword), reached the same way (a raw
    // offset cast) rather than widening a shared struct's field for one caller.
    // Meaning of the bit pair: bit 0x200 cleared, bit 0x100 set, on the branch
    // where the closest point falls OUTSIDE the feature it was matched
    // against -- outside the triangle in the poly case below, outside the
    // segment's [0,1] parameter range in the edge case.
    inline void IVP_FlagOwnerContactOutsideFeature(IVP_Impact_Solver_Long_Term* Owner)
    {
        uint32_t* const State = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(Owner) + 0x54);
        *State = (*State & 0xFFFFFDFFu) | 0x00000100u;
    }

    // The owner's ContactNormalWS (+0x00) and TangentAxis0 (+0x78) are 16-byte
    // slots that retail hands to IVP_U_Float_Point methods (FUN_06347ef0 with
    // ECX = owner / owner+0x78); the methods touch only k[0..2].
    inline IVP_U_Point* IVP_ContactFrameSlotAsPoint(Vector* Slot)
    {
        return reinterpret_cast<IVP_U_Point*>(Slot);
    }

    uint32_t IVP_ContactFrameFloatBits(float Value)
    {
        uint32_t Bits;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    // ---------------------------------------------------------------------------
    // FUN_063599f0 (.text:063599f0-06359ad4, thiscall, RET 0x10): the point vs
    // point leaf of 0635a1e0 (second switch cases 0 and 3). this = the pair
    // record; arguments (PointA, PointB, Owner, Situation), the last never read.
    //   :063599f3-:06359a1b Delta = float(B - A) per component.
    //   :06359a1f-:06359a24 FUN_06347e90(&Delta) -> record+0x58 (FSTP float).
    //   :06359a27-:06359a46 ContactNormalWS = Delta.
    //   :06359a49-:06359a76 `FCOMP 0.9f` on x*x, `TEST AH,0x1` + `JZ`: x*x >= 0.9f
    //     -> R = 0.0f, Q = 1.0f; less (or unordered) -> R = 1.0f, Q = 0.0f.
    //   :06359a78-:06359ab0 TangentAxis0 = (float(y*Q), float(z*R - x*Q),
    //     float(0.0f - float(y*R))), then FUN_06347ef0 on it (:06359ab3).
    //   :06359ab8-:06359ac9 SolverContactPointWS = *PointA.
    // ---------------------------------------------------------------------------
    inline void IVP_BuildPointPointContactFrame(IVP_Contact_Point* Candidate, const IVP_U_Point* PointA,
                                                const IVP_U_Point* PointB, IVP_Impact_Solver_Long_Term* Owner, Vector* /*Situation*/)
    {
        IVP_U_Point Delta;
        Delta.x = (float)((double)(PointB->x) - PointA->x);
        Delta.y = (float)((double)(PointB->y) - PointA->y);
        Delta.z = (float)((double)(PointB->z) - PointA->z);
        Candidate->GapDistance = (float)(Delta.real_length_plus_normize()); // :06359a1f FUN_06347e90

        Owner->ContactNormalWS.x = Delta.x;
        Owner->ContactNormalWS.y = Delta.y;
        Owner->ContactNormalWS.z = Delta.z;

        double ReferenceR;
        float ReferenceQ;
        if ((double)(Delta.x) * Delta.x >= (double)(0.9f)) // DAT_063a53fc
        {
            ReferenceR = 0.0; // DAT_063a53d4
            ReferenceQ = 1.0f;
        }
        else
        {
            ReferenceR = 1.0; // DAT_063a53d0
            ReferenceQ = 0.0f;
        }

        const float YTimesR = (float)((double)(Delta.y) * ReferenceR); // :06359a89 FSTP param_3
        Owner->TangentAxis0.x = (float)((double)(Delta.y) * ReferenceQ);
        Owner->TangentAxis0.y = (float)((double)(Delta.z) * ReferenceR - (double)(Delta.x) * ReferenceQ);
        Owner->TangentAxis0.z = (float)(0.0 - (double)(YTimesR));
        IVP_ContactFrameSlotAsPoint(&Owner->TangentAxis0)->normize(); // :06359ab3 FUN_06347ef0

        Owner->SolverContactPointWS.x = PointA->x;
        Owner->SolverContactPointWS.y = PointA->y;
        Owner->SolverContactPointWS.z = PointA->z;
    }

    // ---------------------------------------------------------------------------
    // FUN_06359850 (.text:06359850-063599ed, thiscall, RET 0x14): the point vs
    // triangle leaf (second switch case 2). this = the pair record (EBP);
    // arguments (Point, F, CacheF, Owner, Situation), the last never read.
    //   :06359857-:0635987e SolverContactPointWS = *Point.
    //   :06359881-:06359885 FUN_0634d860 on CacheF->cache_object: Point into
    //     F's object space (local_30).
    //   :0635988a-:063598d9 record+0x60 low byte == 1: clear the low byte;
    //     FUN_0635b210(CacheF->compact_ledge, F, &local, &qr); sign bit of
    //     (qr1 | qr2 | qr0) -> Owner+0x54 = (x & ~0x200) | 0x100.
    //   :063598dc-:06359924 FUN_0635b940(F, ledge, &n); n = float(n * record+0x4c)
    //     per component.
    //   :06359926 FUN_0634d9d0 on the cache object: n into world (local_20).
    //   :0635992b-:06359988 record+0x58 = float(((n.z*l.z + n.y*l.y) + l.x*n.x)
    //     - ((n.z*V0.z + n.y*V0.y) + n.x*V0.x)), V0 = start point of F.
    //   :0635998b-:063599ae ContactNormalWS = float(world n * -1.0f) (DAT_063a5498).
    //   :063599b1-:063599e1 e = float(V1 - V0), V1 = start point of the next
    //     edge; FUN_0634d9d0(e) -> TangentAxis0.
    // ---------------------------------------------------------------------------
    inline void IVP_BuildPointPolyContactFrame(IVP_Contact_Point* Candidate, const IVP_U_Point* Point,
                                               const IVP_Compact_Edge* F, const IVP_Cache_Ledge_Point* CacheF,
                                               IVP_Impact_Solver_Long_Term* Owner, Vector* /*Situation*/)
    {
        Owner->SolverContactPointWS.x = Point->x;
        Owner->SolverContactPointWS.y = Point->y;
        Owner->SolverContactPointWS.z = Point->z;

        IVP_U_Point LocalPoint;
        IVP_TransformWorldToSlotSpace(CacheF->cache_object, Point, &LocalPoint); // :06359885 FUN_0634d860

        if (static_cast<uint8_t>(Candidate->_flags60) == 1)
        {
            Candidate->_flags60 &= 0xFFFFFF00u;
            IVP_Unscaled_QR_Result Qr;
            IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheF->compact_ledge, F, &LocalPoint, &Qr); // :063598ac
            const uint32_t SignBits = (IVP_ContactFrameFloatBits(Qr.Checks[1]) | IVP_ContactFrameFloatBits(Qr.Checks[2])) |
                                      IVP_ContactFrameFloatBits(Qr.Checks[0]);
            if ((SignBits & 0x80000000u) != 0u)
                IVP_FlagOwnerContactOutsideFeature(Owner);
        }

        IVP_U_Point Normal;
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(F, CacheF->compact_ledge, &Normal); // :063598e6
        const double InvNormalLength = Candidate->_field4c;
        Normal.y = (float)((double)(Normal.y) * InvNormalLength);
        Normal.z = (float)((double)(Normal.z) * InvNormalLength);
        Normal.x = (float)((double)(Normal.x) * InvNormalLength);

        IVP_U_Point WorldNormal;
        IVP_RotateSlotToWorldSpace(CacheF->cache_object, &Normal, &WorldNormal); // :06359926 FUN_0634d9d0

        const IVP_U_Float_Point* const V0 = CacheF->compact_poly_points + F->GetStartPointIndex();
        const IVP_U_Float_Point* const V1 = CacheF->compact_poly_points + F->GetNextEdge()->GetStartPointIndex();
        const double PointDot = ((double)(Normal.z) * LocalPoint.z + (double)(Normal.y) * LocalPoint.y) +
                                (double)(LocalPoint.x) * Normal.x;
        const double VertexDot = ((double)(Normal.z) * V0->Z + (double)(Normal.y) * V0->Y) +
                                 (double)(Normal.x) * V0->X;
        Candidate->GapDistance = (float)(PointDot - VertexDot);

        Owner->ContactNormalWS.x = (float)((double)(WorldNormal.x) * -1.0f);
        Owner->ContactNormalWS.y = (float)((double)(WorldNormal.y) * -1.0f);
        Owner->ContactNormalWS.z = (float)((double)(WorldNormal.z) * -1.0f);

        IVP_U_Point EdgeVec;
        EdgeVec.x = (float)((double)(V1->X) - V0->X);
        EdgeVec.y = (float)((double)(V1->Y) - V0->Y);
        EdgeVec.z = (float)((double)(V1->Z) - V0->Z);
        IVP_U_Point Tangent;
        IVP_RotateSlotToWorldSpace(CacheF->cache_object, &EdgeVec, &Tangent); // :063599e1, out = Owner+0x78
        Owner->TangentAxis0.x = Tangent.x;
        Owner->TangentAxis0.y = Tangent.y;
        Owner->TangentAxis0.z = Tangent.z;
    }

    // ---------------------------------------------------------------------------
    // FUN_06359ae0 (.text:06359ae0-06359d03, thiscall, RET 0x14): the point vs
    // edge leaf (second switch case 1). this = the pair record (EBX);
    // arguments (Point, K, CacheK, Owner, Situation), the last never read.
    //   :06359aee-:06359b10 FUN_0635bda0(K) -> V0, FUN_0635bda0(next(K)) -> V1 (world).
    //   :06359b15-:06359b5c e = float(V1 - V0), t = float(Point - V0).
    //   :06359b60-:06359b84 FUN_06347df0(float((e.z^2 + e.y^2) + e.x^2)) -> inv (float).
    //   :06359b88-:06359bcd p = (float(t.z*e.y - e.z*t.y), float(e.z*t.x - t.z*e.x),
    //     float(t.y*e.x - e.y*t.x)).
    //   :06359bd1-:06359bda d = FUN_06347e60(&p) * inv (kept), record+0x58 = float(d).
    //   :06359bdd-:06359bec `FCOMP 1e-10f` on d*d, `TEST AH,0x41` + `JNZ`: less, equal
    //     or unordered -> p = (1, 0, 0); else s = inv / d and p = float(p * s).
    //   :06359c2e-:06359c72 ContactNormalWS = (float(p.z*e.y - p.y*e.z),
    //     float(e.z*p.x - p.z*e.x), float(p.y*e.x - e.y*p.x)); FUN_06347ef0 on it.
    //   :06359c77-:06359ca0 SolverContactPointWS = *Point, TangentAxis0 = p.
    //   :06359ca6-:06359cfa record+0x60 low byte == 1: clear it; a =
    //     FUN_0628be40(e, t) * (inv*inv); `FCOM 0.0f` / `TEST AH,0x1` / `JNZ`
    //     (less or unordered) -> flag; else `FCOMP 1.0f` / `TEST AH,0x41` / `JNZ`
    //     (less or equal) -> return; else flag.
    // ---------------------------------------------------------------------------
    inline void IVP_BuildPointEdgeContactFrame(IVP_Contact_Point* Candidate, const IVP_U_Point* Point,
                                               const IVP_Compact_Edge* K, const IVP_Cache_Ledge_Point* CacheK,
                                               IVP_Impact_Solver_Long_Term* Owner, Vector* /*Situation*/)
    {
        IVP_U_Point V0;
        IVP_U_Point V1;
        IVP_Compact_Ledge_Solver::give_world_coords_AT(K, CacheK, &V0);
        IVP_Compact_Ledge_Solver::give_world_coords_AT(K->GetNextEdge(), CacheK, &V1);

        Vector EdgeDir;
        EdgeDir.x = (float)((double)(V1.x) - V0.x);
        EdgeDir.y = (float)((double)(V1.y) - V0.y);
        EdgeDir.z = (float)((double)(V1.z) - V0.z);
        Vector ToPoint;
        ToPoint.x = (float)((double)(Point->x) - V0.x);
        ToPoint.y = (float)((double)(Point->y) - V0.y);
        ToPoint.z = (float)((double)(Point->z) - V0.z);

        const float EdgeQuadLength = (float)(
            ((double)(EdgeDir.z) * EdgeDir.z + (double)(EdgeDir.y) * EdgeDir.y) +
            (double)(EdgeDir.x) * EdgeDir.x);
        const float InvEdgeLength = (float)(1.0 / std::sqrt((double)(EdgeQuadLength))); // FUN_06347df0

        IVP_U_Point Perp;
        Perp.x = (float)((double)(ToPoint.z) * EdgeDir.y - (double)(EdgeDir.z) * ToPoint.y);
        Perp.y = (float)((double)(EdgeDir.z) * ToPoint.x - (double)(ToPoint.z) * EdgeDir.x);
        Perp.z = (float)((double)(ToPoint.y) * EdgeDir.x - (double)(EdgeDir.y) * ToPoint.x);

        const double Distance = Perp.real_length() * InvEdgeLength; // :06359bd1 FUN_06347e60
        Candidate->GapDistance = (float)(Distance);
        if (Distance * Distance > (double)(1.0e-10f))
        {
            const double Scale = InvEdgeLength / Distance;
            Perp.z = (float)((double)(Perp.z) * Scale);
            Perp.x = (float)((double)(Perp.x) * Scale);
            Perp.y = (float)((double)(Perp.y) * Scale);
        }
        else
        {
            Perp.x = 1.0f;
            Perp.y = 0.0f;
            Perp.z = 0.0f;
        }

        Owner->ContactNormalWS.x = (float)((double)(Perp.z) * EdgeDir.y - (double)(Perp.y) * EdgeDir.z);
        Owner->ContactNormalWS.y = (float)((double)(EdgeDir.z) * Perp.x - (double)(Perp.z) * EdgeDir.x);
        Owner->ContactNormalWS.z = (float)((double)(Perp.y) * EdgeDir.x - (double)(EdgeDir.y) * Perp.x);
        IVP_ContactFrameSlotAsPoint(&Owner->ContactNormalWS)->normize(); // :06359c72 FUN_06347ef0

        Owner->SolverContactPointWS.x = Point->x;
        Owner->SolverContactPointWS.y = Point->y;
        Owner->SolverContactPointWS.z = Point->z;
        Owner->TangentAxis0.x = Perp.x;
        Owner->TangentAxis0.y = Perp.y;
        Owner->TangentAxis0.z = Perp.z;

        if (static_cast<uint8_t>(Candidate->_flags60) == 1)
        {
            Candidate->_flags60 &= 0xFFFFFF00u;
            // FUN_0628be40 (this = &EdgeDir, arg = &ToPoint): (t.z*e.z + t.y*e.y) + t.x*e.x.
            const double Dot = ((double)(ToPoint.z) * EdgeDir.z + (double)(ToPoint.y) * EdgeDir.y) +
                               (double)(ToPoint.x) * EdgeDir.x;
            const double Along = Dot * ((double)(InvEdgeLength) * InvEdgeLength);
            if (!(Along >= 0.0) || Along > 1.0)
                IVP_FlagOwnerContactOutsideFeature(Owner);
        }
    }

    // FUN_06359d10, the edge vs edge leaf (first switch's case 1), is
    // IVP_Contact_Point_Calc_Friction_Edge_Edge in ivp_contact_point.cpp
    // (cmn-ivp3e 2026-10-08).

} // namespace -- IVP_UpdatePairRecordContactFrame has external linkage
  // (declared in ivp_listmanager_topology.h): the friction controllers in
  // ivp_friction_solver.cpp call it too (2026-10-08, claude-code:cmn-phy3).

// ---------------------------------------------------------------------------
// FUN_0635a1e0 (.text:0635a1e0-0635a8ef, thiscall, no arguments): the contact
// frame of a pair record (EDI). Traced from the listing (cmn-ivp2e):
//   :0635a1e9-:0635a229 the 0xd8-byte owner from the environment's short-term
//     memory (FUN_06347380), +0x52 = 0, +0x54 &= 0xfffffc00, record+0x40 =
//     owner, env+0x80 += 1 (IVP_AllocatePairRecordOwner).
//   :0635a22f-:0635a25d tag1 / tag2 = sign-extended low bytes of record+0x16 /
//     +0x2a; :0635a23d-:0635a26b Situation (local_48..local_40) = 0.
//   :0635a261-:0635a301 the two IVP_Cache_Ledge_Points: side 2 (local_28)
//     {points, ledge(edge2), FUN_06353510(object2), object2, record+0x1c}
//     first, then side 1 (local_14) {.., FUN_06353510(object1), object1,
//     record+0x8}.
//   first switch on tag1: 0 -> FUN_0635bda0(edge1, &cache1, &A); 1 ->
//     FUN_06359d10(edge1, edge2, &cache1, &cache2, owner, &Situation) and the
//     tail; 3 -> A = cache1->vv, record+0x60 = (x & 0xffffff01) | 1; else
//     CORE (:0635a413) and the tail.
//   second switch on tag2 (unsigned): 0 -> FUN_0635bda0(edge2, &cache2, &B),
//     FUN_063599f0(&A, &B); 1 -> FUN_06359ae0; 2 -> FUN_06359850; 3 ->
//     FUN_063599f0(&A, &cache2->vv); else CORE.
//   :0635a41d-:0635a432 FUN_0628be60: SolverContactPointWS =
//     float(r1 * n + SolverContactPointWS), r1 = object1+0x94.
//   :0635a437-:0635a45b g = record+0x58 - (r2 + r1); store float; `FCOMP 0.0f`
//     / `TEST AH,0x1` / `JZ`: less or unordered -> record+0x58 = 0.
//   :0635a45e-:0635a46c --cache1->RefCount, --cache2->RefCount.
//   :0635a46f-:0635a4ad FUN_06347ef0(TangentAxis0); TangentAxis1 = n x t0.
//   :0635a4b3-:0635a685 side 1 (object1's core): `TEST byte [core],0xc` set ->
//     zeros, speed 0, core 0; else CoordOS[0] = M^T (P - vv), the normal in
//     object space, CoordOSCrossNormal[0], FUN_063461a0 point speed, +0x6c =
//     float(((cz*Iz*cz + cy*Iy*cy) + cx*Ix*cx) + InvMass). +0x70 = core / 0.
//   :0635a685-:0635a85b side 2, the same with its own operand order; on the
//     dynamic path the relative speed -= its point speed (FUN_0628bea0) and
//     +0x6c = float(term + +0x6c). +0x74 = core / 0.
//   :0635a85b-:0635a8e6 +0x68 = float(1 / +0x6c); dt = ((now0 - rec+0x68) +
//     now1) - rec+0x6c, then the record's time is restamped; rec+0x38 / +0x3c
//     -= (relative speed . TangentAxis0 / 1) * dt.
// The owner belongs to the environment's short-term arena; the caller's
// FUN_06344070 reset releases/reuses its storage after this pass.
// FUN_06359d10 is IVP_Contact_Point_Calc_Friction_Edge_Edge (ivp_contact_point.cpp).
// ---------------------------------------------------------------------------
void IVP_UpdatePairRecordContactFrame(IVP_Contact_Point* Candidate)
{
    IVP_Impact_Solver_Long_Term* const Owner = IVP_AllocatePairRecordOwner(Candidate);

    const int32_t ShapeTag1 = static_cast<int8_t>(static_cast<uint8_t>(Candidate->_hash1 & 0xFFu));
    const int32_t ShapeTag2 = static_cast<int8_t>(static_cast<uint8_t>(Candidate->_hash2 & 0xFFu));
    Vector Situation = { 0.0f, 0.0f, 0.0f };

    IVP_Compact_Edge* const Edge2 = static_cast<IVP_Compact_Edge*>(Candidate->Feature2);
    IVP_Cache_Ledge_Point Cache2;
    Cache2.compact_ledge = Edge2->GetCompactLedge();
    Cache2.cache_object = Candidate->Object2->ResolveNarrowPhaseCacheSlot(); // :0635a290
    Cache2.object = Candidate->Object2;
    Cache2.compact_poly_points = Cache2.compact_ledge->GetPointArray();
    // The library's friction synapse (record+0x1c); none of the leaves reads it.
    Cache2.synapse = reinterpret_cast<IVP_Synapse_Real*>(&Candidate->Next2);

    IVP_Compact_Edge* const Edge1 = static_cast<IVP_Compact_Edge*>(Candidate->Feature1);
    IVP_Cache_Ledge_Point Cache1;
    Cache1.compact_ledge = Edge1->GetCompactLedge();
    Cache1.cache_object = Candidate->Object1->ResolveNarrowPhaseCacheSlot(); // :0635a2da
    Cache1.object = Candidate->Object1;
    Cache1.compact_poly_points = Cache1.compact_ledge->GetPointArray();
    Cache1.synapse = reinterpret_cast<IVP_Synapse_Real*>(&Candidate->Next1); // record+0x8

    IVP_U_Point PointA; // local_38
    IVP_U_Point PointB; // local_68
    bool bSecondSwitch = true;
    switch (ShapeTag1)
    {
    case 0:
        IVP_Compact_Ledge_Solver::give_world_coords_AT(Edge1, &Cache1, &PointA); // :0635a37c
        break;
    case 1:
        IVP_Contact_Point_Calc_Friction_Edge_Edge(Candidate, Edge1, Edge2, &Cache1, &Cache2, Owner, &Situation); // :0635a363
        bSecondSwitch = false;
        break;
    case 3:
        PointA.x = Cache1.cache_object->WorldTransform.vv.x;
        PointA.y = Cache1.cache_object->WorldTransform.vv.y;
        PointA.z = Cache1.cache_object->WorldTransform.vv.z;
        Candidate->_flags60 = (Candidate->_flags60 & 0xFFFFFF01u) | 0x1u;
        break;
    default:
        // :0635a413 `MOV dword ptr [0],0` -- the library's CORE trap;
        // retail then runs the tail.
        IVP_CoreTrap();
        bSecondSwitch = false;
        break;
    }

    if (bSecondSwitch)
    {
        switch (static_cast<uint32_t>(ShapeTag2))
        {
        case 0:
            IVP_Compact_Ledge_Solver::give_world_coords_AT(Edge2, &Cache2, &PointB); // :0635a3b6
            IVP_BuildPointPointContactFrame(Candidate, &PointA, &PointB, Owner, &Situation);
            break;
        case 1:
            IVP_BuildPointEdgeContactFrame(Candidate, &PointA, Edge2, &Cache2, Owner, &Situation);
            break;
        case 2:
            IVP_BuildPointPolyContactFrame(Candidate, &PointA, Edge2, &Cache2, Owner, &Situation);
            break;
        case 3:
            IVP_BuildPointPointContactFrame(Candidate, &PointA, &Cache2.cache_object->WorldTransform.vv, Owner, &Situation);
            break;
        default:
            IVP_CoreTrap(); // 0635a413
            break;
        }
    }

    // :0635a41d-:0635a432 FUN_0628be60(dst = base = &SolverContactPointWS, dir = &ContactNormalWS, r1).
    const float Radius1 = Candidate->Object1->field_94;
    Owner->SolverContactPointWS.y = (float)((double)(Radius1) * Owner->ContactNormalWS.y + Owner->SolverContactPointWS.y);
    Owner->SolverContactPointWS.z = (float)((double)(Radius1) * Owner->ContactNormalWS.z + Owner->SolverContactPointWS.z);
    Owner->SolverContactPointWS.x = (float)((double)(Radius1) * Owner->ContactNormalWS.x + Owner->SolverContactPointWS.x);

    // :0635a437-:0635a45b.
    const double SurfaceGap = (double)(Candidate->GapDistance) -
                              ((double)(Candidate->Object2->field_94) + Radius1);
    Candidate->GapDistance = (float)(SurfaceGap);
    if (!(SurfaceGap >= 0.0))
        Candidate->GapDistance = 0.0f;

    --Cache1.cache_object->RefCount; // :0635a465
    --Cache2.cache_object->RefCount; // :0635a46c

    IVP_ContactFrameSlotAsPoint(&Owner->TangentAxis0)->normize(); // :0635a474 FUN_06347ef0

    // :0635a479-:0635a4ad TangentAxis1 = n x t0.
    const Vector& Normal = Owner->ContactNormalWS;
    const Vector& Tangent0 = Owner->TangentAxis0;
    Owner->TangentAxis1.x = (float)((double)(Normal.y) * Tangent0.z - (double)(Tangent0.y) * Normal.z);
    Owner->TangentAxis1.y = (float)((double)(Normal.z) * Tangent0.x - (double)(Normal.x) * Tangent0.z);
    Owner->TangentAxis1.z = (float)((double)(Tangent0.y) * Normal.x - (double)(Normal.y) * Tangent0.x);

    Vector RelativeSpeed; // local_58

    // ---- side 1, object1's core (:0635a4b3-:0635a685) ----
    IVP_Core* const Core0 = Candidate->Object1->core;
    if ((*reinterpret_cast<const uint8_t*>(Core0) & 0x0Cu) != 0)
    {
        // :0635a64e-:0635a67f
        RelativeSpeed.x = 0.0f;
        RelativeSpeed.y = 0.0f;
        RelativeSpeed.z = 0.0f;
        Owner->EffectiveMassDenom = 0.0f;
        Owner->CoordOS[0].x = 0.0f;
        Owner->CoordOS[0].y = 0.0f;
        Owner->CoordOS[0].z = 0.0f;
        Owner->CoordOSCrossNormal[0].x = 0.0f;
        Owner->CoordOSCrossNormal[0].y = 0.0f;
        Owner->CoordOSCrossNormal[0].z = 0.0f;
        Owner->ChildCandidate[0] = nullptr;
    }
    else
    {
        const IVP_U_Matrix& M = Core0->MWorldFCore;
        const double Dx = (double)(Owner->SolverContactPointWS.x) - M.vv.x;
        const double Dy = (double)(Owner->SolverContactPointWS.y) - M.vv.y;
        const double Dz = (double)(Owner->SolverContactPointWS.z) - M.vv.z;
        Vector& Coord = Owner->CoordOS[0];
        Coord.x = (float)((M.rows[1].x * Dy + M.rows[2].x * Dz) + M.rows[0].x * Dx); // :0635a4e8-:0635a504
        Coord.y = (float)((M.rows[1].y * Dy + M.rows[2].y * Dz) + M.rows[0].y * Dx); // :0635a506-:0635a522
        Coord.z = (float)((M.rows[1].z * Dy + M.rows[2].z * Dz) + M.rows[0].z * Dx); // :0635a525-:0635a541

        // :0635a54a-:0635a5b3 the normal in object space; x stays on the stack.
        const double NormalOSx = ((double)(M.rows[1].x) * Normal.y + (double)(M.rows[2].x) * Normal.z) +
                                 (double)(M.rows[0].x) * Normal.x;
        const float NormalOSy = (float)(((double)(M.rows[2].y) * Normal.z + (double)(M.rows[0].y) * Normal.x) +
                                                   (double)(M.rows[1].y) * Normal.y);
        const float NormalOSz = (float)(((double)(M.rows[1].z) * Normal.y + (double)(M.rows[2].z) * Normal.z) +
                                                   (double)(M.rows[0].z) * Normal.x);

        // :0635a5b7-:0635a604
        const float CrossZa = (float)((double)(NormalOSy) * Coord.x);
        const float CrossZb = (float)(NormalOSx * Coord.y);
        Owner->CoordOSCrossNormal[0].x = (float)((double)(NormalOSz) * Coord.y - (double)(NormalOSy) * Coord.z);
        Owner->CoordOSCrossNormal[0].y = (float)(NormalOSx * Coord.z - (double)(NormalOSz) * Coord.x);
        Owner->CoordOSCrossNormal[0].z = (float)((double)(CrossZa) - CrossZb);

        Core0->calc_point_speed_ws(static_cast<const Vector*>(&Owner->CoordOS[0]), &Core0->Speed, &Core0->RotSpeed,
                                   &RelativeSpeed); // :0635a60a FUN_063461a0

        // :0635a60f-:0635a649
        const Vector& Cross = Owner->CoordOSCrossNormal[0];
        Owner->EffectiveMassDenom = (float)(
            (((double)(Cross.z) * Core0->InvRotInertia.z * Cross.z +
              (double)(Cross.y) * Core0->InvRotInertia.y * Cross.y) +
             (double)(Cross.x) * Core0->InvRotInertia.x * Cross.x) +
            Core0->InvMass);
        Owner->ChildCandidate[0] = Core0; // :0635a685
    }

    // ---- side 2, object2's core (:0635a688-:0635a85b) ----
    IVP_Core* const Core1 = Candidate->Object2->core;
    if ((*reinterpret_cast<const uint8_t*>(Core1) & 0x0Cu) != 0)
    {
        // :0635a835-:0635a859
        Owner->CoordOS[1].x = 0.0f;
        Owner->CoordOS[1].y = 0.0f;
        Owner->CoordOS[1].z = 0.0f;
        Owner->CoordOSCrossNormal[1].x = 0.0f;
        Owner->CoordOSCrossNormal[1].y = 0.0f;
        Owner->CoordOSCrossNormal[1].z = 0.0f;
        Owner->ChildCandidate[1] = nullptr;
    }
    else
    {
        const IVP_U_Matrix& M = Core1->MWorldFCore;
        const double Dx = (double)(Owner->SolverContactPointWS.x) - M.vv.x;
        const double Dy = (double)(Owner->SolverContactPointWS.y) - M.vv.y;
        const double Dz = (double)(Owner->SolverContactPointWS.z) - M.vv.z;
        Vector& Coord = Owner->CoordOS[1];
        Coord.x = (float)((M.rows[2].x * Dz + M.rows[1].x * Dy) + M.rows[0].x * Dx); // :0635a6bb-:0635a6d7
        Coord.y = (float)((M.rows[0].y * Dx + M.rows[1].y * Dy) + M.rows[2].y * Dz); // :0635a6d9-:0635a6f5
        Coord.z = (float)((M.rows[2].z * Dz + M.rows[0].z * Dx) + M.rows[1].z * Dy); // :0635a6f8-:0635a714

        // :0635a71d-:0635a786
        const double NormalOSx = ((double)(M.rows[2].x) * Normal.z + (double)(M.rows[0].x) * Normal.x) +
                                 (double)(M.rows[1].x) * Normal.y;
        const float NormalOSy = (float)(((double)(M.rows[1].y) * Normal.y + (double)(M.rows[2].y) * Normal.z) +
                                                   (double)(M.rows[0].y) * Normal.x);
        const float NormalOSz = (float)(((double)(M.rows[2].z) * Normal.z + (double)(M.rows[0].z) * Normal.x) +
                                                   (double)(M.rows[1].z) * Normal.y);

        // :0635a78a-:0635a7d7
        const float CrossZa = (float)((double)(NormalOSy) * Coord.x);
        const float CrossZb = (float)(NormalOSx * Coord.y);
        Owner->CoordOSCrossNormal[1].x = (float)((double)(NormalOSz) * Coord.y - (double)(NormalOSy) * Coord.z);
        Owner->CoordOSCrossNormal[1].y = (float)(NormalOSx * Coord.z - (double)(NormalOSz) * Coord.x);
        Owner->CoordOSCrossNormal[1].z = (float)((double)(CrossZa) - CrossZb);

        Vector PointSpeed1; // local_38
        Core1->calc_point_speed_ws(static_cast<const Vector*>(&Owner->CoordOS[1]), &Core1->Speed, &Core1->RotSpeed,
                                   &PointSpeed1); // :0635a7dd FUN_063461a0
        // :0635a7f0 FUN_0628bea0(dst = a = &local_58, b = &local_38).
        RelativeSpeed.y = (float)((double)(RelativeSpeed.y) - PointSpeed1.y);
        RelativeSpeed.z = (float)((double)(RelativeSpeed.z) - PointSpeed1.z);
        RelativeSpeed.x = (float)((double)(RelativeSpeed.x) - PointSpeed1.x);

        // :0635a7f5-:0635a830
        const Vector& Cross = Owner->CoordOSCrossNormal[1];
        Owner->EffectiveMassDenom = (float)(
            ((((double)(Core1->InvRotInertia.z) * Cross.z * Cross.z +
               (double)(Cross.y) * Core1->InvRotInertia.y * Cross.y) +
              (double)(Core1->InvRotInertia.x) * Cross.x * Cross.x) +
             Core1->InvMass) +
            Owner->EffectiveMassDenom);
        Owner->ChildCandidate[1] = Core1; // :0635a861
    }

    // :0635a85b-:0635a867 DAT_063a53d0 = 1.0f.
    Owner->EffectiveMassDenomRecip = (float)(1.0 / (double)(Owner->EffectiveMassDenom));

    // :0635a86a-:0635a899 the environment's split current_time (two floats).
    IVP_Environment* const Env = Candidate->Object1->environment;
    const float NowBase = *reinterpret_cast<const float*>(&Env->current_time.time_base);
    const float NowOffset = *reinterpret_cast<const float*>(&Env->current_time.time_offset);
    const double DeltaTime = (((double)(NowBase) - Candidate->_envTick68) + NowOffset) - Candidate->_envTick6c;
    Candidate->_envTick68 = NowBase;
    Candidate->_envTick6c = NowOffset;

    // :0635a895-:0635a8e3
    const double Slip0 = ((double)(RelativeSpeed.z) * Owner->TangentAxis0.z + (double)(RelativeSpeed.y) * Owner->TangentAxis0.y) +
                         (double)(RelativeSpeed.x) * Owner->TangentAxis0.x;
    Candidate->SpanFrictionS[0] = (float)((double)(Candidate->SpanFrictionS[0]) - Slip0 * DeltaTime);
    const double Slip1 = ((double)(RelativeSpeed.z) * Owner->TangentAxis1.z + (double)(RelativeSpeed.y) * Owner->TangentAxis1.y) +
                         (double)(RelativeSpeed.x) * Owner->TangentAxis1.x;
    Candidate->SpanFrictionS[1] = (float)((double)(Candidate->SpanFrictionS[1]) - Slip1 * DeltaTime);
}


// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::do_impact -- FUN_06358cb0 (.text:06358cb0-06358d54,
// thiscall on the mindist, plain RET), retail vtable slot 8 of 063a55e0: the
// library's IVP_Mindist::do_impact. Was the free function
// IVP_ResolveAmbiguousShapeWinner (cmn-phy8 2026-10-08: made the member).
//   :06358cb3-:06358cb8 Env = obj0->environment (+0x18), loaded first.
//   :06358ccd-:06358cdd for both synapse objects (+0x28, +0x44), saved to
//     locals: FUN_063450e0 (IVP_Real_Object::ReviveOriginalCoreIfSleeping).
//   :06358cdf-:06358ce5 env->+0xa4 (short-term IVP_U_Memory) State word += 1.
//   :06358ce9-:06358d18 for both: core = obj->+0x98; low byte of core+0x60
//     (SHL 24, CMP 0x8000000, JGE) < 8 and !(core->+0x0 & 0x300) (TEST AH,3)
//     -> FUN_06347240 (IVP_Core::recalc_interpolated_frame).
//   :06358d1a-:06358d29 env->+0x114 += 1 (integer INC).
//   :06358d2f-:06358d36 FUN_06358680(this, objA, objB), cdecl.
//   :06358d3b-:06358d48 State word -= 1; FUN_06344070 (reset_chunks).
// ---------------------------------------------------------------------------
void IVP_Ledge_Mindist::do_impact()
{
    IVP_Real_Object* const ObjA = synapse[0].l_obj;
    IVP_Real_Object* const ObjB = synapse[1].l_obj;
    IVP_Environment* const Env = ObjA->environment;

    ObjA->ReviveOriginalCoreIfSleeping();
    ObjB->ReviveOriginalCoreIfSleeping();

    ++Env->vector_cores_frozen->State;

    if (int8_t(ObjA->core->MovementState & 0xff) < 8 && (ObjA->core->Flags & 0x300) == 0)
        ObjA->core->recalc_interpolated_frame();
    if (int8_t(ObjB->core->MovementState & 0xff) < 8 && (ObjB->core->Flags & 0x300) == 0)
        ObjB->core->recalc_interpolated_frame();

    ++Env->field_114; // :06358d1a-:06358d29 MOV / INC / MOV

    IVP_Mindist_Do_Impact(this, ObjA, ObjB);

    --Env->vector_cores_frozen->State;
    Env->vector_cores_frozen->reset_chunks(); // FUN_06344070
}

namespace
{

    // (IVP_DEFERRED_LodSwitchDistance = 3e38 removed 2026-10-08, cmn-phy5:
    // DAT_0658ab1c is G_IVP_MindistSettings.friction_dist, and 06362d90
    // compares it against SeparationGap, not CombinedRadius.)

    // Ledgetree-node radius used by the winner-recalc family: retail reaches
    // it as `*(float*)((char*)Ledge + Ledge->LedgetreeNodeOffset + 0x14)`
    // (FUN_06362840 / LAB_06362920, both confirmed via independent
    // instruction traces). Kept local rather than added to the shared
    // ivp_compact_ledge.h since the +0x14 ledgetree-node field itself was
    // not independently identified/named there.
    inline float LedgetreeNodeRadius(const IVP_Compact_Ledge* Ledge)
    {
        const char* Base = reinterpret_cast<const char*>(Ledge);
        return *reinterpret_cast<const float*>(Base + Ledge->LedgetreeNodeOffset + 0x14);
    }

    // Shared "does FeatureA or FeatureB win" decision used identically by
    // FUN_06362840 and LAB_06362920 (independently confirmed to compute the
    // exact same result -- see ivp_mindist.h comment): if ledge A is flagged
    // (SizeAndFlags & 3) and ledge B is not, B wins (0); if both are
    // flagged, whichever has the larger ledgetree-node radius wins; otherwise
    // (A unflagged, or A flagged but not smaller-radius) A wins (1).
    int32_t RecalcWinnerFlag(const IVP_Compact_Edge* FeatureA, const IVP_Compact_Edge* FeatureB)
    {
        const IVP_Compact_Ledge* LedgeA = FeatureA->GetCompactLedge();
        const IVP_Compact_Ledge* LedgeB = FeatureB->GetCompactLedge();
        if ((LedgeA->SizeAndFlags & 3) != 0)
        {
            if ((LedgeB->SizeAndFlags & 3) == 0) return 0;
            if (LedgetreeNodeRadius(LedgeB) < LedgetreeNodeRadius(LedgeA)) return 0;
        }
        return 1;
    }

    // Used only by IVP_Complex_Mindist::do_impact (LAB_06362650). Several of its
    // branches test bit 31 of a feature's OWNING TRIANGLE's TriInfo word --
    // retail masks the edge pointer down to its 16-byte-aligned triangle
    // header first (`AND reg,0xfffffff0`, e.g. Common.dll.txt:0636278c/
    // 063626af/06362776), i.e. exactly IVP_Compact_Edge::GetCompactTriangle().
    // This is a DIFFERENT bit than IVP_Compact_Edge::IsPiercing() (which
    // tests the edge's own unmasked Data word) -- that do_impact
    // uses both, deliberately, in different branches; see its trace comment.
    inline bool TriangleFlagBit31Set(const IVP_Compact_Edge* Feature)
    {
        return (Feature->GetCompactTriangle()->TriInfo & 0x80000000u) != 0;
    }
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::DispatchNarrowPhaseSolve -- FUN_06352cf0 (.text:06352cf0-06352e1b)
// ---------------------------------------------------------------------------
// Thiscall, no arguments, plain RET. Traced from the listing (cmn-ivp2d /
// cmn-ivp2e):
//   :06352d10-:06352d26  tick = synapse[0].l_obj->environment->+0x110; if
//                        [this+0x70] == tick return 4.
//   :06352d2b-:06352d47  [this+0x70] = tick; the 0x81c-byte
//                        IVP_Mindist_Minimize_Solver on the stack: mindist =
//                        this (+0), loop_hash_size = 0 (+0x818),
//                        P_Finish_Counter = 0x14 (+4). EH state 0: its unwind
//                        action jumps to the empty `RET` at 062a8920 -- the
//                        solver has a trivial destructor.
//   :06352d52            retry counter (EBX) = 0.
//   :06352d54-:06352d85  index = sorted(1)->status (FUN_06352e20(1), MOVSX
//                        word +0x1a) + sorted(0)->status * 4 (inline, MOVSX
//                        word [mindist + ((flags>>8)&3)*0x1c + 0x32]);
//                        `CALL [index*4 + DAT_0658ab4c]` (&solver), cdecl.
//   :06352d8a-:06352d8d  result 1 -> :06352dce StateFlags &= 0xffff3fff,
//                        return 1 (no rescue push).
//   :06352d8f-:06352da7  otherwise StateFlags = (StateFlags & 0xffff7fff) |
//                        0x4000; result 2 -> tail; result 3 -> :06352db9.
//   :06352da9-:06352db7  anything else: the library's CORE null store, then
//                        the dispatch is repeated with ECX = solver.mindist
//                        (no retry consumed).
//   :06352db9-:06352dcc  proove_polypoly (06352b30) on the solver; ++EBX;
//                        EBX >= 2 -> tail, else dispatch again.
//   :06352ddc-:06352e02  tail: unless (StateFlags & 0x3000) == 0x1000 or
//                        (StateFlags & 0x3c0000) == 0x100000, `CALL
//                        [vtbl+0x14]` = slot 5 (mindist_rescue_push).
//                        Returns the handler result.
int32_t IVP_Ledge_Mindist::DispatchNarrowPhaseSolve()
{
    const uint32_t Tick = static_cast<uint32_t>(synapse[0].l_obj->environment->sim_step_count);
    if (LastDispatchTick == Tick)
        return IVP_MRC_ALREADY_CALCULATED;
    LastDispatchTick = Tick;

    IVP_Mindist_Minimize_Solver Solver;
    Solver.mindist = this;          // :06352d30
    Solver.loop_hash_size = 0;      // :06352d34
    Solver.P_Finish_Counter = 0x14; // :06352d3f

    int32_t RetryCount = 0;
    int32_t Result;
    for (;;)
    {
        IVP_Ledge_Mindist* const Mindist = Solver.mindist;
        const int32_t TableIndex = Mindist->get_sorted_synapse(1)->status + Mindist->get_sorted_synapse(0)->status * 4;
        // Retail indexes the flat 16-entry table directly (no range check).
        Result = (&IVP_Mindist_Minimize_Solver::mms_function_table[0][0])[TableIndex](&Solver);

        if (Result == IVP_MRC_OK)
        {
            StateFlags &= 0xffff3fffu; // :06352dce
            return IVP_MRC_OK;
        }

        StateFlags = (StateFlags & 0xffff7fffu) | 0x4000u;
        if (Result == IVP_MRC_ENDLESS_LOOP)
            break;
        if (Result == IVP_MRC_BACKSIDE)
        {
            Solver.proove_polypoly(); // :06352dbd FUN_06352b30
            if (++RetryCount >= 2)
                break;
            continue;
        }
        // :06352da9 `MOV dword ptr [0],0` -- the library's CORE trap,
        // retail then repeats the dispatch.
        IVP_CoreTrap();
    }

    if ((StateFlags & 0x3000u) != 0x1000u && (StateFlags & 0x3c0000u) != 0x100000u)
        mindist_rescue_push(); // :06352dff `CALL [EDX+0x14]`, slot 5

    return Result;
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::recalc_mindist -- FUN_06352b90 (.text:06352b90-06352ce2)
// ---------------------------------------------------------------------------
// Thiscall, no arguments, plain RET. The library's IVP_Mindist::recalc_mindist.
//   :06352baf-:06352bbe  tick = synapse[0].l_obj->environment->+0x110; if
//                        [this+0x70] == tick return 4.
//   :06352bde-:06352bfb  [this+0x70] = tick; the solver on the stack:
//                        mindist = this (+0), loop_hash_size = 0 (+0x818),
//                        P_Finish_Counter = 0 (+4); EH state 0 as in 06352cf0.
//   :06352c03-:06352c3d  index = sorted(1)->status + sorted(0)->status * 4,
//                        both inline (flags and flags ^ 0x100, MOVSX word
//                        +0x32 / +0x1c stride); `CALL [index*4 + DAT_0658ab4c]`.
//   :06352c49-:06352c4c  result 1 -> :06352c78 StateFlags &= 0xffff3fff, return 1.
//   :06352c4e-:06352c66  otherwise StateFlags = (StateFlags & 0xffff7fff) |
//                        0x4000; 2 -> tail; 3 -> :06352c9b proove_polypoly,
//                        then the tail (no retry).
//   :06352c68-:06352c76  anything else: CORE null store, mindist reloaded from
//                        the solver, dispatch again.
//   :06352ca4-:06352cc7  tail as in 06352cf0 (slot 5 unless 0x1000 / 0x100000).
int32_t IVP_Ledge_Mindist::recalc_mindist()
{
    const uint32_t Tick = static_cast<uint32_t>(synapse[0].l_obj->environment->sim_step_count);
    if (LastDispatchTick == Tick)
        return IVP_MRC_ALREADY_CALCULATED;
    LastDispatchTick = Tick;

    IVP_Mindist_Minimize_Solver Solver;
    Solver.mindist = this;       // :06352be1
    Solver.loop_hash_size = 0;   // :06352be5
    Solver.P_Finish_Counter = 0; // :06352bfb

    int32_t Result;
    for (;;)
    {
        IVP_Ledge_Mindist* const Mindist = Solver.mindist;
        const int32_t TableIndex = Mindist->get_sorted_synapse(1)->status + Mindist->get_sorted_synapse(0)->status * 4;
        Result = (&IVP_Mindist_Minimize_Solver::mms_function_table[0][0])[TableIndex](&Solver);

        if (Result == IVP_MRC_OK)
        {
            StateFlags &= 0xffff3fffu; // :06352c78
            return IVP_MRC_OK;
        }

        StateFlags = (StateFlags & 0xffff7fffu) | 0x4000u;
        if (Result == IVP_MRC_ENDLESS_LOOP)
            break;
        if (Result == IVP_MRC_BACKSIDE)
        {
            Solver.proove_polypoly(); // :06352c9f FUN_06352b30
            break;
        }
        // :06352c68 `MOV dword ptr [0],0` -- the library's CORE trap,
        // retail then repeats the dispatch.
        IVP_CoreTrap();
    }

    if ((StateFlags & 0x3000u) != 0x1000u && (StateFlags & 0x3c0000u) != 0x100000u)
        mindist_rescue_push(); // :06352cc7 `CALL [EDX+0x14]`, slot 5

    return Result;
}

// LAB_06352b10 (.text:06352b10-06352b2a), retail vtable 063a55e0 slot 5.
// EDX = this->synapse[0].l_obj (+0x28); ECX = EDX->environment (+0x18)
// ->anomaly_manager (+0x20); push synapse[1].l_obj (+0x44), EDX, this;
// `CALL [vtbl+0x8]` -- IVP_Anomaly_Manager slot 2, inter_penetration.
void IVP_Ledge_Mindist::mindist_rescue_push()
{
    IVP_Real_Object* const Object0 = synapse[0].l_obj;
    Object0->environment->anomaly_manager->inter_penetration(this, Object0, synapse[1].l_obj);
}

// ---------------------------------------------------------------------------
// IVP_Mindist_Base::GetLedges
// ---------------------------------------------------------------------------
// LAB_0634de90 (decomp-tracker item 3096, Common.dll.txt:423412-423430, 18
// insns). D227 pass (2026-09-10). Retail vtable slot 2 of IVP_Mindist_Base
// (063a55ac), inherited unchanged by 063a55e0 / 063a5878.
// Body: `Out[0] = ((FeatureA & ~0xF) as IVP_Compact_Triangle*)->GetCompactLedge();`
// and the same for FeatureB/Out[1] -- i.e. masking a tagged feature pointer
// down to its enclosing 16-byte-aligned IVP_Compact_Triangle (the exact same
// mask IVP_Compact_Edge::GetCompactTriangle() already applies) and then
// applying IVP_Compact_Triangle::GetCompactLedge()'s own formula
// (`Triangle - (TriIndex+1)*16`) BY HAND, instruction for instruction
// identical to that existing inline method in ivp_compact_ledge.h. Sibling of
// LAB_0634de70 (slot 1, "GetObjects": `Out[0]=ObjectA; Out[1]=ObjectB;`, NOT
// one of this task's assigned items).
void IVP_Mindist_Base::GetLedges(const IVP_Compact_Ledge* Out[2]) const
{
    auto RecoverLedge = [](const void* TaggedFeature) -> const IVP_Compact_Ledge*
    {
        const IVP_Compact_Triangle* Triangle = reinterpret_cast<const IVP_Compact_Triangle*>(
            reinterpret_cast<uintptr_t>(TaggedFeature) & ~static_cast<uintptr_t>(0x0F));
        return Triangle->GetCompactLedge();
    };
    Out[0] = RecoverLedge(synapse[0].edge);
    Out[1] = RecoverLedge(synapse[1].edge);
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::exact_mindist_went_invalid
// ---------------------------------------------------------------------------
// LAB_0634e9d0 (decomp-tracker item 3108, .text:0634e9d0-0634e9ea, RET 4),
// retail vtable slot 7 (063a55fc). cmn-phy5 2026-10-08: the stack argument
// is the mindist manager and is `this` of both calls (`MOV ECX,EDI`), the
// mindist is their stack argument (`PUSH ESI`).
void IVP_Ledge_Mindist::exact_mindist_went_invalid(IVP_Mindist_Manager* Manager)
{
    Manager->DetachMindist(this);          // :0634e9db FUN_0634fec0
    Manager->insert_invalid_mindist(this); // :0634e9e3 FUN_0634e960
}

// ===========================================================================
// FUN_0634f320 / FUN_06350140 / FUN_06350050
// (decomp-tracker items 3117 / 3129 / 3128)
// ===========================================================================
// Ported 2026-09-17, lane claude:w16-ivp, from Common.dll.txt:425448-425942,
// :426767-426830 and :426671-426753 read end to end. All three had been
// carried as `blocked` since 2026-09-09; every field, constant and global
// those blockers asked for is resolved (see the tolerance-block writeup at the
// top of this file and IVP_Hull_Manager / IVP_Environment in the headers).
// What is left deferred is ONE call, the estimator dispatch below.
//
// THREE READINGS IN THE INHERITED NOTES ARE WRONG AND ARE CORRECTED HERE, each
// one checked instruction by instruction against the listing:
//   a. Item 3117's step 12 claims the final horizon test "must be >= 0.0f or
//      the function returns without queueing". Backwards. :0634f957 is
//      `FCOMP [DAT_063a53d4]` with DAT_063a53d4 == 0.0f (bytes 00 00 00 00 at
//      .rdata:063a53d4), so C0 means the value is < 0, and the `TEST AH,0x1;
//      JZ LAB_0634f9a0` at :0634f95f-:0634f962 RETURNS on >= 0. The event is
//      enqueued only when it lands BEFORE the next PSI, which is also the only
//      reading that makes physical sense.
//   b. Item 3117's step 8 calls the StateFlags bits 22-29 update "bumped by
//      one". It is a DECREMENT: :0634f592-:0634f5a3 is
//      `EDX = S & 0xffc00000; EDX--; EDX ^= S; EDX &= 0x3fc00000; EDX ^= S`,
//      and because the low 22 bits of `S & 0xffc00000` are zero the DEC
//      borrows all the way up, leaving (field - 1) in bits 22-29.
//   c. Item 3129's summary has FUN_06350140's two flag branches inverted. The
//      listing is `TEST byte ptr [EAX+0x78],0x7` + `JNZ` at :06350147-:0635014e,
//      so the branch that is TAKEN INTO the first FUN_06350050 call is
//      (ObjectA->flags & 7) == 0, not != 0. Same at :06350161-:06350165 for
//      ObjectB. The argument mapping in that note (param_2 -> side A's key,
//      param_3 -> side B's key, and the k*(b*0.1+a) / k*(a*0.1+b) split) is
//      correct and is reproduced verbatim.
//
// One more inherited claim re-measured and DISPROVEN while wiring this up:
// ivp_environment.h's +0xF8 comment warns that "`IVP_Time` (ivp_core.h) still
// declares its two members `uint32_t`, which forces ~25
// `*reinterpret_cast<const float*>(&...)` spellings". It does not -- ivp_core.h
// declares `float time_base; float time_offset;` today. That note is stale; the
// code below reads the pair as plain floats.
// ===========================================================================

namespace
{
    // The 0x34-byte context FUN_0634f320 builds on its own frame at BASE+0x20
    // and hands to the estimator as a single pointer (`LEA EAX,[ESP+0x20];
    // PUSH EAX; CALL [EDX*4 + DAT_0658af9c]`, Common.dll.txt:0634f5e5-:0634f5ea).
    // Offsets are retail's, derived by matching every write before the call
    // against every read after it; the two estimator handlers that actually do
    // work (LAB_0635fef0 / LAB_0635fd10) read +0x10 and write +0x28 through
    // exactly these offsets (:0635fef7, :0635ffa8 / :0635fd18, :0635fda8).
    // cmn-phy8 2026-10-08: the limiter's private IVP_CollisionTimeEstimatorContext
    // (a second declaration of this same 0x34-byte object) and its casting
    // wrapper IVP_InvokeCollisionTimeEstimator are gone; FUN_0634f320 builds
    // the one IVP_Sweep_Estimator_Context (ivp_ledge_sweep_solver.h) and calls
    // IVP_InvokeSweepEstimator directly. Field map: +0x00 RadiusSum, +0x04
    // RelativeSpeed, +0x08 ClosingBound, +0x0c MarginSpan, +0x10 Mindist,
    // +0x14 Env, +0x18/+0x1c NowBase/NowOffset (env current_time), +0x20/+0x24
    // NextBase/NextOffset (env next PSI time), +0x28 OutCode, +0x2c/+0x30
    // OutEventBase/OutEventOffset.
    static_assert(sizeof(IVP_Sweep_Estimator_Context) == 0x34,
                  "estimator context must be retail's 0x34 bytes");

    // The estimator dispatch `CALL dword ptr [TableIndex*4 + DAT_0658af9c]`
    // (:0634f5ea) is IVP_InvokeSweepEstimator (ivp_ledge_sweep_solver.cpp):
    // DAT_0658af9c is 16 slots filled by FUN_06360090 -- slots 0,1,2,5 ->
    // LAB_0635fef0, 12,13,14 -> LAB_0635fd10, 15 -> FUN_0635fe30, the other 8
    // -> LAB_06360080 (`MOV [0],0; RET`, retail's trap).

    // Retail's split-precision renormalization, inlined six times in
    // FUN_0634f320 (:0634f6b8, :0634f702, :0634f78b, :0634f82a, :0634f874,
    // :0634f8fe). Each copy is the same `FXCH; FADD 1.0; FXCH; FSUB 1.0;
    // FCOM 1.0; TEST AH,0x41; JZ <top>` pair over (Base, Offset), i.e. carry
    // whole units out of Offset into Base while Offset is strictly greater
    // than 1.0 (`TEST AH,0x41; JNZ` before the loop: <= or unordered skips
    // it). DAT_063a53d0 is 1.0f (.rdata bytes 00 00 80 3f). Both values stay
    // on the x87 stack through the loop, so they are doubles here; the
    // caller spills them (FST/FSTP dword) where retail does (cmn-phy8).
    inline void IVP_RenormalizeSplitTime(double& Base, double& Offset)
    {
        while (Offset > 1.0)
        {
            Base += 1.0;
            Offset -= 1.0;
        }
    }

}

// ---------------------------------------------------------------------------
// FUN_06350050 (decomp-tracker item 3128)
// ---------------------------------------------------------------------------
// Re-keys both sides' hull managers for the slack this pair may still consume
// before its cached mindist stops being valid, and caches the resulting total
// spread on the mindist. Both `Slack` arguments are retail's param_2/param_3.
static void IVP_RefreshHullKeysAndSpread(IVP_Ledge_Mindist* M, float SlackA, float SlackB)
{
    // :0635005b-:06350068
    M->StateFlags = (M->StateFlags & 0xffd7ffffu) | 0x140000u;

    // cmn-phy5 2026-10-08: rewritten per listing with the synapses as objects.
    // Per side (synapse[0] :0635006b-:063500e3, synapse[1] :063500c7-:06350129),
    // with Obj = synapse.l_obj and the time read through Obj->environment:
    //   Elapsed = ((now.base - hull.LastEditTimeHi) + now.offset) - hull.LastEditTimeLo
    //             (x87, `FST float` -> kept as a float for the spread below)
    //   Key     = (Elapsed * OuterGradient + OuterValueAtLastEdit) + Slack (x87 -> float arg)
    //   synapse.HullPoolSlot = hull pool Insert(&synapse, Key)  (06362f90, object +0x64)
    //   Spread  = (OuterGradient - InnerGradient) * Elapsed
    //           + (OuterValueAtLastEdit - InnerValueAtLastEdit)
    // Side 0's spread is stored as a float (:063500e3, into the param_2 slot);
    // side 1's stays on the x87 stack, gets side 0's added and lands in
    // CombinedHullSpread (+0x5c, :06350129).
    float SpreadA = 0.0f;
    for (int32_t Side = 0; Side < 2; ++Side)
    {
        IVP_Synapse_Real* const Syn = &M->synapse[Side];
        IVP_Real_Object* const Obj = Syn->l_obj;
        IVP_Hull_Manager& Hull = Obj->hull_manager;
        const IVP_Environment* const Env = Obj->environment;

        const float Elapsed = (float)(
            (((double)(Env->current_time.time_base) - Hull.LastEditTimeHi)
             + Env->current_time.time_offset) - Hull.LastEditTimeLo);
        const float Key = (float)(
            ((double)(Elapsed) * Hull.OuterGradient + Hull.OuterValueAtLastEdit)
            + ((Side == 0) ? SlackA : SlackB));

        Syn->HullPoolSlot = Hull.PoolInsert(static_cast<IVP_Listener_Hull*>(Syn), Key);

        const double Spread = ((double)(Hull.OuterGradient) - Hull.InnerGradient) * Elapsed
                              + ((double)(Hull.OuterValueAtLastEdit) - Hull.InnerValueAtLastEdit);
        if (Side == 0)
            SpreadA = (float)(Spread);
        else
            M->CombinedHullSpread = (float)(Spread + SpreadA);
    }
}

// ---------------------------------------------------------------------------
// FUN_06350140 (decomp-tracker item 3129)
// ---------------------------------------------------------------------------
// Splits one separation slack between the two sides' hull managers. __cdecl in
// retail (both args on the stack, ECX unused).
static void IVP_DistributeHullSlack(IVP_Ledge_Mindist* M, float Slack)
{
    IVP_Real_Object* const ObjA = M->synapse[0].l_obj; // :06350144
    IVP_Real_Object* const ObjB = M->synapse[1].l_obj; // :0635014b

    // :06350147 / :06350161 -- `TEST byte ptr [obj+0x78],0x7` + `JNZ`, so the
    // call is made when the masked flags are ZERO (see correction (c) above).
    if ((ObjA->flags & 7u) == 0)
    {
        IVP_RefreshHullKeysAndSpread(M, 0.0f, Slack);
        return;
    }
    if ((ObjB->flags & 7u) == 0)
    {
        IVP_RefreshHullKeysAndSpread(M, Slack, 0.0f);
        return;
    }

    const IVP_Core* const CoreA = ObjA->core;
    const IVP_Core* const CoreB = ObjB->core;

    // :06350178-:063501a3, both extents stay on the x87 stack. DAT_063a53d8 ==
    // 1e-10f (.rdata bytes ff e6 db 2e); DAT_063a5464 == 0.1f (cd cc cc 3d).
    const double ExtentA = ((double)(CoreA->BoundingRadius) + CoreA->Margin) + 1.0e-10f;
    const double ExtentB = ((double)(CoreB->BoundingRadius) + CoreB->Margin) + 1.0e-10f;

    // :063501a9-:063501b3 WeightA = ExtentB*0.1 + ExtentA, stored as a float
    // into the param_1 slot; :063501b7-:063501c3 WeightB = ExtentA*0.1 + ExtentB
    // stays on the stack.
    const float WeightA = (float)(ExtentB * 0.1f + ExtentA);
    const double WeightB = ExtentA * 0.1f + ExtentB;

    // :063501c5-:063501cf -- `FLD ST0; FADD WeightA; FDIVR Slack; FST float`:
    // the stack keeps the extended quotient, the param_2 slot gets the float.
    const double ScaleExt = (double)(Slack) / (WeightB + WeightA);
    const float Scale = (float)(ScaleExt);

    // :063501d3-:063501d5 side B's slack = ScaleExt * WeightB (both extended);
    // :063501db-:063501e3 side A's slack = Scale(float) * WeightA(float).
    const float SlackB = (float)(ScaleExt * WeightB);
    const float SlackA = (float)((double)(Scale) * WeightA);
    IVP_RefreshHullKeysAndSpread(M, SlackA, SlackB);
}

// ---------------------------------------------------------------------------
// FUN_063501f0 (.text:063501f0-06350248, cdecl: mindist, slackA, slackB),
// cmn-phy5 2026-10-08 -- the insert half of the library's insert_hull_mindist.
// StateFlags = (x & 0xffd7ffff) | 0x140000; then for synapse 0 (:06350206-
// :06350223) and synapse 1 (:06350226-:06350244): key = float(object+0x5c
// (hull_manager.AccumulatedDeltaA, +0x44+0x18) + slack), Insert(synapse, key)
// on object+0x64, slot -> synapse+4. No spread is written (unlike 06350050).
// ---------------------------------------------------------------------------
static void IVP_InsertHullMindistKeys(IVP_Ledge_Mindist* M, float SlackA, float SlackB)
{
    M->StateFlags = (M->StateFlags & 0xffd7ffffu) | 0x140000u;
    for (int32_t I = 0; I < 2; ++I)
    {
        IVP_Synapse_Real* const Syn = &M->synapse[I];
        IVP_Hull_Manager& Hull = Syn->l_obj->hull_manager;
        const float Key = (float)((double)(Hull.AccumulatedDeltaA) + ((I == 0) ? SlackA : SlackB));
        Syn->HullPoolSlot = Hull.PoolInsert(static_cast<IVP_Listener_Hull*>(Syn), Key);
    }
}

// ---------------------------------------------------------------------------
// FUN_06350250 (.text:06350250-06350305, cdecl: mindist, slack), cmn-phy5
// 2026-10-08 -- the library's insert_hull_mindist. The exact twin of
// IVP_DistributeHullSlack (06350140) except that the "other" side of the
// two early branches gets 1e-10f (`PUSH 0x2edbe6ff`, :06350265 / :0635027e)
// instead of 0, and it ends in 063501f0 instead of 06350050:
//   (obj0->flags & 7) == 0 -> 063501f0(M, 1e-10f, slack);
//   (obj1->flags & 7) == 0 -> 063501f0(M, slack, 1e-10f);
//   else the same extent/weight split as 06350140 (:0635028e-:063502fd).
// Callers: insert_lazy_mindist 0634eb8a, recalc_exact_mindist 06350515,
// 063627ea and 063629f? (recursive mindist).
// ---------------------------------------------------------------------------
static void IVP_InsertHullMindist(IVP_Ledge_Mindist* M, float Slack)
{
    IVP_Real_Object* const ObjA = M->synapse[0].l_obj; // :06350254
    IVP_Real_Object* const ObjB = M->synapse[1].l_obj; // :0635025b

    if ((ObjA->flags & 7u) == 0)
    {
        IVP_InsertHullMindistKeys(M, 1.0e-10f, Slack);
        return;
    }
    if ((ObjB->flags & 7u) == 0)
    {
        IVP_InsertHullMindistKeys(M, Slack, 1.0e-10f);
        return;
    }

    const IVP_Core* const CoreA = ObjA->core;
    const IVP_Core* const CoreB = ObjB->core;
    const double ExtentA = ((double)(CoreA->BoundingRadius) + CoreA->Margin) + 1.0e-10f;
    const double ExtentB = ((double)(CoreB->BoundingRadius) + CoreB->Margin) + 1.0e-10f;
    const float WeightA = (float)(ExtentB * 0.1f + ExtentA);      // :063502bf-:063502c9
    const double WeightB = ExtentA * 0.1f + ExtentB;                          // :063502cd-:063502d9
    const double ScaleExt = (double)(Slack) / (WeightB + WeightA); // :063502db-:063502e1
    const float Scale = (float)(ScaleExt);                         // :063502e5
    const float SlackB = (float)(ScaleExt * WeightB);              // :063502e9-:063502eb
    const float SlackA = (float)((double)(Scale) * WeightA); // :063502f1-:063502f9
    IVP_InsertHullMindistKeys(M, SlackA, SlackB);
}

// ---------------------------------------------------------------------------
// FUN_0634fec0 (decomp-tracker item 3125) -- IVP_Mindist_Manager::DetachMindist
// ---------------------------------------------------------------------------
// Ported 2026-09-17 (lane claude:w26-simunit). Address range read in full:
// 0x0634fec0-0x0634ff8f, all 79 instructions (Common.dll.txt:426466-426557),
// prologue through both epilogue tails (LAB_0634ff6b and LAB_0634ff8d).
//
// The name is this port's own and it is a correction: this function was
// previously stubbed as "IVP_DEFERRED_RearmPendingHullCheck", and before that
// pooled into "IVP_DEFERRED_LinkAsPendingSynapse", on the strength of WHERE it
// is called rather than what it does. It does not re-arm or link anything. It
// is `this` mindist manager's full DETACH: cancel the mindist's queued
// collision event, unlink it from the manager's own list, unlink both of its
// synapse slots from their objects' synapse lists, and remove it from the
// manager's ActiveMindists vector. (The caller at :0634f3fe then follows it
// with IVP_DistributeHullSlack, and it is THAT call, not this one, that arms
// the hull-distance wakeup -- which is presumably where the old name came from.)
//
// Retail's `this` is the mindist manager: :0634f3fa loads ECX from
// `[EBP+0x10]`, Environment+0x10. `RET 0x4` -- one stack argument, the mindist.
void IVP_Mindist_Manager::DetachMindist(IVP_Ledge_Mindist* Mindist)
{
    // (The port-only null-Mindist / null-environment guards were removed
    // 2026-10-08, cmn-phy5: retail dereferences unconditionally.)

    // :0634fec5-:0634fedd. Cancel the queued collision event, through the
    // environment rather than through `this`: `EAX=[EDI+0x4]` is this manager's
    // OWN environment back-pointer (+0x04) and `ECX=[EAX+0x4]` is that
    // environment's sim_unit_manager (+0x04) -- two different +0x04 fields on
    // two different classes, one after the other.
    if (Mindist->EventQueueHandle != 0xffff) // :0634fec5 `CMP dword ptr [ESI+0x4],0xffff`
    {
        environment->sim_unit_manager->CancelEvent(Mindist->EventQueueHandle); // FUN_063609f0
        Mindist->EventQueueHandle = 0xffff; // :0634fedd
    }

    // :0634fee4-:0634ff06. Unlink from this manager's own mindist list. Only a
    // node whose Prev is null rewrites the head, which is what fixes +0x74 as
    // next and +0x78 as prev (see the field comments in ivp_mindist.h).
    if (Mindist->ManagerNext != nullptr)
        Mindist->ManagerNext->ManagerPrev = Mindist->ManagerPrev; // :0634feee
    if (Mindist->ManagerPrev != nullptr)
        Mindist->ManagerPrev->ManagerNext = Mindist->ManagerNext; // :0634fefb
    else
        ActiveMindistHead = Mindist->ManagerNext;                 // :0634ff03

    // :0634ff06-:0634ff2b. Unlink synapse slot A from ObjectA's synapse list.
    // Retail's null-prev branch reads the object out of the SLOT (`[ESI+0x28]`,
    // which is synapse-A + 0x10), not out of some separate field.
    // :0634ff2b-:0634ff50 is the same for slot B and its object (`[ESI+0x44]`).
    // (The two port-only `object != nullptr` tests on the prev-less branch are
    // gone, cmn-phy5 2026-10-08: retail writes the head unconditionally.)
    for (int32_t I = 0; I < 2; ++I)
    {
        IVP_Synapse_Real* const Syn = &Mindist->synapse[I];
        if (Syn->next != nullptr)
            Syn->next->prev = Syn->prev;                          // :0634ff10 / :0634ff35
        if (Syn->prev != nullptr)
            Syn->prev->next = Syn->next;                          // :0634ff1d / :0634ff42
        else
            Syn->l_obj->synapse_list_head = Syn->next;            // :0634ff28 / :0634ff4d
    }

    // :0634ff50-:0634ff8a. Remove it from ActiveMindists, BACKWARD, and with
    // retail's own swap-with-last (not a shift): `DEC word[EDI+0xe];
    // CX=word[EDI+0xe]; [EDX+EAX*4]=[EDX+ECX*4]`. That is
    // IVP_U_Vector::RemoveAtAndAllowSort, which this port already has -- note
    // it is NOT RemoveElement, whose RemoveAt shifts the tail down instead.
    // Retail scans from n_elems-1 downward and stops at the FIRST (i.e.
    // highest-index) match; it does not continue looking for duplicates.
    for (int Index = static_cast<int>(ActiveMindists.n_elems) - 1; Index >= 0; --Index)
    {
        if (ActiveMindists.elems[Index] == Mindist)
        {
            ActiveMindists.RemoveAtAndAllowSort(Index);
            return; // :0634ff8d
        }
    }
    // :0634ff6b -- ran off the front without a match: return, touching nothing.
}

// ---------------------------------------------------------------------------
// IVP_Mindist_Manager::insert_exact_mindist -- FUN_0634e9f0
// (.text:0634e9f0-0634eaef, RET 4), cmn-phy5 2026-10-08.
// ---------------------------------------------------------------------------
namespace
{
    // The synapse at Slot (mindist+0x18 or +0x34) pushed onto the head of
    // *Head: Slot->Next = *Head; if (*Head) (*Head)->Prev = Slot;
    // Slot->Prev = 0; *Head = Slot. Same shape at :0634ea1d-:0634ea3b /
    // :0634ea3e-:0634ea54 (exact list, object+0x20) and :0634e98d-:0634e9ad /
    // :0634e9b0-:0634e9ca (invalid list, object+0x24).
    void IVP_PushSynapseNode(IVP_Synapse_Real* Slot, IVP_Synapse_Real** Head)
    {
        IVP_Synapse_Real* OldHead = *Head;
        Slot->next = OldHead;
        if (OldHead != nullptr)
            OldHead->prev = Slot;
        Slot->prev = nullptr;
        *Head = Slot;
    }

    IVP_Synapse_Real* IVP_SynapseSlotA(IVP_Ledge_Mindist* Mindist)
    {
        return &Mindist->synapse[0]; // mindist+0x18
    }

    IVP_Synapse_Real* IVP_SynapseSlotB(IVP_Ledge_Mindist* Mindist)
    {
        return &Mindist->synapse[1]; // mindist+0x34
    }
}

void IVP_Mindist_Manager::insert_exact_mindist(IVP_Ledge_Mindist* Mindist)
{
    // :0634e9f6-:0634ea05 -- clear bits 20/21, set bits 18/19.
    Mindist->StateFlags = (Mindist->StateFlags & 0xffcfffffu) | 0xc0000u;

    // :0634ea08-:0634ea1a -- push onto this manager's exact list (+0x08).
    Mindist->ManagerNext = ActiveMindistHead;
    Mindist->ManagerPrev = nullptr;
    if (Mindist->ManagerNext != nullptr)
        Mindist->ManagerNext->ManagerPrev = Mindist;

    // :0634ea1d-:0634ea54 -- both synapses onto their objects' +0x20 lists.
    IVP_PushSynapseNode(IVP_SynapseSlotA(Mindist), &Mindist->synapse[0].l_obj->synapse_list_head);
    IVP_PushSynapseNode(IVP_SynapseSlotB(Mindist), &Mindist->synapse[1].l_obj->synapse_list_head);

    ActiveMindistHead = Mindist;          // :0634ea59
    Mindist->DispatchNarrowPhaseSolve();  // :0634ea5c FUN_06352cf0

    // :0634ea61-:0634eaa1 -- either core has a non-null +0x10 -> also keep it
    // in the ActiveMindists vector (+0x0c).
    if (Mindist->synapse[0].l_obj->core->AxisLock != nullptr || Mindist->synapse[1].l_obj->core->AxisLock != nullptr)
        ActiveMindists.Add(Mindist);

    // :0634eaa5-:0634eaef
    if ((Mindist->StateFlags & 0xc000u) != 0) // `TEST AH,0xc0; JNZ`
    {
        Mindist->exact_mindist_went_invalid(this); // :0634eae5 `CALL [EDX+0x1c]` (slot 7) with this manager
        return;
    }
    // (coreB->MovementState | coreA->MovementState) << 24, signed `< 0x21000000`.
    const int32_t Movement = static_cast<int32_t>(
        (Mindist->synapse[1].l_obj->core->MovementState | Mindist->synapse[0].l_obj->core->MovementState) << 24); // :0634eaaf-:0634eac9
    IVP_RescheduleCollisionEvent(Mindist, (Movement < 0x21000000) ? 1 : 0, 0); // :0634eacc-:0634eadb (SETL)
}

// ---------------------------------------------------------------------------
// IVP_Mindist_Manager::insert_lazy_mindist -- FUN_0634eb00
// (.text:0634eb00-0634ebc3, RET 4), cmn-phy5 2026-10-08.
//   :0634eb0a FUN_06352b90(M) = M->recalc_mindist() (result unused).
//   :0634eb0f-:0634eb15 if !(flags & 0xc000):
//     :0634eb17-:0634eb25 SeparationGap(+0x54) vs 0.0f (DAT_063a53d4), `TEST
//       AH,0x41; JNZ` -> <= / unordered goes to :0634eb4d; strictly greater:
//       (flags & 0xc00) -> 06350350(M); insert_exact_mindist_no_recalc(M)
//       (0634e8a0); 0634f320(M, 1, 2); return.
//     :0634eb4d slack = -SeparationGap.
//   else :0634eb58 slack = CombinedRadius (+0x50).
//   :0634eb5f-:0634eb80 phantom = obj0->+0x1c, else obj1->+0x1c; if one
//     exists: slack = float(slack + phantom->+4), EDI = phantom->+0x40 (else 0).
//   :0634eb84-:0634eb8a 06350250(M, slack).
//   :0634eb92-:0634ebab EDI == 1 and M->is_recursive() -> 06362840(M, mm)
//     and return; EDI == 1 and not recursive -> :0634ebb8.
//   :0634ebb0-:0634ebb6 EDI != 1: (flags & 0xc00) -> return.
//   :0634ebb8 06350310(M) (add to phantoms).
// Phantom fields are the typed IVP_Controller_Phantom members:
// +0x4 exit_policy_extra_radius, +0x40 client_data (compared as an int).
// cmn-phy8 2026-10-08: full re-trace, typed phantom access.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::insert_lazy_mindist(IVP_Ledge_Mindist* Mindist)
{
    Mindist->recalc_mindist(); // :0634eb0a FUN_06352b90

    float Slack;
    if ((Mindist->StateFlags & 0xc000u) == 0) // :0634eb0f-:0634eb15 TEST CH,0xc0
    {
        if (Mindist->SeparationGap > 0.0f) // :0634eb17-:0634eb25 TEST AH,0x41; JNZ = not-greater
        {
            if ((Mindist->StateFlags & 0xc00u) != 0) // :0634eb27 TEST CH,0xc
                IVP_RemoveMindistFromPhantoms(Mindist);
            insert_exact_mindist_no_recalc(Mindist);
            IVP_RescheduleCollisionEvent(Mindist, 1, 2);
            return;
        }
        Slack = -Mindist->SeparationGap; // :0634eb4d-:0634eb52 FCHS, FSTP dword
    }
    else
    {
        Slack = Mindist->CombinedRadius; // :0634eb58-:0634eb5b
    }

    int32_t PhantomFlag = 0; // EDI, :0634eb66
    const IVP_Controller_Phantom* Phantom = static_cast<const IVP_Controller_Phantom*>(Mindist->synapse[0].l_obj->first_child);
    if (Phantom == nullptr)
        Phantom = static_cast<const IVP_Controller_Phantom*>(Mindist->synapse[1].l_obj->first_child);
    if (Phantom != nullptr)
    {
        PhantomFlag = static_cast<int32_t>(reinterpret_cast<intptr_t>(Phantom->client_data)); // :0634eb7a
        Slack = (float)((double)(Slack) + Phantom->exit_policy_extra_radius); // :0634eb76-:0634eb80
    }

    IVP_InsertHullMindist(Mindist, Slack); // :0634eb8a FUN_06350250

    if (PhantomFlag == 1)
    {
        if (Mindist->is_recursive() != 0)
        {
            static_cast<IVP_Complex_Mindist*>(Mindist)->RecalcWinnerLedgePair(this); // :0634eba3 PUSH EBX (= this)
            return;
        }
    }
    else if ((Mindist->StateFlags & 0xc00u) != 0)
    {
        return;
    }
    IVP_AddMindistToPhantoms(Mindist);
}

// ---------------------------------------------------------------------------
// IVP_Mindist_Manager::insert_invalid_mindist -- FUN_0634e960
// (.text:0634e960-0634e9cd, RET 4), cmn-phy5 2026-10-08.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::insert_invalid_mindist(IVP_Ledge_Mindist* Mindist)
{
    // :0634e964-:0634e973 -- clear bits 18/20/21, set bit 19.
    Mindist->StateFlags = (Mindist->StateFlags & 0xffcbffffu) | 0x80000u;

    // :0634e976-:0634e98a -- push onto the invalid list (+0x14).
    Mindist->ManagerNext = InvalidMindistHead;
    Mindist->ManagerPrev = nullptr;
    if (InvalidMindistHead != nullptr)
        InvalidMindistHead->ManagerPrev = Mindist;
    InvalidMindistHead = Mindist;

    // :0634e98d-:0634e9ca -- both synapses onto their objects' +0x24 lists.
    IVP_PushSynapseNode(IVP_SynapseSlotA(Mindist), &Mindist->synapse[0].l_obj->invalid_synapse_list_head);
    IVP_PushSynapseNode(IVP_SynapseSlotB(Mindist), &Mindist->synapse[1].l_obj->invalid_synapse_list_head);
}

// ---------------------------------------------------------------------------
// FUN_0634e8a0 (.text:0634e8a0-0634e954, RET 4), cmn-phy5 2026-10-08. The
// linking half of insert_exact_mindist (0634e9f0) without its narrow-phase
// pass and event scheduling: state bits, exact list (+0x08), both synapses
// onto their objects' +0x20 lists, and the ActiveMindists vector when either
// core has a +0x10. Callers: 0634549d, insert_lazy_mindist (0634eb38) and the
// recursive mindist (06362d7d).
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::insert_exact_mindist_no_recalc(IVP_Ledge_Mindist* Mindist)
{
    // :0634e8a5-:0634e8b2 -- clear bits 20/21, set bits 18/19.
    Mindist->StateFlags = (Mindist->StateFlags & 0xffcfffffu) | 0xc0000u;

    // :0634e8b5-:0634e8cc -- push onto the exact list (+0x08); the head is
    // written before the synapses here (0634e9f0 writes it after them).
    Mindist->ManagerNext = ActiveMindistHead;
    Mindist->ManagerPrev = nullptr;
    if (ActiveMindistHead != nullptr)
        ActiveMindistHead->ManagerPrev = Mindist;
    ActiveMindistHead = Mindist;

    // :0634e8cf-:0634e90c -- both synapses onto their objects' +0x20 lists.
    IVP_PushSynapseNode(IVP_SynapseSlotA(Mindist), &Mindist->synapse[0].l_obj->synapse_list_head);
    IVP_PushSynapseNode(IVP_SynapseSlotB(Mindist), &Mindist->synapse[1].l_obj->synapse_list_head);

    // :0634e90f-:0634e94e -- `TEST EDI; JNZ add` on core A's +0x10, else the
    // same test on core B's; add = IVP_U_Vector append on +0x0c.
    if (Mindist->synapse[0].l_obj->core->AxisLock != nullptr || Mindist->synapse[1].l_obj->core->AxisLock != nullptr)
        ActiveMindists.Add(Mindist);
}

// ---------------------------------------------------------------------------
// FUN_0634ffa0 (.text:0634ffa0-06350013, RET 4), cmn-phy5 2026-10-08. The
// inverse of insert_invalid_mindist (0634e960): unlink the mindist from the
// invalid list (+0x14) and both synapses from their objects' +0x24 lists.
// Callers: 06345495 and 0634dddd.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::remove_invalid_mindist(IVP_Ledge_Mindist* Mindist)
{
    // :0634ffa4-:0634ffc5
    if (Mindist->ManagerNext != nullptr)
        Mindist->ManagerNext->ManagerPrev = Mindist->ManagerPrev;
    if (Mindist->ManagerPrev != nullptr)
        Mindist->ManagerPrev->ManagerNext = Mindist->ManagerNext;
    else
        InvalidMindistHead = Mindist->ManagerNext;

    // :0634ffc8-:0634ffea (synapse A, object at +0x28) and :0634ffed-:06350010
    // (synapse B, object at +0x44): the head a prev-less node is unlinked
    // from is that object's +0x24.
    for (int32_t I = 0; I < 2; ++I)
    {
        IVP_Synapse_Real* const Slot = &Mindist->synapse[I];
        if (Slot->next != nullptr)
            Slot->next->prev = Slot->prev;
        if (Slot->prev != nullptr)
            Slot->prev->next = Slot->next;
        else
            Slot->l_obj->invalid_synapse_list_head = Slot->next;
    }
}

// ---------------------------------------------------------------------------
// FUN_06350020 (.text:06350020-06350044, RET 4), cmn-phy5 2026-10-08 --
// remove_hull_mindist. :06350025-:0635002f `PUSH [ESI+0x1c]; ECX =
// [ESI+0x28]+0x64; CALL FUN_06363280`, then :06350034-:0635003e the same with
// [ESI+0x38] / [ESI+0x44]+0x64. Object+0x64 is hull_manager (+0x44) + 0x20,
// the pool PoolRemove forwards to.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::remove_hull_mindist(IVP_Ledge_Mindist* Mindist)
{
    Mindist->synapse[0].l_obj->hull_manager.PoolRemove(static_cast<uint16_t>(Mindist->synapse[0].HullPoolSlot));
    Mindist->synapse[1].l_obj->hull_manager.PoolRemove(static_cast<uint16_t>(Mindist->synapse[1].HullPoolSlot));
}

// ---------------------------------------------------------------------------
// FUN_06350460 (.text:06350460-0635053b, RET 4), cmn-phy5 2026-10-08 -- the
// library's recalc_exact_mindist. EBX = this manager, ESI = the mindist.
//   :0635046b FUN_06352cf0(M) = DispatchNarrowPhaseSolve.
//   :06350470-:0635047b CH = flags bits 8..15:
//     !(flags & 0x3000): (flags & 0xc000) -> M slot 7 (exact_mindist_went_invalid)
//       with this manager (:06350481-:06350486); else nothing. Return.
//   :0635048f (flags & 0x3000) set:
//     (flags & 0xc000) -> value = CombinedRadius (+0x50, :063504c3);
//     else SeparationGap(+0x54) vs 0.0f, `TEST AH,0x41; JNZ` -> <= / unordered:
//       value = -SeparationGap (:063504bc-:063504bf); strictly greater:
//       (flags & 0xc00) -> 06350350(M); return.
//   :063504c6-:063504e1 phantom: obj0->+0x1c; if set and obj1->+0x1c also set
//     -> drop value, return (:06350536). Else the one that is set (when obj0's
//     is null, obj1's is read without a test, :063504de).
//   :063504e1-:063504eb slack = float(value + phantom->+4); EDI = phantom->+0x40.
//   :063504ef-:06350533 M->is_recursive() (slot 6) and EDI == 1 -> M slot 7
//     with this manager; return.
//   :063504f6-:0635051a (flags & 0xc00) -> return; else 06350310(M),
//     DetachMindist(M) (0634fec0), 06350250(M, slack).
// Callers: 063454cb, recalc_all_exact_wheel_mindist 0634f259, recalc_all_exact_mindists 0634f311.
// Phantom fields are the typed IVP_Controller_Phantom members (+0x4
// exit_policy_extra_radius, +0x40 client_data compared as an int); cmn-phy8
// 2026-10-08 re-trace.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::recalc_exact_mindist(IVP_Ledge_Mindist* Mindist)
{
    Mindist->DispatchNarrowPhaseSolve();

    const uint32_t Flags = Mindist->StateFlags; // ECX, read once at :06350470
    if ((Flags & 0x3000u) == 0)
    {
        if ((Flags & 0xc000u) != 0)
            Mindist->exact_mindist_went_invalid(this);
        return;
    }

    float Value;
    if ((Flags & 0xc000u) != 0)
    {
        Value = Mindist->CombinedRadius;
    }
    else if (Mindist->SeparationGap > 0.0f)
    {
        if ((Flags & 0xc00u) != 0)
            IVP_RemoveMindistFromPhantoms(Mindist);
        return;
    }
    else
    {
        Value = -Mindist->SeparationGap;
    }

    const IVP_Controller_Phantom* const Phantom0 = static_cast<const IVP_Controller_Phantom*>(Mindist->synapse[0].l_obj->first_child);
    const IVP_Controller_Phantom* Phantom = Phantom0;
    if (Phantom0 != nullptr)
    {
        if (Mindist->synapse[1].l_obj->first_child != nullptr)
            return; // :063504d8 -> :06350536
    }
    else
    {
        Phantom = static_cast<const IVP_Controller_Phantom*>(Mindist->synapse[1].l_obj->first_child); // :063504de, no null test
    }

    const float Slack = (float)((double)(Value) + Phantom->exit_policy_extra_radius); // :063504e1 FADD, :063504eb FSTP dword
    const int32_t PhantomFlag = static_cast<int32_t>(reinterpret_cast<intptr_t>(Phantom->client_data)); // :063504e6

    if (Mindist->is_recursive() != 0 && PhantomFlag == 1)
    {
        Mindist->exact_mindist_went_invalid(this);
        return;
    }

    if ((Mindist->StateFlags & 0xc00u) != 0) // re-read at :063504f6
        return;
    IVP_AddMindistToPhantoms(Mindist);
    DetachMindist(Mindist);
    IVP_InsertHullMindist(Mindist, Slack);
}

// ---------------------------------------------------------------------------
// FUN_0634f300 (.text:0634f300-0634f31e), cmn-phy5 2026-10-08 -- the
// library's recalc_all_exact_mindists: walk the exact list (+0x08), reading
// next (+0x74) before recalc_exact_mindist (06350460) may unlink the node.
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::recalc_all_exact_mindists()
{
    IVP_Ledge_Mindist* Mindist = ActiveMindistHead;      // :0634f303
    while (Mindist != nullptr)
    {
        IVP_Ledge_Mindist* const Next = Mindist->ManagerNext; // :0634f30b
        recalc_exact_mindist(Mindist);                      // :0634f311
        Mindist = Next;
    }
}

namespace
{
    // FUN_0634fd10 (.text:0634fd10-0634fe54, thiscall on the mindist, RET 8:
    // object, gap), Ipion IVP_Mindist::create_cp_in_advance_pretension.
    // cmn-ivp3d 2026-10-08, traced in full:
    //   0634fd15-0634fd1e: Node = Object->mindist_list (+0x28); null -> return
    //     (no arena bracket on that path).
    //   0634fd24-0634fd45: ++Object->environment->vector_cores_frozen->State;
    //     Cp = 06356bc0(this, &Fs, &HavingNew, nullptr, 1).
    //   0634fd47-0634fd4c: HavingNew != 1 -> tail.
    //   0634fd52-0634fd7a: Old = Node + (int16)[Node+0xc] (the object's first
    //     contact point, read before 06356bc0 ran); its +0x68/+0x6c saved and
    //     set to Object->environment->current_time; 0635a1e0(Old).
    //   0634fd7f-0634fda7: Cp->NowFrictionPressure = Old's, Old's := 0;
    //     Cp->+0x68/+0x6c = the saved pair; Cp->+0x58 = Gap; Old's +0x68/+0x6c
    //     restored.
    //   0634fd99-0634fdf8: world span from Old's SpanFrictionS (s0, s1) and
    //     Old->Owner's tangent axes A (+0x78) / B (+0x88):
    //       X = float(s1*B.x + float(s0*A.x)), Y = float(s1*B.y + float(s0*A.y)),
    //       Z = s0*A.z + s1*B.z (unrounded).
    //   0634fdf1-0634fe39: onto Cp->Owner's axes A' / B':
    //       Cp->SpanFrictionS[0] = float((Z*A'.z + Y*A'.y) + X*A'.x),
    //       Cp->SpanFrictionS[1] = float((Z*B'.z + Y*B'.y) + X*B'.x).
    //   0634fe3c-0634fe49: --State and reset_chunks (06344070) on the arena
    //     re-read through Object->environment.
    void IVP_Create_Cp_In_Advance_Pretension(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* Object, float Gap)
    {
        char* const Node = static_cast<char*>(Object->mindist_list);
        if (Node == nullptr)
        {
            return;
        }

        ++Object->environment->vector_cores_frozen->State;
        IVP_ListManager* FrictionSystem = nullptr;
        int32_t HavingNew = 0;
        IVP_Contact_Point* const Cp =
            IVP_Try_To_Generate_Managed_Friction(Mindist, &FrictionSystem, &HavingNew, nullptr, 1);
        if (HavingNew == 1)
        {
            IVP_Contact_Point* const Old = reinterpret_cast<IVP_Contact_Point*>(
                Node + *reinterpret_cast<const int16_t*>(Node + 0xc));
            const float SavedTick68 = Old->_envTick68;
            const float SavedTick6c = Old->_envTick6c;
            IVP_Environment* const Env = Object->environment;
            std::memcpy(&Old->_envTick68, &Env->current_time.time_base, sizeof(float));   // raw dword copy
            std::memcpy(&Old->_envTick6c, &Env->current_time.time_offset, sizeof(float));
            IVP_UpdatePairRecordContactFrame(Old); // FUN_0635a1e0

            Cp->NowFrictionPressure = Old->NowFrictionPressure;
            Old->NowFrictionPressure = 0.0f;
            Cp->_envTick68 = SavedTick68;
            Cp->GapDistance = Gap;
            Cp->_envTick6c = SavedTick6c;

            const IVP_Impact_Solver_Long_Term* const OldOwner = Old->Owner;
            const double S0 = (double)(Old->SpanFrictionS[0]);
            const double S1 = (double)(Old->SpanFrictionS[1]);
            Old->_envTick68 = SavedTick68;
            Old->_envTick6c = SavedTick6c;

            const float X0 = (float)(S0 * OldOwner->TangentAxis0.x);
            const float Y0 = (float)(S0 * OldOwner->TangentAxis0.y);
            const double Z = S0 * OldOwner->TangentAxis0.z + S1 * OldOwner->TangentAxis1.z;
            const float Y = (float)(S1 * OldOwner->TangentAxis1.y + (double)(Y0));
            const float X = (float)(S1 * OldOwner->TangentAxis1.x + (double)(X0));

            const IVP_Impact_Solver_Long_Term* const NewOwner = Cp->Owner;
            const double Span1 = (Z * NewOwner->TangentAxis1.z + (double)(Y) * NewOwner->TangentAxis1.y) +
                                 (double)(X) * NewOwner->TangentAxis1.x;
            const double Span0 = (Z * NewOwner->TangentAxis0.z + (double)(Y) * NewOwner->TangentAxis0.y) +
                                 (double)(X) * NewOwner->TangentAxis0.x;
            Cp->SpanFrictionS[0] = (float)(Span0);
            Cp->SpanFrictionS[1] = (float)(Span1);
        }

        IVP_U_Memory* const Arena = Object->environment->vector_cores_frozen;
        --Arena->State;
        Arena->reset_chunks(); // FUN_06344070
    }
}

// ---------------------------------------------------------------------------
// FUN_0634f230 (.text:0634f230-0634f2fa), cmn-phy5 2026-10-08 -- the
// library's recalc_all_exact_wheel_mindist. For i = n_elems-1 down to 0 over
// ActiveMindists (+0x0c; n_elems +0x0e, elems +0x10, the element re-read by
// index each round):
//   :0634f259 recalc_exact_mindist(M);
//   :0634f25e-:0634f29b EDI = sorted synapse 0, EBP = sorted synapse 1; unless
//     EDI->status == 3 the two are swapped (`CMP [EDI+0x1a],3; JZ`), so EDI is
//     the ball side when there is one;
//   skip unless EBP->edge != M->+0x7c (:0634f2a3), EBP->status == 2 (:0634f2aa),
//     EDI->status == 3 (:0634f2b0) and SeparationGap < DAT_0658ab2c
//     (G_IVP_MindistSettings.max_distance_for_friction; `FCOMP; TEST AH,1; JZ` skips on
//     >= -- unordered proceeds) and !(EBP's ledge +0x08 & 3) (:0634f2c8-:0634f2d1);
//   then FUN_0634fd10(M, EDI->l_obj, gap) and M->+0x7c = EBP->edge.
// FUN_0634fd10 is IVP_Create_Cp_In_Advance_Pretension above (cmn-ivp3d).
// ---------------------------------------------------------------------------
void IVP_Mindist_Manager::recalc_all_exact_wheel_mindist()
{
    for (int32_t Index = static_cast<int32_t>(ActiveMindists.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Ledge_Mindist* const Mindist = ActiveMindists.elems[Index];
        recalc_exact_mindist(Mindist);

        IVP_Synapse_Real* Ball = Mindist->get_sorted_synapse(0);  // EDI
        IVP_Synapse_Real* Other = Mindist->get_sorted_synapse(1); // EBP
        if (Ball->status != IVP_SYNAPSE_BALL)
        {
            IVP_Synapse_Real* const Tmp = Ball;
            Ball = Other;
            Other = Tmp;
        }

        if (static_cast<int32_t>(reinterpret_cast<uintptr_t>(Other->edge)) == Mindist->Field_0x7c)
            continue;
        if (Other->status != IVP_SYNAPSE_POLY)
            continue;
        if (Ball->status != IVP_SYNAPSE_BALL)
            continue;
        const float Gap = Mindist->SeparationGap;
        if (Gap >= G_IVP_MindistSettings.max_distance_for_friction)
            continue;
        if ((Other->get_ledge()->SizeAndFlags & 3) != 0)
            continue;

        IVP_Create_Cp_In_Advance_Pretension(Mindist, Ball->l_obj, Gap); // FUN_0634fd10
        Mindist->Field_0x7c = static_cast<int32_t>(reinterpret_cast<uintptr_t>(Other->edge));
    }
}

// ---------------------------------------------------------------------------
// FUN_0634f320 (decomp-tracker item 3117) -- IVP_RescheduleCollisionEvent
// ---------------------------------------------------------------------------
// Retail is thiscall with `RET 0x8`, i.e. two stack arguments that Ghidra only
// half-names. Both of UpdatePerTick's call sites pass RearmWhenFar = 0
// (:0634fea2 / :0634feb1 push 0 last, so it is the FIRST argument) with
// EventMode = 2 and 1 respectively; the other four call sites (:06344f98,
// :0634eadb, :0634eb43, :0634f21f) are outside this pass.
//
// Careful when re-reading the listing: retail reuses the first argument's own
// stack slot as float scratch from :0634f4a3 onward, so RearmWhenFar is only
// live at the :0634f3ee test, and it later reuses the SECOND argument's slot
// (:0634f69b / :0634f811) to cache Env->delta_time -- EventMode is read
// exactly once, at :0634f621/:0634f63e.
// FUN_0634f210 (.text:0634f210-0634f22b): ECX = +0x08; `TEST ECX,ECX; JZ`
// done; loop { ESI = [ECX+0x74]; PUSH 1; PUSH 1; CALL FUN_0634f320 (ECX =
// mindist); ECX = ESI } while (ESI).
void IVP_Mindist_Manager::recalc_all_exact_mindists_events()
{
    IVP_Ledge_Mindist* Mindist = ActiveMindistHead;          // :0634f210
    while (Mindist != nullptr)                               // :0634f213 / :0634f224-:0634f228
    {
        IVP_Ledge_Mindist* Next = Mindist->ManagerNext;      // :0634f218
        IVP_RescheduleCollisionEvent(Mindist, 1, 1);         // :0634f21b-:0634f21f
        Mindist = Next;                                      // :0634f226
    }
}

void IVP_RescheduleCollisionEvent(IVP_Ledge_Mindist* This, int32_t RearmWhenFar, int32_t EventMode)
{
    // 1. :0634f328-:0634f358. `XOR EAX,0x100` flips bit 8 only, so the two
    //    indices are always i and i^1 -- the pair of embedded synapse slots,
    //    addressed as this + 0x28 + index*0x1c.
    const uint32_t FlagsAtEntry = This->StateFlags;
    const int32_t SideI = static_cast<int32_t>((FlagsAtEntry >> 8) & 3u);
    const int32_t SideJ = static_cast<int32_t>(((FlagsAtEntry ^ 0x100u) >> 8) & 3u);

    // (The port-only null-object / null-core / null-environment early returns
    // that stood here were removed 2026-10-08, cmn-phy5: retail has none.)
    IVP_Real_Object* const ObjectI = This->synapse[SideI].l_obj;
    IVP_Real_Object* const ObjectJ = This->synapse[SideJ].l_obj;

    const IVP_Core* const CoreI = ObjectI->core; // retail EDI
    const IVP_Core* const CoreJ = ObjectJ->core; // retail EBX
    IVP_Environment* const Env = This->synapse[0].l_obj->environment; // retail EBP -- slot 0, NOT SideI

    // 2. :0634f36c-:0634f398, each sum on the x87 stack and spilled once
    //    (FSTP dword). DAT_063a561c == 2e-10f (.rdata ff e6 5b 2f).
    const float RadiusSum = (float)((double)(CoreI->BoundingRadius) + CoreJ->BoundingRadius);
    const float MarginSpan = (float)(
        (((double)(CoreJ->Margin) + CoreI->Margin) + RadiusSum) + 2.0e-10f);

    // 3. :0634f39c-:0634f3a7. De-queue whatever slot this pair currently holds.
    //    `MOV ECX,[EBP+0x4]` at :0634f39e makes the receiver env+0x04 (the
    //    time manager); FUN_063609f0 removes the pool entry named by the
    //    event's own +0x04 handle. No null test in retail.
    if (This->EventQueueHandle != 0xffff)
    {
        Env->sim_unit_manager->CancelEvent(This->EventQueueHandle); // FUN_063609f0
        This->EventQueueHandle = 0xffff;
    }

    // 4. :0634f3ae-:0634f3ec. COARSE GATE. FLD gap first (ST1), then
    //    threshold = ((dt * MarginSpan) * 2.1f) + ramp[idx] on the stack (ST0);
    //    FCOMPP, `TEST AH,0x1; JZ :0634f421` -- C0 (threshold < gap, or
    //    unordered) falls through to the FAR case, everything else goes on.
    //    DAT_063a5618 == 2.1f (.rdata 66 66 06 40). The ramp value is copied
    //    to a stack float (local_44) and reused at :0634f406.
    const uint32_t FlagsAfterDequeue = This->StateFlags; // retail re-reads at :0634f3b1
    const uint32_t ToleranceIndex = (FlagsAfterDequeue >> 22) & 0xffu;
    const float ToleranceValue = IVP_ToleranceRamp(ToleranceIndex);
    const double CoarseThreshold = (((double)(Env->delta_time) * MarginSpan) * 2.1f) + ToleranceValue;
    const float Gap = This->SeparationGap;

    if (!(CoarseThreshold >= (double)(Gap)))
    {
        // 5. :0634f3ee-:0634f41e. Too far to schedule anything this step; if
        //    the caller asked for it, re-arm the hull-distance watchdog so the
        //    pair wakes up once the objects have closed by that much.
        if (RearmWhenFar == 0)
            return;
        // :0634f3fa `MOV ECX,[EBP+0x10]` -- the MINDIST manager this time.
        Env->mindist_manager->DetachMindist(This); // FUN_0634fec0
        // :0634f403-:0634f40b FLD gap; FSUB local_44; FSTP dword (the argument).
        IVP_DistributeHullSlack(This, (float)((double)(This->SeparationGap) - ToleranceValue));
        return;
    }

    // 6. :0634f421-:0634f4d9. CLOSING-SPEED BOUND, in retail's operand order.
    //    DAT_063a5614 == 1.001f (.rdata c5 20 80 3f).
    //    RelativeSpeed = dot(N, J.Speed) - dot(N, I.Speed), FSUBP, spilled once
    //    (local_30). SpinI stays on the stack; SpinJ is spilled to the first
    //    argument's slot (:0634f4a3) and reloaded as a float.
    const Vector& N = This->SeparatingNormal;
    const double DotSpeedJ = ((double)(N.z) * CoreJ->Speed.z + (double)(N.y) * CoreJ->Speed.y) +
                             (double)(CoreJ->Speed.x) * N.x;
    const double DotSpeedI = ((double)(N.z) * CoreI->Speed.z + (double)(N.y) * CoreI->Speed.y) +
                             (double)(N.x) * CoreI->Speed.x;
    const float RelativeSpeed = (float)(DotSpeedJ - DotSpeedI); // local_30

    const double SpinI = ((double)(CoreI->RotAxisOS.z) * N.z + (double)(CoreI->RotAxisOS.y) * N.y) +
                         (double)(CoreI->RotAxisOS.x) * N.x;
    const float SpinJ = (float)(
        ((double)(CoreJ->RotAxisOS.z) * N.z + (double)(CoreJ->RotAxisOS.y) * N.y) +
        (double)(CoreJ->RotAxisOS.x) * N.x); // :0634f4a3 FSTP dword
    const double RotTermI = sqrt(1.001f - SpinI * SpinI) * CoreI->BoundingRadius;                                     // :0634f4a7-:0634f4b3
    const double RotTermJ = sqrt(1.001f - (double)(SpinJ) * SpinJ) * CoreJ->BoundingRadius;               // :0634f4b9-:0634f4cd
    const float ClosingBound = (float)((RotTermI + RotTermJ) + RelativeSpeed); // :0634f4d3-:0634f4d9, local_2c

    // 7. :0634f4dd-:0634f551. HORIZON TEST. The four environment time words
    //    are dword copies into the context (+0x18..+0x24). DAT_063a53d8 == 1e-10f.
    const float NowHi = Env->current_time.time_base;
    const float NowLo = Env->current_time.time_offset;
    const float NextHi = Env->next_psi_time_base;
    const float NextLo = Env->next_psi_time_offset;

    // :0634f4e3-:0634f52c: `FCOMP MinRate; TEST AH,1; JZ` -- not-less goes on;
    // less or unordered re-tests against 1e-10f and returns on C0 again.
    if (!(ClosingBound >= IVP_ToleranceMinRate()) && !(ClosingBound >= 1.0e-10f))
        return; // nothing is approaching fast enough to matter

    // :0634f532-:0634f551: horizon kept on the stack, `FCOMP gap; TEST AH,0x41;
    // JNZ` returns on <= or unordered; only horizon > gap goes on.
    const double Horizon = (((((double)(NextHi) - NowHi) + NextLo) - NowLo) * ClosingBound) + ToleranceValue;
    if (!(Horizon > (double)(Gap)))
        return;

    // 8. :0634f557-:0634f5a6.
    if ((FlagsAfterDequeue & 0x3000u) == 0x1000u)
        return;

    // `TEST ECX,0x3fc00000` clears CF, so the `JBE` at :0634f579 is a plain JZ.
    if ((FlagsAfterDequeue & 0x3fc00000u) != 0)
    {
        int32_t& Counter = IVP_ToleranceRecomputeCounter(); // DAT_0658ab44
        const int32_t Previous = Counter;
        Counter = Previous + 1;
        if (Previous > 2)
        {
            // :0634f58d-:0634f5a6 in-place DECREMENT of the 8-bit tolerance
            // index in bits 22-29 (fresh read of +0x14). Wraps at zero,
            // exactly as retail does.
            const uint32_t S = This->StateFlags;
            uint32_t Updated = (S & 0xffc00000u) - 1u;
            Updated ^= S;
            Updated &= 0x3fc00000u;
            Updated ^= S;
            This->StateFlags = Updated;
            Counter = 0;
        }
    }

    // 9. :0634f5b0-:0634f5ea. THE DISPATCH. Retail re-reads StateFlags here
    //    (step 8 may just have changed it) and forms the index as
    //    ShapeType[j] + ShapeType[i]*4 -- j first, from the XORed selector.
    const uint32_t FlagsAtDispatch = This->StateFlags;
    const int32_t DispatchJ = static_cast<int32_t>(((FlagsAtDispatch ^ 0x100u) >> 8) & 3u);
    const int32_t DispatchI = static_cast<int32_t>((FlagsAtDispatch >> 8) & 3u);
    const int32_t ShapeJ = This->synapse[DispatchJ].status;
    const int32_t ShapeI = This->synapse[DispatchI].status;

    // The context on retail's frame at ESP+0x20 (stores :0634f37e / :0634f398 /
    // :0634f461 / :0634f4d9 / :0634f4e7-:0634f50b / :0634f571-:0634f575).
    IVP_Sweep_Estimator_Context Ctx;
    Ctx.RadiusSum = RadiusSum;
    Ctx.RelativeSpeed = RelativeSpeed;
    Ctx.ClosingBound = ClosingBound;
    Ctx.MarginSpan = MarginSpan;
    Ctx.Mindist = This;
    Ctx.Env = Env;
    Ctx.NowBase = NowHi;
    Ctx.NowOffset = NowLo;
    Ctx.NextBase = NextHi;
    Ctx.NextOffset = NextLo;
    // +0x28..+0x30 are left uninitialised by retail (every handler zeroes
    // OutCode itself first); zeroed here only so the stack frame is
    // deterministic -- no behavioural difference.
    Ctx.OutCode = 0;
    Ctx.OutEventBase = 0.0f;
    Ctx.OutEventOffset = 0.0f;

    IVP_InvokeSweepEstimator(ShapeJ + ShapeI * 4, &Ctx); // :0634f5e5-:0634f5ea

    if (Ctx.OutCode == 0) // :0634f5f8-:0634f5fa
        return;

    // 10. :0634f600-:0634f933. RESULT POST-PROCESSING. DAT_063a5610 == 1e-6f
    //     (.rdata bd 37 86 35).
    float EventHi = Ctx.OutEventBase;
    float EventLo = Ctx.OutEventOffset;

    // :0634f600-:0634f61b lead on the stack, `FCOMP 1e-6f; TEST AH,1; JZ
    // :0634f964` -- not-less enqueues as is; less or unordered goes on.
    const double RawLead = (((double)(Ctx.OutEventBase) - Ctx.NowBase) + Ctx.OutEventOffset) - Ctx.NowOffset;
    if (RawLead >= 1.0e-6f)
    {
        // Far enough ahead to use as-is; retail jumps straight to the enqueue.
    }
    else if (EventMode == 0)
    {
        // :0634f629-:0634f639 -- clamp to "now" (the context copies) and
        // enqueue, skipping both the epsilon nudge and the final horizon check.
        EventHi = Ctx.NowBase;
        EventLo = Ctx.NowOffset;
    }
    else
    {
        // :0634f641-:0634f64b. DAT_0658aa10 is the tolerance block's own
        // [0x00]; the result (FSTP dword) overwrites retail's first argument
        // slot, which holds the gap copied at :0634f50f.
        const float Slack = (float)((double)(Gap) - IVP_ToleranceBase());
        const float DeltaTime = Env->delta_time;
        const float EpsScale = IVP_ToleranceEpsScale(); // DAT_0658ab40, == 1.0f

        // :0634f64f/:0634f655 pick between two epsilon ladders. `TEST CL,0xf`
        // tests the low nibble of the estimator's own out-code.
        const bool CoarseLadder = (EventMode == 2) || ((Ctx.OutCode & 0x0f) != 0);

        // DAT_063a547c == 1e-4f, DAT_063a5604 == 1e-3f, DAT_063a5608 == 1e-5f,
        // DAT_063a560c == 1e-7f, DAT_063a5464 == 0.1f -- all read from the
        // listing's own .rdata byte dump.
        const float NudgeEpsilon = CoarseLadder
                                       ? (Slack >= 1.0e-6f ? 1.0e-4f : 1.0e-3f)
                                       : (Slack >= 1.0e-6f ? 1.0e-7f : 1.0e-5f);

        // All four epsilon arms re-read the environment's current time
        // (`MOV ECX,[EBP+0xf8]` at :0634f673/:0634f748/:0634f7eb/:0634f8b7)
        // rather than the copies cached in the context -- the estimator has
        // run in between and retail does not trust the cache here.
        // The base is loaded as a float and then lives on the x87 stack (ST1)
        // through both carry loops; the first offset is spilled to a float
        // (FST/FSTP [ESP+0x1c]) before the epsilon is added to it, the final
        // pair is spilled at the end (FST/FSTP [ESP+0x10] / [ESP+0x14]).
        double Base = Env->current_time.time_base;
        float Offset;
        if (Slack >= 1.0e-6f) // `FCOMP 1e-6f; TEST AH,1; JNZ` -- less or unordered skips the gap term
        {
            // :0634f673-:0634f6d5 (fine ladder: FLD slack, FMUL 0.1f, FDIV
            // MarginSpan, FADD now_offset) and :0634f7eb-:0634f847 (coarse
            // ladder: FLD slack, FDIV MarginSpan, FADD now_offset).
            double FirstOffset = (CoarseLadder ? ((double)(Slack) / Ctx.MarginSpan)
                                               : (((double)(Slack) * 0.1f) / Ctx.MarginSpan))
                                 + Env->current_time.time_offset;
            IVP_RenormalizeSplitTime(Base, FirstOffset);
            Offset = (float)(FirstOffset);
        }
        else
        {
            // :0634f748-:0634f776 and :0634f8b7-:0634f8e5 -- no gap term at
            // all, just the epsilon.
            Offset = Env->current_time.time_offset;
        }

        // FLD EpsScale; FMUL eps; FMUL dt; FADD offset (float).
        double SecondOffset = (((double)(EpsScale) * NudgeEpsilon) * DeltaTime) + Offset;
        IVP_RenormalizeSplitTime(Base, SecondOffset);
        EventHi = (float)(Base);
        EventLo = (float)(SecondOffset);

        // 11. :0634f933-:0634f962. All four epsilon arms converge here; the
        //     next-PSI pair is re-read from env+0x100 / +0x104. `FCOMP 0.0f;
        //     TEST AH,1; JZ` returns on not-less (>= 0); less or unordered
        //     enqueues.
        if (((((double)(EventHi) - Env->next_psi_time_base) + EventLo) - Env->next_psi_time_offset) >= 0.0)
            return;
    }

    // 12. :0634f964-:0634f99d. ENQUEUE, then fold the estimator's out-code into
    //     StateFlags' low byte (`ECX = S ^ ((S ^ OutCode) & 0xff)`).
    //     `MOV EBP,[EBP+0x4]` at :0634f964 re-points EBP at env+0x04; the key
    //     is ((EventHi - TimeBaseHi) + EventLo) - TimeBaseLo on the stack,
    //     spilled once (:0634f967-:0634f978, MakeEventKey), and the receiver of
    //     FUN_06362f90 is the POINTER at manager+0x08 (`MOV ECX,[EBP+0x8]`).
    This->EventQueueHandle = static_cast<int32_t>(
        Env->sim_unit_manager->EnqueueEvent(This, EventHi, EventLo));
    const uint32_t S = This->StateFlags;
    This->StateFlags = S ^ ((S ^ static_cast<uint32_t>(Ctx.OutCode)) & 0xffu);
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::UpdatePerTick
// ---------------------------------------------------------------------------
// FUN_0634fe60 (decomp-tracker item 3124, Common.dll.txt:426416-426456, 34
// insns). D227 pass (2026-09-10). Confirmed retail vtable slot 0 (XREF lists
// BOTH 063a55e0 and 063a5878 directly at offset 0, the base vtable's own
// first slot). Fully traced:
//   1. DispatchNarrowPhaseSolve() (FUN_06352cf0, already ported above).
//   2. If StateFlags' "kind" bits (0xc000, the bits DispatchNarrowPhaseSolve
//      sets / clears) are already set, return immediately --
//      nothing left to resolve this tick.
//   3. Else if StateFlags' low nibble (0xF) is nonzero, call
//      FUN_0634f320(this, 0, 1).
//   4. Else, index a per-shape-pair float table (DAT_0658aa18[(StateFlags >>
//      22) & 0xFF] + bias DAT_0658ab34) and compare it against SeparationGap
//      (+0x54, this class's own already-declared field): within tolerance ->
//      the virtual do_impact (slot 8, `CALL [EAX+0x20]` at :0634fe99); out of
//      tolerance -> FUN_0634f320(this, 0, 2) instead.
// Argument order confirmed from the listing: :0634fea0-:0634fea2 pushes 2 then
// 0, so the 0 is the FIRST argument (RearmWhenFar) and the 1/2 is the second
// (EventMode).
// Re-traced 2026-10-08 (cmn-phy8), :0634fe74-:0634fe93:
//     FLD  [ESI+0x54]                ; SeparationGap (ST1 after the next FLD)
//     FLD  [EAX*4 + DAT_0658aa18]    ; ramp[(flags >> 22) & 0xff]
//     FADD [DAT_0658ab34]            ; threshold, kept on the x87 stack
//     FCOMPP; FNSTSW; TEST AH,0x41; JNZ :0634fea0
// FCOMPP compares ST0 (threshold) with ST1 (gap); C0|C3 (threshold <= gap or
// unordered) jumps to the reschedule arm, the fall-through (threshold > gap)
// calls slot 8. The sum is never spilled, so it is held as double.
void IVP_Ledge_Mindist::UpdatePerTick()
{
    DispatchNarrowPhaseSolve(); // :0634fe63 FUN_06352cf0

    if ((StateFlags & 0xc000u) != 0) // :0634fe6b TEST AH,0xc0
        return;

    if ((StateFlags & 0x0Fu) != 0) // :0634fe70 TEST AL,0xf
    {
        IVP_RescheduleCollisionEvent(this, 0, 1); // :0634feaf-:0634feb5
        return;
    }

    const uint32_t Index = (StateFlags >> 22) & 0xffu; // :0634fe77-:0634fe7a
    const double Threshold = (double)(IVP_ToleranceRamp(Index)) + IVP_ToleranceBias();
    if (Threshold > (double)(SeparationGap))
        do_impact(); // :0634fe95-:0634fe99 virtual slot 8
    else
        IVP_RescheduleCollisionEvent(this, 0, 2); // :0634fea0-:0634fea6
}

// ---------------------------------------------------------------------------
// IVP_Complex_Mindist
// ---------------------------------------------------------------------------
// (The port-local IVP_Complex_Mindist::PriorityPool that stood here was
// removed 2026-10-08, cmn-phy5: retail's pool FUN_06362ef0/f70/f90/06363280
// is IVP_Priority_Pool inside each object's hull manager, ivp_real_object.h.)

// 2026-10-08 (cmn-phy5): retail is FUN_06362ab0(environment, delegator)
// (.text:06362ab0-06362b2a, RET 8): FUN_0634dd10(env, delegator) for the
// IVP_Mindist base, the +0x80 IVP_Collision_Delegator sub-object, an empty
// Children vector (+0x88/+0x8a/+0x8c = 0), SpawnedMindistCount (+0x90) = 0,
// WinnerFlag (+0x84) = -1.
IVP_Complex_Mindist::IVP_Complex_Mindist(IVP_Environment* Environment, IVP_Collision_Delegator* Delegator)
    : IVP_Ledge_Mindist(Environment, Delegator) // :06362ad4
    , IVP_Collision_Delegator()                 // :06362ad9
{
    Children.memsize = 0;      // :06362ae9
    Children.n_elems = 0;      // :06362af0
    Children.elems = nullptr;  // :06362af7
    SpawnedMindistCount = 0;   // :06362afd
    WinnerFlag = -1;           // :06362b13
}

// FUN_06362b50 (+ FUN_06362b30's scalar-deleting-destructor wrapper, which
// in this port is just C++'s own compiler-generated deleting destructor --
// see that item's tracker note). Retail's second vector clear after
// FUN_06362a30 (:06362b8d-:06362bca) is Children's own destructor here; the
// delegator sub-object's dtor (vptr := 063a5750, :06362bd5) is the
// IVP_Collision_Delegator base dtor. FUN_0634dd60's base teardown (env
// counters, manager unlink, ledge references, delegator notification) is
// ~IVP_Ledge_Mindist, run after this body.
IVP_Complex_Mindist::~IVP_Complex_Mindist()
{
    ClearChildren();
}

// FUN_06362840 (.text:06362840-06362915, RET 4), re-ported 2026-10-08
// (cmn-ivp2c). Caller: insert_lazy_mindist :0634eba6, which pushes the
// manager (EBX); retail never reads that argument.
//   :06362846-:0636285b +0x80 sub-object slot 3 (get_spawned_mindist_count)
//     > DAT_0658ab3c (JG) -> return.
//   :06362861-:063628c6 WinnerFlag from the two ledges (RecalcWinnerFlag).
//   :063628d0-:063628ea obj0->environment->range_manager slot 0
//     get_coll_range_intra_objects(obj0, obj1, &R0, &R1).
//   :063628ec-:06362907 StateFlags = (x & 0xffd3ffff) | 0x100000.
//   :0636290a SpawnOrDeferChild((float)(R0 + R1)) (FLD/FADD/FSTP float).
// No DetachMindist and no insert_hull_mindist here (unlike 06362650/06362920).
void IVP_Complex_Mindist::RecalcWinnerLedgePair(IVP_Mindist_Manager* /*Manager*/)
{
    // .text:06362855: CMP EAX,[DAT_0658ab3c] -- IVP_Mindist_Settings+0x12c, the copy
    // PhySys world init writes from PhySys::MAX_SPAWNED_MINDIST_COUNT (.text:0628c997-
    // 0628c99e); retail never reads the PhySys global here (2026-10-08, cmn-phy2).
    if (get_spawned_mindist_count() > G_IVP_MindistSettings.max_spawned_mindist_count) return;

    WinnerFlag = RecalcWinnerFlag(synapse[0].edge, synapse[1].edge);

    IVP_Real_Object* const Object0 = synapse[0].l_obj;
    float Range0;
    float Range1;
    Object0->environment->range_manager->get_coll_range_intra_objects(Object0, synapse[1].l_obj, &Range0, &Range1);
    StateFlags = (StateFlags & 0xffd3ffffu) | 0x100000u;
    SpawnOrDeferChild((float)((double)(Range0) + Range1));
}

// LAB_06362650 (.text:06362650-0636281a, plain RET; vtable 063a5878 slot at
// 063a5898), re-traced 2026-10-08 (cmn-ivp2c). Self-contained: its own two
// jump tables switchdataD_0636281c (on ShapeTypeA = synapse[0].status, +0x32)
// and switchdataD_0636282c (on ShapeTypeB = synapse[1].status, +0x4e).
//   :0636266a-:06362673 +0x80 slot 3 > DAT_0658ab3c (JG) -> hand-off.
//   "Hand-off" = :06362797 CALL FUN_06358cb0 (the base IVP_Ledge_Mindist::
//     do_impact, a direct non-virtual call) and RET, without the epilogue.
// Retail vtable slot 8 of 063a5878, i.e. this class's do_impact override,
// dispatched by UpdatePerTick :0634fe99 (cmn-phy8 2026-10-08; was the
// unreachable RecalcWinnerByShapeType).
//   A outside 0..3 (unsigned JA) -> null-store trap (:063627b1) -> epilogue.
//   A = 0 / 3 (:0636268d): B outside 0..3 -> trap; B = 0 / 3 -> hand-off;
//     B = 1 (:063626a1) FeatureB's own bit 31 (unmasked), B = 2 (:063626ac)
//     FeatureB's triangle bit 31 (masked): set -> WinnerFlag 1, clear ->
//     hand-off.
//   A = 1 (:063626cd): B = 0 / 3 (:06362773) FeatureA's triangle bit 31:
//     set -> 0, clear -> hand-off. B = 1 (:063626f2): own bits of B then A:
//     B clear & A set -> 0, both clear -> hand-off, B set & A clear -> 1,
//     both set -> FLD radius(ledgeA) FCOMP radius(ledgeB), TEST AH,0x41;
//     JNZ (A <= B or unordered) -> 1, fall-through (A > B) -> 0.
//     Any other B -> :063626e7 null-store trap (EDX = 0) -> epilogue.
//   A = 2 (:06362789): FeatureA's triangle bit 31: set -> 0, clear -> hand-off.
//   Epilogue LAB_063627bb: env->mindist_manager->DetachMindist(this)
//     (0634fec0), range_manager slot 0 (obj0, obj1, &R0, &R1),
//     FUN_06350250(this, (float)(R0 + R1)), StateFlags = (x & 0xffd3ffff) |
//     0x100000, FUN_06362c70((float)(R0 + R1)).
void IVP_Complex_Mindist::do_impact()
{
    if (get_spawned_mindist_count() > G_IVP_MindistSettings.max_spawned_mindist_count) // 0636266d: DAT_0658ab3c
    {
        IVP_Ledge_Mindist::do_impact();
        return;
    }

    // :06362679 `MOVSX EAX,word [ESI+0x32]` / :0636268d `[ESI+0x4e]`: the two
    // synapses' status words; the edges are +0x2c / +0x48.
    const int16_t ShapeTypeA = synapse[0].status;
    const int16_t ShapeTypeB = synapse[1].status;
    IVP_Compact_Edge* const FeatureA = synapse[0].edge;
    IVP_Compact_Edge* const FeatureB = synapse[1].edge;

    if (ShapeTypeA < 0 || ShapeTypeA > 3)
    {
        // :06362680 JA caseD_4 -> :063627b1 MOV dword ptr [0],0: retail's
        // release IVP_ASSERT, a deliberate store to address zero (the PE bytes
        // carry a literal 0 displacement, no relocation). Reproduced as retail.
        volatile int32_t* Trap = nullptr;
        *Trap = 0;
    }
    else if (ShapeTypeA == 2)
    {
        if (TriangleFlagBit31Set(FeatureA)) WinnerFlag = 0;
        else { IVP_Ledge_Mindist::do_impact(); return; }
    }
    else if (ShapeTypeA == 0 || ShapeTypeA == 3)
    {
        if (ShapeTypeB < 0 || ShapeTypeB > 3)
        {
            // :06362694 JA caseD_4 -> the same null-store trap.
            volatile int32_t* Trap = nullptr;
            *Trap = 0;
        }
        else if (ShapeTypeB == 0 || ShapeTypeB == 3)
        {
            // switchdataD_0636282c[0] / [3] = 06362797: straight to the
            // 06358cb0 hand-off, no feature test.
            IVP_Ledge_Mindist::do_impact();
            return;
        }
        else if (ShapeTypeB == 1)
        {
            if (FeatureB->IsPiercing()) WinnerFlag = 1;
            else { IVP_Ledge_Mindist::do_impact(); return; }
        }
        else // ShapeTypeB == 2
        {
            if (TriangleFlagBit31Set(FeatureB)) WinnerFlag = 1;
            else { IVP_Ledge_Mindist::do_impact(); return; }
        }
    }
    else // ShapeTypeA == 1
    {
        if (ShapeTypeB == 0 || ShapeTypeB == 3)
        {
            if (TriangleFlagBit31Set(FeatureA)) WinnerFlag = 0;
            else { IVP_Ledge_Mindist::do_impact(); return; }
        }
        else if (ShapeTypeB == 1)
        {
            if (!FeatureB->IsPiercing())
            {
                if (FeatureA->IsPiercing()) WinnerFlag = 0;
                else { IVP_Ledge_Mindist::do_impact(); return; }
            }
            else if (!FeatureA->IsPiercing())
            {
                WinnerFlag = 1;
            }
            else
            {
                const float RadiusA = LedgetreeNodeRadius(FeatureA->GetCompactLedge());
                const float RadiusB = LedgetreeNodeRadius(FeatureB->GetCompactLedge());
                WinnerFlag = (RadiusA > RadiusB) ? 0 : 1;
            }
        }
        else
        {
            // Any other ShapeTypeB (2 or out of range): :063626e7
            // MOV dword ptr [0],EDX (EDX = 0) -- the same null-store trap.
            volatile int32_t* Trap = nullptr;
            *Trap = 0;
        }
    }

    // LAB_063627bb shared epilogue. EDI = obj0->environment was loaded at
    // :06362667, before the switch.
    IVP_Real_Object* const Object0 = synapse[0].l_obj;
    IVP_Environment* const Env = Object0->environment;
    Env->mindist_manager->DetachMindist(this);                                   // :063627bb-:063627bf
    float Range0;
    float Range1;
    Env->range_manager->get_coll_range_intra_objects(Object0, synapse[1].l_obj, &Range0, &Range1); // :063627c4-:063627db
    IVP_InsertHullMindist(this, (float)((double)(Range0) + Range1)); // :063627dd-:063627ea
    StateFlags = (StateFlags & 0xffd3ffffu) | 0x100000u;                          // :063627f3-:0636280c
    SpawnOrDeferChild((float)((double)(Range0) + Range1));  // :063627ef-:0636280f
}

// LAB_06362920 (.text:06362920-06362a2b, retail vtable slot 7 of 063a5878,
// RET 4, stack argument = the mindist manager), cmn-phy5 / cmn-ivp2c
// 2026-10-08.
//   :06362926-:0636293c +0x80 slot 3 > DAT_0658ab3c (JLE = not over cap):
//     over -> Manager->DetachMindist(this) (0634fec0), Manager->
//     insert_invalid_mindist(this) (0634e960), return.
//   :0636295a-:063629c8 WinnerFlag from the two ledges, the same code as
//     06362840 (RecalcWinnerFlag).
//   :063629c8-:063629cd Manager->DetachMindist(this) (the ARGUMENT, not the
//     environment's manager).
//   :063629d2-:063629ec obj0->environment->range_manager slot 0 (obj0, obj1,
//     &R0, &R1).
//   :063629ee-:063629fb FUN_06350250(this, (float)(R0 + R1)).
//   :06362a00-:06362a20 StateFlags = (x & 0xffd3ffff) | 0x100000, then
//     FUN_06362c70((float)(R0 + R1)).
void IVP_Complex_Mindist::exact_mindist_went_invalid(IVP_Mindist_Manager* Manager)
{
    if (get_spawned_mindist_count() > G_IVP_MindistSettings.max_spawned_mindist_count) // 06362936: DAT_0658ab3c (JLE = not over cap)
    {
        Manager->DetachMindist(this);          // :06362945
        Manager->insert_invalid_mindist(this); // :0636294d
        return;
    }

    WinnerFlag = RecalcWinnerFlag(synapse[0].edge, synapse[1].edge);

    Manager->DetachMindist(this);                                                 // :063629cd
    IVP_Real_Object* const Object0 = synapse[0].l_obj;
    float Range0;
    float Range1;
    Object0->environment->range_manager->get_coll_range_intra_objects(Object0, synapse[1].l_obj, &Range0, &Range1); // :063629ec
    IVP_InsertHullMindist(this, (float)((double)(Range0) + Range1)); // :063629fb
    StateFlags = (StateFlags & 0xffd3ffffu) | 0x100000u;                          // :06362a04-:06362a1d
    SpawnOrDeferChild((float)((double)(Range0) + Range1));  // :06362a20
}

// FUN_06362c70 (.text:06362c70-06362d5a, thiscall, RET 4: float Radius),
// re-ported 2026-10-08 (cmn-ivp2c). w = WinnerFlag (+0x84).
//   :06362c78-:06362c99 +0x80 slot 3 (get_spawned_mindist_count) >
//     DAT_0658ab3c (JG) -> return. (obj0 / obj1 are read before the call.)
//   :06362ca5-:06362cb3 four zeroed locals: Single[2] (ESP+0x1c/0x20 after
//     PUSH EDI) and Root[2] (ESP+0x14/0x18).
//   :06362cb8-:06362ce5 Single[1-w] = ledge of synapse[1-w].edge
//     ([this - w*0x1c + 0x48]: &~0xf, minus ((tri & 0xfff) + 1) * 16).
//   :06362ce7-:06362d00 Root[w] = ledge of synapse[w].edge ([this + w*0x1c + 0x2c]).
//   :06362d08-:06362d0a OldCount = Children.n_elems (+0x8a, word).
//   :06362d11-:06362d38 FUN_0634e0b0(obj0, obj1, Radius, &Children, Single[0],
//     Single[1], Root[0], Root[1], this ? this+0x80 : 0).
//   :06362d3d-:06362d50 +0x80 slot 2 change_spawned_mindist_count(n_elems - OldCount).
// Retail does no range check on w; the port indexes exactly as retail does.
// The recalc family (06362650 / 06362840 / 06362920) writes w = 0 / 1 before
// calling; 06362d90 runs on the state they left (StateFlags 0x100000).
void IVP_Complex_Mindist::SpawnOrDeferChild(float Radius)
{
    IVP_Real_Object* const Object1 = synapse[1].l_obj; // :06362c78
    IVP_Real_Object* const Object0 = synapse[0].l_obj; // :06362c81
    if (get_spawned_mindist_count() > G_IVP_MindistSettings.max_spawned_mindist_count) return; // 06362c93: DAT_0658ab3c

    const int32_t W = WinnerFlag;
    const IVP_Compact_Ledge* Single[2] = { nullptr, nullptr };
    const IVP_Compact_Ledge* Root[2] = { nullptr, nullptr };
    Single[1 - W] = synapse[1 - W].edge->GetCompactLedge();
    Root[W] = synapse[W].edge->GetCompactLedge();

    const int32_t OldCount = static_cast<int32_t>(Children.n_elems);
    IVP_Create_Exact_Mindists(Object0, Object1, Radius, &Children,
                              Single[0], Single[1], Root[0], Root[1],
                              static_cast<IVP_Collision_Delegator*>(this));
    change_spawned_mindist_count(static_cast<int32_t>(Children.n_elems) - OldCount);
}

// FUN_06362d60 (.text:06362d60-06362d8f, plain RET), cmn-phy5 2026-10-08:
// drop back out of complex/recursive tracking. :06362d64 FUN_06362a30;
// :06362d69-:06362d6f the manager is ObjectA->environment->mindist_manager
// (+0x28 -> +0x18 -> +0x10); :06362d75 remove_hull_mindist(this) (06350020);
// :06362d7d insert_exact_mindist_no_recalc(this) (0634e8a0); :06362d82-:06362d8b
// clear bits 12/13 of +0x14.
void IVP_Complex_Mindist::DeactivateToSimple()
{
    ClearChildren();
    IVP_Mindist_Manager* const Manager = synapse[0].l_obj->environment->mindist_manager;
    Manager->remove_hull_mindist(this);
    Manager->insert_exact_mindist_no_recalc(this);
    StateFlags &= 0xffffcfffu;
}

// FUN_06362d90 (.text:06362d90-06362e9f, plain RET), rewritten per listing
// 2026-10-08 (cmn-phy5) -- the library's rec_hull_limit_exceeded_event,
// reached from IVP_Ledge_Mindist::hull_limit_exceeded_event (0634fa40) when
// (StateFlags & 0x3c0000) == 0x100000.
//   :06362d96 FUN_06352b90(this) = recalc_mindist() (result unused).
//   :06362d9b-:06362db5 if !(StateFlags & 0xc000) and SeparationGap(+0x54) >
//     DAT_0658ab1c (FCOMP; `TEST AH,0x41; JNZ` skips on <=/unordered):
//     DeactivateToSimple (06362d60) and return.
//   :06362dbf-:06362de2 env = synapse[0].l_obj->environment;
//     env->range_manager slot 0 get_coll_range_intra_objects(obj0, obj1,
//     &Range0, &Range1); then ++env->range_intra_exceeded (+0x78).
//   :06362de5-:06362df3 FUN_06362c70(Range0 + Range1) (SpawnOrDeferChild).
//   :06362df8-:06362e55 synapse[0]: pool Remove(slot) on obj0+0x64, then
//     key = (((now.base - hull.LastEditTimeHi) + now.offset - hull.LastEditTimeLo)
//            * OuterGradient + OuterValueAtLastEdit) + Range0, all on the x87
//     stack, Insert(&synapse[0], key) -> synapse[0].HullPoolSlot.
//   :06362e58-:06362e98 the same for synapse[1] on obj1 with Range1. `now` is
//     the environment time read once at :06362df8-:06362dfe.
void IVP_Complex_Mindist::RecalcAndMaybeDeactivate()
{
    recalc_mindist(); // :06362d96 FUN_06352b90

    if (((StateFlags & 0xc000u) == 0) && (SeparationGap > G_IVP_MindistSettings.friction_dist))
    {
        DeactivateToSimple();
        return;
    }

    IVP_Real_Object* const Object1 = synapse[1].l_obj; // EBX
    IVP_Real_Object* const Object0 = synapse[0].l_obj; // ESI
    IVP_Environment* const Env = Object0->environment;  // EDI
    float Range0;
    float Range1;
    Env->range_manager->get_coll_range_intra_objects(Object0, Object1, &Range0, &Range1);
    ++Env->range_intra_exceeded;

    SpawnOrDeferChild((float)((double)(Range0) + Range1)); // :06362de5-:06362df3

    const float NowBase = Env->current_time.time_base;
    const float NowOffset = Env->current_time.time_offset;
    for (int32_t I = 0; I < 2; ++I)
    {
        IVP_Synapse_Real* const Syn = &synapse[I];
        IVP_Hull_Manager& Hull = Syn->l_obj->hull_manager;
        Hull.PoolRemove(static_cast<uint16_t>(Syn->HullPoolSlot));
        const float Key = (float)(
            (((((double)(NowBase) - Hull.LastEditTimeHi) + NowOffset) - Hull.LastEditTimeLo)
             * Hull.OuterGradient + Hull.OuterValueAtLastEdit)
            + ((I == 0) ? Range0 : Range1));
        Syn->HullPoolSlot = Hull.PoolInsert(static_cast<IVP_Listener_Hull*>(Syn), Key);
    }
}

// FUN_06362a30 (.text:06362a30-06362aa6), cmn-phy5 2026-10-08. Delete every
// child from the top down (`CALL [EDX+0x10]` with 1 = deleting dtor, null
// entries skipped), report -n through the +0x80 sub-object's slot 2
// (:06362a6e, n = the count read on entry), then the generic IVP_U_Vector
// clear: free elems unless it equals this+0x90 (the field after the vector).
void IVP_Complex_Mindist::ClearChildren()
{
    const int32_t OldCount = static_cast<int32_t>(Children.n_elems); // :06362a38
    for (int32_t I = OldCount - 1; I >= 0; --I)                       // :06362a41-:06362a5d
    {
        IVP_Collision* Child = Children.elems[I];                      // :06362a48-:06362a4e (elems re-read)
        if (Child != nullptr)
            delete Child;                                              // :06362a55-:06362a59
    }
    change_spawned_mindist_count(-OldCount);                           // :06362a5f-:06362a6e
    Children.Clear();                                                  // :06362a71-:06362a9c
}

// LAB_06362c00 (.text:06362c00-06362c6b, RET 4): slot 0 of the +0x80
// delegator sub-object. The same O(1) swap-with-last removal as
// IVP_OV_Element::remove_oo_collision (FUN_0635c250), on Children.
void IVP_Complex_Mindist::collision_is_going_to_be_deleted_event(IVP_Collision* Collision)
{
    // :06362c06-:06362c1f -- trust IndexInElement0 only if it is in range and
    // really points back at this collision; otherwise use IndexInElement1.
    int32_t Index = Collision->IndexInElement0;
    if (Index < 0 || Index >= static_cast<int32_t>(Children.n_elems) || Children.elems[Index] != Collision)
        Index = Collision->IndexInElement1;

    --Children.n_elems;                                               // :06362c22
    const int32_t Last = static_cast<int32_t>(Children.n_elems);
    if (Last > Index)                                                 // :06362c31 `CMP EAX,EDX; JLE` (signed)
    {
        IVP_Collision* Moved = Children.elems[Last];                  // :06362c38
        Children.elems[Index] = Moved;                                // :06362c3b
        if (Moved->IndexInElement0 == Last)                           // :06362c44
            Moved->IndexInElement0 = Index;
        else
            Moved->IndexInElement1 = Index;                           // :06362c4e
    }
    if (Collision->IndexInElement0 == Index)                          // :06362c51
        Collision->IndexInElement0 = -1;
    else
        Collision->IndexInElement1 = -1;                              // :06362c62
}

// FUN_06362ec0 (.text:06362ec0-06362edd), slot 3 of the +0x80 sub-object.
// Retail asks the owning delegator TWICE when the first answer is positive.
int32_t IVP_Complex_Mindist::get_spawned_mindist_count()
{
    if (delegator->get_spawned_mindist_count() > 0)   // :06362ec3-:06362ecd (`JLE` = fall back)
        return delegator->get_spawned_mindist_count(); // :06362ecf-:06362ed4
    return SpawnedMindistCount;                        // :06362ed9 `[ESI+0x10]` = this+0x90
}

// LAB_06362ea0 (.text:06362ea0-06362eb5, RET 4), slot 2 of the +0x80 sub-object.
void IVP_Complex_Mindist::change_spawned_mindist_count(int32_t Change)
{
    SpawnedMindistCount += Change;                     // :06362ea0-:06362ea9
    delegator->change_spawned_mindist_count(Change);   // :06362eac-:06362eb2
}

// (IVP_SpawnLedgeMindist, the FUN_0634e0b0 lookalike, removed 2026-10-08,
// cmn-ivp2c: FUN_0634e0b0 is IVP_Create_Exact_Mindists, ivp_mindist_create.cpp.)

// ---------------------------------------------------------------------------
// IVP_Mindist_Base::IVP_Mindist_Base(Delegator)
// FUN_0634db70 (.text:0634db70-0634dbf9, thiscall, RET 4), cmn-soup 2026-10-08:
//   :0634db8d-0634dba0 IVP_Collision's inlined ctor: +0x08 = delegator,
//     vptr 063a55b8, +0x10 = +0x0c = -1.
//   :0634dba3-0634dbbd FUN_06391ef0(this+0x18, 0x1c, 2, LAB_0634dd00,
//     LAB_0634dc50): the eh vector constructor iterator over synapse[2]
//     (LAB_0634dd00 only stores vtable 063a55cc) -- the member construction.
//   :0634dbc2-0634dbdb +0x14 = (+0x14 & 0xcfc00000) | 0x0fc00000; vptr 063a55a4.
//   :0634dbde +0x5c = 0; :0634dbe5 +0x04 = 0xffff.
// Named gaps (owner rule on retail UB, 2026-10-08): retail's +0x14 store keeps
// bits 31/30 and 29..22 of uninitialised heap memory; the port starts from 0.
// Every other field of this layer (synapse fields, +0x50..+0x58, +0x60..+0x6f)
// is left unwritten by retail (heap garbage until init_mindist / the solver
// write them) and is zeroed here, port-only, to stay deterministic.
// ---------------------------------------------------------------------------
IVP_Mindist_Base::IVP_Mindist_Base(IVP_Collision_Delegator* Delegator)
    : IVP_Collision(Delegator)                                   // :0634db8d-:0634dba0
    , StateFlags(0)
    , CombinedRadius(0.0f)
    , SeparationGap(0.0f)
    , SecondaryNormalDot(0.0f)
    , CombinedHullSpread(0.0f)
    , SeparatingNormal{ 0.0f, 0.0f, 0.0f }
    , _pad6c{}
{
    for (int32_t I = 0; I < 2; ++I) // port-only (LAB_0634dd00 stores the vptr only)
    {
        synapse[I].HullPoolSlot = 0;
        synapse[I].next = synapse[I].prev = nullptr;
        synapse[I].l_obj = nullptr;
        synapse[I].edge = nullptr;
        synapse[I].mindist_offset = 0;
        synapse[I].status = 0;
    }
    StateFlags = (StateFlags & 0xcfc00000u) | 0x0fc00000u;       // :0634dbc2-:0634dbdb
    CombinedHullSpread = 0.0f;                                   // :0634dbde
    EventQueueHandle = 0xffff;                                   // :0634dbe5
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::IVP_Ledge_Mindist(Environment, Delegator)
// FUN_0634dd10 (.text:0634dd10-0634dd38, RET 8), cmn-phy5 / cmn-soup 2026-10-08:
//   FUN_0634db70(delegator); +0x70 = +0x7c = 0; vptr 063a55e0;
//   ++env->sum_of_mindists (+0x6c); ++env->mindists_generated (+0x70).
// Named gap: ManagerNext / ManagerPrev (+0x74 / +0x78) are not written by
// retail here (heap garbage until the mindist manager links them); the port
// zeroes them, port-only, to stay deterministic.
// ---------------------------------------------------------------------------
IVP_Ledge_Mindist::IVP_Ledge_Mindist(IVP_Environment* Environment, IVP_Collision_Delegator* Delegator)
    : IVP_Mindist_Base(Delegator)                                // :0634dd18
    , LastDispatchTick(0)                                        // :0634dd1f
    , ManagerNext(nullptr)
    , ManagerPrev(nullptr)
    , Field_0x7c(0)                                              // :0634dd22
{
    ++Environment->sum_of_mindists;                              // :0634dd2f
    ++Environment->mindists_generated;                           // :0634dd32
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::~IVP_Ledge_Mindist -- FUN_0634dd60 (.text:0634dd60-
// 0634de53), cmn-phy5 2026-10-08. (Reached through the deleting dtor
// FUN_0634dd40 and from IVP_Complex_Mindist's dtor 06362b50.)
//   :0634dd86-:0634dd92 env = synapse[0].l_obj->environment; --env+0x6c;
//     ++env+0x74; EDI = env->mindist_manager.
//   :0634dd95-:0634ddab if (StateFlags & 0xc00) FUN_06350350(this) (phantoms).
//   :0634ddae-:0634dddd switch on the signed 4-bit field at bits 18..21
//     (`SHL 0xa; SAR 0x1c`): 2 -> remove_invalid_mindist (0634ffa0),
//     3 -> DetachMindist (0634fec0), 4/5 -> remove_hull_mindist (06350020),
//     anything else -> nothing (`ADD -2; CMP 3; JA`).
//   :0634dde2-:0634de13 for synapse[0] then synapse[1]: its object's
//     surface manager slot 8 (`CALL [EBP+0x20]`, remove_reference_to_ledge)
//     with FUN_0634ded0(synapse) (get_ledge).
//   :0634de16-:0634de1c delegator(+0x08) slot 0 (collision_is_going_to_be_
//     deleted_event) with this.
//   :0634de1e-:0634de40 vptr 063a55a4, eh vector destructor iterator over the
//     two synapses (LAB_0634dc50), vptr 063a55b8 -- the C++ member and base
//     destructors after this body.
// (FUN_06350350's phantom call FUN_06344580 is the real
// IVP_Controller_Phantom::mindist_left_volume since cmn-ivp3d.)
// ---------------------------------------------------------------------------
IVP_Ledge_Mindist::~IVP_Ledge_Mindist()
{
    IVP_Environment* const Env = synapse[0].l_obj->environment;
    --Env->sum_of_mindists;
    ++Env->mindists_deleted;
    IVP_Mindist_Manager* const Manager = Env->mindist_manager;

    if ((StateFlags & 0xc00u) != 0)
        IVP_RemoveMindistFromPhantoms(this);

    switch (static_cast<int32_t>(StateFlags << 10) >> 28)
    {
    case 2:
        Manager->remove_invalid_mindist(this);
        break;
    case 3:
        Manager->DetachMindist(this);
        break;
    case 4:
    case 5:
        Manager->remove_hull_mindist(this);
        break;
    default:
        break;
    }

    for (int32_t I = 0; I < 2; ++I)
    {
        synapse[I].l_obj->surface_manager->remove_reference_to_ledge(
            const_cast<IVP_Compact_Ledge*>(synapse[I].get_ledge()));
    }

    delegator->collision_is_going_to_be_deleted_event(this);
}

// FUN_0634ded0 (.text:0634ded0-0634dee4). See the declaration.
const IVP_Compact_Ledge* IVP_Synapse_Real::get_ledge() const
{
    const IVP_Compact_Triangle* const Triangle = reinterpret_cast<const IVP_Compact_Triangle*>(
        reinterpret_cast<uintptr_t>(edge) & ~static_cast<uintptr_t>(0x0F));
    return Triangle->GetCompactLedge();
}

// ---------------------------------------------------------------------------
// IVP_Synapse_Real's IVP_Listener_Hull slots (vtable 063a55cc), cmn-phy5
// 2026-10-08. Each recovers the mindist as `this + (int16)[this+0x18]`.
// ---------------------------------------------------------------------------
// LAB_0634f9e0 (RET 8): `MOVSX ECX,[EAX+0x18]; ADD ECX,EAX; CALL FUN_0634fa40`
// with the second stack argument (the intrusion value); the hull manager
// argument is not read.
void IVP_Synapse_Real::hull_limit_exceeded_event(IVP_Hull_Manager* /*HullManager*/, float HullIntrusionValue)
{
    get_mindist()->hull_limit_exceeded_event(HullIntrusionValue);
}

// LAB_0634fa00 (RET 4): `MOVSX EAX,[ECX+0x18]; ADD EAX,ECX; JZ skip;
// CALL [vtbl+0x10] with 1` -- the mindist's scalar deleting destructor.
void IVP_Synapse_Real::hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* /*HullManager*/)
{
    IVP_Ledge_Mindist* const Mindist = get_mindist();
    if (Mindist != nullptr)
        delete Mindist;
}

// LAB_0634f9c0 (RET 8): FUN_0634fa20(mindist, DeltaTime, CenterDeltaTime).
void IVP_Synapse_Real::hull_manager_is_reset(float DeltaTime, float CenterDeltaTime)
{
    get_mindist()->hull_manager_is_reset(DeltaTime, CenterDeltaTime);
}

namespace
{
    // FUN_0628be60 (thiscall, RET 0xc): Out[i] = A[i] + B[i] * Scale, each
    // component `FLD Scale; FMUL B[i]; FADD A[i]` and stored as a float.
    void IVP_PointAddMultiple(Vector& Out, const Vector& A, const Vector& B, float Scale)
    {
        Out.x = (float)((double)(Scale) * B.x + A.x);
        Out.y = (float)((double)(Scale) * B.y + A.y);
        Out.z = (float)((double)(Scale) * B.z + A.z);
    }
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::hull_limit_exceeded_event -- FUN_0634fa40 (.text:
// 0634fa40-0634fd0a, RET 4), cmn-phy5 2026-10-08. The library's
// mindist_hull_limit_exceeded_event.
//   :0634fa48-:0634fa6a (StateFlags & 0x3c0000) == 0x100000 -> the recursive
//     mindist's 06362d90 and return.
//   :0634fa6d-:0634faa4 Syn0 = sorted synapse 0, Syn1 = sorted synapse 1
//     (bits 8-9, the second with `XOR 0x100`); Obj/Core of each; now = the
//     time of Syn0's object's environment.
//   :0634facc-:0634fb25 Pos_i = core_i.PosWorldFCore + core_i.DeltaWorldFCore
//     * dt_i (FUN_0628be60), dt_i = ((now.base - core.+0x68) + now.offset) -
//     core.+0x6c rounded to a float argument.
//   :0634fb2a-:0634fb46 d = Pos0 - Pos1, kept on the x87 stack.
//   :0634fb4a-:0634fb7a r_i = (BoundingRadius + Margin) + 1e-10f (DAT_063a53d8):
//     r0 stored as a float, r1 FST'd as a float and its extended value added
//     to r0 -> SumR (float).
//   :0634fb7e (StateFlags & 0x30000) != 0 -> the re-file path.
//   :0634fb84-:0634fb9c dot = (d.y*n.y + d.z*n.z) + d.x*n.x, n = SeparatingNormal.
//   :0634fb9e-:0634fbda S = spread(Obj1) + spread(Obj0), spread = elapsed *
//     (OuterGradient - InnerGradient) + (OuterValueAtLastEdit - InnerValueAtLastEdit).
//   :0634fbdc-:0634fbf1 NewGap = (SeparationGap - (S - CombinedHullSpread))
//     - (SecondaryNormalDot - dot) + HullIntrusionValue (FST as a float).
//   :0634fbf5-:0634fc0e T = env.delta_time(+0xac) * SumR * 6.0f (DAT_063a5620);
//     `FCOMPP; TEST AH,0x41; JNZ` -> NewGap <= T or unordered -> re-file path;
//     fall-through when NewGap > T:
//   :0634fc14-:0634fc26 CombinedHullSpread = S; SecondaryNormalDot = dot;
//     SeparationGap = NewGap(float) - HullIntrusionValue.
//   :0634fc2c-:0634fc40 Ratio = NewGap(float) / SumR, stored as a float.
//   :0634fc29-:0634fcba per synapse (Syn0 with r0, then Syn1 with r1): pool
//     Remove(slot) on its object's hull manager, key = (Ratio * r_i) +
//     ((now - hull.LastEditTime) * OuterGradient + OuterValueAtLastEdit),
//     Insert(synapse, key) -> slot.
//   :0634fcc7-:0634fd0a re-file path: manager = Obj0->environment->mindist_manager;
//     remove_hull_mindist(this); (StateFlags & 0x3000) == 0x1000 ->
//     insert_lazy_mindist (0634eb00), else insert_exact_mindist (0634e9f0).
// ---------------------------------------------------------------------------
void IVP_Ledge_Mindist::hull_limit_exceeded_event(float HullIntrusionValue)
{
    if ((StateFlags & 0x3c0000u) == 0x100000u)
    {
        static_cast<IVP_Complex_Mindist*>(this)->RecalcAndMaybeDeactivate();
        return;
    }

    IVP_Synapse_Real* const Syn0 = get_sorted_synapse(0);
    IVP_Synapse_Real* const Syn1 = get_sorted_synapse(1);
    IVP_Real_Object* const Obj0 = Syn0->l_obj; // EBP
    IVP_Real_Object* const Obj1 = Syn1->l_obj; // local_44
    const IVP_Core* const Core0 = Obj0->core;  // EDI
    const IVP_Core* const Core1 = Obj1->core;  // EBX
    IVP_Environment* const Env = Obj0->environment;
    const float NowBase = Env->current_time.time_base;
    const float NowOffset = Env->current_time.time_offset;

    const float Dt0 = (float)((((double)(NowBase) - Core0->TimeOfLastPsi.time_base)
                                          + NowOffset) - Core0->TimeOfLastPsi.time_offset);
    Vector Pos0;
    IVP_PointAddMultiple(Pos0, Core0->PosWorldFCore, Core0->DeltaWorldFCore, Dt0);
    const float Dt1 = (float)((((double)(NowBase) - Core1->TimeOfLastPsi.time_base)
                                          + NowOffset) - Core1->TimeOfLastPsi.time_offset);
    Vector Pos1;
    IVP_PointAddMultiple(Pos1, Core1->PosWorldFCore, Core1->DeltaWorldFCore, Dt1);

    const double Dx = (double)(Pos0.x) - Pos1.x;
    const double Dy = (double)(Pos0.y) - Pos1.y;
    const double Dz = (double)(Pos0.z) - Pos1.z;

    const float R0 = (float)(((double)(Core0->BoundingRadius) + Core0->Margin) + 1.0e-10f);
    const double R1Ext = ((double)(Core1->BoundingRadius) + Core1->Margin) + 1.0e-10f;
    const float R1 = (float)(R1Ext);
    const float SumR = (float)(R1Ext + R0);

    if ((StateFlags & 0x30000u) == 0)
    {
        const double Dot = (Dy * SeparatingNormal.y + Dz * SeparatingNormal.z) + Dx * SeparatingNormal.x;

        const IVP_Hull_Manager& Hull1 = Obj1->hull_manager;
        const IVP_Hull_Manager& Hull0 = Obj0->hull_manager;
        const double Spread1 = (((((double)(NowBase) - Hull1.LastEditTimeHi) + NowOffset) - Hull1.LastEditTimeLo)
                                * ((double)(Hull1.OuterGradient) - Hull1.InnerGradient))
                               + ((double)(Hull1.OuterValueAtLastEdit) - Hull1.InnerValueAtLastEdit);
        const double Spread0 = (((((double)(NowBase) - Hull0.LastEditTimeHi) + NowOffset) - Hull0.LastEditTimeLo)
                                * ((double)(Hull0.OuterGradient) - Hull0.InnerGradient))
                               + ((double)(Hull0.OuterValueAtLastEdit) - Hull0.InnerValueAtLastEdit);
        const double S = Spread1 + Spread0;

        const double A = (double)(SeparationGap) - (S - CombinedHullSpread);
        const double NewGapExt = (A - ((double)(SecondaryNormalDot) - Dot)) + HullIntrusionValue;
        const float NewGap = (float)(NewGapExt);

        const double Threshold = (double)(Env->delta_time) * SumR * 6.0f;
        if (NewGapExt > Threshold)
        {
            CombinedHullSpread = (float)(S);
            SecondaryNormalDot = (float)(Dot);
            SeparationGap = (float)((double)(NewGap) - HullIntrusionValue);
            const float Ratio = (float)((double)(NewGap) / SumR);

            for (int32_t I = 0; I < 2; ++I)
            {
                IVP_Synapse_Real* const Syn = (I == 0) ? Syn0 : Syn1;
                IVP_Hull_Manager& Hull = Syn->l_obj->hull_manager;
                Hull.PoolRemove(static_cast<uint16_t>(Syn->HullPoolSlot));
                const double Base = ((((double)(NowBase) - Hull.LastEditTimeHi) + NowOffset) - Hull.LastEditTimeLo)
                                    * Hull.OuterGradient + Hull.OuterValueAtLastEdit;
                const float Key = (float)((double)(Ratio) * ((I == 0) ? R0 : R1) + Base);
                Syn->HullPoolSlot = Hull.PoolInsert(static_cast<IVP_Listener_Hull*>(Syn), Key);
            }
            return;
        }
    }

    IVP_Mindist_Manager* const Manager = Obj0->environment->mindist_manager;
    Manager->remove_hull_mindist(this);
    if ((StateFlags & 0x3000u) == 0x1000u)
        Manager->insert_lazy_mindist(this);
    else
        Manager->insert_exact_mindist(this);
}

// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist::init_mindist -- FUN_0634def0 (.text:0634def0-0634e067,
// RET 0x10), cmn-phy5 2026-10-08. EDI = synapse A (+0x18), EBX = synapse B
// (+0x34) unless the ball/ball ordering below swaps them.
//   FUN_0634e070(syn, mindist, obj): word syn+0x18 = mindist - syn; syn+0x10 = obj.
//   FUN_0634e090(syn, edge, status): syn+0x14 = edge; word syn+0x1a = status.
// Those two are IVP_Synapse_Real::init_synapse_real / set_edge (ivp_mindist.h).
// ---------------------------------------------------------------------------
namespace
{
    // CORE trap of the object-type switch (`MOV dword ptr [0],0`, :0634df0f /
    // :0634dfa2). A real null store (IVP_CoreTrap), as retail.
    void IVP_InitMindistBadObjectType()
    {
        IVP_CoreTrap();
    }
}

void IVP_Ledge_Mindist::init_mindist(IVP_Real_Object* Object0, IVP_Real_Object* Object1,
                                     IVP_Compact_Edge* Edge0, IVP_Compact_Edge* Edge1)
{
    IVP_Synapse_Real* Syn0 = &synapse[0]; // EDI
    IVP_Synapse_Real* Syn1 = &synapse[1]; // EBX

    // ---- Object1 -> Syn1 (:0634defd-:0634df93) ----
    switch (Object1->object_type)
    {
    case 2: // :0634df4d -- polygon: status 0, take a ledge reference
        Syn1->init_synapse_real(this, Object1);      // FUN_0634e070
        Syn1->set_edge(Edge1, IVP_SYNAPSE_POINT);    // FUN_0634e090(edge, 0)
        Object1->surface_manager->add_reference_to_ledge(Edge1->GetCompactLedge()); // :0634df64-:0634df90 (`CALL [EDX+0x1c]`)
        break;
    case 3: // :0634df1b -- ball
        // Two balls are ordered by the unsigned dword at +0xa4: Object0 keeps
        // synapse A only when it is a ball too and its value is >= Object1's
        // (`CMP ECX,[EAX+0xa4]; JNC` keeps, otherwise swap EDI/EBX).
        if (!(Object0->object_type == 3 &&
              reinterpret_cast<uintptr_t>(Object0->client_data) >= reinterpret_cast<uintptr_t>(Object1->client_data)))
        {
            IVP_Synapse_Real* Tmp = Syn0; // :0634df2f-:0634df33
            Syn0 = Syn1;
            Syn1 = Tmp;
        }
        Syn1->init_synapse_real(this, Object1);      // FUN_0634e070
        Syn1->set_edge(Edge1, IVP_SYNAPSE_BALL);     // :0634df42 / :0634df45
        break;
    default:
        IVP_InitMindistBadObjectType();              // :0634df0f, then JMP :0634df97
        break;
    }

    // ---- Object0 -> Syn0 (:0634df97-:0634e009) ----
    switch (Object0->object_type)
    {
    case 2: // :0634dfc9
        Syn0->init_synapse_real(this, Object0);      // :0634dfd4 / :0634dfda
        Syn0->set_edge(Edge0, IVP_SYNAPSE_POINT);    // FUN_0634e090(edge, 0)
        Object0->surface_manager->add_reference_to_ledge(Edge0->GetCompactLedge()); // :0634dfe2-:0634e006
        break;
    case 3: // :0634dfae
        Syn0->init_synapse_real(this, Object0);      // :0634dfb6 / :0634dfbf
        Syn0->set_edge(Edge0, IVP_SYNAPSE_BALL);     // FUN_0634e090(edge, 3)
        break;
    default:
        IVP_InitMindistBadObjectType();              // :0634dfa2, then JMP :0634e00d
        break;
    }

    // :0634e00d-:0634e019 -- both owner offsets rewritten unconditionally.
    Syn0->mindist_offset = static_cast<int16_t>(reinterpret_cast<char*>(this) - reinterpret_cast<char*>(Syn0));
    Syn1->mindist_offset = static_cast<int16_t>(reinterpret_cast<char*>(this) - reinterpret_cast<char*>(Syn1));

    // :0634e01d-:0634e029 -- FLD [Object0+0x94]; FADD [Object1+0x94]; FSTP float.
    CombinedRadius = Object0->field_94 + Object1->field_94;

    // :0634e02c-:0634e067 -- ECX = Object0->environment->mindist_manager.
    IVP_Mindist_Manager* Manager = Object0->environment->mindist_manager;
    if (Object0->first_child == nullptr && Object1->first_child == nullptr)
    {
        Manager->insert_exact_mindist(this);                  // FUN_0634e9f0
        return;
    }
    StateFlags = (StateFlags & 0xffffdfffu) | 0x1000u;        // :0634e04d-:0634e05b
    Manager->insert_lazy_mindist(this);                       // FUN_0634eb00
}

// ---------------------------------------------------------------------------
// d229:cluster-06355200 pass (2026-09-10): items 3231/3243. Appended here
// (not interleaved above) for the same reason FUN_06363620/item 3510's own
// tail addition documents -- multiple concurrent lanes are actively editing
// the earlier sections of this file today, and both items below genuinely
// need the anonymous-namespace-local IVP_PopulatePairRecordIntoOwner /
// IVP_ResolvePairRecordSideMaterialSlots helpers declared above (~line 1485/
// 1513), which is why they live in THIS file rather than the new
// ivp_listmanager_topology.cpp (see that file's own header comment for the
// full cluster writeup) -- anonymous-namespace names are visible anywhere
// later in the same translation unit, so calling them from way down here is
// fine despite the physical distance.
// ---------------------------------------------------------------------------

// Item 3231: FUN_06355c10 (Common.dll.txt:06355c10-06355c76, 43 insns).
// Disasm-traced in full this pass, including the caller (FUN_06359152's own
// call site, Common.dll.txt:0635914c-06359152) to recover the true 2nd
// argument: `this`=Entry (IVP_ListManager_CorePairEntry*, its own +0x00
// AttachedCtx vector IS what FUN_06355c10 iterates, since AttachedCtx is
// Entry's first member) and a genuine 2nd stack argument that the caller's
// own [EDI+0x20] dereference chain (`MOV ECX,[EDI+0x20]; MOV BX,
// word[ECX+0x36]`, matching IVP_ListManager::CorePairEntries' own n_elems at
// ListManager+0x36) confirms is an IVP_ListManager*. For every record in
// Entry->AttachedCtx: FUN_0635a1e0 (IVP_UpdatePairRecordContactFrame), then
// IVP_PopulatePairRecordIntoOwner; when the record's Owner's packed +0x54
// dword reads 0x100 under mask 0x300, decrements the running live-count and
// calls FUN_06355490 (IVP_ListManager::RemoveDistFromSystem) on Manager.
// Returns the post-decrement live count (FUN_06359152's caller tests it > 0).
//
// 2026-10-08 (claude-code:cmn-phy3), re-traced .text:06355c10-06355c72:
//   06355c13-06355c15: EBP = zero-extended word [Entry+2] (the live count
//     starts at n_elems), EBX = EBP - 1, `TEST; JL` -> empty returns EBP.
//   06355c2c-06355c2f: element re-read from [Entry+4] every iteration.
//   06355c34: FUN_0635a1e0.  06355c39: EDI = Record->Owner, read AFTER it.
//   06355c3c-06355c3f: FUN_06358c50(Record, Owner).
//   06355c44-06355c53: ([EDI+0x54] & 0x300) == 0x100 -> 06355c5a DEC EBP,
//     06355c5b FUN_06355490(this=param_1, Record).
//   06355c60-06355c61: DEC EBX; JNS.  RET 4, EAX = EBP.
// The two empty stubs this used to call (IVP_DEFERRED_RecomputePairRecordMindist,
// IVP_DEFERRED_RetirePairRecordFromListManager) are gone: both callees are ported.
int IVP_ListManager_RefreshAttachedCtxAndCountLive(IVP_ListManager_CorePairEntry* Entry, IVP_ListManager* Manager)
{
    int LiveCount = static_cast<int>(Entry->AttachedCtx.n_elems);

    for (int I = LiveCount - 1; I >= 0; --I)
    {
        IVP_Contact_Point* Record = Entry->AttachedCtx.elems[I];

        IVP_UpdatePairRecordContactFrame(Record);
        IVP_Impact_Solver_Long_Term* Owner = Record->Owner;
        IVP_PopulatePairRecordIntoOwner(Record, Owner);

        const uint32_t PackedState = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<char*>(Owner) + offsetof(IVP_Impact_Solver_Long_Term, StateProcessed));
        if ((PackedState & 0x300) == 0x100)
        {
            --LiveCount;
            Manager->RemoveDistFromSystem(Record);
        }
    }

    return LiveCount;
}

// ---------------------------------------------------------------------------
// LAB_063568a0 (item 3252) used to be ported here as the free function
// IVP_ListManager_RemovePairEntriesReferencingCore. It is slot 0 of the static
// friction handle's vtable (.rdata:063a5500) and now lives in
// ivp_friction_solver.cpp as IVP_Friction_Sys_Static::
// core_is_going_to_be_deleted_event (2026-10-08, claude-code:cmn-phy3).

// Item 3243: FUN_063562d0 (Common.dll.txt:063562d0-063563c0, 85 insns,
// medium). Disasm-traced in full this pass. Computes Record->MassFactor
// (this pass's own newly-confirmed field, see ivp_mindist.h) as the
// reciprocal two-body reduced mass of Record->Object1->core/
// Object2->core (IVP_Core::calc_conservative_effective_mass, already
// ported, ivp_core.cpp), using each side's own IVP_Impact_Solver_Long_Term::CoordOS[N] as
// the lever-arm argument (CoordOS[0] paired with Object1's core, CoordOS[1]
// with Object2's -- Common.dll.txt:06356347-06356368's own loop). When
// either core is kinematic-ish (Flags&0xc or Flags&0x300), only the OTHER
// side's effective mass is used (a kinematic core has effectively infinite
// mass, matching the general pattern used throughout this cluster). Also
// sets Record->_flags34 bit 0 when either side's resolved material
// (IVP_ResolvePairRecordSideMaterialSlots, already ported -- confirmed this
// pass to yield IVP_Real_Object::material, an IVP_Material*, not a generic
// handle) has a non-zero dword at its own ShortName[4..7] (`[Material+8]`,
// landing inside IVP_Material::ShortName -- byte-exact transliteration of
// retail's own `TEST [Material+8],...` idiom, not a claim about a separate
// +0x08 field). Retail's own OutArray[2] out-params are read even on the
// `Tag != 0` deferred branch (IVP_ResolvePairRecordSideMaterialSlots leaves
// that slot untouched, i.e. uninitialized stack garbage in retail) -- this
// port zero-initializes them first instead (SAFETY DEVIATION, matching
// IVP_Contact_Point::IVP_Contact_Point's own memset precedent above), so the material
// check below is deterministic (treated as "no material") rather than
// reading garbage.
//
// 2026-10-08 (claude-code:cmn-phy3), re-traced .text:063562d0-063563c0 now
// that +0x40/+0x44 are real IVP_Material*:
//   063562e1-063562f5: Materials[0]->SecondFrictionXEnabled != 0, or else
//     Materials[1]->SecondFrictionXEnabled != 0 -> 063562f7 `AND 0xffffff01;
//     OR 1` on the dword at +0x34 (SolverMode = 1, bytes 1..3 kept).
//   0635630c-06356322: both cores read (Object[side]->core, +0x98).
//   0635632a-06356340: core1 (Flags & 0xc || Flags & 0x300) -> 0635639c,
//     core2's effective mass at CoordOS[1]; else core2 likewise -> 06356390,
//     core1's at CoordOS[0]. Both: `FLD 1.0; FDIV ST0,ST1` on the unrounded
//     FUN_063463f0 result, one FSTP float into +0x30.
//   06356347-06356368: both effective masses stored as floats (E0, E1);
//     0635636a-06356386: (E1*E0) / (E1+E0) on the FPU, then 1.0 / that, one
//     FSTP float into +0x30.
// FUN_063463f0 returns its quotient on the FPU stack; calc_conservative_
// effective_mass returns that value as a double, so the single-side paths
// round once, as retail does.
void IVP_ComputePairRecordMassFactor(IVP_Contact_Point* Record)
{
    IVP_Material* Materials[2];
    IVP_ResolvePairRecordSideMaterialSlots(Record, Materials);

    if (Materials[0]->SecondFrictionXEnabled != 0 || Materials[1]->SecondFrictionXEnabled != 0)
    {
        Record->SolverMode = 1;
    }

    IVP_Core* Core1 = Record->Object1->core;
    IVP_Core* Core2 = Record->Object2->core;

    if ((Core1->Flags & 0xc) != 0 || (Core1->Flags & 0x300) != 0)
    {
        const double EffMass = Core2->calc_conservative_effective_mass(&Record->Owner->CoordOS[1]);
        Record->MassFactor = (float)(1.0 / EffMass);
        return;
    }
    if ((Core2->Flags & 0xc) != 0 || (Core2->Flags & 0x300) != 0)
    {
        const double EffMass = Core1->calc_conservative_effective_mass(&Record->Owner->CoordOS[0]);
        Record->MassFactor = (float)(1.0 / EffMass);
        return;
    }

    const float EffMass0 = (float)(Core1->calc_conservative_effective_mass(&Record->Owner->CoordOS[0]));
    const float EffMass1 = (float)(Core2->calc_conservative_effective_mass(&Record->Owner->CoordOS[1]));
    const double Reduced = ((double)(EffMass1) * EffMass0) / ((double)(EffMass1) + EffMass0);
    Record->MassFactor = (float)(1.0 / Reduced);
}


// ---------------------------------------------------------------------------
// d230:cluster-063568a0 pass (2026-09-10): item 3306, FUN_0635aa20
// (Common.dll.txt:444010-444034, 22 insns). Zero-initializes a 4-float (or
// 4-dword) structure. Retail calls FUN_0635aa10 (`AND [field],0x80000000` --
// "preserve sign bit, clear the rest", a common "reset to signed zero"
// idiom for an IEEE float) on the 3 fields at +0x4/+0x8/+0xc, then
// UNCONDITIONALLY overwrites all 4 fields (+0x0 through +0xc) with plain
// 0. Since -0.0f == 0.0f for every purpose this port cares about, and the
// unconditional zero pass runs immediately afterward and touches the exact
// same 3 fields, the 3 FUN_0635aa10 calls have NO OBSERVABLE EFFECT on the
// final state here -- reproduced anyway (as literal, harmless zero-writes)
// for 1:1 fidelity with retail's actual instruction sequence, documented as
// a retail curiosity rather than silently dropped.
//
// Single caller (0x0637a4f0) is out of this session's scope, so the owning
// class/semantic field names (plausibly an IVP_U_Float_Hesse-shaped
// plane-equation value, given the 4-float shape, but not confirmed) aren't
// independently determined here -- ported as a generic 4-float zero-init.
extern "C" void* FUN_0635aa20(void* param_1_00)
{
    float* fields = static_cast<float*>(param_1_00);
    fields[0] = 0.0f;
    fields[1] = 0.0f;
    fields[2] = 0.0f;
    fields[3] = 0.0f;
    return param_1_00;
}
