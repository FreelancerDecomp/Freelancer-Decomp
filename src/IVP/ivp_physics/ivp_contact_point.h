// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IVP per-PSI friction-impulse pass: Coulomb friction-cone clamp,
// redundant-dist merging and the two tangential reaction kernels. Retail
// Common.dll cluster 0x06353b50-0x063555a8 plus LAB_06356660 (decomp-tracker
// Common.dll items 3205-3215/3221/3248).
//
// MERGED 2026-10-08 (claude-code:cmn-phy3). This header used to carry four
// partial second declarations of retail classes the port already declares in
// full in ivp_mindist.h; every field mapped 1:1 onto the offsets there:
//   IVP_Friction_System  -> IVP_ListManager (Ipion's IVP_Friction_System)
//       SinglePendingContact +0x20 = CtxListHead, SetCount/Sets +0x36/+0x38 =
//       CorePairEntries, SinglePendingCount +0x3e = CtxListCount
//   IVP_Contact_Set      -> IVP_ListManager_CorePairEntry
//       Count/Points +0x02/+0x04 = AttachedCtx, MergeCacheCountdown +0x18,
//       FrictionCorrectionTotal +0x24 = _field24
//   IVP_Contact_Point    -> IVP_Contact_Point (then still called IVP_Mindist_PairRecord)
//       ReactionGate +0x00 = ListManagerListNext, TouchedObject(2) +0x10/+0x24
//       = Object1/Object2, NormalImpulse +0x30 = MassFactor, TangentImpulseX/Y
//       +0x38/+0x3c = SpanFrictionS[0/1], Frame +0x40 = Owner, MassFactor
//       +0x54 = NowFrictionPressure, PenetrationMargin +0x58 = GapDistance,
//       Flags +0x60 = _flags60 (SolverMode/FrictionCoefficient/
//       FrictionCorrection/TangentWork kept their names)
//   IVP_Contact_Frame    -> IVP_Impact_Solver_Long_Term (then still called IVP_Mindist)
//       Normal +0x00 = ContactNormalWS, Position +0x20 = SolverContactPointWS,
//       NormalMassFactor +0x68 = EffectiveMassDenomRecip, CoreA/CoreB
//       +0x70/+0x74 = ChildCandidate[0/1], ContactPointOS0/1 +0x98/+0xa8 =
//       CoordOS[0/1], JacobianAngularA/B +0xb8/+0xc8 = CoordOSCrossNormal[0/1]
//   IVP_Friction_Step_Params -> IVP_Event_Sim (+0 delta_time, +4
//       i_delta_time, +8 environment): LAB_06356660 is the friction system's
//       do_simulation_controller and passes its event straight down.

#include "ivp_mindist.h"

// FUN_06354c10: per-PSI Coulomb friction-cone clamp over every core pair of
// the system, then the per-dist reaction kernel.
void IVP_ClampFrictionCone(IVP_ListManager* FrictionSystem, const IVP_Event_Sim* EventSim);

// FUN_063551c0: age each pair's merge countdown by one PSI; at zero, re-merge
// its redundant dists (FUN_06355050) and reset the countdown to 5.
void IVP_AgeAndRemergeContactSets(IVP_ListManager* FrictionSystem);

// FUN_06355590 (decomp-tracker Common.dll item 3221): the two steps above,
// run back to back once per PSI.
void IVP_StepFrictionSolver(IVP_ListManager* FrictionSystem, const IVP_Event_Sim* EventSim);

// FUN_06356bc0 (thiscall on the dist, RET 0x10): Ipion's
// IVP_Mindist::try_to_generate_managed_friction. Finds or creates the dist's
// contact point; a new one is announced to the listeners, put into a friction
// system (found on either core, merged, or created) and the two cores' sim
// units are fused. *OutFrictionSystem gets the system, *OutHavingNew 1 for a
// new contact point. SimUnitNotDestroy is the sim unit that survives the
// fusion; bCallRecalcSvals re-runs the contact frame and the materials.
// cmn-ivp3d 2026-10-08.
IVP_Contact_Point* IVP_Try_To_Generate_Managed_Friction(IVP_Ledge_Mindist* Mindist,
                                                             IVP_ListManager** OutFrictionSystem,
                                                             int32_t* OutHavingNew,
                                                             IVP_Simulation_Unit* SimUnitNotDestroy,
                                                             int32_t bCallRecalcSvals);

// ---- impact-system helpers of the contact point (cmn-ivp3e 2026-10-08) ----

// FUN_06359560 (thiscall on the contact point, RET 4): Ipion
// IVP_Contact_Point::get_rescue_speed_impact. Returned on the x87 stack
// (double here); both callers store it as a float at once.
double IVP_Contact_Point_Get_Rescue_Speed_Impact(IVP_Contact_Point* Cp, IVP_Environment* Env);

// FUN_063595c0 (thiscall on the contact point, no arguments): marks the
// long-term data processed and recomputes its impact priority (+0x5c).
void IVP_Contact_Point_Calc_Impact_Priority(IVP_Contact_Point* Cp);

// FUN_06359380 (thiscall on the IVP_Impact_Solver, RET 4): the second
// friction axis of an anisotropic material -- FrictionReferenceAxis (+0xdc),
// ConeBlendScale (+0xd8) and UseFrictionConeBlend (+0xd4).
void IVP_Impact_Solver_Calc_Second_Friction(IVP_Impact_Solver* Solver, IVP_Contact_Point* Cp);

// FUN_06359d10 (thiscall on the contact point, RET 0x18): the edge / edge
// contact frame -- case 1 of FUN_0635a1e0's first switch. K and L are the
// two sides' edges (raw edge pointers, the low nibble is the edge's slot in
// its triangle), CacheK / CacheL the two sides' IVP_Cache_Ledge_Point.
// Writes the long-term normal (+0x00), contact point (+0x20) and first
// tangent axis (+0x78), the contact's gap (+0x58); the parallel-edge branch
// also zeroes *OutScratch.
class IVP_Compact_Edge;
struct IVP_Cache_Ledge_Point;
void IVP_Contact_Point_Calc_Friction_Edge_Edge(IVP_Contact_Point* Cp, const IVP_Compact_Edge* K,
                                                const IVP_Compact_Edge* L, const IVP_Cache_Ledge_Point* CacheK,
                                                const IVP_Cache_Ledge_Point* CacheL, IVP_Impact_Solver_Long_Term* LongTerm,
                                                Vector* OutScratch);
