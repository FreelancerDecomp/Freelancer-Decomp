// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- Behavior (the AI behavior base class) and the Behavior_* helpers; also the two
// shared follower helpers FollowerExternalIfaceIsPlayer / FollowerPlayerShipOrNull, declared for
// the follower/formation TUs in common_behavior_internal.h.
// CMN-SPLIT stage 1 (2026-10-14): BaseFollowerBehavior moved to common_basefollowerbehavior.cpp,
// FormationBehavior to common_formationbehavior.cpp, unchanged.

#include "common_behavior.h"
#include "common_behavior_internal.h" // CmnBehavior:: -- the shared follower helpers defined here
#include "common_aisteer.h" // GetIfacePositionOrZero, IsWithinInterceptRangeOfIface
#include "common_systemmanager.h" // SystemManager -- BaseFollowerBehavior::NavHelper's real type
#include "common_misc.h" // FastInvSqrt
#include "common_csys.h"  // Matrix, Transform, MatrixSetIdentity, Acquire3DMathEngine
#include "common_iface.h" // IObjInspect / IObjRW / ObjInspectOf / ObjRwFromWatchable
#include "x86math.h"      // I3DMathEngine::TransformMatrix (+0x2c), FVector, FMatrix
#include "common_pathfind.h" // PathfindManager -- Behavior::Slot4 / Behavior::SetCruiseLock

#include <cmath>
#include <cstdint>
#include <cstring>

// MatchThrottle2Idea (formerly FormationMoveFx here) lives in common_idea.h/.cpp.

// FUN_062e5b70 (decomp-tracker item 1765). See common_behavior.h for the full
// derivation. Generic null-guarded vtable-slot-0xd4 trampoline on an
// unidentified interface.
float Behavior_InvokeManagerSlotD4(IObjInspect const* Obj, float Value, ThrustEquipType Type)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408

    return Obj->get_projected_throttle(Value, Type);
}

// FUN_062d21c0 (item 1537). All five instructions, .text:062d21c0 `MOV AL,[ECX+0x28]`
// through the `RET` at .text:062d21ca. See common_behavior.h for the receiver proof
// and for the three claims in the tracker note that did not survive re-measuring.
// No null test, as retail: every call site has already called through the same pointer.
void Behavior_ReleaseDirectiveTarget(void* Target)
{
    unsigned char* const Flags28 = reinterpret_cast<unsigned char*>(Target) + 0x28;
    *Flags28 = static_cast<unsigned char>((*Flags28 & 0xfd) | 0x01);  // 062d21c3 / 062d21c5
}

// FUN_062e5af0 (decomp-tracker item 1762). See common_behavior.h for the full
// derivation -- byte-identical to Behavior_InvokeManagerSlotD4 above, just a
// different fixed vtable slot.
float Behavior_InvokeManagerSlotD8(IObjInspect const* Obj, float Value, ThrustEquipType Type)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408

    return Obj->get_speed(Value, Type);
}

// FUN_062e5950 (item 1759), all 29 instructions (Common.dll.txt 240922-240962).
float Behavior_GetAngularDistanceTravelled(IObjInspect const* Obj, float From, float To, float Dt,
                                           float MaxSpeed)
{
    if (Obj == nullptr)
        return 0.0f;                                   // 062e5958 FLD DAT_06399408
    // 062e595f-062e596e: FCOMP 0.0; `TEST AH,0x41; JP` -- falls through (returns 0) when
    // C0 or C3 alone is set, i.e. MaxSpeed < 0 or MaxSpeed == 0.
    if (MaxSpeed < 0.0f || MaxSpeed == 0.0f)
        return 0.0f;
    // 062e5977-062e5988: FCOMP double 0.001; `TEST AH,0x5; JP` -- the store runs for C0
    // alone, i.e. Dt < 0.001.
    if (static_cast<double>(Dt) < 0.001)
        Dt = 0.0f;
    return Obj->get_angular_distance_travelled(From, To, Dt, MaxSpeed);   // 062e59a6 [EAX+0xec]
}

// FUN_062e6010 (decomp-tracker item 1769). See common_behavior.h for the full
// derivation. No null-check of its own -- matches retail, every confirmed
// caller pre-validates Iface.
float Behavior_GetIfaceMaxScalarAttribute(IObjInspect const* Iface)
{
    // Slot 0x2c is get_physical_radius(float& Radius, Vector& Center). The second out-parameter
    // used to be a bare `float Unused` -- four bytes catching a twelve-byte write.
    float Radius = 1.0f;
    Vector Center{ 0.0f, 0.0f, 0.0f };
    Iface->get_physical_radius(Radius, Center);

    float AtmosphereRange = 0.0f;
    Iface->get_atmosphere_range(AtmosphereRange);

    return (AtmosphereRange > Radius) ? AtmosphereRange : Radius;
}

// FUN_062e5850 (decomp-tracker item 1757). See common_behavior.h for the full
// derivation, hand-re-verified against Common.dll.txt 240814-240875.
float Behavior_QueryRadiusEffect(IObjInspect const* Obj, float Unused2, float Radius, float Falloff, ThrustEquipType Type)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408

    // 062e585f-062e5889: `FCOMP 0.0f; TEST AH,0x5; JP` keeps the value on >= and on
    // unordered -- only a strict less-than clamps.
    if (Radius < 0.0f)
        Radius = 0.0f;
    if (Falloff < 0.0f)
        Falloff = 0.0f;

    // Early-out (no vtable call at all) when both Radius and Unused2 are
    // exactly 0.0f -- `TEST AH,0x44; JP` falls through only on equal (062e5891-062e58b9).
    if (Radius == 0.0f && Unused2 == 0.0f)
        return 0.0f;

    // Snap Radius to 0 below the double 0.001 (DAT_0639c9f0), `TEST AH,0x5; JP`.
    if ((double)Radius < 0.001)
        Radius = 0.0f;

    return Obj->get_time_to_accelerate(Unused2, Radius, Falloff, Type);
}

// FUN_062e58f0 (decomp-tracker item 1758). See common_behavior.h for the full
// derivation, hand-re-verified against Common.dll.txt 240876-240917.
float Behavior_QueryFalloffEffect(IObjInspect const* Obj, float Unused2, float Radius, float Falloff, ThrustEquipType Type)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408

    // Early-out (no vtable call at all) when Falloff is strictly negative --
    // 062e58ff-062e5916 `FCOMP 0.0f; TEST AH,0x5; JP` continues on >= and on unordered.
    if (Falloff < 0.0f)
        return 0.0f;

    // Snap Radius to 0 below the same double 0.001 (DAT_0639c9f0) the
    // Radius-effect sibling above uses.
    if ((double)Radius < 0.001)
        Radius = 0.0f;

    return Obj->get_distance_travelled(Unused2, Radius, Falloff, Type);
}

// FUN_062e59b0 (decomp-tracker item 1760), .text:062e59b0-062e5aae. Retail inlines the
// slot calls of Behavior_InvokeManagerSlotD8 / QueryRadiusEffect / QueryFalloffEffect
// (their null tests and the 0/0 early-out drop away because the arguments are known);
// the clamps that remain are reproduced in retail's order.
float Behavior_QueryChainedRadiusEffect(void* Obj, float Radius, int Selector)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408

    IObjInspect const* const Inspect = ObjInspectOf(Obj);
    ThrustEquipType const Type = (ThrustEquipType)Selector;

    // 062e59c3-062e59fb: V = get_speed(1.0f, Sel) - 1.0 stays on the FPU; `< 0` -> 0.0
    // (TEST AH,0x5; JP); V < Radius (FCOM float, TEST AH,0x5; JP) -> 0.0f.
    double Value = (double)Inspect->get_speed(1.0f, Type) - 1.0;           // slot 0xd8
    if (Value < 0.0)
        Value = 0.0;
    if (Value < Radius)
        return 0.0f;

    // 062e5a08-062e5a19: Radius (the parameter itself) snaps to 0 below double 0.001.
    if ((double)Radius < 0.001)
        Radius = 0.0f;

    // 062e5a21-062e5a74: a copy of Radius and FST float V, each clamped `< 0` -> 0, the copy
    // snapped again below 0.001 -- QueryRadiusEffect's clamps on the inlined arguments.
    float RadiusCopy = Radius;
    float Falloff = (float)Value;
    if (RadiusCopy < 0.0f)
        RadiusCopy = 0.0f;
    if (Value < 0.0)
        Falloff = 0.0f;
    if ((double)RadiusCopy < 0.001)
        RadiusCopy = 0.0f;

    // 062e5a74-062e5a8e: get_time_to_accelerate(1.0f, RadiusCopy, Falloff, Sel), FSTP float.
    float const RadiusResult = Inspect->get_time_to_accelerate(1.0f, RadiusCopy, Falloff, Type);
    // 062e5a92-062e5aa6: get_distance_travelled(1.0f, Radius, RadiusResult, Sel), returned in ST0.
    return Inspect->get_distance_travelled(1.0f, Radius, RadiusResult, Type);
}

// FUN_062e5ab0 (decomp-tracker item 1761). See common_behavior.h for the full
// derivation.
float Behavior_QueryFixedFalloffEffect(void* Obj, float Radius)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408
    if (Radius < 0.001f) // DAT_0639e6c8
        return 0.0f;

    // DAT_063a0760 = 0x447a0000 = 1000.0f; Selector fixed to literal 1
    // (Common.dll.txt 241084/241086-241088).
    return Behavior_QueryFalloffEffect(ObjInspectOf(Obj), 0.0f, Radius, 1000.0f, ThrustEquipEngine);
}

// FUN_062e5bc0 (decomp-tracker item 1767). See common_behavior.h for the full
// derivation. New sibling vtable slot (0xdc) not shared with any other
// already-ported member of this family.
float Behavior_InvokeManagerSlotDC(IObjInspect const* Obj, float Value)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408
    if (Value == 0.0f)
        return 0.0f;

    return Obj->get_initial_speed_to_coast_distance(Value, true);
}

// FUN_062e5b20 (decomp-tracker item 1763). Hoisted from common_gotobehavior.cpp
// 2026-09-17 at that lane's request -- see the header for the full writeup.
float Behavior_InvokeManagerSlotF0(IObjInspect const* Obj, float Speed, float MaxSpeed)
{
    if (Obj == nullptr)
        return 0.0f; // FLD .rdata:DAT_06399408
    return Obj->get_angular_speed_XY(Speed, MaxSpeed);
}

// FUN_062e5b50 (decomp-tracker item 1764), Common.dll.txt 241167-241183, 10 insns, cdecl:
// `MOV ECX,[ESP+4]; TEST; JNZ; FLD 0.0f (DAT_06399408); RET` / `PUSH [ESP+8]; CALL
// [EAX+0xf4]; RET` -- IObjInspect::get_angular_speed_Z (slot 61) or 0.0f for a null object.
float Behavior_InvokeManagerSlotF4(IObjInspect const* Obj, float Speed)
{
    if (Obj == nullptr)
        return 0.0f;
    return Obj->get_angular_speed_Z(Speed);
}

// FUN_062e5ba0 (decomp-tracker item 1766), Common.dll.txt 241230-241240, 10 insns, cdecl:
// null Obj -> FLD 0.0f (DAT_06399408); else `PUSH [ESP+8]; CALL [EAX+0xf8]; RET` --
// IObjInspect::get_projected_axis_throttle_XY (slot 62).
float Behavior_InvokeManagerSlotF8(IObjInspect const* Obj, float Value)
{
    if (Obj == nullptr)
        return 0.0f;
    return Obj->get_projected_axis_throttle_XY(Value);
}

// FUN_062e91d0 / 062e8c20 / 062e9030 / 062e9120 / 062e89a0 / 062e8ce0 (items 1819-1825, the
// former Behavior_* "ComponentThis" helpers) are ThrustManager members now
// (common_systemmanager.h, common_thrustmanager.cpp).

// FUN_062e61f0 (item 1774). See common_behavior.h for the derivation --
// a trivial `Node->vtbl[0xc](Node)` tail-thunk, not a real vtable dispatch on
// some unrelated object.
Transform const* Behavior_InvokeNodeSlotC(void* Node)
{
    // 062e61f0: `MOV ECX,[ESP+4]; MOV EAX,[ECX]; JMP [EAX+0xc]` -- no null test.
    // Slot 0xc is get_transform(); slots 3 and 4 are COMDAT-folded onto one address, so the
    // pointer this hands back is equally the object's orientation Matrix (common_iface.h).
    return &ObjInspectOf(Node)->get_transform();
}


// ============================================================================
// Behavior -- the base class's own +0x1c per-tick delta.
//
// See the long comment on Behavior's declaration (common_behavior.h) for why this is an
// accessor pair over a raw offset and not a data member: nineteen classes in this port
// re-declare Behavior's whole +0x04..+0x29 block on themselves, and every one of their
// offsetof static_asserts depends on sizeof(Behavior) staying 4. Giving the base a member
// would slide all nineteen by 0x28 bytes in one edit, so the migration is an open TODO for a
// wave that can touch all of them at once -- not something to sneak in from six files.
//
// The offset itself is retail's, from two independent places: the constructor FUN_062d2020
// zeroes a dword at +0x1c (.text:062d2037), and IBehaviorManager::UpdateBehaviorTick stores a
// float there through a bare Behavior* immediately before dispatching Slot7
// (.text:062d335d-062d336a, `FLD [ESI+0xd4]` / `FSTP [EDI+0x1c]` / `CALL [EAX+0x1c]`).
// ============================================================================

float Behavior::GetTickDelta() const
{
    return *reinterpret_cast<float const*>(reinterpret_cast<char const*>(this) + 0x1c);
}

void Behavior::SetTickDelta(float Delta)
{
    *reinterpret_cast<float*>(reinterpret_cast<char*>(this) + 0x1c) = Delta;
}

// Vtable slot 2 (+0x08), 0x0629afd0: `MOV EAX,[ESP+4]; MOV [ECX+0x18],EAX; RET 4`. Same
// raw-offset device as SetTickDelta -- +0x18 is declared on each subclass (see common_behavior.h).
void Behavior::Slot2(IObjDB* ObjectDb)
{
    *reinterpret_cast<IObjDB**>(reinterpret_cast<char*>(this) + 0x18) = ObjectDb;
}

// Vtable slot 23 (+0x5c), LAB_062c2f50: `AND byte ptr [ECX+0x28],0xfd; RET`. Shared by all 21
// Behavior vtables. Callers: IBehaviorManager::set_current_directive (one entry, then all 21).
void Behavior::cancel_notify()
{
    unsigned char* const Flags28 = reinterpret_cast<unsigned char*>(this) + 0x28;
    *Flags28 = static_cast<unsigned char>(*Flags28 & 0xfd);
}

// Same raw-offset device, same reason, for the type id FUN_062d2020 parks at +0x04 -- see the
// declaration's comment in common_behavior.h for the factory-side evidence. Read-only: the
// storage is written by whichever derived class declares it, from its own `int TypeId`
// constructor argument.
int Behavior::GetTypeId() const
{
    return *reinterpret_cast<int const*>(reinterpret_cast<char const*>(this) + 0x04);
}

// Same raw-offset device again, for the NavHelper pointer at +0x24 -- see the declaration's
// comment in common_behavior.h. Read-only; the binder FUN_062d2090 is what writes it.
void* Behavior::GetNavHelper() const
{
    return *reinterpret_cast<void* const*>(reinterpret_cast<char const*>(this) + 0x24);
}

// Vtable slot 3 (+0x0c), FUN_062d20d0 (Common.dll.txt 210738-210750), 6 instructions:
// `MOV EAX,1; MOV [ECX+0xc],EAX; MOV [ECX+0x10],EAX; AND byte [ECX+0x28],0xfe;
// MOV dword [ECX+0x1c],0; RET`.
void Behavior::Slot3()
{
    char* const Self = reinterpret_cast<char*>(this);
    *reinterpret_cast<int*>(Self + 0x0c) = 1;                        // 062d20d5
    *reinterpret_cast<int*>(Self + 0x10) = 1;                        // 062d20d8
    unsigned char* const Flags28 = reinterpret_cast<unsigned char*>(Self + 0x28);
    *Flags28 = static_cast<unsigned char>(*Flags28 & 0xfe);          // 062d20db
    *reinterpret_cast<float*>(Self + 0x1c) = 0.0f;                   // 062d20df (dword 0)
}

// Vtable slot 4 (+0x10), FUN_062d20f0 (Common.dll.txt 210760-210776), 8 instructions:
// `EAX = [[ECX+0x20]+0xc]; if (EAX) [EAX+0x64] = 0; byte [ECX+0x29] = 0; RET`. The manager
// pointer itself is not null-tested.
void Behavior::Slot4()
{
    char* const Self = reinterpret_cast<char*>(this);
    IBehaviorManager* const Manager = *reinterpret_cast<IBehaviorManager* const*>(Self + 0x20);
    PathfindManager* const Pathfind = Manager->Pathfind;              // 062d20f3
    if (Pathfind != nullptr)
        Pathfind->Field_064 = 0;                                     // 062d20fc
    *reinterpret_cast<unsigned char*>(Self + 0x29) = 0;              // 062d20ff
}

// FUN_062d2020 (.text:062d2020-062d205a), all 16 instructions, the vptr store aside.
void Behavior::ConstructBase(int TypeId)
{
    char* const Self = reinterpret_cast<char*>(this);
    *reinterpret_cast<int*>(Self + 0x04) = TypeId;                         // 062d2026
    *reinterpret_cast<unsigned int*>(Self + 0x08) = 0;                     // 062d202b
    *reinterpret_cast<void**>(Self + 0x14) = nullptr;                      // 062d202e
    *reinterpret_cast<void**>(Self + 0x24) = nullptr;                      // 062d2031
    *reinterpret_cast<void**>(Self + 0x18) = nullptr;                      // 062d2034
    *reinterpret_cast<unsigned int*>(Self + 0x1c) = 0;                     // 062d2037
    *reinterpret_cast<unsigned char*>(Self + 0x29) = 0;                    // 062d203a
    unsigned char* const Flags28 = reinterpret_cast<unsigned char*>(Self + 0x28);
    *Flags28 = static_cast<unsigned char>(*Flags28 & 0xfc);                // 062d2040
    *reinterpret_cast<int*>(Self + 0x0c) = 2;                              // 062d2049
    *reinterpret_cast<int*>(Self + 0x10) = 1;                              // 062d2050
}

// FUN_062d2090 (Common.dll.txt 210691-210725), all 24 instructions: Owner null or Sys
// null -> AL = 0; else [+0x14] = Owner, `CALL [[Owner]+0x20]` (IObjInspect::get_id),
// [+0x24] = Sys, [+0x08] = the id, [+0x20] = Manager, AL = 1. RET 0xc.
bool Behavior::BindContext(void* Owner, void* Manager, void* Sys)
{
    if (Owner == nullptr || Sys == nullptr)
        return false;
    char* const Self = reinterpret_cast<char*>(this);
    *reinterpret_cast<void**>(Self + 0x14) = Owner;                         // 062d20a4
    unsigned int const Id = ObjInspectOf(Owner)->get_id();                  // 062d20a9
    *reinterpret_cast<void**>(Self + 0x24) = Sys;                           // 062d20b0
    *reinterpret_cast<unsigned int*>(Self + 0x08) = Id;                     // 062d20b3
    *reinterpret_cast<void**>(Self + 0x20) = Manager;                       // 062d20b7
    return true;
}

// Vtable slot 9 (+0x24): `MOV EAX,[ECX+0x10]; RET`.
int Behavior::Slot9()
{
    return *reinterpret_cast<int const*>(reinterpret_cast<char const*>(this) + 0x10);
}

// FUN_062d21d0 (Common.dll.txt 210910-210923): tail-jump to FUN_062dd3d0 with
// this = [[this+0x20]+0xc] and both stack arguments unchanged; no null tests.
void Behavior::SetCruiseLock(int Value, int Mask)
{
    IBehaviorManager* const Manager =
        *reinterpret_cast<IBehaviorManager* const*>(reinterpret_cast<char const*>(this) + 0x20);
    Manager->Pathfind->SetCruiseLock(Value, static_cast<unsigned int>(Mask));
}

// ----------------------------------------------------------------------------
// Behavior vtable slot 16 (+0x40) -- FUN_062d2110, decomp-tracker item 1535, 54
// instructions, hand-translated from Common.dll.txt 210791-210860. PORTED 2026-09-17
// (claude:w23-stubsweep2); it was `virtual void set_afterburner(bool) {}` before, which is
// what eighteen behavior-family vtable slots and IBehaviorManager's own afterburner entry
// point were all reaching.
//
// Retail, instruction by instruction:
//   062d2114  MOV EAX,[ESI+0x24] / ADD EAX,0x8 / MOV EAX,[EAX]   ; NavHelper+0x8 list head
//   062d211a  the two out-bytes at [ESP+7] and [ESP+6] are pre-cleared
//   062d2128  head == 0 -> ECX = 0, else ECX = head - 8          ; the Watchable adjustment
//   062d213d  CALL [ECX+0x5c] with (&[ESP+7], &[ESP+6])          ; is_cruise_active(a, b)
//             -- push order is right-to-left, so [ESP+7] is parameter ONE (OutCruise) and
//             [ESP+6] is parameter TWO (OutTradeLane).
//   062d2144  if (OutCruise != 0 && OutTradeLane == 0) -> RET, do nothing at all.
//             (`TEST AL,AL; JZ 062d2150` on OutCruise, then `TEST AL,AL; JZ 062d219c` on
//             OutTradeLane -- the SECOND jump is the one that returns, so the do-nothing
//             case is "cruising but not in a trade lane".)
//   062d2150  ECX = [ESI+0x24] + 0x18 (ComponentThis); AL = [ESP+0xc], i.e. this method's
//             own dword argument, read as a byte.
//   062d215c  argument == 0 -> LAB_062d2195: Thrust.SetThrustersActive(false); RET.
//   062d215e  argument != 0 -> Thrust.SetThrustersActive(true), then re-load
//             ComponentThis and, on OutTradeLane, Thrust.SetThrottle(1.0f,
//             2) at 062d217a, else Thrust.SetThrottle(1.0f, 0) at
//             062d218b. The pushed 0x3f800000 is raw 1.0f in both arms; the mode literal is
//             the only thing that differs.
//
// Retail dereferences [this+0x24] with no null test, and its `head == 0` arm calls through a
// null receiver; both are reproduced as written (no guards).
//
// NAME CAVEAT: `set_afterburner` is this slot's inherited positional name and this pass did
// NOT re-derive it. The body is a cruise/throttle-gate poke, not an afterburner write, so the
// name is worth re-checking against the pub::AI interface -- but renaming slot 16 would touch
// IBehaviorManager and every subclass, which is not this pass's scope.
void Behavior::set_afterburner(bool bOn)
{
    void* const NavHelper = GetNavHelper();

    // 062d2114-062d213d: both out-bytes pre-cleared, then slot 0x5c on the -8 adjusted head.
    void* const Head = *reinterpret_cast<void* const*>(static_cast<char*>(NavHelper) + 0x8);
    bool bCruise = false;
    bool bTradeLane = false;
    (void)ObjInspectOf(ObjRwFromWatchable(Head))->is_cruise_active(bCruise, bTradeLane);

    // 062d2144-062d214e: the single early-out.
    if (bCruise && !bTradeLane)
        return;

    ThrustManager* const Thrust = &((SystemManager*)NavHelper)->Thrust;

    if (!bOn)
    {
        // LAB_062d2195.
        Thrust->SetThrustersActive(false);
        return;
    }

    // 062d215e-062d2160.
    Thrust->SetThrustersActive(true);

    // 062d2165-062d218b: 1.0f either way, mode 2 while in a trade lane, mode 0 otherwise.
    Thrust->SetThrottle(1.0f, bTradeLane ? 2 : 0);
}

// ----------------------------------------------------------------------------
// Behavior vtable slot 17 (+0x44) -- FUN_062d21b0, decomp-tracker item 1536, Common.dll.txt
// 210877-210887. PORTED 2026-09-17 (claude:w23-stubsweep2). The entire retail body is
//     MOV ECX,[ECX+0x24] ; ADD ECX,0x18 ; JMP FUN_062e9030
// and FUN_062e9030 is ThrustManager::SetStrafeDir. The argument is one dword (FUN_062e9030's
// three exits all `RET 4`).
void Behavior::slide_strafe_burst(StrafeDir Dir)
{
    ((SystemManager*)GetNavHelper())->Thrust.SetStrafeDir(Dir);
}

// ============================================================================
// Shared follower helpers (common_behavior_internal.h, namespace CmnBehavior). Neither is a
// retail function: both are inlined instruction sequences, on the SystemManager and on a
// CObject. The SystemManager+0x1364 accessors that used to sit here (FUN_062ec520, 062ec680,
// 062ecab0, 062ecad0, 062ecb20, 062ecc00, 062ecc50, 062ecc60, 062ecce0, 062ecd30) are
// FGunConvergenceStateBlock members in common_gunconvergence.cpp (CMN-GC 2026-10-14).
// ============================================================================

// `MOV EAX,[Sys+0x1580]; MOV EDX,[EAX]; CALL [EDX+0xbc]` (IObjInspect::is_player). Retail
// tests neither pointer (.text:062ecb23-062ecb35, 062ecbc7-062ecbd1, 062cb76a-062cb784).
bool CmnBehavior::FollowerExternalIfaceIsPlayer(SystemManager* Sys)
{
    return Sys->SelfTarget.Iface->is_player();
}

// `(*(uint*)(CObject + 0x4c) & 0x503) == 0x503` -- the port's established
// "[obj+0x4c] & mask == mask" object-class test (common_cobject.h documents the
// 0x103 variant of the same idiom). Used at .text:062cb7bd and 062cb7e2 to reduce
// a cobject() result to "a player-flavoured ship, else null".
CObject const* CmnBehavior::FollowerPlayerShipOrNull(CObject const* Object)
{
    if (Object == nullptr)
        return nullptr;
    unsigned int const Class = *reinterpret_cast<unsigned int const*>(
        reinterpret_cast<char const*>(Object) + 0x4c);
    return ((Class & 0x503u) == 0x503u) ? Object : nullptr;
}
