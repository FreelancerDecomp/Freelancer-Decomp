// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_impact.h"
#include "ivp_environment.h"
#include "ivp_core.h"
#include "ivp_real_object.h"
#include "ivp_contact_point.h"                 // IVP_Try_To_Generate_Managed_Friction, the contact point helpers
#include "ivp_cluster_manager.h"               // fire_event_pre_collision (FUN_0634b030)
#include "ivp_listener.h"                      // IVP_Event_Collision
#include "ivp_mindist_settings.h"
#include "ivp_controller.h"                    // IVP_Event_Sim
#include "ivp_calc_next_psi_solver.h"
#include "../ivp_collision/ivp_listmanager_topology.h" // FUN_0635a1e0 / FUN_06358c50 / FUN_06355c10
#include <cmath>
#include <cstdint>
#include "../ivp_utility/ivp_signed_byte.h" // IVP_SignedLowByte

// The impact system, cmn-ivp3e 2026-10-08. Every function below was traced
// against Common.dll.txt instruction by instruction; the x87 convention of the
// IVP lanes holds: a value kept on the FPU stack is double here, every
// `FSTP float` is a float rounding.

namespace
{
    const float GIvpEpsilon = 0x1.b7cdfep-34f;  // DAT_063a53d8 (0x2edbe6ff, 1e-10f)
    const float GIvpOne = 1.0f;                 // DAT_063a53d0 (0x3f800000)
    const float GIvpHalf = 0.5f;                // DAT_063a5460 (0x3f000000)
    const float GIvpOneTwentyFourth = 0x1.555556p-5f; // DAT_063a5660 (0x3d2aaaab)

    // The kinetic energy of a core, as FUN_06358680 computes it inline four
    // times (.text:06358762-063587dd for Objects[0]'s core, 063587e3-06358866
    // for Objects[1]'s, 06358971-063589e1 and 063589e3-06358a5e after the
    // impact). Both instruction orders reduce to the same expression:
    //   linear     = (((v.z*v.z + v.y*v.y) + v.x*v.x) * Mass) * 0.5
    //   rotational = (((w.z*I.z)*w.z + (w.y*I.y)*w.y) + (w.x*I.x)*w.x) * 0.5
    //   result     = linear + rotational, left on the FPU stack.
    // v = Speed (+0xa4), w = RotSpeed (+0x94), I = RotInertia (+0x14), Mass (+0x20).
    double IVP_Impact_Core_Energy(const IVP_Core* Core)
    {
        const double RotIx = (double)(Core->RotSpeed.x) * Core->RotInertia.x;
        const double RotIy = (double)(Core->RotSpeed.y) * Core->RotInertia.y;
        const double RotIz = (double)(Core->RotSpeed.z) * Core->RotInertia.z;
        const double SpeedSq = ((double)(Core->Speed.z) * Core->Speed.z +
                                (double)(Core->Speed.y) * Core->Speed.y) +
                               (double)(Core->Speed.x) * Core->Speed.x;
        const double Linear = (SpeedSq * Core->Mass) * GIvpHalf;
        const double Rotational = ((RotIz * Core->RotSpeed.z + RotIy * Core->RotSpeed.y) +
                                   RotIx * Core->RotSpeed.x) * GIvpHalf;
        return Linear + Rotational;
    }
}

// ---------------------------------------------------------------------------
// FUN_06358c00 (.text:06358c00-06358c48, 25 insns, RET 0x10).
//   FLD p2; FSQRT; FADD 1.0f; FMUL p1        -> t = (sqrt(OrthComp) + 1) * FrictionVal
//   FLD ST0; FLD1; FPATAN                    -> a = atan(t / 1)
//   FLD ST0; FMULP                           -> a2 = a*a
//   FLD ST0; FMUL 0.5f; FSUBR 1.0f           -> 1 - a2*0.5
//   FLD ST1; FMUL ST2; FMUL 1/24f; FADDP     -> c = (1 - a2*0.5) + (a2*a2)*(1/24)
//   FSTP ST1; FLD ST0; FSTP [p3]             -> *OutCos = float(c)
//   FMUL ST1; FSTP [p4]                      -> *OutSin = float(c * t), c and t unrounded
// ---------------------------------------------------------------------------
void IVP_Impact_Solver_Long_Term_Get_Cos_Sin_For_Impact(float FrictionVal, float OrthComp,
                                                         float* OutCos, float* OutSin)
{
    const double TanAngle = (std::sqrt((double)(OrthComp)) + GIvpOne) * FrictionVal;
    const double Angle = std::atan(static_cast<double>(TanAngle));
    const double Angle2 = Angle * Angle;
    const double Cos = (GIvpOne - Angle2 * GIvpHalf) + (Angle2 * Angle2) * GIvpOneTwentyFourth;
    *OutCos = (float)(Cos);
    *OutSin = (float)(Cos * TanAngle);
}

// ---------------------------------------------------------------------------
// FUN_06358d60 (.text:06358d60-06358edd, 99 insns, thiscall, RET 0xc).
// ECX = the long-term contact data (ESI). The 0x110-byte IVP_Impact_Solver is
// the local at ESP+0x1c (local_110); local_120 is a negated copy of the
// normal. FUN_06273bc0(&solver+0x84, 0x10, 2, Csys::Csys) at :06358d7b is the
// MSVC vector-constructor iterator with a ctor that is `MOV EAX,ECX; RET`
// (.text:06261260): no state, nothing to port.
//   :06358d80-06358de0  contact core 1 (+0x74) null: the normal is negated
//     into local_120, Cores = {Objects[1]->core, contact core 0 (+0x70)},
//     obj points = {&CoordOS[1], &CoordOS[0]}, normal = &local_120.
//   :06358de2-06358e21  otherwise Cores = {contact core 0, or Objects[0]->core
//     when null; contact core 1}, obj points in order, normal = this.
//   :06358e28-06358e40  Restitution (+0xfc) = +0x60 (also kept in local_124),
//     the speed pointer (+0x10c) = this+0x10, UseFrictionConeBlend (+0xd4) = 0.
//   :06358e4b-06358e93  both cores' +0x10 null: FUN_06358c00(Cp->+0x44,
//     +0x60, &+0x100, &+0x104), then FUN_06359380(&solver, Cp) when the byte
//     Cp->+0x34 is non-zero. Either core's +0x10 set: FUN_06358c00(0, +0x60,
//     &+0x100, &+0x104).
//   :06358eb5-06358ed0  FUN_063578a0(&solver, PushedCores, 1,
//     MOVSX word +0x52, RescueSpeedVal).
// No null test anywhere: a null contact core 0 in the first branch is read
// through (`[ECX+0x10]`) exactly as retail does.
// ---------------------------------------------------------------------------
void IVP_Impact_Solver_Long_Term_Do_Impact_Long_Term(IVP_Impact_Solver_Long_Term* LongTerm, IVP_Core* PushedCores[2],
                                                     float RescueSpeedVal, IVP_Contact_Point* Cp)
{
    IVP_Impact_Solver ImpactSolver;
    Vector NegatedNormal;

    if (LongTerm->ChildCandidate[1] == nullptr)
    {
        NegatedNormal.x = -LongTerm->ContactNormalWS.x;
        NegatedNormal.y = -LongTerm->ContactNormalWS.y;
        NegatedNormal.z = -LongTerm->ContactNormalWS.z;
        ImpactSolver.Cores[0] = LongTerm->Objects[1]->core;
        ImpactSolver.Cores[1] = LongTerm->ChildCandidate[0];
        ImpactSolver.ContactCoordsOS[0] = &LongTerm->CoordOS[1];
        ImpactSolver.ContactCoordsOS[1] = &LongTerm->CoordOS[0];
        ImpactSolver.NormalVectorWS = &NegatedNormal;
    }
    else
    {
        IVP_Core* Core0 = LongTerm->ChildCandidate[0];
        if (Core0 == nullptr)
        {
            Core0 = LongTerm->Objects[0]->core;
        }
        ImpactSolver.Cores[0] = Core0;
        ImpactSolver.ContactCoordsOS[0] = &LongTerm->CoordOS[0];
        ImpactSolver.Cores[1] = LongTerm->ChildCandidate[1];
        ImpactSolver.ContactCoordsOS[1] = &LongTerm->CoordOS[1];
        ImpactSolver.NormalVectorWS = &LongTerm->ContactNormalWS;
    }

    const float Elasticity = LongTerm->ContactElasticity;
    ImpactSolver.Restitution = Elasticity;
    // +0x10 of the long-term data is Ipion's contact-situation speed.
    ImpactSolver.FrictionVectorWS = &LongTerm->Speed;
    ImpactSolver.UseFrictionConeBlend = 0;

    if (ImpactSolver.Cores[0]->AxisLock == nullptr && ImpactSolver.Cores[1]->AxisLock == nullptr)
    {
        IVP_Impact_Solver_Long_Term_Get_Cos_Sin_For_Impact(Cp->FrictionCoefficient, Elasticity,
                                                            &ImpactSolver.NormalCoeff, &ImpactSolver.TangentCoeff);
        if (Cp->SolverMode != 0)
        {
            IVP_Impact_Solver_Calc_Second_Friction(&ImpactSolver, Cp); // FUN_06359380
        }
    }
    else
    {
        IVP_Impact_Solver_Long_Term_Get_Cos_Sin_For_Impact(0.0f, Elasticity,
                                                            &ImpactSolver.NormalCoeff, &ImpactSolver.TangentCoeff);
    }

    ImpactSolver.DoImpactSystem(PushedCores, 1, static_cast<int32_t>(LongTerm->Status), RescueSpeedVal); // FUN_063578a0
}

// ---------------------------------------------------------------------------
// FUN_06358680 (.text:06358680-06358aff, 293 insns, cdecl, plain RET). EH frame
// LAB_06398148 for the three impact-system vectors.
//   :06358698-063586d6  SimUnitNotDestroy = (int)(ObjA->flags << 24) <
//     0x08000000 ? ObjB->core->+0x15c : ObjA->core->+0x15c (signed compare of
//     the low flags byte).
//   :063586dd-063586ec  Cp = FUN_06356bc0(Mindist, &Fs, &HavingNew,
//     SimUnitNotDestroy, 1); LongTerm = Cp->+0x40 (EBX).
//   :063586fe-06358704  Pair = FUN_06356280(Fs, ObjA->core, ObjB->core).
//   :0635870b-0635874c  Env = Mindist->synapse[0].l_obj (+0x28)->environment;
//     the pair's last impact time (+0x1c/+0x20) is saved and replaced by
//     Env->current_time.
//   :0635873b-06358755  RescueSpeed = float(FUN_06359560(Cp, ObjA->environment)).
//   :06358759-06358866  energies before, float (local_74 / local_70).
//   :0635886c           FUN_06358d60(LongTerm, &PushedCores, RescueSpeed, Cp).
//   :06358871-063588d5  LongTerm speed (+0x10..+0x18) saved, negated when
//     ObjB->core has (Flags & 0xc).
//   :063588d7-0635896c  impact system on the stack; FUN_06358f40(&System,
//     Mindist, Fs, Pair, Cp); its three vectors destroyed.
//   :06358971-06358a73  +0x48 = float(E0' - float E0) with E0' unrounded,
//     +0x4c = float(float E1' - float E1).
//   :06358a76-06358aa4  event {dt, Env, LongTerm}, dt = ((now.base -
//     old.base) + now.offset) - old.offset; the saved speed is written back.
//   :06358aa7-06358ae2  FUN_0634cf60(Env, &Event); for ObjA then ObjB with
//     flags bit 0x2000: FUN_0634b030(Env->cluster_manager, Obj, &Event).
// ---------------------------------------------------------------------------
void IVP_Mindist_Do_Impact(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* ObjA, IVP_Real_Object* ObjB)
{
    IVP_Core* const CoreB = ObjB->core;
    IVP_Core* const CoreA = ObjA->core;
    IVP_Simulation_Unit* const SimUnitNotDestroy =
        (IVP_SignedLowByte(ObjA->flags) < 8) ? CoreB->OwningSimUnit : CoreA->OwningSimUnit;

    IVP_ListManager* Fs;
    int32_t HavingNewDist;
    IVP_Contact_Point* const Cp =
        IVP_Try_To_Generate_Managed_Friction(Mindist, &Fs, &HavingNewDist, SimUnitNotDestroy, 1); // FUN_06356bc0
    IVP_Impact_Solver_Long_Term* const LongTerm = Cp->Owner;

    IVP_ListManager_CorePairEntry* const Pair = Fs->FindCorePairEntryExact(CoreA, CoreB); // FUN_06356280

    IVP_Environment* const Env = Mindist->synapse[0].l_obj->environment;
    // +0x1c/+0x20 of the core pair is an IVP_Time (Ipion last_impact_time).
    IVP_Time* const LastImpactTime = &Pair->LastImpactTime;
    const IVP_Time OldTime = *LastImpactTime;
    const IVP_Time NowTime = Env->current_time;
    *LastImpactTime = NowTime;

    const float RescueSpeed =
        (float)(IVP_Contact_Point_Get_Rescue_Speed_Impact(Cp, ObjA->environment)); // FUN_06359560

    IVP_Core* const Core0 = LongTerm->Objects[0]->core;
    IVP_Core* const Core1 = LongTerm->Objects[1]->core;
    const float EnergyBefore0 = (float)(IVP_Impact_Core_Energy(Core0));
    const float EnergyBefore1 = (float)(IVP_Impact_Core_Energy(Core1));

    IVP_Core* PushedCores[2];
    IVP_Impact_Solver_Long_Term_Do_Impact_Long_Term(LongTerm, PushedCores, RescueSpeed, Cp); // FUN_06358d60

    Vector SavedSpeed = LongTerm->Speed;
    if ((CoreB->Flags & 0xcu) != 0)
    {
        SavedSpeed.x = -SavedSpeed.x;
        SavedSpeed.y = -SavedSpeed.y;
        SavedSpeed.z = -SavedSpeed.z;
    }

    {
        IVP_Impact_System ImpactSystem;
        ImpactSystem.init_and_solve_impact_system(Mindist, Fs, Pair, Cp); // FUN_06358f40
    }

    const double EnergyAfter0 = IVP_Impact_Core_Energy(Core0);
    const float EnergyAfter1 = (float)(IVP_Impact_Core_Energy(Core1));
    LongTerm->_field48 = (float)(EnergyAfter0 - EnergyBefore0);
    LongTerm->_field4c = (float)((double)(EnergyAfter1) - EnergyBefore1);

    IVP_Event_Collision Event;
    Event.d_time_since_last_collision = (float)(
        (((double)(NowTime.time_base) - OldTime.time_base) + NowTime.time_offset) - OldTime.time_offset);
    Event.environment = Env;
    Event.contact_situation = reinterpret_cast<IVP_Contact_Situation*>(LongTerm);
    LongTerm->Speed = SavedSpeed;

    IVP_Environment_Notify_Listeners_C8(Env, &Event); // FUN_0634cf60
    if ((ObjA->flags & 0x2000u) != 0)
    {
        Env->cluster_manager->fire_event_pre_collision(ObjA, &Event); // FUN_0634b030
    }
    if ((ObjB->flags & 0x2000u) != 0)
    {
        Env->cluster_manager->fire_event_pre_collision(ObjB, &Event);
    }
}

// ---------------------------------------------------------------------------
// FUN_06358f40 (.text:06358f40-0635907e, 120 insns, RET 0x10).
//   :06358f41-06358f7b  Core0 = Cp->Object1->core, Core1 = Cp->Object2->core;
//     ImpactsWhileSystem = 0; AssociatedFs = Fs; LEnvironment =
//     Pair->CoreA->Environment.
//   :06358f7d-06358f8f  Core0 non-null and neither (Flags & 0xc) nor
//     (Flags & 0x300): add_pushed_core_with_pairs_except(Core0, Pair).
//   :06358f94-06358fc7  Pair->CoreA dynamic in the same sense: SyncedCores.add
//     (FUN_063441a0 grows when n_elems >= memsize).
//   :06358fcb-06358fde  Core1 likewise; :06358fe3-06359012 Pair->CoreB likewise.
//   :06359016-06359026  Pairs.add(Pair) (FUN_06359090); FUN_06358ee0(Pair, Cp).
//   :0635902b-06359056  while test_loop_all_pairs() == 1: ++Count;
//     ++ImpactsWhileSystem; past 5000 (`JG`) Mindist's slot 4 (the deleting
//     destructor) with 1 when Mindist is non-null, then leave the loop.
//   :06359067-06359074  LEnvironment->+0x50 += Count + 1;
//     recalc_all_affected_cores().
// ---------------------------------------------------------------------------
void IVP_Impact_System::init_and_solve_impact_system(IVP_Ledge_Mindist* Mindist, IVP_ListManager* Fs,
                                                     IVP_ListManager_CorePairEntry* Pair,
                                                     IVP_Contact_Point* Cp)
{
    IVP_Core* const Core0 = Cp->Object1->core;
    IVP_Core* const Core1 = Cp->Object2->core;
    ImpactsWhileSystem = 0;
    AssociatedFs = Fs;
    LEnvironment = Pair->CoreA->Environment;

    if (Core0 != nullptr && (Core0->Flags & 0xcu) == 0 && (Core0->Flags & 0x300u) == 0)
    {
        add_pushed_core_with_pairs_except(Core0, Pair); // FUN_063590e0
    }
    IVP_Core* const PairCoreA = Pair->CoreA;
    if ((PairCoreA->Flags & 0xcu) == 0 && (PairCoreA->Flags & 0x300u) == 0)
    {
        SyncedCores.Add(PairCoreA);
    }
    if (Core1 != nullptr && (Core1->Flags & 0xcu) == 0 && (Core1->Flags & 0x300u) == 0)
    {
        add_pushed_core_with_pairs_except(Core1, Pair);
    }
    IVP_Core* const PairCoreB = Pair->CoreB;
    if ((PairCoreB->Flags & 0xcu) == 0 && (PairCoreB->Flags & 0x300u) == 0)
    {
        SyncedCores.Add(PairCoreB);
    }

    Pairs.Add(Pair);                     // FUN_06359090
    recalc_pair_dists_except(Pair, Cp);  // FUN_06358ee0

    int32_t Count = 0;
    if (test_loop_all_pairs() == 1) // FUN_063591a0
    {
        for (;;)
        {
            ++Count;
            ++ImpactsWhileSystem;
            if (ImpactsWhileSystem > 5000)
            {
                if (Mindist != nullptr)
                {
                    delete Mindist; // `MOV EDX,[ECX]; PUSH 1; CALL [EDX+0x10]`
                }
                break;
            }
            if (test_loop_all_pairs() != 1)
            {
                break;
            }
        }
    }

    LEnvironment->counter_50 += Count + 1; // .text:06359067-0635906f
    recalc_all_affected_cores(); // FUN_063596b0
}

// ---------------------------------------------------------------------------
// FUN_06358ee0 (.text:06358ee0-06358f3a, 37 insns, RET 8). this is spilled
// to local_4 and read back only for AssociatedFs (+0x20). n_elems of the
// pair's dist vector is read once (zero-extended word), elems every pass.
// For every dist except CpNot, last first: FUN_0635a1e0(Cp); LongTerm =
// Cp->+0x40 (read after that call); FUN_06358c50(Cp, LongTerm); when
// (LongTerm->dword+0x54 & 0x300) == 0x100: FUN_06355490(AssociatedFs, Cp).
// ---------------------------------------------------------------------------
void IVP_Impact_System::recalc_pair_dists_except(IVP_ListManager_CorePairEntry* Pair,
                                                 IVP_Contact_Point* CpNot)
{
    for (int32_t Index = static_cast<int32_t>(Pair->AttachedCtx.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Contact_Point* const Cp = Pair->AttachedCtx.elems[Index];
        if (Cp == CpNot)
        {
            continue;
        }
        IVP_UpdatePairRecordContactFrame(Cp);            // FUN_0635a1e0
        IVP_Impact_Solver_Long_Term* const LongTerm = Cp->Owner;
        IVP_PopulatePairRecordIntoOwner(Cp, LongTerm);   // FUN_06358c50
        if ((LongTerm->FrictionIsBroken & 0x3u) == 0x1u)
        {
            AssociatedFs->RemoveDistFromSystem(Cp);      // FUN_06355490
        }
    }
}

// ---------------------------------------------------------------------------
// FUN_063590c0 (.text:063590c0-063590d6, 7 insns, RET 4). ECX is replaced by
// the argument before use. `SHL EAX,0x18; CMP EAX,0x8000000; JGE` -- the core
// is synchronised when the signed low byte of MovementState (+0x60) is below 8.
// ---------------------------------------------------------------------------
void IVP_Impact_System::synchronize_core_if_simulated(IVP_Core* Core)
{
    if (IVP_SignedLowByte(Core->MovementState) < 8)
    {
        Core->recalc_interpolated_frame(); // FUN_06347240 (Ipion synchronize_with_rot_z)
    }
}

// ---------------------------------------------------------------------------
// FUN_063590e0 (.text:063590e0-0635916a, 54 insns, RET 8).
//   :063590e6-0635910c  PushedCores.add(Core).
//   :06359110-06359116  Core->+0x1b4->WasPushedDuringImpactSystem = 1.
//   :0635911d-06359164  n = AssociatedFs->CorePairEntries.n_elems (read once),
//     last first, elems re-read from AssociatedFs every pass: a pair that has
//     Core as CoreA or CoreB, is not PairNot, is not already in Pairs
//     (FUN_06359170) and whose FUN_06355c10(Pair, AssociatedFs) is > 0 is
//     added to Pairs (FUN_06359090).
// ---------------------------------------------------------------------------
void IVP_Impact_System::add_pushed_core_with_pairs_except(IVP_Core* Core, IVP_ListManager_CorePairEntry* PairNot)
{
    PushedCores.Add(Core);
    static_cast<IVP_Old_Sync_Rot_Z*>(Core->FastSimUnitData)->WasPushedDuringImpactSystem = 1;

    for (int32_t Index = static_cast<int32_t>(AssociatedFs->CorePairEntries.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_ListManager_CorePairEntry* const Pair = AssociatedFs->CorePairEntries.elems[Index];
        if (Pair->CoreA != Core && Pair->CoreB != Core)
        {
            continue;
        }
        if (Pair == PairNot)
        {
            continue;
        }
        if (Pairs.Contains(Pair)) // FUN_06359170
        {
            continue;
        }
        if (IVP_ListManager_RefreshAttachedCtxAndCountLive(Pair, AssociatedFs) > 0) // FUN_06355c10
        {
            Pairs.Add(Pair); // FUN_06359090
        }
    }
}

// ---------------------------------------------------------------------------
// FUN_063591a0 (.text:063591a0-06359332, 144 insns, no arguments).
//   :063591a3-063591c7  Best = G_IVP_MindistSettings+0x108 (DAT_0658ab18),
//     BestCp = BestPair = null; n = Pairs.n_elems read once; empty -> 0.
//   :063591cd-06359203  per pair (last first, elems re-read): the pair is
//     skipped when the two cores' packed state bits intersect:
//       B = (int)((CoreB->Flags & ~3) << 28) >> 6 | (CoreB->MovementState & ~0xff) << 16
//       A = (int)(CoreA->Flags << 28) >> 6        | CoreA->MovementState << 16
//     (`SHL 0x1c; SAR 6` on both flags words, `TEST EAX,EBP; JNZ`).
//   :06359205-0635924c  per dist of the pair (last first): a long-term state
//     byte (+0x54) other than 1 -> ++LEnvironment->+0x60 and FUN_063595c0(Cp);
//     then `FLD +0x5c; FCOMP Best; TEST AH,1; JZ` -- the dist becomes the best
//     one when its priority is below Best (or unordered).
//   :0635925d-0635925f  no best dist -> 0.
//   :06359265-063592b8  for the best dist's contact core 1 then 0 (+0x74,
//     +0x70, re-read through Cp->+0x40 each time): a non-null core whose
//     +0x1b4 is null is added to SyncedCores and synchronised (FUN_063590c0).
//   :063592ba-063592ce  ++LongTerm->+0x52; FUN_06358d60(LongTerm,
//     &PushedCores, LongTerm->+0x58, Cp).
//   :063592d3-0635931a  for PushedCores[1] then [0]: non-null, neither
//     (Flags & 0xc) nor (Flags & 0x300): WasPushedDuringImpactSystem == 0 ->
//     add_pushed_core_with_pairs_except(Core, BestPair), else
//     FUN_06359340(Core). Returns 1.
// ---------------------------------------------------------------------------
int32_t IVP_Impact_System::test_loop_all_pairs()
{
    IVP_Contact_Point* BestCp = nullptr;
    IVP_ListManager_CorePairEntry* BestPair = nullptr;
    float BestPriority = G_IVP_MindistSettings.max_coll_dist;

    for (int32_t PairIndex = static_cast<int32_t>(Pairs.n_elems) - 1; PairIndex >= 0; --PairIndex)
    {
        IVP_ListManager_CorePairEntry* const Pair = Pairs.elems[PairIndex];
        const IVP_Core* const CoreB = Pair->CoreB;
        const IVP_Core* const CoreA = Pair->CoreA;
        const int32_t StateB = (static_cast<int32_t>((CoreB->Flags & 0xfffffffcu) << 28) >> 6) |
                               static_cast<int32_t>((CoreB->MovementState & 0xffffff00u) << 16);
        const int32_t StateA = (static_cast<int32_t>(CoreA->Flags << 28) >> 6) |
                               static_cast<int32_t>(CoreA->MovementState << 16);
        if ((StateA & StateB) != 0)
        {
            continue;
        }

        for (int32_t CpIndex = static_cast<int32_t>(Pair->AttachedCtx.n_elems) - 1; CpIndex >= 0; --CpIndex)
        {
            IVP_Contact_Point* const Cp = Pair->AttachedCtx.elems[CpIndex];
            if (Cp->Owner->StateProcessed != 1)
            {
                ++LEnvironment->counter_60; // .text:0635921f
                IVP_Contact_Point_Calc_Impact_Priority(Cp); // FUN_063595c0
            }
            const float Priority = Cp->Owner->WinnerPriority;
            if (!(Priority >= BestPriority))
            {
                BestPriority = Priority;
                BestCp = Cp;
                BestPair = Pair;
            }
        }
    }

    if (BestCp == nullptr)
    {
        return 0;
    }

    for (int32_t Side = 1; Side >= 0; --Side)
    {
        IVP_Core* const Core = BestCp->Owner->ChildCandidate[Side];
        if (Core == nullptr || Core->FastSimUnitData != nullptr)
        {
            continue;
        }
        SyncedCores.Add(Core);
        synchronize_core_if_simulated(Core); // FUN_063590c0
    }

    IVP_Impact_Solver_Long_Term* const LongTerm = BestCp->Owner;
    ++LongTerm->Status;
    IVP_Core* PushedCoresOfImpact[2];
    IVP_Impact_Solver_Long_Term_Do_Impact_Long_Term(BestCp->Owner, PushedCoresOfImpact,
                                                    BestCp->Owner->LastImpactTimeStep, BestCp); // FUN_06358d60

    for (int32_t Side = 1; Side >= 0; --Side)
    {
        IVP_Core* const Core = PushedCoresOfImpact[Side];
        if (Core == nullptr || (Core->Flags & 0xcu) != 0 || (Core->Flags & 0x300u) != 0)
        {
            continue;
        }
        if (static_cast<IVP_Old_Sync_Rot_Z*>(Core->FastSimUnitData)->WasPushedDuringImpactSystem == 0)
        {
            add_pushed_core_with_pairs_except(Core, BestPair); // FUN_063590e0
        }
        else
        {
            clear_processed_state_of_core_dists(Core); // FUN_06359340
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// FUN_06359340 (.text:06359340-06359370, 19 insns, RET 4).
// Info = Core->find_object_hash_entry(AssociatedFs) (FUN_06347c40, no null
// test of the result); for every dist of Info (n_elems read once, elems every
// pass, last first): `AND dword [LongTerm+0x54],0xffffff00` -- the state byte
// becomes 0.
// ---------------------------------------------------------------------------
void IVP_Impact_System::clear_processed_state_of_core_dists(IVP_Core* Core)
{
    IVP_Friction_Info_For_Core* const Info = static_cast<IVP_Friction_Info_For_Core*>(
        Core->find_object_hash_entry(static_cast<unsigned>(reinterpret_cast<uintptr_t>(AssociatedFs))));
    for (int32_t Index = static_cast<int32_t>(Info->FrDists.n_elems) - 1; Index >= 0; --Index)
    {
        Info->FrDists.elems[Index]->Owner->StateProcessed = 0;
    }
}

// ---------------------------------------------------------------------------
// FUN_063596b0 (.text:063596b0-0635984e, 121 insns, no arguments). The SEH
// frame (LAB_0639818b) guards the stack hull-manager vector only.
//   :063596cb-063596fe  SyncedCores, last first: WasPushedDuringImpactSystem
//     == 0 -> FUN_063473b0 (restore); +0x1b4 = 0 either way.
//   :06359700-0635974e  Delta = float(((next_psi.base - now.base) +
//     next_psi.offset) - now.offset) from LEnvironment +0xf8..+0x104.
//   :0635973a-06359749  IVP_U_Vector_Fixed<IVP_Hull_Manager, 0x100> on the
//     stack (elems = the inline 0x400-byte buffer, memsize 0x100).
//   :06359752-063597d6  PushedCores, last first, cores without (Flags & 0xc):
//     event sim {Delta, float(1/Delta), Env} when Delta > 1e-10 (`TEST AH,0x41;
//     JNZ`), else {Env->delta_time, Env->inv_delta_time, Env} (its +0xc is
//     never written); FUN_06351310 on a solver holding the core; +0x1b4 = 0;
//     FUN_06359340(Core).
//   :063597d8-063597e0  FUN_06351f80(Env, &HullManagers) (cdecl).
//   :063597e5-0635981b  the hull vector's destructor (free when not inline).
//   :06359815-06359834  PushedCores, last first, cores without (Flags & 0xc):
//     FUN_06347be0.
// ---------------------------------------------------------------------------
void IVP_Impact_System::recalc_all_affected_cores()
{
    for (int32_t Index = static_cast<int32_t>(SyncedCores.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Core* const Core = SyncedCores.elems[Index];
        if (static_cast<IVP_Old_Sync_Rot_Z*>(Core->FastSimUnitData)->WasPushedDuringImpactSystem == 0)
        {
            Core->restore_fast_sim_snapshot(); // FUN_063473b0
        }
        Core->FastSimUnitData = nullptr;
    }

    IVP_Environment* const Env = LEnvironment;
    const float Delta = (float)(
        (((double)(Env->next_psi_time_base) - Env->current_time.time_base) + Env->next_psi_time_offset) -
        Env->current_time.time_offset);

    {
        IVP_U_Vector_Fixed<IVP_Hull_Manager, 0x100> ActiveHullManagers;
        for (int32_t Index = static_cast<int32_t>(PushedCores.n_elems) - 1; Index >= 0; --Index)
        {
            IVP_Core* const Core = PushedCores.elems[Index];
            if ((Core->Flags & 0xcu) != 0)
            {
                continue;
            }
            IVP_Event_Sim EventSim;
            EventSim.environment = LEnvironment;
            if (Delta > GIvpEpsilon)
            {
                EventSim.delta_time = Delta;
                EventSim.i_delta_time = (float)((double)(GIvpOne) / Delta);
            }
            else
            {
                EventSim.delta_time = LEnvironment->delta_time;
                EventSim.i_delta_time = LEnvironment->inv_delta_time;
            }
            IVP_Calc_Next_PSI_Solver Solver(Core);
            Solver.calc_next_PSI_matrix(&EventSim, &ActiveHullManagers); // FUN_06351310
            Core->FastSimUnitData = nullptr;
            clear_processed_state_of_core_dists(Core); // FUN_06359340
        }
        IVP_Calc_Next_PSI_Solver::commit_all_hull_managers(LEnvironment, &ActiveHullManagers); // FUN_06351f80
    }

    for (int32_t Index = static_cast<int32_t>(PushedCores.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Core* const Core = PushedCores.elems[Index];
        if ((Core->Flags & 0xcu) == 0)
        {
            Core->notify_objects_and_cache_env_field(); // FUN_06347be0
        }
    }
}
