// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// d229:cluster-06355200 pass (2026-09-10). Common.dll.txt 0x06355200-0x06356660
// (decomp-tracker Common.dll items 3216/3219/3228/3229/3230/3231/3232/3233/
// 3234/3235/3236/3237/3238/3239/3240/3243/3245/3246/3247/3248).
//
// This cluster sits immediately after the already-ported friction-cone-clamp
// layer (ivp_contact_point.h/.cpp, items 3208-3221) and turned out to be a
// SEPARATE-but-related IVP subsystem: bookkeeping for IVP_Contact_Point
// (ivp_mindist.h) and its owning IVP_ListManager / IVP_ListManager_CorePairEntry
// (also ivp_mindist.h, ported today by concurrent lanes on this exact cluster
// -- IVP_ListManager::FindCorePairEntry/RegisterCore/MergeFrom etc, item
// 2992/3001-3006/3222/3226/3241/3280/3283/3301/3303). Evidence trail:
//
//   - FUN_06355200 (item 3216)'s "ESI" object has objects at +0x10/+0x24 and
//     tagged features at +0x18/+0x2c with intrusive-list link pairs at
//     +0x08/+0x0c and +0x1c/+0x20 immediately before each -- an EXACT match
//     to IVP_Contact_Point's Next1/Prev1/Object1/Feature1 (+0x08-0x18)
//     and Next2/Prev2/Object2/Feature2 (+0x1c-0x2c), already declared in
//     ivp_mindist.h. The unlink targets (obj->+0x28) match IVP_Real_Object::
//     mindist_list exactly.
//   - FUN_063557e0 (item 3228) and FUN_06355890 (item 3229) both read a
//     `count@+0x26/array@+0x28` header and a `count@+0x36/array@+0x38`
//     header off their own `this` -- these line up EXACTLY with
//     IVP_ListManager::RegisteredCores (+0x24, so count/array at +0x26/+0x28)
//     and IVP_ListManager::CorePairEntries (+0x34, count/array at +0x36/+0x38)
//     respectively. Both functions union-find over IVP_Core::
//     MergedCoreWhichReplacedIt (+0x1b0, via the already-ported
//     FUN_063557c0/"get_master_core", ivp_core.cpp) exactly mirroring
//     IVP_Simulation_Unit_Find_Disconnected_Core / Split_Off_Core_Group in
//     ivp_simulation_unit.cpp -- same algorithm shape, different owning class
//     (IVP_ListManager instead of IVP_Simulation_Unit).
//   - LAB_06355bb0 (item 3230) iterates `this->CorePairEntries` then each
//     entry's own `AttachedCtx` (count@2/data@4, IVP_U_Vector<IVP_Mindist_
//     PairRecord>, already declared) calling FUN_0635a160/IVP_Contact_Point::ShiftTimeStamp
//     (already ported, ivp_contact_point.cpp) on each record's +0x68/+0x6c
//     -- matching IVP_Contact_Point::_envTick68/_envTick6c exactly.
//   - FUN_06355c10 (item 3231) iterates an AttachedCtx-shaped vector, calling
//     FUN_06358c50/IVP_PopulatePairRecordIntoOwner (already ported,
//     ivp_mindist_limiter.cpp) on each record via its own Owner (+0x40,
//     already declared).
//   - FUN_06355c80/ca0/cc0 (items 3232/3233/3234) all operate on
//     IVP_ListManager::CorePairEntries (count@0x36/array@0x38) and its own
//     CoreA/CoreB (+0x28/+0x2c, already declared).
//   - FUN_063562d0 (item 3243) writes IVP_Contact_Point::MassFactor
//     (newly confirmed this pass, +0x30) using the already-ported
//     IVP_Core::calc_conservative_effective_mass (FUN_063463f0, ivp_core.cpp)
//     and IVP_ResolvePairRecordSideMaterialSlots (FUN_06358bb0, already
//     ported, ivp_mindist_limiter.cpp).
//
// Items 3235-3240 (FUN_06355d20/d60, FUN_06356000/050, FUN_063561c0) are a
// self-contained scalar-math sub-cluster (restitution/impulse-split
// formulas) operating on a byte-identical-to-retail scratch layout whose
// OWNING CLASS was not independently identified this pass (some slots hold
// EITHER a float or a reinterpreted object pointer depending on a runtime
// branch -- see IVP_PairContactAxisSetup's own comment below for why this is
// deliberately kept as a raw float array rather than a typed struct). Ported
// as free functions taking that raw buffer.
//
// Items 3245/3246/3247 (FUN_06356510/560/5c0) are the do_simulation_controller
// slots of the friction system's two embedded controller handles and live in
// ivp_friction_solver.cpp (IVP_Friction_Sys_Energy / IVP_Friction_Sys_Static,
// declared in ivp_mindist.h) since 2026-10-08.

#include "../ivp_physics/ivp_mindist.h"
#include "../ivp_physics/ivp_core.h"

// FUN_063557c0 (decomp-tracker item 3227, already ported): union-find
// "get master core" walk over IVP_Core::MergedCoreWhichReplacedIt.
// Already declared in ivp_core.h (included above) -- do not redeclare
// here with a different linkage spec (a prior redeclaration with
// `extern "C"` conflicted with ivp_core.h's plain C++ linkage, C2732).

// FUN_06355200 (item 3216). Unlinks Record from both Object1->mindist_list
// and Object2->mindist_list (IVP_Real_Object::mindist_list, already
// declared, ivp_real_object.h). Retail also releases Record's two tagged
// Feature1/Feature2 pointers through each object's surface_manager (vtable
// slot 8, raw offset +0x20) first -- that slot is not one of the two
// semantically-resolved IVP_SurfaceManager slots (0/1, see
// IVP_GetDefaultSurfaceManager's own comment in ivp_real_object.h) so this
// port leaves it FULLPORT-DEFERRED rather than guessing its effect; see the
// .cpp for the exact deferred call shape.
void IVP_UnlinkPairRecordFromObjects(IVP_Contact_Point* Record);

// FUN_063557e0 (item 3228). Union-find connectivity check over
// Manager->RegisteredCores via Manager->CorePairEntries, mirroring
// IVP_Simulation_Unit_Find_Disconnected_Core (ivp_simulation_unit.cpp)
// exactly but keyed off CorePairEntry::CoreA/CoreB instead of controller
// buckets. Returns the first Core whose union-find master differs from the
// first registered Core's master, or nullptr if every registered Core is
// still one connected component.
IVP_Core* IVP_ListManager_FindDisconnectedCore(IVP_ListManager* Manager);

// FUN_06355890 (item 3229, 278 insns, large). The ListManager-side twin of
// IVP_Simulation_Unit_Split_Off_Core_Group: peels every Core whose
// get_master_core() equals TargetMaster (and any further disconnected group
// found among the cores left behind) out of Manager into one or more
// brand-new IVP_ListManager instances (constructed via IVP_ListManager's
// already-ported ctor, FUN_0634b600/item 2992). See the .cpp for the full
// per-branch trace and the real gaps this port leaves (retail's own
// tail dispatches through 2 not-yet-materialized vtables, matching
// IVP_ListManager::MergeFrom's own already-documented FULLPORT-DEFERRED
// precedent for the exact same shared table).
void IVP_ListManager_SplitOffCoreGroup(IVP_ListManager* Manager, IVP_Core* TargetMaster);

// FUN_0635a1e0 (item 3301): the contact-frame dispatcher of one dist.
// Defined in ivp_mindist_limiter.cpp; external linkage since 2026-10-08
// because the friction controllers (ivp_friction_solver.cpp) call it too.
void IVP_UpdatePairRecordContactFrame(IVP_Contact_Point* Candidate);

// FUN_06358c50 (item 3280): read_materials_for_contact_situation -- fills
// Owner's objects/features/materials/elasticity and Candidate's friction
// factor. Defined in ivp_mindist_limiter.cpp (external linkage since
// 2026-10-08, cmn-ivp3d, for IVP_Core 063465a0 / 063464a0).
void IVP_PopulatePairRecordIntoOwner(IVP_Contact_Point* Candidate, IVP_Impact_Solver_Long_Term* Owner);

// FUN_0635a950 (item 3303): get_contact_point -- the existing record of a
// dist or a new one (*OutTag 0 = found, 1 = created). Defined in
// ivp_mindist_limiter.cpp (external linkage since 2026-10-08, cmn-ivp3d, for
// try_to_generate_managed_friction 06356bc0).
IVP_Contact_Point* IVP_FindOrCreatePairRecord(IVP_Ledge_Mindist* Mindist, int32_t* OutTag);

// FUN_06355c10 (item 3231). CORRECTED this pass: takes a SECOND real
// argument beyond Entry -- the caller (FUN_06359152, Common.dll.txt
// :0635914c-06359152) passes its own [EDI+0x20] field, which dereferences
// as an IVP_ListManager* (its own CorePairEntries.n_elems read at +0x36
// matches IVP_ListManager's already-declared layout exactly), not just
// Entry alone as originally guessed. Iterates Entry->AttachedCtx, calling
// FUN_0635a1e0/"recompute mindist" (still `partial`, no callable port yet)
// then the already-ported IVP_PopulatePairRecordIntoOwner on each record;
// when the record's own Owner->StateProcessed-area packed dword reads
// exactly 0x100 under mask 0x300, decrements the running live count and
// calls FUN_06355490/"retire from ListManager" (also still `partial`, no
// callable port yet) with the passed-in Manager. Both FULLPORT-DEFERRED
// calls are documented no-op stubs in the .cpp, cited by their raw address.
// Returns the post-decrement live count, matching retail's own return value.
// Implemented in ivp_mindist_limiter.cpp (not this cluster's own .cpp) since
// it needs that file's anonymous-namespace-local
// IVP_PopulatePairRecordIntoOwner helper, which has internal linkage there.
int IVP_ListManager_RefreshAttachedCtxAndCountLive(IVP_ListManager_CorePairEntry* Entry, IVP_ListManager* Manager);

// FUN_06355c80 (item 3232). Calls IVP_ListManager_TickCorePairBudget (below)
// on every entry in Manager->CorePairEntries.
void IVP_ListManager_TickAllCorePairBudgets(IVP_ListManager* Manager);

// FUN_06355ca0 (item 3233). Zeroes CoreB's own _field24 (see
// IVP_ListManager_CorePairEntry's own class comment, ivp_mindist.h) is NOT
// what this does -- re-read: zeroes `entry->CoreB` is wrong too; this
// actually clears CorePairEntry+0x24 (_field24, the SAME "budget" field
// IVP_ListManager_TickCorePairBudget consumes) for every entry in
// Manager->CorePairEntries. See the .cpp for the exact citation.
void IVP_ListManager_ClearAllCorePairBudgets(IVP_ListManager* Manager);

// FUN_06355cc0 (item 3234). Per-CorePairEntry budget tick: scales
// Entry->_field24 by CoreA->Environment->field_120 (already-declared
// IVP_Environment field), and -- only while the scaled budget is
// non-negative and BOTH CoreA/CoreB have a null AxisLock (IVP_Core+
// 0x10, already declared) -- consumes some of it via FUN_063561c0/
// IVP_ComputeAndApplyContactAxisImpulse (item 3240, below), accumulating the
// consumed amount into a float at IVP_Environment+0x68 (the base of
// `manager_context_68` since the 2026-09-17 split of the old 84-byte
// manager_context blob, ivp_environment.h) and subtracting it back out of
// Entry->_field24.
void IVP_ListManager_TickCorePairBudget(IVP_ListManager_CorePairEntry* Entry);

// ---------------------------------------------------------------------------
// Items 3235-3240/3243: scalar restitution/impulse-axis math sub-cluster.
// ---------------------------------------------------------------------------
// RESOLVED (d229:cluster-06355200 followup pass, 2026-09-10): slots 0x18/0x19
// (bytes 0x60/0x64) ARE `IVP_Core*` in every real path -- FUN_06355db0's own
// entry code (Common.dll.txt:06355db6-06355dd8) dereferences its raw 2nd
// argument as `*(uint32_t*)Arg` and tests bits 0xc/0x300 against it (the
// SAME "kinematic-ish core Flags" test used throughout this whole cluster,
// e.g. IVP_ListManager_FindDisconnectedCore's `(Core->Flags&0xc)!=0` gate),
// picking which of the two incoming Core*s lands in slot 0x18 vs 0x19; later
// consumers (FUN_06356050) dereference the SAME slots as `IVP_Core*` too
// (`TEST byte[Core],0xc`, then `Core+0x74`/`Core+0x84` == IVP_Core::
// RotSpeedChange/SpeedChange, already declared, ivp_core.h). No literal-float
// call site was ever found. The owning class of the REST of the buffer
// (indices 0x00-0x17 and 0x1A, an intermediate per-pair-axis scratch
// composed from IVP_Core+0x94/+0xa4/+0xa8/+0xf4 -- offsets not otherwise
// examined by this pass) was still not independently identified as a NAMED
// class, so it stays a raw float buffer (byte-exact transliteration, not a
// semantic model) rather than a guessed layout for THAT part.
//
// SIZE CORRECTED this pass: FUN_063561c0's own `SUB ESP,0x6c` (Common.dll.txt
// :063561c6) is the real total scratch extent passed as `this` to
// FUN_06355db0/FUN_06356000/FUN_06356050 -- 0x6c bytes (27 floats, indices
// 0x00-0x1A inclusive), ONE float wider than this struct's original 0x68/26-
// element size (FUN_06356000 writes index 0x1A == Setup+0x68, one past the
// old declared end -- Common.dll.txt:06356041 `FSTP float ptr [ESI+0x68]`).
struct IVP_PairContactAxisSetup
{
    float Field[0x1B]; // indices 0x00-0x1A, matching Ghidra's own param_1_00[N] indexing 1:1

    // Field[0x18]/[0x19] are IVP_Core* (see class comment above) -- accessors
    // spare every caller from repeating the reinterpret_cast.
    IVP_Core*& CoreA() { return *reinterpret_cast<IVP_Core**>(&Field[0x18]); }
    IVP_Core*& CoreB() { return *reinterpret_cast<IVP_Core**>(&Field[0x19]); }
};
static_assert(sizeof(IVP_PairContactAxisSetup) == 0x6C, "IVP_PairContactAxisSetup must match retail's 0x6C-byte scratch extent (FUN_063561c0's own SUB ESP,0x6c)");

// FUN_06355d20 (item 3235). Pure scalar math, zero external state:
// (A - sqrt(|A*A - (D+D)*(B+C)|)) * (1/(B+C)). The result is left on the x87
// stack (double here); both callers spill it as float.
double IVP_SolveContactAxisRoot(float A, float B, float C, float D);

// FUN_06355d60 (item 3236, cdecl). Pure scalar math over 5 floats, result left
// on the x87 stack (double here): with t = A/(D+E) and X = A - t*E,
// 0.5f * ((A*A*C - (t*D)^2*B) - float(X*X*C)).
double IVP_SolveContactAxisEnergySplit(float A, float B, float C, float D, float E);

// FUN_06355db0 (item 3237, 175 insns, medium). Populates Setup from the two
// participant Cores (CoreA/CoreB, resolved this pass -- see the class
// comment above): picks which of the two lands in Setup->CoreA()/CoreB()
// based on CoreB's own kinematic-ish Flags test (swap if CoreB is
// kinematic-ish, matching retail's own bit-for-bit branch), then computes a
// per-pair scratch: the normalised relative linear speed (IVP_Core::Speed,
// +0xa4), the normalised relative world-space spin (IVP_Core::RotSpeed +0x94
// through MWorldFCore +0xf4, IVP_U_Matrix3::vmult FUN_06348580) taken back
// into each core's frame (vimult FUN_06366010), the inertia-weighted lengths
// (FUN_06347e60) with their inverses, and the masses / inverse masses.
void IVP_PopulateContactAxisSetup(IVP_PairContactAxisSetup* Setup, IVP_Core* CoreA, IVP_Core* CoreB);

// FUN_06356000 (item 3238). Calls IVP_SolveContactAxisEnergySplit twice
// (once per axis-pair, Field[0x11..0x15] and Field[0x0c..0x10]) and combines
// the two results into Field[0x1a]'s neighbor slots.
void IVP_ComputeContactAxisEnergies(IVP_PairContactAxisSetup* Setup);

// FUN_06356050 (item 3239, 115 insns, medium). Applies the two energy
// results computed above as impulse contributions onto each object's own
// core (via the SAME `+0x7c/+0x84/+0x88/+0x8c` raw-offset accumulation
// pattern IVP_Contact_Point::AccumulateFrameTangent's neighbors use), gated
// on the object's own kinematic flags (`(*pbVar3 & 0xc) == 0`).
void IVP_ApplyContactAxisImpulses(IVP_PairContactAxisSetup* Setup, float Scale);

// FUN_063561c0 (item 3240). CORRECTED this pass (`this`=Entry, NOT Record --
// Common.dll.txt:063561c0-063561c3 reads `Entry->CoreB`/`Entry->CoreA`,
// IVP_ListManager_CorePairEntry's own already-declared fields at +0x2c/+0x28,
// not IVP_Contact_Point+0x2c/+0x28 which would be Feature2/Delta2 --
// confirmed independently by item 3234's own call site passing `ECX=Entry`
// unchanged straight into this function, Common.dll.txt:06355cfd).
// Orchestrates the 3 functions above: IVP_PopulateContactAxisSetup(Setup,
// Entry->CoreA, Entry->CoreB) via a local IVP_PairContactAxisSetup, then
// IVP_ComputeContactAxisEnergies, clamps Budget against a 10% energy cap
// (`Budget = min(Budget, EnergySum * 0.1f)`, DAT_063a5464 confirmed == 0.1f
// from the raw .rdata bytes this pass), and -- unless EnergySum is
// negligible (< ~1e-10, DAT_063a53d8's own raw bytes) -- calls
// IVP_ApplyContactAxisImpulses(Setup, Budget/EnergySum), returning the
// (possibly clamped) Budget, matching FUN_06355cc0/item 3234's own use of
// the return value as "amount actually consumed this tick".
float IVP_ComputeAndApplyContactAxisImpulse(IVP_ListManager_CorePairEntry* Entry, float Budget);

// FUN_063562d0 (item 3243, 85 insns, medium). Computes Record->MassFactor
// (newly confirmed field, ivp_mindist.h) as the reciprocal two-body reduced
// mass of Record->Object1->core/Object2->core (IVP_Core::
// calc_conservative_effective_mass, FUN_063463f0, already ported,
// ivp_core.cpp; when either core is kinematic-ish, only the OTHER side's
// effective mass is used), and sets Record->SolverMode to 1 when either
// side's material (IVP_ResolvePairRecordSideMaterialSlots, FUN_06358bb0) has
// IVP_Material::SecondFrictionXEnabled (+0x08) set. Implemented in
// ivp_mindist_limiter.cpp.
void IVP_ComputePairRecordMassFactor(IVP_Contact_Point* Record);

// FUN_06358bb0 (item 3278): the two sides' IVP_Material* -- the object's
// material, or the environment material manager's get_material_by_index for
// a tagged feature. Implemented in ivp_mindist_limiter.cpp.
class IVP_Material;
void IVP_ResolvePairRecordSideMaterialSlots(IVP_Contact_Point* Candidate, IVP_Material** OutArray);
