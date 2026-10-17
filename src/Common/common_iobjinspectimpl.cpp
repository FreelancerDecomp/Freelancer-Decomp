// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- IObjInspectImpl (the CObject-backed IObjInspect implementation), its file-local
// class-mask helpers and ResolveLaneStart, the exported std::vector<IObjInspect*> instantiation,
// and InitEquipStatusBits.
// CMN-SPLIT stage 1 (2026-10-14): split out of common_iface.cpp unchanged.

#include "common_iface.h"
#include "common_behavior.h"
#include "common_aisteer.h"
#include "common_cobjdb.h"
#include "common_csys.h"
#include "x86math.h"
#include "common_goods.h"
#include "common_iustategraph.h"
#include "common_misc.h"
#include "common_newchar.h"
#include "common_pub.h"
#include "common_pathfind.h"
#include "common_universe.h" // Universe::IZone (CSimple::ZoneAtA4, get_zone_props)
#include "dacom.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

// ============================================================================
// Helpers for CSimple / CShip method access
// ============================================================================

// These inline helpers access CObject/CSimple/CShip fields directly by offset,
// matching the Ghidra disasm. Where a proper method exists on the class we call it;
// where it does not yet exist (OBJ batch, wave 3) we either read the field directly
// or defer with FULLPORT-DEFERRED[OBJ].

static const char* kIfaceSource = "common_iface.cpp";

// ObjectClass bitmask checks from retail code:
//   & 3 == 3         → CSimple-derived (CSimple, CEqObj, CShip, etc.)
//   & 0x103 == 0x103 → CEqObj-derived (CEqObj, CShip)
//   & 0x503 == 0x503 → CShip
//   & 0x303 == 0x303 → CSolar
//   == 0xa7          → CGuided

// ============================================================================
// IObjInspectImpl — ctor / dtor / operator=
// ============================================================================

IObjInspectImpl::IObjInspectImpl()
{
    // Ghidra @ 0x5EFA0. The two vtable stores and the Watchable/CBase field zeroing that used
    // to be written out here are now what the base constructors do.
}

IObjInspectImpl::IObjInspectImpl(IObjInspectImpl const& Other)
{
    // Ghidra @ 0x5EFD0 / raw asm 0x062befd0-0x062bf01f (Common.dll.c is corrupted here,
    // raw disasm is ground truth): EDX = (&Other != null) ? &Other+8 : 0, then
    // Field08 = *EDX (reads Other.Field08); separately EDX = (&Other != null) ? &Other+0xc : 0,
    // then Object = *(EDX+4) = Other's field at +0x10, i.e. Other.Object IS copied here.
    // (Retail's null-guard reads through the computed-zero pointer in the &Other==null case,
    // an unreachable-in-practice retail quirk not worth replicating as a crash.)
    Head = Other.Head;
    Object = Other.Object;
}

IObjInspectImpl::~IObjInspectImpl()
{
    // .text:062bf070-062bf0df: the body is empty. Retail runs the base destructors only:
    // ~CBase (062bf09e) first, then the IObjRW base's ~Watchable (062bf0b6; ~Watchable is
    // `JMP unwatch`, 06280b30), which unlinks every watcher of this inspect instance.
}

IObjInspectImpl& IObjInspectImpl::operator=(IObjInspectImpl const& Other)
{
    // Ghidra @ 0x5F030 / raw asm 0x062bf030-0x062bf060 (Common.dll.c corrupted, raw disasm
    // authoritative): same shape as the copy ctor above -- Field08 = Other.Field08 and
    // Object = Other.Object (its field at +0x10) are both copied unconditionally.
    // Retail does not special-case self-assignment here; harmless to copy-onto-self.
    Head = Other.Head;
    Object = Other.Object;
    return *this;
}

// ============================================================================
// IObjInspectImpl — helper accessors
// ============================================================================

// File-local, not IObjInspectImpl members: retail inlines these class-mask tests at each call
// site and exports no GetSimple/GetShip/GetEqObj on IObjInspectImpl.
namespace
{
    CSimple* InspectSimple(CObject const* Object)
    {
        if (Object != nullptr && ((unsigned)Object->ObjectClass & 3) == 3) {
            return static_cast<CSimple*>(const_cast<CObject*>(Object));
        }
        return nullptr;
    }

    CShip* InspectShip(CObject const* Object)
    {
        if (Object != nullptr && (Object->ObjectClass & CObject::ClassShip) == CObject::ClassShip) {
            return static_cast<CShip*>(const_cast<CObject*>(Object));
        }
        return nullptr;
    }

    CEqObj* InspectEqObj(CObject const* Object)
    {
        if (Object != nullptr && (Object->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj) {
            return static_cast<CEqObj*>(const_cast<CObject*>(Object));
        }
        return nullptr;
    }
}

// ============================================================================
// IObjInspectImpl — core identity (all real, simple field reads)
// ============================================================================

int IObjInspectImpl::get_toughness() const
{
    // Ghidra @ 0x5F160: Reputation::Vibe::GetRank(this+0x104) if CEqObj
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        unsigned Rank = 0;
        Reputation::Vibe::GetRank(Eq->VibeId, Rank);
        return static_cast<int>(Rank);
    }
    return 0;
}

Vector const& IObjInspectImpl::get_position() const
{
    // .text:062bf1a0-062bf1a6: `MOV EAX,[this+0x10]; ADD EAX,0x2c; RET` -- Object is not
    // null-tested.
    return Object->WorldPosition;
}

Vector IObjInspectImpl::get_velocity() const
{
    // .text:062bf1b0-062bf1c1: Object (no null test) -> virtual +0x54, CObject::get_velocity.
    return Object->get_velocity();
}

Vector IObjInspectImpl::get_angular_velocity() const
{
    // .text:062bf1d0-062bf1e1: Object (no null test) -> virtual +0x58,
    // CObject::get_angular_velocity.
    return Object->get_angular_velocity();
}

Transform const& IObjInspectImpl::get_transform() const
{
    // .text:062bf1f0-062bf1f6 (ICF-folded with get_orientation): `MOV EAX,[this+0x10];
    // ADD EAX,8; RET` -- no null test. The Transform is EngineObject::Orientation (+0x08)
    // followed by WorldPosition (+0x2c), the same 0x30 bytes as Transform's Rotation /
    // Translation.
    return *(Transform const*)&Object->Orientation;
}

Matrix const& IObjInspectImpl::get_orientation() const
{
    // .text:062bf1f0 (ICF-folded with get_transform): Object + 8, no null test.
    return Object->Orientation;
}

long IObjInspectImpl::get_index() const
{
    // .text:062bf200-062bf206: `MOV EAX,[this+0x10]; MOV EAX,[EAX+4]; RET` -- no null test.
    return Object->EngineInstanceId;
}

unsigned int const IObjInspectImpl::get_id() const
{
    // .text:062bf210 .. 062bf22a: the (ObjectClass & 3) == 3 guard yields the CSimple or
    // null (XOR EAX,EAX at 062bf222), and 062bf224 `MOV EAX,[EAX+0xb0]` reads Id through it
    // UNCONDITIONALLY -- there is no `return 0` path. Server.dll's local copy (.text:06ce61d0,
    // GameObject's IObjInspect slot 8) is byte-identical. The port's old `if (S) ... return 0`
    // was a guard retail does not have; removed 2026-09-25 (claude-code:g-vt1).
    return InspectSimple(Object)->Id;
}

int IObjInspectImpl::get_good_id(unsigned& OutId) const
{
    // .text:062bf230-062bf26a, RET 0x4: Object null or (class & 0x803) != 0x803 (not a
    // CLoot) -> -3, OutId untouched. Otherwise 062bf248-062bf25e: CLoot::ContentsArch (+0xec,
    // no null test), its id (+0x08) through Arch2Good into OutId, 0. This is the loot's
    // CONTENTS archetype, not CObject::Archetype (+0x88, the crate).
    if (Object != nullptr && (Object->ObjectClass & CObject::ClassLoot) == CObject::ClassLoot)
    {
        OutId = Arch2Good(((CLoot const*)Object)->ContentsArch->Nickname.Id);
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_archetype_extents(Vector& Min, Vector& Max) const
{
    // .text:062bf270-062bf2a8, RET 0x8: Object (no null test) -> virtual +0x70,
    // CObject::get_surface_extents(Min, Max). True -> 0; false -> both vectors zeroed
    // (062bf28f-062bf29f) and -5.
    if (Object->get_surface_extents(Min, Max))
    {
        return 0;
    }
    memset(&Min, 0, sizeof(Min));
    memset(&Max, 0, sizeof(Max));
    return -5;
}

float IObjInspectImpl::get_mass() const
{
    // .text:062bf2d0-062bf2d5: Object (no null test) -> `JMP [EAX+0x6c]`, CObject::get_mass.
    return Object->get_mass();
}

int IObjInspectImpl::get_physical_radius(float& Radius, Vector& Center) const
{
    // .text:062bf2b0..062bf2c5: Object (+0x10, no null test) -> virtual +0x64
    // (CObject::get_physical_radius_r(Center)), FSTP dword into Radius; returns 0.
    Radius = Object->get_physical_radius_r(Center);
    return 0;
}

// ============================================================================
// IObjInspectImpl — status / health
// ============================================================================

int IObjInspectImpl::get_status(float& Health) const
{
    // Ghidra @ 0x5F2E0 / raw asm 0x062bf2e0-0x062bf3c8 (ground truth -- the decompiled .c
    // dereferences a possibly-null pointer at this offset, unreliable). Two real bugs fixed
    // here vs. the previous port:
    //  1) the TypeBits==0x80 (asteroid/field) branch walks CEquipTraverser(0x100) calling
    //     vtable+0xc on each piece, which the port's own CEquip vtable dump (Common.dll.txt
    //     558303+) confirms is IsFunctioning(), NOT IsDestroyed() -- and retail OVERWRITES
    //     Health=0/return 0 as soon as it finds ONE non-functioning piece (pessimistic:
    //     any failure kills the field), only reaching Health=1/return 0 if every piece is
    //     functioning. The prior port inverted this (returned 1.0 on the first NON-destroyed
    //     piece) and used the wrong predicate entirely.
    //  2) if Traverse() finds nothing at all on the very first call, retail returns -3, not
    //     0/0.0 (see the EDI=-3 preload used verbatim as the return value on that path).
    //  3) the non-shield get_relative_health() path clamps a negative result to 0.0
    //     (FCOMP against 0.0 + conditional zero-store in the raw asm), which the port dropped.
    // 062bf2f0-062bf303: the CSimple narrowing yields 0 for a null / non-CSimple Object and
    // TypeBits (+0xe0) is read through it unconditionally; 062bf313-062bf349 likewise walks
    // the CEqObj narrowing's EquipManager (+0xe4) with no null test. -3 (EDI) is returned only
    // when the first Traverse finds nothing (062bf3c0).
    CSimple* S = InspectSimple(Object);
    if (S->TypeBits == 0x80) {
        // Asteroid/field type: EqObj is re-derived from the raw Object here (as retail does),
        // not from S, though in practice they coincide whenever this branch is reachable.
        CEqObj* Eq = InspectEqObj(Object);
        CEquipTraverser Walk(0x100);
        CEquip* E = Eq->EquipManager.Traverse(Walk);
        if (E == nullptr) {
            return -3;
        }
        do {
            if (!E->IsFunctioning()) {
                Health = 0.0f;
                return 0;
            }
            Health = 1.0f;
            E = Eq->EquipManager.Traverse(Walk);
        } while (E != nullptr);
        return 0;
    }
    // 062bf395..062bf3af: FLD ST0 / FSTP float Health, then FCOMP 0.0f on the unrounded value
    // (TEST AH,0x5 / JP: only "less" falls through to the zero store).
    double const Relative = S->get_relative_health();
    Health = (float)Relative;
    if (Relative < 0.0) {
        Health = 0.0f;
    }
    return 0;
}

int IObjInspectImpl::get_status(float& HitPts, float& MaxHitPts) const
{
    // Ghidra @ 0x5F3D0: CSimple::get_hit_pts / get_max_hit_pts
    CSimple* S = InspectSimple(Object);
    if (S == nullptr) {
        return -3;
    }
    HitPts = S->get_hit_pts();
    MaxHitPts = S->get_max_hit_pts();
    return 0;
}

int IObjInspectImpl::get_shield_status(float& Health, bool& bActive) const
{
    // Ghidra @ 0x5F410: calls self vtable+0x44 (3-arg get_shield_status), then computes ratio
    float CurHealth = 0.0f;
    float MaxHealth = 0.0f;
    int Ret = get_shield_status(CurHealth, MaxHealth, bActive);
    if (Ret != 0) {
        return Ret;
    }
    if (MaxHealth > 0.0f) {
        Health = CurHealth / MaxHealth;
    } else {
        Health = 0.0f;
    }
    return 0;
}

int IObjInspectImpl::get_shield_status(float& CurHealth, float& MaxHealth, bool& bActive) const
{
    // Ghidra @ 0x5F480 / raw asm 0x062bf480-0x062bf52e (ground truth): iterate shields via
    // CEquipTraverser(0x100), accumulating GetHitPoints() (vtbl+0x3c) into CurHealth and
    // GetMaxHitPoints() (vtbl+0x38) into MaxHealth -- but bActive is a plain OVERWRITE each
    // iteration with IsFunctioning() (vtbl+0xc, confirmed via the port's own CEquip vtable
    // dump, Common.dll.txt 558303+), ending as the LAST traversed piece's functioning state.
    // The prior port instead OR-accumulated "!IsDestroyed()" across all pieces -- a different
    // predicate (destroyed vs. functioning) combined a different way (any vs. last).
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq == nullptr) {
        return -3;
    }
    CurHealth = 0.0f;
    MaxHealth = 0.0f;
    bActive = false;
    CEquipTraverser Walk(0x100);
    CEquip* E = Eq->EquipManager.Traverse(Walk);
    while (E != nullptr) {
        CurHealth += E->GetHitPoints();
        MaxHealth += E->GetMaxHitPoints();
        bActive = E->IsFunctioning();
        E = Eq->EquipManager.Traverse(Walk);
    }
    return 0;
}

int IObjInspectImpl::get_data(void const*& OutData) const
{
    // Ghidra @ 0x5F540: always returns null/-3
    OutData = nullptr;
    return -3;
}

// ============================================================================
// IObjInspectImpl — movement / physics queries
// ============================================================================

float IObjInspectImpl::get_time_to_accelerate(float From, float To, float Dt,
                                               ThrustEquipType Type) const
{
    // Ghidra @ 0x5F560: tail-call CShip::get_time_to_accelerate if CShip
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_time_to_accelerate(From, To, Dt, Type);
    }
    return -1.0f;
}

float IObjInspectImpl::get_distance_travelled(float Speed, float Dt, float Accel,
                                               ThrustEquipType Type) const
{
    // Ghidra @ 0x5F5A0: tail-call CShip::get_distance_travelled if CShip, else -1.0f
    // (Common.dll.c 97383 -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_distance_travelled(Speed, Dt, Accel, Type);
    }
    return -1.0f;
}

float IObjInspectImpl::get_initial_speed_to_coast_distance(float Speed, bool bStop) const
{
    // Ghidra @ 0x5F5E0: tail-call CShip::get_initial_speed_to_coast_distance if CShip, else
    // -1.0f (Common.dll.c 97406 -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_initial_speed_to_coast_distance(Speed, bStop);
    }
    return -1.0f;
}

float IObjInspectImpl::get_speed(float Throttle, ThrustEquipType Type) const
{
    // Ghidra @ 0x5F650: tail-call CShip::get_speed if CShip, else -1.0f (Common.dll.c 97449 --
    // BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_speed(Throttle, Type);
    }
    return -1.0f;
}

float IObjInspectImpl::get_projected_throttle(float Speed, ThrustEquipType Type) const
{
    // Ghidra @ 0x5F620: tail-call CShip::get_projected_throttle if CShip, else -1.0f
    // (Common.dll.c 97427 -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_projected_throttle(Speed, Type);
    }
    return -1.0f;
}

float IObjInspectImpl::get_time_to_accelerate_angularly(float From, float To, float Dt) const
{
    // Ghidra @ 0x5F720: tail-call CShip::get_time_to_accelerate_angularly(3) if CShip, else
    // DAT_06399404 = -1.0f (raw asm 0x062bf753 -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_time_to_accelerate_angularly(From, To, Dt);
    }
    return -1.0f;
}

float IObjInspectImpl::get_time_to_accelerate_angularly(float From, float To, float Dt,
                                                         float MaxSpeed) const
{
    // Ghidra @ 0x5F680: tail-call CShip::get_time_to_accelerate_angularly(4) if CShip, else
    // DAT_06399404 = -1.0f (raw asm 0x062bf6bb -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_time_to_accelerate_angularly(From, To, Dt, MaxSpeed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_angular_distance_travelled(float From, float To, float Dt) const
{
    // Ghidra @ 0x5F760: tail-call CShip::get_angular_distance_travelled(3) if CShip, else
    // DAT_06399404 = -1.0f (raw asm 0x062bf793 -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_angular_distance_travelled(From, To, Dt);
    }
    return -1.0f;
}

float IObjInspectImpl::get_angular_distance_travelled(float From, float To, float Dt,
                                                       float MaxSpeed) const
{
    // Ghidra @ 0x5F6D0: tail-call CShip::get_angular_distance_travelled(4) if CShip, else
    // DAT_06399404 = -1.0f (raw asm 0x062bf70b -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_angular_distance_travelled(From, To, Dt, MaxSpeed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_angular_speed_XY(float Speed, float MaxSpeed) const
{
    // Ghidra @ 0x5F800: tail-call CShip::get_angular_speed_XY if CShip, else -1.0f
    // (Common.dll.c 97596 -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_angular_speed_XY(Speed, MaxSpeed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_projected_axis_throttle_XY(float Speed) const
{
    // Ghidra @ 0x5F7A0: tail-call CShip::get_projected_axis_throttle_XY if CShip, else -1.0f
    // (Common.dll.c 97557 -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_projected_axis_throttle_XY(Speed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_projected_axis_throttle_Z(float Speed) const
{
    // Ghidra @ 0x5F7D0: tail-call CShip::get_projected_axis_throttle_Z if CShip, else -1.0f
    // (Common.dll.c 97576 -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_projected_axis_throttle_Z(Speed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_angular_speed_Z(float Speed) const
{
    // Ghidra @ 0x5F840: tail-call CShip::get_angular_speed_Z if CShip, else -1.0f
    // (Common.dll.c 97615 -- BUG FIX: was 0.0f).
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_angular_speed_Z(Speed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_max_bank_angle() const
{
    // Ghidra @ 0x5F870: tail-call CShip::get_max_bank_angle if CShip
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_max_bank_angle();
    }
    return 0.0f;
}

int IObjInspectImpl::get_throttle(float& OutThrottle) const
{
    // Ghidra @ 0x5F8A0: if CShip, read *(float*)(delegate + 0x29c) -- CShip::Throttle
    // (common_cobject.h:1572, static_assert'd at +0x29c). Read via the named field instead of
    // a raw offset cast now that it exists.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutThrottle = Sh->Throttle;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_axis_throttle(Vector& OutThrottle) const
{
    // .text:062bf8d0-062bf90b, RET 0x4: a CShip copies the three floats at +0x24c
    // (CShip::get_axis_throttle) into OutThrottle, 0; otherwise -3 with OutThrottle untouched.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutThrottle = Sh->get_axis_throttle();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_nudge_vec(Vector& OutVec) const
{
    // .text:062bf910-062bf957, RET 0x4: a CShip copies CShip::get_nudge_vec() (+0x274) into
    // OutVec, 0; otherwise -3 with OutVec untouched.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutVec = Sh->get_nudge_vec();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_strafe_dir(StrafeDir& OutDir) const
{
    // .text:062bf960-062bf98e, RET 0x4: a CShip writes CShip::get_strafe_dir() (+0x298) into
    // OutDir, 0; otherwise -3 with OutDir untouched.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutDir = static_cast<StrafeDir>(Sh->get_strafe_dir());
        return 0;
    }
    return -3;
}

// ============================================================================
// IObjInspectImpl — combat / target
// ============================================================================

int IObjInspectImpl::are_thrusters_active(bool& OutActive) const
{
    // Ghidra @ 0x5FA80: CShip::any_thrusters_on
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutActive = Sh->any_thrusters_on();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::is_cruise_active(bool& OutActive) const
{
    // Ghidra @ 0x5F9A0: CShip::is_cruise_active
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutActive = Sh->is_cruise_active();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::is_cruise_active(bool& OutCruise, bool& OutTradLane) const
{
    // BUG FIX: Ghidra @ 0x5F9E0 / raw asm 0x062bf9e0-0x062bfa78 (ground truth -- the decompiled
    // .c scales a CEEngine* by sizeof(CEEngine) here, unreliable) is NOT CShip::is_cruise_active()
    // + is_using_tradelane() as previously assumed. Retail finds the ship's first EquipTypeEngine
    // equipment, casts to CEEngine, and: OutCruise = Engine->bCruiseActive; if false, OutTradLane
    // = false; else OutTradLane = (Engine's CruiseChargeTime > CruiseChargeTimeElapsed), i.e.
    // true while the cruise engine is still spooling up, false once fully charged. Retail reaches
    // that field via CEArmor::ArmorArch() on the CEEngine (a same-offset archetype-pointer
    // reinterpret trick, since CEEngine and CEArmor are sibling CInternalEquip subclasses sharing
    // the Arch field) -- this port's own CEEngine::GetCruiseChargeTime() already reads the
    // identical Archetype::Engine+0x80 field directly, so that's used here instead of
    // replicating the cross-cast. Despite the interface parameter's SDK name "OutTradLane",
    // retail's actual computation is charge-in-progress, not tradelane use.
    CShip* Sh = InspectShip(Object);
    if (Sh == nullptr) {
        return -3;
    }
    CEquip* Equip = const_cast<CEquip*>(Sh->EquipManager.FindFirst(EquipTypeEngine));
    CEEngine* Engine = CEEngine::cast(Equip);
    if (Engine == nullptr) {
        return -3;
    }
    OutCruise = Engine->IsCruiseEngaged();
    if (!OutCruise) {
        OutTradLane = false;
        return 0;
    }
    OutTradLane = (Engine->GetCruiseChargeTime() > Engine->GetCruiseChargeTimeElapsed());
    return 0;
}

int IObjInspectImpl::get_target(IObjRW*& OutTarget) const
{
    // .text:062bfc00-062bfc4b, RET 0x4. Object (+0x10) is read with no null test and its
    // class compared EXACTLY (CMP, no mask): 0xa7 (CGuided) -> the watched target (+0xf0)
    // minus 8, or 0, and 0; 0x503 (CShip) -> CShip::get_target(), 0; any other class -> -3,
    // OutTarget untouched.
    if (Object->ObjectClass == CObject::ClassGuided) {
        CGuided const* G = (CGuided const*)Object;
        OutTarget = G->get_target();
        return 0;
    }
    if (Object->ObjectClass == CObject::ClassShip) {
        CShip const* Sh = (CShip const*)Object;
        OutTarget = Sh->get_target();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_rank(uint& OutRank) const
{
    // .text:062bfc50-062bfcaa, RET 0x4: not a CEqObj -> -3. Vibe::GetRank gets a stack copy
    // of VibeId (+0x104) and a stack out-word; only when it returns 0 is the word copied into
    // OutRank (062bfc90-062bfc98), else -2 with OutRank untouched.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        int VibeId = static_cast<int>(Eq->VibeId);
        unsigned Rank;
        if (Reputation::Vibe::GetRank(VibeId, Rank) == 0) {
            OutRank = Rank;
            return 0;
        }
        return -2;
    }
    return -3;
}

int IObjInspectImpl::get_affiliation(uint& OutAffil) const
{
    // .text:062bfcb0-062bfd0c, RET 0x4: same shape as get_rank -- Vibe::GetAffiliation(copy of
    // VibeId, stack out-word, false); the word reaches OutAffil only on 0 (062bfcf2-062bfcfa),
    // else -2 with OutAffil untouched; not a CEqObj -> -3.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        int VibeId = static_cast<int>(Eq->VibeId);
        unsigned Affiliation;
        if (Reputation::Vibe::GetAffiliation(VibeId, Affiliation, false) == 0) {
            OutAffil = Affiliation;
            return 0;
        }
        return -2;
    }
    return -3;
}

int IObjInspectImpl::get_type(uint& OutType) const
{
    // Ghidra @ 0x5FD10 (Common.dll.c 97982-97995): CSimple::TypeBits at +0xe0. Retail zeroes
    // *param_1 unconditionally up front, even on the -3 failure path.
    OutType = 0;
    CSimple* S = InspectSimple(Object);
    if (S != nullptr) {
        OutType = S->TypeBits;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_base(uint& OutBase) const
{
    // .text:062bfd50-062bfd7b, RET 0x4: Object null or (class & 0x303) != 0x303 -> -3 with
    // OutBase untouched; else dword +0x160 (CEqObj::DockingBaseId) into OutBase, 0.
    if (Object != nullptr && (Object->ObjectClass & CObject::ClassSolar) == CObject::ClassSolar) {
        OutBase = static_cast<CEqObj const*>(Object)->DockingBaseId;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_dock_target(uint& OutDock) const
{
    // BUG FIX: Ghidra @ 0x5FD80 (Common.dll.c 98021-98035) gates on the SAME CSolar-only 0x303
    // bitmask as get_base() above (not the generic CEqObj 0x103 mask the port used), and reads
    // DockingPointBaseId at +0x164 (the field right after get_base's DockingBaseId at +0x160,
    // common_cobject.h:1248) -- i.e. this only succeeds for a CSolar-family object, not any
    // CEqObj.
    if (Object != nullptr && (Object->ObjectClass & CObject::ClassSolar) == CObject::ClassSolar) {
        CEqObj* Eq = static_cast<CEqObj*>(const_cast<CObject*>(Object));
        OutDock = Eq->DockingPointBaseId;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_zone_props(unsigned long& OutProps) const
{
    // Common.dll.c 0x5fdb0
    CSimple* S = InspectSimple(Object);
    if (S != nullptr)
    {
        if (S->ZoneAtA4 != nullptr)
        {
            // Zone slot +0x1c (Common 062a6690, ZONE::GetProperties).
            OutProps = S->ZoneAtA4->GetProperties();
            return 0;
        }
        OutProps = 0;
        return -2;
    }
    return -3;
}

float IObjInspectImpl::get_scanner_interference() const
{
    // .text:062bfe00..062bfe1c: narrow the delegate, then tail-call its actual
    // zone-adjusted accessor. The raw +0xa8 field is only the zone falloff.
    CSimple const* const Simple = InspectSimple(Object);
    if (Simple != nullptr)
        return Simple->get_scanner_interference();
    return 0.0f;
}

int IObjInspectImpl::get_power(float& OutPower) const
{
    // Ghidra @ 0x5FE20: CEqObj Power at +0x198
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        OutPower = Eq->Power;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_reputation(float& OutRep, uint const& GroupId) const
{
    // Ghidra @ 0x5FBC0: Reputation::Vibe::GetGroupFeelingsTowards(this+0x104, group, out)
    // Returns 0 on success, -4 on any Vibe error (NEG/SBB/AND pattern).
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        int VibeId = static_cast<int>(Eq->VibeId);
        int Result = Reputation::Vibe::GetGroupFeelingsTowards(VibeId, GroupId, OutRep);
        // Retail: NEG EAX; SBB EAX,EAX; AND EAX,0xfffffffc
        // Maps 0→0, any nonzero→-4
        if (Result != 0)
            return -4;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_attitude_towards(float& OutAtt, IObjInspect const* Other) const
{
    // Ghidra @ 0x5FAC0: get CEqObj from both, call CEqObj::attitude_towards
    if (Other == nullptr) {
        return -4;
    }
    // Get CEqObj from Other via cobject()
    CObject const* OtherObj = Other->cobject();
    if (OtherObj == nullptr || (OtherObj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj) {
        return -3;
    }
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        CEqObj const* OtherEq = static_cast<CEqObj const*>(OtherObj);
        OutAtt = Eq->attitude_towards(OtherEq);
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_attitude_towards_symmetrical(float& OutAtt, IObjInspect const* Other,
                                                       float& OutReverse) const
{
    // Ghidra @ 0x5FB40: bidirectional attitude via CEqObj::attitude_towards_symmetrical
    if (Other == nullptr) {
        return -4;
    }
    CObject const* OtherObj = Other->cobject();
    if (OtherObj == nullptr || (OtherObj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj) {
        return -3;
    }
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        CEqObj const* OtherEq = static_cast<CEqObj const*>(OtherObj);
        Eq->attitude_towards_symmetrical(OutAtt, OtherEq, OutReverse);
        return 0;
    }
    return -3;
}

bool IObjInspectImpl::is_player() const
{
    // Ghidra @ 0x5FE60: CShip+0x2bd
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->bIsPlayer != 0;
    }
    return false;
}

int IObjInspectImpl::get_hardpoint(char const* Name, Vector* Pos, Matrix* Orient) const
{
    // .text:062bfe90-062bfec1, RET 0xc: HardpointPosition(Object->EngineInstanceId (+0x04, no
    // null test on Object), Name, Pos, Orient); true -> 0, false -> -2.
    bool bSuccess = HardpointPosition(Object->EngineInstanceId, Name, Pos, Orient);
    return bSuccess ? 0 : -2;
}

int IObjInspectImpl::find_equipment(CEquip const*& OutEquip, unsigned short const& EquipId) const
{
    // Common.dll.c 0x5fed0
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr)
    {
        CEquip* Found = Eq->EquipManager.FindByID(EquipId);
        OutEquip = Found;
        if (Found != nullptr && !Found->IsDestroyed())
        {
            return 0;
        }
        return -2;
    }
    return -3;
}

int IObjInspectImpl::find_equipment(unsigned short* const Buf, uint MaxCount,
                                    uint TypeMask) const
{
    // 062bff30. Returns 0, not the buffer: the tail is SBB EAX,EAX / AND EAX,ESI reached only
    // when MaxCount <= Index, where the borrow is always clear, so the result is always zero.
    // The buffer is never null-checked in retail either -- the parameter is ushort* const, and
    // the caller is expected to pass a real array.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq == nullptr)
        return -3;

    uint Index = 0;
    CEquipTraverser Walk(static_cast<int>(TypeMask));
    for (CEquip* EqItem = Eq->EquipManager.Traverse(Walk); EqItem != nullptr;
         EqItem = Eq->EquipManager.Traverse(Walk))
    {
        if (Index < MaxCount)
        {
            Buf[Index] = EqItem->Id;
            ++Index;
            if (MaxCount == Index)
                return 0;
        }
    }
    if (Index < MaxCount)
    {
        // 062bffac reads the SUBOBJ_ID_NONE word out of .rdata as the terminator.
        Buf[Index] = 0;
    }
    return 0;
}

int IObjInspectImpl::get_equipment_status(EquipStatus& OutStatus,
                                           unsigned short const& Id) const
{
    // Common.dll.c 0x5ffe0
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr)
    {
        CEquip* Found = Eq->EquipManager.FindByID(Id);
        if (Found != nullptr && !Found->IsDestroyed())
        {
            // Common.dll.c 0x5ffe0 (FIXED). Retail calls CEquip's own virtual GetStatus()
            // (0629ae80) directly, modifying only 3 specific bits of OutStatus.Flags in place
            // (it does not zero the struct first -- see CEquip::GetStatus in common_cequip.cpp,
            // which is now a real, correctly-ordered virtual override). The stale comment this
            // replaced predates that fix and claimed the vtable slot didn't exist yet.
            Found->GetStatus(OutStatus);
            return 0;
        }
        return -2;
    }
    return -3;
}

int IObjInspectImpl::get_equipment_val(EquipmentVal& OutVal, unsigned short const& Id,
                                        EquipmentValType Type, float Param) const
{
    // .text:062c0460-062c0837, RET 0x10. Re-ported 2026-10-08 (cmn-ai8) from the listing.
    // Retail writes ONE dword (or one byte, types 9 / 0xa of a gun) at &OutVal; nothing else.
    unsigned char* const Out = reinterpret_cast<unsigned char*>(&OutVal);
    int const Kind = static_cast<int>(Type);

    // 062c0460-062c04b8: Object (+0x10) must be a CEqObj, else -3; FindByID(Id) null or
    // vtable slot 2 (IsDestroyed) true -> -2.
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -3;
    CEquip* const Found = static_cast<CEqObj*>(Object)->EquipManager.FindByID(Id);
    if (Found == nullptr || Found->IsDestroyed())
        return -2;

    float FloatVal;
    int IntVal;
    switch (Found->Type)
    {
    case EquipTypePower:                                                       // 0x40000, 062c0755
    {
        // Type 0 -> ChargeRate (+0x74), type 4 -> Capacity (+0x70); 0.0f once destroyed.
        Archetype::Power const* const Arch = static_cast<Archetype::Power const*>(Found->Arch);
        if (Kind == 0)
            FloatVal = Found->IsDestroyed() ? 0.0f : Arch->ChargeRate;
        else if (Kind == 4)
            FloatVal = Found->IsDestroyed() ? 0.0f : Arch->Capacity;
        else
            return -3;
        std::memcpy(Out, &FloatVal, sizeof(FloatVal));
        return 0;
    }
    case 0x20000:                                                              // CEEngine, 062c04fc
    {
        if (Kind != 0)
            return -3;
        // 062c0508-062c0568: Param clamped to [-1, 1] (NaN passes through); unless the clamp
        // left it bit-for-bit equal (TEST AH,0x44 + JNP), -4.
        float Throttle;
        if (1.0f < Param)
            Throttle = 1.0f;
        else if (-1.0f > Param)
            Throttle = -1.0f;
        else
            Throttle = Param;
        if (!(Throttle == Param))
            return -4;
        FloatVal = static_cast<CEEngine*>(Found)->GetPowerDraw(Throttle);
        std::memcpy(Out, &FloatVal, sizeof(FloatVal));
        return 0;
    }
    case 0x400:                                                                // CEThruster, 062c0585
        if (Kind != 0)
            return -3;
        FloatVal = static_cast<CEThruster*>(Found)->GetPowerDraw();
        std::memcpy(Out, &FloatVal, sizeof(FloatVal));
        return 0;
    case 0x80:                                                                 // CEGun, 062c05a7
    {
        CEGun* const Gun = static_cast<CEGun*>(Found);
        // FUN_062969c0 (`MOV EAX,[ECX+0x4c]`) is the loaded projectile (munition) archetype;
        // FUN_0629fb90 (`MOV EAX,[ECX+0xc]`) the gun's own archetype.
        Archetype::Munition const* const Ammo = static_cast<Archetype::Munition const*>(Gun->ProjectileArch());
        Archetype::Gun const* const GunArch = static_cast<Archetype::Gun const*>(Found->Arch);
        switch (Kind)
        {
        case 0:                                                                // vtable +0x9c
            FloatVal = (float)Gun->GetPowerDrawPerFire();   // 062c0606 FSTP float [Out]
            std::memcpy(Out, &FloatVal, sizeof(FloatVal));
            return 0;
        case 1:
        {
            // FILD qword {ProjectilesPerFire, 0} * HullDamage (+0x80), FSTP float.
            unsigned const PerFire = Gun->GetProjectilesPerFire();
            FloatVal = static_cast<float>(static_cast<double>(PerFire) * Ammo->HullDamage);
            std::memcpy(Out, &FloatVal, sizeof(FloatVal));
            return 0;
        }
        case 2:
            std::memcpy(Out, &GunArch->RefireDelay, sizeof(float));             // +0x90
            return 0;
        case 3:                                                                // vtable +0xa0
            IntVal = Gun->GetAmmoCount();
            std::memcpy(Out, &IntVal, sizeof(IntVal));
            return 0;
        case 5:
            FloatVal = Gun->GetMunitionRange();
            std::memcpy(Out, &FloatVal, sizeof(FloatVal));
            return 0;
        case 6:
            // +0xac byte == 1 -> 3; HpType (+0xa8) == 0xa -> 2; else Seeker (+0x90) != 0.
            if (*reinterpret_cast<unsigned char const*>(&Ammo->bCruiseDisruptor) == 1)
                IntVal = 3;
            else if (Ammo->HpType == 0xa)
                IntVal = 2;
            else
                IntVal = Ammo->Seeker != 0 ? 1 : 0;
            std::memcpy(Out, &IntVal, sizeof(IntVal));
            return 0;
        case 8:
            std::memcpy(Out, &GunArch->MuzzleVelocity, sizeof(float));          // +0x94
            return 0;
        case 9:
            Out[0] = *reinterpret_cast<unsigned char const*>(&GunArch->bAutoTurret);   // +0xba byte
            return 0;
        case 0xa:
            Out[0] = Ammo->Seeker != 0 ? 1 : 0;                                // byte
            return 0;
        case 0xb:
        {
            // 062c05bb-062c05f5: CEGun::GetTarget; the target's Id (+0xb0), or 0.
            CSimple* Target = nullptr;
            unsigned short SubId = 0;
            Gun->GetTarget(Target, SubId);
            IntVal = Target != nullptr ? static_cast<int>(Target->Id) : 0;
            std::memcpy(Out, &IntVal, sizeof(IntVal));
            return 0;
        }
        default:                                                               // 4, 7, > 0xb
            return -3;
        }
    }
    case 0x80000:                                                              // CEScanner, 062c07fd
        if (Kind == 5)
        {
            FloatVal = static_cast<CEScanner*>(Found)->GetRadarRange();
            std::memcpy(Out, &FloatVal, sizeof(FloatVal));
            return 0;
        }
        if (Kind == 7)
        {
            std::memcpy(Out, &static_cast<Archetype::Scanner const*>(Found->Arch)->CargoScanRange, sizeof(float));
            return 0;
        }
        return -3;
    case 0x100000:                                                             // CETractor, 062c07e0
        if (Kind != 5)
            return -3;
        FloatVal = static_cast<CETractor*>(Found)->GetRange();
        std::memcpy(Out, &FloatVal, sizeof(FloatVal));
        return 0;
    default:
        return -3;
    }
}

int IObjInspectImpl::can_point_at(bool& OutCanPoint, unsigned short const& EquipId,
                                    Vector const& Point, float Angle) const
{
    // Common.dll.c 0x60170
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr)
    {
        CEquip* Found = Eq->EquipManager.FindByID(EquipId);
        if (Found == nullptr || Found->IsDestroyed())
            return -2;
        if (Found->GetType() != 0x80)
            return -3;
        CEGun* Gun = static_cast<CEGun*>(Found);
        OutCanPoint = Gun->CanPointAt(Point, Angle);
        return 0;
    }
    return -3;
}

int IObjInspectImpl::is_pointing_at(bool& OutIsPointing, unsigned short const& EquipId,
                                     float Angle) const
{
    // Common.dll.c 0x600e0
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr)
    {
        CEquip* Found = Eq->EquipManager.FindByID(EquipId);
        if (Found == nullptr || Found->IsDestroyed())
            return -2;
        if (Found->GetType() != 0x80)
            return -3;
        CEGun* Gun = static_cast<CEGun*>(Found);
        OutIsPointing = Gun->CanSeeTargetObject(Angle);
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_tgt_lead_fire_pos(unsigned short const& EquipId, Vector& OutPos) const
{
    // Common.dll.c 0x60050
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr)
    {
        CEquip* Found = Eq->EquipManager.FindByID(EquipId);
        if (Found == nullptr || Found->IsDestroyed())
            return -2;
        if (Found->GetType() != 0x80)
            return -3;
        CEGun* Gun = static_cast<CEGun*>(Found);
        bool bComputed = Gun->ComputeTgtLeadPosition(OutPos);
        return bComputed ? 0 : -1;
    }
    return -3;
}

int IObjInspectImpl::get_subtarget(unsigned short& OutSub) const
{
    // .text:062c0200-062c023c, RET 0x4. Object (+0x10) is read with no null test and its
    // class compared EXACTLY (CMP, no mask): 0xa7 (CGuided) -> word +0xf8; 0x503 (CShip) ->
    // CShip::get_sub_target(); any other class -> -3, OutSub untouched.
    if (Object->ObjectClass == CObject::ClassGuided)
    {
        CGuided const* G = (CGuided const*)Object;
        OutSub = G->SubTarget;
        return 0;
    }
    if (Object->ObjectClass == CObject::ClassShip)
    {
        CShip const* S = (CShip const*)Object;
        OutSub = S->get_sub_target();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_subtarget_center_of_mass(Vector& OutCenter) const
{
    // .text:062c0240-062c02a9, RET 0x4.
    // 062c0242-062c025c: Object (+0x10) null, or (ObjectClass & 0x503) != 0x503 -> -3.
    CShip const* Ship = InspectShip(Object);
    if (Ship == nullptr)
        return -3;

    // 062c025e-062c026a: this->get_subtarget (own vtable +0x78) into a stack word; nonzero ->
    // -3. 062c026c-062c027b: SUBOBJ_ID_NONE -> -1.
    unsigned short SubId = 0;
    if (get_subtarget(SubId) != 0)
        return -3;
    if (SubId == SUBOBJ_ID_NONE)
        return -1;

    // 062c027d-062c028b: CShip::get_target(); none -> -5.
    IObjRW* TargetRW = Ship->get_target();
    if (TargetRW == nullptr)
        return -5;

    // 062c028d-062c029b: the TARGET's own IObjInspect::get_sub_obj_center_of_mass (vtable
    // +0x18) with (SubId, OutCenter); its status is returned as-is.
    return TargetRW->get_sub_obj_center_of_mass(SubId, OutCenter);
}

Vector IObjInspectImpl::get_center_of_mass() const
{
    // 062c0360 -- straight tail call into CObject vtable +0x68, which the CObject vtable at
    // .rdata 0639b66c resolves to CObject::get_center_of_mass. No null check in retail.
    return Object->get_center_of_mass();
}

int IObjInspectImpl::get_sub_obj_center_of_mass(unsigned short SubId, Vector& OutCenter) const
{
    // Common.dll.c 0x60380 (FIXED). Retail checks (Object->ObjectClass & 0x103) == 0x103
    // (CEqObj-derived) and forwards SubId to Object's own vtable+0xe0 virtual --
    // CObject::get_sub_obj_center_of_mass(SubId, Out), mapping its bool result to 0/-2 -- it
    // never ignores SubId and returns the object's flat WorldPosition unconditionally like the
    // previous port did. (CEqObj::get_sub_obj_center_of_mass itself is a separately documented,
    // already-flagged stub that always returns false -- see common_ceqobj.cpp -- so this now
    // faithfully reproduces retail's dispatch structure even though the underlying query is a
    // known gap.)
    if (Object != nullptr && (Object->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        // get_sub_obj_center_of_mass is a CEqObj virtual, not declared on CObject itself
        // (common_cobject.h) -- the ObjectClass check above guarantees this cast is safe.
        return static_cast<CEqObj*>(Object)->get_sub_obj_center_of_mass(SubId, OutCenter)
               ? 0 : -2;
    }
    return -3;
}

bool IObjInspectImpl::is_targetable() const
{
    // .text:062c03c0: Object null or (ObjectClass & 3) != 3 -> false; otherwise
    // `JMP [EAX+0x8c]`, the virtual CSimple::is_targetable (CEqObj overrides it).
    CSimple* S = InspectSimple(Object);
    if (S == nullptr)
        return false;
    return S->is_targetable();
}

int IObjInspectImpl::get_scanlist(ScanList const*& OutList, uint Type, bool bFlag) const
{
    // .text:062c03e0-062c044f, RET 0xc. 062c03e7: OutList = 0 first. 062c03ed-062c0417: the
    // CEqObj narrowing (0 for a null / non-CEqObj Object) and FindFirst(0x80000) on its
    // EquipManager (+0xe4) with no null test -- there is no -3 path. No scanner -> -2.
    // Otherwise bFlag false -> clear_cache; scan_for_types(Type); OutList = scanner + 0x24
    // (CEScanner::Scans); 0.
    OutList = nullptr;
    CEqObj* Eq = InspectEqObj(Object);
    CEquip* Equip = const_cast<CEquip*>(Eq->EquipManager.FindFirst(0x80000));
    CEScanner* Scanner = (CEScanner*)Equip;
    if (Scanner == nullptr)
    {
        return -2;
    }
    if (!bFlag)
    {
        Scanner->clear_cache();
    }
    Scanner->scan_for_types(Type);
    OutList = &Scanner->Scans;
    return 0;
}

int IObjInspectImpl::get_hold_left(float& OutHold) const
{
    // .text:062c0330-062c035b, RET 0x4: Object null or (class & 0x503) != 0x503 -> -3 with
    // OutHold untouched; else FSTP float CShip::get_cargo_hold_remaining() into OutHold, 0.
    CShip* Ship = InspectShip(Object);
    if (Ship != nullptr)
    {
        OutHold = (float)Ship->get_cargo_hold_remaining();          // 062c034f FSTP float
        return 0;
    }
    return -3;
}

CObject const* IObjInspectImpl::cobject() const
{
    // ICF-folded @ 0x3A70: return *(CObject**)(this + 0x10)
    return Object;
}

// ============================================================================
// IObjInspectImpl — dock / formation
// ============================================================================

bool IObjInspectImpl::has_dock_hardpoints() const
{
    // .text:062c0870-062c08be. 062c0870-062c0886: Object (+0x10) null, or
    // (ObjectClass & 0x103) != 0x103 -> false.
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return false;

    // 062c0888-062c089c: Archetype (CObject+0x88, loaded with no null test), then the
    // archetype's DockingSpheres first/last (Archetype::EqObj +0x88 / +0x8c loaded as values);
    // first == last -> false.
    std::vector<Archetype::EqObj::DockingSphere> const& Spheres =
        static_cast<Archetype::EqObj const*>(Object->Archetype)->DockingSpheres;

    // 062c08a0-062c08b7: walk the 0x10-byte records; Type (+0x0c) == 1 (berth), 6 (ring/pad)
    // or 7 (jump) -> true (062c08bc); end of the list -> false (062c08b9).
    for (std::vector<Archetype::EqObj::DockingSphere>::const_iterator It = Spheres.begin();
         It != Spheres.end(); ++It)
    {
        unsigned const Type = It->Type;
        if (Type == 1 || Type == 6 || Type == 7)
            return true;
    }
    return false;
}

int IObjInspectImpl::get_dock_hardpoints(int Index, TERMINAL_TYPE* Type, Transform* XformA,
                                          Transform* XformB, Transform* XformC,
                                          float* Distance) const
{
    // .text:062c08c0-062c0a9a, RET 0x18. Re-ported 2026-10-08 (cmn-ai8).
    // 062c08c6-062c08eb: Object (+0x10) must be a CEqObj ((class & 0x103) == 0x103), else -5.
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -5;

    // 062c08ee-062c0921: the archetype's docking spheres (+0x88 begin / +0x8c end, 0x10-byte
    // records): Index -1, no buffer, or Index >= count (unsigned) -> -4.
    std::vector<Archetype::EqObj::DockingSphere> const& Spheres =
        static_cast<Archetype::EqObj const*>(Object->Archetype)->DockingSpheres;
    if (Index == -1 || Spheres.data() == nullptr || static_cast<unsigned>(Index) >= Spheres.size())
        return -4;
    Archetype::EqObj::DockingSphere const& Sphere = Spheres[static_cast<unsigned>(Index)];

    // 062c0931-062c0948: the optional type (+0x0c) and radius (+0x00) outputs.
    if (Type != nullptr)
        *Type = static_cast<TERMINAL_TYPE>(Sphere.Type);
    if (Distance != nullptr)
        *Distance = Sphere.Radius;

    // 062c094a-062c0995: XformA from the mount hardpoint (get_hardpoint +0xc0 with
    // &XformA->Translation and XformA as the Matrix); failure -> FDUMP warning, -5.
    char const* const MountName = Sphere.HardpointName.c_str();
    if (get_hardpoint(MountName, &XformA->Translation, &XformA->Rotation) != 0)
    {
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", "E:\\FL\\Scratch\\Source\\Common\\IObjImpl.cpp", 0x4f4,
              "station hardpoint in file must be misnamed");
        return -5;
    }

    // 062c0998-062c09dd: the name must contain "hpdockmount", else the same warning (line
    // 0x4fc) and -5.
    if (std::strstr(MountName, "hpdockmount") == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", "E:\\FL\\Scratch\\Source\\Common\\IObjImpl.cpp", 0x4fc,
              "station hardpoint in file must be misnamed");
        return -5;
    }

    // 062c09e0-062c0a8e: the mount's last character picks "hpdockpoint<c>01" for XformB and
    // "hpdockpoint<c>02" for XformC; a missing point copies XformA (12 dwords) and makes the
    // result -2 (it starts at 0 in the reused Index slot).
    char const Suffix = MountName[std::strlen(MountName) - 1];
    char PointName[0x40];
    int Result = 0;
    std::sprintf(PointName, "%s%c01", "hpdockpoint", Suffix);
    if (get_hardpoint(PointName, &XformB->Translation, &XformB->Rotation) != 0)
    {
        *XformB = *XformA;
        Result = -2;
    }
    std::sprintf(PointName, "%s%c02", "hpdockpoint", Suffix);
    if (get_hardpoint(PointName, &XformC->Translation, &XformC->Rotation) != 0)
    {
        Result = -2;
        *XformC = *XformA;
    }
    return Result;
}

int IObjInspectImpl::enumerate_subtargets(SubtargetEnumerator* Enum) const
{
    // Common.dll.c 0x60aa0 (FIXED). Retail runs TWO independent loops, not one: first the
    // equipment traversal (mask 0x1fe0) calling Enum->enumerate_equip(), THEN -- unconditionally,
    // even if the equip loop's callback returned false and broke out early -- a SEPARATE
    // CArchGroupManager traversal calling Enum->enumerate_subpart(Group->GetID(),
    // Group->GetCollisionGroupType()) for every physical sub-object/collision group, stopping
    // only if that second callback itself returns false. The previous port only ran the first
    // loop, so callers never learned about a ship's physical subtargets (turrets, engines,
    // etc.), only its equipment.
    // .text:062c0aa0-062c0b66, RET 0x4: Object null / not CEqObj -> -3; Enum is not tested.
    // Equip callback slot +0x00 (Id word +0x08, Type +0x14); subpart callback slot +0x04
    // (inlined GetID = [[Group+4]+4] word, GetCollisionGroupType); 0.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq == nullptr)
        return -3;

    CEquipTraverser Walk(0x1fe0);
    CEquip* EqItem = Eq->EquipManager.Traverse(Walk);
    while (EqItem != nullptr)
    {
        if (!Enum->enumerate_equip(EqItem->Id, EqItem->Type))
            break;
        EqItem = Eq->EquipManager.Traverse(Walk);
    }

    CArchGrpTraverser GroupWalk;
    CArchGroup* Group = Eq->ArchGroups.Traverse(GroupWalk);
    while (Group != nullptr)
    {
        if (!Enum->enumerate_subpart(Group->GetID(), Group->GetCollisionGroupType()))
            break;
        Group = Eq->ArchGroups.Traverse(GroupWalk);
    }

    return 0;
}

int IObjInspectImpl::enumerate_cargo(CargoEnumerator* Enum) const
{
    // .text:062c0b70-062c0bf8. Object (+0x10) must be a CEqObj ((ObjectClass & 0x103) ==
    // 0x103), otherwise -3. Enum is not tested. The 0x10000 traverser yields cargo only, and
    // retail reads CEquip+0x28 (CECargo::Count) without a cast check; the id is passed by
    // reference to a stack copy of CEquip+0x08, the archetype as the raw +0x0C pointer.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq == nullptr)
        return -3;

    CEquipTraverser Walk(0x10000);
    CEquip* E = Eq->EquipManager.Traverse(Walk);
    while (E != nullptr)
    {
        unsigned short const Id = E->Id;
        if (!Enum->enumerate(Id, E->Arch, static_cast<int>(static_cast<CECargo*>(E)->Count)))
            break;
        E = Eq->EquipManager.Traverse(Walk);
    }
    return 0;
}

int IObjInspectImpl::scan_cargo(IObjRW* Target, EquipDescVector& OutCargo) const
{
    // .text:062c0c00-062c0e2a, RET 0x8. The IObjRW parameter is the scanned TARGET; the
    // payload is the copy of this ship's scanner results (CEScanner+0x82c vector) into
    // OutCargo once ScanCargo reports ScanComplete.
    // 062c0c00-062c0c21: Object (+0x10) null or not a CEqObj -> -3.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq == nullptr)
        return -3;

    // 062c0c27-062c0c3d: own scanner = EquipManager.FindFirst(0x80000).
    CEquip* Equip = const_cast<CEquip*>(Eq->EquipManager.FindFirst(0x80000));
    CEScanner* Sc = reinterpret_cast<CEScanner*>(Equip);

    // 062c0c37-062c0c5a: the target's CObject through its own cobject() (vtable +0x150; no
    // null test on the IObjRW itself), kept only when (ObjectClass & 0x103) == 0x103.
    CObject const* TargetObj = Target->cobject();
    CEqObj* TargetEq = InspectEqObj(TargetObj);

    // 062c0c5c-062c0c66: no scanner or no CEqObj target -> -3.
    if (Sc == nullptr || TargetEq == nullptr)
        return -3;

    // 062c0c6c-062c0c77: ScanCargo(target) != 2 (ScanComplete) -> -3.
    CEScanner::ScanResult Res = Sc->ScanCargo(TargetEq);
    if (Res != CEScanner::ScanComplete)
        return -3;

    // 062c0c7d-062c0e23: OutCargo = scanner vector (retail skips the copy when &OutCargo is
    // the scanner's own vector, which a separate EquipDescVector cannot be here), then 0.
    // Shallow, non-owning view of the scanner's own buffer; operator= performs the real deep
    // copy into OutCargo, matching retail's inlined vector assignment (.text:062c0c83..).
    // The view must drop the scanner's pointers before it goes out of scope, because
    // ~EquipDescVector (0x06269110) frees Begin.
    EquipDescVector ScannerCargoView;
    ScannerCargoView.Begin = Sc->CargoVecBegin;
    ScannerCargoView.End = Sc->CargoVecEnd;
    ScannerCargoView.Cap = Sc->CargoVecCapacityEnd;
    OutCargo = ScannerCargoView;
    ScannerCargoView.Begin = nullptr;
    ScannerCargoView.End = nullptr;
    ScannerCargoView.Cap = nullptr;
    return 0;
}

int IObjInspectImpl::get_formation_offset(Vector& OutOffset) const
{
    // 062c02b0 -- copies the three floats out of CShip+0x1c0 and returns 0; -3 when the
    // delegate is absent or is not a ship. OutOffset is left alone on the failure path.
    CShip* Sh = InspectShip(Object);
    if (Sh == nullptr)
        return -3;
    OutOffset = Sh->FormationOffset;
    return 0;
}

int IObjInspectImpl::get_formation_leader(IObjRW*& OutLeader) const
{
    // .text:062c02f0..062c032d: CShip's +0x1b8 BaseWatcher; Watched != 0 -> Watched - 8
    // (the leader's IObjRW), else 0.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutLeader = Sh->FormationLeader.get();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_follow_offset(Vector& OutOffset) const
{
    // .text:062c10c0-062c10f1, RET 0x4: a CShip ((class & 0x503) == 0x503) copies the three
    // floats of CShip::get_follow_offset() (062b4f00, LEA [this+0x1e8]) into OutOffset. Retail
    // returns 0 on BOTH paths and leaves OutOffset untouched when there is no ship.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutOffset = Sh->get_follow_offset();
    }
    return 0;
}

int IObjInspectImpl::get_follow_leader(IObjRW*& OutLeader) const
{
    // .text:062c1100-062c1123, RET 0x4: a CShip writes CShip::get_follow_leader() into
    // OutLeader. Retail returns 0 on BOTH paths and leaves OutLeader untouched otherwise.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutLeader = Sh->get_follow_leader();
    }
    return 0;
}

bool IObjInspectImpl::generate_follow_offset(IObjInspect const* Leader, Transform& OutXform) const
{
    // .text:062c1130-062c114d, RET 0x8: a CShip tail-jumps into CShip::generate_follow_offset
    // with both arguments; otherwise false with OutXform untouched.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->generate_follow_offset(Leader, OutXform);
    }
    return false;
}

int IObjInspectImpl::get_behavior_id() const
{
    // .text:062c1150-062c1186: a CEqObj ((class & 0x103) == 0x103) answers with its behavior
    // manager's CurrentBehaviorIndex (+0xc0) when CEqObj::get_behavior_interface() is set,
    // else with its own dword +0x1a0; no CEqObj -> -1.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        IBehaviorManager* Mgr = Eq->get_behavior_interface();
        if (Mgr != nullptr) {
            return Mgr->CurrentBehaviorIndex;
        }
        return Eq->BehaviorIndex;
    }
    return -1;
}

int IObjInspectImpl::get_formation_followers(IObjRW** const OutFollowers, uint MaxCount) const
{
    // 062c1190 -- tail call into CShip::get_formation_followers; 0 when there is no ship.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_formation_followers(OutFollowers, MaxCount);
    }
    return 0;
}

int IObjInspectImpl::get_atmosphere_range(float& OutRange) const
{
    // 062c11b0 -- the range is a float at CSolar+0x1ac; the status code is the return value,
    // and OutRange is untouched when the object is not a solar.
    if (Object != nullptr && (Object->ObjectClass & CObject::ClassSolar) == CObject::ClassSolar) {
        OutRange = static_cast<CSolar const*>(Object)->AtmosphereRange;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::is_using_tradelane(bool* OutUsing) const
{
    // 062c1040 -- always returns 0 and always writes through the pointer, with no null check:
    // false when the delegate is not a ship, otherwise CShip::is_using_tradelane.
    CShip* Sh = InspectShip(Object);
    *OutUsing = (Sh != nullptr) ? Sh->is_using_tradelane() : false;
    return 0;
}

namespace
{
    // FUN_062c2430 (item 1243), cdecl, plain RET, Common.dll.txt 062c2430-062c25bd. Its only
    // caller is IObjInspectImpl::get_lane_start, which forwards (this, its four arguments,
    // its CSimple's ObjectDb). Self is the asking object, Ship the lane-entering one;
    // InOutLane, when set, is the ring already chosen.
    int ResolveLaneStart(IObjInspect const* Self, IObjInspect const* Ship, IObjInspect const*& InOutLane,
                         Vector& OutStart, Vector& OutDir, IObjDB* ObjectDb)
    {
        // 062c2434-062c244b: either object missing -> -4.
        if (Self == nullptr || Ship == nullptr)
            return -4;

        // 062c2451-062c2455: Ship->get_orientation() (+0x10), the result unused.
        (void)Ship->get_orientation();

        // 062c2458-062c2497: the lane direction into the reused first-argument slot -- from
        // the given ring against the ship's frame, else from the ship against Self's frame
        // (get_lane_direction +0x128; the frame is the folded get_orientation/get_transform
        // pointer). Nonzero -> -4.
        bool bForward = false;
        int Status;
        if (InOutLane != nullptr)
            Status = InOutLane->get_lane_direction(Ship->get_transform(), &bForward);
        else
            Status = Ship->get_lane_direction(Self->get_transform(), &bForward);
        if (Status != 0)
            return -4;

        // 062c249d-062c24f8: the next ring through traverse_rings (+0x130) and ObjectDb
        // FindById (+0x04); none -> the other way round, once.
        unsigned RingId = 0;
        Ship->traverse_rings(RingId, bForward);
        InOutLane = ObjectDb->FindById(&RingId);
        if (InOutLane == nullptr)
        {
            bForward = !bForward;
            Ship->traverse_rings(RingId, bForward);
            InOutLane = ObjectDb->FindById(&RingId);
        }

        // 062c24fa-062c2532: the ship's HpRightLane (forward) or HpLeftLane hardpoint
        // (get_hardpoint +0xc0); failure -> -5.
        Vector HardpointPos;
        Matrix HardpointOrient;
        if (Ship->get_hardpoint(bForward ? "HpRightLane" : "HpLeftLane", &HardpointPos, &HardpointOrient) != 0)
            return -5;

        // 062c2533-062c25a4: OutStart = that point; OutDir = FUN_062e1680(ring position - ship
        // position), each difference FSTP'd to float.
        OutStart = HardpointPos;
        Vector const& ShipPos = Ship->get_position();
        Vector const& LanePos = InOutLane->get_position();
        Vector const Delta{ LanePos.x - ShipPos.x, LanePos.y - ShipPos.y, LanePos.z - ShipPos.z };
        Vector Dir;
        VecNormalize3(&Delta, &Dir);
        OutDir = Dir;
        return 0;
    }
}

int IObjInspectImpl::get_lane_start(IObjInspect const* Self, IObjInspect const*& OutLane,
                                     Vector& OutStart, Vector& OutEnd) const
{
    // .text:062c1080-062c10b8, RET 0x10. Object (+0x10) is kept when (class & 3) == 3 (a
    // CSimple), else 0, and its ObjectDb (+0xac) is read with no null test (a null Object
    // faults in retail as here). Everything else is FUN_062c2430.
    CSimple const* Simple = nullptr;
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 3) == 3)
        Simple = static_cast<CSimple const*>(Object);
    return ResolveLaneStart(this, Self, OutLane, OutStart, OutEnd, Simple->ObjectDb);
}

int IObjInspectImpl::get_lane_direction(Transform const& Xform, bool* OutDir) const
{
    // .text:062c0e30..062c0f19. -4 unless Object (+0x10) is set, is a CSolar ((ObjectClass &
    // 0x303) == 0x303) and is a lane ring (signed byte +0xe0 < 0, CSolar::is_lane_ring).
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassSolar) != CObject::ClassSolar)
        return -4;
    if (!static_cast<CSolar const*>(Object)->is_lane_ring())
        return -4;

    // 062c0e64..062c0e8f: the ring centre is the physical-radius centre (virtual slot +0x2c);
    // only when that call returns non-zero is get_position() (slot +0x0) used instead.
    float Radius;
    Vector Center;
    if (get_physical_radius(Radius, Center) != 0)
        Center = get_position();

    // 062c0e93..062c0eb8: diff = centre - Xform.Translation (+0x24), each component FSTP
    // dword. 062c0ebc..062c0ef6: get_transform() (slot +0xc) column 2 (+0x08/+0x14/+0x20);
    // dot = (dz * m22 + m12 * dy) + m02 * dx on the x87 stack; FCOMP 0.0 / TEST AH,0x5 / JP:
    // only "less than" writes 1, everything else (>= 0, NaN) writes 0. *OutDir has no null
    // test.
    float const Dx = Center.x - Xform.Translation.x;
    float const Dy = Center.y - Xform.Translation.y;
    float const Dz = Center.z - Xform.Translation.z;
    Matrix const& Rot = get_transform().Rotation;
    double const Dot = (static_cast<double>(Dz) * Rot.E[2][2] + static_cast<double>(Rot.E[1][2]) * Dy) +
                       static_cast<double>(Rot.E[0][2]) * Dx;
    *OutDir = (Dot < 0.0);
    return 0;
}

int IObjInspectImpl::get_ring_side(Transform const& Xform, bool* OutSide) const
{
    // .text:062c0f20..062c0fd8. Same -4 preconditions as get_lane_direction (Object set,
    // CSolar, lane ring).
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassSolar) != CObject::ClassSolar)
        return -4;
    if (!static_cast<CSolar const*>(Object)->is_lane_ring())
        return -4;

    // 062c0f50..062c0f6c: get_transform() (slot +0xc) column 2 copied as floats;
    // 062c0f70..062c0fa7: get_position() (slot +0x0); diff = Xform.Translation - position kept
    // unrounded; dot = (dz * m22 + dy * m12) + dx * m02; FCOMP 0.0 / TEST AH,0x41 / JNZ:
    // only "greater than" writes 1, everything else (<= 0, NaN) writes 0. *OutSide has no
    // null test.
    Matrix const& Rot = get_transform().Rotation;
    float const M02 = Rot.E[0][2];
    float const M12 = Rot.E[1][2];
    float const M22 = Rot.E[2][2];
    Vector const& Pos = get_position();
    double const Dx = static_cast<double>(Xform.Translation.x) - Pos.x;
    double const Dy = static_cast<double>(Xform.Translation.y) - Pos.y;
    double const Dz = static_cast<double>(Xform.Translation.z) - Pos.z;
    double const Dot = (M22 * Dz + Dy * M12) + Dx * M02;
    *OutSide = (Dot > 0.0);
    return 0;
}

int IObjInspectImpl::traverse_rings(uint& OutRing, bool bForward) const
{
    // Common.dll.c 0x60fe0. FIXED: retail also requires is_lane_ring() (a signed-byte-negative
    // test on the low byte of CSimple::TypeBits at +0xe0, i.e. bit 7 of TypeBits -- see
    // CSolar::is_lane_ring()) before it will read Next/PrevRing at all; a previous version of
    // this port skipped that guard, so it would happily "traverse" rings on any solar/simple
    // object, lane ring or not. Also matches retail's exact return-code computation: the
    // top 2 bits of the ring id are masked off before the zero-check (`*param_1 & 0x3fffffff`)
    // -- likely tag/type bits packed into the high bits of a ring identifier -- so an id with
    // only those bits set is treated the same as "no ring" (-2).
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassSolar) != CObject::ClassSolar)
        return -4;

    CSolar const* Solar = static_cast<CSolar const*>(Object);
    if (!Solar->is_lane_ring())
        return -4;

    OutRing = bForward ? Solar->NextRing : Solar->PrevRing;
    return ((OutRing & 0x3fffffff) != 0) ? 0 : -2;
}

bool IObjInspectImpl::is_dying() const
{
    // ICF-folded @ 0x3BA00: always returns false
    return false;
}

// ===========================================================================
// Explicit STL instantiation exported like retail's. Retail Common.dll exports all 47
// members of its VC6 std::vector<IObjInspect*>. The port exports the modern
// instantiation; the importing declaration is in common_iface.h.
// ===========================================================================
template class COMMON_API std::vector<IObjInspect*>;

// ---------------------------------------------------------------------------
// InitEquipStatusBits -- formerly FUN_062e8270 (the dissolved common_stubs.cpp), Common.dll.c 0x62e8270
// (decomp line 118349): EquipStatus's default constructor as retail emits it out of line --
// `Flags &= 0xF8; EquipFlags &= 0xE0000000` (the low 3 bits of the flag byte and the low 29
// bits of the following dword cleared, the top bits kept). The same pair is inlined at
// .text:062eb96f-062eb989 (HasReadyWeaponSlot) and .text:062ebb24-062ebb41 (SetWatcherActive),
// where the port zero-initialises the local instead (a defined superset; documented there).
// No port caller: its one retail call (.text:062e7c2c) is in FUN_062e7a30, ported as
// FindClosestObstacleToAvoid (common_obstacleavoidance.cpp), which does not call it. Kept as a free
// function (not a ctor) in CMN-SPLIT stage 1 so those two zero-init sites stay unchanged.
// ---------------------------------------------------------------------------
void InitEquipStatusBits(EquipStatus* Status)
{
    unsigned char* const param_1 = (unsigned char*)Status;
    param_1[0] = param_1[0] & 0xf8;
    *reinterpret_cast<unsigned*>(param_1 + 4) &= 0xe0000000u;
}
