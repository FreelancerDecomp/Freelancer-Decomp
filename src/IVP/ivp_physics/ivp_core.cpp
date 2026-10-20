// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_core.h"
#include "ivp_real_object.h"
#include "ivp_environment.h"
#include "ivp_simulation_unit.h"
#include "ivp_controller.h"
#include "../ivp_utility/ivp_u_memory.h"
#include "ivp_ov_element.h"
#include "ivp_cluster_manager.h" // IVP_Cluster_Manager::fire_event_object_revived / _frozen (0634afd0 / 0634af70)
#include "ivp_mindist.h" // IVP_Contact_Point / IVP_Impact_Solver_Long_Term / IVP_ListManager (063464a0 / 063465a0), cmn-ivp3d
#include "../ivp_collision/ivp_listmanager_topology.h" // contact-point helpers (063464a0 / 063465a0), cmn-ivp3d
#include "ivp_contact_point.h"    // IVP_Try_To_Generate_Managed_Friction (06356bc0), cmn-ivp3d
#include "ivp_impact.h"           // IVP_Old_Sync_Rot_Z (06347240 / 063473b0), cmn-ivp3g
#include "ivp_mindist_settings.h" // G_IVP_MindistSettings (DAT_0658ab2c), cmn-ivp3d

#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <new>

// The Robin-Hood key -> value store behind IVP_Core::ObjectHashMap in
// hashmap mode is IVP_VHash_Store (decomp-tracker item 3178): its destructor
// (FUN_06352620) frees the map in IVP_Core_Remove_Object below, and its
// ctor / AddElem / RemoveElem / FindElem (FUN_063525f0 / 063526d0 / 063527e0 /
// 06352930) serve find_object_hash_entry() / register_object_hash_entry() /
// unregister_object_hash_entry() further down. ObjectHashMap stays void*
// because the single-slot mode stores a candidate pointer in it directly.
#include "../ivp_utility/ivp_vhash.h"
#include "../ivp_utility/ivp_signed_byte.h" // IVP_SignedLowByte


// Internal IVP helper types
// IVP_U_Memory used to be re-declared here with its own member names (FirstBlock/
// CurrentBlock/CurrentPtr/EndPtr/MarkCount/BlockSize). That was the fourth
// transcription of the same 0x14-byte arena; the canonical one now lives in
// ivp_utility/ivp_u_memory.h and the duplicate is gone. The old `MarkCount` at +0x10
// is that header's `State`, same offset and width -- Mark()/Unmark() were just ++/--
// on it, so the call sites below increment it directly.

// cmn-ivp3d 2026-10-08: IVP_Real_Object::mindist_list (+0x28) heads the
// object's contact-point nodes: the node is the record's per-object link
// (IVP_Contact_Point +0x08 / +0x1c), node+0 is the next node and the
// int16 at node+0x0c is record - node (Delta1 / Delta2). The fake
// IVP_Synapse_Friction / IVP_Core_Fast_Sim structs that stood here are gone.
namespace
{
    // A .rdata float constant by its exact bits (some differ from the nearest
    // decimal literal by one ulp).
    float IVP_Core_Float_From_Bits(uint32_t Bits)
    {
        float Value;
        std::memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }

    char* IVP_Contact_Node_Next(char* Node)
    {
        return *reinterpret_cast<char**>(Node); // `MOV EDI,[EAX]`
    }

    IVP_Contact_Point* IVP_Contact_Node_Record(char* Node)
    {
        return reinterpret_cast<IVP_Contact_Point*>(Node + *reinterpret_cast<int16_t*>(Node + 0xc)); // `MOVSX ESI,word [EAX+0xc]; ADD ESI,EAX`
    }

    // FUN_06346770 (thiscall): the long-term impact info's constructor body
    // -- Status (+0x52) word = 0, the dword at +0x54 &= 0xfffffc00. 063464a0
    // inlines the same two stores (:06346534-:0634654e).
    void IVP_Mindist_Reset_Long_Term_State(IVP_Impact_Solver_Long_Term* Info)
    {
        uint32_t* const Dword54 = reinterpret_cast<uint32_t*>(&Info->StateProcessed);
        *Dword54 &= 0xfffffc00u;
        Info->Status = 0;
    }
}

// ============================================================================
// Helper Subroutines
// ============================================================================


// FUN_06346fb0: Halts movement, zeros accumulators and velocities for static cores
void __fastcall FUN_06346fb0(IVP_Core* Core)
{
    Core->MovementState = (Core->MovementState & 0xFFFFFF08) | 8;

    Core->RotSpeedChange.x = 0.0f;
    Core->RotSpeedChange.y = 0.0f;
    Core->RotSpeedChange.z = 0.0f;

    Core->SpeedChange.x = 0.0f;
    Core->SpeedChange.y = 0.0f;
    Core->SpeedChange.z = 0.0f;

    Core->RotSpeed.x = 0.0f;
    Core->RotSpeed.y = 0.0f;
    Core->RotSpeed.z = 0.0f;

    Core->Speed.x = 0.0f;
    Core->Speed.y = 0.0f;
    Core->Speed.z = 0.0f;

    Core->DeltaWorldFCore.x = 0.0f;
    Core->DeltaWorldFCore.y = 0.0f;
    Core->DeltaWorldFCore.z = 0.0f;

    // FUN_06346e70 (item 2850), NOW WIRED (D-target-2989 pass, 2026-09-10):
    // retail's own tail (Common.dll.txt:06347014-06347039) reads
    // Core->Environment (no null check -- a live Core always has one) and
    // passes {delta_time, inv_delta_time} (+0xAC/+0xB0) straight through.
    // See IVP_Core::synchronize_simulation_time_snapshot's header comment in
    // ivp_core.h for the full 2-caller trace.
    Core->synchronize_simulation_time_snapshot(Core->Environment->delta_time,
                                                 Core->Environment->inv_delta_time);
}

// ============================================================================
// FUN_063464a0 / FUN_063465a0 (cmn-ivp3d 2026-10-08, re-ported from the
// listing; the old lookalike read env->memory_manager as the arena, invented
// null guards and dropped every contact-point call).
// ============================================================================

// FUN_063464a0 (.text:063464a0-06346593), reached only from 063465a0's
// fixed-core arm. Port-local name. For every contact point of every object
// of a core that has just become fixed: flag its friction system (low byte of
// +0x44 := 1); if the OTHER side's core is fixed too (`TEST byte [core],0xc`)
// the contact point is destroyed (06355200 + operator delete), else the other
// core's objects are put back into simulation (06347db0) and the contact
// point is recomputed (0635a1e0, 06358c50 into a reused stack long-term
// info, 063562d0).
static void IVP_Core_Recalc_Contact_Points_Of_Fixed_Core(IVP_Core* Core)
{
    IVP_U_Memory* const Arena = Core->Environment->vector_cores_frozen; // :063464a9-:063464ac (+0xa4)
    ++Arena->State;                                                      // :063464b2

    IVP_Impact_Solver_Long_Term Info; // the stack long-term info (ESP+0x14), only +0x52 / +0x54 reset per use
    for (int32_t ObjIndex = static_cast<int32_t>(Core->Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex) // count read once (:063464b8)
    {
        char* Node = static_cast<char*>(Core->Objects.elems[ObjIndex]->mindist_list); // :063464cc-:063464d2
        while (Node != nullptr)
        {
            IVP_Contact_Point* const Record = IVP_Contact_Node_Record(Node);
            char* const Next = IVP_Contact_Node_Next(Node); // read first (:063464e1)

            IVP_ListManager* const FrictionSystem = Record->ListManagerOwner;              // :063464e5
            FrictionSystem->union_find_necessary = 1;                                      // :063464e8-:063464f4

            IVP_Core* OtherCore = Record->Object1->core;                                   // :063464f7-:063464fa
            if (OtherCore == Core)
            {
                OtherCore = Record->Object2->core;                                         // :06346504-:06346507
            }

            if ((static_cast<uint8_t>(OtherCore->Flags) & 0xcu) != 0)                      // :0634650d `TEST byte [ECX],0xc`
            {
                if (Record != nullptr)                                                     // :06346512
                {
                    IVP_UnlinkPairRecordFromObjects(Record); // FUN_06355200
                    delete Record;                           // FUN_06391d5a
                }
            }
            else
            {
                OtherCore->ensure_objects_in_simulation();  // :06346528 FUN_06347db0 (ECX = the other core)
                IVP_UpdatePairRecordContactFrame(Record);   // :0634652f FUN_0635a1e0
                IVP_Mindist_Reset_Long_Term_State(&Info);   // :06346534-:0634654e
                IVP_PopulatePairRecordIntoOwner(Record, &Info); // :06346552 FUN_06358c50
                IVP_ComputePairRecordMassFactor(Record);    // :06346559 FUN_063562d0
            }
            Node = Next;
        }
    }

    --Arena->State;        // :0634657a-:06346583 (arena re-read through the environment)
    Arena->reset_chunks(); // :06346587 FUN_06344070
}

// FUN_063465a0 (.text:063465a0-06346767), the library's
// IVP_Core::values_changed_recalc_redundants. Name kept as the raw address
// because ivp_real_object.cpp calls it so; rename with that caller.
void __fastcall FUN_063465a0(IVP_Core* Core)
{
    // :063465a9-:063465ca: inertia terms inside an arena bracket.
    ++Core->Environment->vector_cores_frozen->State;
    Core->RecomputeInverseInertia();                    // FUN_06347410
    --Core->Environment->vector_cores_frozen->State;
    Core->Environment->vector_cores_frozen->reset_chunks(); // FUN_06344070

    // :063465cf-:0634660b: the movement byte every object takes over.
    int32_t ObjectMovement;
    if ((static_cast<uint8_t>(Core->Flags) & 0xcu) != 0)
    {
        Core->MovementState = (Core->MovementState & 0xffffff08u) | 0x8u;
        ObjectMovement = 0x10;
    }
    else
    {
        // `SHL ECX,0x18; CMP ECX,0x8000000; JLE skip` -- signed.
        if (IVP_SignedLowByte(Core->MovementState) > 8)
        {
            Core->MovementState = (Core->MovementState & 0xffffff08u) | 0x8u;
        }
        ObjectMovement = static_cast<int8_t>(Core->MovementState & 0xffu); // re-read, sign-extended
    }

    // :0634660e-:06346630: low byte of every object's flags := ObjectMovement.
    for (int32_t ObjIndex = static_cast<int32_t>(Core->Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex)
    {
        IVP_Real_Object* const Object = Core->Objects.elems[ObjIndex];
        Object->flags = ((Object->flags ^ static_cast<uint32_t>(ObjectMovement)) & 0xffu) ^ Object->flags;
    }

    if ((static_cast<uint8_t>(Core->Flags) & 0xcu) != 0) // :06346632 (re-tested)
    {
        // :0634672b-:06346732.
        FUN_06346fb0(Core);
        IVP_Core_Recalc_Contact_Points_Of_Fixed_Core(Core);
        // :06346737-:0634675c: retail then walks every object's contact-node
        // list to its end without reading or writing anything else -- a loop
        // whose body was optimised away. No observable effect; not reproduced.
        return;
    }

    // :0634663d-:06346721.
    Core->wake_or_ensure_simulation(); // FUN_06347d80
    ++Core->Environment->vector_cores_frozen->State;
    for (int32_t ObjIndex = static_cast<int32_t>(Core->Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex) // count read once (:06346651)
    {
        char* Node = static_cast<char*>(Core->Objects.elems[ObjIndex]->mindist_list); // :06346661-:06346667
        if (Node == nullptr)
        {
            continue;
        }
        // The friction system of the first contact point; every other one is
        // merged into it (:06346672-:06346676, :06346684-:0634668b).
        IVP_ListManager* const FrictionSystem = IVP_Contact_Node_Record(Node)->ListManagerOwner;
        do
        {
            IVP_Contact_Point* const Record = IVP_Contact_Node_Record(Node);
            if (Record->ListManagerOwner != FrictionSystem)
            {
                FrictionSystem->MergeFrom(Record->ListManagerOwner); // FUN_063555b0
            }

            // :06346690-:063466d6: a fresh stack long-term info per contact
            // point (three trivial vector ctor iterators 06273bc0, then 06346770).
            IVP_Impact_Solver_Long_Term Info;
            IVP_Mindist_Reset_Long_Term_State(&Info);
            IVP_UpdatePairRecordContactFrame(Record);       // :063466dd FUN_0635a1e0
            IVP_PopulatePairRecordIntoOwner(Record, &Info);  // :063466e9 FUN_06358c50
            IVP_ComputePairRecordMassFactor(Record);         // :063466f0 FUN_063562d0

            Node = IVP_Contact_Node_Next(Node); // :063466f5, read after the calls
        } while (Node != nullptr);
    }
    --Core->Environment->vector_cores_frozen->State;        // :0634670f-:06346718
    Core->Environment->vector_cores_frozen->reset_chunks(); // :0634671c
}

// FUN_063557c0: IVP_Core::get_master_core() equivalent. Cores can be merged
// (welded together, e.g. by a fixed constraint); when that happens one core's
// MergedCoreWhichReplacedIt is linked to point at the other (see FUN_06348ad0,
// which unions two cores' chains together and zero-initializes the field for a
// fresh group). This walks that chain to its terminal (null-linked) node, i.e.
// the current representative/master core for the merged group. A null input
// core is passed straight through, matching retail's explicit ECX==0 check.
IVP_Core* __fastcall FUN_063557c0(IVP_Core* Core)
{
    if (Core == nullptr)
        return Core;

    IVP_Core* Master = Core;
    while (Master->MergedCoreWhichReplacedIt != nullptr)
        Master = Master->MergedCoreWhichReplacedIt;
    return Master;
}

// ============================================================================
// Core Management (Attachment / Detachment)
// ============================================================================

void IVP_Core_Add_Object(IVP_Core* Core, IVP_Real_Object* Object)
{
    Core->Objects.Add(Object);
}

void IVP_Core_Remove_Object(IVP_Core* Core, IVP_Real_Object* Object)
{
    int RemovedIndex = Core->Objects.RemoveElement(Object);

    if (Core->Objects.n_elems == 0)
    {
        // Retail (FUN_06346f20 @ Common.dll.txt:410395-410400) guards the call with a
        // SIGNED comparison of the low byte of MovementState against 8 (byte is shifted
        // into the sign position: `EDX = MovementState; EDX <<= 24; CMP EDX,0x08000000`),
        // not an unsigned one -- matters for byte values 0x80-0xFF, which retail treats
        // as "< 8" (negative once sign-extended) but a plain unsigned compare would not.
        if (static_cast<int8_t>(Core->MovementState & 0xFF) < 8)
        {
            Core->freeze_simulation_core(); // :06346f87 FUN_06347040 (its object loop is empty here)
        }

        // Real IVP_Core::~IVP_Core() cleanup (FUN_063477e0, Common.dll.txt:411253-411349;
        // decomp-tracker item 2866). D69 (2026-09-09) identified all 4 explicit steps
        // retail performs before freeing Core (beyond the Objects/Controllers vector-buffer
        // release IVP_U_Vector's own destructor already replicates via `delete Core` below);
        // D72 (2026-09-09) closed steps 1/3/4. D76 (2026-09-09) closes step 2, the last gap:
        // the earlier "Core+0x5c overlaps Objects' inline buffer" concern is resolved --
        // Objects is really IVP_U_Vector_Fixed<IVP_Real_Object,1> (1 inline slot, not 2), so
        // ObjectHashMap (+0x5c) is a genuinely separate field, never written by Objects'
        // vector machinery. See ivp_core.h's field comments for the full evidence.

        // Step 1 (Common.dll.txt:411269-411272): dequeue Core from Environment->event_queue
        // and clear Core->Flags &= ~0x30, in case it was still mid-queue when torn down.
        IVP_Environment_Dequeue_Core(Core->Environment, Core);

        // Step 2 (Common.dll.txt:411273-411301), NOW WIRED (D76, 2026-09-09):
        // `if ((Core->Flags & 0xC) == 4)`: free the Robin-Hood hashmap at
        // Core->ObjectHashMap (+0x5c) -- clear its bucket array via FUN_06352620
        // (~IVP_VHash_Store, ivp_utility/ivp_vhash.cpp, item 3178) then free the
        // container itself (FUN_06391d5a == free()), matching retail's
        // `CALL thunk_FUN_06352620 (0x06347824); PUSH EDI; CALL FUN_06391d5a
        // (0x0634782a); MOV [ESI+0x5c],0 (0x06347832)` exactly, Common.dll.txt:
        // 411282-411286.
        // `else`: walk Core->Controllers (now correctly Fixed<1>, elems/n_elems read
        // fresh at loop start exactly once like retail's `MOV DI,word[ESI+0x152]`),
        // calling each attached IVP_Controller's core_is_going_to_be_deleted_event(Core)
        // -- retail's raw `MOV EDX,[ECX]; CALL [EDX]` thiscall dispatch through vtable
        // slot 0, confirmed to be exactly this method by src/include/common_physys.h's
        // PhySys::Controller vtable-slot documentation (ivp_controller.h's own comment
        // has the full trace). No concrete IVP_Controller subclass exists anywhere in
        // this port yet, so Core->Controllers is always empty in practice -- this loop
        // is a real, ABI-correct no-op today, ready to fire once one is ported.
        if ((Core->Flags & 0xC) == 4)
        {
            if (Core->ObjectHashMap != nullptr)
            {
                ((IVP_VHash_Store*)Core->ObjectHashMap)->~IVP_VHash_Store(); // FUN_06352620
                free(Core->ObjectHashMap); // FUN_06391d5a
                Core->ObjectHashMap = nullptr;
            }
        }
        else
        {
            int ControllerCount = Core->Controllers.Len();
            for (int i = ControllerCount - 1; i >= 0; --i) // :06347839-06347852, count read once, elems re-read, no null test
            {
                Core->Controllers.ElementAt(i)->core_is_going_to_be_deleted_event(Core);
            }
        }

        // Step 3 (Common.dll.txt:411302-411306): if Core has an owning IVP_Simulation_Unit
        // (Core->OwningSimUnit, +0x15c), detach Core from it. PORTED (D72, 2026-09-09) as
        // IVP_Simulation_Unit_Detach_Core (ivp_simulation_unit.h/.cpp, decomp-tracker item
        // 2919 and its 5 former unprocessed helpers).
        if (Core->OwningSimUnit != nullptr)
        {
            IVP_Simulation_Unit_Detach_Core(Core->OwningSimUnit, Core);
        }

        // Step 4 (Common.dll.txt:411308-411311): free the owned block at Core+0x4c
        // (ivp_core.h: SpinClipping) and zero it.
        free(Core->SpinClipping); // :06347864-0634786d FUN_06391d5a, unconditional
        Core->SpinClipping = nullptr;

        // Core->Objects/Controllers vector-buffer freeing (the rest of FUN_063477e0,
        // Common.dll.txt:411312-411349) IS already correctly replicated, for free, by
        // IVP_U_Vector<T>'s own destructor running as part of `delete Core` here --
        // verified byte-for-byte against retail's manual inline-buffer-checked free
        // (see item 2866's notes). No gap in that part.
        delete Core;
    }
}

// ============================================================================
// FUN_06347550 + FUN_06347660: IVP_Core's real constructor. See the header's
// own comment above the declaration for the full field-by-field evidence
// trail (D-ivpenv-staticobject pass, 2026-09-10).
// ============================================================================
IVP_Core::IVP_Core(IVP_Real_Object* Owner, const IVP_U_Quat* InitialOrientation,
                    const IVP_U_Point* InitialPosition, uint32_t FlagsHi, uint32_t FlagsLo)
{
    // Step 1 (FUN_06347550, `REP STOSD` over 0x6F dwords): zero the whole object.
    memset(this, 0, sizeof(IVP_Core));

    // Step 2: Objects (+0x50) -- re-run its Fixed<1> inline-buffer init (the
    // memset above wiped out whatever a member default-constructor would have
    // set). MUST be placement-new, not copy-assignment from a temporary: the
    // temporary's own constructor would compute `elems` relative to ITS OWN
    // address (see IVP_U_Vector_Fixed's D76 comment in ivp_u_vector.h), and
    // copy-assigning that into `Objects` would leave `Objects.elems` pointing
    // at the temporary's (about-to-be-destroyed) inline buffer instead of
    // `this->Objects`'s own. Placement-new runs the constructor in place, so
    // `elems` correctly resolves to `&Objects`'s own inline Buffer[0].
    // Then add Owner as this Core's sole initial attached object, and copy
    // Owner's own environment link.
    new (&Objects) IVP_U_Vector_Fixed<IVP_Real_Object, 1>();
    Objects.Add(Owner);
    Environment = Owner->environment; // IVP_Object::environment, +0x18

    // Step 3: allocate + construct the owning IVP_Simulation_Unit, attach this
    // Core to it, and mark both as "static/inactive" (low byte 8).
    // .text:063475ae-063475f8: only the constructor call is behind the
    // operator_new null test (JZ 063475cf); the Add (FUN_06348ec0) and the
    // `(State & 0xffffff08) | 8` store run on the result unconditionally.
    // The EH state local_4 = 0 around the ctor (063475bc) is the compiler's
    // "free the block if the ctor throws" funclet: a nothrow new-expression
    // (null test + ctor + deallocation on throw) is exactly that shape.
    IVP_Simulation_Unit* Unit = new (std::nothrow) IVP_Simulation_Unit(); // operator_new(0x24) + FUN_06348e10
    OwningSimUnit = Unit;                 // 063475dc
    Unit->Cores.Add(this);                // FUN_06348ec0, byte-identical to IVP_U_Vector<T>::Add
    OwningSimUnit->State = (OwningSimUnit->State & 0xFFFFFF08u) | 8u; // 063475e7-063475f8

    // Step 4 (.text:063475fa-06347602): `MOV EAX,[ESI+0xc]; MOV ECX,[EAX]; PUSH ECX;
    // MOV ECX,ESI; CALL FUN_06349180` -- every new core is registered with the
    // environment's standard gravity controller (IVP_Environment+0x00).
    register_controller(Environment->standard_gravity_controller);
    MovementState = (MovementState & 0xFFFFFF08u) | 8u; // 06347607-06347617, after the register

    // Step 5 (FUN_06347660, .text:063476b0-063476d0): Flags bits [3:2] from
    // FlagsHi, then FUN_06349400 on Environment->+0x08 with OwningSimUnit --
    // unconditional in retail (cmn-ivp3f 2026-10-09: the port's former
    // `Environment != nullptr && OwningSimUnit != nullptr` guard was invented
    // and is removed).
    Flags = (Flags & ~0xCu) | ((FlagsHi & 0x3u) << 2);
    Environment->sim_units_manager->add_sim_unit_to_manager(OwningSimUnit); // FUN_06349400

    // Step 6: initial orientation, normalized, duplicated into QWorldFCoreNext.
    QWorldFCore = *InitialOrientation;
    QWorldFCore.normize(); // FUN_06350fb0, byte-identical to IVP_U_Quat::normize()
    QWorldFCoreNext = QWorldFCore;

    // Step 7: initial position, zero pose delta, rotation-only matrix refresh,
    // then the matrix's own translation slot gets the same position again.
    // PosWorldFCore/DeltaWorldFCore are the plain 3-float `Vector` type
    // (common_ini.h), not IVP_U_Point, hence the field-by-field copy.
    PosWorldFCore.x = InitialPosition->x;
    PosWorldFCore.y = InitialPosition->y;
    PosWorldFCore.z = InitialPosition->z;
    DeltaWorldFCore.x = 0.0f;
    DeltaWorldFCore.y = 0.0f;
    DeltaWorldFCore.z = 0.0f;
    QWorldFCore.set_matrix(&MWorldFCore); // FUN_06350bc0, writes rows only, not vv
    MWorldFCore.vv.set(InitialPosition);

    // Step 8: retail default speed limits.
    SetSpeedLimits(1000.0f, 1000.0f); // FUN_06346870

    // Step 9: Flags bits [1:0] from FlagsLo (combined with step 5's bits
    // [3:2], the constructed Core's Flags low nibble is fully determined by
    // FlagsHi/FlagsLo; every other Flags bit stays 0 from step 1).
    Flags = (Flags & ~0x3u) | (FlagsLo & 0x3u);
}

// FUN_06346870 (Common.dll.txt:409632-409642, decomp-tracker item 2837).
// See the header's own comment above the declaration for the re-attribution
// evidence (D-ivpenv-staticobject pass, 2026-09-10).
void IVP_Core::SetSpeedLimits(float NewUpperLimitRadius, float NewSpeedLimit)
{
    UpperLimitRadius = NewUpperLimitRadius;
    SpeedLimit = NewSpeedLimit;
}

// The tail of FUN_06347d80 (:06347d9b-06347da1): ECX = OwningSimUnit (+0x15c),
// JMP FUN_063494f0 -- no null test on the unit and none on the cores'
// environments (cmn-ivp3g 2026-10-09 removed both invented guards and the
// raw-offset duplicate of 063494f0).
void IVP_Core::ensure_in_simulation()
{
    IVP_Simulation_Unit_Update_Cores_Last_Motion_Time(OwningSimUnit); // FUN_063494f0
}

// FUN_063470e0: Common.dll.txt:410541-410589 (decomp-tracker item 2855, 40
// insns). See the header comment above this method's declaration in
// ivp_core.h for the full evidence trail. Retail recomputes the identical
// (Sum, IntPart, Frac) triple twice, once per target field pair -- a pure,
// side-effect-free recomputation with no observable difference, so this port
// computes it once and writes both pairs (see also recalc_interpolated_frame
// above, which documents the same class of simplification).
void IVP_Core::update_time_of_last_motion()
{
    IVP_Environment* Env = this->Environment;

    // cmn-ivp3g 2026-10-09: 063470f9-06347128. Sum = offset + base stays on
    // the x87 stack; ftol truncates it; FILD + FST gives the float int part;
    // the fraction is float(Sum - int) from the unrounded Sum and the exact
    // FILD value. The second pair (0634713a-0634715d) repeats the same chain.
    const double Sum = (double)(Env->current_time.time_offset) + Env->current_time.time_base;

    const int32_t IntValue = static_cast<int32_t>(Sum); // MSVCRT ftol, truncates toward 0
    const float IntPart = (float)(IntValue);
    const float Frac = (float)(Sum - (double)(IntValue));

    // IVP_Time's members are floats: store the values, not their bit patterns
    // (an integer bit pattern assigned to a float member is converted).
    TimeOfLastMotion.time_base = IntPart;
    TimeOfLastMotion.time_offset = Frac;
    TimeOfLastLongMotion.time_base = IntPart;
    TimeOfLastLongMotion.time_offset = Frac;
}

// FUN_06346e70: Common.dll.txt:410293-410326 (decomp-tracker item 2850, 29
// insns). See the header comment above this method's declaration in
// ivp_core.h for the full 2-caller trace.
void IVP_Core::synchronize_simulation_time_snapshot(float DeltaTime, float InvDeltaTime)
{
    (void)DeltaTime; // computed by every caller, never read by retail here -- see header comment
    IDeltaTime = InvDeltaTime;

    QWorldFCoreNext = QWorldFCore; // struct copy, 4 floats (x/y/z/w)

    Speed.x = 0.0f;
    Speed.y = 0.0f;
    Speed.z = 0.0f;

    DeltaWorldFCore.x = 0.0f;
    DeltaWorldFCore.y = 0.0f;
    DeltaWorldFCore.z = 0.0f;

    MaxSurfaceDeviation = 0.0f;
    Margin = 0.0f;
    BoundingRadius = 0.0f;

    RotAxisOS.x = 1.0f;
    RotAxisOS.y = 0.0f;
    RotAxisOS.z = 0.0f;
}

// FUN_06347170: Common.dll.txt:410593-410665 (decomp-tracker item 2856, 63
// insns). See the header comment above this method's declaration in
// ivp_core.h for the full trace, including why the FUN_063454e0 call per
// object is FULLPORT-DEFERRED (blocked on item 3111/FUN_0634ebd0).
void IVP_Core::mark_active_and_sync_objects()
{
    // cmn-ivp3e 2026-10-08: re-traced .text:06347170-06347230. The invented
    // Environment / object null guards are gone (retail has none), and the
    // `current_time - delta_time` stamp is written before
    // update_time_of_last_motion as retail does, then replaced by the raw
    // current_time dwords (:063471fe-06347203).
    MovementState = (MovementState & 0xFFFFFF01u) | 1u;          // :06347177-06347182

    const IVP_Time Now = Environment->current_time;              // EDI / EBX, :0634718e-063471a1
    const double DeltaTime = Environment->delta_time;            // FLD [env+0xac]
    const int32_t DeltaInt = static_cast<int32_t>(DeltaTime);    // ftol (truncation)
    const float DeltaIntFloat = (float)(DeltaInt);    // FILD; FST float local_10
    const double DeltaFrac = DeltaTime - (double)(DeltaInt); // FSUB ST0,ST1 on the FILD value
    // FLD base; FSUB local_10; FADD offset; FSUBRP (DE E1: ST1 = ST0 - ST1).
    const double Shifted = (((double)(Now.time_base) - DeltaIntFloat) + Now.time_offset) - DeltaFrac;
    const int32_t ShiftedInt = static_cast<int32_t>(Shifted);    // ftol
    TimeOfLastPsi.time_base = (float)(ShiftedInt);    // FILD; FST float -> +0x68
    TimeOfLastPsi.time_offset = (float)(Shifted - (double)(ShiftedInt)); // -> +0x6c

    update_time_of_last_motion();                                // FUN_063470e0

    TimeOfLastPsi = Now;                                         // :063471fe-06347203

    for (int32_t Index = static_cast<int32_t>(Objects.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Real_Object* const Obj = Objects.elems[Index];
        Obj->flags = (Obj->flags & 0xFFFFFF01u) | 1u;             // :06347213-0634721f
        Obj->recheck_ov_element_as_moving();                     // FUN_063454e0, :06347222
    }
}

// FUN_063461a0 (.text:063461a0-06346286, thiscall, RET 0x10; args Lever, Lin,
// Ang, Out). cmn-ivp3f 2026-10-09, x87-exact:
//   C = Ang x Lever stays extended: Cx = Ang.y*L.z - Ang.z*L.y,
//   Cy = Ang.z*L.x - Ang.x*L.z, Cz = Ang.x*L.y - Ang.y*L.x.
//   X = (r0.z*Cz + r0.y*Cy) + r0.x*Cx      -> FSTP dword Out.x (06346221)
//   Y = (r1.z*Cz + r1.y*Cy) + r1.x*Cx      -> FSTP dword temp, MOV Out.y
//   Z = (r2.z*Cz + r2.y*Cy) + r2.x*Cx      -> FST dword Out.z, kept extended
//   Out.y = float(float(Y) + Lin.y); Out.z = float(Z + Lin.z);
//   Out.x = float(Lin.x + Out.x) -- Out.x is read back from memory (06346279).
// The rows are MWorldFCore (+0xf4 / +0x104 / +0x114).
void IVP_Core::calc_point_speed_ws(const Vector* LeverArmOS, const Vector* LinearVelocityWS,
                                    const Vector* AngularVelocityOS, Vector* OutWorldSpeed) const
{
    const double CrossX = (double)(AngularVelocityOS->y) * LeverArmOS->z -
                          (double)(AngularVelocityOS->z) * LeverArmOS->y;
    const double CrossY = (double)(AngularVelocityOS->z) * LeverArmOS->x -
                          (double)(AngularVelocityOS->x) * LeverArmOS->z;
    const double CrossZ = (double)(AngularVelocityOS->x) * LeverArmOS->y -
                          (double)(AngularVelocityOS->y) * LeverArmOS->x;

    const IVP_U_Point& Row0 = MWorldFCore.rows[0];
    const IVP_U_Point& Row1 = MWorldFCore.rows[1];
    const IVP_U_Point& Row2 = MWorldFCore.rows[2];

    OutWorldSpeed->x = (float)((Row0.z * CrossZ + Row0.y * CrossY) + Row0.x * CrossX); // 06346221
    const float RotatedY = (float)((Row1.z * CrossZ + Row1.y * CrossY) + Row1.x * CrossX); // 06346235
    OutWorldSpeed->y = RotatedY;                                                                   // 06346241
    const double RotatedZ = (Row2.z * CrossZ + Row2.y * CrossY) + Row2.x * CrossX;
    OutWorldSpeed->z = (float)(RotatedZ);                                               // 06346260 FST

    const float NewY = (float)((double)(RotatedY) + LinearVelocityWS->y);
    const float NewZ = (float)(RotatedZ + LinearVelocityWS->z);
    const float NewX = (float)((double)(LinearVelocityWS->x) + OutWorldSpeed->x);
    OutWorldSpeed->z = NewZ; // 0634627b
    OutWorldSpeed->x = NewX; // 0634627e
    OutWorldSpeed->y = NewY; // 06346280
}

// FUN_06346be0: Common.dll.txt:410012-410027. Thiscall wrapper that forwards
// this core's own Speed/RotSpeed into calc_point_speed_ws.
void IVP_Core::calc_speed_at_object_position(const Vector* LeverArmOS, Vector* OutWorldSpeed) const
{
    calc_point_speed_ws(LeverArmOS, &Speed, &RotSpeed, OutWorldSpeed);
}

// FUN_06346cf0: Common.dll.txt 410138-410194. See header comment -- CORRECTED
// 2026-09-09 (D82) to take the genuinely-separate 5th retail parameter
// (param_2=PointImpulseOS for the cross product, param_3=PointImpulseWS for
// the linear scale; RET 0x14 proves 5 stack dwords, not 4). The two scalings
// (InvMass uniform, InvRotInertia per-axis) match this class's own already
// static_assert'd +0x40 / +0x34 fields exactly.
void IVP_Core::calc_impulse_deltas(const Vector* LeverArmOS, const Vector* PointImpulseOS,
                                    const Vector* PointImpulseWS,
                                    Vector* OutLinearDelta, Vector* OutAngularDelta) const
{
    // cmn-ivp3f 2026-10-09, x87-exact (.text:06346cf0-06346d74):
    //   Cx = I.z*L.y - I.y*L.z, Cy = I.x*L.z - I.z*L.x stay extended;
    //   Cz = I.y*L.x - I.x*L.y is FSTP'd to float first (06346d25).
    //   OutAngular = (float(Cx*IRI.x), float(Cy*IRI.y), float(float(Cz)*IRI.z)),
    //   stored z, x, y; only then OutLinear = float(WS.c * InvMass), stored z, x, y.
    const double CrossX = (double)(PointImpulseOS->z) * LeverArmOS->y -
                          (double)(PointImpulseOS->y) * LeverArmOS->z;
    const double CrossY = (double)(PointImpulseOS->x) * LeverArmOS->z -
                          (double)(PointImpulseOS->z) * LeverArmOS->x;
    const float CrossZ = (float)((double)(PointImpulseOS->y) * LeverArmOS->x -
                                            (double)(PointImpulseOS->x) * LeverArmOS->y);

    const float AngularY = (float)(CrossY * InvRotInertia.y);
    OutAngularDelta->z = CrossZ * InvRotInertia.z;
    OutAngularDelta->x = (float)(CrossX * InvRotInertia.x);
    OutAngularDelta->y = AngularY;

    const float LinearY = PointImpulseWS->y * InvMass;
    const float LinearZ = PointImpulseWS->z * InvMass;
    const float LinearX = PointImpulseWS->x * InvMass;
    OutLinearDelta->z = LinearZ;
    OutLinearDelta->x = LinearX;
    OutLinearDelta->y = LinearY;
}

// FUN_06346790: Common.dll.txt:409518-409567 (decomp-tracker item 2835, 39
// insns). D89 pass (2026-09-09): traced instruction-by-instruction, confirming
// D86's hypothesis (item 3269's blocker_reason) that this is a genuinely
// DIFFERENT IVP_Core method from calc_impulse_deltas (FUN_06346cf0), even
// though it calls it internally. Sole (currently known) caller is
// IVP_Impact_Solver's FUN_06358560 (decomp-tracker item 3274, traced by D86,
// Common.dll.txt:440501-440572), which invokes
// `Cores[N]->FUN_06346790(ContactCoordsOS[N], &NormalOS, &NormalWS)` once per
// core -- NormalOS is that core's MWorldFCore-rotated (vimult'd) copy of the
// SAME world-space normal passed as NormalWS, i.e. "the same physical
// direction in two frames", exactly matching calc_impulse_deltas's own
// PointImpulseOS/PointImpulseWS convention (see that method's header comment
// above).
//
// Body (confirmed byte-for-byte against Common.dll.txt):
//   1. TEST AH,0x3 on this->Flags (mask 0x300, bits 8-9) -- NOT the 0x0C
//      dynamic/kinematic mask used everywhere else in this cluster
//      (PushContactPoint, the destructor's `(Flags & 0xC) == 4`, etc). This
//      bit pair's real meaning is not identified anywhere else in the
//      current port. When set, retail short-circuits straight to `FLD
//      DAT_063a53d0; RET` (Common.dll.txt:063467ec-063467f6) -- returns
//      1.0f without touching any of the 3 vector params. Ported faithfully
//      as an unnamed flag check rather than guessed semantics.
//   2. Otherwise: calc_impulse_deltas(LeverArmOS, ImpulseDirOS, ImpulseDirWS,
//      &LinDelta, &AngDelta) (Common.dll.txt:063467b9, forwarding all 3 of
//      this function's own params straight through), then
//      calc_point_speed_ws(LeverArmOS, &LinDelta, &AngDelta, &PointVelDelta)
//      (Common.dll.txt:063467d0) -- the exact same 2-call chain
//      IVP_Impact_Solver::PushContactPoint already uses for its own
//      TotalEffectiveMassInv accumulation (ivp_impact_solver.cpp).
//   3. UNLIKE PushContactPoint (which dots PointVelDelta against the push
//      direction), this function calls FUN_06347e60 (decomp-tracker item
//      2879, confirmed this pass: `sqrt(v.x^2+v.y^2+v.z^2)`, i.e. plain
//      Vector magnitude -- ECX=Vector*, no stack params, 55 call sites
//      across the DLL, ~16 x87-only instructions, Common.dll.txt:411949-
//      411964) on PointVelDelta alone, then returns
//      `DAT_063a53d0(=1.0f) / |PointVelDelta|` (Common.dll.txt:063467df,
//      FDIVR). Since PointVelDelta is "world-space point velocity produced
//      per unit impulse along ImpulseDir", its magnitude is 1/effective-mass,
//      so this returns a genuine per-core scalar MASS (not inverse mass) --
//      matching FUN_06358630/CalcImpulseNeeded's real reduced-mass formula
//      `2*m0*m1/(m0+m1)` (see IVP_Impact_Solver::CalcEffectiveMass's header
//      warning in ivp_impact_solver.cpp for the full writeup).
// cmn-ivp3f 2026-10-09: returns double -- retail leaves 1.0f / FUN_06347e60()
// unrounded in ST0 (FDIVR at 063467df, RET 0xc); the only caller (06358560)
// rounds with FSTP dword. FUN_06347e60 is IVP_U_Point::real_length
// (sqrt((x*x + y*y) + z*z), extended).
double IVP_Core::calc_effective_mass_along_direction(const Vector* LeverArmOS,
                                                       const Vector* ImpulseDirOS,
                                                       const Vector* ImpulseDirWS) const
{
    if ((Flags & 0x300) != 0)
        return (double)(1.0f); // DAT_063a53d0; TEST AH,0x3 at 06346798

    Vector LinDelta, AngDelta;
    calc_impulse_deltas(LeverArmOS, ImpulseDirOS, ImpulseDirWS, &LinDelta, &AngDelta); // 063467b9

    IVP_U_Point PointVelDelta;
    calc_point_speed_ws(LeverArmOS, &LinDelta, &AngDelta, reinterpret_cast<Vector*>(&PointVelDelta)); // 063467d0

    return (double)(1.0f) / PointVelDelta.real_length(); // 063467d9 / 063467df
}

// FUN_063463f0: see the header comment above calc_conservative_effective_mass
// (ivp_core.h) for the full formula/evidence trail. Verified term-by-term
// against Common.dll.txt:409276-409353 -- this function has ZERO callees, so
// every step below is directly, unambiguously portable.
//
// 2026-10-08 (claude-code:cmn-phy3), precision re-trace of .text:063463f0-
// 06346497: z*z stays on the FPU for the whole function; y*y (local_14) and
// x*x (param slot) are spilled as floats; the three terms are each stored as
// float (`FSTP float` at 06346430/0634643b/06346442); the result
// 1.0 / (Max + InvMass-or-1.0) is returned unrounded in ST0, hence double.
// Max: TermY replaces TermX only when TermY > TermX (`TEST AH,0x41; JNZ` keeps
// TermX on less/equal/unordered), then TermZ replaces it only when greater.
double IVP_Core::calc_conservative_effective_mass(const Vector* LeverArm) const
{
    const double Z2 = (double)(LeverArm->z) * LeverArm->z;
    const float Y2 = (float)((double)(LeverArm->y) * LeverArm->y);
    const float SumYZ = (float)(Y2 + Z2);
    const float X2 = (float)((double)(LeverArm->x) * LeverArm->x);
    const double SumZX = Z2 + X2;

    const float TermZ = (float)(((double)(X2) + Y2) * InvRotInertia.z);
    const float TermX = (float)((double)(SumYZ) * InvRotInertia.x);
    const float TermY = (float)(SumZX * InvRotInertia.y);

    float Max = TermX;
    if (TermY > TermX)
        Max = TermY;
    if (TermZ > Max)
        Max = TermZ;

    if ((Flags & 0x300) != 0)
        return 1.0 / ((double)(Max) + 1.0);
    return 1.0 / ((double)(Max) + InvMass);
}

// FUN_06347240: Common.dll.txt:410682-410784 (decomp-tracker item 2857, 91
// insns). A prior lane (D108, 2026-09-09) traced this partway and left it
// FULLPORT-DEFERRED (IVP_DEFERRED_RecalcCoreContactAngles in
// ivp_mindist_limiter.cpp) with 5 unexamined sub-callees. This pass traced
// every one of those 5 (plus one further transitive callee, FUN_063510c0)
// term-by-term against Common.dll.txt and found each is a byte-exact match
// for a generic primitive ALREADY implemented in ivp_math.h:
//   - FUN_06347380 (item 2858): IVP_U_Memory::GetMem, the arena that backs
//     the 0x24-byte IVP_Old_Sync_Rot_Z record below.
//   - FUN_063510c0 (item 3155, 13 insns, Common.dll.txt:428535-428552):
//     `dst.x=-src.x; dst.y=-src.y; dst.z=-src.z; dst.w=src.w` -- exactly
//     IVP_U_Quat::set_invert_unit.
//   - FUN_06351250 (item 3158, 63 insns, :428715-428787): calls
//     FUN_063510c0 on param_1 then Hamilton-multiplies the result by
//     param_2 -- exactly IVP_U_Quat::set_inv_mult_quat(param_1, param_2).
//   - FUN_06350df0 (item 3152, 152 insns, :428234-428401): dot-product +
//     epsilon branch (DAT_063a5630 = 0x3f7fbe77) between two 4-float structs,
//     acos/sin-weighted blend with a `sign` correction reusing param_1's dead
//     stack slot (Ghidra mislabels the later reads of that slot as reads of
//     "param_1" -- it is NOT the original pointer at that point, it is the
//     compiler reusing the slot for the `dot<=0 ? -1 : 1` sign scratch), else
//     a linear lerp + 2 Newton renormalization passes (DAT_063a5460=0.5,
//     DAT_063a562c=1.5) -- exactly IVP_U_Quat::set_interpolate_quat(q0, q1,
//     alpha), called here with `this` aliased to q1 (safe: the existing
//     generic method only writes x/y/z/w after all q0/q1 reads complete).
//   - FUN_06350bc0 (item 3150, 64 insns, :428019-428098): the classic
//     `1.0f-(yy+zz)` / `xy-wz` / `xy+wz` diagonal-and-cross-term matrix
//     build (DAT_063a53d0 = 1.0f confirmed byte-for-byte) -- exactly
//     IVP_U_Quat::set_matrix.
//   - FUN_0628be60 (item 772): add_mult, dst.c = float(scale*dir.c +
//     base.c), written out inline below (the operands are 12-byte Vectors).
// Every field touched (QWorldFCore +0xD4, QWorldFCoreNext +0xE4, MWorldFCore
// +0xF4 incl. its vv translation at +0x124, PosWorldFCore +0xB4,
// DeltaWorldFCore +0xC4, RotSpeed +0x94, TimeOfLastPsi +0x68, IDeltaTime
// +0x70) was already a static_assert'd, previously-verified IVP_Core field --
// no layout guessing was needed anywhere in this trace. Environment+0xF8/+0xFC
// is IVP_Environment::current_time, also already typed (ivp_environment.h),
// matching PhySys::GetCurrentTime's own documented use of the same fields
// (docs/re/common_physys.md).
//
// Retail control flow (Ipion IVP_Core::synchronize_with_rot_z; cmn-ivp3g
// 2026-10-09 re-trace of all 91 insns):
//   06347247-063472c7: FastSimUnitData = Env->vector_cores_frozen (+0xa4)
//     ->GetMem(0x24) (FUN_06347380); record +0x20 = 0; record +0x00..+0x08 =
//     RotSpeed; record +0x10 = QWorldFCoreNext (raw 16 bytes). Record +0x0c is
//     not written.
//   063472c6-063472d5: DeltaQuat = set_inv_mult_quat(&QWorldFCore,
//     &QWorldFCoreNext), unconditionally, before the interpolation below.
//   063472da-06347301: Alpha = float((((Etb - tb) + Eto) - to) * IDeltaTime)
//     (one rounding); QWorldFCoreNext = set_interpolate_quat(&QWorldFCore,
//     &QWorldFCoreNext, Alpha); set_matrix(&MWorldFCore).
//   06347306-0634732c: the same elapsed chain rounded to float WITHOUT the
//     IDeltaTime factor is the scale of add_mult(&MWorldFCore.vv,
//     &PosWorldFCore, &DeltaWorldFCore) -- DeltaWorldFCore is the linear
//     velocity, so the scale is seconds, not Alpha.
//   06347331-06347372: if (byte Flags & 0xc0) == 0: TwoIDT = float(IDT +
//     IDT); RotSpeed.c = float(_CIasin(DeltaQuat.c) * TwoIDT) for x, y, z.
// The record is arena memory (the destructor never frees +0x1b4); it is read
// by the impact system (ivp_impact.cpp) and restored by FUN_063473b0.
void IVP_Core::recalc_interpolated_frame()
{
    IVP_Environment* Env = this->Environment;

    // 06347247-063472c7: the sync record, from the environment's frozen-core
    // arena (Env+0xa4), filled before anything below changes the core.
    IVP_Old_Sync_Rot_Z* Sync =
        static_cast<IVP_Old_Sync_Rot_Z*>(Env->vector_cores_frozen->GetMem(static_cast<int>(sizeof(IVP_Old_Sync_Rot_Z))));
    FastSimUnitData = Sync;
    Sync->WasPushedDuringImpactSystem = 0;
    Sync->OldSyncRotSpeed.x = RotSpeed.x;
    Sync->OldSyncRotSpeed.y = RotSpeed.y;
    Sync->OldSyncRotSpeed.z = RotSpeed.z;
    Sync->OldSyncQWorldFCore = QWorldFCoreNext;

    // 063472c6-063472d5: computed before QWorldFCoreNext is advanced; read
    // only under the flag test at the end.
    IVP_U_Quat DeltaQuat;
    DeltaQuat.set_inv_mult_quat(&QWorldFCore, &QWorldFCoreNext);

    // 063472da-063472ee and 06347306-0634731e: the same x87 chain twice.
    const double ElapsedExt =
        (((double)(Env->current_time.time_base) - TimeOfLastPsi.time_base) + Env->current_time.time_offset) -
        TimeOfLastPsi.time_offset;
    const float Alpha = (float)(ElapsedExt * IDeltaTime);
    const float Elapsed = (float)(ElapsedExt);

    QWorldFCoreNext.set_interpolate_quat(&QWorldFCore, &QWorldFCoreNext, Alpha); // FUN_06350df0
    QWorldFCoreNext.set_matrix(&MWorldFCore);                                   // FUN_06350bc0

    // 0634732c FUN_0628be60 add_mult(&MWorldFCore.vv, &PosWorldFCore,
    // &DeltaWorldFCore, Elapsed): the velocity times the elapsed time, one
    // rounding per component.
    MWorldFCore.vv.y = (float)((double)(Elapsed) * DeltaWorldFCore.y + PosWorldFCore.y);
    MWorldFCore.vv.z = (float)((double)(Elapsed) * DeltaWorldFCore.z + PosWorldFCore.z);
    MWorldFCore.vv.x = (float)((double)(Elapsed) * DeltaWorldFCore.x + PosWorldFCore.x);

    // 06347331-06347372: TEST byte [ESI],0xc0; JNZ skip.
    if ((Flags & 0xC0) == 0)
    {
        const float TwoIDeltaTime = (float)((double)(IDeltaTime) + IDeltaTime);
        RotSpeed.x = (float)(std::asin((double)(DeltaQuat.x)) * TwoIDeltaTime);
        RotSpeed.y = (float)(std::asin((double)(DeltaQuat.y)) * TwoIDeltaTime);
        RotSpeed.z = (float)(std::asin((double)(DeltaQuat.z)) * TwoIDeltaTime);
    }
}

// FUN_063563d0 (.text:063563d0-063564f5), Ipion
// IVP_Core::grow_friction_system. cmn-ivp3d 2026-10-08, traced in full:
//   063563d6-063563e7: Result = 0; i = Objects.n_elems - 1 (read once); i < 0
//     -> return 0.
//   063563f0-06356408: per object, Syn = synapse_list_head (+0x20); per
//     synapse, Mdist = Syn + (int16)Syn->mindist_offset (+0x18) and Next =
//     Syn->next (+0x8), read before anything else.
//   0635640a-06356410: StateFlags & 0x3000 -> next synapse.
//   06356416-06356429: Other = synapse[0].l_obj->core (+0x28 -> +0x98); when
//     that is this core, synapse[1].l_obj->core (+0x44 -> +0x98).
//   0635642c-0635644b: Other fixed (`TEST byte [Other],0xc`): if this core is
//     fixed too, `TEST ESI,ESI` + slot 4 (deleting dtor) with 1, i.e.
//     `delete Mdist`; otherwise nothing.
//   0635644d-06356475: Other movable: Other->+0x5c (0633ba90) non-null -> skip;
//     06352cf0 (DispatchNarrowPhaseSolve); StateFlags & 0xc000 -> skip;
//     `FLD [Mdist+0x54]; FCOMP DAT_0658ab2c; TEST AH,1; JZ skip` -- go on only
//     when C0 is set (gap below max_distance_for_friction, or unordered).
//   06356477-063564c3: ++vector_cores_frozen->State (env +0xa4, +0x10);
//     06356bc0(Mdist, &Fs, &HavingNew, OwningSimUnit, 1); --State and
//     reset_chunks (06344070) on the arena re-read through the environment;
//     HavingNew == 1 -> Result = 1, Other->update_time_of_last_motion()
//     (063470e0).
int IVP_Core::grow_friction_system()
{
    int Result = 0;
    for (int32_t ObjIndex = static_cast<int32_t>(Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex)
    {
        IVP_Synapse_Real* Syn = Objects.elems[ObjIndex]->synapse_list_head;
        while (Syn != nullptr)
        {
            IVP_Ledge_Mindist* const Mdist = Syn->get_mindist();
            IVP_Synapse_Real* const Next = Syn->next;
            if ((Mdist->StateFlags & 0x3000u) == 0)
            {
                IVP_Core* Other = Mdist->synapse[0].l_obj->core;
                if (Other == this)
                {
                    Other = Mdist->synapse[1].l_obj->core;
                }

                if ((static_cast<uint8_t>(Other->Flags) & 0xcu) != 0)
                {
                    if ((static_cast<uint8_t>(Flags) & 0xcu) != 0)
                    {
                        delete Mdist; // `TEST ESI,ESI` + CALL [vtbl+0x10] with 1
                    }
                }
                else if (Other->ObjectHashMap == nullptr) // FUN_0633ba90
                {
                    Mdist->DispatchNarrowPhaseSolve(); // FUN_06352cf0
                    if ((Mdist->StateFlags & 0xc000u) == 0 &&
                        !(Mdist->SeparationGap >= G_IVP_MindistSettings.max_distance_for_friction))
                    {
                        ++Environment->vector_cores_frozen->State;
                        IVP_ListManager* FrictionSystem = nullptr;
                        int32_t HavingNew = 0;
                        IVP_Try_To_Generate_Managed_Friction(Mdist, &FrictionSystem, &HavingNew, OwningSimUnit, 1);
                        IVP_U_Memory* const Arena = Environment->vector_cores_frozen;
                        --Arena->State;
                        Arena->reset_chunks(); // FUN_06344070
                        if (HavingNew == 1)
                        {
                            Result = 1;
                            Other->update_time_of_last_motion(); // FUN_063470e0
                        }
                    }
                }
            }
            Syn = Next;
        }
    }
    return Result;
}

// FUN_0634b480 (.text:0634b480-0634b58d). cmn-ivp3d 2026-10-08, re-ported
// from the listing (the old body had invented Environment / object null
// guards, computed Alpha in float and returned a stubbed 0):
//   0634b488-0634b4a7: every object's flags (+0x78) low byte := 1, n-1..0.
//   0634b4ab: FUN_06347170 (mark_active_and_sync_objects).
//   0634b4b0-0634b4eb: Alpha = ((f[env+0x100] - f[env+0xf8]) + f[env+0x104])
//     - f[env+0xfc], all on the x87 stack (double here).
//   0634b4ef-0634b4fa: `FCOM DAT_063a53d8 (1e-10f); TEST AH,0x41; JNZ` --
//     fall-through (Alpha > eps, ordered) stores {(float)Alpha,
//     (float)(1.0 / Alpha)} with the UNROUNDED Alpha as divisor; the other
//     side takes {env->delta_time (+0xac), env->inv_delta_time (+0xb0)}.
//   0634b526-0634b52d: FUN_06346e70(&pair).
//   0634b532-0634b534: Result = FUN_063563d0 (grow_friction_system).
//   0634b539-0634b579: per object n-1..0: event {Environment, Obj} ->
//     FUN_0634afd0 on env->cluster_manager (+0xc); a second stack event
//     {Environment (re-read), Obj} -> FUN_0634cf30 on the environment.
int IVP_Core::revive_pass()
{
    for (int32_t ObjIndex = static_cast<int32_t>(Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex)
    {
        IVP_Real_Object* const Obj = Objects.elems[ObjIndex];
        Obj->flags = (Obj->flags & 0xFFFFFF01u) | 1u;
    }

    mark_active_and_sync_objects(); // FUN_06347170

    IVP_Environment* const Env = this->Environment;
    const double CurrentTimeBase = (double)(*reinterpret_cast<const float*>(&Env->current_time.time_base));
    const double CurrentTimeOffset = (double)(*reinterpret_cast<const float*>(&Env->current_time.time_offset));
    const double Alpha = (((double)(Env->next_psi_time_base) - CurrentTimeBase) +
                          (double)(Env->next_psi_time_offset)) - CurrentTimeOffset;

    float DeltaTime;
    float InvDeltaTime;
    if (Alpha > (double)(1.0e-10f)) // DAT_063a53d8 = 0x2edbe6ff
    {
        DeltaTime = (float)(Alpha);
        InvDeltaTime = (float)(1.0 / Alpha); // DAT_063a53d0 = 1.0f
    }
    else
    {
        DeltaTime = Env->delta_time;
        InvDeltaTime = Env->inv_delta_time;
    }
    synchronize_simulation_time_snapshot(DeltaTime, InvDeltaTime); // FUN_06346e70

    const int Result = grow_friction_system(); // FUN_063563d0

    for (int32_t ObjIndex = static_cast<int32_t>(Objects.n_elems) - 1; ObjIndex >= 0; --ObjIndex) // count read once (:0634b53b)
    {
        IVP_Real_Object* const Obj = Objects.elems[ObjIndex];
        IVP_Environment_Object_Notify_Event RevivedEvent{ this->Environment, Obj };
        this->Environment->cluster_manager->fire_event_object_revived(&RevivedEvent); // FUN_0634afd0
        IVP_Environment_Object_Notify_Event ListenerEvent{ this->Environment, Obj };
        IVP_Environment_Notify_Object_Listeners(this->Environment, &ListenerEvent);  // FUN_0634cf30
    }

    return Result;
}

// cmn-ivp3d 2026-10-08: no retail function of its own -- the call-site
// pattern `OwningSimUnit(+0x15c)->FUN_06349490(Environment(+0xc))` as
// 06347d80 :06347d8b-:06347d95 emits it. The body that used to be here was a
// second (guarded, dword-compare) copy of 06349490; the single port is
// IVP_Simulation_Unit_Revive_If_Sleeping.
void IVP_Core::revive_sleeping_core()
{
    IVP_Simulation_Unit_Revive_If_Sleeping(OwningSimUnit, Environment);
}

// FUN_06349180 (Common.dll.txt:414429-414455, decomp-tracker item 2932, 22
// insns). D-cluster-06348f60 pass (2026-09-10), ported as a direct
// dependency of this cluster's own item 2930. See ivp_core.h's own comment
// above the declaration.
void IVP_Core::register_controller(IVP_Controller* Controller)
{
    Controllers.Add(Controller);
    IVP_Simulation_Unit_Register_Core_With_Controller(OwningSimUnit, Controller, this);
}

// FUN_06349110 (Common.dll.txt:414380-414424, decomp-tracker item 2931, 37
// insns). D-cluster-06348f60 pass (2026-09-10). See ivp_core.h's own comment
// above the declaration for the full evidence trail.
void IVP_Core::detach_controller(IVP_Controller* Controller)
{
    Controllers.RemoveElement(Controller);

    // Retail: unconditional, no null-guard on OwningSimUnit -- reproduced as-is.
    IVP_Simulation_Unit_Detach_Core_From_Controller_Bucket(OwningSimUnit, Controller, this);
}

// FUN_06347c40 (Common.dll.txt:411624-411653, decomp-tracker item 2870, 19
// insns). D-target-ObjectHashMap pass (2026-09-10): full raw-disasm trace,
// closing the field-identity TODO ivp_core.h's ObjectHashMap comment left
// open (3 independent prior lanes -- items 3290/2870/3276/2629 -- had each
// confirmed +0x5c was a real, meaningful pointer without pinning its type).
//
// Calling-convention proof that ObjectHashMap really is FUN_06352930's own
// "self" container pointer (not an unrelated value carried through by
// coincidence): retail's Flags==4 branch does
//   `MOV ECX,[ECX+0x5c]; TEST ECX,ECX; JZ <null>; MOV EDX,[ESP+Key]; PUSH EDX;
//    CALL FUN_06352930`
// -- ECX (ObjectHashMap) is loaded and never touched again before the CALL,
// and FUN_06352930's own body (26 insns, Common.dll.txt:431171-431203) never
// references ECX at all: it only hashes the pushed stack key into EDX, then
// tail-calls `PUSH EDX(hash); PUSH EBP(key); CALL FUN_06352980` with ECX
// still carrying whatever the caller left in it. FUN_06352980 (the raw
// Robin-Hood find, IVP_VHash_Store::FindElem(Key, HashIndex)) is exactly HashMap063525f0's
// own "self" parameter in every other caller (FUN_06344400/FUN_06344580's
// contact-pair dispatch, decomp-tracker item 3184) -- so ObjectHashMap IS
// that same self pointer here, passed through two full function bodies via a
// dead register rather than an explicit argument.
//
// Non-hashmap (single-slot) mode read directly off Common.dll.txt:411644-
// 411653: `MOV EAX,[ECX+0x5c]; TEST EAX,EAX; JZ <null>; MOV ECX,[EAX+0x8];
// CMP ECX,[ESP+Key]; JZ <return EAX>` -- ObjectHashMap holds a single
// candidate pointer directly (no container object), and this just compares
// that candidate's own +0x8 field against Key.
//
// Reused as a generic "resolve tagged child by key" utility by unrelated
// classes elsewhere in Common.dll -- Common.dll.txt's own XREF block on
// FUN_06347c40 lists 16 call sites; decomp-tracker item 3290/FUN_06359340
// traces one such unrelated reuse over a DIFFERENT class's own +0x20 field,
// left blocked since that owner is not IVP_Core. This port only covers the
// confirmed IVP_Core call sites (e.g. FUN_06356bc0/item 3262 at
// Common.dll.txt:06356d1d). The candidate entries' own concrete C++ type was
// not determined this pass -- no call site was found that dereferences one
// beyond its +0x8 key field -- so it stays void*/unsigned rather than
// guessing a struct layout.
void* IVP_Core::find_object_hash_entry(unsigned Key) const
{
    if ((this->Flags & 0xC) == 4)
    {
        if (this->ObjectHashMap == nullptr)
            return nullptr;
        return ((IVP_VHash_Store*)this->ObjectHashMap)->FindElem((void*)Key); // FUN_06352930
    }

    void* Candidate = this->ObjectHashMap;
    if (Candidate == nullptr)
        return nullptr;

    unsigned CandidateKey = *reinterpret_cast<unsigned*>(reinterpret_cast<uintptr_t>(Candidate) + 0x8);
    return (CandidateKey == Key) ? Candidate : nullptr;
}

// FUN_06347c80 (Common.dll.txt:411669-411731, decomp-tracker item 2871, 50
// insns; SEH-wrapped in retail via the standard frame-based handler, not
// reproduced here -- nothing in this straight-line body can throw). Registers
// Candidate under the key at Candidate's own +0x8 field.
//
// Hashmap mode (Flags&0xC==4): if ObjectHashMap is still null, allocates+
// constructs it -- `operator_new(0x14)` then `FUN_063525f0(self, 2)` (an
// initial capacity of 2 buckets), Common.dll.txt:411693-411709 -- then
// inserts (key=Candidate->0x8, value=Candidate) via FUN_063526d0/item 3178
// (Common.dll.txt:411713-411718). Retail has an explicit "operator_new
// returned null -> skip the ctor call, leave ObjectHashMap null" branch
// (411699-411701, `TEST ESI,ESI; JZ`) that this port does not reproduce:
// `::operator new` throws instead of returning null, so that branch is
// unreachable here by construction, same precedent as
// ivp_surface_builder_ledge_soup.cpp's BuildNode allocation.
//
// Single-slot mode: unconditionally overwrites ObjectHashMap with Candidate
// itself (Common.dll.txt:411727), discarding whatever was previously cached
// there -- no free, since ObjectHashMap never owns the pointer in this mode
// (see find_object_hash_entry() above).
void IVP_Core::register_object_hash_entry(void* Candidate)
{
    if ((this->Flags & 0xC) == 4)
    {
        if (this->ObjectHashMap == nullptr)
        {
            // :06347caa-06347cd3: MSVCRT operator new (NULL on failure), the
            // ctor only on a non-null block, else the field gets 0.
            void* Map = ::operator new(0x14, std::nothrow);
            this->ObjectHashMap = (Map != nullptr) ? new (Map) IVP_VHash_Store(2) : nullptr; // FUN_063525f0
        }

        unsigned CandidateKey = *reinterpret_cast<unsigned*>(reinterpret_cast<uintptr_t>(Candidate) + 0x8);
        ((IVP_VHash_Store*)this->ObjectHashMap)->AddElem((void*)CandidateKey, Candidate); // FUN_063526d0
    }
    else
    {
        this->ObjectHashMap = Candidate;
    }
}

// FUN_06347d20 (Common.dll.txt:411743-411754, decomp-tracker item 2872, 12
// insns). Hashmap mode (Flags&0xC==4): removes the entry keyed by Candidate's
// own +0x8 field via FUN_063527e0/item 3178's CRC32-then-erase
// (Common.dll.txt:411747-411751). Single-slot mode: unconditionally clears
// ObjectHashMap to null (Common.dll.txt:411754, `MOV [ECX+0x5c],0`, no
// comparison) -- retail does NOT check that Candidate actually matches the
// cached single pointer first, so this port doesn't add one either.
void IVP_Core::unregister_object_hash_entry(void* Candidate)
{
    if ((this->Flags & 0xC) == 4)
    {
        unsigned CandidateKey = *reinterpret_cast<unsigned*>(reinterpret_cast<uintptr_t>(Candidate) + 0x8);
        ((IVP_VHash_Store*)this->ObjectHashMap)->RemoveElem((void*)CandidateKey); // FUN_063527e0
    }
    else
    {
        this->ObjectHashMap = nullptr;
    }
}

// FUN_06347d50 (Common.dll.txt:411767-411786, decomp-tracker item 2873, 13
// insns). D-target063555b0-followup pass (2026-09-10, traced while porting
// IVP_ListManager::MergeFrom's Phase 2 in ivp_mindist_limiter.cpp).
// thiscall(this=ECX=IVP_Core*, param_1=Candidate on stack, `MOV ESI,
// [ESP+param_1+0x4]` after the single PUSH ESI prologue). Body:
//   unregister_object_hash_entry(Candidate);           // 06347d56
//   if (Candidate != nullptr) {                        // 06347d5b/5d
//       ECX = Candidate; CALL FUN_063449d0;             // 06347d5f-d61: Clear()
//       PUSH Candidate; CALL FUN_06391d5a; ADD ESP,4;    // 06347d66-d6c: free()
//   }
// FUN_063449d0/item 2767 is the confirmed generic IVP_U_Vector<T>::Clear()
// instantiation, called here with `this` = Candidate itself (not a
// sub-member) -- every real caller of this specific release path therefore
// has an IVP_U_Vector<T>-shaped header (memsize/n_elems/elems) starting at
// its own +0x0. See IVP_Core_ObjectHashEntry in ivp_mindist_limiter.cpp for
// the one traced caller's own concrete shape (a 0xc-byte vector-plus-Key
// struct, matching this cluster's confirmed operator_new(0xc) allocation
// site).
// IVP_Core::release_object_hash_entry (FUN_06347d50) is defined in
// ivp_friction_solver.cpp: it deletes an IVP_Friction_Info_For_Core, whose
// full declaration lives in ivp_mindist.h. (The file-local IVP_Synapse_Real
// that used to collide with that header is now IVP_Synapse_Friction above.)

// FUN_063474d0 (Common.dll.txt:410941-410980, decomp-tracker item 2861, 35
// insns). D-worldtransform pass (2026-09-10): world-transform-init leaf of
// FUN_06345b20/item 2817's own construction chain (see that function's own
// header comment above IVP_InitCoreRestStateAndDamping, ivp_real_object.cpp,
// for the wider context). thiscall(this=ECX=IVP_Core*, param_1=LocalCsys on
// stack). Sole traced call site: FUN_06345b20 at Common.dll.txt:06345d9a,
// `MOV ECX,ESI(=Self->core); PUSH &local_80; CALL FUN_063474d0` -- `this`
// here really is IVP_Core* (matches item 2817's own already-confirmed
// `ESI=Self->core` binding for the rest of that function).
//
// Body (all 4 field offsets independently re-confirmed against ivp_core.h's
// own static_asserts this pass):
//   MWorldFCore.mmult4(LocalCsys, &MWorldFCore);  // Common.dll.txt:410948-410952,
//                                                  // ECX=&MWorldFCore, calls
//                                                  // FUN_063481d0/item 2890
//                                                  // (see ivp_math.h for the
//                                                  // full trace of that call)
//   PosWorldFCore = MWorldFCore.vv;               // 410953-410967 (raw
//                                                  // float/dword copy of
//                                                  // MWorldFCore+0x30's 3
//                                                  // components into
//                                                  // PosWorldFCore, i.e.
//                                                  // Core+0x124/128/12c ->
//                                                  // Core+0xb4/b8/bc)
//   QWorldFCore.set_from_matrix(&MWorldFCore);    // 410972-410987, calls
//                                                  // FUN_06350ca0/item 3151
//                                                  // (this=&QWorldFCore,
//                                                  // param_1=&MWorldFCore) --
//                                                  // see item 3151's own
//                                                  // notes: byte-exact match
//                                                  // for the already-generic
//                                                  // IVP_U_Quat::set_from_matrix
//                                                  // (branch shapes, DAT
//                                                  // constants, next_idx=
//                                                  // {1,2,0} table, and the
//                                                  // trailing FUN_06350fb0=
//                                                  // normize() call all
//                                                  // match term-for-term).
//                                                  // MWorldFCore is a full
//                                                  // IVP_U_Matrix (0x40
//                                                  // bytes) but
//                                                  // set_from_matrix only
//                                                  // reads the 0x30-byte
//                                                  // IVP_U_Matrix3 rotation
//                                                  // base, so passing it
//                                                  // straight through is
//                                                  // exact, matching retail's
//                                                  // own `CALL
//                                                  // FUN_06350ca0` with
//                                                  // param_1=&MWorldFCore
//                                                  // (Common.dll.txt:412512-
//                                                  // 06347520... i.e.
//                                                  // 06347513/0634751b).
//   QWorldFCoreNext = QWorldFCore;                 // 410988-410996, raw
//                                                  // 4-dword copy (matches
//                                                  // ivp_core.h's
//                                                  // QWorldFCore@+0xD4 /
//                                                  // QWorldFCoreNext@+0xE4).
//
// NOW WIRED into IVP_InitCoreRestStateAndDamping (D-item2785-wire pass,
// 2026-09-10): Self->vtable[2] is confirmed to be IVP_Real_Object::
// ApplyLocalCsys (LAB_06344de0, decomp-tracker item 2785), and retail's
// LocalCsys (`local_80` in FUN_06345b20) is now fully traced (identity
// rotation + a Template-override-or-surface_manager-vtable1 translation) --
// see IVP_InitCoreRestStateAndDamping's own header comment (ivp_real_object.cpp)
// for the complete evidence trail. Call order matches retail exactly: this
// method runs AFTER Self->ApplyLocalCsys(&LocalCsys) (Common.dll.txt:06345bd9
// precedes 06345d9a), with the identical (pre-inversion) LocalCsys pointer.
void IVP_Core::init_world_transform_from_local_csys(const IVP_U_Matrix* LocalCsys)
{
    MWorldFCore.mmult4(LocalCsys, &MWorldFCore); // FUN_063481d0, result == this

    PosWorldFCore.x = MWorldFCore.vv.k[0];
    PosWorldFCore.y = MWorldFCore.vv.k[1];
    PosWorldFCore.z = MWorldFCore.vv.k[2];

    QWorldFCore.set_from_matrix(&MWorldFCore);
    QWorldFCoreNext = QWorldFCore;
}

// FUN_06347410. D-rotinertia-selector pass (2026-09-10). See this method's
// own declaration comment (ivp_core.h) for why this duplicates (against real
// named fields) rather than calls common_physys.cpp's file-local
// PhySys_RecomputeInverseInertia, which is the SAME retail function ported
// for the separate PhySys reimplementation's own callers. Body re-derived
// from that existing, already-verified port (itself traced from
// Common.dll.txt:0x347410's raw bytes) -- field offsets cross-checked
// 1:1 against this class's own static_asserts (Flags@0x00,
// UpperLimitRadius@0x04, RotInertia@0x14, Mass@0x20, InvRotInertia@0x34,
// InvMass@0x40, DAngularLimitFactor@0x48).
// cmn-ivp3d 2026-10-08: re-traced .text:06347410-063474c3 term by term. The
// 1/Izz and 1/Iyy quotients stay on the x87 stack (double here) and feed the
// isotropy test unrounded; 1/Ixx is used as the float re-read from +0x34;
// the (1/Ixx - 1/Iyy) difference is rounded through a float stack local.
//   S = ((A - Cf)^2 + d3f^2) + (B - A)^2,   d3f = (float)(Cf - B)
//   M = (Cf^2 + B^2) + A^2
// with A = 1/Izz, B = 1/Iyy, Cf = (float)(1/Ixx). `FCOMPP` compares M * 0.01f
// (DAT_063a5468, 0x3c23d70b) with S; `TEST AH,0x41; JNZ` takes the clearing
// arm when M*k <= S or unordered, the fall-through (M*k > S) sets 0x40.
void IVP_Core::RecomputeInverseInertia()
{
    const double InvIzz = 1.0 / (double)(RotInertia.z); // :06347411-:06347417 (A)
    const double InvIyy = 1.0 / (double)(RotInertia.y); // :0634741a-:06347420 (B)
    const double InvIxx = 1.0 / (double)(RotInertia.x); // :06347423-:06347429
    InvRotInertia.x = (float)(InvIxx);                   // :0634742c
    InvRotInertia.y = (float)(InvIyy);                   // :0634742f
    InvRotInertia.z = (float)(InvIzz);                   // :06347434
    InvMass = (float)(1.0 / (double)(Mass));  // :06347437-:06347440

    const double InvIxxF = InvRotInertia.x;                         // :06347449 / :0634744c / :06347455 re-reads
    const double D1 = InvIyy - InvIzz;                              // :06347443-:06347445
    const double D2 = InvIzz - InvIxxF;                             // :06347447-:06347449
    const float D3 = (float)(InvIxxF - InvIyy);          // :0634744c-:06347451
    const double Spread = (D2 * D2 + (double)(D3) * D3) + D1 * D1;          // :06347458-:0634746a
    const double Magnitude = (InvIxxF * InvIxxF + InvIyy * InvIyy) + InvIzz * InvIzz;  // :0634746c-:0634747a

    if (Magnitude * (double)(0.010000001f) > Spread)     // :0634747c-:06347495
        Flags = (Flags & 0xffffff7fu) | 0x40u;
    else
        Flags = Flags & 0xffffff3fu;

    DAngularLimitFactor = (float)(0.5 / (double)(UpperLimitRadius)); // :06347497 / :063474af
}

// ============================================================================
// cmn-physys3 block (2026-10-08): the core helpers PhySys calls. x87 values that
// stay on the stack are kept in double (the CRT runs the FPU at 53 bits); every
// FSTP float is a static_cast<float>.
// ============================================================================

// FUN_06347c10 (.text:06347c10..06347c39): FLD Mass / FDIVR NewMass leaves the ratio
// on the stack; Mass = NewMass (raw dword); each RotInertia component = ratio * component
// (FMUL, FSTP float); then FUN_06347410.
void IVP_Core::set_mass(float NewMass)
{
    const double Ratio = (double)(NewMass) / Mass;     // :06347c10-:06347c17
    Mass = NewMass;                                               // :06347c1b
    RotInertia.x = (float)(Ratio * RotInertia.x);      // :06347c1e-:06347c23
    RotInertia.y = (float)(Ratio * RotInertia.y);      // :06347c26-:06347c2b
    RotInertia.z = (float)(Ratio * RotInertia.z);      // :06347c2e-:06347c31
    RecomputeInverseInertia();                                    // :06347c34
}

// FUN_06346bb0 (.text:06346bb0..06346bd0): x and y through FLD / FSTP float, z as a raw
// dword; then FUN_06347410.
void IVP_Core::set_rotation_inertia(const Vector* Inertia)
{
    RotInertia.x = Inertia->x;    // :06346bba / :06346bbe
    RotInertia.y = Inertia->y;    // :06346bb4 / :06346bc5
    RotInertia.z = Inertia->z;    // :06346bb7 / :06346bc8
    RecomputeInverseInertia();    // :06346bcb
}

// FUN_06346a00 (.text:06346a00..06346a45): K = InvMass * Magnitude on the stack; the
// three accumulators get K * Direction + SpeedChange, stored z, x, y.
void IVP_Core::push_core_linear(const Vector* Direction, float Magnitude)
{
    const double K = (double)(InvMass) * Magnitude;                    // :06346a00-:06346a07
    const float NewY = (float)(K * Direction->y + SpeedChange.y);      // :06346a0b-:06346a10
    const float NewZ = (float)(K * Direction->z + SpeedChange.z);      // :06346a16-:06346a21
    const float NewX = (float)(K * Direction->x + SpeedChange.x);      // :06346a25-:06346a33
    SpeedChange.z = NewZ;                                                         // :06346a2d
    SpeedChange.x = NewX;                                                         // :06346a39
    SpeedChange.y = NewY;                                                         // :06346a3f
}

// FUN_06346c80 (.text:06346c80..06346ce2): Local = MWorldFCore.vimult(AxisWs) (06366010 on
// core+0xf4); x * InvRotInertia.x stays on the stack, y / z products are stored as floats;
// RotSpeedChange += each * Magnitude.
void IVP_Core::push_core_angular_ws(const Vector* AxisWs, float Magnitude)
{
    IVP_U_Point In;
    In.set(AxisWs->x, AxisWs->y, AxisWs->z);
    IVP_U_Point Local;
    MWorldFCore.vimult(&In, &Local);                                              // :06346c86-:06346c96

    const double ScaledX = (double)(Local.k[0]) * InvRotInertia.x;     // :06346c9b-:06346c9f
    const float ScaledY = (float)((double)(Local.k[1]) * InvRotInertia.y); // :06346ca2-:06346ca9
    const float ScaledZ = (float)((double)(Local.k[2]) * InvRotInertia.z); // :06346cad-:06346cb4
    RotSpeedChange.x = (float)(ScaledX * Magnitude + RotSpeedChange.x);            // :06346cb8-:06346cbf
    RotSpeedChange.y = (float)((double)(ScaledY) * Magnitude + RotSpeedChange.y); // :06346cc2-:06346ccd
    RotSpeedChange.z = (float)((double)(ScaledZ) * Magnitude + RotSpeedChange.z); // :06346cd0-:06346cdb
}

// FUN_06346d80 (.text:06346d80..06346dc9): the same as 06346c80 without the transform.
void IVP_Core::push_core_angular_cs(const Vector* AxisCs, float Magnitude)
{
    const double ScaledX = (double)(InvRotInertia.x) * AxisCs->x;      // :06346d83-:06346d8a
    const float ScaledY = (float)((double)(InvRotInertia.y) * AxisCs->y); // :06346d8c-:06346d92
    const float ScaledZ = (float)((double)(InvRotInertia.z) * AxisCs->z); // :06346d96-:06346d9c
    RotSpeedChange.x = (float)(ScaledX * Magnitude + RotSpeedChange.x);            // :06346da0-:06346da7
    RotSpeedChange.y = (float)((double)(ScaledY) * Magnitude + RotSpeedChange.y); // :06346daa-:06346db5
    RotSpeedChange.z = (float)((double)(ScaledZ) * Magnitude + RotSpeedChange.z); // :06346db8-:06346dc3
}
// ---- end cmn-physys3 block ----

// ============================================================================
// D-cluster-06346800 pass (2026-09-10): 12-item cluster, Common.dll.txt:
// 409576-411849 (decomp-tracker items 2836/2838/2840/2842/2845/2849/2859/
// 2863/2867/2868/2874/2875), sitting between the DO-NOT-TOUCH ranges ending
// ~0x06339010 and starting ~0x06360000. Every item turned out to be a real
// IVP_Core method (or, for item 2874, an already-partially-triaged dispatcher
// whose two callees are now both real ported methods) -- see each method's
// own header comment above its declaration in ivp_core.h for the summary,
// and the per-method comments below for the full instruction-level trace.
// ============================================================================

// FUN_06346800: Common.dll.txt:409576-409621 (decomp-tracker item 2836, 40
// insns, zero callees). Confirmed via its sole caller (Common.dll.txt:
// 06369143, a backward walk over a Cores[] array summing this call's result
// into a running total, out of this cluster's scope) that this is a per-core
// KINETIC ENERGY term: the caller builds param_1 = Speed+SpeedChange and
// param_2 = RotSpeed+RotSpeedChange (this core's PREDICTED post-pending-
// impulse velocities) before calling, then accumulates the returned float
// across every core -- a total-system-kinetic-energy scan, likely an
// impact-solver convergence/stability check (the scanning function itself is
// out of scope). Formula (DAT_063a5460 = 0.5f, confirmed byte pattern
// 0x3f000000):
//   0.5f * (Mass*|LinearVelocity|^2 + RotInertia.x*Angular.x^2
//                                    + RotInertia.y*Angular.y^2
//                                    + RotInertia.z*Angular.z^2)
// LinearVelocity is scaled isotropically by Mass (translational KE);
// AngularVelocityOS must be in OBJECT space since RotInertia is only
// diagonal in the core's own body frame (same convention as
// calc_impulse_deltas's InvRotInertia use above).
//
// 2026-10-08 (claude-code:cmn-phy2) re-traced .text:06346800-06346864: the whole
// sum stays on the x87 stack and is returned in ST0 (the caller FUN_063690b0
// FADDs it into a float accumulator before rounding), so it is computed and
// returned in double, in retail's association order:
//   Lin = ((L.x*L.x + L.y*L.y) + L.z*L.z) * Mass          (06346828-0634683c)
//   Rot = (A.y^2*Iy + A.z^2*Iz) + A.x^2*Ix                (06346841-06346850)
//   E   = (Lin + Rot) * 0.5f (DAT_063a5460)              (06346854-06346858)
double IVP_Core::calc_kinetic_energy(const Vector* LinearVelocity, const Vector* AngularVelocityOS) const
{
    const double Ax = AngularVelocityOS->x;
    const double Ay = AngularVelocityOS->y;
    const double Az = AngularVelocityOS->z;
    const double Lx = LinearVelocity->x;
    const double Ly = LinearVelocity->y;
    const double Lz = LinearVelocity->z;

    const double Lin = ((Lx * Lx + Ly * Ly) + Lz * Lz) * (double)(Mass);
    const double Rot = ((Ay * Ay) * (double)(RotInertia.y) + (Az * Az) * (double)(RotInertia.z)) +
                       (Ax * Ax) * (double)(RotInertia.x);
    return (Lin + Rot) * (double)(0.5f);
}

// FUN_06346890 (.text:06346890-06346956, thiscall, RET 0xc), Ipion
// IVP_Core::calc_at_matrix. Sole caller 0634612a passes
// Environment->current_time's two floats (+0xF8/+0xFC) and a caller-owned
// output matrix. cmn-ivp3g 2026-10-09, traced in full:
//   06346896-063468a2: MovementState low byte as a SIGNED byte (SHL 24;
//     CMP 0x8000000; JGE) >= 8 -> copy MWorldFCore (+0xf4, REP MOVSD 0x10).
//   063468a8-063468c5: DeltaExt = ((Base - TimeOfLastPsi.time_base) + Offset)
//     - TimeOfLastPsi.time_offset on the x87 stack; FST rounds a float copy
//     into the param_1 slot; FCOMP of the UNROUNDED value against 0.0f
//     (DAT_063a53d4), TEST AH,0x40 + JNZ: equal OR unordered -> the same copy.
//   063468c7-063468f2: Alpha = float(Delta * IDeltaTime) (Delta = the float
//     copy); a local quat = set_interpolate_quat(&QWorldFCore,
//     &QWorldFCoreNext, Alpha) (FUN_06350df0), then set_matrix(OutMatrix)
//     (FUN_06350bc0).
//   063468f7-06346935: OutMatrix->vv(+0x30) = DeltaWorldFCore (+0xc4, the
//     linear velocity) * Delta + PosWorldFCore (+0xb4) -- the float elapsed
//     time, NOT Alpha; product and sum stay on the stack, one rounding each.
void IVP_Core::calc_interpolated_transform(float CurrentTimeBase, float CurrentTimeOffset,
                                            IVP_U_Matrix* OutMatrix) const
{
    if (!(static_cast<int8_t>(MovementState & 0xFFu) < 8))
    {
        std::memcpy(OutMatrix, &MWorldFCore, sizeof(IVP_U_Matrix));
        return;
    }

    const double DeltaExt = (((double)(CurrentTimeBase) - TimeOfLastPsi.time_base) + CurrentTimeOffset) -
                            TimeOfLastPsi.time_offset;
    const float Delta = (float)(DeltaExt); // 063468b6 FST

    if (!(DeltaExt < 0.0 || DeltaExt > 0.0)) // DAT_063a53d4, equal or unordered
    {
        std::memcpy(OutMatrix, &MWorldFCore, sizeof(IVP_U_Matrix));
        return;
    }

    const float Alpha = (float)((double)(Delta) * IDeltaTime);

    IVP_U_Quat InterpolatedQuat;
    InterpolatedQuat.set_interpolate_quat(&QWorldFCore, &QWorldFCoreNext, Alpha);
    InterpolatedQuat.set_matrix(OutMatrix);

    OutMatrix->vv.y = (float)((double)(Delta) * DeltaWorldFCore.y + PosWorldFCore.y);
    OutMatrix->vv.z = (float)((double)(Delta) * DeltaWorldFCore.z + PosWorldFCore.z);
    OutMatrix->vv.x = (float)((double)(Delta) * DeltaWorldFCore.x + PosWorldFCore.x);
}

// FUN_06346a50 (.text:06346a50-06346ba4, thiscall, RET 0xc), Ipion
// IVP_Core::damp_object(delta_time, rot_factor, speed_factor). cmn-ivp3g
// 2026-10-09, traced in full:
//   06346a53-06346a6f: DTx = dt*Rot.x kept on the stack; DTy, DTz the same,
//     each with an FST float copy.
//   06346a73-06346a92: Quad = ((DTz*DTz + DTy*DTy) + DTx*DTx) on the
//     unrounded values; FCOMP 0.5f (DAT_063a5460); TEST AH,0x1 + JZ: >= takes
//     the exp branch, < or unordered the linear one.
//   linear 06346a94-06346ab2: Fx = 1.0f - DTx (kept), Fy = float(1 - DTy_f),
//     Fz = float(1 - DTz_f).
//   exp 06346ab4-06346b06: the FLDL2E/F2XM1/FSCALE idiom, i.e. exp(-v):
//     Fx = exp(-DTx) kept, Fy = float(exp(-DTy_f)), Fz = float(exp(-DTz_f)).
//   06346b0c-06346b3f: S = dt*speed_factor kept; FCOM 0.9f (DAT_063a53fc);
//     TEST AH,0x1 + JZ: >= -> exp(-S), < or unordered -> 1.0f - S; kept.
//   06346b41-06346b9b: RotSpeed.y/z = float(Fy/Fz * RotSpeed.y/z), RotSpeed.x
//     = float(Fx * RotSpeed.x), Speed.x/y/z = float(Fs * Speed.c).
void IVP_Core::apply_velocity_damping(float DeltaTime, const Vector* RotDampVec, float SpeedDampValue)
{
    const double DTx = (double)(DeltaTime) * RotDampVec->x;
    const double DTyExt = (double)(DeltaTime) * RotDampVec->y;
    const float DTy = (float)(DTyExt); // 06346a64 FST
    const double DTzExt = (double)(DeltaTime) * RotDampVec->z;
    const float DTz = (float)(DTzExt); // 06346a6f FST
    const double Quad = (DTzExt * DTzExt + DTyExt * DTyExt) + DTx * DTx;

    double Fx;
    float Fy;
    float Fz;
    if (!(Quad >= 0.5)) // DAT_063a5460
    {
        Fx = 1.0 - DTx;
        Fy = (float)(1.0 - (double)(DTy));
        Fz = (float)(1.0 - (double)(DTz));
    }
    else
    {
        Fx = std::exp(-DTx);
        Fy = (float)(std::exp(-(double)(DTy)));
        Fz = (float)(std::exp(-(double)(DTz)));
    }

    const double S = (double)(DeltaTime) * SpeedDampValue;
    const double Fs = !(S >= (double)(0.9f)) ? (1.0 - S) : std::exp(-S); // DAT_063a53fc

    RotSpeed.z = (float)((double)(Fz) * RotSpeed.z);
    RotSpeed.x = (float)(Fx * RotSpeed.x);
    RotSpeed.y = (float)((double)(Fy) * RotSpeed.y);

    Speed.x = (float)(Fs * Speed.x);
    Speed.y = (float)(Fs * Speed.y);
    Speed.z = (float)(Fs * Speed.z);
}

// FUN_06346980 (.text:06346980-063469ee, thiscall, RET 4). cmn-ivp3g
// 2026-10-09, traced in full: MovementState SHL 24; CMP 0x2000000; JL -- the
// signed low byte >= 2 damps with RotSpeedDamp (+0x24..+0x2c) + 0.1f per axis
// and SpeedDamp (+0x44) + 0.1f (DAT_063a5464 = 0x3dcccccd; FLD/FADD/FSTP
// float, one rounding each); otherwise passes &RotSpeedDamp and SpeedDamp
// unchanged. Both paths call FUN_06346a50 (apply_velocity_damping) with
// DeltaTime.
void IVP_Core::damp_velocities(float DeltaTime)
{
    if (!(static_cast<int8_t>(MovementState & 0xFFu) < 2))
    {
        Vector FudgedRotDamp;
        FudgedRotDamp.x = RotSpeedDamp.x + 0.1f; // DAT_063a5464
        FudgedRotDamp.y = RotSpeedDamp.y + 0.1f;
        FudgedRotDamp.z = RotSpeedDamp.z + 0.1f;
        apply_velocity_damping(DeltaTime, &FudgedRotDamp, SpeedDamp + 0.1f);
    }
    else
    {
        apply_velocity_damping(DeltaTime, &RotSpeedDamp, SpeedDamp);
    }
}

// FUN_06346c00: Common.dll.txt:410029-410077 (decomp-tracker item 2845, 36
// insns). Thin wrapper around calc_impulse_deltas() (FUN_06346cf0, already
// ported above) that ACCUMULATES the resulting linear/angular deltas into
// this core's pending impulse accumulators (RotSpeedChange/SpeedChange,
// +0x74/+0x84) instead of returning them -- the real "apply an impulse at a
// point" entry point; commit_speed_changes() below later flushes these
// accumulators into RotSpeed/Speed and resets them to zero.
void IVP_Core::apply_impulse_at_point(const Vector* LeverArmOS, const Vector* PointImpulseOS,
                                       const Vector* PointImpulseWS)
{
    Vector LinearDelta, AngularDelta;
    calc_impulse_deltas(LeverArmOS, PointImpulseOS, PointImpulseWS, &LinearDelta, &AngularDelta);

    RotSpeedChange.x += AngularDelta.x;
    RotSpeedChange.y += AngularDelta.y;
    RotSpeedChange.z += AngularDelta.z;

    SpeedChange.x += LinearDelta.x;
    SpeedChange.y += LinearDelta.y;
    SpeedChange.z += LinearDelta.z;
}

// FUN_06346dd0: Common.dll.txt:410242-410278 (decomp-tracker item 2849, 32
// insns). Flushes the pending impulse accumulators (RotSpeedChange/
// SpeedChange) into RotSpeed/Speed, then resets both accumulators to zero --
// the commit step apply_impulse_at_point() above builds toward. 4 callers
// (Common.dll.txt XREF: 06349565/0634973a/063497db/06369183), all inside the
// revive/ensure-in-simulation family, consistent with being the standard
// "flush pending impulses" step run once per core per PSI.
void IVP_Core::commit_speed_changes()
{
    RotSpeed.x += RotSpeedChange.x;
    RotSpeed.y += RotSpeedChange.y;
    RotSpeed.z += RotSpeedChange.z;

    Speed.x += SpeedChange.x;
    Speed.y += SpeedChange.y;
    Speed.z += SpeedChange.z;

    RotSpeedChange.x = 0.0f;
    RotSpeedChange.y = 0.0f;
    RotSpeedChange.z = 0.0f;

    SpeedChange.x = 0.0f;
    SpeedChange.y = 0.0f;
    SpeedChange.z = 0.0f;
}

// FUN_06346960 (.text:06346960-0634697d, 8 insns, 2026-10-08 claude-code:cmn-phy2):
// XOR EAX,EAX then MOV [ECX+0x74/0x78/0x7c/0x84/0x88/0x8c],EAX -- zeroes
// RotSpeedChange then SpeedChange, leaving the dword at +0x80 alone. Sole caller
// the friction system's undo path FUN_06369190 (IVP_ListManager::UndoComplexPushes).
void IVP_Core::clear_speed_change()
{
    RotSpeedChange.x = 0.0f;
    RotSpeedChange.y = 0.0f;
    RotSpeedChange.z = 0.0f;
    SpeedChange.x = 0.0f;
    SpeedChange.y = 0.0f;
    SpeedChange.z = 0.0f;
}

// FUN_063473b0 (.text:063473b0-06347409, 26 insns), Ipion
// IVP_Core::undo_synchronize_rot_z. cmn-ivp3g 2026-10-09, traced in full:
//   063473b1-063473d2: RotSpeed = Sync->OldSyncRotSpeed (FLD/FSTP float for
//     x/y, a dword MOV for z).
//   063473d8-063473fa: QWorldFCoreNext = Sync->OldSyncQWorldFCore (4 dword
//     MOVs).
//   063473fd: FastSimUnitData = 0; the record is arena memory and is not freed.
// Sole caller FUN_063596b0 runs this when the record's +0x20 is 0 and does
// not null-check FastSimUnitData either.
void IVP_Core::restore_fast_sim_snapshot()
{
    const IVP_Old_Sync_Rot_Z* Sync = static_cast<const IVP_Old_Sync_Rot_Z*>(FastSimUnitData);

    RotSpeed.x = Sync->OldSyncRotSpeed.x;
    RotSpeed.y = Sync->OldSyncRotSpeed.y;
    RotSpeed.z = Sync->OldSyncRotSpeed.z;

    QWorldFCoreNext = Sync->OldSyncQWorldFCore;

    FastSimUnitData = nullptr;
}

// FUN_06347630: Common.dll.txt:411079-411100 (decomp-tracker item 2863, 16
// insns, tiny). Byte-for-byte matches the already-generic
// IVP_U_Vector<T>::Clear() (ivp_u_vector.h) run against THIS core's own
// Objects field (+0x50) -- confirmed via the exact same "elems == this+1"
// inline-buffer check the class-level Objects field comment (ivp_core.h)
// already documents from the destructor's own equivalent check (memsize@
// +0x50, n_elems@+0x52, elems@+0x54 relative to Objects, matching
// IVP_U_Vector<T>'s own field order exactly). Sole caller is a single
// tail-JMP thunk (Common.dll.txt:06397b83) with no further context
// recovered this pass.
void IVP_Core::clear_objects_vector()
{
    Objects.Clear();
}

// FUN_063478f0: Common.dll.txt:411363-411559 (decomp-tracker item 2867, 184
// insns, medium). Sole caller (Common.dll.txt:06349621, inside FUN_063495e0
// -- iterates a SimUnit's Cores[], calls this per core with
// Environment->current_time's two raw dwords, then folds the returned int
// STRAIGHT into that core's MovementState LOW BYTE via an XOR idiom
// equivalent to `MovementState = (MovementState & ~0xFF) | (Result & 0xFF)`)
// confirms this is THE per-PSI "classify this core's recent motion, update
// its Last-Motion/Last-Long-Motion checkpoints, and report a new
// MovementState low-byte value" step -- the direct producer of the 1/2/3/8
// state codes the rest of this class's already-ported methods (the
// constructor's state=8 default, mark_active_and_sync_objects' state=1,
// damp_velocities'/calc_interpolated_transform's state>=2/>=8 gates above)
// already treat as meaningful.
//
// Two independent "has this core moved enough to still be considered awake"
// checks, each comparing a squared-position-delta and a quaternion-
// similarity formula against its own pair of thresholds -- a SHORT-term pair
// (against QWorldFCoreNext/PosLastMotion/QLastMotion, DAT_063a547c ~= 1e-4 /
// DAT_063a5478 ~= 2.5e-5) and a LONG-term pair (against QWorldFCore/
// PosLastLongMotion/QLastLongMotion, DAT_063a5474 = 0.01 / DAT_063a5470 =
// 0.04, roughly 100x looser). Both checks share the exact rotation-
// similarity formula `2*(1 - dot(Qa,Qb)^2) * UpperLimitRadius^2`.
//
//   1. If EITHER short-term delta exceeds its threshold: update the
//      short-term checkpoint (QLastMotion=QWorldFCoreNext, PosLastMotion=
//      PosWorldFCore, TimeOfLastMotion=int/frac-split(CurrentTimeBase+
//      CurrentTimeOffset), same idiom as update_time_of_last_motion), THEN
//      fall through into the long-term check below (retail does NOT return
//      here).
//   2. Else (both short-term deltas small): return 2 if elapsed time since
//      the (unchanged) TimeOfLastMotion is <= Environment->default_friction
//      (+0x88 -- retail genuinely compares an elapsed TIME against this
//      field despite its own established 0.3f "friction" identity, flagged
//      as a real, faithfully-preserved oddity rather than silently
//      reinterpreted), else return 3.
//   3. Long-term check (reached only via step 1's fallthrough): if EITHER
//      long-term delta exceeds its threshold, update the long-term
//      checkpoint (QLastLongMotion=QWorldFCore, PosLastLongMotion=
//      PosWorldFCore, TimeOfLastLongMotion=int/frac-split(...)) and return 1
//      unconditionally. Else, return 1 if elapsed time since the (unchanged)
//      TimeOfLastLongMotion is <= DAT_063a546c(=4.0f, confirmed byte pattern
//      0x40800000), else return 3.
//
// Retail name/exact 1/2/3 semantics not recovered -- this port's own name,
// documenting only the verified control flow/formulas above.
int IVP_Core::classify_motion_state(float CurrentTimeBase, float CurrentTimeOffset)
{
    // cmn-ivp3g 2026-10-09: re-traced all 184 insns in x87 precision. Every
    // threshold test is `FCOMP; TEST AH,0x41; JZ` -- taken only on strictly
    // greater, so NaN never counts as movement and the elapsed tests give the
    // "recent" result (2 / 1) on NaN. Constants by their exact bits: 0.01 and
    // 0.04 are one ulp above the nearest float literal.
    const double ShortPosThresholdSq = IVP_Core_Float_From_Bits(0x38d1b717u); // DAT_063a547c
    const double ShortRotThreshold = IVP_Core_Float_From_Bits(0x37d1b717u);   // DAT_063a5478
    const double LongPosThresholdSq = IVP_Core_Float_From_Bits(0x3c23d70bu);  // DAT_063a5474
    const double LongRotThreshold = IVP_Core_Float_From_Bits(0x3d23d70bu);    // DAT_063a5470
    const double LongElapsedThreshold = IVP_Core_Float_From_Bits(0x40800000u); // DAT_063a546c, 4.0f

    // :063478f6-0634793b: ((dz*dz + dy*dy) + dx*dx) on unrounded differences.
    const double ShortDX = (double)(PosWorldFCore.x) - PosLastMotion.x;
    const double ShortDY = (double)(PosWorldFCore.y) - PosLastMotion.y;
    const double ShortDZ = (double)(PosWorldFCore.z) - PosLastMotion.z;
    bool bShortMoved = ((ShortDZ * ShortDZ + ShortDY * ShortDY) + ShortDX * ShortDX) > ShortPosThresholdSq;
    if (!bShortMoved)
    {
        // :06347941-06347996: Dot = ((Qlm.w*Qn.w + Qlm.z*Qn.z) + Qlm.y*Qn.y) + Qn.x*Qlm.x;
        // ((1 - Dot*Dot) * 2) * R * R.
        const double Dot = (((double)(QLastMotion.w) * QWorldFCoreNext.w +
                             (double)(QLastMotion.z) * QWorldFCoreNext.z) +
                            (double)(QLastMotion.y) * QWorldFCoreNext.y) +
                           (double)(QWorldFCoreNext.x) * QLastMotion.x;
        const double RotTerm = (((1.0 - Dot * Dot) + (1.0 - Dot * Dot)) * UpperLimitRadius) * UpperLimitRadius;
        bShortMoved = RotTerm > ShortRotThreshold;
    }

    if (!bShortMoved)
    {
        // :06347998-063479d1: ((t.base - last.base) + t.offset) - last.offset
        // against Environment+0x88; not greater (or unordered) -> 2, else 3.
        const double Elapsed = (((double)(CurrentTimeBase) - TimeOfLastMotion.time_base) + CurrentTimeOffset) -
                               TimeOfLastMotion.time_offset;
        return !(Elapsed > Environment->default_friction) ? 2 : 3;
    }

    // :063479d4-06347a67: short-term checkpoint. Sum = offset + base stays on
    // the stack for both the int/frac split here and the long-term one below.
    QLastMotion = QWorldFCoreNext;
    PosLastMotion.x = PosWorldFCore.x;
    PosLastMotion.y = PosWorldFCore.y;
    PosLastMotion.z = PosWorldFCore.z;
    const double Sum = (double)(CurrentTimeOffset) + CurrentTimeBase;
    const int32_t SumInt = static_cast<int32_t>(Sum); // MSVCRT ftol, truncates
    const float SumIntPart = (float)(SumInt);
    const float SumFrac = (float)(Sum - (double)(SumInt));
    TimeOfLastMotion.time_base = SumIntPart;
    TimeOfLastMotion.time_offset = SumFrac;

    // :06347a69-06347b09: the long-term pair against QWorldFCore.
    const double LongDX = (double)(PosWorldFCore.x) - PosLastLongMotion.x;
    const double LongDY = (double)(PosWorldFCore.y) - PosLastLongMotion.y;
    const double LongDZ = (double)(PosWorldFCore.z) - PosLastLongMotion.z;
    bool bLongMoved = ((LongDZ * LongDZ + LongDY * LongDY) + LongDX * LongDX) > LongPosThresholdSq;
    if (!bLongMoved)
    {
        const double Dot = (((double)(QLastLongMotion.w) * QWorldFCore.w +
                             (double)(QLastLongMotion.z) * QWorldFCore.z) +
                            (double)(QLastLongMotion.y) * QWorldFCore.y) +
                           (double)(QLastLongMotion.x) * QWorldFCore.x;
        const double RotTerm = (((1.0 - Dot * Dot) + (1.0 - Dot * Dot)) * UpperLimitRadius) * UpperLimitRadius;
        bLongMoved = RotTerm > LongRotThreshold;
    }

    if (!bLongMoved)
    {
        // :06347b0b-06347b3b: not greater than 4.0 (or unordered) -> 1, else 3.
        const double LongElapsed =
            (((double)(CurrentTimeBase) - TimeOfLastLongMotion.time_base) + CurrentTimeOffset) -
            TimeOfLastLongMotion.time_offset;
        return !(LongElapsed > LongElapsedThreshold) ? 1 : 3;
    }

    // :06347b3e-06347bc9: long-term checkpoint from the same Sum.
    QLastLongMotion = QWorldFCore;
    PosLastLongMotion.x = PosWorldFCore.x;
    PosLastLongMotion.y = PosWorldFCore.y;
    PosLastLongMotion.z = PosWorldFCore.z;
    TimeOfLastLongMotion.time_base = SumIntPart;
    TimeOfLastLongMotion.time_offset = SumFrac;
    return 1;
}

// FUN_06347be0: Common.dll.txt:411572-411595 (decomp-tracker item 2868, 18
// insns, tiny). Caches Environment->field_114 (+0x114, ivp_environment.h)
// into Unconfirmed_0x1B8 -- a REAL IVP-internals (0x0634xxxx) write to that
// field, correcting the header's own prior note that every one of its xrefs
// was outside the 0x0634xxxx range (see the updated field comment above
// Unconfirmed_0x1B8 in ivp_core.h). Then walks Objects backwards, calling
// IVP_Real_Object::recheck_mindists_of_other_cores (06344f50, cmn-phy6) on
// each attached object, last first. cmn-ivp3d 2026-10-08: the invented
// Environment / object null guards are gone (.text:06347be0-06347c0c has
// none).
void IVP_Core::notify_objects_and_cache_env_field()
{
    const uint32_t RawBits = *reinterpret_cast<const uint32_t*>(&Environment->field_114); // :06347be4-:06347be7
    Unconfirmed_0x1B8 = reinterpret_cast<void*>(static_cast<uintptr_t>(RawBits));          // :06347bef

    for (int32_t I = static_cast<int32_t>(Objects.n_elems) - 1; I >= 0; --I)
    {
        Objects.elems[I]->recheck_mindists_of_other_cores(); // FUN_06344f50
    }
}

// FUN_06347d80: Common.dll.txt:411801-411817 (decomp-tracker item 2874, 11
// insns, tiny). PORTS (previously released-without-porting per this item's
// own prior-pass notes, since both callees were unported IVP internals at
// the time) the standing "wake this core if needed" dispatcher: no-op if
// (Flags & 0xC) != 0; otherwise revive_sleeping_core() if MovementState low
// byte == 8 (static/inactive), else ensure_in_simulation(). Both callees are
// now real, already-ported IVP_Core methods (see their own declarations
// above). 4 callers (Common.dll.txt XREF: 06345589/06345590/0634663d/
// 0634bbfc): a physics-contact/connection-list walker (FUN_06345550), not
// otherwise touched by this cluster.
void IVP_Core::wake_or_ensure_simulation()
{
    if ((Flags & 0xCu) != 0)
        return;

    if ((MovementState & 0xFFu) == 8u)
        revive_sleeping_core();
    else
        ensure_in_simulation();
}

// FUN_06347db0: Common.dll.txt:411829-411849 (decomp-tracker item 2875, 15
// insns, tiny). Walks Objects backwards calling
// IVP_Real_Object::EnsureInSimulation() (FUN_06346170, already ported,
// ivp_real_object.cpp) on each attached object. Sole caller:
// Common.dll.txt:06346528.
void IVP_Core::ensure_objects_in_simulation()
{
    // .text:06347db0-06347dcd: ESI = n_elems (+0x52) - 1, JS done; ECX = elems
    // (+0x54)[ESI]; CALL 06346170; ESI--, JNS. No null test.
    for (int i = Objects.Len() - 1; i >= 0; --i)
    {
        Objects.ElementAt(i)->EnsureInSimulation();
    }
}

// ============================================================================
// FUN_0634d510 (.text:0634d510-0634d523, cdecl).
// ============================================================================
void IVP_Real_Object_Free_Cache_Object(IVP_Real_Object* Object)
{
    IVP_NarrowPhaseCacheSlot* Slot = Object->cache_object;
    if (Slot != nullptr)
    {
        Slot->Owner = nullptr;          // slot +0x08
        Object->cache_object = nullptr;
    }
}

// ============================================================================
// FUN_06347040 (.text:06347040-063470d9).
// ============================================================================
void IVP_Core::freeze_simulation_core()
{
    FUN_06346fb0(this); // stop movement

    // :0634704c-:06347065 -- current_time captured once (dword copies).
    const float NowHi = Environment->current_time.time_base;
    const float NowLo = Environment->current_time.time_offset;

    for (int32_t Index = static_cast<int32_t>(Objects.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Real_Object* Object = Objects.elems[Index];

        // :06347076-:06347082 -- movement byte = 8 (AND 0xffffff08; OR 8).
        Object->flags = (Object->flags & 0xFFFFFF08u) | 0x8u;

        // :06347085-:0634708c
        Environment->mindist_manager->RecheckOvElement(Object); // FUN_0634ebd0

        // :06347091-:063470b9 -- dt = ((now_hi - t_hi) + now_lo) - t_lo; both values advance
        // with their gradients, which are zeroed (each FMUL reads before the store).
        IVP_Hull_Manager* Hull = &Object->hull_manager;
        const double SinceLastEdit = (((double)(NowHi) - Hull->LastEditTimeHi) + NowLo) - Hull->LastEditTimeLo;
        const double OuterGradient = Hull->OuterGradient;
        Hull->OuterGradient = 0.0f;
        Hull->OuterValueAtLastEdit = (float)(SinceLastEdit * OuterGradient + Hull->OuterValueAtLastEdit);
        const double InnerGradient = Hull->InnerGradient;
        Hull->InnerGradient = 0.0f;
        Hull->InnerValueAtLastEdit = (float)(SinceLastEdit * InnerGradient + Hull->InnerValueAtLastEdit);

        // :063470bc
        Hull->DrainAndShift(); // FUN_06352220

        // :063470c1-:063470c7
        if (Object->cache_object != nullptr)
        {
            IVP_Real_Object_Free_Cache_Object(Object); // FUN_0634d510
        }
    }
}

// ============================================================================
// FUN_0634b590 (.text:0634b590-0634b5dd).
// ============================================================================
void IVP_Core::fire_event_object_frozen()
{
    // IVP_Event_Object as built on the stack twice per object: {environment, object}.
    struct FIvpEventObject
    {
        IVP_Environment* Environment; // +0x00
        IVP_Real_Object* RealObject;  // +0x04
    };

    for (int32_t Index = static_cast<int32_t>(Objects.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Real_Object* Object = Objects.elems[Index];

        // :0634b5a7-:0634b5ba -- cluster manager (env+0x0c) listeners of this
        // object, 0634af70 (IVP_Cluster_Manager::fire_event_object_frozen).
        IVP_Environment_Object_Notify_Event ClusterEvent;
        ClusterEvent.Env = Environment;
        ClusterEvent.Object = Object;
        Environment->cluster_manager->fire_event_object_frozen(&ClusterEvent);

        // :0634b5bf-:0634b5cf -- environment object listeners.
        FIvpEventObject EnvironmentEvent;
        EnvironmentEvent.Environment = Environment;
        EnvironmentEvent.RealObject = Object;
        IVP_Environment_Notify_Object_Listeners_Slot3(Environment, &EnvironmentEvent); // FUN_0634cf00
    }
}

// ============================================================================
// FUN_0634b5e0 (.text:0634b5e0-0634b5f0).
// ============================================================================
void IVP_Core::freeze_and_fire_object_frozen()
{
    freeze_simulation_core();    // FUN_06347040
    fire_event_object_frozen();  // FUN_0634b590
}

// ---- cmn-phy6 block (2026-10-08) ----------------------------------------
// FUN_06346290 (.text:06346290-06346331, thiscall, RET 8).
//   :06346296-:063462a8 W = MWorldFCore(+0xf4).vmult(RotSpeed +0x94) via
//     FUN_06348580 into a stack point; 06348580 sums rows 1 and 2 as
//     (r.x*p.x + r.z*p.z) + r.y*p.y and row 0 as (r.z*p.z + r.y*p.y) + r.x*p.x
//     and stores floats -- inlined here in that order (the generic
//     IVP_U_Matrix3::vmult sums in another order, in float).
//   :063462ad-:063462c9 D = PositionWs - MWorldFCore.vv (+0x124), kept on the
//     x87 stack.
//   :063462cf-:063462ff C = W x D: Cx = Wy*Dz - Wz*Dy and Cy = Wz*Dx - Wx*Dz are
//     stored as floats (FSTP local_10 / local_c), Cz = Wx*Dy - Wy*Dx stays on
//     the stack.
//   :06346301-:0634632b Out.z = float(Cz + Speed.z), Out.x = float(Cx + Speed.x),
//     Out.y = float(Cy + Speed.y).
void IVP_Core::get_surface_speed(const IVP_U_Point* PositionWs, IVP_U_Point* SpeedWsOut) const
{
    const IVP_U_Matrix3& M = MWorldFCore;
    // Row 0 is summed (r.z*p.z + r.y*p.y) + r.x*p.x (:063485b2-:063485cf).
    const float Wx = (float)(((double)(M.rows[0].k[2]) * RotSpeed.z + (double)(M.rows[0].k[1]) * RotSpeed.y) + (double)(M.rows[0].k[0]) * RotSpeed.x);
    const float Wy = (float)(((double)(M.rows[1].k[0]) * RotSpeed.x + (double)(M.rows[1].k[2]) * RotSpeed.z) + (double)(M.rows[1].k[1]) * RotSpeed.y);
    const float Wz = (float)(((double)(M.rows[2].k[0]) * RotSpeed.x + (double)(M.rows[2].k[2]) * RotSpeed.z) + (double)(M.rows[2].k[1]) * RotSpeed.y);

    const double Dx = (double)(PositionWs->x) - MWorldFCore.vv.x;
    const double Dy = (double)(PositionWs->y) - MWorldFCore.vv.y;
    const double Dz = (double)(PositionWs->z) - MWorldFCore.vv.z;

    const float Cx = (float)(Dz * Wy - Wz * Dy);
    const float Cy = (float)(Wz * Dx - Dz * Wx);
    const double Cz = Dy * Wx - Wy * Dx;

    SpeedWsOut->z = (float)(Cz + Speed.z);
    SpeedWsOut->x = (float)((double)(Cx) + Speed.x);
    SpeedWsOut->y = (float)((double)(Cy) + Speed.y);
}
// ---- end cmn-phy6 block ---------------------------------------------------


