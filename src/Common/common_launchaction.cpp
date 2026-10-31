// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// LaunchAction, JumpInAction, LaunchActionVars -- split out of common_cequip.cpp (CMN-B2).
// Declared in common_cequip.h; helpers shared with the sibling TUs are in
// common_cequip_internal.h (namespace CmnCequip).
#include <cmath>
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine (MatrixToQuaternionViaMathEngine)
#include "common_fuses.h"    // WeaponModDB_GetShieldMod (CEShield::GetWeaponModifier)
#include "common_iface.h"    // EquipStatus, EquipStatusFlag* (CEquip::GetStatus)
#include "common_ini.h"
#include "common_misc.h"     // ComputeExplosiveImpulses (CAttachedEquip::Disconnect)
#include "common_physys.h"
#include "common_spatial.h"  // ISpatialPartition (ScanList::add_in_range bucket query)
#include "common_timing.h"   // Timing::GetGlobalTime (CEShieldGenerator / CEShield)
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "../IVP/ivp_physics/ivp_real_object.h"
#include "../IVP/ivp_physics/ivp_controller.h"  // IVP_Event_Sim (do_simulation_controller)

#include <cstdarg>
#include <cstdint>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common_cequip_internal.h"

using namespace CmnCequip;

// Retail exports 3235/3236 (.rdata:0639e568 / 0639e564), see common_cequip.h.
namespace LaunchActionVars
{
    COMMON_API float const launch_coast_time = 0.5f;
    COMMON_API float const launch_decel_time = 2.5f;
}

namespace
{
    // The `Obj && (ObjectClass & 0x103) == 0x103 ? Obj : 0` receiver retail builds before
    // every CEqObj::set_control_exclusion call in this file; the call itself is made
    // through the possibly-null result with no test.
    CEqObj* EqObjOrNull(CObject* Obj)
    {
        return (Obj != nullptr && (Obj->ObjectClass & 0x103) == 0x103) ? (CEqObj*)Obj : nullptr;
    }
}

// .text:062afda0: the watcher at +0x1c is zeroed and, for a non-null DockObj, pointed at its
// Watchable (+8); +0x24 BaseObj, +0x2c LaunchType, +0x30..+0x48 and the two bytes at
// +0x4c/+0x4d zeroed, LaunchOrient identity, +0x74 zeroed; then LaunchFlags = BaseObj's
// TypeBits (+0xe0), and bit 0x20 forces LaunchType to 1. The pad bytes +0x4e/+0x4f are not
// written.
LaunchAction::LaunchAction(IObjRW* DockObj, CEqObj* InBaseObj, int InLaunchType)
    : BaseObj(InBaseObj)
    , LaunchFlags(0)
    , LaunchType(InLaunchType)
    , CoastElapsed(0.0f)
    , bNoDockingCamera(false)
    , bUpdateComplete(false)
    , LaunchOrient(1.0f, 0.0f, 0.0f,
                   0.0f, 1.0f, 0.0f,
                   0.0f, 0.0f, 1.0f)
    , CoastTimer(0.0f)
{
    DockObjWatcher.set(DockObj);   // IObjRW -> its Watchable base (+8); null stays null
    LaunchPos.x = 0.0f;
    LaunchPos.y = 0.0f;
    LaunchPos.z = 0.0f;
    LaunchDir.x = 0.0f;
    LaunchDir.y = 0.0f;
    LaunchDir.z = 0.0f;

    LaunchFlags = BaseObj->TypeBits;
    if ((LaunchFlags & 0x20) != 0)
    {
        LaunchType = 1;
    }
}

LaunchAction::~LaunchAction()
{
    // Common.dll 0x62afec0 (FUN_062afec0), the real complete-object destructor --
    // previously ported here as an empty body, which silently dropped this
    // cleanup. Real bug: an AI-directive launch (Start()'s LaunchFlags&0x20==0
    // path) registers `this` as a directive callback via
    // BM->set_directive_callback(this); if the LaunchAction were destroyed while
    // still registered, the BehaviorManager's DirectiveCallbacks[5] array would
    // keep a dangling pointer and could later dispatch into freed memory.
    // Base-class vtable reinstallation and the PhySys::Controller/IAction base
    // destructors themselves are handled automatically by the compiler once this
    // body returns -- only the two pieces of LaunchAction's own cleanup logic
    // need porting here.
    IObjRW* Dock = ObjRwFromWatchable(WatchedTarget(DockObjWatcher));

    if (Dock != nullptr)
    {
        IBehaviorManager* BM = GetBehaviorManager(Dock);
        if (BM != nullptr)
        {
            // Upcasts `this` to pub::AI::DirectiveCallback*, applying the same
            // +0x18 this-adjustment Start()'s set_directive_callback(this) call
            // does -- unregisters every DirectiveCallbacks[] slot that still
            // points at this LaunchAction.
            BM->clear_directive_callback(this);
        }
    }

    // .text:062aff36: the watcher member's destructor (unlink when watching) runs after
    // this body.
}

// Common.dll 0x62b01f0 (FUN_062b01f0), dispatched through LaunchAction's own
// DirectiveCallback sub-vtable (PTR_FUN_0639e57c slot 0) -- previously
// unidentified; pub::AI::DirectiveCallback only had a placeholder virtual dtor
// (see common_pub.h). Retail's own field reads here are relative to the
// DirectiveCallback subobject (this+0x18: e.g. its "+0x4"==LaunchAction's
// DockObjWatcher at +0x1c, its "+0xc"==BaseObj at +0x24, its "+0x35"==
// bUpdateComplete at +0x4d) -- ported as a normal member override using
// LaunchAction's own field names directly since the compiler generates the
// this-adjustment thunk automatically.
void LaunchAction::OnDirectiveComplete(int ResultWord11c, int ResultWord118)
{
    // Retail's body (0x062b01f0..062b025d) ends `RET 0x8`, which is the callee-side
    // confirmation of the two-argument signature, but it never touches either one:
    // the whole range contains not a single `[ESP + 0x..]` reference. Ignoring them
    // here is therefore faithful, not an unported gap.
    (void)ResultWord11c;
    (void)ResultWord118;

    IObjRW* Dock = ObjRwFromWatchable(WatchedTarget(DockObjWatcher));

    if (Dock == nullptr)
    {
        return;
    }

    CObject* Docked = (CObject*)Dock->cobject();   // slot 84 (+0x150)

    // Raw asm 062b0211-062b0232: retail zeroes EAX (this) when Docked is null or
    // fails the ObjectClass&0x103 check, then calls set_control_exclusion through
    // that possibly-null this UNCONDITIONALLY -- same "trust the invariant, don't
    // guard" pattern as Start()'s unconditional Dock deref above. Reproduced as-is.
    EqObjOrNull(Docked)->set_control_exclusion(0);

    {
        ObjAffectOf(Dock)->slot_70(BaseObj->Id);
    }

    bUpdateComplete = true;
}

void LaunchAction::Start()
{
    // Common.dll 0x62aff80. Recover the docking IObjRW* from the Watchable*
    // the ctor stored (Watchable sits at IObjRW+8), then resolve the CObject
    // actually docked via IObjRW vtable slot 84 (0x150): get_cobject. Retail
    // dereferences this unconditionally (crashes if the watcher was never set,
    // e.g. a null DockObj or a failed LaunchAction allocation) -- reproduced
    // as-is rather than guarded, matching CShip::launch's own unconditional call.
    IObjRW* Dock = ObjRwFromWatchable(WatchedTarget(DockObjWatcher));

    CObject* Docked = (CObject*)Dock->cobject();   // slot 84 (+0x150)

    if ((LaunchFlags & 0x20) == 0)
    {
        // Not ready for an immediate physical launch -- queue an AI directive
        // instead (the ship flies itself out under AI control) and return.
        if ((LaunchFlags & 0x200100) != 0)
        {
            pub::AI::DirectiveLaunchOp Op;
            Op.LaunchObjectId = BaseObj->Id; // CSimple::Id, +0xb0
            Op.Field_10 = LaunchType;
            Op.Field_14 = 1;

            IBehaviorManager* BM = GetBehaviorManager(Dock);
            if (BM != nullptr)
            {
                BM->set_directive_callback(this);

                EqObjOrNull(Docked)->set_control_exclusion(6);

                // 062b01b7-062b01cf: a 12-byte IDirectiveInfo {1, Op+0x08, 8}. The middle
                // dword is the op's BaseOp::Priority, which the DirectiveLaunchOp ctor
                // (062db13b) zeroes.
                unsigned const DirWords[3] = { 1, Op.Priority, 8 };
                IDirectiveInfo Dir;
                memcpy(&Dir, DirWords, sizeof(Dir));

                BM->set_current_directive(Dir, &Op);
            }
        }
        return;
    }

    // Immediate physical launch.
    Docked->enable_collisions_r(false);

    EqObjOrNull(Docked)->set_control_exclusion(7);

    // IObjAffect vtable slot 0 on the docking object's IObjAffect sub-object
    // (IObjRW+4), thiscall(this, 0.0f). IObjAffect now has its thirty slots published --
    // see common_iface.h -- and slot 0 takes a float: the sibling site below passes 1.0f
    // through the SAME slot, which is what settles the type. Passing the integer 0 worked
    // only because 0 and 0.0f have the same bits.
    ObjAffectOf(Dock)->set_throttle(0.0f);

    BaseObj->launch_pos(LaunchPos, LaunchOrient, LaunchType);

    // Push the launch point back along the (negated) forward direction, then
    // correct by how far the docked ship's own origin sits from the hardpoint
    // "hpmount" is anchored to (Common.dll.txt 062b0003-062b00d7 -- ported from
    // the raw asm, not the FPU-mangled .c, which lost track of these registers).
    Vector Forward{-LaunchOrient.E[0][2], -LaunchOrient.E[1][2], -LaunchOrient.E[2][2]};
    LaunchPos.x += Forward.x * 150.0f;
    LaunchPos.y += Forward.y * 150.0f;
    LaunchPos.z += Forward.z * 150.0f;

    Vector HpOut;
    HardpointPosition(Docked->EngineInstanceId, "hpmount", &HpOut, nullptr);

    LaunchPos.x += Docked->WorldPosition.x - HpOut.x;
    LaunchPos.y += Docked->WorldPosition.y - HpOut.y;
    LaunchPos.z += Docked->WorldPosition.z - HpOut.z;

    LaunchDir = Vector{LaunchOrient.E[0][2], LaunchOrient.E[1][2], LaunchOrient.E[2][2]};

    // LaunchActionVars::launch_coast_time (retail .rdata 0.5) * 80.0 (retail
    // .rdata, unnamed in retail's own symbol table).
    CoastTimer = LaunchActionVars::launch_coast_time * 80.0f;

    PhySys::Controller::Register(this, Docked);

    Archetype::EqObj* Arch = (Archetype::EqObj*)BaseObj->Archetype;
    bNoDockingCamera = !Arch->bDockingCamera;
}

// Common.dll 0x62b0270 (LAB_062b0270), dispatched through LaunchAction's own
// IAction sub-vtable (PTR_LAB_0639e5a4 slot 1). Decomp-checked against the raw asm
// 062b0270-062b055b (no .c pseudocode was generated for this range). Retail's `Dt`
// stack argument (the RET 0x4 cleanup) is never actually read anywhere in the body.
bool LaunchAction::update(float /*Dt*/)
{
    IObjRW* Dock = ObjRwFromWatchable(WatchedTarget(DockObjWatcher));

    if (Dock == nullptr)
    {
        // Docked watcher gone (target destroyed) -- report the action finished so
        // ActionDB::update() removes it.
        bUpdateComplete = true;
        return !bUpdateComplete;
    }

    if ((LaunchFlags & 0x20) == 0)
    {
        // AI-directive launch path (Start() queued a DirectiveLaunchOp instead of
        // an immediate physical launch): nothing to do per tick. bUpdateComplete
        // only flips true here once OnDirectiveComplete() fires (IBehaviorManager
        // calling back through DirectiveCallbacks[] when the queued directive
        // finishes) -- until then this reports "not done" every tick.
        return !bUpdateComplete;
    }

    if (!bNoDockingCamera)
    {
        // LaunchActionVars::launch_coast_time (0.5, same .rdata constant Start()
        // uses for CoastTimer) + launch_decel_time (2.5) == the 3.0-second
        // coast-complete threshold, compared against the elapsed-coast counter at
        // +0x30. CORRECTION: a real per-tick producer for CoastElapsed WAS found --
        // LaunchAction::Step() (Common.dll 0x62b0560, tracker item 1102) -- see its
        // own comment and the CoastElapsed field comment in common_cequip.h. Step()
        // is extracted but not yet wired into a live vtable (needs the out-of-scope
        // IVP_Controller interface), so this compare is still always false in
        // practice in this port today, but the gap is no longer "producer
        // unidentified" -- it's "producer identified, not yet callable".
        // .text:062b02b0-062b02cf: FLD launch_coast_time; FADD launch_decel_time;
        // FCOMP [this+0x30]; TEST AH,0x41 / JP -> complete when CoastElapsed >= 3.0.
        bUpdateComplete = ((double)CoastElapsed >=
                           (double)LaunchActionVars::launch_coast_time +
                           LaunchActionVars::launch_decel_time);

        if (bUpdateComplete)
        {
            // CORRECTED 2026-09-23 against .text:062b02d6-062b034c. Retail pokes IObjAffect
            // slot 0 (1.0f, `PUSH 0x3f800000; CALL [EAX]` at 062b02ea) FIRST, then re-resolves
            // the watcher, calls get_cobject (+0x150) and nudges with no null check on the
            // result, and the factor is -80.0f (DAT_0639e560 == 0x42a00000, FCHS), not
            // -launch_coast_time: the earlier port had the order reversed, a guard retail does
            // not have, and -0.5f.
            ObjAffectOf(Dock)->set_throttle(1.0f);

            CObject* Docked0 = (CObject*)Dock->cobject();   // slot 84 (+0x150)

            // Docked0's own current forward (Orientation.E[*][2] = +0x10/+0x1c/+0x28), not
            // the frozen LaunchOrient Start() computed, scaled by -80.0f and built through
            // FUN_0628b030 (Vec3Assign).
            float const Scale = -80.0f;
            Vector Delta{ Docked0->Orientation.E[0][2] * Scale,
                          Docked0->Orientation.E[1][2] * Scale,
                          Docked0->Orientation.E[2][2] * Scale };
            PhySys::AddToVelocity(Docked0, Delta);
        }
    }

    if (!bUpdateComplete)
    {
        // Coast threshold not reached (or bNoDockingCamera skipped the check
        // entirely, which retail also never re-checks on a later tick) -- keep the
        // physics controller alive and report "not done".
        PhySys::Controller::wakeup();
        return !bUpdateComplete;
    }

    // Coast phase complete: resolve the docked CObject once more (matches retail
    // re-resolving through the watcher rather than caching Docked0 above).
    CObject* Docked = (CObject*)Dock->cobject();   // slot 84 (+0x150)

    if (!SinglePlayer())
    {
        // Slot 47 (+0xbc) is IObjInspect::is_player(). The port read it as an unidentified
        // "is this side authoritative" predicate; what retail is asking is whether the docked
        // object is the human player.
        bool const bIsServer = ObjInspectOf(Dock)->is_player();

        if (bIsServer)
        {
            // .text:062b039e..062b04a0: a multiplayer player leaves the dock with a random
            // heading: the rotation part of the watched object's transform (IObjInspect slot 3,
            // get_transform) is turned by two random angles in [-pi/8, pi/8) -- first in the
            // x/z columns, then in the y/z columns -- and the docked object is beamed to the
            // watched object's center of mass (slot 5) with that orientation (CObject vt+0x7c).
            // Angle = rand() * 3.0517578e-05f * pi/4 (double DAT_0639e5c8) - pi/8 (DAT_0639e5c0);
            // the cosine is stored as a float, the sine stays on the x87 stack.
            Matrix Orient = ObjInspectOf(Dock)->get_transform().Rotation;

            double Angle = (double)((float)rand() * 3.0517578125e-05f) *
                           0.78539818525314331 - 0.39269909262657166;
            float Cos = static_cast<float>(std::cos(static_cast<double>(Angle)));
            double Sin = std::sin(static_cast<double>(Angle));
            unsigned Row = 0;
            for (; Row < 3; ++Row)
            {
                float X = Orient.E[Row][0];
                float Z = Orient.E[Row][2];
                // 062b03f5-062b0419: FLD X, FLD Cos(float) FMUL ST1, FLD sin FMUL Z, FSUBP,
                // FSTP; FLD Cos FMUL Z, FXCH, FMUL sin, FADDP, FSTP -- every product unrounded.
                Orient.E[Row][0] = (float)((double)Cos * X - Sin * Z);
                Orient.E[Row][2] = (float)((double)Cos * Z + X * Sin);
            }

            Angle = (double)((float)rand() * 3.0517578125e-05f) *
                    0.78539818525314331 - 0.39269909262657166;
            Cos = static_cast<float>(std::cos(static_cast<double>(Angle)));
            Sin = std::sin(static_cast<double>(Angle));
            for (Row = 0; Row < 3; ++Row)
            {
                float Y = Orient.E[Row][1];
                float Z = Orient.E[Row][2];
                // 062b0450-062b0474: sin*Z + Cos*Y, then Cos*Z - Y*sin, products unrounded.
                Orient.E[Row][1] = (float)(Sin * Z + (double)Cos * Y);
                Orient.E[Row][2] = (float)((double)Cos * Z - Y * Sin);
            }

            Vector Center = ObjInspectOf(Dock)->get_center_of_mass();
            Docked->beam_object(Center, Orient, false);
        }
    }

    // Retail dereferences Docked unconditionally here (matches Start()'s own
    // unconditional Docked-> calls) -- not guarded.
    Docked->enable_collisions_r(true);

    EqObjOrNull(Docked)->set_control_exclusion(0);

    {
        // IObjAffect vtable slot 28 (0x70/4) on the dock object, thiscall(BaseObj->Id).
        ObjAffectOf(Dock)->slot_70(BaseObj->Id);
    }

    {
        // IObjAffect vtable slot 0 again, thiscall(this, 1.0f) -- retail really
        // does poke this slot a second time on this path.
        ObjAffectOf(Dock)->set_throttle(1.0f);
    }

    IBehaviorManager* BM = GetBehaviorManager(Dock);
    if (BM != nullptr)
    {
        BM->update_current_behavior_throttle(1.0f);
    }

    return !bUpdateComplete;
}

// Common.dll 0x62b0560 (LAB_062b0560), LaunchAction's own PhySys::Controller vtable slot 4
// (.rdata PTR_core_is_going_to_be_deleted_event_0639e584+0x10, installed at [this+8] by the
// ctor). Decomp-checked against the raw asm 062b0560-062b0665 (no .c pseudocode was
// generated for this range). Structurally identical in role to JumpInAction's slot 4; now
// the class's do_simulation_controller override (was the plain method Step(float Dt)).
//
// Retail behavior:
//   - If LaunchFlags == 0x20 (exact-value compare in the raw asm, not a bit test -- ported
//     literally), advances CoastElapsed by Dt. THIS IS the per-tick producer that
//     update()'s old comment (and the CoastElapsed field comment) said was never found.
//   - If WatchedTarget(DockObjWatcher) is null, returns without touching anything else (update()'s own
//     null-watcher branch already handles bUpdateComplete in that case).
//   - Otherwise re-resolves the docked object via get_cobject (vtable slot 0x150) --
//     unconditionally, with no type/null guard, matching retail's "trusts the watcher
//     resolved" idiom used throughout this class.
//   - If bNoDockingCamera: immediately beams the docked object to LaunchPos/LaunchOrient
//     and sets bUpdateComplete = true right here (short-circuiting the coast entirely).
//   - Otherwise computes a two-phase ease-out blend distance X along LaunchDir:
//       - While CoastElapsed < launch_decel_time (2.5): X = 150*Frac^2 + 80*(2.5-CoastElapsed)
//         + CoastTimer, where Frac = (2.5-CoastElapsed)/2.5. The two retail .rdata constants
//         150.0f (DAT_0639e56c) and 80.0f (DAT_0639e560) have no separate named symbols.
//       - Once CoastElapsed >= 2.5: X = ((launch_coast_time(0.5) + launch_decel_time(2.5) -
//         CoastElapsed) / launch_coast_time(0.5)) * CoastTimer -- a simple linear countdown
//         over the last 0.5s.
//     Then beams the docked object to LaunchPos + LaunchDir*X (same orientation, LaunchOrient)
//     every tick, without touching bUpdateComplete (update() itself flips that once
//     CoastElapsed reaches 3.0).
void LaunchAction::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // Slot 4 (RET 0x8): the timestep is the event's delta_time (.text:062b056c-062b0570,
    // read only inside the LaunchFlags == 0x20 test); CoreList is never read.
    (void)CoreList;
    float const Dt = EventSim->delta_time;
    if (LaunchFlags == 0x20)
    {
        CoastElapsed += Dt;
    }

    // 062b0578-062b0588: watched head non-null and head - 8 non-null.
    IObjRW* Watched = ObjRwFromWatchable(WatchedTarget(DockObjWatcher));
    if (Watched == nullptr)
    {
        return;
    }

    CObject* Dock = (CObject*)Watched->cobject();   // slot 84 (+0x150)

    if (bNoDockingCamera)
    {
        Dock->beam_object(LaunchPos, LaunchOrient, false);
        bUpdateComplete = true;
        return;
    }

    // 062b05c7-062b061a: FCOMP CoastElapsed against launch_decel_time, TEST AH,5 + JP: below
    // eases (Remaining = decel - elapsed, Frac = Remaining / decel, 150*Frac*Frac +
    // 80*Remaining + CoastTimer); >= or unordered coasts ((coast + decel - elapsed) / coast *
    // CoastTimer). Everything stays on the FPU.
    double X;
    float const Decel = LaunchActionVars::launch_decel_time;
    float const Coast = LaunchActionVars::launch_coast_time;
    if (CoastElapsed < Decel)
    {
        double const Remaining = (double)Decel - CoastElapsed;
        double const Frac = Remaining / Decel;
        X = (150.0 * Frac * Frac + 80.0 * Remaining) + CoastTimer;
    }
    else
    {
        X = (((double)Coast + Decel - CoastElapsed) / Coast) * CoastTimer;
    }

    // 062b061d-062b065a: the x product is added to LaunchPos.x on the FPU, while the y and z
    // products are spilled as floats first (VC6 register pressure) -- reproduced.
    float const StepY = (float)(X * LaunchDir.y);
    float const StepZ = (float)(X * LaunchDir.z);
    Vector InterpPos;
    InterpPos.x = (float)(X * LaunchDir.x + LaunchPos.x);
    InterpPos.y = StepY + LaunchPos.y;
    InterpPos.z = StepZ + LaunchPos.z;

    Dock->beam_object(InterpPos, LaunchOrient, false);
}

// Retail inlines this constructor into CShip::jump_in (.text:062b2d7c-062b2edb): the watcher
// at +0x18 is zeroed and pointed at JumpObj's Watchable (+8) when non-null; JumpOrient is set
// to identity, JumpPos and the +0x64 flag zeroed (062b2dbd-062b2de8); then the fields below.
// The pad bytes +0x65..+0x67 are not written.
JumpInAction::JumpInAction(IObjRW* JumpObj, Vector const& Pos, Matrix const& Orient, float Speed)
    : JumpElapsed(0.0f)
    , JumpSpeed(0.0f)
    , JumpApproachBudget(0.0f)
    , JumpOrient(1.0f, 0.0f, 0.0f,
                 0.0f, 1.0f, 0.0f,
                 0.0f, 0.0f, 1.0f)
    , Field5c(0.0f)
    , Field60(0.0f)
    , bJumpComplete(false)
{
    JumpPos.x = 0.0f;
    JumpPos.y = 0.0f;
    JumpPos.z = 0.0f;

    JumpObjWatcher.set(JumpObj);                                        // Watchable base at +8; null stays null

    // Re-resolve the actual target through the watched object's own vtable slot 84
    // (0x150) -- the same "get_cobject" idiom LaunchAction::Start() uses -- and validate
    // it's really a CShip (ClassShip mask 0x503) before trusting it below.
    // .text:062b2df9-062b2e0d: receiver = head ? head - 8 : 0 and the call goes out with no
    // null test (an unwatched JumpObj faults on the vtable load, as in retail).
    IObjRW* const Watched = ObjRwFromWatchable(WatchedTarget(JumpObjWatcher));
    CObject* Target = (CObject*)Watched->cobject();                     // slot 84 (+0x150)
    if (Target == nullptr || (Target->ObjectClass & 0x503) != 0x503)
    {
        Target = nullptr;
    }

    JumpPos = Pos;
    JumpOrient = Orient;
    JumpSpeed = Speed;
    JumpApproachBudget = Speed * 2000.0f;

    // Retail .rdata constants written here with no reader anywhere in this decompiled
    // range (see JumpInAction's class comment on the missing Step/ControlObject
    // override) -- kept for fidelity even though nothing in this port consumes them yet.
    Field5c = 1500.0f;
    Field60 = 10.0f;

    // Retail dereferences Target unconditionally for the rest of this ctor -- same
    // "trusts the watcher resolved" pattern as LaunchAction::Start()'s Docked-> calls --
    // reproduced as-is rather than guarded (crashes on a null/non-ship JumpObj, matching
    // retail).
    CShip* TargetShip = (CShip*)Target;
    TargetShip->enable_collisions_r(false);
    TargetShip->Throttle = 0.0f;

    CEquip* EngineEquip = const_cast<CEquip*>(TargetShip->EquipManager.FindFirst(EquipTypeEngine));
    if (EngineEquip != nullptr)
    {
        if (!EngineEquip->IsActive())
        {
            EngineEquip->Activate(true);
        }
    }

    TargetShip->beam_object(Pos, Orient, false);
    PhySys::Controller::Register(this, TargetShip);
}

JumpInAction::~JumpInAction()
{
    // Common.dll.c ~85070 (FUN_062b2f30) -- undo the ctor's enable_collisions_r(false)
    // on the resolved target, then unregister the physics controller and drop the
    // watcher. Re-resolves the target the same way the ctor did rather than caching it,
    // matching retail (which re-walks the watcher here too instead of storing the
    // CObject* it validated in the ctor).
    // .text:062b2f60-062b2fb3: watched head non-null and head - 8 non-null, then the target
    // re-resolved and, when non-null, collisions re-enabled; UnRegister; the watcher member's
    // destructor unlinks it.
    IObjRW* const Watched = ObjRwFromWatchable(WatchedTarget(JumpObjWatcher));
    if (Watched != nullptr)
    {
        CObject* Target = (CObject*)Watched->cobject();   // slot 84 (+0x150)
        if (Target != nullptr)
        {
            Target->enable_collisions_r(true);
        }
    }

    PhySys::Controller::UnRegister(this);
}

bool JumpInAction::update(float /*Dt*/)
{
    // .text:062afd80 (IAction slot 1): while the jump is not complete, wake the controller;
    // the flag (set by do_simulation_controller) is read again after wakeup() for the
    // result, so the action is removed once the jump has completed.
    if (!bJumpComplete)
    {
        PhySys::Controller::wakeup();
    }
    return !bJumpComplete;
}

// Common.dll 0x62afbe0 (LAB_062afbe0), JumpInAction's own PhySys::Controller vtable slot 4
// (.rdata PTR_core_is_going_to_be_deleted_event_0639e680+0x10, installed at [this+8] by the
// ctor above). Decomp-checked against the raw asm 062afbe0-062afd6e (no .c pseudocode was
// generated for this range) -- see the class comment in common_cequip.h for how this slot
// was identified as IVP_Controller::do_simulation_controller; now the class's override (was
// the plain method Step(float Dt)).
//
// Retail behavior:
//   - If the watched jump target is gone (WatchedTarget(JumpObjWatcher) == nullptr), sets bJumpComplete
//     (the "done" flag JumpInAction::update() polls) and returns immediately.
//   - Otherwise re-resolves the target via get_cobject (vtable slot 0x150) and validates the
//     ClassShip mask (0x503) -- same idiom used throughout this class.
//   - Accumulates JumpElapsed (previously annotated "no reader found"; it is in fact a per-tick
//     elapsed-time counter) by Dt, comparing the running total against JumpSpeed, which is
//     reused here as an elapsed-time cap in seconds despite its name.
//   - While JumpElapsed < JumpSpeed: computes a clamped, squared ease-out fraction
//     ((JumpSpeed - JumpElapsed) / JumpSpeed)^2 clamped to [0,1] (retail .rdata constants
//     DAT_06399408/DAT_0639a1d0 = 0.0f/1.0f), scaled by JumpApproachBudget (also previously
//     "no reader found"; it is the ease-out distance budget), and offsets JumpPos along
//     JumpOrient's forward column (E[*][2], the same LaunchDir idiom LaunchAction::Start
//     uses) to get a smoothly-converging interpolated position, then re-teleports the target
//     there this tick via the same raw CObject vtable-slot-0x7c beam_object() idiom the ctor
//     uses. This refutes the class's old "single immediate teleport" assumption -- retail
//     actually eases the ship toward JumpPos over JumpSpeed seconds.
//   - Once JumpElapsed >= JumpSpeed: sets bJumpComplete (done), beams the target exactly to
//     JumpPos/JumpOrient, zeroes Target->Throttle, re-activates the target's engine equip if
//     inactive, and reports the arrival system id through the watched object's
//     IObjAffect::slot_74.
void JumpInAction::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // Listing 0x062afbe0..0x062afd6e (re-traced 2026-10-08). Slot 4 (RET 0x8): the timestep is
    // the event's delta_time (.text:062afc35-062afc39); CoreList is never read. No null guard
    // on the resolved ship: a non-ship target leaves it null and retail then faults on it.
    (void)CoreList;
    IObjRW* Watched = ObjRwFromWatchable(WatchedTarget(JumpObjWatcher));

    if (Watched == nullptr)
    {
        bJumpComplete = true;
        return;
    }

    CObject* Target = (CObject*)Watched->cobject();   // slot 84 (+0x150)
    CShip* TargetShip = (Target != nullptr && (Target->ObjectClass & 0x503) == 0x503)
        ? (CShip*)Target : nullptr;

    // 062afc32-062afc3e: FLD JumpElapsed, FADD delta_time, FCOM JumpSpeed, FST JumpElapsed -- the float
    // is stored, but the unrounded sum stays on the FPU and feeds the compare and the ease.
    double const Elapsed = (double)JumpElapsed + EventSim->delta_time;
    JumpElapsed = (float)Elapsed;

    // TEST AH,0x1 + JNZ: below (or unordered) eases in, otherwise arrives.
    if (!(Elapsed >= JumpSpeed))
    {
        // 062afcde-062afd14: ((JumpSpeed - Elapsed) / JumpSpeed)^2 on the FPU; above 1.0
        // (TEST AH,0x41 + JNZ keeps <=, NaN) -> 1.0; else below 0.0 (TEST AH,0x5 + JP keeps
        // >=, NaN) -> 0.0; then * JumpApproachBudget (still unrounded).
        double Blend = ((double)JumpSpeed - Elapsed) / JumpSpeed;
        Blend = Blend * Blend;
        if (Blend > 1.0)
            Blend = 1.0;
        else if (Blend < 0.0)
            Blend = 0.0;
        Blend = Blend * JumpApproachBudget;

        // 062afd1a-062afd62: the x and z products are stored as floats before the add, the y
        // product is added to JumpPos.y while still on the FPU.
        float const OffsetX = (float)(JumpOrient.E[0][2] * Blend);
        float const OffsetZ = (float)(JumpOrient.E[2][2] * Blend);
        Vector InterpPos;
        InterpPos.x = OffsetX + JumpPos.x;
        InterpPos.y = (float)(JumpOrient.E[1][2] * Blend + JumpPos.y);
        InterpPos.z = OffsetZ + JumpPos.z;
        TargetShip->beam_object(InterpPos, JumpOrient, false);   // vt+0x7c
        return;
    }

    // Arrival (0x062afc4c..0x062afcdb).
    bJumpComplete = true;
    TargetShip->beam_object(JumpPos, JumpOrient, false);
    TargetShip->Throttle = 0.0f;
    CEquip* EngineEquip = const_cast<CEquip*>(TargetShip->EquipManager.FindFirst(EquipTypeEngine));
    if (EngineEquip != nullptr && !EngineEquip->IsActive())
        EngineEquip->Activate(true);

    // The jumping object's IObjAffect::slot_74 (+0x74; ship gameobjects send JumpInComplete
    // with it) receives the id of the system the ship's object database belongs to
    // (IObjDB::GetZoneDb (+8) -> Universe::ISystem::GetId (+0), the system id).
    IObjDB* ObjDb = TargetShip->ObjectDb;   // +0xac
    if (ObjDb == nullptr)
        return;
    Universe::ISystem const* System = ObjDb->GetZoneDb();
    if (System == nullptr)
        return;
    unsigned const SystemId = System->GetId();
    ObjAffectOf(Watched)->slot_74(SystemId);
}
