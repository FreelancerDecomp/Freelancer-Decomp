// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- ThrustManager (SystemManager+0x18).
// CMN-SPLIT stage 1 (2026-10-14): split out of common_systemmanager.cpp unchanged.

#include "common_systemmanager.h"
#include "common_aisteer.h"   // SustainedThresholdTimer, ConstructGunConvergenceState, RefreshWeaponFireControlConfig
#include "common_behavior.h"  // Behavior_InvokeNodeSlotC (FUN_062e61f0)
#include "common_csys.h"      // Transform, Acquire3DMathEngine
#include "x86math.h"          // I3DMathEngine::TransposeTransform / TransformMatrix, FVector, FMatrix
#include "common_misc.h"      // SUBOBJ_ID_NONE
#include "common_iface.h"     // IBehaviorManager::Pathfind, IObjInspect, ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager::IsCruiseUnobstructed (FUN_062de160)
#include "common_iustategraph.h" // ClearAiTargetVector, BuildAiTargetList (ScanGunGroupTargets)
#include "common_cobject.h"    // CEqObj::EquipManager / get_behavior_interface (ScanGunGroupTargets)
#include "common_cequip.h"     // CELauncher::cast / GetBarrelPosWS (ScanGunGroupTargets)
#include "common_cobjdb.h"     // IObjDB::FindById (AppendSystemManagerTimerListEntry)
#include "dacom.h"             // DACOM_API

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <new>

// ============================================================================
// ThrustManager — SystemManager+0x18. See common_systemmanager.h for the
// layout derivation and for how it resolves the long-standing
// "SystemManager+0x18 size contradiction" (tracker items 1786/1796).
// ============================================================================

// FUN_062e6490's ThrustManager initialiser block, Common.dll.txt 242201-242214
// (item 1785; the surrounding function stays blocked on the gun-hardpoint
// resolver family, but THIS block is self-contained and fully traced).
void InitThrustManager(ThrustManager* Self, SystemManager* Owner)
{
    Self->Owner = Owner;                    // .text:062e653c  MOV [ESI+0x18],ESI
    Self->MaxThrottle = 5.0f;               // .text:062e654f  0x40a00000
    Self->AxisThrottle.x = 0.0f;            // .text:062e6556
    Self->AxisThrottle.y = 0.0f;            // .text:062e6559
    Self->AxisThrottle.z = 0.0f;            // .text:062e655c
    Self->bCruiseEngaged = 0;               // .text:062e654b  [ESI+0x2c]
    Self->bCruiseUnobstructed = 1;          // .text:062e6562  [ESI+0x2d]
    Self->bEngineActive = 0;                // .text:062e6547  [ESI+0x2e]
    Self->bAutoLevel = 1;                   // .text:062e653f  [ESI+0x2f]
    Self->bAutoAvoidance = 1;               // .text:062e6543  [ESI+0x30]

    // Retail then re-reads Owner back out of the field it just wrote, resolves
    // Owner->Watcher through the -8 downcast, and clears bAutoAvoidance when the
    // watched object answers true to is_player() (vtable+0xbc).
    // Common.dll.txt 242215-242232.
    IObjRW* const Target =
        ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
    if (Target != nullptr && ObjInspectOf(Target)->is_player())
        Self->bAutoAvoidance = 0;           // .text:062e658e
}

// ThrustManager::engage_cruise, .text:062e90a0, retail symbol
// `?engage_cruise@ThrustManager@@QAEX_N@Z` (tracker item 12724).
// Instruction-for-instruction:
//   062e90a6  CMP BL,1                     ; BL == bEngage
//   062e90ac  JNZ  -> body                 ; a DISENGAGE always runs the body...
//   062e90ae  MOV AL,[ESI+0x15]            ; ...an ENGAGE only when bCruiseUnobstructed
//   062e90b3  JZ   -> return               ;    is non-zero
//   062e90b5  PUSH 1; CALL FUN_062e91d0    ; SetEngineActive(true)
//   062e90be  ... ObjRwFromWatchable(Owner->Watcher) ...
//   062e90e1  CALL [EAX+0x5c]              ; IObjInspect::is_cruise_active(bool&, bool&)
//   062e90e4  CMP [ESP+0x10],BL            ; out-param #1 (the param_1 slot, reused) vs bEngage
//   062e90ea  CMP [ESI+0x14],BL            ; cached state vs bEngage
//   062e90ed  JZ   -> return               ; both already agree: nothing to do
//   062e9101  PUSH -1; PUSH 0; PUSH BL
//   062e910b  CALL [EAX+0x50]              ; IObjAffect::slot_50(bEngage, 0, -1) -- EngageCruise
//   062e910e  MOV [ESI+0x14],BL            ; cache it
//
// NOTE on the out-param aliasing, which Ghidra renders confusingly: retail
// passes `LEA EDX,[ESP+0x14]` -- the stack slot holding its OWN `bool param_1`
// argument -- as the FIRST out-param of is_cruise_active, so the callee
// OVERWRITES the incoming argument. That is harmless only because the value
// was already copied into EBX at function entry (062e90a2), and every later
// use reads EBX. Reproduced here with an explicit local, not by aliasing the
// parameter.
void ThrustManager::engage_cruise(bool bEngage)
{
    unsigned char const EngageByte = bEngage ? 1 : 0;

    if (EngageByte == 1 && bCruiseUnobstructed == 0)
        return;

    // FUN_062e91d0. No Owner test: retail dereferences `*this` here and in the callee (the
    // former `Owner == nullptr` early-out was invented; removed CMN-S2 AIS 2026-10-14).
    SetEngineActive(true);

    // 062e90be-062e90e1: the owner ship (`XOR ECX,ECX` arm at 062e90d3 dereferences 0) ->
    // is_cruise_active (+0x5c); the first out-param is the zeroed argument slot.
    bool CruiseActive = false;
    bool TradeLaneActive = false;  // retail's `local_1` scratch byte, never read again
    (void)OwnerShip()->is_cruise_active(CruiseActive, TradeLaneActive);

    unsigned char const ActiveByte = CruiseActive ? 1 : 0;
    if (ActiveByte == EngageByte && bCruiseEngaged == EngageByte)
        return;

    // 062e90ef-062e910b: re-resolved the same way (`XOR EAX,EAX` arm at 062e90ff, no guard),
    // IObjAffect sub-object at +4, slot 0x50 (bEngage, 0, -1).
    ObjAffectOf(OwnerShip())->slot_50(EngageByte != 0, false, -1);

    bCruiseEngaged = EngageByte;
}

// FUN_062e92d0, item 1826. `*(byte*)(this+0x17) = AL; RET 4`.
void ThrustManager::SetSteeringFallbackFlag(unsigned char Value)
{
    bAutoLevel = Value;
}

// 0x06263bd0 (Common.dll.txt 9383-9385), three instructions:
//   MOV AL,byte ptr [ESP+4]; MOV byte ptr [ECX+0x18],AL; RET 4
// An ICF fold shared by CNudgeEngine::Activate and three unrelated setters; on a
// ThrustManager receiver (ECX = Sys+0x18, from update_current_behavior_auto_avoidance)
// the byte it writes is bAutoAvoidance, and AL still holds the argument on return.
bool ThrustManager::SetAutoAvoidance(bool bOn)
{
    bAutoAvoidance = bOn ? 1 : 0;
    return bOn;
}

// FUN_062e92e0, item 1827 (.text:062e92e0-062e932f, RET 0). No null tests anywhere: the
// former Sys / Sys->Owner / ExternalIface guards were invented (removed CMN-S2 AIS 2026-10-14).
bool ThrustManager::CheckSteeringGate()
{
    // 062e92e3-062e92f4: [[this]+4] = the owning IBehaviorManager.
    if (Owner->Owner->get_camera_level_status())
        return true;

    // 062e92f5-062e930e: Sys+0x14fc+0x84 = ExternalIface -> is_player (+0xbc).
    if (Owner->SelfTarget.Iface->is_player())
    {
        // 062e9310-062e9329: IBehaviorManager+0xc0, 10 or 7 -> true (AL = 1).
        int const BehaviorIndex = Owner->Owner->CurrentBehaviorIndex;
        if (BehaviorIndex == 10 || BehaviorIndex == 7)
            return true;
    }

    // 062e932b: the +0x17 byte as the result.
    return bAutoLevel != 0;
}

// ============================================================================
// The throttle / nudge / engine / thruster setters (items 1819-1825). Each one re-reads
// `[this]` and re-applies the -8 Watchable adjustment before every interface call, as
// retail does; none of them null-tests the result beyond the entry gates noted.
// ============================================================================

IObjRW* ThrustManager::OwnerShip() const
{
    return ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
}

// FUN_062e91d0 (item 1825), .text:062e91d0-062e92c6, RET 4.
void ThrustManager::SetEngineActive(bool bActive)
{
    // 062e91d7-062e91e9: `head == 0` or `head - 8 == 0` -> return.
    if (OwnerShip() == nullptr)
        return;

    // 062e9214-062e922d: SelfTarget.Iface (Owner+0x1580) is_player (+0xbc); a non-player whose
    // cached byte already equals bActive returns.
    bool const bPlayer = Owner->SelfTarget.Iface->is_player();
    if (!bPlayer && bEngineActive == (unsigned char)bActive)
        return;

    // 062e9233-062e9253: find_equipment (+0x114) for one engine id; Id starts as
    // SUBOBJ_ID_NONE (.rdata:063a4aa4 == 0, loaded at 062e91f7). Unguarded receiver.
    unsigned short Id = SUBOBJ_ID_NONE;
    ObjInspectOf(OwnerShip())->find_equipment(&Id, 1, EquipTypeEngine);

    if (Id != SUBOBJ_ID_NONE)                                               // 062e925e
    {
        // 062e9260-062e927c: get_equipment_status (+0x118) into the 8-byte EquipStatus
        // whose Flags low three bits retail clears up front (062e9209).
        EquipStatus Status{};
        ObjInspectOf(OwnerShip())->get_equipment_status(Status, Id);
        unsigned char const StatusByte = Status.Flags;

        // Gate on the status byte just written (Ghidra conflates it with bActive): not
        // destroyed and the active bit differs from the request.
        if ((StatusByte & EquipStatusFlagDestroyed) == 0 &&
            ((StatusByte & EquipStatusFlagActive) != 0) != bActive)
        {
            // 062e9299-062e92b9: IObjAffect (+4) slot_4c(&Id, bActive, -1), unguarded.
            ObjAffectOf(OwnerShip())->slot_4c(&Id, bActive, -1);
        }
    }

    bEngineActive = (unsigned char)bActive;                                 // 062e92bc
}

// FUN_062e8c20 (item 1820), .text:062e8c20-062e8cd5, RET 4.
void ThrustManager::SetAxisThrottle(Vector const& Wanted)
{
    // 062e8c23-062e8c37: `head == 0` or `head - 8 == 0` -> return.
    if (OwnerShip() == nullptr)
        return;

    // 062e8c3d-062e8c56: get_axis_throttle (+0x50) into AxisThrottle; nonzero -> return.
    if (ObjInspectOf(OwnerShip())->get_axis_throttle(AxisThrottle) != 0)
        return;

    // Epsilon = DAT_063a07e4 raw bits (~1.049e-6f).
    unsigned int const EpsilonBits = 0x358637BEu;
    float Epsilon;
    std::memcpy(&Epsilon, &EpsilonBits, sizeof(Epsilon));

    // 062e8c5c-062e8ca3: each difference and its square stay on the FPU (double); `FCOMP
    // float eps; TEST AH,0x41; JP` -- greater or unordered counts as moved.
    double const DeltaX = (double)Wanted.x - AxisThrottle.x;
    double const DeltaY = (double)Wanted.y - AxisThrottle.y;
    double const DeltaZ = (double)Wanted.z - AxisThrottle.z;
    if (DeltaX * DeltaX <= Epsilon && DeltaY * DeltaY <= Epsilon && DeltaZ * DeltaZ <= Epsilon)
        return;

    // Re-resolved, unguarded: IObjAffect (+4) slot 1 set_axis_throttle(Wanted).
    ObjAffectOf(OwnerShip())->set_axis_throttle(Wanted);

    AxisThrottle = Wanted;
}

// FUN_062e9030 (item 1823), RET 4.
void ThrustManager::SetStrafeDir(StrafeDir Dir)
{
    SetEngineActive(true);                                                  // 062e9036

    // 062e903b-062e9054: head-8 (or 0) and get_strafe_dir (+0x58) with no null test.
    StrafeDir Current = StrafeNone;
    if (ObjInspectOf(OwnerShip())->get_strafe_dir(Current) != 0)
        return;
    if (Current == Dir)
        return;

    // Re-resolved, unguarded: IObjAffect (+4) slot 3 set_strafe_dir(Dir).
    ObjAffectOf(OwnerShip())->set_strafe_dir(Dir);
}

// FUN_062e9120 (item 1824), RET 4.
void ThrustManager::SetThrustersActive(bool bActive)
{
    // 062e9124-062e914c: head-8 (or 0) and is_cruise_active (+0x5c) with no null test.
    bool bCruise = false;
    bool bTradeLane = false;
    (void)ObjInspectOf(OwnerShip())->is_cruise_active(bCruise, bTradeLane);
    if (bCruise && !bTradeLane)
        return;

    if (bActive)
        SetEngineActive(true);

    // 062e9170-062e918e: re-resolved, are_thrusters_active (+0x64).
    bool bThrusters = false;
    (void)ObjInspectOf(OwnerShip())->are_thrusters_active(bThrusters);
    if (bThrusters == bActive)
        return;

    // Re-resolved a third time, unguarded: IObjAffect (+4) slot_54(bActive, -1).
    ObjAffectOf(OwnerShip())->slot_54(bActive, -1);
}

// FUN_062e89a0 (item 1819), RET 8.
bool ThrustManager::SetThrottle(float Throttle, int Mode)
{
    // Gate: the owner's watcher head must be neither 0 nor 8.
    if (OwnerShip() == nullptr)
        return false;

    // 062e89d3: is_player (+0xbc) on the re-resolved ship; the answer is discarded.
    (void)ObjInspectOf(OwnerShip())->is_player();

    float Clamped = (Throttle > MaxThrottle) ? MaxThrottle : Throttle;
    int const EffectiveMode = (bCruiseUnobstructed == 0 && Mode == 2) ? 0 : Mode;

    if (EffectiveMode != 0 && EffectiveMode != 2)
        return true;   // retail's `SUB EAX,2; JNZ <epilogue>` for any other mode

    if (EffectiveMode == 2)
    {
        // Clamp into [0.0, 1.2] (DAT_063a07a0 / DAT_06399408).
        if (Clamped > 1.2f) Clamped = 1.2f;
        if (Clamped < 0.0f) Clamped = 0.0f;

        // 062e8a71: is_cruise_active (+0x5c).
        bool bCruise = false;
        bool bTradeLane = false;
        (void)ObjInspectOf(OwnerShip())->is_cruise_active(bCruise, bTradeLane);

        // Cruising outside a trade lane: the set_throttle commit moves to the tail.
        bool const bDeferCommitToTail = (bCruise && !bTradeLane);
        if (!bDeferCommitToTail)
            ObjAffectOf(OwnerShip())->set_throttle(Clamped);                // 062e8a9d

        if (Clamped > 0.0f && bCruiseUnobstructed != 0)
        {
            SetEngineActive(true);

            bool bCruiseNow = false;
            bool bTradeLaneNow = false;
            (void)ObjInspectOf(OwnerShip())->is_cruise_active(bCruiseNow, bTradeLaneNow);   // 062e8ae0

            if (!(bCruiseNow && bCruiseEngaged == 1))
            {
                ObjAffectOf(OwnerShip())->slot_50(true, false, -1);         // 062e8b0c
                bCruiseEngaged = 1;
            }
        }

        if (bDeferCommitToTail)
            ObjAffectOf(OwnerShip())->set_throttle(Clamped);                // 062e8b3c / 062e8b52
        return true;
    }

    // EffectiveMode == 0: clamp into [-1.0, 1.0] (DAT_063a03f0 / DAT_06399410).
    if (Clamped < -1.0f) Clamped = -1.0f;
    else if (Clamped > 1.0f) Clamped = 1.0f;

    SetEngineActive(true);

    // 062e8bbe: is_cruise_active (+0x5c).
    bool bCruise = false;
    bool bTradeLane = false;
    (void)ObjInspectOf(OwnerShip())->is_cruise_active(bCruise, bTradeLane);

    if (!(!bCruise && bCruiseEngaged == 0))
    {
        ObjAffectOf(OwnerShip())->slot_50(false, false, -1);                // 062e8be7
        bCruiseEngaged = 0;
    }

    SetEngineActive(true);

    ObjAffectOf(OwnerShip())->set_throttle(Clamped);                        // 062e8c13
    return true;
}

// FUN_062e8ce0 (item 1821), Common.dll.txt 246113-246254, RET 4; all 133 instructions traced.
void ThrustManager::SetNudgeDirection(Vector const& WorldDir)
{
    SetEngineActive(true);                                                  // 062e8ce4 PUSH 1

    // 062e8ced-062e8d50: a copy of WorldDir; when (y*y + z*z) + x*x is above 1.0f (TEST
    // AH,0x41 / JNZ keeps <=, == and unordered) it is replaced by FUN_062e1680's result.
    Vector Local = WorldDir;
    if ((double)Local.y * Local.y + (double)Local.z * Local.z + (double)Local.x * Local.x > 1.0)
    {
        Vector Normalized;
        Local = *VecNormalize3(&Local, &Normalized);
    }

    // 062e8d54-062e8db7: the owner ship's transform (FUN_062e61f0), then the 3DMathEngine's
    // TransposeTransform (+0x38, DAT_063fca58 is this function's engine cache) of Local into
    // ship space -- the nudge vector to apply.
    Transform const* const Frame = Behavior_InvokeNodeSlotC(OwnerShip());
    Vector Nudge;
    Acquire3DMathEngine()->TransposeTransform(reinterpret_cast<FVector&>(Nudge),
                                              reinterpret_cast<FMatrix const&>(Frame->Rotation),
                                              reinterpret_cast<FVector const&>(Local));

    // 062e8db3-062e8dcd: the current nudge (IObjInspect slot 21, +0x54); a nonzero result
    // returns without committing.
    Vector Current;
    if (ObjInspectOf(OwnerShip())->get_nudge_vec(Current) != 0)
        return;

    // 062e8dd3-062e8e22: commit when any axis differs: (New - Current)^2 against 1e-6f
    // (DAT_063a07e4); TEST AH,0x41 / JP commits on > and unordered, the last axis's JNP
    // returns on <= .
    double const DeltaX = (double)Nudge.x - Current.x;
    double const DeltaY = (double)Nudge.y - Current.y;
    double const DeltaZ = (double)Nudge.z - Current.z;
    float const Epsilon = 1.0000001111620804e-06f;                  // 0x358637be
    if (!(DeltaX * DeltaX <= Epsilon) || !(DeltaY * DeltaY <= Epsilon) || !(DeltaZ * DeltaZ <= Epsilon))
    {
        // 062e8e24-062e8e52: IObjAffect slot 2 (+0x08) set_nudge_vec; no null test.
        ObjAffectOf(OwnerShip())->set_nudge_vec(Nudge);
    }
}

// FUN_062e8e60 (item 1822), Common.dll.txt 246264-246416 (Ghidra's `.c` for this address is
// the artefact the old `unaff_ESI`/`unaff_EDI` blocker was built on and was not used).

// .data:063ec6ac, raw bytes 00 00 c0 3f == 0x3fc00000 == 1.5f. Lives in .data, but its
// complete xref list (Common.dll.txt:634985) is three reads, all inside this function, and no
// write anywhere in the module: a fixed tuning constant, not runtime state.
static float const NudgeBlendScale = 1.5f;

// .rdata:0639e6cc, raw bytes a4 70 7d 3f == 0x3f7d70a4 == 0.99f exactly.
static float const NudgeAlignmentThreshold = 0.99f;

void ThrustManager::BlendNudgeDirection(Vector const& WorldDir, float DeltaTime)
{
    // 062e8e63-062e8e7f: the argument's three floats are copied into this frame; retail never
    // writes back through the argument.
    Vector Working = WorldDir;

    // 062e8e76-062e8e98: the -8 adjustor on the owner's watcher target (a null head yields a
    // null receiver, which retail calls through unguarded), then get_nudge_vec (+0x54).
    Vector NudgeLocal;
    ObjInspectOf(OwnerShip())->get_nudge_vec(NudgeLocal);

    // 062e8e9b-062e8eed: TransformMatrix (3DMathEngine slot 0x2c, the DAT_063fca58 lazy
    // singleton) of the nudge by SelfTarget.Iface's (Owner+0x1580) get_transform(). No null
    // tests.
    Vector NudgeWorld;
    {
        Matrix const& Orientation = Owner->SelfTarget.Iface->get_transform().Rotation;
        Acquire3DMathEngine()->TransformMatrix(reinterpret_cast<FVector&>(NudgeWorld),
                                               reinterpret_cast<FMatrix const&>(Orientation),
                                               reinterpret_cast<FVector const&>(NudgeLocal));
    }

    // 062e8eed-062e8f39: FUN_062e1680 on Working then on the nudge, FUN_062e1630(NormNudge,
    // NormWorking) FCOMP 0.99f; TEST AH,0x5 / JP jumps over the zeroing, so only "less" zeroes
    // the nudge.
    {
        Vector NormWorking;
        Vector NormNudge;
        VecNormalize3(&Working, &NormWorking);
        VecNormalize3(&NudgeWorld, &NormNudge);
        if (ClampedDot3(NormNudge, NormWorking) < (double)NudgeAlignmentThreshold)
            NudgeWorld = Vector{ 0.0f, 0.0f, 0.0f };
    }

    // 062e8f41-062e8f86: both lengths stay on the x87 stack, summed (y,x,z) for Working and
    // (y,z,x) for the nudge; FLD ST1 / FCOMPP / TEST AH,0x41 / JNZ skips the blend unless
    // |Working| > |Nudge|.
    double const LenWorking = std::sqrt(((double)Working.y * Working.y +
                                         (double)Working.x * Working.x) +
                                        (double)Working.z * Working.z);
    double const LenNudge = std::sqrt(((double)NudgeWorld.y * NudgeWorld.y +
                                       (double)NudgeWorld.z * NudgeWorld.z) +
                                      (double)NudgeWorld.x * NudgeWorld.x);
    if (LenWorking > LenNudge)
    {
        // 062e8f8c-062e900e, roundings exactly where retail FSTPs:
        //   x: (float)((float)(K*Wx*Dt) * Len), then (float)(+ Nx)
        //   y: (float)(K*Wy*Dt*Len), then (float)(Ny +)
        //   z: (float)(K*Wz) [FSTP to [F+0x30]], * Dt * Len unrounded, then (float)(Nz +)
        double const K = NudgeBlendScale;
        float const XScaled = (float)((K * Working.x) * DeltaTime);
        float const XTerm = (float)(XScaled * LenWorking);
        float const YTerm = (float)(((K * Working.y) * DeltaTime) * LenWorking);
        float const ZScaled = (float)(K * Working.z);
        double const ZTerm = ((double)ZScaled * DeltaTime) * LenWorking;
        Working.x = (float)((double)XTerm + NudgeWorld.x);
        Working.y = (float)((double)NudgeWorld.y + YTerm);
        Working.z = (float)((double)NudgeWorld.z + ZTerm);
    }

    // 062e9012-062e901b: commit through the same `this`.
    SetNudgeDirection(Working);
}
