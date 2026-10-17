// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CELauncher (retail CELauncher.cpp) -- split out of common_cequip.cpp (CMN-B2).
// Declared in common_cequip.h; helpers shared with the sibling TUs are in
// common_cequip_internal.h (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine (MatrixToQuaternionViaMathEngine)
#include "common_fuses.h"    // WeaponModDB_GetShieldMod (CEShield::GetWeaponModifier)
#include "common_iface.h"    // EquipStatus, EquipStatusFlag* (CEquip::GetStatus)
#include "common_ini.h"
#include "common_misc.h"     // ComputeExplosiveImpulses (CAttachedEquip::Disconnect), SUBOBJ_ID_NONE
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
    const char kCELauncherSource[] = "E:\\FL\\Scratch\\Source\\Common\\CELauncher.cpp";

    // The barrel count every CELauncher member inlines (ComputeLaunchInfo 06299636..0629964f,
    // GetPowerDrawPerFire 06299583..0629959d, ConsumeFireResources 062995f4..0629960b, the
    // averages 06299c47 / 06299daa, GetProjectilesPerFire 06299f60, AmmoNeedsMet 0629a06f): the
    // leading run of Barrels[] whose EngineIndex is not -1. Very strange: retail has no upper
    // bound, so with all eight barrels bound it keeps reading 0x34-byte strides past Barrels[7]
    // (CEGun's yaw joint, or whatever follows a plain launcher) until it meets a -1. That count
    // depends on unrelated bytes and is not reproduced; the port stops at eight.
    unsigned CountLauncherBarrels(CELauncher const& Launcher)
    {
        unsigned Count = 0;
        unsigned Barrel = 0;
        for (; Barrel < 8; ++Barrel)
        {
            if (Launcher.Barrels[Barrel].EngineIndex == -1)
                break;
            Count += 1;
        }
        return Count;
    }

    // The world position of a barrel-local point: the 12-dword engine transform of the barrel
    // instance (IEngine+0xa0) copied to the stack, then 3DMathEngine+0x28 TransformXform
    // (GetBarrelPosWS 06299af7..06299b3b, ComputeProjectilePosAndVelocity_OneBarrel
    // 06299907..0629994d, GetAvgBarrelPosWS 06299cb5..06299cfa). No guards.
    Vector BarrelPointWS(long Instance, Vector const& Local)
    {
        Transform const Xform = *(Transform const*)DALib::Engine->GetInstanceTransform((void*)Instance);
        Vector Out;
        Acquire3DMathEngine()->TransformXform((FVector&)Out, (FTransform const&)Xform,
                                              (FVector const&)Local);
        return Out;
    }

    // The world firing direction of a barrel: the 9-dword engine orientation (IEngine+0x98) times
    // the hardpoint orientation (3DMathEngine+0x20 MulMatrix), negated third column
    // (GetBarrelDirWS 06299ba7..06299c13, GetAvgBarrelDirWS 06299e27..06299ecf). No guards.
    Vector BarrelDirWS(long Instance, Matrix const& HpOrient)
    {
        Matrix const Ori = *(Matrix const*)DALib::Engine->GetInstanceOrientation((void*)Instance);
        Matrix World;
        Acquire3DMathEngine()->MulMatrix((FMatrix&)World, (FMatrix const&)Ori, (FMatrix const&)HpOrient);
        Vector Out;
        Out.x = -World.E[0][2];
        Out.y = -World.E[1][2];
        Out.z = -World.E[2][2];
        return Out;
    }

    // .data DAT_063eb6b0: CELauncher::Connect's hardpoint name. Initial bytes "HPFire0\0\0" (raw
    // PE, capital P); the digit at +7 (DAT_063eb6b7) is patched per barrel.
    char GLauncherHpFireName[9] = { 'H', 'P', 'F', 'i', 'r', 'e', '0', '\0', '\0' };
}

CELauncher::CELauncher(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                       Archetype::Launcher const* ArchObj, bool bMissionFlag)
    : CAttachedEquip(TypeBits, OwnerObj, EquipId, ArchObj, bMissionFlag)
    , Projectile(nullptr)
    , RefireElapsed(0.0f)
{
    // Listing 0x062992b0..0x06299373. Retail only sets each barrel's EngineIndex (+0x54 + 0x34*i)
    // to -1 and leaves the rest of the barrel records unwritten; the port zeroes them first so
    // they are deterministic.
    memset(Barrels, 0, sizeof(Barrels));
    unsigned Barrel = 0;
    for (; Barrel < 8; ++Barrel)
        Barrels[Barrel].EngineIndex = -1;

    Archetype::Launcher* LauncherArch = (Archetype::Launcher*)(Arch);
    Projectile = Archetype::GetEquipment(LauncherArch->ProjectileArchetype.Id);   // Arch+0x98
    if (Projectile == nullptr)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Failed to find projectile archetype for gun.  Archetype is \"%s\"",
              kCELauncherSource, 0x22, LauncherArch->ProjectileArchetype.get_string());
    }
    else
    {
        Projectile->load();   // vt+0xc
    }
}

CELauncher::CELauncher(CELauncher const& Other)
    : CAttachedEquip(Other)
    , Projectile(Other.Projectile)
    , RefireElapsed(Other.RefireElapsed)
{
    memcpy(Barrels, Other.Barrels, sizeof(Barrels));
}

CELauncher::~CELauncher()
{
    // .text:06299380..062993d6: CExternalEquip::Destroy (direct call), then Root::Release on the
    // projectile archetype with no null test (the ctor already FDUMPed a missing one), then
    // ~CAttachedEquip.
    CExternalEquip::Destroy();
    Projectile->Release();
}

CELauncher* __cdecl CELauncher::cast(CEquip* Equip)
{
    if (Equip == nullptr)
        return nullptr;
    unsigned TypeBits = Equip->GetType();
    if (TypeBits != EquipTypeGun && TypeBits != EquipTypeMineDropper &&
        TypeBits != EquipTypeCmDropper)
        return nullptr;
    return (CELauncher*)(Equip);
}

CELauncher const* __cdecl CELauncher::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

bool CELauncher::Update(float Dt, unsigned Flags)
{
    if (RefireElapsed < 10000.0f)
        RefireElapsed = Dt + RefireElapsed;
    return CAttachedEquip::Update(Dt, Flags);
}

bool CELauncher::IsDisabled() const
{
    // .text:06299470..0629949a: Owner+0x15c, then vt+0x44 GetRelativeHealth() FCOMP 0.0,
    // TEST AH,0x41 / JP: true when health is less than or equal to 0, false when greater or
    // unordered. No Owner null test.
    if (Owner->bWeaponsOffline == 0)
        return false;
    return GetRelativeHealth() <= 0.0f;
}

void CELauncher::GetStatus(EquipStatus& OutStatus) const
{
    // Common.dll.c 0x39410: base fields first, then (only when neither Destroyed nor Bit2 is
    // already set) GetAmmoCount()==0 -> EquipFlags|=4 and stop; else PowerNeedsMet() ->
    // EquipFlags|=8; RefireDelayElapsed() -> EquipFlags|=2. Bit values match the
    // "0x2 / 0x4 / 0x8 from CELauncher::GetStatus" comment already on EquipStatus::EquipFlags
    // (common_iface.h:61).
    CEquip::GetStatus(OutStatus);
    if ((OutStatus.Flags & (EquipStatusFlagDestroyed | EquipStatusFlagDisabled)) == 0)
    {
        if (GetAmmoCount() == 0)
        {
            OutStatus.EquipFlags |= 4;
            return;
        }
        if (PowerNeedsMet())
            OutStatus.EquipFlags |= 8;
        if (RefireDelayElapsed())
            OutStatus.EquipFlags |= 2;
    }
}

bool CELauncher::Connect(char const* HpName)
{
    // Listing 0x062994a0..0x06299571. The name buffer is the .data string DAT_063eb6b0
    // ("HpFire01"); retail patches its digit (DAT_063eb6b7) in place for every barrel.
    if (!CAttachedEquip::Connect(HpName))
        return false;

    unsigned Barrel = 0;
    for (; Barrel < 8; ++Barrel)
    {
        GLauncherHpFireName[7] = (char)('1' + Barrel);
        long Found = -1;   // retail reuses the HpName argument slot; read only after a hit
        HardpointInfo Info;
        if (!FindHardpoint_OS(GetRootIndex(), GLauncherHpFireName, Found, Info))   // vt+0x84
        {
            if (Barrel != 0)
                return true;
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s has no HpFire01 hardpoint.", kCELauncherSource,
                  0x6f, Arch->DaArchetype.Value);
            return false;
        }
        Barrels[Barrel].EngineIndex = (int)(Found);
        memcpy(&Barrels[Barrel].Orient, &Info.Orient, sizeof(Matrix));
        memcpy(&Barrels[Barrel].Position, &Info.Position, sizeof(Vector));
    }
    return true;
}

void CELauncher::Disconnect()
{
    // Common.dll.c 0x3a1f0 (?Disconnect@CELauncher@@MAEXXZ).
    for (unsigned Barrel = 0; Barrel < 8; ++Barrel)
        Barrels[Barrel].EngineIndex = kInvalidEngineInstance;
    CAttachedEquip::Disconnect();
}

FireResult CELauncher::Fire(Vector const& Target)
{
    FireResult Result = CanFire(Target);
    if (Result != FireRefireDelay)
        RefireElapsed = 0.0f;
    return Result;
}

void CELauncher::ConsumeFireResources()
{
    int Count = (int)(CountLauncherBarrels(*this));
    DrawAmmoFromCargo(Count);
    DrawPower(Count);
}

void CELauncher::ComputeLaunchInfo(std::vector<ProjLaunchInfo>& Out, Vector const& Target) const
{
    // .text:06299630..062996d8. Per barrel: push_back a default record (FUN_0629a4c0 inserts one
    // copy at the end) and fill the stored element through vt+0xa8 ComputeLaunchInfo_OneBarrel.
    // The default (06299673..062996ab) zeroes Pos, Velocity and LockTarget and sets LockSubId to
    // SUBOBJ_ID_NONE; Orient and the two pad bytes are left unwritten in retail (zeroed here).
    unsigned Count = CountLauncherBarrels(*this);
    unsigned Barrel = 0;
    for (; Barrel < Count; ++Barrel)
    {
        ProjLaunchInfo Info;
        memset(&Info, 0, sizeof(Info));
        Info.LockSubId = SUBOBJ_ID_NONE;
        Out.push_back(Info);
        ComputeLaunchInfo_OneBarrel(Out.back(), (int)(Barrel), Target);
    }
}

double CELauncher::GetPowerDrawPerFire() const
{
    // .text:06299580..062995b9: FILD (qword) barrel count * Arch+0x8c, no Arch test, returned
    // unrounded in ST0.
    return (double)CountLauncherBarrels(*this) * LauncherArch()->PowerUsage;
}

int CELauncher::GetAmmoCount() const
{
    // .text:06299a10..06299a85. No Projectile / Owner / Arch tests: -1 when the projectile does
    // not need ammo (+0x78); else the Count (+0x28) of the first cargo (traverser 0x10000) whose
    // archetype nickname (+0xc -> +8) is the projectile's (+8), else 0.
    Archetype::Projectile const* Proj = (Archetype::Projectile const*)(Projectile);
    if (!Proj->bRequiresAmmo)
        return -1;

    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* Equip = Owner->EquipManager.Traverse(Walk);
    while (Equip != nullptr)
    {
        if (Equip->Arch->Nickname.Id == Projectile->Nickname.Id)
            return (int)(((CECargo*)Equip)->Count);
        Equip = Owner->EquipManager.Traverse(Walk);
    }
    return 0;
}

bool CELauncher::AmmoNeedsMet() const
{
    int Ammo = GetAmmoCount();
    if (Ammo < 0)
        return true;
    unsigned Need = CountLauncherBarrels(*this);
    if (Need != 0 && (unsigned)(Ammo) < Need)
        return false;
    return true;
}

FireResult CELauncher::CanFire(Vector const& Target) const
{
    // .text:06299f80..0629a052. Owner+0x140 is read with no Owner test (06299fd0); the later
    // `Owner && class == ship` test before is_using_tradelane is retail's own.
    (void)Target;
    if (IsDestroyed())
        return FireDestroyed;
    if (IsDisabled())
        return FireDisabled;
    if (!RefireDelayElapsed())
        return FireRefireDelay;
    if (!PowerNeedsMet())
        return FireNoPower;
    if (!AmmoNeedsMet())
        return FireNoAmmo;
    // 06299fd6: FCOMP 1.0, TEST AH,0x1 / JZ: not less (and ordered) -> FireOwnerBlocked.
    if (Owner->CloakPercent >= 1.0f)
        return FireOwnerBlocked;
    if (Owner != nullptr && (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
    {
        CShip* Ship = (CShip*)(Owner);
        if (Ship->is_using_tradelane())
            return FireTradelane;
    }
    return FireOk;
}

bool CELauncher::PowerNeedsMet() const
{
    // .text:0629a140..0629a171: Owner+0x198 - vt+0x9c GetPowerDrawPerFire() on the FPU stack,
    // FCOMP 0.0, TEST AH,0x1 / JNZ: false when less or unordered. No Owner test.
    double const Left = (double)(Owner->Power) - GetPowerDrawPerFire();
    return Left >= 0.0;
}

void CELauncher::DrawPower(int BarrelCount)
{
    // .text:0629a180..0629a1c5. Cost = FILD count * Arch+0x8c kept on the FPU stack; FCOM 0.0,
    // TEST AH,0x41 / JNZ returns when not greater (or unordered). Next = Power - Cost, still on
    // the stack; 0.0 FCOMP Next, TEST AH,0x41 / JNZ keeps it unless 0 is greater; FSTP float.
    // No Owner / Arch tests.
    double const Cost = (double)(BarrelCount) *
                        LauncherArch()->PowerUsage;
    if (!(Cost > 0.0))
        return;
    double Next = (double)(Owner->Power) - Cost;
    if (0.0 > Next)
        Next = 0.0;
    Owner->Power = (float)(Next);
}

void CELauncher::DrawAmmoFromCargo(int BarrelCount)
{
    // Listing 0x0629a0a0..0x0629a135. No null guards; the first cargo (traverser 0x10000) whose
    // archetype nickname id (+0xc -> +8) equals the projectile's (+0x4c -> +8) gives up the ammo.
    Archetype::Projectile* Proj = (Archetype::Projectile*)(Projectile);
    if (!Proj->bRequiresAmmo)   // Proj+0x78
        return;

    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* Equip = Owner->EquipManager.Traverse(Walk);
    while (Equip != nullptr)
    {
        if (Equip->Arch->Nickname.Id == Projectile->Nickname.Id)
        {
            ((CECargo*)Equip)->RemoveFromStack((unsigned)(BarrelCount));
            return;
        }
        Equip = Owner->EquipManager.Traverse(Walk);
    }
    FDUMP(0x100002,
          "%s(%d) : *** WARNING: CELauncher::DrawAmmoFromCargo(%d).  Ammo pack not found!",
          kCELauncherSource, 0x197, BarrelCount);
}

bool CELauncher::RefireDelayElapsed() const
{
    // .text:0629a1d0..0629a1eb: RefireElapsed FCOMP Arch+0x90, TEST AH,0x1 / JNZ: false when less
    // or unordered. No Arch test.
    return LauncherArch()->RefireDelay <= RefireElapsed;
}

void CELauncher::ComputeLaunchInfo_OneBarrel(ProjLaunchInfo& Out, int Barrel,
                                            Vector const& Target) const
{
    // .text:062996e0..06299873. Leaves LockTarget / LockSubId as the caller's default.
    // 062996e7..062996f1: Owner vt+0x54 get_velocity, no Owner test.
    Vector const OwnerVel = Owner->get_velocity();
    // 062996f8..06299764: muzzle = 0; unless destroyed, Csys = engine transform of the barrel
    // instance (Csys::operator=) and FUN_06299880 = 3DMathEngine+0x28 of the barrel position.
    Vector Muzzle;
    Muzzle.x = 0.0f;
    Muzzle.y = 0.0f;
    Muzzle.z = 0.0f;
    if (!IsDestroyed())
        Muzzle = BarrelPointWS(Barrels[Barrel].EngineIndex, Barrels[Barrel].Position);
    // 06299768..06299795: Delta = Vector(Target - Muzzle), each component FSTP'd to float.
    Vector Delta;
    Delta.x = Target.x - Muzzle.x;
    Delta.y = Target.y - Muzzle.y;
    Delta.z = Target.z - Muzzle.z;
    // 0629979d..062997db: Scale = MuzzleVelocity / sqrt((dy*dy + dz*dz) + dx*dx), kept on the
    // FPU stack. No zero-length guard.
    double const Scale = (double)(LauncherArch()->MuzzleVelocity) /
                         sqrt(((double)(Delta.y) * Delta.y +
                               (double)(Delta.z) * Delta.z) +
                              (double)(Delta.x) * Delta.x);
    // 062997dd..06299816: the muzzle-velocity vector, each component FSTP'd to float.
    Vector DirVel;
    DirVel.x = (float)(Delta.x * Scale);
    DirVel.y = (float)(Delta.y * Scale);
    DirVel.z = (float)(Delta.z * Scale);
    // 062997c1..062997d0: Out.Pos = muzzle.
    Out.Pos = Muzzle;
    // 06299818..06299852: Out.Velocity = owner velocity + muzzle velocity (float sums).
    Out.Velocity.x = OwnerVel.x + DirVel.x;
    Out.Velocity.y = DirVel.y + OwnerVel.y;
    Out.Velocity.z = DirVel.z + OwnerVel.z;
    // 06299845..0629986c: the orientation looks along the muzzle velocity alone, not along the
    // velocity that includes the owner's.
    Out.Orient = LookMatrix(DirVel);
}

void CELauncher::ComputeProjectilePosAndVelocity_OneBarrel(Vector& Pos, Vector& Vel, int Barrel,
                                                          Vector const& Target) const
{
    // .text:062998d0..062999fe.
    // 062998d9..06299966: muzzle = 0; unless destroyed (vt+8), the barrel instance's transform
    // (IEngine+0xa0, 12 dwords) applied to the barrel position by 3DMathEngine+0x28.
    Vector Muzzle;
    Muzzle.x = 0.0f;
    Muzzle.y = 0.0f;
    Muzzle.z = 0.0f;
    if (!IsDestroyed())
        Muzzle = BarrelPointWS(Barrels[Barrel].EngineIndex, Barrels[Barrel].Position);
    // 0629996f..0629998f: dx and dy stay on the FPU stack; dz is FSTP'd to float.
    double const Dx = (double)(Target.x) - Muzzle.x;
    double const Dy = (double)(Target.y) - Muzzle.y;
    float const Dz = (float)((double)(Target.z) - Muzzle.z);
    // 062999a5..062999b6: Pos = muzzle.
    Pos = Muzzle;
    // 06299993..062999c5: Scale = MuzzleVelocity / sqrt((dz*dz + dy*dy) + dx*dx), FSTP'd to
    // float. No zero-length guard.
    float const Scale = (float)(
        (double)(LauncherArch()->MuzzleVelocity) /
        sqrt(((double)(Dz) * Dz + Dy * Dy) + Dx * Dx));
    // 062999c9..062999f8: each component times the float scale, FSTP'd to float.
    Vel.x = (float)(Dx * Scale);
    Vel.y = (float)(Dy * Scale);
    Vel.z = (float)((double)(Dz) * Scale);
}

unsigned CELauncher::GetProjectilesPerFire() const
{
    return CountLauncherBarrels(*this);
}

CELauncher::Barrel const* CELauncher::GetBarrelInfo(unsigned Index) const
{
    // .text:06299a90..06299abb: null when destroyed or when the barrel is unbound; no index test.
    if (IsDestroyed())
        return nullptr;
    if (Barrels[Index].EngineIndex == -1)
        return nullptr;
    return &Barrels[Index];
}

Vector CELauncher::GetBarrelPosWS(unsigned Barrel) const
{
    // .text:06299ac0..06299b5e: zero when destroyed; otherwise the barrel position through the
    // barrel instance's engine transform. No index or -1 test.
    Vector Out;
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (!IsDestroyed())
        Out = BarrelPointWS(Barrels[Barrel].EngineIndex, Barrels[Barrel].Position);
    return Out;
}

Vector CELauncher::GetBarrelDirWS(unsigned Barrel) const
{
    // .text:06299b70..06299c36: zero when destroyed; otherwise -Z of (engine orientation of the
    // barrel instance * hardpoint orientation). No index or -1 test.
    Vector Out;
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (!IsDestroyed())
        Out = BarrelDirWS(Barrels[Barrel].EngineIndex, Barrels[Barrel].Orient);
    return Out;
}

Vector CELauncher::GetAvgBarrelPosWS() const
{
    // .text:06299c40..06299d92: GetBarrelPosWS inlined per counted barrel (IsDestroyed re-tested
    // each time), float sums Pos + Sum; then FILD count, FDIVR 1.0 -- the reciprocal stays on
    // the FPU stack -- and each sum times it FSTP'd to float. Not called inside Common.dll.
    Vector Sum;
    Sum.x = 0.0f;
    Sum.y = 0.0f;
    Sum.z = 0.0f;
    unsigned Count = CountLauncherBarrels(*this);
    for (unsigned Barrel = 0; Barrel < Count; ++Barrel)
    {
        Vector Pos = GetBarrelPosWS(Barrel);
        Sum.x = Pos.x + Sum.x;
        Sum.y = Pos.y + Sum.y;
        Sum.z = Pos.z + Sum.z;
    }
    double const Inv = 1.0 / (double)((int)(Count));
    Vector Out;
    Out.x = (float)(Sum.x * Inv);
    Out.y = (float)(Sum.y * Inv);
    Out.z = (float)(Sum.z * Inv);
    return Out;
}

Vector CELauncher::GetAvgBarrelDirWS() const
{
    // .text:06299da0..06299f5a: GetBarrelDirWS inlined per counted barrel, the same float sums
    // and the same unrounded reciprocal as GetAvgBarrelPosWS. Not called inside Common.dll.
    Vector Sum;
    Sum.x = 0.0f;
    Sum.y = 0.0f;
    Sum.z = 0.0f;
    unsigned Count = CountLauncherBarrels(*this);
    for (unsigned Barrel = 0; Barrel < Count; ++Barrel)
    {
        Vector Dir = GetBarrelDirWS(Barrel);
        Sum.x = Dir.x + Sum.x;
        Sum.y = Dir.y + Sum.y;
        Sum.z = Dir.z + Sum.z;
    }
    double const Inv = 1.0 / (double)((int)(Count));
    Vector Out;
    Out.x = (float)(Sum.x * Inv);
    Out.y = (float)(Sum.y * Inv);
    Out.z = (float)(Sum.z * Inv);
    return Out;
}

Archetype::Launcher const* CELauncher::LauncherArch() const
{
    return (Archetype::Launcher const*)(Arch);
}

Archetype::Projectile const* CELauncher::ProjectileArch() const
{
    // Listing 0x369c0 (ICF-shared): MOV EAX,[ECX+0x4c]; RET.
    return (Archetype::Projectile const*)(Projectile);
}
