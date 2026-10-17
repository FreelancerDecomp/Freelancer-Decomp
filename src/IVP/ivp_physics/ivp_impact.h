// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The Ipion impact system: Common.dll .text:06358680-063596ff (decomp-tracker
// module Common.dll). cmn-ivp3e 2026-10-08.
//
// Retail keeps no symbols here; the class and method names are the Ipion SDK's
// roles for these functions (IVP_Mindist::do_impact, IVP_Impact_Solver_Long_Term
// ::do_impact_long_term, IVP_Impact_System). The port's names for the retail
// classes these functions work on are the ones already declared in
// ivp_mindist.h:
//   IVP_Impact_Solver_Long_Term    = Ipion's name (the contact point's +0x40)
//   IVP_Contact_Point              = Ipion's name
//   IVP_ListManager                = Ipion IVP_Friction_System
//   IVP_ListManager_CorePairEntry  = Ipion IVP_Friction_Core_Pair
//   IVP_Ledge_Mindist              = Ipion IVP_Mindist

#include "ivp_mindist.h"

// The 0x24-byte record IVP_Core::FastSimUnitData (+0x1b4) points at while an
// impact system runs. FUN_06347240 allocates it from the environment's frozen
// core arena (operator new through FUN_06347380, `PUSH 0x24`) and fills it
// (.text:06347258-063472c7); FUN_063473b0 restores the core from it. Ipion's
// IVP_Old_Sync_Rot_Z.
struct IVP_Old_Sync_Rot_Z
{
    Vector OldSyncRotSpeed;        // +0x00: core RotSpeed (+0x94..+0x9c) at sync time
    float Field_0x0c;              // +0x0c: never written by 06347240; TODO
    IVP_U_Quat OldSyncQWorldFCore; // +0x10: core QWorldFCoreNext (+0xe4) at sync time
    // +0x20: 0 at allocation (.text:0634725e); FUN_063590e0 sets 1 when the
    // core is pushed by the impact system (.text:06359110-06359116);
    // FUN_063591a0 and FUN_063596b0 test it as a dword.
    int32_t WasPushedDuringImpactSystem;
};
static_assert(sizeof(IVP_Old_Sync_Rot_Z) == 0x24, "retail allocation size, .text:06347251 PUSH 0x24");
static_assert(offsetof(IVP_Old_Sync_Rot_Z, OldSyncQWorldFCore) == 0x10, "IVP_Old_Sync_Rot_Z+0x10");
static_assert(offsetof(IVP_Old_Sync_Rot_Z, WasPushedDuringImpactSystem) == 0x20, "IVP_Old_Sync_Rot_Z+0x20");

// The impact system. Retail builds it on do_impact's stack (`&local_30`,
// FUN_06358680 .text:063588d7-06358930): the three vectors are constructed with
// FUN_06344cf0(0) in member order (+0x08, +0x10, +0x18) and destroyed with
// FUN_063449d0 in reverse order (.text:06358935-0635896c; the out-of-line copy
// of that destructor sequence is FUN_06358b00). +0x00, +0x04 and +0x20 are
// only written by init_and_solve_impact_system.
class IVP_Impact_System
{
public:
    IVP_Environment* LEnvironment;                    // +0x00: Pair->CoreA->Environment
    int32_t ImpactsWhileSystem;                       // +0x04: loop guard, at most 5000
    IVP_U_Vector<IVP_Core> PushedCores;               // +0x08: cores whose speed the system changed
    IVP_U_Vector<IVP_Core> SyncedCores;               // +0x10: cores whose IVP_Old_Sync_Rot_Z the system owns
    IVP_U_Vector<IVP_ListManager_CorePairEntry> Pairs; // +0x18: core pairs taking part
    IVP_ListManager* AssociatedFs;                    // +0x20

    IVP_Impact_System()
        : PushedCores(0)
        , SyncedCores(0)
        , Pairs(0)
    {
    }

    // FUN_06358f40 (thiscall, RET 0x10).
    void init_and_solve_impact_system(IVP_Ledge_Mindist* Mindist, IVP_ListManager* Fs,
                                      IVP_ListManager_CorePairEntry* Pair, IVP_Contact_Point* Cp);

    // FUN_06358ee0 (thiscall, RET 8): re-runs the contact frame and the
    // materials of every dist of Pair except CpNot, and drops a dist from the
    // friction system when its long-term state says so.
    void recalc_pair_dists_except(IVP_ListManager_CorePairEntry* Pair, IVP_Contact_Point* CpNot);

    // FUN_063590c0 (thiscall, RET 4; `this` is not read).
    void synchronize_core_if_simulated(IVP_Core* Core);

    // FUN_063590e0 (thiscall, RET 8).
    void add_pushed_core_with_pairs_except(IVP_Core* Core, IVP_ListManager_CorePairEntry* PairNot);

    // FUN_063591a0 (thiscall, no arguments): one impact of the system; 1 when
    // an impact was done, 0 when nothing is left.
    int32_t test_loop_all_pairs();

    // FUN_06359340 (thiscall, RET 4).
    void clear_processed_state_of_core_dists(IVP_Core* Core);

    // FUN_063596b0 (thiscall, no arguments; SEH frame for the stack vector).
    void recalc_all_affected_cores();
};
static_assert(sizeof(void*) != 4 || sizeof(IVP_Impact_System) == 0x24, "do_impact's &local_30 .. local_c");

// FUN_06358680 (cdecl, plain RET): Ipion IVP_Mindist::do_impact. The only
// caller is FUN_06358cb0 (.text:06358d36).
void IVP_Mindist_Do_Impact(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* ObjA, IVP_Real_Object* ObjB);

// FUN_06358d60 (thiscall on the long-term contact data, RET 0xc): Ipion
// IVP_Impact_Solver_Long_Term::do_impact_long_term. Callers 0635886c, 063592ce.
void IVP_Impact_Solver_Long_Term_Do_Impact_Long_Term(IVP_Impact_Solver_Long_Term* LongTerm, IVP_Core* PushedCores[2],
                                                     float RescueSpeedVal, IVP_Contact_Point* Cp);

// FUN_06358c00 (stdcall-shaped, RET 0x10; ECX is set by every caller but never
// read). Callers 06358e7d, 06358eb0, 0635948e.
void IVP_Impact_Solver_Long_Term_Get_Cos_Sin_For_Impact(float FrictionVal, float OrthComp,
                                                         float* OutCos, float* OutSin);
