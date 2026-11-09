// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEGun, CEGun::JointChannel and the turret helpers (retail CEGun.cpp) -- split out of common_cequip.cpp (CMN-B2).
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

namespace
{
    const char kCEGunSource[] = "E:\\FL\\Scratch\\Source\\Common\\CEGun.cpp";
    const float kDegToRad = 0.017453292f;   // .rdata:0639d210 = 0x3c8efa35
    const float kGunPi = 3.1415927f;
    const float kGunTwoPi = 6.2831855f;
    const float kSeeTargetAngle = 0.05235988f;
    const float kTargetPointHold = 2.0f;
    const float kSeekerLeadSpeed = 1.0e9f;
    const unsigned kJointTypeRev = 2;

    const unsigned kWatchableOffset = 0x9c;   // offsetof(CSimple, SelfWatchable)

    // CEGun's lock watcher (+0x250) watches CSimple::SelfWatchable; retail recovers the CSimple
    // with `W ? W - 0x9c : 0` and then tests the result for null as well (W == 0x9c).
    CSimple* DecodeLockWatchable(Watchable const* Watch)
    {
        if (Watch == nullptr)
            return nullptr;
        // The owning CSimple of its SelfWatchable member (container-of; no base class to cast to).
        return (CSimple*)((char*)Watch - kWatchableOffset);
    }

    Watchable const* EncodeLockWatchable(CSimple* Target)
    {
        if (Target == nullptr)
            return nullptr;
        return &Target->SelfWatchable;
    }

    double JointAbsDelta(CEGun::JointChannel const& Joint)
    {
        // Update / CanSeeTargetObject / FUN_06297560: abs(cur-des); limited==0 && >pi → pi-delta.
        double Delta = fabs((double)(Joint.Current) - Joint.Desired);
        if (Joint.bLimited == 0 && kGunPi < Delta)
            Delta = (double)(kGunPi) - Delta;
        return Delta;
    }

    bool JointWithin(CEGun::JointChannel const& Joint, float Threshold)
    {
        if (Joint.EngineIndex == kInvalidEngineInstance)
            return true;
        return JointAbsDelta(Joint) < Threshold;
    }

    void ComputeAimAngles(CEGun const& Gun, float& OutYaw, float& OutPitch, bool bUseLead,
                          Vector const& StoredRel)
    {
        // 0629718b / 06297296 / 062982d5: the fixed parent connector, not the rotating
        // gun root. Local = transpose(ConnectorOrientation * TurretFrame) * Dir.
        Vector Dir;
        if (bUseLead)
        {
            // Retail ignores ComputeTgtLeadPosition's result (06297254 / 0629829d); on false
            // (a destroyed gun) the lead is uninitialised stack. Zeroed here (deterministic).
            Vector Lead;
            Lead.x = 0.0f;
            Lead.y = 0.0f;
            Lead.z = 0.0f;
            Gun.ComputeTgtLeadPosition(Lead);
            Vector Origin = Gun.GetTurretOrigin();
            Dir.x = Lead.x - Origin.x;
            Dir.y = Lead.y - Origin.y;
            Dir.z = Lead.z - Origin.z;
        }
        else
        {
            Dir = StoredRel;
        }

        long const Connector = Gun.GetParentConnector(false);
        float const* const Ori = DALib::Engine->GetInstanceOrientation(
            (void*)(Connector));
        Matrix ConnectorOri(Ori[0], Ori[1], Ori[2], Ori[3], Ori[4], Ori[5], Ori[6], Ori[7], Ori[8]);
        Matrix Combined;
        Acquire3DMathEngine()->MulMatrix((FMatrix&)(Combined),
                                        (FMatrix const&)(ConnectorOri),
                                        (FMatrix const&)(Gun.TurretFrame));
        Matrix Inv(Combined.E[0][0], Combined.E[1][0], Combined.E[2][0],
                   Combined.E[0][1], Combined.E[1][1], Combined.E[2][1],
                   Combined.E[0][2], Combined.E[1][2], Combined.E[2][2]);
        Vector Local;
        Acquire3DMathEngine()->TransformMatrix((FVector&)(Local),
                                              (FMatrix const&)(Inv),
                                              (FVector const&)(Dir));
        // Both Update branches negate atan2(x,-z) at 0629734c; SetTarget does so at
        // 062983bb. FPATAN's pitch operands are y / sqrt(z*z + x*x) and 1, including
        // its zero-direction NaN. Values retained on the x87 stack use double here.
        // FPATAN, FCHS, FSTP dword: one rounding of the 80-bit angle (negation is exact).
        OutYaw = -static_cast<float>(std::atan2(static_cast<double>(Local.x), -(double)(Local.z)));
        double const Horiz = sqrt((double)(Local.z) * Local.z +
                                  (double)(Local.x) * Local.x);
        OutPitch = static_cast<float>(std::atan((double)(Local.y) / Horiz));
    }
}

// ---------------------------------------------------------------------------
// CEGun::JointChannel (vftable .rdata:0639d200)
// ---------------------------------------------------------------------------

// Inlined in CEGun::CEGun (.text:06296f78..06296fd7) and CEGun::LoseJoints
// (.text:06298192..062981d0): owner 0, instance -1, limited 1, desired/current 0, settled 0.
CEGun::JointChannel::JointChannel()
    : Owner(nullptr)
    , EngineIndex(kInvalidEngineInstance)
    , bLimited(1)
    , Desired(0.0f)
    , Current(0.0f)
    , bSettled(0)
{
}

// FUN_06299080 (__thiscall, RET 8), .text:06299080..0629911e.
void CEGun::JointChannel::Bind(CEGun* NewOwner, long Instance)
{
    Owner = NewOwner;                                             // 0629908b
    EngineIndex = (int)(Instance);                     // 0629908e
    // 06299091..062990bc: IEngine+0xc4 GetInstanceRelative; non-null -> |[+0x4c] - [+0x50]|
    // FCOMP double 2pi (DAT_0639c9e0), TEST AH,0x1 / JNZ skips when below or unordered; the
    // fall-through (>= 2pi, a full-rotation joint) clears bLimited.
    FJointRelative const* const Rel = InstanceRelative(Instance);
    if (Rel != nullptr)
    {
        if (fabs((double)Rel->Min - Rel->Max) >= 6.2831854820251465)
            bLimited = 0;
    }
    // 062990c0..062990dc: IEngine+0xcc GetJointParams(instance, 0, &Current); 0 -> FDUMP.
    if (DALib::Engine->GetJointParams((void*)(EngineIndex), 0, &Current) == 0)
    {
        // 062990de..06299117: Owner->Owner+0xb0, Arch nickname, Arch+0xc; no guards.
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: ID(0x%X) Failed to get_joint_state() for gun.  Get Scott. ARCH(%s) FILE(%s)",
              kCEGunSource, 0x336, Owner->Owner->Id, Owner->Arch->Nickname.get_string(),
              Owner->Arch->DaArchetype.Value);
    }
}

// LAB_06299130 (__thiscall, RET 4), .text:06299130..06299277. The x87 values that stay on the
// FPU stack (Delta, the unrounded Step, |Delta|) are held in double; every FSTP float is a
// float store here.
void CEGun::JointChannel::Tick(float Dt)
{
    // 06299134..0629913c: Delta = Desired - Current.
    double Delta = (double)(Desired) - (double)(Current);
    if (bLimited == 0)
    {
        // 06299141..06299154: FCOM pi, TEST AH,0x41 / JNZ: the fall-through (Delta > pi)
        // subtracts 2pi.
        if (Delta > (double)(kGunPi))
            Delta -= (double)(kGunTwoPi);
        // 06299156..06299163: FCOM -pi, TEST AH,0x5 / JP: the fall-through (Delta < -pi)
        // adds 2pi.
        else if (Delta < -(double)(kGunPi))
            Delta += (double)(kGunTwoPi);
    }

    // 06299169..0629916c: Next starts as Current.
    float Next = Current;
    // 06299170..06299188: rate = Owner ? Owner->Arch->TurnRate (+0xa4) : 0.0f; Step = rate*Dt,
    // FST'd into the Dt slot as float and kept unrounded on the stack.
    float const Rate =
        (Owner != nullptr) ? Owner->GunArch()->TurnRate : 0.0f;
    double const Step = (double)(Rate) * (double)(Dt);
    float const StepF = (float)(Step);
    // 06299190..0629919d: FCOMPP Step vs |Delta|, TEST AH,0x41 / JNZ: the fall-through
    // (Step > |Delta|) lands exactly on Desired; equal or unordered goes on.
    if (Step > fabs(Delta))
    {
        Next = (float)((double)(Next) + Delta);   // 0629919f..062991a5
    }
    // 062991ad..062991b8: FCOMP 0.0 (double), TEST AH,0x41 / JNZ: fall-through (Delta > 0)
    // adds the float step, otherwise (<= 0 or unordered) subtracts it.
    else if (Delta > 0.0)
    {
        Next = StepF + Next;                                         // 062991ba..062991cc
    }
    else
    {
        Next = Next - StepF;                                         // 062991c4..062991cc
    }

    // 062991d0..06299204: wrap Next into [-pi, pi]: > pi -> -2pi (TEST AH,0x41 / JNZ), else
    // < -pi -> +2pi (TEST AH,0x5 / JP).
    if (Next > kGunPi)
        Next = Next - kGunTwoPi;
    else if (Next < -kGunPi)
        Next = Next + kGunTwoPi;

    // 06299208..0629923f: IEngine+0xc8 SetJointParams(instance, 0, &Next), then +0xcc
    // GetJointParams(instance, 0, &Next) into the same local; results ignored, no guards.
    DALib::Engine->SetJointParams((void*)(EngineIndex), 0, &Next);
    DALib::Engine->GetJointParams((void*)(EngineIndex), 0, &Next);

    // 0629923f..06299272: |Current - Next| FCOMP 1e-4 (double DAT_0639c2b8), TEST AH,0x5 / JP:
    // taken (settled = 0) when greater, equal or unordered; fall-through (less) -> settled = 1.
    // Current = the read-back either way.
    bSettled = (fabs((double)(Current) - (double)(Next)) < 1.0e-4) ? 1 : 0;
    Current = Next;
}

// .text:062a66a0: `MOV EAX,[this+8]; RET` (ICF-folded getter).
int CEGun::JointChannel::GetId() const
{
    return EngineIndex;
}

CEGun::CEGun(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Gun const* ArchObj,
             bool bMissionFlag)
    : CELauncher(EquipTypeGun, OwnerObj, EquipId, ArchObj, bMissionFlag)
    , LockWatcher()
    , LockSubId(SUBOBJ_ID_NONE)
    , LockElapsed(0.0f)
{
    // .text:06296f40..0629706b: joints by JointChannel() (-1 / limited 1), watcher 0, LockSubId
    // = SUBOBJ_ID_NONE (06296fe9), TargetPoint 0, +0x268 = 0x7f7fffff, frame identity,
    // LockElapsed 0. +0x25a is padding, not written.
    MatrixSetIdentity(TurretFrame);
    TargetPoint.x = 0.0f;
    TargetPoint.y = 0.0f;
    TargetPoint.z = 0.0f;
    unsigned AgeBits = 0x7f7fffffu;
    memcpy(&TargetPointAge, &AgeBits, sizeof(TargetPointAge));
}

CEGun::CEGun(CEGun const& Other)
    : CELauncher(Other)
    , Yaw(Other.Yaw)
    , Pitch(Other.Pitch)
    , TurretFrame(Other.TurretFrame)
    , LockWatcher()
    , LockSubId(Other.LockSubId)
    , TargetPoint(Other.TargetPoint)
    , TargetPointAge(Other.TargetPointAge)
    , LockElapsed(Other.LockElapsed)
{
    // Listing 0x62996d00: base-then-derived memberwise copy through CELauncher, then Yaw/Pitch's
    // own fields (Owner/EngineIndex/bLimited/Desired/Current/bSettled) with the JointChannel
    // vftable 0639d200 stored at 06296dd6 / 06296e24 -- JointChannel's implicit copy ctor.
    // 06296e3d-06296e74: the watcher at +0x250 is zeroed (both dwords), then, when Other watches
    // something whose CSimple (W - 0x9c) is non-null, set_pointer(CSimple + 0x9c) links this
    // copy into the same target's watcher chain. The freshly zeroed watcher makes set()'s
    // dedup test a no-op for any non-null target. LockSubId and the tail (06296e79-06296ebb)
    // are plain copies.
    CSimple* OtherTarget = DecodeLockWatchable(Other.LockWatcher.target());
    if (OtherTarget != nullptr)
        LockWatcher.set(EncodeLockWatchable(OtherTarget));
}

CEGun::~CEGun()
{
    // Listing 0x37070: retail devirtualizes to CExternalEquip::Destroy() (neither CAttachedEquip
    // nor CELauncher overrides Destroy()) while CEGun's own vtable is still active, so Destroy()'s
    // internal Disconnect() call virtual-dispatches to CEGun::Disconnect() -- LoseJoints()
    // (AnimDB::Rem + joint reset) plus CELauncher::Disconnect() -- exactly once, before
    // CELauncher::~CELauncher() makes its own (separate, matches retail) Destroy() call with
    // CELauncher's own vtable active a moment later. Then 062970a0-062970b7:
    // `if ([this+0x250]) BaseWatcher::set_pointer(this+0x250, 0)` unlinks the gun from its
    // target's watcher chain -- exactly LockWatcher's own ~BaseWatcher (set(nullptr)), which
    // runs after this body and before ~CELauncher, the same order as retail.
    CExternalEquip::Destroy();
}

CEGun* __cdecl CEGun::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeGun)
        return nullptr;
    return (CEGun*)(Equip);
}

CEGun const* __cdecl CEGun::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

bool CEGun::Activate(bool bOn)
{
    // Common.dll.c 0x370e0 / listing 0x62970e0: capture IsActive() before the base call,
    // then if activation state actually changed and Owner is a CShip (ObjectClass & 0x503
    // == ClassShip), mark CShip::GunStatsDirty (Owner+0x2c4) so get_gun_stats recomputes.
    bool WasActive = IsActive();
    bool Result = CEquip::Activate(bOn);
    if (WasActive != Result && Owner != nullptr &&
        (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
    {
        ((CShip*)Owner)->GunStatsDirty = 1;
    }
    return Result;
}

bool CEGun::is_guided() const
{
    // .text:06296ce0..06296cf2: [[this+0x4c]+0x90] != 0 (Munition::Seeker), no null or class test.
    return MunitionArch()->Seeker != 0;
}

FireResult CEGun::Fire(Vector const& Target)
{
    // .text:062976e0: JMP CELauncher::Fire.
    return CELauncher::Fire(Target);
}

FireResult CEGun::CanFire(Vector const& Target) const
{
    // 062976f0. No barrel-count or zero-length guard in retail: a zero Delta divides by zero
    // (FDIVR 1.0 at 0629776f) and the NaN dot falls into the unordered -> FireMuzzleCone side.
    FireResult Result = CELauncher::CanFire(Target);
    if (Result != FireOk)
        return Result;

    // 0629770a: FLD MUZZLE_CONE_ANGLE / FMUL DAT_0639d210 / FSTP float -- rounded to float
    // before the FCOS at 062977d9.
    float const ConeAngle = MUZZLE_CONE_ANGLE * kDegToRad;
    Vector Muzzle = GetBarrelPosWS(0);
    Vector Delta;
    Delta.x = Target.x - Muzzle.x;
    Delta.y = Target.y - Muzzle.y;
    Delta.z = Target.z - Muzzle.z;
    // 0629774f..0629776f: sqrt(z*z + y*y + x*x) and 1/len kept on the FPU stack; each
    // normalized component is FSTP'd to float.
    double const Len = sqrt((double)(Delta.z) * Delta.z + (double)(Delta.y) * Delta.y +
                            (double)(Delta.x) * Delta.x);
    double const Inv = 1.0 / Len;
    Vector Norm;
    Norm.x = (float)(Delta.x * Inv);
    Norm.y = (float)(Delta.y * Inv);
    Norm.z = (float)(Delta.z * Inv);

    Vector Dir = GetBarrelDirWS(0);
    // 062977b9..062977d3: z*z' + y*y' + x*x' unrounded; 062977d9 FCOS on the float angle;
    // FCOMPP cos vs dot, TEST AH,0x5 / JP: taken (FireMuzzleCone) when cos > dot, equal or
    // unordered; fall-through only when cos < dot.
    double const Dot = (double)(Norm.z) * Dir.z + (double)(Norm.y) * Dir.y +
                       (double)(Norm.x) * Dir.x;
    double const ConeCos = std::cos(static_cast<double>(ConeAngle));
    if (ConeCos < Dot)
    {
        if (Owner != nullptr &&
            (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
        {
            CShip* Ship = (CShip*)(Owner);
            if (Ship->is_cruise_active())
                return FireCruise;
        }
        return FireOk;
    }
    return FireMuzzleCone;
}

void CEGun::ConsumeFireResources()
{
    CELauncher::ConsumeFireResources();
    LockElapsed = 0.0f;
}

void CEGun::ComputeLaunchInfo_OneBarrel(ProjLaunchInfo& Out, int Barrel,
                                       Vector const& Target) const
{
    // .text:06297870..06297af6. LockTarget / LockSubId keep the caller's default unless the lock
    // block at the end writes them. No null or class tests on Projectile, Owner or Arch.
    Archetype::Munition const* const Munition = (Archetype::Munition const*)(Projectile);
    if (Munition->Seeker == 0)                                                  // 06297876..06297884
    {
        // 0629788a..06297912: owner vt+0x54 velocity; Pos goes straight into Out.Pos; velocity =
        // Vector(DirVel + OwnerVel) (float sums); orientation along that total velocity.
        Vector const OwnerVel = Owner->get_velocity();
        Vector DirVel;
        ComputeProjectilePosAndVelocity_OneBarrel(Out.Pos, DirVel, Barrel, Target);
        Out.Velocity.x = DirVel.x + OwnerVel.x;
        Out.Velocity.y = DirVel.y + OwnerVel.y;
        Out.Velocity.z = DirVel.z + OwnerVel.z;
        Out.Orient = LookMatrix(Out.Velocity);
    }
    else
    {
        CELauncher::ComputeLaunchInfo_OneBarrel(Out, Barrel, Target);          // 06297927
    }

    // 0629792c..06297940: Arch+0xa0 FCOMP 0.0, TEST AH,0x41 / JNZ skips the spread when not
    // greater (or unordered).
    float const Spread = GunArch()->DispersionAngle;
    if (Spread > 0.0f)
    {
        Vector& Vel = Out.Velocity;
        Vector const Rand = RandomVector(1.0f);                                 // 06297953
        // 06297958..06297993: Perp = Vel x Rand, each component FSTP'd to float.
        Vector Perp;
        Perp.x = (float)((double)(Vel.z) * Rand.y - (double)(Rand.z) * Vel.y);
        Perp.y = (float)((double)(Rand.z) * Vel.x - (double)(Vel.z) * Rand.x);
        Perp.z = (float)((double)(Vel.y) * Rand.x - (double)(Vel.x) * Rand.y);
        // 06297997..062979b1: Speed = sqrt((x*x + y*y) + z*z), FSTP'd to float.
        float const Speed = (float)(sqrt(((double)(Vel.x) * Vel.x +
                                                     (double)(Vel.y) * Vel.y) +
                                                    (double)(Vel.z) * Vel.z));
        // 062979bb..062979f9: FILD rand() * 2^-15 (DAT_063997d0) * Spread, FPTAN, * Speed, divided
        // by sqrt((pz*pz + py*py) + px*px); all on the FPU stack, no zero-length guard.
        int const RandValue = rand();
        double const Lean = std::tan((double)(RandValue) * (1.0 / 32768.0) * Spread) * Speed;
        double const Scale = Lean / sqrt(((double)(Perp.z) * Perp.z +
                                          (double)(Perp.y) * Perp.y) +
                                         (double)(Perp.x) * Perp.x);
        // 062979fb..06297a32: x keeps its unrounded product; y and z are FSTP'd first.
        double const LeanX = Perp.x * Scale;
        float const LeanY = (float)(Perp.y * Scale);
        float const LeanZ = (float)(Perp.z * Scale);
        Vel.x = (float)(LeanX + Vel.x);
        Vel.y = LeanY + Vel.y;
        Vel.z = LeanZ + Vel.z;
        // 06297a35..06297a6a: rescale to Speed; Speed / sqrt((x*x + y*y) + z*z) kept on the stack.
        double const Keep = (double)(Speed) /
                            sqrt(((double)(Vel.x) * Vel.x + (double)(Vel.y) * Vel.y) +
                                 (double)(Vel.z) * Vel.z);
        Vel.x = (float)(Keep * Vel.x);
        Vel.y = (float)(Keep * Vel.y);
        Vel.z = (float)(Keep * Vel.z);
        Out.Orient = LookMatrix(Vel);                                           // 06297a6d
    }

    // 06297a81..06297aeb: a lock seeker (2) whose TimeToLock - LockElapsed (FPU stack) is <= 0
    // (TEST AH,0x41 pair; unordered skips) hands its watched target (W - 0x9c, both W and the
    // result non-null) and sub id to the record.
    if (Munition->Seeker == 2)
    {
        double const Remaining = (double)(Munition->TimeToLock) - LockElapsed;
        if (Remaining <= 0.0)
        {
            CSimple* const Locked = DecodeLockWatchable(LockWatcher.target());
            if (Locked != nullptr)
            {
                Out.LockTarget = Locked;
                Out.LockSubId = LockSubId;
            }
        }
    }
}

bool CEGun::SetTarget(CSimple* Target, unsigned short SubId)
{
    // Common.dll.c 0x381e0: watcher + subid + zero +0x26c. Turret aim if joints and age > 2.
    CSimple* Current = DecodeLockWatchable(LockWatcher.target());
    if (Target == Current && SubId == LockSubId)
        return false;
    LockElapsed = 0.0f;
    // 06298233-06298243: W = Target ? Target + 0x9c : 0; W != [+0x250] -> set_pointer(W),
    // i.e. BaseWatcher::set -- registers the gun in the target's Watchable chain.
    LockWatcher.set(EncodeLockWatchable(Target));
    LockSubId = SubId;
    if ((Yaw.EngineIndex != kInvalidEngineInstance ||
         Pitch.EngineIndex != kInvalidEngineInstance) &&
        DecodeLockWatchable(LockWatcher.target()) != nullptr && kTargetPointHold < TargetPointAge)
    {
        float YawRad = 0.0f;
        float PitchRad = 0.0f;
        ComputeAimAngles(*this, YawRad, PitchRad, true, TargetPoint);
        if (Yaw.EngineIndex != kInvalidEngineInstance &&
            (double)(kSeeTargetAngle) * 0.1f < fabs((double)(YawRad) - Yaw.Current))
            AnimDB::Add(&Yaw);
        Yaw.Desired = YawRad;
        if (Pitch.EngineIndex != kInvalidEngineInstance &&
            (double)(kSeeTargetAngle) * 0.1f < fabs((double)(PitchRad) - Pitch.Current))
            AnimDB::Add(&Pitch);
        Pitch.Desired = PitchRad;
    }
    return true;
}

void CEGun::GetTarget(CSimple*& OutTarget, unsigned short& OutSubId) const
{
    // Listing 0x38440.
    OutTarget = DecodeLockWatchable(LockWatcher.target());
    OutSubId = LockSubId;
}

void CEGun::SetTargetPoint(Vector const& Point)
{
    // Common.dll.c 0x38470: store world - origin, zero +0x268.
    Vector Origin = GetTurretOrigin();
    TargetPoint.x = Point.x - Origin.x;
    TargetPoint.y = Point.y - Origin.y;
    TargetPoint.z = Point.z - Origin.z;
    TargetPointAge = 0.0f;
}

bool CEGun::Update(float Dt, unsigned Flags)
{
    // .text:06297120-06297501: turret aim, per-frame joint animation, then seeker lock.
    if (!CELauncher::Update(Dt, Flags))
        return false;

    if (Yaw.EngineIndex != kInvalidEngineInstance ||
        Pitch.EngineIndex != kInvalidEngineInstance)
    {
        // FST stores float age, but the following comparison retains the unrounded sum.
        double const NewAge = (double)(Dt) + TargetPointAge;
        TargetPointAge = (float)(NewAge);
        float YawRad = -0.0f;
        float PitchRad = 0.0f;
        // 0629717c TEST AH,0x5 / JP: equal, greater or unordered takes the lead branch.
        if (NewAge < kTargetPointHold)
        {
            ComputeAimAngles(*this, YawRad, PitchRad, false, TargetPoint);
        }
        else if (DecodeLockWatchable(LockWatcher.target()) != nullptr)
        {
            ComputeAimAngles(*this, YawRad, PitchRad, true, TargetPoint);
        }
        if (Yaw.EngineIndex != kInvalidEngineInstance &&
            (double)(kSeeTargetAngle) * 0.1f < fabs((double)(YawRad) - Yaw.Current))
            AnimDB::Add(&Yaw);
        Yaw.Desired = YawRad;
        if (Pitch.EngineIndex != kInvalidEngineInstance &&
            (double)(kSeeTargetAngle) * 0.1f < fabs((double)(PitchRad) - Pitch.Current))
            AnimDB::Add(&Pitch);
        Pitch.Desired = PitchRad;
    }

    if (DecodeLockWatchable(LockWatcher.target()) == nullptr)
        return true;
    // 062973f6 TEST AH,0x41 / JNZ: non-positive or unordered returns without changing lock.
    if (!(MunitionArch()->TimeToLock > 0.0f))
        return true;

    bool bIncrement = false;
    if ((Yaw.EngineIndex == kInvalidEngineInstance &&
         Pitch.EngineIndex == kInvalidEngineInstance) ||
        !(kTargetPointHold < TargetPointAge))
    {
        Vector Lead;
        if (ComputeTgtLeadPosition(Lead) && CanSeeTargetPoint(Lead, kSeeTargetAngle))
            bIncrement = true;
    }
    else
    {
        // 0629744e-06297484: |desired-current|, unbounded delta>pi -> pi-delta.
        // TEST AH,0x5 / JP resets when delta >= threshold or unordered.
        if (!JointWithin(Yaw, kSeeTargetAngle))
        {
            LockElapsed = 0.0f;
            return true;
        }
        // FUN_06297560: an unbound pitch is within; otherwise the same ordered delta test.
        if (JointWithin(Pitch, kSeeTargetAngle))
            bIncrement = true;
    }

    if (bIncrement)
        LockElapsed = Dt + LockElapsed;
    else
        LockElapsed = 0.0f;
    return true;
}

// The joint walk retail inlines in both CEGun::Connect (.text:062975d1..06297655) and
// CEGun::LocateJoints (.text:062980dd..06298162): IEngine+0xc4 GetInstanceRelative(Root) is
// dereferenced with NO null test and a type of 2 binds the yaw joint (FUN_06299080 on +0x1f4);
// then every child from IEngine+0xd8 GetRelatedInstance(Root, 1, prev) (starting at -1, until
// -1) whose Relative block is non-null with type 2 binds the pitch joint (+0x210) -- the last
// such child wins.
static void BindTurretJoints(CEGun& Gun, long Root)
{
    void* const RootInst = (void*)(Root);
    if (InstanceRelative(Root)->Type == kJointTypeRev)
        Gun.Yaw.Bind(&Gun, Root);

    long Child = (long)(DALib::Engine->GetRelatedInstance(RootInst, 1, G_InvalidEngineHandle));
    while (Child != kInvalidEngineInstance)
    {
        FJointRelative const* const ChildRel = InstanceRelative(Child);
        if (ChildRel != nullptr && ChildRel->Type == kJointTypeRev)
            Gun.Pitch.Bind(&Gun, Child);
        Child = (long)(
            DALib::Engine->GetRelatedInstance(RootInst, 1, (void*)(Child)));
    }
}

bool CEGun::Connect(char const* HpName)
{
    // Common.dll.c raw 0x062975b0 (listing 0x375b0): CELauncher::Connect, IEngine+0xc4 type==2 →
    // yaw, +0xd8 children type==2 → pitch, then an unconditional CALL CEGun::ComputeTurretFrame
    // right before returning true (confirmed against the raw listing -- ComputeTurretFrame is
    // no longer left as an identity-matrix skip now that it has been ported below).
    // .text:062975b8..062975bf: CELauncher::Connect false -> false.
    if (!CELauncher::Connect(HpName))
        return false;
    // .text:062975c5..06297657 (same walk as LocateJoints), then ComputeTurretFrame, true.
    BindTurretJoints(*this, GetRootIndex());
    ComputeTurretFrame();
    return true;
}

void CEGun::Disconnect()
{
    // Common.dll.c 0x37670 / LoseJoints 0x38170 then CELauncher::Disconnect.
    LoseJoints();
    CELauncher::Disconnect();
}

void CEGun::ComputeTurretFrame()
{
    // Common.dll.c raw 0x06297c70 (listing 0x37c70, ?ComputeTurretFrame@CEGun@@IAEXXZ). Ghidra's
    // decompilation for this function is badly corrupted (register-recycled "float**"/"int*"
    // locals that are really raw struct/array pointers reinterpreted as pointers-to-pointers) and
    // was not trusted -- reconstructed instruction-by-instruction from the raw .txt listing,
    // tracking every ESP-relative local by hand against a fixed post-prologue baseline (all calls
    // in this function are stdcall/thiscall-with-callee-cleanup, so ESP returns to that baseline
    // after every CALL -- confirmed by cross-checking every "MOV [ESP+n]" write against the read
    // that later consumes it).
    //
    // Yaw's reference frame is this equip's own connector instance (GetParentConnector(false),
    // CExternalEquip's own vtable+0x64) and that instance's world orientation (ConnOri,
    // GetEngineInstanceOrientation / IEngine+0x98). If Yaw is bound, its axis comes straight from
    // GetInstanceRelative(Root)+0x40 (a cached local rotation-axis Vector); otherwise it falls
    // back to transforming local "up" (0,1,0) by GetInstanceRelative(Root)'s own static/rest
    // orientation matrix at +0x1c. Either way the local axis is transformed into world space by
    // ConnOri (Acquire3DMathEngine() slot 0x2c TransformMatrix) to get YawAxisWS.
    //
    // Pitch is symmetric but keyed off its own engine PARENT instance (IEngine+0xd4 on
    // Pitch.EngineIndex -- i.e. the yaw joint sitting above it in the engine hierarchy) rather
    // than the equip's connector. If Pitch is bound, its axis is GetInstanceRelative(Pitch)+0x40
    // transformed by that parent's own orientation; otherwise it falls back to transforming local
    // "right" (1,0,0) by the ROOT instance's own raw orientation (IEngine+0x98 on Root directly --
    // NOT GetInstanceRelative(Root)+0x1c; retail genuinely reads a different source here than
    // Yaw's fallback does, confirmed by the raw opcode being 0x98 not 0xc4 in this branch).
    //
    // CrossAxisWS = PitchAxisWS x YawAxisWS (confirmed by hand-expanding the raw FLD/FMUL/FSUBP/
    // FXCH chain term-by-term against the standard cross-product formula; every component matched
    // exactly).
    //
    // Retail's FUN_06284f70 (a trivial 9-float positional store into *ECX -- confirmed from its
    // own trivial body, RET 0x24 = 9 stack dwords) is called here with all 9 args sourced from
    // ConnOri alone, in a scrambled push order that -- once every push/read pair is matched back
    // to its exact ConnOri[i] source slot -- turns out to be nothing but Transpose(ConnOri)
    // (dest[0..8] = ConnOri[0,3,6, 1,4,7, 2,5,8]). Built directly here via the Matrix constructor
    // instead of replaying that scramble.
    //
    // Final: TurretFrame = Transpose(ConnOri) * [ PitchAxisWS | YawAxisWS | CrossAxisWS ]
    // (columns, 3x3 multiply via Acquire3DMathEngine() slot 0x20 MulMatrix -- confirmed by the
    // call's own destination address matching CEGun+0x22c/TurretFrame exactly after the final
    // 9-float store). This re-expresses the world-space turret basis in the connector's local
    // frame -- i.e. TurretFrame is relative orientation, not a world matrix.
    // .text:06297c70..06297fb6. No null tests anywhere (engine, relative blocks, math engine).
    long const Root = GetRootIndex();                                             // vt+0x84
    long const Conn = GetParentConnector(false);                                  // vt+0x64
    Matrix const ConnOri = *(Matrix const*)DALib::Engine->GetInstanceOrientation((void*)Conn); // +0x98

    // 06297ca3..06297d84: the yaw axis is always ConnOri times a local axis. Bound yaw: the
    // root's relative axis (+0x40). Unbound: the root's rest orientation (+0x1c) times (0,1,0)
    // first (06297ce6..06297d53), and that result goes through ConnOri as well.
    Vector LocalYaw;
    if (Yaw.EngineIndex != kInvalidEngineInstance)
    {
        LocalYaw = InstanceRelative(Root)->Axis;
    }
    else
    {
        FJointRelative const* const Rel = InstanceRelative(Root);
        Vector Up;
        Up.x = 0.0f;
        Up.y = 1.0f;
        Up.z = 0.0f;
        Acquire3DMathEngine()->TransformMatrix((FVector&)LocalYaw, (FMatrix const&)Rel->Orient,
                                               (FVector const&)Up);           // +0x2c
    }
    Vector YawAxisWS;
    Acquire3DMathEngine()->TransformMatrix((FVector&)YawAxisWS, (FMatrix const&)ConnOri,
                                           (FVector const&)LocalYaw);         // 06297d75 +0x2c

    // 06297d88..06297eaa: bound pitch: its relative axis (+0x40) through its engine parent's
    // orientation (IEngine+0xd4, then +0x98); unbound: (1,0,0) through the root's orientation.
    Vector PitchAxisWS;
    if (Pitch.EngineIndex != kInvalidEngineInstance)
    {
        void* const Parent = DALib::Engine->GetInstanceParent((void*)(long)Pitch.EngineIndex);
        Matrix const ParentOri = *(Matrix const*)DALib::Engine->GetInstanceOrientation(Parent);
        FJointRelative const* const Rel = InstanceRelative(Pitch.EngineIndex);
        Acquire3DMathEngine()->TransformMatrix((FVector&)PitchAxisWS, (FMatrix const&)ParentOri,
                                               (FVector const&)Rel->Axis);    // +0x2c
    }
    else
    {
        Matrix const RootOri = *(Matrix const*)DALib::Engine->GetInstanceOrientation((void*)Root);
        Vector Right;
        Right.x = 1.0f;
        Right.y = 0.0f;
        Right.z = 0.0f;
        Acquire3DMathEngine()->TransformMatrix((FVector&)PitchAxisWS, (FMatrix const&)RootOri,
                                               (FVector const&)Right);        // +0x2c
    }

    // 06297eac..06297f41: Cross = Pitch x Yaw, products and difference on the FPU stack, each
    // component FSTP'd to float.
    Vector CrossAxisWS;
    CrossAxisWS.x = (float)((double)(YawAxisWS.z) * PitchAxisWS.y -
                                       (double)(PitchAxisWS.z) * YawAxisWS.y);
    CrossAxisWS.y = (float)((double)(PitchAxisWS.z) * YawAxisWS.x -
                                       (double)(YawAxisWS.z) * PitchAxisWS.x);
    CrossAxisWS.z = (float)((double)(YawAxisWS.y) * PitchAxisWS.x -
                                       (double)(PitchAxisWS.y) * YawAxisWS.x);

    // 06297f65 FUN_06284f70 = Matrix ctor fed the transpose of ConnOri; 06297f95 MulMatrix
    // (+0x20) into a temporary, copied to TurretFrame (+0x22c) at 06297f98..06297faa.
    Matrix const ConnOriT(ConnOri.E[0][0], ConnOri.E[1][0], ConnOri.E[2][0],
                          ConnOri.E[0][1], ConnOri.E[1][1], ConnOri.E[2][1],
                          ConnOri.E[0][2], ConnOri.E[1][2], ConnOri.E[2][2]);
    Matrix const Basis(PitchAxisWS.x, YawAxisWS.x, CrossAxisWS.x,
                       PitchAxisWS.y, YawAxisWS.y, CrossAxisWS.y,
                       PitchAxisWS.z, YawAxisWS.z, CrossAxisWS.z);
    Matrix Frame;
    Acquire3DMathEngine()->MulMatrix((FMatrix&)Frame, (FMatrix const&)ConnOriT, (FMatrix const&)Basis);
    TurretFrame = Frame;
}

void CEGun::DecomposeJointMotion(Vector const& Dir, float& OutYaw, float& OutPitch) const
{
    // .text:06297fc0-062980ca. Conn = this->vtbl[0x64](0) = GetParentConnector(false);
    // 9 floats copied (REP MOVSD) from DALib::Engine slot 0x98 (GetInstanceOrientation) of it;
    // Combined = MulMatrix(ConnOri, TurretFrame +0x22c) (3DMathEngine slot 0x20, the lazily
    // acquired DAT_063fc184); Inv = Transpose(Combined) (the nine FLD/FSTP pairs at
    // .text:06298018-06298066 read Combined[0,3,6,1,4,7,2,5,8]); Local = TransformMatrix(Inv, Dir)
    // (slot 0x2c). Yaw: FLD x, FLD z, FCHS, FPATAN -> atan2(x, -z). Pitch: y / FSQRT(z*z + x*x)
    // (FDIVP ST1/ST0), FLD1, FPATAN -> atan2(y / hypot, 1). No null test of the engine, the
    // orientation pointer or the math engine in retail.
    long Conn = GetParentConnector(false);
    float const* Ori = DALib::Engine->GetInstanceOrientation((void*)(Conn));
    Matrix ConnOri(Ori[0], Ori[1], Ori[2], Ori[3], Ori[4], Ori[5], Ori[6], Ori[7], Ori[8]);

    I3DMathEngine* Math = Acquire3DMathEngine();
    Matrix Combined;
    Math->MulMatrix((FMatrix&)(Combined),
                    (FMatrix const&)(ConnOri),
                    (FMatrix const&)(TurretFrame)); // +0x20
    Matrix Inv(Combined.E[0][0], Combined.E[1][0], Combined.E[2][0],
               Combined.E[0][1], Combined.E[1][1], Combined.E[2][1],
               Combined.E[0][2], Combined.E[1][2], Combined.E[2][2]);

    Math = Acquire3DMathEngine();
    Vector Local;
    Math->TransformMatrix((FVector&)(Local),
                          (FMatrix const&)(Inv),
                          (FVector const&)(Dir)); // +0x2C

    OutYaw = static_cast<float>(std::atan2(static_cast<double>(Local.x), -(double)(Local.z)));
    double Horiz = sqrt((double)(Local.z) * Local.z + (double)(Local.x) * Local.x);
    OutPitch = static_cast<float>(std::atan((double)(Local.y) / Horiz));
}

void CEGun::LocateJoints()
{
    // Common.dll.c raw 0x062980d0 (listing 0x380d0, ?LocateJoints@CEGun@@IAEXXZ). The same
    // joint walk as Connect() (BindTurretJoints) without the CELauncher::Connect gate, ending in
    // a tail-call to ComputeTurretFrame. Its only reference is the export table entry 0x63b67ec.
    // .text:062980d4..062980d7: Root = vtable+0x84 (GetRootIndex); the walk is
    // BindTurretJoints (above Connect); 06298169 JMP ComputeTurretFrame.
    BindTurretJoints(*this, GetRootIndex());
    ComputeTurretFrame();
}

Vector CEGun::GetTurretOrigin() const
{
    // .text:06297b20..06297c47. No null tests.
    if (Pitch.EngineIndex != kInvalidEngineInstance)
    {
        // 06297b3c..06297bed: the pitch joint's world position (IEngine+0x90) minus its
        // orientation (+0x98, 9-dword copy) applied to the relative child position (+0xc4 block
        // +0x10) by 3DMathEngine+0x2c; each difference FSTP'd to float.
        void* const PitchInst = (void*)(long)Pitch.EngineIndex;
        Matrix const PitchOri = *(Matrix const*)DALib::Engine->GetInstanceOrientation(PitchInst);
        Vector const PitchPos = *(Vector const*)DALib::Engine->GetInstancePosition(PitchInst);
        FJointRelative const* const Rel = InstanceRelative(Pitch.EngineIndex);
        Vector Offset;
        Acquire3DMathEngine()->TransformMatrix((FVector&)Offset, (FMatrix const&)PitchOri,
                                               (FVector const&)Rel->ChildPos);
        Vector Origin;
        Origin.x = PitchPos.x - Offset.x;
        Origin.y = PitchPos.y - Offset.y;
        Origin.z = PitchPos.z - Offset.z;
        return Origin;
    }
    // 06297bf3..06297c29: the yaw joint's position, else the root instance's (vt+0x84).
    if (Yaw.EngineIndex != kInvalidEngineInstance)
        return *(Vector const*)DALib::Engine->GetInstancePosition((void*)(long)Yaw.EngineIndex);
    return *(Vector const*)DALib::Engine->GetInstancePosition((void*)GetRootIndex());
}

bool CEGun::IsJointInMotion() const
{
    // .text:06297b00..06297b1c: byte +0x20c (Yaw +0x18) and byte +0x228 (Pitch +0x18) must
    // both be non-zero. Despite the export's name that byte is JointChannel::bSettled -- Tick
    // sets it when the joint moved less than 1e-4 (see CEGun::JointChannel::Tick).
    return Yaw.bSettled != 0 && Pitch.bSettled != 0;
}

bool CEGun::IsMovable() const
{
    // Common.dll.c 0x37c50: false only when neither joint was ever located.
    return !(Yaw.EngineIndex == kInvalidEngineInstance && Pitch.EngineIndex == kInvalidEngineInstance);
}

float CEGun::GetMunitionRange() const
{
    // .text:06298e70..06298ed2: 0 if destroyed (vt+8); a lock seeker (+0x90 == 2) whose
    // SeekerRange (+0x98) is greater than 0 (TEST AH,0x41/JNZ) and SeekerFovCos (+0x9c) is below
    // 1.0 (TEST AH,0x5/JP) answers with that range; otherwise JMP Gun::get_munition_range on
    // Arch. No null or class tests.
    if (IsDestroyed())
        return 0.0f;
    Archetype::Munition const* const Proj = (Archetype::Munition const*)(Projectile);
    if (Proj->Seeker == 2 && Proj->SeekerRange > 0.0f && Proj->SeekerFovCos < 1.0f)
        return Proj->SeekerRange;
    return GunArch()->get_munition_range();
}

float CEGun::GetSecondsTillLock() const
{
    // .text:06298ee0..06298f17: a lock seeker (+0x90 == 2) returns TimeToLock (+0x94) -
    // LockElapsed (+0x26c) from the FPU stack; 0.0 FCOMP it, TEST AH,0x41/JNZ keeps it unless
    // 0 is greater (NaN passes through). Anything else returns 0. No null or class tests.
    Archetype::Munition const* const Proj = (Archetype::Munition const*)(Projectile);
    if (Proj->Seeker != 2)
        return 0.0f;
    double const Remaining = (double)(Proj->TimeToLock) - LockElapsed;
    if (0.0 > Remaining)
        return 0.0f;
    return (float)(Remaining);
}

bool CEGun::CanSeeTargetPoint(Vector const& Point, float Angle) const
{
    // Listing .text:062984d0-06298582 (RET 8). No zero-length guard: a zero Delta divides by
    // zero and the NaN dot falls into the unordered -> false side.
    // 062984d4-062984f9: Delta = Point - GetBarrelPosWS(0); x and y stay on the FPU stack,
    // z is FST'd to float (062984ff) but retained on the stack for the following multiply.
    Vector Muzzle = GetBarrelPosWS(0);
    double const Dx = (double)(Point.x) - Muzzle.x;
    double const Dy = (double)(Point.y) - Muzzle.y;
    double const DzRetained = (double)(Point.z) - Muzzle.z;
    float const Dz = (float)(DzRetained);
    // 06298503-0629851b: Inv = 1 / sqrt((zRetained*zStored + x*x) + y*y), stored as float.
    double const Len = sqrt((DzRetained * Dz + Dx * Dx) + Dy * Dy);
    float const Inv = (float)(1.0 / Len);
    // 0629851f-06298539: each normalized component stored as float.
    float const Nx = (float)(Dx * Inv);
    float const Ny = (float)(Dy * Inv);
    float const Nz = (float)((double)(Dz) * Inv);
    // 0629853d-06298570: Dir = GetBarrelDirWS(0); dot = Dir.z*Nz + Dir.y*Ny + Nx*Dir.x on the
    // FPU; FCOS of the float Angle; FCOMPP cos vs dot, TEST AH,0x5 / JP -> false when cos is
    // greater, equal or unordered; true only when cos < dot.
    Vector Dir = GetBarrelDirWS(0);
    double const Dot = (double)(Dir.z) * Nz + (double)(Dir.y) * Ny +
                       (double)(Nx) * Dir.x;
    double const ConeCos = std::cos(static_cast<double>(Angle));
    return ConeCos < Dot;
}

bool CEGun::CanSeeTargetObject(float Angle) const
{
    // Listing .text:06298590-0629873e (RET 4).
    // 06298597-062985ae: watcher empty or W - 0x9c null -> false.
    if (DecodeLockWatchable(LockWatcher.target()) == nullptr)
        return false;

    // 062985b4-062985de: a bound joint (Pitch +0x218 or Yaw +0x1fc != -1) and TargetPointAge
    // (+0x268) > TARGET_POINT_VALID_TIME (TEST AH,0x41 / JNZ takes the lead path when not
    // greater): the turret answers from its joints alone. 062985e4-06298662: each bound joint
    // (Yaw first, then Pitch) needs |Current - Desired| (pi-folded when !bLimited, DAT_0639d208
    // = pi) < Angle (TEST AH,0x5 / JP -> false on greater, equal or unordered); then true.
    if ((Pitch.EngineIndex != kInvalidEngineInstance || Yaw.EngineIndex != kInvalidEngineInstance) &&
        kTargetPointHold < TargetPointAge)
    {
        if (!JointWithin(Yaw, Angle))
            return false;
        if (!JointWithin(Pitch, Angle))
            return false;
        return true;
    }

    // 06298672-06298721: lead position, then the same barrel-0 cone test CanSeeTargetPoint
    // (062984d0) performs, inlined here term for term.
    Vector Lead;
    if (!ComputeTgtLeadPosition(Lead))
        return false;
    return CanSeeTargetPoint(Lead, Angle);
}

namespace
{
    // One joint's leg of CEGun::CanPointAt: yaw .text:06298974-06298a36, pitch 06298a3c-06298ae3.
    // The window starts as [-Angle, Angle]; a bound joint widens it by its engine limits
    // (IEngine+0xc4 GetInstanceRelative, +0x4c min / +0x50 max -- no null test in retail), the low
    // end FSTP'd to float. Then, on the FPU stack, Lo -= 2pi while Lo > 0 (FCOM double 0.0,
    // TEST AH,0x41 / JZ loops on greater) and Hi += 2pi while Hi < 0 (TEST AH,0x5 / JNP loops on
    // less), 2pi = float DAT_0639c9e8. Value passes when Lo <= Value (FCOMP, TEST AH,0x41 / JP
    // fails on greater or unordered) and Value <= Hi (same test).
    bool JointWindowContains(int EngineIndex, float Angle, float NegAngle, float Value)
    {
        float Lo = NegAngle;
        double Hi = (double)(Angle);
        if (EngineIndex != kInvalidEngineInstance)
        {
            FJointRelative const* const Rel = InstanceRelative(EngineIndex);
            Lo = (float)((double)NegAngle + Rel->Min);
            Hi = (double)Angle + Rel->Max;
        }
        double const TwoPi = (double)(kGunTwoPi);   // 0x40c90fdb
        double LoX = (double)(Lo);
        while (LoX > 0.0)
            LoX -= TwoPi;
        while (Hi < 0.0)
            Hi += TwoPi;
        if (!(LoX <= (double)(Value)))
            return false;
        if (!((double)(Value) <= Hi))
            return false;
        return true;
    }
}

bool CEGun::CanPointAt(Vector const& Point, float Angle) const
{
    // Listing .text:06298860-06298b0b (RET 8).
    // 0629886a-06298874: IsDestroyed() (vtable +0x8) -> false.
    if (IsDestroyed())
        return false;

    // 0629887c-062988ae: Dir = Point - GetTurretOrigin(), each component FSTP'd to float.
    Vector Origin = GetTurretOrigin();
    Vector Dir;
    Dir.x = Point.x - Origin.x;
    Dir.y = Point.y - Origin.y;
    Dir.z = Point.z - Origin.z;
    // 062988b2-06298970: DecomposeJointMotion (06297fc0) inlined term for term --
    // GetParentConnector(false) (vtable +0x64), IEngine+0x98 orientation, MulMatrix(ConnOri,
    // TurretFrame) (FUN_06288900 = 3DMathEngine +0x20), transpose (FUN_06284f70 with
    // C0,C3,C6,C1,C4,C7,C2,C5,C8), TransformMatrix (FUN_062888b0 = 3DMathEngine +0x2c), yaw
    // atan2(x, -z), pitch atan2(y / hypot(x, z), 1), both stored as float.
    float YawRad = 0.0f;
    float PitchRad = 0.0f;
    DecomposeJointMotion(Dir, YawRad, PitchRad);

    // 06298974-0629897d: -Angle kept as a float for both legs. Yaw is tested negated
    // (06298a10-06298a16 FCHS / FSTP float), pitch as is.
    float const NegAngle = -Angle;
    if (!JointWindowContains(Yaw.EngineIndex, Angle, NegAngle, -YawRad))
        return false;
    if (!JointWindowContains(Pitch.EngineIndex, Angle, NegAngle, PitchRad))
        return false;
    return true;
}

bool CEGun::GetTargetObjectPos(Vector& Out) const
{
    // Listing .text:06298750-0629885d (RET 4).
    // 06298756-0629876b: watcher empty or W - 0x9c null -> false, Out untouched.
    CSimple* Target = DecodeLockWatchable(LockWatcher.target());
    if (Target == nullptr)
        return false;
    // 06298771-062987e1: LockSubId != SUBOBJ_ID_NONE and the target is a CEqObj
    // (ObjectClass & 0x103 == 0x103): vtable +0xe0 get_sub_obj_center_of_mass(SubId, Out);
    // true -> done.
    if (LockSubId != SUBOBJ_ID_NONE &&
        ((unsigned)(Target->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        CEqObj const* EqTarget = (CEqObj const*)(Target);
        if (EqTarget->get_sub_obj_center_of_mass(LockSubId, Out))
            return true;
    }
    // 062987e3-06298811 / 0629881b-0629884d: vtable +0x68 get_center_of_mass().
    Out = Target->get_center_of_mass();
    return true;
}

bool CEGun::ComputeTimeToTgt(Vector const& RelPos, Vector const& RelVel, float Speed,
                             float& OutTime)
{
    // .text:06298f40..0629907c. Dot, discriminant, root and reciprocal remain on the
    // FPU stack; only the quadratic coefficient (06298f80) and roots are stored as float.
    double const Dot = (double)(RelPos.y) * RelVel.y +
                       (double)(RelPos.z) * RelVel.z +
                       (double)(RelPos.x) * RelVel.x;
    float const Vel2 = (float)((double)(RelVel.x) * RelVel.x +
                                         (double)(RelVel.y) * RelVel.y +
                                         (double)(RelVel.z) * RelVel.z -
                                         (double)(Speed) * Speed);
    double const Pos2 = (double)(RelPos.x) * RelPos.x +
                        (double)(RelPos.y) * RelPos.y +
                        (double)(RelPos.z) * RelPos.z;
    double const Disc = Dot * Dot - Pos2 * Vel2;
    // FCOM zero; TEST AH,1; JNZ -> failure for negative or unordered discriminants.
    if (!(Disc >= 0.0))
        return false;
    double const Root = sqrt(Disc);
    double const Inv = 1.0 / Vel2; // 06298fcb: retail has no zero-denominator guard.
    float const T1 = (float)((Root - Dot) * Inv);
    float const T2 = (float)((-Dot - Root) * Inv);
    // FCOMP zero; TEST AH,0x41; JNZ -> non-positive or unordered, fall-through positive.
    bool const bFirstPositive = T1 > 0.0f;
    bool const bSecondPositive = T2 > 0.0f;
    if (!bFirstPositive && !bSecondPositive)
        return false;
    // Both positive: FCOMP T2; TEST AH,5; JP -> T2 for not-less, T1 for less.
    OutTime = bFirstPositive && (!bSecondPositive || T1 < T2) ? T1 : T2;
    return true;
}

bool CEGun::ComputeTgtLeadPosition(Vector& Out) const
{
    // Listing .text:06298b10-06298e65 (RET 4). Out is written only on the true paths.
    // 06298b16-06298b38: watcher empty, or W - 0x9c null, or IsDestroyed() (vtable +0x8) -> false.
    CSimple* Target = DecodeLockWatchable(LockWatcher.target());
    if (Target == nullptr)
        return false;
    if (IsDestroyed())
        return false;

    Vector TgtPos;
    Vector TgtVel;
    // 06298b3e-06298b7c: the sub-target path needs LockSubId != SUBOBJ_ID_NONE and a target
    // whose ObjectClass & 0x103 == 0x103 (a CEqObj).
    if (LockSubId != SUBOBJ_ID_NONE &&
        ((unsigned)(Target->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        CEqObj const* EqTarget = (CEqObj const*)(Target);
        // 06298bad-06298bef: vtable +0xe0 get_sub_obj_center_of_mass(SubId, Pos); on false
        // the whole-object vtable +0x68 get_center_of_mass().
        if (!EqTarget->get_sub_obj_center_of_mass(LockSubId, TgtPos))
            TgtPos = Target->get_center_of_mass();
        // 06298bf3-06298c21 / 06298c69-06298c83: vtable +0xdc get_sub_obj_velocity(SubId, Vel);
        // on false vtable +0x54 get_velocity().
        if (!EqTarget->get_sub_obj_velocity(LockSubId, TgtVel))
            TgtVel = Target->get_velocity();
    }
    else
    {
        // 06298c23-06298c83: vtable +0x68 get_center_of_mass(), then vtable +0x54 get_velocity().
        TgtPos = Target->get_center_of_mass();
        TgtVel = Target->get_velocity();
    }

    // 06298c87-06298c96: IEngine +0x80 GetInstanceUserData(Barrels[0].EngineIndex) (gun+0x54),
    // called unconditionally. Non-null: that CObject's WorldPosition (+0x2c, a field read) and
    // its vtable +0x54 get_velocity(); null (06298d1f): the owner's (+0x4) WorldPosition and
    // get_velocity(). Each difference is rounded to float (FSTP).
    CObject* Shooter = (CObject*)(DALib::Engine->GetInstanceUserData(
        (void*)((long)(Barrels[0].EngineIndex))));
    if (Shooter == nullptr)
        Shooter = Owner;
    Vector RelPos;
    RelPos.x = TgtPos.x - Shooter->WorldPosition.x;
    RelPos.y = TgtPos.y - Shooter->WorldPosition.y;
    RelPos.z = TgtPos.z - Shooter->WorldPosition.z;
    Vector ShootVel = Shooter->get_velocity();
    Vector RelVel;
    RelVel.x = TgtVel.x - ShootVel.x;
    RelVel.y = TgtVel.y - ShootVel.y;
    RelVel.z = TgtVel.z - ShootVel.z;

    // 06298d99-06298db3: speed = Arch (+0xc) MuzzleVelocity (+0x94); replaced by 1e9
    // (0x4e6e6b28) when the projectile (+0x4c) has a Seeker (+0x90) -- no class or null test.
    float Speed = GunArch()->MuzzleVelocity;
    if (MunitionArch()->Seeker != 0)
        Speed = kSeekerLeadSpeed;

    // 06298dbb-06298ddf: Time = 0; ComputeTimeToTgt(RelPos, RelVel, Speed, Time).
    float Time = 0.0f;
    if (!ComputeTimeToTgt(RelPos, RelVel, Speed, Time))
    {
        // 06298e3e: Out = target position.
        Out = TgtPos;
        return true;
    }
    // 06298de3-06298e35: Out = Pos + RelVel * Time -- the RELATIVE velocity (target minus
    // shooter), product kept on the FPU stack, sum stored as float.
    Out.x = (float)((double)(RelVel.x) * Time + TgtPos.x);
    Out.y = (float)((double)(RelVel.y) * Time + TgtPos.y);
    Out.z = (float)((double)(RelVel.z) * Time + TgtPos.z);
    return true;
}

// Exported named constants recovered from retail's .rdata (each has its own export ordinal, so
// these are real symbols other modules link against, not just literals).
// CEGun::TARGET_POINT_VALID_TIME -- ordinal 2046, .rdata 0x0639cedc.
float const CEGun::TARGET_POINT_VALID_TIME = 2.0f;

Archetype::Gun const* CEGun::GunArch() const
{
    return (Archetype::Gun const*)(Arch);
}

Archetype::Munition const* CEGun::MunitionArch() const
{
    // Listing 0x369c0 (ICF-shared): MOV EAX,[ECX+0x4c]; RET.
    return (Archetype::Munition const*)(Projectile);
}

void CEGun::LoseJoints()
{
    // .text:06298170..062981d7. For each joint: AnimDB::Rem if bound, then owner 0,
    // instance -1, limited 1, desired 0, current 0, settled 0. The vftable is left alone.
    if (Yaw.EngineIndex != kInvalidEngineInstance)
        AnimDB::Rem(&Yaw);
    Yaw.Owner = nullptr;
    Yaw.EngineIndex = kInvalidEngineInstance;
    Yaw.bLimited = 1;
    Yaw.Desired = 0.0f;
    Yaw.Current = 0.0f;
    Yaw.bSettled = 0;
    if (Pitch.EngineIndex != kInvalidEngineInstance)
        AnimDB::Rem(&Pitch);
    Pitch.Owner = nullptr;
    Pitch.EngineIndex = kInvalidEngineInstance;
    Pitch.Desired = 0.0f;
    Pitch.Current = 0.0f;
    Pitch.bSettled = 0;
    Pitch.bLimited = 1;
}
