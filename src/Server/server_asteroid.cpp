// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_asteroid.cpp -- SrvAsteroid: MedAsteroid, MedMine and SrvAsteroidSystem.
// Split out of server_classes.cpp unchanged; the declarations the split files share are in
// server_classes_internal.h.

#include "server_pub.h"
#include "common_net.h"
#include "common_physys.h"
#include "ivp_physics/ivp_real_object.h" // IVP_Real_Object +0x1c/+0xa4 (StarSystem_MpCollisionFilter)
#include "common_cequip.h" // TractorArm
#include "common_casteroid.h" // CmnAsteroid::CAsteroidSystem (SrvAsteroidSystem AddRef/Release/get_sys_id)
#include "common_id.h"
#include "common_server_playerdb.h"
#include "server_savegame.h" // PlayerData_CreateNewCharacter (retail FUN_06d4b790)
#include "server_savefile.h" // Server_AccountDirName / Server_GetAccountsRoot (FUN_06d49f60)
#include "common_ini.h"
#include "common_misc.h"
#include "x86math.h" // I3DMathEngine
#include "common_universe.h"
#include "common_reputation.h"
#include "server_sysdb.h"
#include "server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "common_cobjdb.h"  // IObjDB::GetZoneDb (SrvAsteroid_UpdateSystem, FUN_06d03480)
#include "DALib.h"          // DALib::Data (SrvAsteroid's GetFileSysPath calls)
#include "readfile.h"       // IFileSystem::GetFileSysPath (+0x84), create_restart_file
#include "server_createinfo.h"
#include "server_characterset.h" // CCharacterSet (FUN_06d499c0)
#include "common_goods.h"    // AmbientScriptProperties (server_basedb.h uses it by value)
#include "server_basedb.h"
#include "rpc_iclient.h"
#include "rpc_iclient_args.h" // FServerSetReputationInfo / FPayloadServerActivateObject (add_player MP walk)
#include "md5hash.h"      // MD5HASH_API: Server exports its implicit members like retail
#include <windows.h>
#include <mmsystem.h>
#include "../include/platform.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <new>          // placement new: Observer is constructed into a raw
                        // player-list node, as std::list's _Construct does
#include <io.h>
#include <direct.h>
#include <wchar.h>
#include <stdarg.h>
#include <algorithm>
#include <unordered_map> // PORT(perf-index): the ObjectList blocks' id index (find_by_id)
#include <cassert>       // PORT(perf-index): the id index's _DEBUG cross-check
#include "server_classes_internal.h"

// ===========================================================================
// SrvAsteroid -- the server's per-system asteroid bookkeeping. Retail source
// file "E:\FL\Scratch\Source\Server\Space\SrvAsteroid.cpp" (.rdata:06d6777c,
// the __FILE__ of the one FDUMP below).
//
// Three levels, all owned by the Server.dll-static map DAT_06d8d980:
//   std::map<unsigned int, SrvAsteroidSystem>   one per loaded system
//     SrvAsteroidSystem::m_Fields               one FSrvAsteroidField per
//                                               CmnAsteroid::CAsteroidField
//       FSrvAsteroidField::Cubes                one FSrvAsteroidCube per cube
//                                               that reported changes
//         FSrvAsteroidCube::Objects             the server object standing in
//                                               for each live asteroid/mine
//
// The containers are std:: containers, not VC6 layouts (AGENTS.md "No retail
// ABI"). Every member keeps its retail offset in a comment.
//
// The server objects themselves are MedAsteroid (ctor FUN_06d04ba0, vtable
// .rdata:06d67894) and MedMine (ctor FUN_06d04c70, vtable .rdata:06d67aec),
// declared in server_gameobject.h and defined below; StarSystem makes them
// (create_med_asteroid / create_med_mine, FUN_06d09bb0 / FUN_06d09c80).
// ===========================================================================

namespace SrvAsteroid {

namespace {

// .rdata:06d6777c -- the __FILE__ of SrvAsteroidSystem::load's FDUMP.
char const* const SrvAsteroidSource = "E:\\FL\\Scratch\\Source\\Server\\Space\\SrvAsteroid.cpp";

// FUN_06d03cf0 (.text:06d03cf0, cdecl (Dst, Size, Src)); the same body is
// CopyBoundedString in src/Server/server_savegame.cpp. SrvAsteroidSystem::load
// inlines the null arm itself (.text:06d039bb / .text:06d039f7), so only the
// non-null arm is called: length clamped to Size - 1, copied, terminated,
// returned.
unsigned int SrvAsteroid_CopyBounded(char* Dst, unsigned int Size, char const* Src) {
    unsigned int Length = static_cast<unsigned int>(strlen(Src));
    if (!(Length < Size))
        Length = Size - 1;
    memcpy(Dst, Src, Length);
    Dst[Length] = '\0';
    return Length;
}


} // namespace

// ---------------------------------------------------------------------------
// MedAsteroid / MedMine. Declarations and the vtable dump in
// src/include/server_gameobject.h.
// ---------------------------------------------------------------------------

// FUN_06d04ba0 (.text:06d04ba0 .. 06d04bc7): FUN_06cee360 (GameObject), then
//   .text:06d04baa  MOV [ESI+0x64],EAX   ; 0
//   .text:06d04bad  MOV [ESI+0x60],AL    ; 0
// and the three vtables (.rdata:06d67894 / 06d67814 / 06d67808).
MedAsteroid::MedAsteroid()
    : Field_0x60(0)
    , Field_0x64(nullptr)
{
}

// FUN_06d04c00 (.text:06d04c00, ECX = the CBase sub-object, so [ESI+0x58] is
// +0x64): the vtables restored, `if ([+0x64] != 0) operator delete([+0x64])`
// (.text:06d04c06 .. 06d04c1f), then `JMP FUN_06cee4a0` -- GameObject's
// destructor, which the compiler runs after this body. The scalar deleting
// destructor FUN_06d04bd0 frees this - 0xc (the object start) when bit 0 of its
// flag is set: plain `delete` of the most derived object.
MedAsteroid::~MedAsteroid()
{
    if (Field_0x64 != nullptr)
        operator delete(Field_0x64);
}

// LAB_06d02cb0 (.text:06d02cb0): `XOR AL,AL / RET 4`. The same body sits in 21
// slots across Server.dll's wrappers (create of MedAsteroid/MedMine, find of
// CounterMeasure/Mine, and more); here it is create().
bool MedAsteroid::create(Archetype::Root* Arch)
{
    (void)Arch;
    return false;
}

// LAB_06d04c30 (.text:06d04c30, RET 4), shared by MedAsteroid and MedMine:
//   .text:06d04c35  PUSH 0x1003 / PUSH Id / CALL CObject::Find
//   .text:06d04c48  MOV [ESI+0x10],EAX          ; CBase::Object
//   .text:06d04c4b  JZ -> skip the advise
//   .text:06d04c52  CBase::advise(1)
//   .text:06d04c58 .. 06d04c5f  return [ESI+0x10] != 0
// Unlike Ship::find (GameObject::FindOfClass), advise runs only on a hit.
bool MedAsteroid::find(unsigned int const& Id)
{
    Object = CObject::Find(Id, CObject::ClassAsteroid);
    if (Object != nullptr)
        advise(true);
    return Object != nullptr;
}

// .text:06d28880 `RET 8` (the listing's IServerImpl::Connect label -- ICF).
void MedAsteroid::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 `RET 8`.
void MedAsteroid::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 `RET 8`.
void MedAsteroid::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    (void)Blast;
    (void)Damage;
}

// .text:06ce1050 `RET 4` (the listing's CAccount::AppendCharacterNames label).
// MedMine does NOT override this slot; it keeps GameObject's FUN_06ceee70.
void MedAsteroid::apply_damage_list(DamageList& Damage)
{
    (void)Damage;
}

// FUN_06d04c70 (.text:06d04c70 .. 06d04cab): FUN_06cee360 (GameObject), then
//   .text:06d04c78 .. 06d04c83  +0x80 = +0x7c = 0x3f800000 (1.0f)
//   .text:06d04c86 .. 06d04c9a  vtables .rdata:06d67aec / 06d67a6c / 06d67a60,
//                               and the CObjectCB vtable .rdata:06d67a54 at +0x60
//   .text:06d04ca1  +0x78 = 0
// +0x64, +0x68, +0x6c..+0x74 and +0x84 are not touched (arm() writes them);
// the port zeroes them rather than leave indeterminate values in its members.
MedMine::MedMine()
    : Phantom(nullptr)
    , IntruderCount(0)
    , Explosion(nullptr)
    , RechargeTimer(1.0f)
    , RechargeTime(1.0f)
    , ExplosionOffset(0.0f)
{
    Direction.x = 0.0f;
    Direction.y = 0.0f;
    Direction.z = 0.0f;
}

// FUN_06d04ce0 (.text:06d04ce0, ECX = the CBase sub-object, so [ESI+0x58] is
// +0x64): vtables restored (the CObjectCB one included, .text:06d04d11), then
//   .text:06d04d18 .. 06d04d24  PhySys::DestroyPhantom([+0x64]) -- no null test
//   .text:06d04d2f              [+0x64] = 0
//   .text:06d04d3e              FUN_06cee4a0 (GameObject's destructor)
// FUN_06d04cb0 is the scalar deleting destructor (frees this - 0xc).
MedMine::~MedMine()
{
    PhySys::DestroyPhantom(Phantom);
    Phantom = nullptr;
}

// LAB_06d02cb0 -- see MedAsteroid::create.
bool MedMine::create(Archetype::Root* Arch)
{
    (void)Arch;
    return false;
}

// LAB_06d04c30 -- see MedAsteroid::find (the same body, ClassAsteroid 0x1003).
bool MedMine::find(unsigned int const& Id)
{
    Object = CObject::Find(Id, CObject::ClassAsteroid);
    if (Object != nullptr)
        advise(true);
    return Object != nullptr;
}

// .text:06d28880 `RET 8`.
void MedMine::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 `RET 8`.
void MedMine::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 `RET 8`.
void MedMine::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    (void)Blast;
    (void)Damage;
}

// .text:06d28880 `RET 8` -- MedMine's own +0x1bc (.rdata:06d67ca8): nothing.
void MedMine::Slot1bc(unsigned int Arg1, unsigned int Arg2)
{
    (void)Arg1;
    (void)Arg2;
}

// FUN_06d04d60 (.text:06d04d60 .. 06d04e43, RET 8). Arguments (radius, system
// id): .text:06d04dd4 pushes [entry+4] as CreatePhantom's float, .text:06d04da1
// stores [entry+8] into the parms' first dword. PhantomParms is built on the
// stack at S+4:
//   .text:06d04d6a  +0x68 (IntruderCount) = 0
//   .text:06d04d6d .. 06d04d9d  bytes +4/+5/+6 = 0; +0x10..+0x18 and the
//                   off-diagonal +0x20..+0x28/+0x30..+0x38 = 0; +0x40 = 0
//   .text:06d04da6 .. 06d04db6  1.0f at +0x1c/+0x2c/+0x3c (identity basis)
//   .text:06d04dbe  +0x00 = SystemId
//   .text:06d04dc2 .. 06d04ddc  +0x10 = vt[0]() (get_position, three dwords)
//   .text:06d04de9  +0x40 = this + 0x60 (the CObjectCB sub-object)
//   .text:06d04ded  PhySys::CreatePhantom(parms, radius, &[+0x64])
//   .text:06d04dfa  PhySys::AttachPhantom([+0x10], [+0x64])   ; raw CBase::Object
//   .text:06d04e05  PhySys::SetCollisionStatePhantom([+0x64], true)
//   .text:06d04e11  arch = CObject::get_archetype([+0x10])
//   .text:06d04e1d  +0x78 = Archetype::GetExplosion(arch + 0x24)
//   .text:06d04e26  +0x84 = arch +0x60 (ExplosionOffset)
//   .text:06d04e2f .. 06d04e3c  +0x80 = +0x7c = arch +0x64 (RechargeTime)
// Name[1..9] of the parms is never written (retail leaves stack garbage).
void MedMine::arm(float DetectRadius, unsigned int SystemId)
{
    IntruderCount = 0;

    PhySys::PhantomParms Parms;
    Parms.Flag04 = 0;
    Parms.Flag05 = 0;
    Parms.Name[0] = '\0';
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.SystemId = SystemId;
    Parms.Position = get_position();
    Parms.Listener = static_cast<PhySys::CObjectCB*>(this);

    PhySys::CreatePhantom(Parms, DetectRadius, Phantom);
    PhySys::AttachPhantom(Object, Phantom);
    PhySys::SetCollisionStatePhantom(Phantom, true);

    Archetype::Asteroid const* const Arch =
        static_cast<Archetype::Asteroid const*>(Object->get_archetype());
    Explosion = Archetype::GetExplosion(Arch->ExplosionArch);
    ExplosionOffset = Arch->ExplosionOffset;
    RechargeTime = Arch->RechargeTime;
    RechargeTimer = Arch->RechargeTime;
}

// LAB_06d04e50 (.text:06d04e50 .. 06d04fa0, RET 4) -- MedMine's update (+0x16c).
//   .text:06d04e56 .. 06d04e65  FCOMP timer (+0x7c) vs limit (+0x80),
//                   `TEST AH,0x5 / JP` -- the add runs only for timer < limit
//   .text:06d04e67 .. 06d04e6e  timer = Dt + timer
//   .text:06d04e71 .. 06d04e78  count (+0x68) == 0 -> done (JBE, unsigned)
//   .text:06d04e7e .. 06d04e8c  `TEST AH,0x1 / JNZ` -- timer < limit -> done
//   .text:06d04e92 .. 06d04ee7  the blast record at S+0x24: +0 = +4 = get_id()
//                   (vt +0x20, twice), +8 = 7 (after a first store of 4),
//                   +0xc = vt[0]() (position), +0x18 = [+0x78], +0x1c = 0
//   .text:06d04eda .. 06d04f2b  Explosion != 0 && Radius > 0.0001 (double
//                   DAT_06d65190, `TEST AH,0x41 / JNZ` = not greater) && (Impulse
//                   > 0.0001 || HullDamage > 0.0001 || EnergyDamage > 0.0001)
//   .text:06d04f35  StarSystem::explode(OwnerSystem (+0x28), record)
//   .text:06d04f3a .. 06d04f80  {get_id(), ExplosionOffset (+0x84) * Direction
//                   (+0x6c/+0x70/+0x74)}, handed to
//   .text:06d04f84  StarSystem::send_mine_explosion(OwnerSystem, record)
//   .text:06d04f89  timer = 0
//   .text:06d04f8c .. 06d04f9c  return !vt +0x160 (is_destroyed)
// The explosion test failing skips only the explode call; the packet and the
// timer reset still happen.
bool MedMine::update(float Dt)
{
    if (RechargeTimer < RechargeTime)
        RechargeTimer = Dt + RechargeTimer;

    if (IntruderCount != 0 && RechargeTimer >= RechargeTime) {
        ExplosionDamageEvent Blast;
        Blast.ExplodingObjId = get_id();
        Blast.InflictorId = get_id();
        // 7 is not one of common_misc.h's (unsourced) DamageCause names; it is
        // the literal at .text:06d04ec4.
        Blast.Cause = static_cast<DamageCause>(7);
        Blast.Position = get_position();
        Blast.Explosion = Explosion;
        Blast.SkipCollisionGroup = nullptr;
        if (Explosion != nullptr && Explosion->Radius > 0.0001 &&
            (Explosion->Impulse > 0.0001 || Explosion->HullDamage > 0.0001 ||
             Explosion->EnergyDamage > 0.0001))
            OwnerSystem->explode(Blast);

        FMineExplosion Rec;
        Rec.Offset.x = ExplosionOffset * Direction.x;
        Rec.Offset.y = ExplosionOffset * Direction.y;
        Rec.Offset.z = ExplosionOffset * Direction.z;
        Rec.MineId = get_id();
        OwnerSystem->send_mine_explosion(Rec);
        RechargeTimer = 0.0f;
    }
    return !is_destroyed();
}

// LAB_06d04fb0 (.text:06d04fb0 .. 06d05060, RET 4) -- CObjectCB slot 0, ECX =
// this + 0x60.
//   .text:06d04fbc .. 06d04fda  GetRoot(Obj); null or (class & 0x503) != 0x503
//                               -> nothing
//   .text:06d04fdc .. 06d05017  Direction = Obj's own WorldPosition (+0x2c;
//                   the ARGUMENT, not its root) minus vt[0]() of the mine
//                   ([ESI-0x60]), stored to +0x6c..+0x74
//   .text:06d0501a .. 06d05032  length = sqrt((x*x + y*y) + z*z), read back
//                               from the stored floats
//   .text:06d0503a  FDIVR DAT_06d65520 (1.0f) -- no zero test
//   .text:06d05040 .. 06d05051  each component times 1/length
//   .text:06d05054 .. 06d05058  ++[+0x68]
void MedMine::added(CObject* Obj)
{
    CObject* const Root = GetRoot(Obj);
    if (Root == nullptr)
        return;
    if ((static_cast<unsigned int>(Root->ObjectClass) & 0x503u) != 0x503u)
        return;

    Vector const& MinePos = get_position();
    Direction.x = Obj->WorldPosition.x - MinePos.x;
    Direction.y = Obj->WorldPosition.y - MinePos.y;
    Direction.z = Obj->WorldPosition.z - MinePos.z;

    float const Length = sqrtf((Direction.x * Direction.x + Direction.y * Direction.y) +
                               Direction.z * Direction.z);
    float const Inverse = 1.0f / Length;
    Direction.x = Inverse * Direction.x;
    Direction.y = Inverse * Direction.y;
    Direction.z = Inverse * Direction.z;

    ++IntruderCount;
}

// LAB_06d05070 (.text:06d05070 .. 06d0509a, RET 4) -- CObjectCB slot 1:
// GetRoot(Obj); a ship ((class & 0x503) == 0x503) -> --[+0x68] (.text:06d05096).
void MedMine::deleted(CObject* Obj)
{
    CObject* const Root = GetRoot(Obj);
    if (Root == nullptr)
        return;
    if ((static_cast<unsigned int>(Root->ObjectClass) & 0x503u) != 0x503u)
        return;
    --IntruderCount;
}

// ---------------------------------------------------------------------------
// The value of a field entry's cube map: node+0x10, 0x24 bytes. Offsets from
// FUN_06d04480's temporary (.text:06d044f3 .. 06d04503: +0x00/+0x04/+0x08 = 0,
// +0x0c = 0xfa) and its post-insert stores (.text:06d045cf .. 06d045e7).
// ---------------------------------------------------------------------------
struct FSrvAsteroidCube {
    CmnAsteroid::CAsteroidCube* Cube;              // +0x00
    unsigned int SystemId;                         // +0x04 (FUN_06d04ab0 reads it for SysMap)
    unsigned long CubeId;                          // +0x08
    int CubeSize;                                  // +0x0c
    // +0x10..+0x20: asteroid_marker::AsteroidId -> the server object (node+0x10).
    // Size at +0x20 is what FUN_06d04480's prune pass tests (node+0x30).
    std::map<unsigned long, GameObject*> Objects;

    FSrvAsteroidCube()
        : Cube(nullptr)
        , SystemId(0)
        , CubeId(0)
        , CubeSize(0xfa) {
    }

    // FUN_06d048f0 (on the value) and FUN_06d046c0 (on the pair, value at +4):
    // every object's vtable +0x15c -- GameObject::destroy_notify -- in map
    // order (.text:06d04579 in the inlined copy), then the tree is freed.
    ~FSrvAsteroidCube() {
        for (std::map<unsigned long, GameObject*>::iterator It = Objects.begin(); It != Objects.end(); ++It)
            It->second->destroy_notify();
    }

    void apply_marker(CmnAsteroid::asteroid_marker const* Marker);
};

// FUN_06d04ab0 (.text:06d04ab0, thiscall, RET 4). Only caller FUN_06d04480
// (.text:06d045ed).
//   .text:06d04acb  find Marker->AsteroidId (+0x04) in Objects
//   .text:06d04adc  bCubeActive (+0x0c) == 0:
//     .text:06d04b77  found -> object vtable +0x15c, erase the node (FUN_06d58e50)
//   .text:06d04ae4  active and found -> object vtable[0] (.text:06d04b69),
//                   IObjInspectImpl::get_position through the import thunk
//                   .text:06d5fef4; the result is not used
//   active and missing:
//     .text:06d04af4  SysDB::SysMap.find(SystemId) (+0x04), no end check
//     .text:06d04b09  bMine (+0x0d) ? FUN_06d09c80 (StarSystem: new MedMine,
//                     push onto the +0xc0 block, find(ObjId)) followed by
//                     FUN_06d04d60(mine, archetype +0x5c, SystemId)
//                   : FUN_06d09bb0 (new MedAsteroid, likewise) with ObjId (+0x08)
//       .text:06d04b16  the mine's radius: vt +0x150 (cobject(), no class
//                       narrowing) -> CObject::get_archetype -> FLD [+0x5c]
//       .text:06d04b27  MOV ECX,[EDI] -- SystemId (+0x04) is the second argument
//     .text:06d04b55  Objects.insert({AsteroidId, object})
void FSrvAsteroidCube::apply_marker(CmnAsteroid::asteroid_marker const* Marker) {
    std::map<unsigned long, GameObject*>::iterator It = Objects.find(Marker->AsteroidId);
    if (!Marker->bCubeActive) {
        if (It != Objects.end()) {
            It->second->destroy_notify();
            Objects.erase(It);
        }
        return;
    }
    if (It == Objects.end()) {
        StarSystem& System = SysDB::SysMap.find(SystemId)->second;
        GameObject* Obj = nullptr;
        if (Marker->bMine) {
            MedMine* const Mine = System.create_med_mine(Marker->ObjId);
            Archetype::Asteroid const* const Arch =
                static_cast<Archetype::Asteroid const*>(Mine->cobject()->get_archetype());
            Mine->arm(Arch->DetectRadius, SystemId);
            Obj = Mine;
        } else {
            Obj = System.create_med_asteroid(Marker->ObjId);
        }
        Objects.insert(std::map<unsigned long, GameObject*>::value_type(Marker->AsteroidId, Obj));
        return;
    }
    It->second->get_position();
}

// ---------------------------------------------------------------------------
// A field entry: operator_new(0x20). Constructed in place at .text:06d03a74 ..
// 06d03a7e (load), in map_asteroid_fields and inside FUN_06d03500: map
// initialised, +0x00 = 0, +0x1c = 0, +0x18 = 0xfa.
// ---------------------------------------------------------------------------
struct FSrvAsteroidField {
    CmnAsteroid::CAsteroidField* Field;                   // +0x00
    std::map<unsigned long, FSrvAsteroidCube> Cubes;      // +0x04..+0x14, keyed by CubeId
    int CubeSize;                                         // +0x18
    unsigned int SystemId;                                // +0x1c

    FSrvAsteroidField()
        : Field(nullptr)
        , CubeSize(0xfa)
        , SystemId(0) {
    }

    // FUN_06d03fc0, reached through the scalar deleting destructor
    // FUN_06d038a0: CAsteroidField::Release on +0x00 when set, then the cube
    // map is torn down (each value's FUN_06d046c0) -- here the member's own
    // destructor, which runs after this body as in retail.
    ~FSrvAsteroidField() {
        if (Field != nullptr)
            Field->Release();
    }

    void set_field(CmnAsteroid::CAsteroidField* InField);
    void load(char const* Path);
    void update();
};

// FUN_06d04810 (.text:06d04810, thiscall, RET 4): store; when non-null AddRef
// and CubeSize = field+0x44. The previous field is not released.
void FSrvAsteroidField::set_field(CmnAsteroid::CAsteroidField* InField) {
    Field = InField;
    if (InField != nullptr) {
        InField->AddRef();
        CubeSize = Field->CubeSize;
    }
}

// FUN_06d04140 (.text:06d04140, thiscall, one argument: the [Asteroids]
// "file" value). Only caller SrvAsteroidSystem::load (.text:06d03b24).
//   .text:06d0416a/6e  FillDist = EmptyFreq = 0 (both ints)
//   .text:06d04197     GetFileSysPath(path, Path, DALib::Data); open(path, false)
//   .text:06d041c7     get_state -- the start of the file, for the second pass
//   every [Field]: cube_size -> this+0x18, fill_dist, empty_cube_frequency,
//                  each ftol(get_value_float(0))
//   .text:06d04294     when Field is set: set_state, CAsteroidField::load(reader,
//                      CubeSize), then field+0x48 = FillDist and
//                      field+0x4c = (float)EmptyFreq (FILD .text:06d042bb)
void FSrvAsteroidField::load(char const* Path) {
    int FillDist = 0;
    int EmptyFreq = 0;
    INI_Reader Reader;
    char FullPath[0x100];
    GetFileSysPath(FullPath, Path, DALib::Data);
    if (!Reader.open(FullPath, false))
        return;

    INI_Reader::State Start;
    Reader.get_state(Start);
    while (Reader.read_header()) {
        if (!Reader.is_header("Field"))
            continue;
        while (Reader.read_value()) {
            // .text:06d04224 / 06d04244 / 06d04265 -- MSVCRT ftol straight on
            // the unrounded ST0 (64-bit truncation, low dword stored).
            if (Reader.is_value("cube_size"))
                CubeSize = (int)(long long)Reader.get_value_float(0);
            else if (Reader.is_value("fill_dist"))
                FillDist = (int)(long long)Reader.get_value_float(0);
            else if (Reader.is_value("empty_cube_frequency"))
                EmptyFreq = (int)(long long)Reader.get_value_float(0);
        }
    }

    if (Field != nullptr) {
        Reader.set_state(Start);
        Field->load(Reader, CubeSize);
        Field->FillDist = FillDist;
        Field->EmptyFreq = static_cast<float>(EmptyFreq);
    }
}

// FUN_06d04480 (.text:06d04480, thiscall on the entry). Callers
// SrvAsteroidSystem::update (.text:06d03d40) and FUN_06d03480.
//   .text:06d044a3  markers = Field->get_changes(); for each (Next at +0x10):
//     .text:06d044c8  Cubes.find(marker->CubeId)
//     missing: insert {CubeId, FSrvAsteroidCube()} (FUN_06d05690), then
//       .text:06d045c3  cube = Field->find_cube(marker->CubeId); when non-null
//                       record = {cube, SystemId, cube->get_id(), CubeSize}
//     FSrvAsteroidCube::apply_marker(marker)            (.text:06d045ed)
//   .text:06d04602 .. 06d04699  erase every record whose Objects is empty
void FSrvAsteroidField::update() {
    CmnAsteroid::asteroid_marker* Marker = Field->get_changes();
    while (Marker != nullptr) {
        FSrvAsteroidCube* Record;
        std::map<unsigned long, FSrvAsteroidCube>::iterator It = Cubes.find(Marker->CubeId);
        if (It == Cubes.end()) {
            It = Cubes.insert(std::make_pair(Marker->CubeId, FSrvAsteroidCube())).first;
            Record = &It->second;
            CmnAsteroid::CAsteroidCube* Cube = Field->find_cube(Marker->CubeId);
            if (Cube != nullptr) {
                Record->Cube = Cube;
                Record->CubeId = Cube->get_id();
                Record->CubeSize = CubeSize;
                Record->SystemId = SystemId;
            }
        } else {
            Record = &It->second;
        }
        Record->apply_marker(Marker);
        Marker = Marker->Next;
    }

    std::map<unsigned long, FSrvAsteroidCube>::iterator It = Cubes.begin();
    while (It != Cubes.end()) {
        if (It->second.Objects.size() == 0)
            It = Cubes.erase(It);
        else
            ++It;
    }
}

// Retail .text:06d03710 (ordinal 17): +0x00 CmnSystem = 0, +0x04/+0x08/+0x0c the
// empty field-entry list, +0x10 SystemId = 0, +0x14 RefCount = 0.
SrvAsteroidSystem::SrvAsteroidSystem()
    : m_CmnSystem(nullptr)
    , m_SystemId(0)
    , m_RefCount(0) {
}

// Retail .text:06ce30d0 (ordinal 16): a member-wise copy. CmnSystem without an
// AddRef, the list element by element (the entry pointers, shallow), SystemId
// and RefCount as they are.
SrvAsteroidSystem::SrvAsteroidSystem(const SrvAsteroidSystem& Other)
    : m_CmnSystem(Other.m_CmnSystem)
    , m_Fields(Other.m_Fields)
    , m_SystemId(Other.m_SystemId)
    , m_RefCount(Other.m_RefCount) {
}

// Retail .text:06d03740 (ordinal 22): CAsteroidSystem::Release when set; every
// non-null field entry through its destructor (FUN_06d03fc0, inlined) and
// freed; then the list nodes and the head.
SrvAsteroidSystem::~SrvAsteroidSystem() {
    if (m_CmnSystem != nullptr)
        m_CmnSystem->Release();
    for (std::list<FSrvAsteroidField*>::iterator It = m_Fields.begin(); It != m_Fields.end(); ++It) {
        if (*It != nullptr)
            delete *It;
    }
    m_Fields.clear();
}

// Retail .text:06ce3170 (ordinal 32): CmnSystem copied without AddRef (before
// the self check, which guards only the list assign), the list assigned, then
// SystemId and RefCount copied.
SrvAsteroidSystem& SrvAsteroidSystem::operator=(const SrvAsteroidSystem& Other) {
    m_CmnSystem = Other.m_CmnSystem;
    m_Fields = Other.m_Fields;
    m_SystemId = Other.m_SystemId;
    m_RefCount = Other.m_RefCount;
    return *this;
}

// Retail .text:06d038c0 (ordinal 498).
//   .text:06d03917  GetFileSysPath(path, Path, DALib::Data); open(path, false)
//   every [Asteroids]: "file" -> char[0x100], "zone" -> char[0x40]; a null
//     value string zeroes the length and the first byte (.text:06d039bb /
//     .text:06d039f7), otherwise FUN_06d03cf0
//   both lengths non-zero (.text:06d03a2a / 06d03a34):
//     .text:06d03a42  field = CmnAsteroid::CreateAsteroidField(SystemId)
//     .text:06d03a4c  new entry, push_back (FUN_06d05d90); entry->Field = field;
//                     non-null -> AddRef, CubeSize = field+0x44; SystemId
//     .text:06d03ad8  zone = Universe::get_zone(SystemId, zone name); null ->
//                     FDUMP (SrvAsteroid.cpp line 0xa1)
//     .text:06d03b11  zone vtable +0x18, result unused
//     .text:06d03b17  CAsteroidField::set_zone(field, zone)
//     .text:06d03b24  FUN_06d04140(entry, file)
void SrvAsteroidSystem::load(const char* Path) {
    INI_Reader Reader;
    char FullPath[0x100];
    GetFileSysPath(FullPath, Path, DALib::Data);
    if (!Reader.open(FullPath, false))
        return;

    while (Reader.read_header()) {
        if (!Reader.is_header("Asteroids"))
            continue;

        unsigned int FileLen = 0;
        char FileName[0x100];
        FileName[0] = '\0';
        unsigned int ZoneLen = 0;
        char ZoneName[0x40];
        ZoneName[0] = '\0';

        while (Reader.read_value()) {
            if (Reader.is_value("file")) {
                char const* Value = Reader.get_value_string();
                if (Value == nullptr) {
                    FileLen = 0;
                    FileName[0] = '\0';
                } else {
                    FileLen = SrvAsteroid_CopyBounded(FileName, 0x100, Value);
                }
            } else if (Reader.is_value("zone")) {
                char const* Value = Reader.get_value_string();
                if (Value == nullptr) {
                    ZoneLen = 0;
                    ZoneName[0] = '\0';
                } else {
                    ZoneLen = SrvAsteroid_CopyBounded(ZoneName, 0x40, Value);
                }
            }
        }

        if (FileLen == 0 || ZoneLen == 0)
            continue;

        CmnAsteroid::CAsteroidField* Field = CmnAsteroid::CreateAsteroidField(m_SystemId);
        m_Fields.push_back(new FSrvAsteroidField());
        FSrvAsteroidField* Entry = m_Fields.back();
        Entry->Field = Field;
        if (Field != nullptr) {
            Field->AddRef();
            Entry->CubeSize = Entry->Field->CubeSize;
        }
        Entry->SystemId = m_SystemId;

        Universe::IZone const* Zone = Universe::get_zone(m_SystemId, ZoneName);
        if (Zone == nullptr)
            FDUMP(0x100001, "%s(%d) : *** ERROR: AsteroidField linked to missing Zone (%s)",
                  SrvAsteroidSource, 0xa1, ZoneName);
        // .text:06d03b11: zone vtable +0x18 -- the shape id (common_universe.h,
        // EZoneShape). Retail discards the result; a null zone faults here.
        static_cast<void>(Zone->Shape);
        Field->set_zone(Zone);
        Entry->load(FileName);
    }
}

// Retail .text:06d03d40 (ordinal 520): FUN_06d04480 on every field entry.
void SrvAsteroidSystem::update() {
    for (std::list<FSrvAsteroidField*>::iterator It = m_Fields.begin(); It != m_Fields.end(); ++It)
        (*It)->update();
}

// Retail .text:06d03d70 (ordinal 514).
void SrvAsteroidSystem::set_sys_id(unsigned int SystemId) {
    m_SystemId = SystemId;
}

// Retail .text:06d03d80 (ordinal 511): store the pointer; null -> SystemId = 0;
// otherwise CAsteroidSystem::AddRef(Sys) and SystemId = Sys->SystemId (+0x04).
// The previous pointer is NOT released.
void SrvAsteroidSystem::set_cmn_system(CmnAsteroid::CAsteroidSystem* Sys) {
    m_CmnSystem = Sys;
    if (Sys == nullptr) {
        m_SystemId = 0;
        return;
    }
    Sys->AddRef();
    m_SystemId = m_CmnSystem->get_sys_id();
}

// Retail .text:06d03db0 (ordinal 504). No null check on CmnSystem. For every
// CAsteroidField from FindFirst/FindNext: AddRef, a new entry pushed back,
// the entry's FUN_06d04810 inlined (Field, AddRef, CubeSize = field+0x44),
// entry SystemId = this SystemId, Release.
void SrvAsteroidSystem::map_asteroid_fields() {
    CmnAsteroid::CAsteroidField* Field = m_CmnSystem->FindFirst();
    while (Field != nullptr) {
        Field->AddRef();
        m_Fields.push_back(new FSrvAsteroidField());
        FSrvAsteroidField* Entry = m_Fields.back();
        Entry->set_field(Field);
        Entry->SystemId = m_SystemId;
        Field->Release();
        Field = m_CmnSystem->FindNext();
    }
}

// Retail .text:06d03ed0 (ordinal 54).
int SrvAsteroidSystem::AddRef() {
    return ++m_RefCount;
}

// Retail .text:06d03ee0 (ordinal 358).
int SrvAsteroidSystem::Release() {
    return --m_RefCount;
}

// FUN_06d032b0 (.text:06d032b0).
//   .text:06d032d7  find(SystemId); found -> ++RefCount (node+0x24), return 2
//   .text:06d0336d  insert {SystemId, SrvAsteroidSystem()} (FUN_06d05de0); the
//                   temporary is destroyed (.text:06d0337a ..)
//   .text:06d033dd  ++RefCount; .text:06d033e6 SystemId (node+0x20) = SystemId
//   .text:06d033e9  load(Path); return 1
int SrvAsteroid_LoadSystem(unsigned int SystemId, const char* Path) {
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It != GSrvAsteroidSystems.end()) {
        ++It->second.m_RefCount;
        return 2;
    }
    It = GSrvAsteroidSystems.insert(std::make_pair(SystemId, SrvAsteroidSystem())).first;
    ++It->second.m_RefCount;
    It->second.m_SystemId = SystemId;
    It->second.load(Path);
    return 1;
}

// FUN_06d03410 (.text:06d03410): lower_bound + key check (= find); missing ->
// 0; --RefCount (node+0x24) non-zero -> 2; otherwise erase (FUN_06ce50d0,
// which runs ~SrvAsteroidSystem) -> 1.
int SrvAsteroid_UnloadSystem(unsigned int SystemId) {
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It == GSrvAsteroidSystems.end())
        return 0;
    if (--It->second.m_RefCount != 0)
        return 2;
    GSrvAsteroidSystems.erase(It);
    return 1;
}

// FUN_06d03480 (.text:06d03480, cdecl). The argument is the StarSystem as an
// IObjDB (.text:06d0c13c pushes EBX, the same `this` CmnAsteroid::Update got):
//   .text:06d0348b  Db->GetZoneDb() (vtable +0x08) -> the Universe::ISystem
//   .text:06d03492  its vtable[0] -> the system id (this port's flat ISystem
//                   keeps it in NicknameId, common_universe.h)
//   .text:06d034a2  find; found -> FUN_06d04480 on every field entry (node+0x18
//                   is SrvAsteroidSystem::m_Fields' head)
void SrvAsteroid_UpdateSystem(IObjDB* Db) {
    Universe::ISystem const* Info = Db->GetZoneDb();
    unsigned int SystemId = Info->NicknameId;
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It == GSrvAsteroidSystems.end())
        return;
    for (std::list<FSrvAsteroidField*>::iterator Field = It->second.m_Fields.begin();
         Field != It->second.m_Fields.end(); ++Field)
        (*Field)->update();
}

// FUN_06d03500 (.text:06d03500, cdecl). Single player.
//   .text:06d03520  Cmn = CmnAsteroid::Find(SystemId); null -> return
//   .text:06d0353b  Cmn->AddRef()
//   .text:06d035a6  insert {SystemId, SrvAsteroidSystem()} -- no find first: an
//                   existing entry is kept and reused
//   then, inline, ++RefCount; set_cmn_system(Cmn) without its null arm
//   (CmnSystem, AddRef, SystemId = Cmn+4); map_asteroid_fields (the entry
//   built with FUN_06d05230 and filled by FUN_06d04810)
//   Cmn->Release()
void SrvAsteroid_MapSystem(unsigned int SystemId) {
    CmnAsteroid::CAsteroidSystem* Cmn = CmnAsteroid::Find(SystemId);
    if (Cmn == nullptr)
        return;
    Cmn->AddRef();
    FSrvAsteroidSystemMap::iterator It =
        GSrvAsteroidSystems.insert(std::make_pair(SystemId, SrvAsteroidSystem())).first;
    SrvAsteroidSystem& System = It->second;
    ++System.m_RefCount;
    System.set_cmn_system(Cmn);
    System.map_asteroid_fields();
    Cmn->Release();
}

} // namespace SrvAsteroid
