// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// ===========================================================================
// Server.dll's IObjRW wrapper family: GameObject / EqObj / Ship / Solar / Loot.
// See src/include/server_gameobject.h for the RTTI evidence behind the
// hierarchy and for the list of things this port does NOT model yet.
// ===========================================================================

#include "../include/server_gameobject.h"
#include "../include/common_timing.h"  // Timing::read
#include "../include/server_sysdb.h"   // StarSystem::on_object_destroyed (GameObject::destroy)
#include "../include/common_misc.h"    // SUBOBJ_ID_NONE (Ship::allocate_equip_id)
#include "../include/common_cequip.h"  // CEquipTraverser / CEquip (get_damage_list)
#include "../include/dacom.h"          // FDUMP (Ship::allocate_equip_id)
#include "../include/common_goods.h"   // FindHardpoint / JETTISONED_CARGO_VELOCITY / EquipDesc (Ship::tick_jettison)
#include "../include/common_id.h"      // CFLIDMaker (Ship::tick_jettison)
#include "../include/server_createinfo.h" // Server_Acquire3DMathEngine (DAT_06d8d71c)
#include "../include/x86math.h"          // I3DMathEngine::MatrixToQuaternion
#include "../include/common_physys.h"   // PhySys::RCBegin/RCEnd, Collision::CalcCollisionDamage
#include "../include/common_universe.h" // Universe::IZone (CSimple::ZoneAtA4)
#include <cmath>
#include <windows.h>
#include <cstdio>
#include <cstdlib>   // rand (Ship::tick_jettison / compute_jettison_launch)
#include "../include/rpc_iclient.h"   // IClient (Ship::request_returned / set_formation_*)
#include "../include/DALib.h"         // DALib::Engine (Ship::create / Solar::create, IEngine +0x6c)
#include "../include/common_ini.h"              // StringAlloc (EqObj::*_by_name / dump_cargo)
#include "../include/common_server.h"           // Players, Server_BroadcastGroupControl (EqObj::destroy)
#include "../include/common_server_playerdb.h"  // PlayerData_ShouldQueueVisit / PlayerData_QueueVisit
#include "../include/common_server_types.h"     // ServerStats (EqObj::destroy)
#include "../include/rpc_iclient_args.h"        // FServerBurnFuseInfo (EqObj::light_fuse)
#include <map>                                  // ServerFuseDB::Get (EqObj::light_fuse)
#include <cstring>                              // memcpy (EqObj::build_equip_loot)

// DAT_06d8fb14 (IServerImpl.cpp) and FUN_06d43650 (pub::Controller::_SendMessage
// with a zero id; Server_ControllerDispatch, common_server_controller.h) -- the Ship
// IObjAffect bodies use both.
extern IClient* g_pClientInterface;
// .data:06d8f998 (IServerImpl.cpp) -- EqObj::destroy bumps +0x10 (.text:06ce825f).
extern ServerStats GServerStats;

// ---------------------------------------------------------------------------
// GameObject
// ---------------------------------------------------------------------------

// .text:06cee360. The IObjRW / IObjInspectImpl prefix is constructed by the
// base-class initialiser; what is left is GameObject's own state, every store
// of which is here (2026-09-24):
//   .text:06cee3ba .. 06cee3d0  MarkedByPlayers (+0x14) -- empty list
//   .text:06cee3d7 / 06cee3da   LastDamageTime (+0x20/+0x24) = 0
//   .text:06cee3dd              OwnerSystem (+0x28) = 0
//   .text:06cee3e0 .. 06cee3f7  Invincibilities (+0x2c) -- empty list
//   .text:06cee3fe / 06cee401   +0x38 / +0x39 = 0
//   .text:06cee404              +0x3c = 1.0f (0x3f800000)
//   .text:06cee40b / 06cee40e   bDestroyed (+0x40) / bInDamageCallback (+0x41) = 0
//   .text:06cee411 .. 06cee438  PendingDamage (+0x44) -- DamageList, inline-built
//   .text:06cee452              PendingDamage.Cause (+0x54) = 0x18
GameObject::GameObject()
    : LastDamageTime(0.0)
    , bInvincible(0)
    , bInvincibleFlag(0)
    , InvincibleTimer(1.0f)
    , bDestroyed(0)
    , bInDamageCallback(0)
    , OwnerSystem(nullptr)
{
    PendingDamage.Cause = static_cast<DamageCause>(0x18);
}

// FUN_06cee4a0 (.text:06cee4a0 .. .text:06cee604), entered with ECX on the CBase
// sub-object at +0x0c (the scalar deleting destructor FUN_06cee470 is the CBase
// vtable's slot 0, .rdata:06d65764). Retail order, raw bytes:
//   .text:06cee4c9 .. 06cee4d6  re-stamp GameObject's three vtables
//   .text:06cee4de  55              PUSH EBP                 ; EBP == 0
//   .text:06cee4e7  ff158841d606    CALL [->CBase::advise]   ; advise(false)
//   .text:06cee4ed  8b4e04          MOV  ECX,[ESI + 0x4]     ; CBase::Object (+0x10)
//   .text:06cee4f0  ff156443d606    CALL [->CObject::Release]; no null test
//   .text:06cee4f6 .. 06cee541  clear + free the list at +0x44 (Common's
//                               std::list<DamageEntry>::erase, .rdata:06d641e8)
//   .text:06cee544 .. 06cee57b  clear + free the list at +0x2c (FUN_06d0f620)
//   .text:06cee57e .. 06cee5b7  clear + free the list at +0x14 (MarkedByPlayers)
//   .text:06cee5cf  CALL [->CBase::~CBase]
//   .text:06cee5ec  CALL [->Watchable::unwatch]            ; on this+0x08
// The reference released here is the one the wrapper took when it was bound:
// CObject::Alloc hands back a count of 1 (Common .text:062aeea7 `INC [EAX+0x98]`)
// and CObject::Find adds one (Common .text:062aea28 `INC ECX`), so create() and
// find() both leave the wrapper owning exactly one reference -- and nothing but
// this destructor gives it back. The member lists and the IObjInspectImpl / CBase
// / Watchable bases are torn down by the compiler after this body, in the same
// order: the compiler destroys PendingDamage (+0x44), Invincibilities (+0x2c)
// and MarkedByPlayers (+0x14) in reverse declaration order, exactly retail's.
GameObject::~GameObject()
{
    advise(false);        // .text:06cee4e7
    Object->Release();    // .text:06cee4f0
}

// FUN_06cee9e0 (.text:06cee9e0, RET 4): return !is_destroyed(). The float
// argument is discarded (retail pushes Dt then ignores it).
bool GameObject::update(float Dt)
{
    (void)Dt;
    return !is_destroyed();
}

// FUN_06cef3b0 (.text:06cef3b0, `RET 0x8`). See the declaration for why the
// element is a player id.
void GameObject::mark_for_player(unsigned int PlayerId, int MarkType)
{
    // .text:06cef3b4 .. .text:06cef3ca -- the search runs FIRST, before either
    // branch looks at MarkType, and it stops on the first match
    // (.text:06cef3c1 `CMP [EAX + 0x8],EDX`).
    std::list<unsigned int>::iterator It = MarkedByPlayers.begin();
    while (It != MarkedByPlayers.end() && *It != PlayerId)
        ++It;

    // .text:06cef3d0  83fa01  CMP EDX,0x1 / .text:06cef3d3 JNZ LAB_06cef41b, and
    // .text:06cef41b  85d2  TEST EDX,EDX / .text:06cef41d JNZ LAB_06cef442 --
    // retail compares against the literals 1 and 0, so any other MarkType falls
    // out at LAB_06cef442 having done nothing. Do not turn this into a bool.
    if (MarkType == 1) {
        // .text:06cef3d5 `CMP EAX,ECX` / .text:06cef3d7 `JNZ LAB_06cef442` --
        // only when it is NOT already in the list.
        if (It == MarkedByPlayers.end())
            MarkedByPlayers.push_back(PlayerId);   // .text:06cef3e0 PUSH 0xc / new
        return;
    }
    if (MarkType == 0) {
        // .text:06cef41f / .text:06cef421 -- only when it IS present; then
        // .text:06cef423 .. .text:06cef43f unlink, free, size--.
        if (It != MarkedByPlayers.end())
            MarkedByPlayers.erase(It);
    }
}

// FUN_06cef450 (.text:06cef450, `RET 0x4`), fourteen instructions:
//   .text:06cef450  8b4918    MOV    ECX,[ECX + 0x18]     ; the list sentinel
//   .text:06cef460  395008    CMP    [EAX + 0x8],EDX      ; node value vs the id
//   .text:06cef46b  3bc1      CMP    EAX,ECX
//   .text:06cef46d  0f95c0    SETNZ  AL                   ; "not end()" == found
bool GameObject::is_marked_by(unsigned int PlayerId) const
{
    for (std::list<unsigned int>::const_iterator It = MarkedByPlayers.begin();
         It != MarkedByPlayers.end(); ++It) {
        if (*It == PlayerId)
            return true;
    }
    return false;
}

// Retail inlines this guard at every single call site: load CBase::Object and
// null it unless (ObjectClass & 3) == 3. .text:06cef1c0 is the shortest
// example, .text:06cf9069 (pub::SpaceObj::GetCenterOfMass) the clearest.
CObject* GameObject::cobj() const
{
    CObject* Obj = Object;
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & 3u) != 3u)
        return nullptr;
    return Obj;
}

void GameObject::attach(CObject* Obj)
{
    Object = Obj;
    advise(true);
}

CSimple* GameObject::csimple() const
{
    return static_cast<CSimple*>(cobj());
}

unsigned char GameObject::get_invincible_flag() const
{
    return bInvincibleFlag;
}

unsigned char GameObject::is_invincible() const
{
    return bInvincible;
}

float GameObject::get_invincible_timer() const
{
    return InvincibleTimer;
}

// ---------------------------------------------------------------------------
// Retail slot +0x1a8 -- "give me a DamageList describing every sub-object of
// mine whose relative health is below StatusCeiling".
//
// FUN_06ceee20 (.text:06ceee20, 27 instructions, `RET 0x8`) is the BASE body
// that ten of the thirteen wrapper classes carry unchanged; the EqObj family's
// three (EqObj .rdata:06d65244, Ship .rdata:06d6711c, Solar .rdata:06d6746c,
// each + 0x1a8) override it with .text:06ceb210, which calls this one first.
//
// The base only ever reports the HULL:
//   .text:06ceee21  MOV  ESI,[ECX + 0x10]        ; CBase::Object
//   .text:06ceee2b  AND  EAX,0x3 / CMP AL,0x3    ; the usual CSimple narrowing
//   .text:06ceee34  CALL [->COMMON.DLL::CSimple::get_relative_health]
//   .text:06ceee3a  FCOMP float ptr [ESP + 0xc]  ; against StatusCeiling
//   .text:06ceee40  TEST AH,0x5 / JP  -> return
//   .text:06ceee49  CALL [->COMMON.DLL::CSimple::get_hit_pts]
//   .text:06ceee50  MOV  ECX,[->COMMON.DLL::SUBOBJ_ID_ROOT]
//   .text:06ceee63  CALL [->COMMON.DLL::DamageList::add_damage_entry]
//
// The x87 predicate, spelled out because it inverts if read loosely: FCOMP
// leaves C3 in AH bit 6, C2 in bit 2 and C0 in bit 0. `TEST AH,0x5` looks at
// C0 and C2 only, and JP jumps on EVEN parity of the masked byte. ST0 > mem
// gives AH&5 == 0 (even, jump); ST0 == mem gives AH&5 == 0 (even, jump);
// unordered gives AH&5 == 5, two bits set (even, jump); only ST0 < mem gives
// AH&5 == 1 (odd, fall through). So the entry is added ONLY when the health is
// strictly BELOW the ceiling -- which is why the observer's 2.0f means
// "everything".
void GameObject::get_damage_list(DamageList& Out, float StatusCeiling) const
{
    CSimple* const Simple = csimple();
    if (Simple == nullptr)
        return;
    if (!(Simple->get_relative_health() < StatusCeiling))
        return;
    // .text:06ceee45  PUSH 0x0 -- the fate argument, pushed before the
    // get_hit_pts call whose float lands on top of it.
    Out.add_damage_entry(SUBOBJ_ID_ROOT, Simple->get_hit_pts(),
                         DamageEntry::SubObjFate::None);
}

// ---------------------------------------------------------------------------
// EqObj::get_damage_list -- .text:06ceb210, 104 instructions, `RET 0x8`.
//
// Three vtables hold it and only three: .rdata:06d653ec, .rdata:06d672c4 and
// .rdata:06d67614, i.e. base + 0x1a8 for EqObj (.rdata:06d65244, stamped at
// .text:06ce66b6 inside EqObj::EqObj at .text:06ce6620), Ship
// (.rdata:06d6711c, .text:06d00f1b) and Solar (.rdata:06d6746c,
// .text:06d02b78). Declaring it on EqObj reproduces exactly that set.
//
// Two traps in this body, both caught on the bytes:
//
//  1. The value COMPARED and the value RECORDED are different accessors. The
//     comparison is `.text:06ceb295  ff5244  CALL dword ptr [EDX + 0x44]` and
//     the recorded number is `.text:06ceb2ad  ff503c  CALL dword ptr [EAX + 0x3c]`.
//     CETractor's own vftable (.rdata:0639d8c4) names both: base + 0x44 at
//     .rdata:0639d908 is CEquip::GetRelativeHealth and base + 0x3c at
//     .rdata:0639d900 is CEquip::GetHitPoints. An earlier note on this item
//     said the entry carries "the equip's own status", i.e. the +0x44 value.
//     It does not.
//
//  2. The traverser is the TWO-argument overload. .text:06ceb232 calls through
//     .rdata:06d642cc, whose decorated name is "??0CEquipTraverser@@QAE@H_N@Z"
//     -- (int, bool) -- with `PUSH 0x1` then `PUSH -0x1` at .text:06ceb22a /
//     .text:06ceb22c, so the arguments are (-1, true): every type, destroyed
//     items included. That is a different import from the one-argument
//     "??0CEquipTraverser@@QAE@H@Z" at .rdata:06d64204 the create builders use.
//     And the manager call is the NON-const Traverse overload
//     (.rdata:06d64200, "?Traverse@CEquipManager@@QAEPAVCEquip@@...") where
//     FUN_06d07690 uses the const one at .rdata:06d6428c.
// ---------------------------------------------------------------------------
void EqObj::get_damage_list(DamageList& Out, float StatusCeiling) const
{
    // .text:06ceb225  CALL FUN_06ceee20 -- a DIRECT call to the base body, with
    // the same two arguments re-pushed at .text:06ceb21f / .text:06ceb220.
    GameObject::get_damage_list(Out, StatusCeiling);

    // .text:06ceb240 .. .text:06ceb25e -- the inlined accessor: [this+0x10],
    // and zero unless (ObjectClass & 0x103) == 0x103. Retail has NO early exit
    // on the zero: .text:06ceb25c zeroes EAX and .text:06ceb263 still does
    // `LEA ECX,[EAX + 0xe4]` and calls CEquipManager::Traverse on it. Neither
    // does the port. The override sits only in EqObj's, Ship's and Solar's
    // vtables, whose wrappers always hold a CEqObj, so retail never reaches that
    // path; a null here is an upstream defect and faults here as in retail.
    CEqObj* const Owner = ceqobj();

    // .text:06ceb232 -- CEquipTraverser(-1, true).
    CEquipTraverser Walk(-1, true);
    for (;;) {
        // .text:06ceb269  CALL [->COMMON.DLL::CEquipManager::Traverse]
        CEquip* const Equip = Owner->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;

        // .text:06ceb279  ff5208  CALL dword ptr [EDX + 0x8] -- CEquip vtable
        // +0x08 is IsDestroyed (.rdata:0639d8cc in ??_7CETractor@@6B@).
        if (Equip->IsDestroyed()) {
            // .text:06ceb286 PUSH 0x3 / .text:06ceb288 PUSH 0x0 /
            // .text:06ceb28c PUSH EAX -- so the arguments in source order are
            // (id, 0.0f, SubObjFate(3)). The pushed 0 is a float bit pattern,
            // not an int: the other arm at .text:06ceb2b1 FSTPs into the same
            // stack slot. SubObjFate(3) is HealthProxy in common_misc.h.
            Out.add_damage_entry(Equip->Id, 0.0f,
                                 DamageEntry::SubObjFate::HealthProxy);
            continue;
        }

        // .text:06ceb295  CALL [EDX + 0x44]  ; CEquip::GetRelativeHealth
        // .text:06ceb298  FCOMP float ptr [ESP + 0x34]  ; unrounded ST0 (double)
        // .text:06ceb29e  TEST AH,0x5 / JP -> skip unless ST0 < ceiling
        if (!(Equip->GetRelativeHealth() < StatusCeiling))
            continue;

        // .text:06ceb2a5  MOV DI,word ptr [ESI + 0x8] -- the id is taken before
        // the call, as a word out of CEquip+0x08.
        Out.add_damage_entry(Equip->Id, Equip->GetHitPoints(),
                             DamageEntry::SubObjFate::None);
    }

    // .text:06ceb2bb  MOV byte ptr [ESP + 0x14],0x1 -- the CArchGrpTraverser's
    // "at start" byte, then the identical second pass over the arch groups at
    // CEqObj+0x144.
    CArchGrpTraverser Groups;
    for (;;) {
        // .text:06ceb2e9  CALL [->COMMON.DLL::CArchGroupManager::Traverse]
        CArchGroup* const Group = Owner->ArchGroups.Traverse(Groups);
        if (Group == nullptr)
            break;

        // .text:06ceb2f7  CALL [->COMMON.DLL::CArchGroup::GetRelativeHealth]
        // .text:06ceb2fd  FCOMP float ptr [ESP + 0x34]  ; the same ceiling
        if (!(Group->GetRelativeHealth() < StatusCeiling))
            continue;

        // .text:06ceb308  MOV EDX,[ESI + 0x4] -- CArchGroup::Arch --
        // .text:06ceb30b  MOV DI,word ptr [EDX + 0x4] -- and its Id.
        // .text:06ceb313  CALL [->COMMON.DLL::CArchGroup::GetHitPoints]
        Out.add_damage_entry(Group->GetID(), Group->GetHitPoints(),
                             DamageEntry::SubObjFate::None);
    }
}

// ---------------------------------------------------------------------------
// The four retail slots between IObjInspect's last and create(). See the
// header for the vtable arithmetic that pins them down.
// ---------------------------------------------------------------------------

// +0x154 -- .text:06cef1c0
unsigned int GameObject::get_type_bits() const
{
    // .text:06cef1c0 .. 06cef1d4 -- the inlined (ObjectClass & 3) guard yields
    // null and `MOV EAX,[EAX+0xe0]` reads through it anyway: retail has no
    // default value, so neither does the port (a null here is an upstream
    // defect and faults as in retail).
    return csimple()->TypeBits;
}

// +0x158 -- .text:06cee630
bool GameObject::destroy(int Kind, unsigned int ObjId)
{
    // .text:06cee633 .. 06cee64a -- the (ObjectClass & 3) guard nulls the
    // pointer and the call goes through it anyway: no null test in retail.
    // PUSH 0 / CALL [CObjectVt + 0x98] == CSimple::set_hit_pts.
    csimple()->set_hit_pts(0.0f);

    // .text:06cee659  CALL FUN_06d0b160 with ECX = OwnerSystem, i.e.
    // StarSystem::on_object_destroyed(this, Kind). PORTED 2026-09-17 (wave 11);
    // all three PlayerData helpers it needs exist now
    // (PlayerData_TerminateTrade / _CancelTradeRequest / _SetRelativeHealth),
    // and the three fields the earlier note called unmodelled turned out to be
    // modelled already: +0x268 is `relative_health` (0x418 - 0x1b0), +0x3CC is
    // `trade_offer` and +0x3D4 is `inbound_trade_requests`.
    //
    // Retail passes only the FIRST argument through; ObjId is this slot's own
    // second parameter and never leaves GameObject::destroy.
    // .text:06cee654 -- ECX = [ESI+0x28] with no null test.
    OwnerSystem->on_object_destroyed(this, Kind);
    (void)ObjId;

    // .text:06cee662  CALL [own vtable + 0x15c]
    destroy_notify();

    // .text:06cee668  MOV AL,1
    return true;
}

// +0x15c -- .text:06cee670
void GameObject::destroy_notify()
{
    // .text:06cee673  MOV byte ptr [ESI + 0x40],0x1
    bDestroyed = 1;

    // .text:06cee677  CALL COMMON.DLL::SinglePlayer / .text:06cee695
    // MOV ECX,[EAX+0xb4] -- CSimple::OwnerPlayer. In single player an object
    // that belongs to a player is left connected. The CSimple comes out of the
    // (ObjectClass & 3) == 3 guard (.text:06cee681 .. 06cee693) and is read
    // without a null test.
    if (SinglePlayer())
    {
        if (csimple()->OwnerPlayer != 0)
            return;
    }

    // .text:06cee69f .. .text:06cee6bb -- the same guard again, then
    // CALL [CObjectVt + 0x94] == CSimple::disconnect, ECX = the CSimple and
    // the OwnerSystem (+0x28) pushed as the IObjDB*; no null test on either.
    csimple()->disconnect(OwnerSystem);
}

// +0x160 -- .text:06cee6d0
bool GameObject::is_destroyed() const
{
    return bDestroyed != 0;
}

// ---------------------------------------------------------------------------
// GameObject's extension slots +0x170 .. +0x1b8 (declaration order == retail
// slot order, see server_gameobject.h). PORTED 2026-09-24
// (claude-code:w5-server-06ce) from the listing, one body per slot.
// ---------------------------------------------------------------------------

// .data:DAT_06d8d71c -- GameObj.cpp's own lazily fetched math engine
// (.text:06cee736 `MOV EAX,[DAT_06d8d71c]` / JNZ / CALL FUN_06d5ff70 / store).
static I3DMathEngine* GGameObjectMathEngine = nullptr;

// +0x170 -- .text:06cee6e0 (RET 8).
void GameObject::enter_system(StarSystem* Sys, PhySys::CreateParms const* Parms)
{
    // .text:06cee6ea  MOV [EDI+0x28],EAX
    OwnerSystem = Sys;

    // .text:06cee6f1 .. 06cee6fd -- only when Parms is given: the RAW CBase
    // Object (no class guard), CObject vt +0x78 remake_physical(*Parms, 0.0f).
    if (Parms != nullptr)
        Object->remake_physical(*Parms, 0.0f);

    // .text:06cee700 .. 06cee79a -- outside single player an object that has an
    // owner player is put under remote correction. The owner test reads the
    // guarded CSimple without a null test (.text:06cee726).
    if (!SinglePlayer() && csimple()->OwnerPlayer != 0)
    {
        // .text:06cee733  LEA ESI,[EAX+0x8] -- the RAW Object's orientation;
        // .text:06cee736 .. 06cee752 -- DAT_06d8d71c's MatrixToQuaternion (+0x50).
        if (GGameObjectMathEngine == nullptr)
            GGameObjectMathEngine = Server_Acquire3DMathEngine();
        Quaternion Orientation;
        GGameObjectMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orientation),
                                                  reinterpret_cast<FMatrix const&>(Object->Orientation));

        // .text:06cee755 .. 06cee794 -- EBP = &RawObject->WorldPosition (+0x2c),
        // EBX = the guarded CSimple; the angular velocity (vt +0x58) is fetched
        // before the velocity (vt +0x54), both from the raw Object, then
        // RCBegin(cs, pos, vel, quat, angvel).
        CSimple* const Simple = csimple();
        Vector const AngularVelocity = Object->get_angular_velocity();
        Vector const Velocity = Object->get_velocity();
        PhySys::RCBegin(Simple, Object->WorldPosition, Velocity, Orientation, AngularVelocity);
    }

    // .text:06cee7a0 .. 06cee7e2 -- connect(Sys) (CSimple vt +0x90) unless this
    // is a player's object in single player. Retail's null-guard arm
    // (.text:06cee7ef) calls through a null pointer; there is no extra test.
    if (SinglePlayer() && csimple()->OwnerPlayer != 0)
        return;
    csimple()->connect(Sys);
}

// +0x174 -- .text:06cee810 (RET 4).
void GameObject::leave_system(PhySys::CreateParms* OutParms)
{
    // .text:06cee817  EDI = the RAW Object (no guard) -- every physics read
    // below goes through it.
    CObject* const Raw = Object;

    // .text:06cee82e .. 06cee838 -- OwnerSystem->FUN_06d0cfa0(cs->Id): every
    // ship and solar of the system hears that this object is leaving (+0x178).
    OwnerSystem->notify_object_left(csimple()->Id);

    // .text:06cee83d .. 06cee87b -- RCEnd only outside single player and only
    // for an owned object.
    if (!SinglePlayer() && csimple()->OwnerPlayer != 0)
        PhySys::RCEnd(csimple());

    // .text:06cee885 .. 06cee918 -- the caller's CreateParms, from the live body.
    if (OutParms != nullptr)
    {
        OutParms->Mass = Raw->get_mass();                  // .text:06cee895 vt +0x6c
        OutParms->bHasInertia = 1;                         // .text:06cee89a
        // .text:06cee89e .. 06cee8c6 -- BodyPtr (+0x54) picks the IVP moment of
        // inertia or a zero vector.
        Vector Inertia;
        if (Raw->BodyPtr != nullptr)
            Inertia = PhySys::GetMomentOfInertia(Raw);
        else
        {
            Inertia.x = 0.0f;
            Inertia.y = 0.0f;
            Inertia.z = 0.0f;
        }
        OutParms->Inertia = Inertia;                       // .text:06cee8d9 .. 06cee8e3
        OutParms->bEnableCollisions = PhySys::GetCollisionState(Raw) ? 1 : 0;   // .text:06cee8ec
        PhySys::GetDamping(Raw, OutParms->LinearDamping,
                           *reinterpret_cast<Vector*>(OutParms->AngularDamping));  // .text:06cee8f8
        // .text:06cee8ff .. 06cee918 -- an unbounded byte copy of the collision
        // group name into +0x1e, terminator included (retail's inlined strcpy).
        char const* Src = PhySys::GetCollisionGroup(Raw);
        char* Dst = OutParms->Name;
        char Ch;
        do
        {
            Ch = *Src++;
            *Dst++ = Ch;
        } while (Ch != 0);
    }

    // .text:06cee91e  CALL [EDX+0x74] -- unmake_physical on the raw Object.
    Raw->unmake_physical();

    // .text:06cee921 .. 06cee964 -- disconnect(OwnerSystem) (vt +0x94) unless a
    // player's object in single player; OwnerSystem = null either way.
    if (!(SinglePlayer() && csimple()->OwnerPlayer != 0))
        csimple()->disconnect(OwnerSystem);
    OwnerSystem = nullptr;                                 // .text:06cee96b
}

// +0x178 -- .text:06cee980 (RET 4).
void GameObject::on_object_left_system(unsigned int ObjId)
{
    // .text:06cee980 .. 06cee997 -- the raw Object narrowed with the CEqObj mask
    // 0x103; anything else does nothing.
    CObject* const Obj = Object;
    if (Obj == nullptr)
        return;
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x103u) != 0x103u)
        return;
    IBehaviorManager* const Behavior = static_cast<CEqObj*>(Obj)->get_behavior_interface();
    if (Behavior == nullptr)
        return;

    // .text:06cee9a3 .. 06cee9d4 -- a stack record {3, &Payload} with Payload
    // {0, 0, ObjId}, handed to IBehaviorManager's vtable slot 0 (Common
    // LAB_062d3f90, the behavior manager's notification switch; case 3 is
    // "an object left the system"). The port's IBehaviorManager has the same
    // one-slot vtable; slot 0 is virtual Slot00.
    unsigned int Payload[3];
    Payload[0] = 0;
    Payload[1] = 0;
    Payload[2] = ObjId;
    FBehaviorManagerEvent Event;
    Event.Type = 3;
    Event.Data = Payload;
    Behavior->Slot00(&Event);
}

// +0x17c -- .text:06ceeb00 (RET 8).
void GameObject::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    // .text:06ceeb04 .. 06ceeb1d -- Hit+4 against SUBOBJ_ID_ROOT, then NONE.
    if (Hit.SubObjId == SUBOBJ_ID_ROOT || Hit.SubObjId == SUBOBJ_ID_NONE)
        damage_hull(Hit.Damage, Damage);                   // .text:06ceeb2a vt +0x1b0
}

// +0x180 -- .text:06ceeb40 (RET 8).
void GameObject::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    // .text:06ceeb44 .. 06ceeb5d -- Hit+8 against ROOT, then NONE.
    if (Hit.SubObjId == SUBOBJ_ID_ROOT || Hit.SubObjId == SUBOBJ_ID_NONE)
        damage_hull(Hit.Munition->HullDamage, Damage);     // .text:06ceeb66 Munition+0x80
}

// +0x184 -- .text:06ceeb80 (RET 8).
void GameObject::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    // .text:06ceeb88 .. 06ceebbd -- R2 = Radius^2 and the two inner bands
    // R2 * (4/9) (DAT_06d655d4) and R2 * (1/9) (DAT_06d655d0): the squares of
    // 2/3 and 1/3 of the radius.
    float const Radius = Blast.Explosion->Radius;
    float const Radius2 = Radius * Radius;
    float const Band2 = Radius2 * (4.0f / 9.0f);
    float const Band3 = Radius2 * (1.0f / 9.0f);

    // .text:06ceebc1 -- raw Object vt +0x64 get_physical_radius_r(Center).
    Vector Center;
    float const ObjRadius = Object->get_physical_radius_r(Center);

    // .text:06ceebc4 .. 06ceebed -- ((dz*dz + dx*dx) + dy*dy) - r*r. The three
    // differences and the result never leave the x87 stack (FSTP ST4 / FSTP ST0
    // at .text:06ceebef .. 06ceebf5), so they are doubles here.
    double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
    double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
    double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
    double Dist2 = ((Dz * Dz + Dx * Dx) + Dy * Dy) - static_cast<double>(ObjRadius) * ObjRadius;
    // .text:06ceebf7  FCOM 0.0 / TEST AH,0x41 / JZ -- kept only when > 0.
    if (!(Dist2 > 0.0))
        Dist2 = 0.0;

    // .text:06ceec0c .. 06ceec45 -- TEST AH,5 / JP skips on >= or unordered, so
    // each band is a strict "<".
    int Factor;
    if (Dist2 < Band3)
        Factor = 3;
    else if (Dist2 < Band2)
        Factor = 2;
    else if (Dist2 < Radius2)
        Factor = 1;
    else
        return;

    // .text:06ceec50 .. 06ceec7a -- ((FILD Factor * 1/3f (DAT_06d655cc)) *
    // HullDamage) * Root::ExplosionResistance (+0x28), one FSTP dword at the
    // end, then vt +0x1b0.
    float const Resistance = Object->get_archetype()->ExplosionResistance;
    float const Amount = static_cast<float>(
        ((static_cast<double>(Factor) * (1.0f / 3.0f)) * Blast.Explosion->HullDamage) * Resistance);
    damage_hull(Amount, Damage);
}

// +0x188 -- .text:06ceee70 (RET 4).
void GameObject::apply_damage_list(DamageList& Damage)
{
    // .text:06ceee77 .. 06ceeeca -- re-entry is reported, not refused.
    if (is_in_damage_callback())
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: ID(0x%X) Arch(%s): Tried to apply damage of type %s within another damage callback.",
              "E:\\FL\\Scratch\\Source\\Server\\space\\Gameobj.cpp", 0x1cf,
              get_id(), Object->get_archetype()->Nickname.get_string(),
              DamageList::DmgCauseToString(Damage.Cause));
    }

    set_in_damage_callback(true);                          // .text:06ceeed5 vt +0x190

    // .text:06ceeedb .. 06ceef18 -- only a non-empty list: every ROOT entry goes
    // through vt +0x1b4, then the system broadcast (FUN_06d0b800).
    if (Damage.Count != 0)
    {
        for (std::list<DamageEntry>::iterator It = Damage.Entries.begin();
             It != Damage.Entries.end(); ++It)
        {
            if (It->SubObjId == SUBOBJ_ID_ROOT)
                apply_root_damage(*It);
        }
        StarSystem_DamageObject(OwnerSystem, this, &Damage);
    }

    // .text:06ceef1e .. 06ceef4c
    if (Damage.bDestroyed && !is_destroyed() && !is_damage_blocked(Damage))
        destroy(1, Damage.InflictorId);

    set_in_damage_callback(false);                         // .text:06ceef58
}

// +0x18c -- .text:06ce6190 `MOV AL,[ECX+0x41]; RET`.
bool GameObject::is_in_damage_callback() const
{
    return bInDamageCallback != 0;
}

// The flush set_in_damage_callback (+0x190, .text:06cef000 .. 06cef092) and
// set_relative_health (+0x1ac, .text:06cef28c .. 06cef31d) both inline: a fresh
// DamageList receives a copy of PendingDamage's entries (std::list::insert,
// which carries the size, i.e. Count) and its four scalars; PendingDamage's
// entries are then erased one by one (Count back to 0 -- bDestroyed is NOT
// reset), and the copy goes through apply_damage_list (vt +0x188).
static void GameObject_FlushPendingDamage(GameObject* Obj, DamageList& Pending)
{
    DamageList Local;
    Local.Entries.insert(Local.Entries.begin(), Pending.Entries.begin(), Pending.Entries.end());
    Local.Count = Pending.Count;
    Local.bDestroyed = Pending.bDestroyed;
    Local.Cause = Pending.Cause;
    Local.InflictorId = Pending.InflictorId;
    Local.InflictorOwner = Pending.InflictorOwner;
    Pending.Entries.clear();
    Pending.Count = 0;
    Obj->apply_damage_list(Local);
}

// +0x190 -- .text:06ceefa0 (RET 4).
void GameObject::set_in_damage_callback(bool bIn)
{
    bInDamageCallback = bIn ? 1 : 0;                       // .text:06ceefc4
    // .text:06ceefc7 / 06ceefcd -- only when clearing, and only with a non-empty
    // PendingDamage (+0x4c, its Count).
    if (bIn || PendingDamage.Count == 0)
        return;
    GameObject_FlushPendingDamage(this, PendingDamage);
}

// +0x194 -- .text:06cef0f0 (RET 4).
void GameObject::set_invincible(FInvincibility const& Cmd)
{
    // .text:06cef0f5 .. 06cef10a -- the command's own values start the reduction.
    bInvincible = Cmd.bProtect;
    bInvincibleFlag = Cmd.bFromPlayers;
    InvincibleTimer = Cmd.MaxHpLoss;

    // .text:06cef112 .. 06cef16d -- one pass over every node, no early exit.
    bool bNew = true;
    for (std::list<FInvincibility>::iterator It = Invincibilities.begin();
         It != Invincibilities.end(); ++It)
    {
        if (It->Type == Cmd.Type)
        {
            // .text:06cef11d .. 06cef130 -- the stored command is replaced and not
            // folded (its new values already are).
            *It = Cmd;
            bNew = false;
            continue;
        }
        // .text:06cef134 .. 06cef14b -- the two flags are OR-ed in.
        if (bInvincible == 0)
            bInvincible = It->bProtect;
        if (bInvincibleFlag == 0)
            bInvincibleFlag = It->bFromPlayers;
        // .text:06cef14e .. 06cef165 -- FCOMPP of node against current; TEST
        // AH,0x41 / JNZ takes the node's value on <= or unordered: the minimum.
        float const Current = InvincibleTimer;
        float const Node = It->MaxHpLoss;
        InvincibleTimer = (Node > Current) ? Current : Node;
    }

    // .text:06cef16f .. 06cef1af -- push_back of a Type not seen before.
    if (bNew)
        Invincibilities.push_back(Cmd);
}

// +0x1a4 -- .text:06cee9f0 (RET 4).
void GameObject::on_collision(FCollisionEvent const& Hit)
{
    // .text:06cee9fb .. 06ceea2a -- nothing for an id-less partner or for a
    // partner that belongs to a player (its CSimple read without a null test).
    if (Hit.Other->get_id() == 0)
        return;
    if (Hit.Other->csimple()->OwnerPlayer != 0)
        return;

    // .text:06ceea30 .. 06ceea68 -- relative velocity through IObjInspect +4;
    // the partner's is fetched first.
    Vector const OtherVelocity = Hit.Other->get_velocity();
    Vector const OwnVelocity = get_velocity();
    float const Rx = OwnVelocity.x - OtherVelocity.x;
    float const Ry = OwnVelocity.y - OtherVelocity.y;
    float const Rz = OwnVelocity.z - OtherVelocity.z;

    // .text:06ceea6c .. 06ceeaa2 -- this object's mass (IObjInspect +0x30) is
    // the third argument, the partner's the second, sqrt((x*x + z*z) + y*y)
    // the first.
    float const OwnMass = get_mass();
    float const OtherMass = Hit.Other->get_mass();
    // .text:06ceea81 .. 06ceea9f -- the squares, their sum and FSQRT stay on
    // the x87 stack; one FSTP dword stores the speed.
    float const Speed = static_cast<float>(sqrt((static_cast<double>(Rx) * Rx
                                                 + static_cast<double>(Rz) * Rz)
                                                + static_cast<double>(Ry) * Ry));
    float const Amount = Collision::CalcCollisionDamage(Speed, OtherMass, OwnMass);

    // .text:06ceeab3 .. 06ceeae6 -- the record FUN_06d0d730 reads.
    FCollisionDamage Record;
    Record.SourceId = get_id();
    Record.SourceSubObjId = Hit.SubObjId;
    Record.TargetId = Hit.Other->get_id();
    Record.TargetSubObjId = Hit.OtherSubObjId;
    Record.Pad0e = 0;
    Record.Damage = Amount;
    OwnerSystem->collision_damage(Record, Hit.Other);     // .text:06ceeaea
}

// +0x1ac -- .text:06cef1e0 (RET 4).
void GameObject::set_relative_health(float Relative)
{
    // .text:06cef215 .. 06cef239 -- a root entry of max * Relative, fate 0, into
    // PendingDamage.
    PendingDamage.add_damage_entry(SUBOBJ_ID_ROOT, csimple()->get_max_hit_pts() * Relative,
                                   DamageEntry::SubObjFate::None);
    // .text:06cef23f .. 06cef250 -- TEST AH,0x41 / JP skips on > or unordered.
    if (Relative <= 0.0f)
        PendingDamage.bDestroyed = true;
    // .text:06cef254 .. 06cef31d -- applied at once unless inside a callback.
    if (is_in_damage_callback())
        return;
    GameObject_FlushPendingDamage(this, PendingDamage);
}

// +0x1b0 -- .text:06ceec90 (RET 8).
bool GameObject::damage_hull(float Amount, DamageList& Damage)
{
    // .text:06ceec97 .. 06ceeca6 -- TEST AH,0x44 / JNP: exactly zero does nothing.
    if (Amount == 0.0f)
        return false;

    // .text:06ceecac .. 06ceecf0 -- what the list already says, else the hull.
    float Current = Damage.get_hit_pts_left(SUBOBJ_ID_ROOT);
    if (Current < 0.0f)
        Current = csimple()->get_hit_pts();

    // .text:06ceecf4 .. 06ceed3d
    float NewHitPts = Current;
    if (Amount > 0.0f)
    {
        float const Left = Current - Amount;
        NewHitPts = (Left > 0.0f) ? Left : 0.0f;
    }
    else if (Amount < 0.0f)
    {
        // .text:06ceed2c .. 06ceed3d -- a negative amount zeroes the hull.
        NewHitPts = 0.0f;
    }

    // .text:06ceed45 .. 06ceedca -- the invincibility floor: the hull may not go
    // below (1 - InvincibleTimer) of its maximum. Already below it -> unchanged;
    // crossing it -> clamped onto it.
    if (is_damage_blocked(Damage))
    {
        // Max (.text:06ceed70) and Floor (.text:06ceed84) are stored floats;
        // 1/Max (.text:06ceed88 .. 06ceed8e) stays on the x87 stack for both
        // products, so it is a double here.
        float const MaxHitPts = csimple()->get_max_hit_pts();
        float const Floor = 1.0f - get_invincible_timer();
        double const InvMax = 1.0 / static_cast<double>(MaxHitPts);
        if (Current * InvMax < Floor)
            NewHitPts = Current;
        else if (NewHitPts * InvMax < Floor)
            NewHitPts = Floor * MaxHitPts;
    }

    // .text:06ceedce .. 06ceeddb -- TEST AH,0x44 / JNP: no change, nothing added.
    if (NewHitPts == Current)
        return false;

    Damage.add_damage_entry(SUBOBJ_ID_ROOT, NewHitPts, DamageEntry::SubObjFate::None);
    // .text:06ceedf8 .. 06ceee0b -- TEST AH,0x41 / JP: <= 0 (ordered) destroys.
    if (NewHitPts <= 0.0f)
    {
        Damage.bDestroyed = true;
        return true;
    }
    return false;
}

// +0x1b4 -- .text:06ceef70 (RET 4): tail-jump into CSimple vt +0x98.
void GameObject::apply_root_damage(DamageEntry const& Entry)
{
    csimple()->set_hit_pts(Entry.HitPts);
}

// +0x1b8 -- .text:06cef0b0 (RET 4).
bool GameObject::is_damage_blocked(DamageList const& Damage) const
{
    // .text:06cef0ba  CMP EDX,0x10 / JGE -- a signed compare of the cause.
    if (static_cast<int>(Damage.Cause) >= 0x10)
        return false;
    // .text:06cef0bf .. 06cef0e4
    if (Damage.InflictorOwner != 0)
        return get_invincible_flag();
    return is_invincible();
}

// Ship::create's body (.text:06d01040). Only Ship uses it: Solar::create opens
// before it advises and Loot::create has a different order and return value, so
// those two spell their own sequence out below.
//     PUSH 0x503 / CALL COMMON.DLL::CObject::Alloc
//     MOV [ESI+0x10],EAX            ; CBase::Object = the new CObject
//     PUSH EAX / MOV ECX,EDI / CALL [.rdata:06d6418c]
//                                   ; COMMON.DLL::Archetype::Root::create_instance(
//                                   ;   (IEngineInstance*)Object) -- ONE argument,
//                                   ;   so it is not Root::AddRef (no arguments);
//                                   ;   the import label at .rdata:06d6418c names it
//     MOV EDX,[ESI+0x10] / MOV EDX,[EDX+4]        ; EngineObject::EngineInstanceId
//     ... CALL [DALib::Engine vtable + 0x6c]      ; IEngine::UpdateInstance(inst,0,0)
//     PUSH 1 / LEA ECX,[ESI+0xc] / CALL COMMON.DLL::CBase::advise
//     MOV ECX,[ESI+0x10] / MOV EAX,[ECX] / PUSH EDI / CALL [EAX+0x4c]  ; CObject::open
//     return Object != nullptr
// No null checks anywhere: retail dereferences Arch and Object unconditionally.
bool GameObject::CreateOfClass(CObject::Class ObjectClass, Archetype::Root* Arch)
{
    Object = CObject::Alloc(ObjectClass);

    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    // .text:06d01062 .. 06d01077: DALib::Engine vtable +0x6c (slot 27),
    // IEngine::UpdateInstance(Object->EngineInstanceId, 0, 0). Server.vcxproj
    // references DALib (the same call Loot::create / Mine::create make).
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Object->EngineInstanceId), 0, nullptr);

    advise(true);

    Object->open(Arch);

    return Object != nullptr;
}

// Ship::find at .text:06d010a0 -- the same shape with CObject::Find instead of
// CObject::Alloc, no archetype and no engine touch.
bool GameObject::FindOfClass(CObject::Class ObjectClass, unsigned int const& Id)
{
    Object = CObject::Find(Id, ObjectClass);
    advise(true);
    return Object != nullptr;
}

// ---------------------------------------------------------------------------
// IObjAffect -- GameObject's own table, .rdata:06d6576c (stored at +4 by the ctor,
// .text:06cee444). PORTED 2026-09-24 against that table: every GameObject entry is a
// one-instruction error stub, and eleven of the thirteen Server wrappers keep it slot for
// slot (the table dumps are in common_iface.h). The error codes are retail's:
//   .text:06cef3a0  MOV EAX,0xfffffffd / RET 4
//   .text:06cef340  MOV EAX,0xfffffff8 / RET 4
//   .text:06d567c0  XOR EAX,EAX / RET          (ICF-folded with pub::Player::SetCostume)
//   .text:06cef380  MOV EAX,0xfffffffd / RET 0x10
//   .text:06cef370  MOV EAX,0xfffffffd / RET 0xc
//   .text:06cef350  MOV EAX,0xfffffffd / RET 8
//   .text:06cef360  MOV EAX,0xfffffffd / RET 0x14
//   .text:06cef390  MOV EAX,0xfffffffd / RET
// The real bodies are Ship's (.rdata:06d6709c) and EqObj/Solar's (.rdata:06d651c4 /
// .rdata:06d673ec) overrides: EqObj::slot_34/3c/40/44/4c/58 (.text:06ce68a0..06ce7210,
// below) and the Ship items .text:06d01f30..06d02ad0.
// ---------------------------------------------------------------------------
int GameObject::set_throttle(float) { return -3; }                                   // 06cef3a0
int GameObject::set_axis_throttle(Vector const&) { return -3; }                      // 06cef3a0
int GameObject::set_nudge_vec(Vector const&) { return -3; }                          // 06cef3a0
int GameObject::set_strafe_dir(StrafeDir) { return -3; }                             // 06cef3a0
int GameObject::set_formation_leader(IObjRW*) { return -8; }                         // 06cef340
int GameObject::set_formation_offset(Vector const&) { return -8; }                   // 06cef340
int GameObject::add_formation_follower(IObjRW*) { return -8; }                       // 06cef340
int GameObject::remove_formation_follower(IObjRW*) { return -8; }                    // 06cef340
int GameObject::get_formation_follower_count() { return 0; }                         // 06d567c0
int GameObject::set_follow_leader(IObjRW*) { return -8; }                            // 06cef340
int GameObject::set_follow_offset(Vector const&) { return -8; }                      // 06cef340
int GameObject::add_follow_follower(IObjRW*) { return -8; }                          // 06cef340
int GameObject::remove_follow_follower(IObjRW*) { return -8; }                       // 06cef340
int GameObject::slot_34(unsigned short const*, unsigned, Vector const*, int) { return -3; }  // 06cef380
int GameObject::slot_38(unsigned short, int, int) { return -3; }                     // 06cef370
int GameObject::slot_3c(unsigned short const*, std::vector<IObjRW*> const&, int) { return -3; }  // 06cef370
int GameObject::slot_40(unsigned short const*, IObjRW*, int) { return -3; }          // 06cef370
int GameObject::slot_44(unsigned short const*, IObjRW*, unsigned short, int) { return -3; }  // 06cef380
int GameObject::set_target(IObjRW*, unsigned short, int) { return -3; }              // 06cef370
int GameObject::slot_4c(unsigned short const*, bool, int) { return -3; }             // 06cef370
int GameObject::slot_50(bool, bool, int) { return -3; }                              // 06cef370
int GameObject::slot_54(bool, int) { return -3; }                                    // 06cef350
int GameObject::slot_58(unsigned short, unsigned, int) { return -3; }                // 06cef370
int GameObject::slot_5c(int, unsigned const*, unsigned const*, int, RequestCB*) { return -3; }  // 06cef360
int GameObject::slot_60(int, unsigned const*, int) { return -3; }                    // 06cef370
int GameObject::go_tradelane(IObjInspect const*, IObjInspect const*, bool, float) { return -3; }  // 06cef380
int GameObject::stop_tradelane() { return -3; }                                      // 06cef390
int GameObject::slot_6c(int, int) { return -3; }                                     // 06cef350
int GameObject::slot_70(unsigned) { return -3; }                                     // 06cef3a0
int GameObject::slot_74(unsigned) { return -3; }                                     // 06cef3a0

// ---------------------------------------------------------------------------
// EqObj -- .text:06ce6620
// ---------------------------------------------------------------------------

// .text:06ce6620. GameObject's ctor (FUN_06cee360), then the three list heads
// (+0x60 DockQueue, +0x70 ServerEquips, +0x7c Fuses -- operator new(0x10 / 0xc /
// 0x44) self-linked sentinels, sizes zeroed), +0x6c CargoDecayTimer = 0
// (.text:06ce6668) and +0x88 bDeathFuseLit = 0 (.text:06ce66b0); then EqObj's
// three vtables (.rdata:06d65244 / 06d651c4 / 06d651b8).
EqObj::EqObj()
    : CargoDecayTimer(0.0f)
    , bDeathFuseLit(0)
{
}

// .text:06ce6710 (the body behind the deleting dtor .text:06ce66e0; ECX is the
// CBase sub-object at +0x0c). EqObj's vtables go back in first, so the
// clear_server_equip call below is EqObj's own. Order:
//   .text:06ce6746 .. 06ce6773  every DockQueue node erased (FUN_06d0f620);
//   .text:06ce677b              clear_server_equip (vt +0x1e0);
// then the member destructors in reverse declaration order -- Fuses (+0x7c,
// FUN_06cece50 per node), ServerEquips (+0x70), DockQueue (+0x60) -- and
// GameObject's destructor (FUN_06cee4a0).
EqObj::~EqObj()
{
    DockQueue.clear();
    clear_server_equip();
}

// FUN_06ce7c50 (.text:06ce7c50 .. .text:06ce7c72, RET 4). PORTED 2026-09-24
// (claude-code:w5-server-06cf), all 15 instructions: ECX = [this + 0x64] (the
// DockQueue head); walk from head->next until back at the head, comparing the
// node's +0xc (FDockRequest::ShipId) with the argument; a hit returns 1, the
// end of the list 0.
bool EqObj::is_ship_queued(unsigned int ShipId) const
{
    for (std::list<FDockRequest>::const_iterator It = DockQueue.begin(); It != DockQueue.end(); ++It)
    {
        if (It->ShipId == ShipId)
            return true;
    }
    return false;
}

CEqObj* EqObj::ceqobj() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    // Retail's own narrowing test on this family is the ObjectClass mask, not
    // RTTI: Ship::create checks (ObjectClass & 0x503) == 0x503, Loot::create
    // (ObjectClass & 0x803) == 0x803. Everything CEqObj-derived carries 0x103.
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x103u) != 0x103u)
        return nullptr;
    return static_cast<CEqObj*>(Obj);
}

// FUN_06ce83c0 (.text:06ce83c0, RET 0; only caller EqObj::destroy .text:06ce823a):
// the CBase Object under the (ObjectClass & 3) == 3 guard, then CSimple+0xb4.
// Retail dereferences the null the guard produces; so does this.
static unsigned int EqObj_OwnerPlayer(EqObj const* Obj)
{
    return Obj->csimple()->OwnerPlayer;
}

// FUN_06ce7e20 (.text:06ce7e20, RET 4).
bool EqObj::update(float Dt)
{
    // .text:06ce7e46 .. 06ce7e65 -- GameObject's gate (FUN_06cee9e0, direct).
    if (!GameObject::update(Dt))
        return false;

    // .text:06ce7e68 .. 06ce7fda -- the weapons-offline repair pass, only while
    // CEqObj+0x15c is set (ceqobj() is dereferenced without a null test).
    if (ceqobj()->bWeaponsOffline)
    {
        double const Now = Timing::read();               // .text:06ce7e92 / 06ce7e98
        DamageList Repair;                                // .text:06ce7ea5 list ctor
        Repair.Cause = static_cast<DamageCause>(0x1a);    // .text:06ce7ec1

        // .text:06ce7ee0 .. 06ce7f05 -- relative health <= 0.0 (TEST AH,0x41 /
        // JP skips > and unordered) and more than 60 s (DAT_06d65530, double)
        // since the last damage (TEST AH,0x41 / JNZ skips <= and unordered):
        // the root back at its max hit points.
        if (!(ceqobj()->get_relative_health() > 0.0) &&
            (Now - LastDamageTime) > 60.0)
        {
            Repair.add_damage_entry(SUBOBJ_ID_ROOT, ceqobj()->get_max_hit_pts(),
                                    DamageEntry::SubObjFate::None);
        }

        // .text:06ce7f43 .. 06ce7faf -- every ServerEquip whose equip is a
        // disabled launcher (CEquip vt +0x10 IsDisabled) that has not been hit
        // for more than 60 s gets its max hit points back (vt +0x38).
        for (std::list<ServerEquip*>::iterator It = ServerEquips.begin();
             It != ServerEquips.end(); ++It)
        {
            CEquip* const Equip = (*It)->Equip;
            if (CELauncher::cast(Equip) == nullptr)
                continue;
            if (!Equip->IsDisabled())
                continue;
            double const Since = Now - static_cast<SrvLauncher*>(*It)->LastDamageTime;
            if (!(Since > 60.0))
                continue;
            Repair.add_damage_entry(Equip->Id, Equip->GetMaxHitPoints(), DamageEntry::SubObjFate::None);
        }

        // .text:06ce7fb1 .. 06ce7fc2 -- only a non-empty list (Count, +0x08).
        if (Repair.Count != 0)
            apply_damage_list(Repair);                    // vt +0x188
    }

    // .text:06ce7fdd  CALL FUN_06ce7930 -- the dock-bay tick.
    process_dock_queue(Dt);

    // .text:06ce7fe5 .. 06ce7ffe -- every ServerEquip's update (slot 2, +0x08).
    for (std::list<ServerEquip*>::iterator It = ServerEquips.begin();
         It != ServerEquips.end(); ++It)
        (*It)->update(Dt);

    // .text:06ce8000 .. 06ce8029 -- FLD/FADD/FST into +0x6c, compared with 1.0f
    // (DAT_06d651a4; TEST AH,0x41 / JNZ skips <= and unordered).
    CargoDecayTimer = Dt + CargoDecayTimer;
    if (CargoDecayTimer > 1.0f)
    {
        decay_cargo(CargoDecayTimer);                     // vt +0x24c
        CargoDecayTimer = 0.0f;
    }

    // .text:06ce8030 .. 06ce8051 -- Fuse::Burn on every lit fuse.
    for (std::list<ServerFuse>::iterator It = Fuses.begin(); It != Fuses.end(); ++It)
        It->Burn(Dt);

    // .text:06ce8053 .. 06ce806f -- NEG/SBB/INC: !is_destroyed().
    return !is_destroyed();
}

// +0x158 -- .text:06ce8080 (RET 8).
bool EqObj::destroy(int Kind, unsigned int ObjId)
{
    // .text:06ce809c .. 06ce80b6 -- the first destroy only: latch +0x88.
    if (!bDeathFuseLit)
    {
        bDeathFuseLit = 1;

        if (csimple()->OwnerPlayer == 0)
        {
            // .text:06ce80d8 -- an AI object killed outright (kind 1) first
            // gets its death fuse, if the controller hands out a lifetime.
            if (Kind == 1)
            {
                // .text:06ce80e1 .. 06ce812a -- controller event 0x38.
                FDeathFuseEvent Ev;
                Ev.SystemId = 0;
                Ev.ObjId = 0;
                Ev.ObjType = 0;
                Ev.Lifetime = 0.0f;
                Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;     // FUN_06d06cd0
                Ev.ObjId = get_id();                                    // vt +0x20
                get_type(Ev.ObjType);                                   // vt +0x88
                Server_ControllerDispatch(0, pub::Controller::EMessage::DeathFuseLit, &Ev);

                // .text:06ce812f .. 06ce8141 -- Lifetime > 0.0f (TEST AH,0x41 /
                // JNZ skips <= and unordered).
                if (Ev.Lifetime > 0.0f)
                {
                    // .text:06ce8147 .. 06ce8171 -- light_fuse(ObjId,
                    // DEATH_COMM_FUSE, SUBOBJ_ID_NONE, 0.0f, Lifetime), vt +0x1e4.
                    if (light_fuse(ObjId, DEATH_COMM_FUSE, SUBOBJ_ID_NONE, 0.0f, Ev.Lifetime))
                    {
                        // .text:06ce8177 .. 06ce81c3 -- ceqobj() (no null test on
                        // the narrowing) -> get_behavior_interface; with a manager,
                        // {7, &get_id()} through its slot 0 (Common LAB_062d3f90,
                        // the port's virtual IBehaviorManager::Slot00).
                        IBehaviorManager* const Behavior = ceqobj()->get_behavior_interface();
                        if (Behavior != nullptr)
                        {
                            unsigned int SelfId = get_id();         // .text:06ce81a3 vt +0x20
                            FBehaviorManagerEvent Event;
                            Event.Type = 7;
                            Event.Data = &SelfId;
                            Behavior->Slot00(&Event);               // .text:06ce81c3
                        }

                        // .text:06ce81c5 .. 06ce81de -- once per DockQueue entry.
                        for (std::list<FDockRequest>::iterator It = DockQueue.begin();
                             It != DockQueue.end(); ++It)
                            OwnerSystem->remove_from_list_d4(this);

                        // .text:06ce81e0 .. 06ce81ff -- CEqObj::disconnect(IObjRW*)
                        // (CEqObj vt +0xc0).
                        ceqobj()->disconnect(static_cast<IObjRW*>(this));
                        // .text:06ce8205  XOR AL,AL -- not destroyed yet: the
                        // fuse will do it.
                        return false;
                    }
                }
            }
        }
        else if (!SinglePlayer() && Kind == 1 && is_player())
        {
            // .text:06ce821b .. 06ce825f -- a player's ship died in multiplayer:
            // control message 0x14 with the player's id to every active player
            // (FUN_06d19110), and ServerStats+0x10 counts it (.data:06d8f9a8).
            Server_BroadcastGroupControl(0x14, EqObj_OwnerPlayer(this));
            ++GServerStats.Field_0x10;
        }
    }

    // .text:06ce8264 .. 06ce827b -- CSimple::set_hit_pts(0) (vt +0x98).
    csimple()->set_hit_pts(0.0f);

    // .text:06ce8281 .. 06ce8339 -- controller event 0x39.
    FObjectDestroyedEvent Ev;
    Ev.ObjId = get_id();
    Ev.KillerId = ObjId;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.OwnerPlayer = csimple()->OwnerPlayer;
    Ev.Kind = Kind;
    Ev.Position = get_position();                                  // vt +0x00
    if (Kind == 1)
        collect_droppable_equip(Ev.DroppedEquip);                  // vt +0x1f4
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDestroyed, &Ev);

    // .text:06ce8340 .. 06ce8351
    OwnerSystem->on_object_destroyed(this, Kind);                  // FUN_06d0b160
    destroy_notify();                                              // vt +0x15c

    // .text:06ce8357 .. 06ce8395
    if (Kind == 1)
    {
        explode();                                                 // vt +0x1f0
        if (SinglePlayer() || csimple()->OwnerPlayer == 0)
            drop_loot(Ev.DroppedEquip);                            // vt +0x1f8
    }
    return true;
}

// +0x15c -- .text:06ce7c80 (RET 0).
void EqObj::destroy_notify()
{
    // .text:06ce7c83 .. 06ce7c9e -- StarSystem FUN_06d0c730(this), once per
    // DockQueue entry.
    for (std::list<FDockRequest>::iterator It = DockQueue.begin(); It != DockQueue.end(); ++It)
        OwnerSystem->remove_from_list_d4(this);

    // .text:06ce7ca0 .. 06ce7ce7 -- CEqObj::disconnect(this) (vt +0xc0) unless
    // this is a player's object in single player.
    if (!(SinglePlayer() && csimple()->OwnerPlayer != 0))
        ceqobj()->disconnect(static_cast<IObjRW*>(this));

    // .text:06ce7cf1  JMP FUN_06cee670
    GameObject::destroy_notify();
}

// +0x1bc -- .text:06ce7d00 (RET 0).
void EqObj::check_fuses()
{
    rebuild_server_equip();                                        // vt +0x1dc

    // .text:06ce7d11 .. 06ce7d7b -- the archetype's fuse list (Archetype::EqObj
    // +0x74 list, node {+8 Fuse, +0xc Value1, +0x10 Value2}): hull hit points <=
    // Value2 (TEST AH,0x41 / JP skips > and unordered) lights the fuse at Value1.
    CEqObj* const Ce = ceqobj();
    std::vector<Archetype::FGroupFuse> const& ArchFuses = *Ce->eqobjarch()->Fuses;
    for (std::vector<Archetype::FGroupFuse>::const_iterator It = ArchFuses.begin();
         It != ArchFuses.end(); ++It)
    {
        if (Ce->get_hit_pts() > It->Value2)
            continue;
        light_fuse(0, It->Fuse, SUBOBJ_ID_ROOT, It->Value1, -1.0f); // vt +0x1e4
    }

    // .text:06ce7d7d .. 06ce7e0e -- every arch group's own FuseIgnitions, same
    // test on the group's hit points, sub-object = the group's id.
    CArchGrpTraverser Walk;
    CArchGroupManager& Groups = Ce->ArchGroups;
    for (CArchGroup* Group = Groups.Traverse(Walk); Group != nullptr; Group = Groups.Traverse(Walk))
    {
        std::vector<Archetype::FGroupFuse> const& GroupFuses = *Group->FuseIgnitions().Items;
        for (std::vector<Archetype::FGroupFuse>::const_iterator It = GroupFuses.begin();
             It != GroupFuses.end(); ++It)
        {
            if (Group->GetHitPoints() > It->Value2)
                continue;
            light_fuse(0, It->Fuse, Group->Arch->Id, It->Value1, -1.0f);
        }
    }
}

// +0x1e4 -- .text:06cec4d0 (RET 0x14).
bool EqObj::light_fuse(unsigned int SourceId, ID_String const& FuseId, unsigned short SubObjId,
                       float AtT, float Lifetime)
{
    bool bFound = false;                                           // BL
    bool bIgnited = false;                                         // [ESP+0x13]

    // .text:06cec4ed .. 06cec566 -- a copy of this fuse is already burning:
    // re-ignite it. The Lifetime argument is not applied here; retail only
    // rewrites it to -1.0f (DAT_06d6572c) unless it already is -1.0f, and that
    // value is what the broadcast below carries.
    for (std::list<ServerFuse>::iterator It = Fuses.begin(); It != Fuses.end() && !bFound; ++It)
    {
        if (It->Name().Id != FuseId.Id)
            continue;
        bFound = true;
        bIgnited = It->IgniteAt(SourceId, SubObjId, AtT);
        if (Lifetime != -1.0f)
            Lifetime = -1.0f;
    }

    if (!bFound)
    {
        // .text:06cec570 .. 06cec594 -- the prototype from ServerFuseDB
        // (FUN_06cfd390 / FUN_06d15d10 map find). Retail reads the value slot of
        // whatever node find() returns and tests it for null; the port reads it
        // only for a real node.
        std::map<unsigned, ServerFuse*>& Db = ServerFuseDB::Get();
        std::map<unsigned, ServerFuse*>::iterator Found = Db.find(FuseId.Id);
        ServerFuse* const Proto = (Found != Db.end()) ? Found->second : nullptr;
        if (Proto == nullptr)
        {
            // .text:06cec6d6 .. 06cec703
            FDUMP(0x100002, "%s(%d) : *** WARNING: Fuse(%s) not found in FuseDB. Aborting.",
                  "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp", 0xc73, FuseId.get_string());
        }
        else
        {
            bFound = true;
            bIgnited = true;                                       // .text:06cec5a1 [ESP+0x13] = 1

            // .text:06cec59c .. 06cec5f0 -- a stack copy, list insert at end()
            // (FUN_06cecdf0), the stack copy destroyed.
            {
                ServerFuse Copy(*Proto);
                Fuses.push_back(Copy);
            }
            ServerFuse& Lit = Fuses.back();                        // .text:06cec5f6 .. 06cec5ff

            Lit.SetOwner(this);                                    // FUN_06cfd1e0
            Lit.RandomizeActions();

            if (!bDeathFuseLit && Lit.bHasDestroyRoot)
            {
                // .text:06cec623 .. 06cec66a -- the first fuse that destroys the
                // root: controller event 0x38.
                bDeathFuseLit = 1;
                FDeathFuseEvent Ev;
                Ev.SystemId = 0;
                Ev.ObjId = 0;
                Ev.ObjType = 0;
                Ev.Lifetime = 0.0f;
                Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
                Ev.ObjId = get_id();
                get_type(Ev.ObjType);
                Server_ControllerDispatch(0, pub::Controller::EMessage::DeathFuseLit, &Ev);

                // .text:06cec66f .. 06cec68e -- the fuse's own lifetime below the
                // controller's (TEST AH,5 / JP skips >= and unordered): take the
                // controller's, and it replaces the Lifetime argument as well.
                if (Lit.Lifetime() < Ev.Lifetime)
                {
                    Lifetime = Ev.Lifetime;
                    Lit.OverrideLifetime(Ev.Lifetime);
                }
            }
            else if (Lifetime != -1.0f)
            {
                // .text:06cec690 .. 06cec6ae
                Lit.OverrideLifetime(Lifetime);
            }

            // .text:06cec6b4 .. 06cec6ce -- the result is not kept.
            Lit.IgniteAt(SourceId, SubObjId, AtT);
        }
    }

    // .text:06cec707 .. 06cec79e -- tell the system (FUN_06d0e750).
    if (OwnerSystem != nullptr && bIgnited)
    {
        FServerBurnFuseInfo Info;
        Info.SourceId = SourceId;
        Info.ObjectId = get_id();
        Info.FuseId = FuseId.Id;
        Info.SubObjId = SubObjId;
        Info.Pad_0x0e = 0;
        Info.AtT = AtT;
        Info.Lifetime = Lifetime;
        Info.bLit = 1;
        Info.Pad_0x19[0] = 0;
        Info.Pad_0x19[1] = 0;
        Info.Pad_0x19[2] = 0;
        OwnerSystem->broadcast_fuse(Info);
    }
    return bFound;
}

// +0x1e8 -- .text:06cec7f0 (RET 0xc).
bool EqObj::unlight_fuse(ID_String const& FuseId, unsigned short SubObjId, float AtT)
{
    // .text:06cec7f8 .. 06cec8b3 -- the first burning copy of FuseId only (the
    // loop ends once one matched; retail keeps the flag in the SubObjId
    // argument's stack slot).
    bool bFound = false;
    for (std::list<ServerFuse>::iterator It = Fuses.begin(); It != Fuses.end() && !bFound; ++It)
    {
        if (It->Name().Id != FuseId.Id)
            continue;
        bFound = true;
        if (!It->UnBurn(SubObjId, AtT))
            continue;
        if (OwnerSystem == nullptr)
            continue;

        // .text:06cec84b .. 06cec8a4 -- {0, id, fuse, SubObjId, AtT, -1.0f, 0}.
        FServerBurnFuseInfo Info;
        Info.SourceId = 0;
        Info.ObjectId = get_id();
        Info.FuseId = FuseId.Id;
        Info.SubObjId = SubObjId;
        Info.Pad_0x0e = 0;
        Info.AtT = AtT;
        Info.Lifetime = -1.0f;
        Info.bLit = 0;
        Info.Pad_0x19[0] = 0;
        Info.Pad_0x19[1] = 0;
        Info.Pad_0x19[2] = 0;
        OwnerSystem->broadcast_fuse(Info);
    }
    return bFound;
}

// +0x1ec -- .text:06cec8d0 (RET 0). A death fuse (Fuse+0x0c) that is not spent.
// IObjInspect slot 14 (+0x38) -- .text:06ce6890, a two-instruction tail jump
// through vt +0x1ec. Retail's slot is not const-qualified; the const_cast only
// bridges the port's const IObjInspect declaration.
bool EqObj::is_dying() const
{
    return const_cast<EqObj*>(this)->is_death_fuse_burning();
}

bool EqObj::is_death_fuse_burning()
{
    for (std::list<ServerFuse>::iterator It = Fuses.begin(); It != Fuses.end(); ++It)
    {
        if (It->IsDeathFuse() && !It->IsSpent())
            return true;
    }
    return false;
}

// +0x188 -- .text:06ceb330 (RET 4).
void EqObj::apply_damage_list(DamageList& Damage)
{
    // .text:06ceb338 .. 06ceb38d -- re-entry is reported, not refused.
    if (is_in_damage_callback())
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: ID(0x%X) Arch(%s): Tried to apply damage of type %s within another damage callback.",
              "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp", 0x9d7,
              get_id(), Object->get_archetype()->Nickname.get_string(),
              DamageList::DmgCauseToString(Damage.Cause));
    }

    set_in_damage_callback(true);                                  // vt +0x190

    // .text:06ceb39c .. 06ceb3d1 -- the time of the last damage, while the hull
    // is still above 0.0 (DAT_06d65538; TEST AH,0x41 / JNZ skips <=).
    if (ceqobj()->get_relative_health() > 0.0)
        LastDamageTime = Timing::read();

    // .text:06ceb3d4 .. 06ceb4bf -- a non-empty list only.
    if (Damage.Count != 0)
    {
        for (std::list<DamageEntry>::iterator It = Damage.Entries.begin();
             It != Damage.Entries.end(); ++It)
        {
            unsigned short const Id = It->SubObjId;
            if (Id == SUBOBJ_ID_ROOT)
                apply_root_entry(Damage, *It);                     // vt +0x234
            else if (Id == SUBOBJ_ID_POWER)
                apply_power_entry(Damage, *It);                    // vt +0x23c
            else if (SubObjectID::IsArchGroupID(Id))
                apply_group_entry(Damage, *It);                    // vt +0x240
            else if (SubObjectID::IsEquipID(Id))
                apply_equip_entry(Damage, *It);                    // vt +0x238
        }

        // .text:06ceb47e .. 06ceb4b4 -- outside single player the dead
        // sub-objects go at once: CEqObj::destroy_sub_objs(Damage, false)
        // (vt +0xfc) and CEquipManager::CleanUp.
        if (!SinglePlayer())
        {
            CEqObj* const Ce = ceqobj();
            Ce->destroy_sub_objs(Damage, false);
            Ce->EquipManager.CleanUp();
        }

        StarSystem_DamageObject(OwnerSystem, this, &Damage);      // FUN_06d0b800
    }

    // .text:06ceb4c4 .. 06ceb52a -- destroyed, not blocked, no death fuse
    // holding a cause below 0x10, not already destroyed, and CEqObj+0x15c clear.
    if (Damage.bDestroyed &&
        !is_damage_blocked(Damage) &&                              // vt +0x1b8
        !(is_death_fuse_burning() && static_cast<int>(Damage.Cause) < 0x10) &&
        !is_destroyed() &&                                         // vt +0x160
        !ceqobj()->bWeaponsOffline)
    {
        destroy(1, Damage.InflictorId);                            // vt +0x158
    }

    set_in_damage_callback(false);                                 // vt +0x190
}

// The light/unlight pass apply_root_entry (.text:06ceb6e0 .. 06ceb785) and
// apply_group_entry (.text:06cebc55 .. 06cebcf6) share, over one fuse list:
// damage (hit points dropping from Before to After) lights every fuse whose
// threshold Value2 lies in [After, Before); healing (Before <= After) unlights
// every fuse whose threshold lies in [Before, After). Before is re-read for
// every node (the light_fuse / unlight_fuse calls may change it).
//   damage:  Before > After (TEST AH,0x41 / JNZ), Before > Value2 (same),
//            !(Value2 < After) (TEST AH,1 / JNZ) -> light_fuse(InflictorId,
//            Fuse, SubObjId, Value1, -1.0f), vt +0x1e4;
//   healing: Value2 < After (TEST AH,5 / JP), Before <= Value2 (TEST AH,0x41 /
//            JP) -> unlight_fuse(Fuse, SubObjId, Value1), vt +0x1e8.
template <typename FReadHitPts>
static void EqObj_SweepFuses(EqObj* Obj, std::vector<Archetype::FGroupFuse> const& List,
                             FReadHitPts ReadHitPts, float After, unsigned short SubObjId,
                             unsigned int InflictorId)
{
    for (std::vector<Archetype::FGroupFuse>::const_iterator It = List.begin(); It != List.end(); ++It)
    {
        if (ReadHitPts() > After)
        {
            if (!(ReadHitPts() > It->Value2))
                continue;
            // TEST AH,1 / JNZ skips "less" and unordered alike.
            if (!(It->Value2 >= After))
                continue;
            Obj->light_fuse(InflictorId, It->Fuse, SubObjId, It->Value1, -1.0f);
        }
        else
        {
            if (!(It->Value2 < After))
                continue;
            // TEST AH,0x41 / JP skips "greater" and unordered alike.
            if (!(ReadHitPts() <= It->Value2))
                continue;
            Obj->unlight_fuse(It->Fuse, SubObjId, It->Value1);
        }
    }
}

// The behavior-manager half of the four apply_*_entry slots: with a manager,
// the controller event 0xd record re-ordered into FBehaviorManagerEvent {0,
// &Record} and handed to its slot 0 (Common LAB_062d3f90, the port's
// virtual IBehaviorManager::Slot00). EquipType and CollisionGroupType are
// passed in because each site writes one of them from the event and zeroes the
// other with an immediate (root .text:06ceb664 / 06ceb668, equip .text:06ceb995,
// group .text:06cebc13, power .text:06cebe5d).
static void EqObj_NotifyBehaviorOfDamage(IBehaviorManager* Behavior, FSubObjDamageEvent const& Ev,
                                         unsigned int EquipType, unsigned int CollisionGroupType)
{
    if (Behavior == nullptr)
        return;
    FBehaviorDamageEvent Record;
    Record.SystemId = Ev.SystemId;
    Record.InflictorId = Ev.InflictorId;
    Record.ObjId = Ev.ObjId;
    Record.EquipType = EquipType;
    Record.CollisionGroupType = CollisionGroupType;
    Record.Cause = static_cast<int>(Ev.Cause);
    Record.Damage = Ev.Damage;
    Record.RelativeDamage = Ev.RelativeDamage;
    Record.RelativeHealth = Ev.RelativeHealth;
    FBehaviorManagerEvent Event;
    Event.Type = 0;
    Event.Data = &Record;
    Behavior->Slot00(&Event);
}

// +0x234 -- .text:06ceb550 (RET 8).
void EqObj::apply_root_entry(DamageList const& Damage, DamageEntry const& Entry)
{
    CEqObj* const Ce = ceqobj();

    // .text:06ceb573 .. 06ceb607 -- controller event 0xd.
    FSubObjDamageEvent Ev;
    Ev.ObjId = 0;
    Ev.SystemId = 0;
    Ev.InflictorId = 0;
    Ev.EquipType = 0;
    Ev.CollisionGroupType = 0;
    Ev.Cause = static_cast<DamageCause>(0);
    Ev.Damage = 0.0f;
    Ev.RelativeDamage = 0.0f;
    Ev.RelativeHealth = 0.0f;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.InflictorId = Damage.InflictorId;
    Ev.ObjId = get_id();
    Ev.Cause = Damage.Cause;
    Ev.Damage = Ce->get_hit_pts() - Entry.HitPts;
    // Root::HitPts (+0x1c), no zero test.
    Ev.RelativeDamage = Ev.Damage / Ce->Archetype->HitPts;
    Ev.RelativeHealth = Entry.HitPts / Ce->Archetype->HitPts;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Ev);

    // .text:06ceb60f .. 06ceb670 -- the behavior manager hears {0, &Record},
    // Record = {SystemId, InflictorId, ObjId, EquipType 0, CollisionGroupType 0,
    // Cause, Damage, RelativeDamage, RelativeHealth}.
    EqObj_NotifyBehaviorOfDamage(Ce->get_behavior_interface(), Ev, 0, 0);

    // .text:06ceb672 .. 06ceb785 -- the archetype's fuses, when the hull's hit
    // points change (TEST AH,0x44 / JNP skips equal).
    if (Ce->get_hit_pts() != Entry.HitPts)
    {
        EqObj_SweepFuses(this, *ceqobj()->eqobjarch()->Fuses,
                         [Ce]() { return Ce->get_hit_pts(); },
                         Entry.HitPts, SUBOBJ_ID_ROOT, Damage.InflictorId);
    }

    // .text:06ceb78f  CALL FUN_06ceef70 -- direct, not through the vtable.
    GameObject::apply_root_damage(Entry);
}

// +0x238 -- .text:06ceb7b0 (RET 8).
void EqObj::apply_equip_entry(DamageList const& Damage, DamageEntry const& Entry)
{
    CEqObj* const Ce = ceqobj();

    // .text:06ceb7dc .. 06ceb80a
    CEquip* const Equip = Ce->EquipManager.FindByID(Entry.SubObjId);
    if (Equip == nullptr || Equip->IsDestroyed())
        return;

    // .text:06ceb810 .. 06ceb88d -- a live launcher (relative health > 0.0,
    // TEST AH,0x41 / JNZ skips <=) stamps the time on its ServerEquip, found by
    // a second FindByID and a walk of the +0x70 list.
    if (Equip->GetRelativeHealth() > 0.0 && CELauncher::cast(Equip) != nullptr)
    {
        CEquip* const Again = ceqobj()->EquipManager.FindByID(Entry.SubObjId);
        for (std::list<ServerEquip*>::iterator It = ServerEquips.begin();
             It != ServerEquips.end(); ++It)
        {
            if ((*It)->Equip != Again)
                continue;
            SrvLauncher* const Launcher = static_cast<SrvLauncher*>(*It);
            if (Launcher != nullptr)
                Launcher->LastDamageTime = Timing::read();
            break;
        }
    }

    // .text:06ceb892 .. 06ceb951 -- controller event 0xd.
    FSubObjDamageEvent Ev;
    Ev.ObjId = 0;
    Ev.SystemId = 0;
    Ev.InflictorId = 0;
    Ev.EquipType = 0;
    Ev.CollisionGroupType = 0;
    Ev.Cause = static_cast<DamageCause>(0);
    Ev.Damage = 0.0f;
    Ev.RelativeDamage = 0.0f;
    Ev.RelativeHealth = 0.0f;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.InflictorId = Damage.InflictorId;
    Ev.ObjId = get_id();
    Ev.Cause = Damage.Cause;
    Ev.EquipType = Equip->Type;
    Ev.Damage = Equip->GetHitPoints() - Entry.HitPts;             // vt +0x3c
    float const Max = Equip->GetMaxHitPoints();                    // vt +0x38
    // TEST AH,0x44 / JNP: a zero max gives 0 for both ratios.
    if (Max != 0.0f)
        Ev.RelativeDamage = Ev.Damage / Max;
    else
        Ev.RelativeDamage = 0.0f;
    if (Max != 0.0f)
        Ev.RelativeHealth = Entry.HitPts / Max;
    else
        Ev.RelativeHealth = 0.0f;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Ev);

    // .text:06ceb956 .. 06ceb9c2 -- behavior record as apply_root_entry's, with
    // EquipType (.text:06ceb981) and CollisionGroupType 0.
    EqObj_NotifyBehaviorOfDamage(Ce->get_behavior_interface(), Ev, Ev.EquipType, 0);

    // .text:06ceb9c4 .. 06ceb9dd -- a shield-battery list (cause 0x13) boosts
    // the shield instead of setting its hit points.
    if (static_cast<int>(Damage.Cause) == 0x13)
    {
        static_cast<CEShield*>(Equip)->ShieldBatteryBoost(Entry.HitPts);
        return;
    }

    // .text:06ceb9e0 .. 06ceba23 -- a cargo stack (type 0x10000, Count +0x28 > 1
    // unsigned) loses one unit when its entry reaches 0 (TEST AH,0x44 / JP
    // skips unequal), otherwise takes the hit points.
    if (Equip->Type == 0x10000 && static_cast<CECargo*>(Equip)->Count > 1)
    {
        if (Entry.HitPts == 0.0f)
        {
            static_cast<CECargo*>(Equip)->RemoveFromStack(1);
            return;
        }
        Equip->SetHitPoints(Entry.HitPts);                         // vt +0x40
        return;
    }

    Equip->SetHitPoints(Entry.HitPts);                             // vt +0x40
}

// +0x23c -- .text:06cebd40 (RET 8).
void EqObj::apply_power_entry(DamageList const& Damage, DamageEntry const& Entry)
{
    CEqObj* const Ce = ceqobj();

    // .text:06cebd62 .. 06cebdf7 -- controller event 0xd; EquipType 0x40000,
    // the ratios over CEqObj::MaxPower (+0x19c) with no zero test (FDIVR).
    FSubObjDamageEvent Ev;
    Ev.ObjId = 0;
    Ev.SystemId = 0;
    Ev.InflictorId = 0;
    Ev.EquipType = 0;
    Ev.CollisionGroupType = 0;
    Ev.Cause = static_cast<DamageCause>(0);
    Ev.Damage = 0.0f;
    Ev.RelativeDamage = 0.0f;
    Ev.RelativeHealth = 0.0f;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.InflictorId = Damage.InflictorId;
    Ev.ObjId = get_id();
    Ev.EquipType = 0x40000;
    Ev.Cause = Damage.Cause;
    Ev.Damage = Ce->Power - Entry.HitPts;
    Ev.RelativeDamage = Ev.Damage / Ce->MaxPower;
    Ev.RelativeHealth = Entry.HitPts / Ce->MaxPower;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Ev);

    // .text:06cebdff .. 06cebe65 -- behavior record with EquipType 0x40000 (from
    // the event, .text:06cebe20) and CollisionGroupType 0.
    EqObj_NotifyBehaviorOfDamage(Ce->get_behavior_interface(), Ev, Ev.EquipType, 0);

    Ce->Power = Entry.HitPts;                                      // .text:06cebe6b
}

// +0x240 -- .text:06ceba40 (RET 8).
void EqObj::apply_group_entry(DamageList const& Damage, DamageEntry& Entry)
{
    CEqObj* const Ce = ceqobj();

    // .text:06ceba6b .. 06ceba85 -- no null test on the group.
    CArchGroup* const Group = Ce->ArchGroups.FindByID(Entry.SubObjId);

    // .text:06ceba87 .. 06cebadf -- a group that dies empties the dock queue
    // (FUN_06cf7720, list erase(begin, end)); a persistent fate is reported.
    if (Entry.Fate != DamageEntry::SubObjFate::None)
    {
        if (ceqobj() != nullptr)
            DockQueue.clear();
        if (static_cast<int>(Entry.Fate) == 4)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                  "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp", 0xaf5,
                  "Persistent CArchGroups not supported.");
        }
    }

    // .text:06cebae2 .. 06cebbac -- controller event 0xd.
    FSubObjDamageEvent Ev;
    Ev.ObjId = 0;
    Ev.SystemId = 0;
    Ev.InflictorId = 0;
    Ev.EquipType = 0;
    Ev.CollisionGroupType = 0;
    Ev.Cause = static_cast<DamageCause>(0);
    Ev.Damage = 0.0f;
    Ev.RelativeDamage = 0.0f;
    Ev.RelativeHealth = 0.0f;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.InflictorId = Damage.InflictorId;
    Ev.ObjId = get_id();
    Ev.CollisionGroupType = Group->GetCollisionGroupType();
    Ev.Cause = Damage.Cause;
    Ev.Damage = Group->GetHitPoints() - Entry.HitPts;
    // .text:06cebb5c CArchGroup::GetMaxHitPoints stays in ST0 unrounded: both
    // zero tests (06cebb62 / 06cebb83 FCOM float 0.0, TEST AH,0x44 / JNP ->
    // zero only when equal) and both FDIV ST0,ST1 use it; only the two
    // quotients are stored as float (06cebb75 / 06cebb95).
    double const Max = Group->GetMaxHitPoints();
    if (Max != 0.0)
        Ev.RelativeDamage = (float)(Ev.Damage / Max);
    else
        Ev.RelativeDamage = 0.0f;
    if (Max != 0.0)
        Ev.RelativeHealth = (float)(Entry.HitPts / Max);
    else
        Ev.RelativeHealth = 0.0f;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Ev);

    // .text:06cebbb1 .. 06cebc1b -- behavior record with EquipType 0 and the
    // event's CollisionGroupType (.text:06cebbde).
    EqObj_NotifyBehaviorOfDamage(Ce->get_behavior_interface(), Ev, 0, Ev.CollisionGroupType);

    // .text:06cebc1d .. 06cebcf6 -- the group's fuses when its hit points change.
    if (Group->GetHitPoints() != Entry.HitPts)
    {
        EqObj_SweepFuses(this, *Group->FuseIgnitions().Items,
                         [Group]() { return Group->GetHitPoints(); },
                         Entry.HitPts, Group->Arch->Id, Damage.InflictorId);
    }

    // .text:06cebd02 .. 06cebd1a -- a burning death fuse keeps low-cause groups
    // attached: the entry's fate is cleared.
    if (is_death_fuse_burning() && static_cast<int>(Damage.Cause) < 0x10)
        Entry.Fate = DamageEntry::SubObjFate::None;

    Group->SetHitPoints(Entry.HitPts);                             // .text:06cebd23
}

// ---------------------------------------------------------------------------
// EqObj helpers shared by the damage / loot slots below.
// ---------------------------------------------------------------------------

// The "is this object a connected player's CURRENT ship" test retail inlines
// at every difficulty-scale site (damage_hull .text:06cea3be .. 06cea412,
// damage_equip .text:06cea565 .. 06cea5b7, damage_shield .text:06cea7c8 ..
// 06cea81f, damage_group .text:06ceaba6 .. 06ceabf8, damage_power
// .text:06ceb07b .. 06ceb0ce): get_id() (vt +0x20) first, then CSimple+0xb4
// through the (ObjectClass & 3) == 3 guard (the null it can produce is
// dereferenced, as in retail), PlayerDB::is_valid's three conditions inlined
// (owner > 0, owner - 1 < MaxPlayers, PlayerData+0x348 != 0), then
// PlayerData+0x3e4 against the id. Null when any of them fails.
static PlayerData* EqObj_PlayerOfShip(EqObj const* Obj)
{
    unsigned int const Id = Obj->get_id();
    unsigned int const Owner = Obj->csimple()->OwnerPlayer;
    if (!Players.is_valid(Owner))
        return nullptr;
    PlayerData& Data = Players[Owner];
    if (Data.ship_object_id != Id)
        return nullptr;
    return &Data;
}

// The raw Object under the (ObjectClass & 0x303) == 0x303 test -- retail's
// inlined CSolar narrowing at the loot-visit sites (.text:06ce8fed,
// .text:06ce9cc7, .text:06cea01b, ...). Null for everything that is not a solar.
static CSolar* EqObj_Solar(EqObj const* Obj)
{
    CObject* const Raw = Obj->cobj();
    if (Raw == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Raw->ObjectClass) & 0x303u) != 0x303u)
        return nullptr;
    return static_cast<CSolar*>(Raw);
}

// .data:DAT_06d8d6d4 -- EqObj.cpp's own lazily fetched math engine
// (.text:06ce7a87 / .text:06ce6a01 `MOV EAX,[DAT_06d8d6d4]` / JNZ / CALL
// FUN_06d5ff70 / store).
static I3DMathEngine* GEqObjMathEngine = nullptr;

// The ServerEquip listening to Equip: EqObj+0x74 (ServerEquips' head) walked
// from head->next, node+8's +4 compared with the CEquip (.text:06ce6962 ..
// 06ce6993, .text:06ce6d... in the siblings). The walk steps with
// FUN_06d1b490 (the list iterator's post-increment). End of list -> nullptr
// and the "not found" flag; retail tells the two apart only by the jump taken.
static bool EqObj_FindServerEquip(std::list<ServerEquip*>& ServerEquips, CEquip const* Equip,
                                  ServerEquip*& Out)
{
    for (std::list<ServerEquip*>::iterator It = ServerEquips.begin(); It != ServerEquips.end(); ++It)
    {
        if ((*It)->Equip == Equip)
        {
            Out = *It;
            return true;
        }
    }
    Out = nullptr;
    return false;
}

// IObjAffect slot 13 (+0x34) -- FUN_06ce68a0 (.text:06ce68a0, RET 0x10).
// Every requested equip is looked up (CEquipManager::FindByID on CEqObj+0xe4)
// and handed to its ServerEquip, which retail calls as a SrvLauncher through
// slots 3/4/5 whatever it really is. Unguided guns that fired are collected and
// announced to the system in one StarSystem::notify_fire_weapon.
int EqObj::slot_34(unsigned short const* EquipIds, unsigned Count, Vector const* TargetPos,
                   int SourceClient)
{
    // .text:06ce68b8 .. 06ce68db -- [this+0x24] (OwnerSystem) null or
    // is_destroyed (vt +0x160): -5.
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    // .text:06ce68e1 .. 06ce6922 -- the fired-id vector, Result = 0, the lock
    // target (+0x14) = 0, its sub-id (+0x28) = SUBOBJ_ID_NONE.
    std::vector<unsigned short> Fired;
    int Result = 0;
    CSimple* LockTarget = nullptr;
    unsigned short LockSubId = SUBOBJ_ID_NONE;

    for (unsigned Index = 0; Index < Count; ++Index)                // .text:06ce69a8 JC
    {
        // .text:06ce6930 .. 06ce695c -- ceqobj() through the (& 0x103) guard,
        // no null test, then FindByID.
        CEquip* const Found = ceqobj()->EquipManager.FindByID(EquipIds[Index]);

        ServerEquip* Listener = nullptr;
        if (!EqObj_FindServerEquip(ServerEquips, Found, Listener) || Listener == nullptr)
        {
            // .text:06ce6995 / 06ce6a9b -- no listener: -2, next id.
            Result = -2;
            continue;
        }

        // .text:06ce6aa1 .. 06ce6aa6 -- a listener whose equip is gone: next id.
        CEquip* const Equip = Listener->Equip;
        if (Equip == nullptr)
            continue;

        SrvLauncher* const Launcher = static_cast<SrvLauncher*>(Listener);
        bool bFired;
        if (SourceClient != -1 && SourceClient != 0)
        {
            // .text:06ce6ac4 .. 06ce6ad0 -- a client's shot: slot 5 fire_unchecked.
            bFired = Launcher->fire_unchecked(*TargetPos);
        }
        else
        {
            // .text:06ce6b44 .. 06ce6b5f -- CELauncher::cast (not null-tested);
            // with an aim point and a projectile without ForceGunOri (+0x79),
            // slot 3 fire(*TargetPos) and on to the fired test.
            CELauncher* const Cast = CELauncher::cast(Equip);
            if (TargetPos != nullptr &&
                !static_cast<Archetype::Projectile const*>(Cast->Projectile)->bForceGunOri)
            {
                bFired = Launcher->fire(*TargetPos);
            }
            else
            {
                // .text:06ce6b6e .. 06ce6b7d -- slot 4 fire_forward(SourceClient);
                // its answer is not read, next id.
                Launcher->fire_forward(SourceClient);
                continue;
            }
        }

        // .text:06ce6ad3 .. 06ce6afd -- only a gun that fired an unguided
        // munition is collected.
        if (!bFired)
            continue;
        CEGun* const Gun = CEGun::cast(Equip);
        if (Gun == nullptr)
            continue;
        if (Gun->MunitionArch()->Seeker != 0)
            continue;

        // .text:06ce6b03 .. 06ce6b1d -- vector<unsigned short>::insert(end, 1, id)
        // (Common import).
        Fired.insert(Fired.end(), 1, Gun->Id);

        // .text:06ce6b23 .. 06ce6b39 -- until one is found, CEGun::GetTarget.
        if (LockTarget == nullptr)
            Gun->GetTarget(LockTarget, LockSubId);
    }

    // .text:06ce69b5 .. 06ce69cb -- nothing collected: straight to the cleanup.
    if (!Fired.empty())
    {
        // .text:06ce69d1 .. 06ce69e8 -- the aim point, read without a null test.
        Vector Aim = *TargetPos;

        // .text:06ce69ec .. 06ce6a4b -- outside single player with a lock
        // target: its Transform (target+8, 12 dwords REP MOVSD) and the lazily
        // fetched EqObj.cpp math engine (DAT_06d8d6d4), InverseTransform (+0x3c)
        // of *TargetPos into the target's frame.
        if (!SinglePlayer() && LockTarget != nullptr)
        {
            FTransform const TargetXform = *reinterpret_cast<FTransform const*>(&LockTarget->Orientation);
            if (GEqObjMathEngine == nullptr)
                GEqObjMathEngine = Server_Acquire3DMathEngine();
            FVector Local;
            GEqObjMathEngine->InverseTransform(Local, TargetXform,
                                               *reinterpret_cast<FVector const*>(TargetPos));
            Aim = reinterpret_cast<Vector const&>(Local);
        }

        // .text:06ce6a4f .. 06ce6a79 -- get_id (vt +0x20), then
        // OwnerSystem->FUN_06d0e2e0(&id, Fired, &Aim, SourceClient).
        unsigned int const ObjId = get_id();
        OwnerSystem->notify_fire_weapon(ObjId, Fired, Aim, SourceClient);
    }

    // .text:06ce6a82 .. 06ce6a91 -- the vector's buffer freed; Result.
    return Result;
}

// IObjRW::cobject() (vt +0x150) under the (ObjectClass & 0x803) == 0x803 test --
// the loot narrowing both tractor slots inline (.text:06ce6c4c .. 06ce6c6d,
// .text:06ce6e3c .. 06ce6e5b). Null when it is not loot.
static CLoot* EqObj_LootOf(IObjRW* Obj)
{
    CObject const* const Raw = Obj->cobject();
    if (Raw == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Raw->ObjectClass) & 0x803u) != 0x803u)
        return nullptr;
    return const_cast<CLoot*>(static_cast<CLoot const*>(Raw));
}

// IObjAffect slot 16 (+0x40) -- FUN_06ce6ba0 (.text:06ce6ba0, RET 0xc).
int EqObj::slot_40(unsigned short const* EquipId, IObjRW* Target, int SourceClient)
{
    // .text:06ce6bb8 .. 06ce6bdc -- no system / destroyed: -5.
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    // .text:06ce6be2 .. 06ce6be8 -- no target: -4.
    if (Target == nullptr)
        return -4;

    // .text:06ce6bee .. 06ce6bf9 -- Result (the Target slot, reused) = 0; an AI
    // request (-1) is treated as the server's own (0).
    int Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    // .text:06ce6bfd .. 06ce6c0b -- single player lets a client's request
    // straight through to the broadcast; otherwise the tractor is driven here.
    if (!SinglePlayer() || SourceClient == 0)
    {
        // .text:06ce6c11 .. 06ce6c40 -- FindByID on ceqobj() (no null test).
        CEquip* const Found = ceqobj()->EquipManager.FindByID(*EquipId);
        if (Found == nullptr)
        {
            Result = -2;                                            // .text:06ce6c84
        }
        else
        {
            // .text:06ce6c4c .. 06ce6c82 -- CETractor::AddTarget on the equip as
            // found (retail does not cast it); any refusal is -5.
            if (static_cast<CETractor*>(Found)->AddTarget(EqObj_LootOf(Target)) != TractorOk)
                Result = -5;
        }

        // .text:06ce6c8c .. 06ce6c97 -- the server's own request stops here
        // unless the tractor took.
        if (SourceClient == 0 && Result != 0)
            return Result;
    }

    // .text:06ce6c99 .. 06ce6ce4 -- XTractorObjects {get_id(), *EquipId,
    // {Target->get_id()}}: the id vector grown by FUN_06cecbd0 (insert at end, 1).
    XTractorObjects Info;
    Info.ObjectId = get_id();
    Info.TractorEquipId = *EquipId;
    Info.TargetIds.insert(Info.TargetIds.end(), 1, Target->get_id());

    // .text:06ce6ce9 .. 06ce6cfa -- OwnerSystem->FUN_06d0e7a0(&SourceClient, &Info).
    unsigned int const SkipPlayerId = static_cast<unsigned int>(SourceClient);
    OwnerSystem->send_tractor_objects(SkipPlayerId, Info);

    // .text:06ce6cff .. 06ce6d21 -- the buffer freed; Result.
    return Result;
}

// IObjAffect slot 15 (+0x3c) -- FUN_06ce6d80 (.text:06ce6d80, RET 0xc).
int EqObj::slot_3c(unsigned short const* EquipId, std::vector<IObjRW*> const& Targets,
                   int SourceClient)
{
    // .text:06ce6d98 .. 06ce6dbb -- no system / destroyed: -5.
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    // .text:06ce6dc1 .. 06ce6dcc
    int Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    // .text:06ce6dd2 .. 06ce6de4
    if (!SinglePlayer() || SourceClient == 0)
    {
        // .text:06ce6dea .. 06ce6e28 -- FindByID, then CETractor::cast.
        CETractor* const Tractor = CETractor::cast(ceqobj()->EquipManager.FindByID(*EquipId));
        if (Tractor == nullptr)
        {
            Result = -2;                                            // .text:06ce6e86
        }
        else
        {
            // .text:06ce6e2a .. 06ce6e7a -- every non-null target through
            // AddTarget; the ones that took (0) are counted. None: -4.
            int Took = 0;
            for (std::vector<IObjRW*>::const_iterator It = Targets.begin(); It != Targets.end(); ++It)
            {
                if (*It == nullptr)
                    continue;
                if (Tractor->AddTarget(EqObj_LootOf(*It)) == TractorOk)
                    ++Took;
            }
            if (Took == 0)
                Result = -4;                                        // .text:06ce6e7c
        }

        // .text:06ce6e8e .. 06ce6e98
        if (SourceClient == 0 && Result != 0)
            return Result;
    }

    // .text:06ce6ea2 .. 06ce6f08 -- XTractorObjects {get_id(), *EquipId, every
    // target's get_id()} -- here without a null test on the targets.
    XTractorObjects Info;
    Info.ObjectId = get_id();
    Info.TractorEquipId = *EquipId;
    for (std::vector<IObjRW*>::const_iterator It = Targets.begin(); It != Targets.end(); ++It)
        Info.TargetIds.insert(Info.TargetIds.end(), 1, (*It)->get_id());

    // .text:06ce6f0a .. 06ce6f17 -- OwnerSystem->FUN_06d0e7a0(&SourceClient, &Info).
    unsigned int const SkipPlayerId = static_cast<unsigned int>(SourceClient);
    OwnerSystem->send_tractor_objects(SkipPlayerId, Info);

    // .text:06ce6f1c .. 06ce6f3f
    return Result;
}

// IObjAffect slot 17 (+0x44) -- LAB_06ce6f60 (.text:06ce6f60, RET 0x10).
int EqObj::slot_44(unsigned short const* EquipId, IObjRW* Target, unsigned short SubTarget,
                   int SourceClient)
{
    // .text:06ce6f66 .. 06ce6f86 -- no system / destroyed: -5.
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    // .text:06ce6f8c .. 06ce6f97 -- Result lives in ESI from here on.
    int Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    // .text:06ce6f9d -- BL, "the gun's target changed".
    bool bChanged = false;
    if (!SinglePlayer() || SourceClient == 0)
    {
        // .text:06ce6fb9 .. 06ce6fed -- FindByID on ceqobj().
        CEquip* const Found = ceqobj()->EquipManager.FindByID(*EquipId);
        if (Found == nullptr || Found->IsDestroyed())               // vt +0x08
        {
            Result = -2;                                            // .text:06ce7052
        }
        else
        {
            CEGun* const Gun = CEGun::cast(Found);
            if (Gun == nullptr)
            {
                Result = -4;                                        // .text:06ce7047
            }
            else
            {
                // .text:06ce700a .. 06ce703f -- Target->cobject() through the
                // (& 3) guard (null target -> null), CEGun::SetTarget.
                CSimple* Simple = nullptr;
                if (Target != nullptr)
                {
                    CObject const* const Raw = Target->cobject();
                    if (Raw != nullptr && (static_cast<unsigned>(Raw->ObjectClass) & 3u) == 3u)
                        Simple = const_cast<CSimple*>(static_cast<CSimple const*>(Raw));
                }
                bChanged = Gun->SetTarget(Simple, SubTarget);
            }
        }

        // .text:06ce7057 .. 06ce7065 -- the server's own request is only told
        // when the target really changed and nothing failed.
        if (SourceClient == 0 && (!bChanged || Result != 0))
            return Result;
    }

    // .text:06ce7067 .. 06ce708c -- get_id(), then
    // OwnerSystem->FUN_06d0e670(&id, *EquipId, Target, &SubTarget, &SourceClient).
    unsigned int const ObjId = get_id();
    OwnerSystem->notify_set_target(ObjId, *EquipId, Target, SubTarget, SourceClient);
    return Result;
}

// IObjAffect slot 19 (+0x4c) -- LAB_06ce70b0 (.text:06ce70b0, RET 0xc).
int EqObj::slot_4c(unsigned short const* EquipId, bool bActivate, int SourceClient)
{
    // .text:06ce70b6 .. 06ce70d2 -- no system / destroyed: -5.
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    // .text:06ce70d8 .. 06ce70e3
    int Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    // .text:06ce70e8 -- [ESP+0xf], "the equip was switched".
    bool bChanged = false;
    if (!SinglePlayer() || SourceClient == 0)
    {
        // .text:06ce7105 .. 06ce7145 -- FindByID on ceqobj(); missing or
        // destroyed (vt +0x08): -2.
        CEquip* const Found = ceqobj()->EquipManager.FindByID(*EquipId);
        if (Found == nullptr || Found->IsDestroyed())
        {
            Result = -2;                                            // .text:06ce71ae
        }
        else
        {
            bool bRefused = false;
            // .text:06ce7147 .. 06ce7188 -- switching an engine (type 0x20000)
            // off is refused (-5) while the ship strafes or has a thruster on;
            // the CShip is the (& 0x503) narrowing of the raw object, called
            // without a null test.
            if (!bActivate && Found->Type == 0x20000u)
            {
                CObject* const Raw = cobj();
                CShip* Ship = nullptr;
                if (Raw != nullptr && (static_cast<unsigned>(Raw->ObjectClass) & 0x503u) == 0x503u)
                    Ship = static_cast<CShip*>(Raw);
                if (Ship->get_strafe_dir() != 0 || Ship->any_thrusters_on())
                {
                    Result = -5;                                    // .text:06ce718a
                    bRefused = true;
                }
            }

            // .text:06ce7194 .. 06ce71a7 -- IsActive (vt +0x04) already as asked:
            // nothing; otherwise Activate (vt +0x30) and remember it.
            if (!bRefused && Found->IsActive() != bActivate)
            {
                Found->Activate(bActivate);
                bChanged = true;
            }
        }

        // .text:06ce71b6 .. 06ce71cd
        if (SourceClient == 0 && (!bChanged || Result != 0))
            return Result;
    }

    // .text:06ce71cf .. 06ce71ef -- get_id(), then
    // OwnerSystem->FUN_06d0e500(&id, *EquipId, bActivate, &SourceClient).
    unsigned int const ObjId = get_id();
    OwnerSystem->notify_activate_equip(ObjId, *EquipId, bActivate, SourceClient);
    return Result;
}

// IObjAffect slot 22 (+0x58) -- LAB_06ce7210 (.text:06ce7210, RET 0xc).
// SourceClient is not read.
int EqObj::slot_58(unsigned short EquipId, unsigned Count, int SourceClient)
{
    (void)SourceClient;

    // .text:06ce7214 .. 06ce7231 -- no system / destroyed: -5.
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    // .text:06ce723f -- EBP, the answer for an item that is neither kind.
    int Result = -5;

    // .text:06ce7237 .. 06ce7278 -- FindByID on ceqobj(), CECargo::cast.
    CECargo* const Cargo = CECargo::cast(ceqobj()->EquipManager.FindByID(EquipId));
    if (Cargo == nullptr)
    {
        // .text:06ce7310 .. 06ce7334
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp",
              0x18c, "EqObj::use_item() couldn't find target item on Client request.");
        return -2;
    }

    // .text:06ce727e .. 06ce7289 -- at most the stack's own count (unsigned).
    unsigned Used = Count;
    if (Cargo->Count < Used)
        Used = Cargo->Count;

    // .text:06ce728b .. 06ce72e1 -- get_class_type (vt +0x10) of the archetype
    // (+0x0c): repair kits and shield batteries; each answers how many it used,
    // and only a positive number is success.
    int const ClassType = static_cast<int>(Cargo->Arch->get_class_type());
    int Taken;
    if (ClassType == Archetype::AClassRepairKit)
    {
        Taken = use_repair_kits(EquipId, static_cast<Archetype::RepairKit const*>(Cargo->Arch),
                                static_cast<int>(Used));            // .text:06ce72cf
    }
    else if (ClassType == Archetype::AClassShieldBattery)
    {
        Taken = use_shield_batteries(EquipId,
                                     static_cast<Archetype::ShieldBattery const*>(Cargo->Arch),
                                     static_cast<int>(Used));       // .text:06ce72ac
    }
    else
    {
        return Result;                                              // .text:06ce7299
    }
    Result = Taken > 0 ? 0 : -5;
    if (Result != 0)
        return Result;

    // .text:06ce72e7 .. 06ce72ff -- CECargo::RemoveFromStack, then
    // OwnerSystem->FUN_06d0efe0(this, EquipId, Taken).
    Cargo->RemoveFromStack(static_cast<unsigned>(Taken));
    OwnerSystem->use_item(this, EquipId, Taken);
    return Result;
}

// FUN_06ce7350 (.text:06ce7350, RET 0xc).
int EqObj::use_repair_kits(unsigned short EquipId, Archetype::RepairKit const* Kit, int Count)
{
    (void)EquipId;

    // .text:06ce7370 .. 06ce7396 -- a DamageList built inline (Buynode, count 0,
    // bDestroyed 0, cause 0x12, inflictor 0 / 0) that nothing ever fills; it is
    // only destroyed on the way out (.text:06ce75d5 .. 06ce760d).
    DamageList Unused;
    Unused.Count = 0;
    Unused.bDestroyed = false;
    Unused.Cause = static_cast<DamageCause>(0x12);
    Unused.InflictorId = 0;
    Unused.InflictorOwner = 0;
    (void)Unused;

    int Taken = 0;                                                  // EBP

    // .text:06ce739a .. 06ce73f2 -- only a hull below full health (TEST AH,5 /
    // JP skips >= 1.0f and unordered) is mended: SUBOBJ_ID_ROOT, else NONE.
    unsigned short SubId = SUBOBJ_ID_NONE;
    if (ceqobj()->get_relative_health() < 1.0f)
        SubId = SUBOBJ_ID_ROOT;
    if (SubId == SUBOBJ_ID_NONE)
        return Taken;

    // .text:06ce7406 .. 06ce746a -- the list that is applied: cause 0x12, this
    // object as inflictor, its CSimple::OwnerPlayer (the (& 3) guard's null
    // dereferenced) as the owner.
    DamageList Damage;
    Damage.Count = 0;
    Damage.bDestroyed = false;
    Damage.Cause = static_cast<DamageCause>(0x12);
    Damage.InflictorId = get_id();
    Damage.InflictorOwner = csimple()->OwnerPlayer;

    // .text:06ce746e .. 06ce750f -- the hull's own points through CSimple, any
    // other sub-object through CEqObj vt +0xe4 / +0xec.
    float HitPts;
    float MaxHitPts;
    if (SubId == SUBOBJ_ID_ROOT)
    {
        HitPts = ceqobj()->get_hit_pts();
        MaxHitPts = ceqobj()->get_max_hit_pts();
    }
    else
    {
        ceqobj()->get_sub_obj_hit_pts(SubId, HitPts);
        ceqobj()->get_sub_obj_max_hit_pts(SubId, MaxHitPts);
    }

    // .text:06ce750f .. 06ce7541 -- kits needed = (max - hp) / Kit->HitPts on
    // the x87 stack, truncated by _ftol, rounded up when the remainder is above
    // 0.05f (TEST AH,0x41 / JNZ keeps <= and unordered).
    double const Needed = (static_cast<double>(MaxHitPts) - HitPts) / Kit->HitPts;
    Taken = static_cast<int>(Needed);
    if (Needed - static_cast<double>(Taken) > 0.05f)
        ++Taken;

    // .text:06ce7541 .. 06ce754b -- no more than asked (signed).
    if (Count < Taken)
        Taken = Count;

    // .text:06ce754f .. 06ce7597 -- mend: hp + Taken * Kit->HitPts, capped at the
    // max (TEST AH,5 / JP takes >= and unordered), into the list as a fate-0
    // entry, then apply_damage_list (vt +0x188).
    if (Taken > 0)
    {
        double const Mended = static_cast<double>(Taken) * Kit->HitPts + HitPts;
        float NewHitPts;
        if (Mended < static_cast<double>(MaxHitPts))
            NewHitPts = static_cast<float>(Mended);
        else
            NewHitPts = MaxHitPts;
        Damage.add_damage_entry(SubId, NewHitPts, DamageEntry::SubObjFate::None);
        apply_damage_list(Damage);
    }

    // .text:06ce759d .. 06ce7629 -- both lists destroyed; the count.
    return Taken;
}

// FUN_06ce7630 (.text:06ce7630, RET 0xc).
int EqObj::use_shield_batteries(unsigned short EquipId, Archetype::ShieldBattery const* Battery,
                                int Count)
{
    (void)EquipId;

    // .text:06ce764e .. 06ce76d5 -- CEquipTraverser(0x100) over the shields:
    // the one with the lowest relative health (vt +0x44) strictly below 1.0f
    // (TEST AH,5 / JP skips >= and unordered) whose max hit points (vt +0x38)
    // are above 0.0f (TEST AH,0x41 / JNZ skips <= and unordered).
    CEquipTraverser Walk(0x100);
    float Lowest = 1.0f;
    CEquip* Weakest = nullptr;
    for (;;)
    {
        CEquip* const Equip = ceqobj()->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;
        // .text:06ce76a8  FST float ptr [ESP + 0x10]   ; rounded copy kept
        // .text:06ce76ac  FCOMP float ptr [ESP + 0x14] ; compare the UNROUNDED ST0
        double const RelativeExact = Equip->GetRelativeHealth();
        float const Relative = (float)RelativeExact;
        if (!(RelativeExact < Lowest))
            continue;
        if (!(Equip->GetMaxHitPoints() > 0.0f))
            continue;
        Lowest = Relative;                                        // .text:06ce76cb
        Weakest = Equip;
    }

    // .text:06ce76d7 .. 06ce76d9 -- nothing to charge: 0.
    if (Weakest == nullptr)
        return 0;

    // .text:06ce76df .. 06ce7740 -- the list: cause 0x13, this object as the
    // inflictor, its CSimple::OwnerPlayer (guard null dereferenced) as the owner.
    DamageList Damage;
    Damage.Count = 0;
    Damage.bDestroyed = false;
    Damage.Cause = static_cast<DamageCause>(0x13);
    Damage.InflictorId = get_id();
    Damage.InflictorOwner = csimple()->OwnerPlayer;

    // .text:06ce7744 .. 06ce7752 -- GetHitPoints (vt +0x3c), GetMaxHitPoints (+0x38).
    float const HitPts = Weakest->GetHitPoints();
    float const MaxHitPts = Weakest->GetMaxHitPoints();

    // .text:06ce7756 .. 06ce778c -- the same count as the repair kits'.
    double const Needed = (static_cast<double>(MaxHitPts) - HitPts) / Battery->HitPts;
    int Taken = static_cast<int>(Needed);
    if (Needed - static_cast<double>(Taken) > 0.05f)
        ++Taken;

    // .text:06ce778c .. 06ce7796
    if (Count < Taken)
        Taken = Count;

    // .text:06ce779a .. 06ce77e5 -- charge the shield by its own id (+0x08).
    if (Taken > 0)
    {
        double const Charged = static_cast<double>(Taken) * Battery->HitPts + HitPts;
        float NewHitPts;
        if (Charged < static_cast<double>(MaxHitPts))
            NewHitPts = static_cast<float>(Charged);
        else
            NewHitPts = MaxHitPts;
        Damage.add_damage_entry(Weakest->Id, NewHitPts, DamageEntry::SubObjFate::None);
        apply_damage_list(Damage);
    }

    // .text:06ce77eb .. 06ce782d -- the list destroyed; the count.
    return Taken;
}

// FUN_06ce7850 (.text:06ce7850, RET 8). `MOV EAX,[ECX+0x10]` / the (& 0x103)
// test zeroes EAX on failure, then `MOV EDX,[EAX]` / `JMP [EDX+0xb4]` with both
// stack arguments passed through untouched -- no null test.
ObjActivateResult EqObj::activate(bool bOn, unsigned int DockIndex)
{
    return ceqobj()->activate(bOn, DockIndex);
}

// FUN_06ce7880 (.text:06ce7880, RET 8).
void EqObj::queue_dock_request(int Port, unsigned int ShipId)
{
    // .text:06ce7889 .. 06ce78bd -- operator new(0x10) linked before the head
    // (push_back), node +8 = Port, +0xc = ShipId, ++[+0x68].
    FDockRequest Request;
    Request.Port = Port;
    Request.ShipId = ShipId;
    DockQueue.push_back(Request);

    // .text:06ce78c0 .. 06ce78c4 -- [this+0x28]->FUN_06d0c6d0(this).
    OwnerSystem->add_to_list_d4(this);
}

// FUN_06ce78d0 (.text:06ce78d0, RET 8).
bool EqObj::dequeue_dock_request(int Port, unsigned int ShipId)
{
    // .text:06ce78d3 .. 06ce78f7 -- head->next onward: node+0xc == ShipId, then
    // `TEST EDX,EDX / JL` (Port < 0 matches any port) or node+8 == Port.
    for (std::list<FDockRequest>::iterator It = DockQueue.begin(); It != DockQueue.end(); ++It)
    {
        if (It->ShipId != ShipId)
            continue;
        if (Port >= 0 && It->Port != Port)
            continue;

        // .text:06ce7900 .. 06ce791c -- unlink, operator delete (FUN_06d5ffb0),
        // --[+0x68].
        DockQueue.erase(It);

        // .text:06ce791f .. 06ce792a -- [this+0x28]->FUN_06d0c730(this); AL = 1.
        OwnerSystem->remove_from_list_d4(this);
        return true;
    }

    // .text:06ce78f9 .. 06ce78fd -- no match: AL = 0, the system is not told.
    return false;
}

// FUN_06ce7930 (.text:06ce7930, RET 4; Dt is not read). The dock-bay tick:
// every queued {port, ship} whose ship has reached the bay's docking sphere is
// handed on to the system, the rest wait.
void EqObj::process_dock_queue(float Dt)
{
    (void)Dt;

    // .text:06ce793a .. 06ce7962 -- ceqobj() once, kept for the whole walk.
    CEqObj* const Ce = ceqobj();

    std::list<FDockRequest>::iterator It = DockQueue.begin();
    while (It != DockQueue.end())
    {
        // .text:06ce7980 .. 06ce7991 -- StarSystem FUN_06d0c8d0(ShipId, 0x7f0000).
        GameObject* const ShipObj = OwnerSystem->find_object(It->ShipId, 0x7f0000u);
        if (ShipObj == nullptr)
        {
            // .text:06ce7997 .. 06ce79c3 -- the ship is gone: the node is
            // erased, the size dropped and the system's +0xd4 entry for this
            // bay removed (FUN_06d0c730).
            It = DockQueue.erase(It);
            OwnerSystem->remove_from_list_d4(this);
            continue;
        }

        // .text:06ce79c8 .. 06ce79ef -- the archetype's docking sphere for this
        // port (Archetype::EqObj +0x88 array, 0x10-byte entries: Radius +0,
        // Type +0xc).
        int const Port = It->Port;
        Archetype::EqObj::DockingSphere const& Sphere = Ce->eqobjarch()->DockingSpheres[Port];

        // .text:06ce79f1 -- CEqObj::launch_pos(Pos, Orient, Port).
        Vector LaunchPos;
        Matrix LaunchOrient;
        Ce->launch_pos(LaunchPos, LaunchOrient, Port);

        // .text:06ce79f7 .. 06ce7a52 -- the ship's "hpmount" hardpoint in object
        // space (FindHardpoint_OS on the ship's engine index, IObjInspect slot 7),
        // copied into a Transform {Orient, Position}.
        long HpHandle = 0;
        HardpointInfo Hp;
        FindHardpoint_OS(ShipObj->get_index(), "hpmount", HpHandle, Hp);
        Transform HpXform;
        HpXform.Rotation = Hp.Orient;
        HpXform.Translation = Hp.Position;

        // .text:06ce7a5d .. 06ce7a77 -- IEngine slot 40 on the same index.
        float* const ShipXform =
            DALib::Engine->GetInstanceTransform(reinterpret_cast<void*>(ShipObj->get_index()));

        // .text:06ce7a77 .. 06ce7aab -- MulTransform(Mounted, ShipXform, HpXform)
        // through the lazily fetched math engine (slot 7). .text:06ce7a82 is
        // FUN_06ce10d0, a no-op constructor (MOV EAX,ECX / RET 4).
        Transform Mounted;
        if (GEqObjMathEngine == nullptr)
            GEqObjMathEngine = Server_Acquire3DMathEngine();
        GEqObjMathEngine->MulTransform(reinterpret_cast<FTransform&>(Mounted),
                                       *reinterpret_cast<FTransform const*>(ShipXform),
                                       reinterpret_cast<FTransform const&>(HpXform));
        // .text:06ce7abd -- FUN_06ce7c30, a 12-dword copy.
        Transform const Copy = Mounted;

        // .text:06ce7ac2 .. 06ce7af5 -- the mount point relative to the launch
        // position (FUN_06ce7c10 is Vector(x, y, z)).
        Vector const Delta = { Copy.Translation.x - LaunchPos.x,
                               Copy.Translation.y - LaunchPos.y,
                               Copy.Translation.z - LaunchPos.z };

        // .text:06ce7afa .. 06ce7b25 -- Radius^2 against (dy^2 + dz^2) + dx^2;
        // TEST AH,0x41 / JNZ keeps the ship queued on <= and unordered.
        // Both sides stay on the x87 stack (FCOMPP), so both are doubles here.
        double const DistSq = (static_cast<double>(Delta.y) * Delta.y + static_cast<double>(Delta.z) * Delta.z) +
                              static_cast<double>(Delta.x) * Delta.x;
        if (!(static_cast<double>(Sphere.Radius) * Sphere.Radius > DistSq))
        {
            ++It;                                                  // .text:06ce7bdf
            continue;
        }

        // .text:06ce7b2b .. 06ce7b43 -- this bay's own type bits (vt +0x154).
        unsigned int const Bits = get_type_bits();
        if (Bits == (Bits & 0x440u))
        {
            // .text:06ce7b45 .. 06ce7b4a -- StarSystem FUN_06d0c970.
            OwnerSystem->complete_dock(ShipObj, this);
        }
        else if ((Bits & 0x800u) != 0)
        {
            // .text:06ce7b51 .. 06ce7b5b -- TEST AH,0x8.
            OwnerSystem->complete_dock(ShipObj, this);
        }
        else
        {
            // .text:06ce7b62 .. 06ce7ba8 -- mooring points 3/4/5 are not a
            // landing; StarSystem FUN_06d0d370(ship, this,
            // &ceqobj()->DockingPointBaseId (+0x164), &bLand). The CEqObj is
            // re-narrowed from the raw Object here.
            bool bLand = true;
            if (Sphere.Type == 3 || Sphere.Type == 4 || Sphere.Type == 5)
                bLand = false;
            CEqObj* const Bay = ceqobj();
            OwnerSystem->queue_dock(ShipObj, this, &Bay->DockingPointBaseId, &bLand);
        }

        // .text:06ce7bad .. 06ce7bd8 -- the node erased, the size dropped, the
        // system's +0xd4 entry removed.
        It = DockQueue.erase(It);
        OwnerSystem->remove_from_list_d4(this);
    }
}

// +0x1f0 -- .text:06ce8400 (RET 0).
void EqObj::explode()
{
    // .text:06ce8407 .. 06ce8437 -- ceqobj()->Archetype->ExplosionArch (+0x24).
    ID_String const& ExplosionArch = ceqobj()->Archetype->ExplosionArch;
    if (ExplosionArch.IsEmpty())
        return;

    Archetype::Explosion const* const Blast = Archetype::GetExplosion(ExplosionArch);
    if (Blast == nullptr)
        return;

    // .text:06ce8451 .. 06ce8493 -- every test against DOUBLE 0.0001
    // (DAT_06d65190), TEST AH,0x41: the radius must exceed it, and so must one
    // of impulse / hull damage / energy damage.
    if (!(static_cast<double>(Blast->Radius) > 0.0001))
        return;
    if (!(static_cast<double>(Blast->Impulse) > 0.0001) &&
        !(static_cast<double>(Blast->HullDamage) > 0.0001) &&
        !(static_cast<double>(Blast->EnergyDamage) > 0.0001))
        return;

    // .text:06ce8495 .. 06ce84fe -- {id, id, cause 4, position, blast,
    // PhySys::GetCollisionGroup(raw Object)}.
    ExplosionDamageEvent Event;
    Event.ExplodingObjId = 0;
    Event.InflictorId = 0;
    Event.Cause = static_cast<DamageCause>(4);
    Event.Explosion = nullptr;
    Event.SkipCollisionGroup = nullptr;
    Event.ExplodingObjId = get_id();
    Event.InflictorId = get_id();
    Event.Cause = static_cast<DamageCause>(4);
    Event.Position = get_position();
    Event.Explosion = Blast;
    Event.SkipCollisionGroup = PhySys::GetCollisionGroup(Object);

    // .text:06ce8502 -- StarSystem FUN_06d0b260.
    OwnerSystem->explode(Event);
}

// +0x1f4 -- .text:06ce8510 (RET 4).
void EqObj::collect_droppable_equip(std::vector<EquipDesc>& Out)
{
    // .text:06ce851b / .text:06ce8525 -- ONE EquipDesc for the whole walk
    // (GetEquipDesc refills it), the default traverser.
    EquipDesc Desc;
    CEquipTraverser Walk;
    for (;;)
    {
        // .text:06ce8533 .. 06ce855c -- ceqobj() re-narrowed every iteration.
        CEquip* const Equip = ceqobj()->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;
        if (Equip->IsDestroyed())                                  // vt +0x08
            continue;
        if (Equip->Type == 0x800)                                  // cargo pods stay
            continue;
        if (!Equip->IsLootable())                                  // vt +0x20
            continue;
        if (!Equip->GetEquipDesc(Desc))                            // vt +0x28
            continue;
        // .text:06ce859b .. 06ce8749 -- the inlined VC6 vector insert at end().
        Out.push_back(Desc);
    }
}

// +0x1f8 -- .text:06ce8760 (RET 4).
void EqObj::drop_loot(std::vector<EquipDesc> const& Descs)
{
    // .text:06ce879a .. 06ce883e -- the record vector, reserved to Descs.size()
    // when that is non-zero.
    std::vector<FLootRecord> Loot;
    if (!Descs.empty())
        Loot.reserve(Descs.size());

    // .text:06ce8842 .. 06ce8873 -- build_loot (vt +0x244) per desc while fewer
    // than six records' worth were produced (CMP EBP,6 / JGE).
    int Total = 0;
    for (std::vector<EquipDesc>::const_iterator It = Descs.begin(); It != Descs.end(); ++It)
    {
        if (Total >= 6)
            break;
        Total += build_loot(*It, Loot);
    }

    // .text:06ce8875 .. 06ce88a9 -- a non-empty vector goes out as loot
    // (StarSystem FUN_06d08a40, mode 2).
    if (!Loot.empty())
        OwnerSystem->create_loot(get_id(), 2, Loot.data(), static_cast<int>(Loot.size()));
}

// +0x1dc -- .text:06ce88d0 (RET 0).
void EqObj::rebuild_server_equip()
{
    clear_server_equip();                                          // vt +0x1e0
    CEquipTraverser Walk;
    for (;;)
    {
        // .text:06ce88f0 .. 06ce8915 -- ceqobj() re-narrowed every iteration.
        CEquip* const Equip = ceqobj()->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;
        create_server_equip(Equip->Id);                            // FUN_06ce8990
    }
}

// +0x1e0 -- .text:06ce8930 (RET 0).
void EqObj::clear_server_equip()
{
    // .text:06ce8940 .. 06ce8956 -- every held ServerEquip deleted through its
    // scalar deleting destructor (vt +0x04, argument 1) and the slot cleared.
    for (std::list<ServerEquip*>::iterator It = ServerEquips.begin(); It != ServerEquips.end(); ++It)
    {
        if (*It != nullptr)
            delete *It;
        *It = nullptr;
    }
    // .text:06ce8958 .. 06ce8986 -- every node erased.
    ServerEquips.clear();
}

// FUN_06ce8990 (.text:06ce8990, RET 4).
void EqObj::create_server_equip(unsigned short EquipId)
{
    // .text:06ce89ad .. 06ce89db -- no null test on the equip.
    CEquip* const Equip = ceqobj()->EquipManager.FindByID(EquipId);

    // .text:06ce89dd .. 06ce8a02 and on -- one wrapper per type, each pushed at
    // the end of +0x70 (operator new, FUN_06ced2d0 + the subclass fields and
    // vtable inlined here; the subclass constructors below carry those stores).
    switch (Equip->Type)
    {
    case 0x80:       // .text:06ce8a08 .. 06ce8a3e  new(0x18), +0x0c = 0, vt .rdata:06d655ac
        ServerEquips.push_back(new SrvGun(Equip, this));
        break;
    case 0x40:       // .text:06ce8a52 .. 06ce8a84  new(0x18), +0x0c = 0, vt .rdata:06d65590
        ServerEquips.push_back(new SrvCounterMeasureDropper(Equip, this));
        break;
    case 0x20:       // .text:06ce8ac7 ..           new(0x18), +0x0c = 0, vt .rdata:06d65574
        ServerEquips.push_back(new SrvMineDropper(Equip, this));
        break;
    case 0x400:      // .text:06ce8b12 ..           new(0x0c), vt .rdata:06d65564
        ServerEquips.push_back(new SrvThruster(Equip, this));
        break;
    case 0x200000:   // .text:06ce8b59 .. 06ce8b9c  new(0x14), +0x0c = 0, +0x10 = NONE
        ServerEquips.push_back(new SrvRepairDroid(Equip, this));
        break;
    case 0x100000:   //                             new(0x0c), vt .rdata:06d65544
        ServerEquips.push_back(new SrvTractor(Equip, this));
        break;
    default:         // .text:06ce8c09 -- no wrapper
        break;
    }
}

// +0x1c0 -- .text:06ce8c50 (RET 0xc).
void EqObj::inflict_hull_damage(float Damage, bool bCanKill, DamageList& Out)
{
    if (bCanKill)
    {
        // .text:06ce8c5b .. 06ce8c6a -- TEST AH,1 / JNZ skips < and unordered.
        if (Damage >= 0.0f)
            damage_hull(Damage, Out);                              // vt +0x1b0
        return;
    }
    // .text:06ce8c84 .. 06ce8cb5 -- only while the hull stays above 0
    // (TEST AH,0x41 / JNZ skips <= and unordered).
    if (ceqobj()->get_hit_pts() - Damage > 0.0f)
        damage_hull(Damage, Out);
}

// +0x1c8 -- .text:06ce8cd0 (RET 0xc).
bool EqObj::destroy_group_by_name(char const* GroupName, DamageEntry::SubObjFate Fate, DamageList& Out)
{
    CacheString Name;
    Name.Value = StringAlloc(GroupName, false);
    CArchGroup* const Group = ceqobj()->ArchGroups.FindByName(Name);
    if (Group == nullptr)
        return false;
    destroy_group(Group, Fate, Out, true);                         // vt +0x220
    return true;
}

// +0x1c4 -- .text:06ce8d40 (RET 0x10).
bool EqObj::inflict_group_damage(char const* GroupName, float Damage, bool bCanKill, DamageList& Out)
{
    CacheString Name;
    Name.Value = StringAlloc(GroupName, false);
    CArchGroup* const Group = ceqobj()->ArchGroups.FindByName(Name);
    if (Group == nullptr)
        return false;

    if (bCanKill)
    {
        // .text:06ce8d94 .. 06ce8da2 -- TEST BL(1),AH / JNZ skips < and unordered.
        if (Damage >= 0.0f)
            damage_group(Group, Damage, Out);                      // vt +0x218
        return true;
    }

    // .text:06ce8dc1 .. 06ce8ded -- the REMAINDER (hit points - Damage) is what
    // is passed on, and only while it stays above 0 (TEST AH,0x41 / JNZ).
    float const Remainder = Group->GetHitPoints() - Damage;
    if (Remainder > 0.0f)
        damage_group(Group, Remainder, Out);
    return true;
}

// +0x1d0 -- .text:06ce8e10 (RET 0xc).
bool EqObj::destroy_equip_by_hardpoint(char const* Hardpoint, DamageEntry::SubObjFate Fate, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();                                   // before StringAlloc
    CacheString Name;
    Name.Value = StringAlloc(Hardpoint, false);
    CExternalEquip* const Equip = Ce->EquipManager.FindByHardpoint(Name);
    if (Equip == nullptr)
        return false;
    destroy_equip(Equip, Fate, Out);                               // vt +0x224
    return true;
}

// +0x1cc -- .text:06ce8e90 (RET 0x10).
bool EqObj::inflict_equip_damage(char const* Hardpoint, float Damage, bool bCanKill, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();
    CacheString Name;
    Name.Value = StringAlloc(Hardpoint, false);
    CExternalEquip* const Equip = Ce->EquipManager.FindByHardpoint(Name);
    if (Equip == nullptr)
        return false;

    if (bCanKill)
    {
        // .text:06ce8ee8 .. 06ce8ef7 -- no sign test on this path.
        damage_equip(Equip, Damage, Out);                          // vt +0x210
        return true;
    }

    // .text:06ce8f05 .. 06ce8f30 -- the remainder, only while above 0.
    float const Remainder = Equip->GetHitPoints() - Damage;        // vt +0x3c
    if (Remainder > 0.0f)
        damage_equip(Equip, Remainder, Out);
    return true;
}

// +0x1d4 -- .text:06ce8f50 (RET 8).
bool EqObj::dump_cargo(char const* Hardpoint, DamageList& Out)
{
    bool bDumped = false;                                          // [ESP+0x13]
    CEquipTraverser Walk(0x10000);
    for (;;)
    {
        // .text:06ce8f83 .. 06ce8fa8 -- ceqobj() re-narrowed every iteration.
        CEquip* const Equip = ceqobj()->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;

        // .text:06ce8fb8 -- a stack already held in a pod (CECargo+0x20) stays.
        if (static_cast<CECargo*>(Equip)->CargoPod != nullptr)
            continue;

        // .text:06ce8fbd .. 06ce8fd5 -- a fresh EquipDesc per item.
        EquipDesc Desc;
        if (!Equip->GetEquipDesc(Desc))
            continue;

        // .text:06ce8fd7 .. 06ce9067 -- dumping at a solar for a valid inflicting
        // player (Out+0x18) asks PlayerData FUN_06d4b200 first; a refusal skips
        // the item.
        unsigned int const Inflictor = Out.InflictorOwner;
        CSolar* const Solar = EqObj_Solar(this);
        if (Solar != nullptr && Players.is_valid(Inflictor))
        {
            if (!PlayerData_ShouldQueueVisit(Players[Inflictor], Solar->Id, Solar->GetVisitValue()))
                continue;
        }

        // .text:06ce906d .. 06ce9092
        bDumped = true;
        if (Hardpoint != nullptr)
        {
            CacheString Hp;
            Hp.Value = StringAlloc(Hardpoint, false);
            Desc.set_hardpoint(Hp);
        }

        // .text:06ce9098 .. 06ce90fc -- build_loot (vt +0x244) into a fresh
        // vector; a non-zero count goes out through StarSystem FUN_06d08a40.
        std::vector<FLootRecord> Loot;
        if (build_loot(Desc, Loot) != 0)
            OwnerSystem->create_loot(get_id(), 2, Loot.data(), static_cast<int>(Loot.size()));

        // .text:06ce9101 .. 06ce9111 -- the stack itself is destroyed.
        Out.add_damage_entry(Equip->Id, 0.0f, DamageEntry::SubObjFate::Destroyed);

        // .text:06ce9117 .. 06ce9192 -- the visit is recorded (FUN_06d4b270),
        // with the owner re-read from Out+0x18.
        CSolar* const SolarAgain = EqObj_Solar(this);
        unsigned int const InflictorAgain = Out.InflictorOwner;
        if (SolarAgain != nullptr && Players.is_valid(InflictorAgain))
            PlayerData_QueueVisit(Players[InflictorAgain], SolarAgain->Id, SolarAgain->GetVisitValue());
    }
    return bDumped;
}

// +0x1d8 -- .text:06ce91e0 (RET 4).
bool EqObj::drain_shields(DamageList& Out)
{
    CEquipTraverser Walk(0x100);
    for (;;)
    {
        CEquip* const Equip = ceqobj()->EquipManager.Traverse(Walk);   // re-narrowed each pass
        if (Equip == nullptr)
            break;
        damage_shield(static_cast<CEShield*>(Equip), -1.0f, Out);  // vt +0x214
    }
    return true;
}

// +0x17c -- .text:06ce9250 (RET 8).
void EqObj::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    // .text:06ce925f -- the base body (FUN_06ceeb00) first, directly.
    GameObject::damage_sub_obj(Hit, Damage);

    if (Hit.SubObjId == SUBOBJ_ID_ROOT)
        return;

    if (SubObjectID::IsArchGroupID(Hit.SubObjId))
    {
        CArchGroup* const Group = ceqobj()->ArchGroups.FindByID(Hit.SubObjId);
        if (Group != nullptr)
            damage_group(Group, Hit.Damage, Damage);               // vt +0x218
        return;
    }

    if (SubObjectID::IsEquipID(Hit.SubObjId))
    {
        CEquip* const Equip = ceqobj()->EquipManager.FindByID(Hit.SubObjId);
        if (Equip == nullptr)
            return;
        if (Equip->Type == 0x100)
            damage_shield(static_cast<CEShield*>(Equip), Hit.Damage, Damage);   // vt +0x214
        else
            damage_equip(Equip, Hit.Damage, Damage);               // vt +0x210
    }
}

// +0x180 -- .text:06ce9350 (RET 8).
void EqObj::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    // .text:06ce9357 .. 06ce9370 -- ceqobj() once, used for both lookups.
    CEqObj* const Ce = ceqobj();

    // .text:06ce9372 .. 06ce9395 -- the behavior manager hears {1, &Hit}
    // through its slot 0 when there is one (Common LAB_062d3f90, the port's
    // virtual IBehaviorManager::Slot00; the record is the caller's own, the
    // const_cast only fits FBehaviorManagerEvent::Data).
    IBehaviorManager* const Behavior = Ce->get_behavior_interface();
    if (Behavior != nullptr)
    {
        FBehaviorManagerEvent Event;
        Event.Type = 1;
        Event.Data = const_cast<FMunitionImpact*>(&Hit);
        Behavior->Slot00(&Event);                                   // .text:06ce9395
    }

    Archetype::Munition const* const Munition = Hit.Munition;

    if (Hit.SubObjId == SUBOBJ_ID_ROOT || Hit.SubObjId == SUBOBJ_ID_NONE)
    {
        // .text:06ce946b .. 06ce947e
        damage_hull(Munition->HullDamage, Damage);                 // vt +0x1b0
    }
    else if (SubObjectID::IsArchGroupID(Hit.SubObjId))
    {
        CArchGroup* const Group = Ce->ArchGroups.FindByID(Hit.SubObjId);
        if (Group == nullptr)
            return;
        damage_group(Group, Munition->HullDamage, Damage);         // vt +0x218
    }
    else if (SubObjectID::IsEquipID(Hit.SubObjId))
    {
        CEquip* const Equip = Ce->EquipManager.FindByID(Hit.SubObjId);
        if (Equip == nullptr)
            return;
        if (Equip->Type == 0x100)
        {
            // .text:06ce9450 .. 06ce9468 -- the shield takes the whole hit; no
            // energy pass.
            munition_hit_shield(static_cast<CEShield*>(Equip), Munition, Damage);   // vt +0x20c
            return;
        }
        damage_equip(Equip, Munition->HullDamage, Damage);         // vt +0x210
    }
    else
    {
        return;
    }

    // .text:06ce9484 .. 06ce9493 -- the energy damage (Munition+0x84).
    damage_power(Munition->EnergyDamage, Damage);                  // vt +0x21c
}

// +0x20c -- .text:06ce94b0 (RET 0xc).
void EqObj::munition_hit_shield(CEShield* Shield, Archetype::Munition const* Munition, DamageList& Out)
{
    // .text:06ce94b4 .. 06ce94d5 -- FLD Hull / FMUL factor / FADD Energy on the
    // FPU, ONE rounding at 06ce94d5 FSTP float; then 06ce94df FMUL float Total
    // on the unrounded CEShield::GetWeaponModifier (Munition+0x88) and 06ce94eb
    // FSTP float into damage_shield's argument.
    float const Total = (float)((double)Munition->HullDamage * ShieldEquipConsts::HULL_DAMAGE_FACTOR
                                + Munition->EnergyDamage);
    damage_shield(Shield, (float)(Shield->GetWeaponModifier(Munition->WeaponType) * Total), Out);   // vt +0x214
}

// +0x184 -- .text:06ce9500 (RET 8).
void EqObj::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    // The shield pass first; a functioning shield absorbs the whole blast.
    if (explosion_hit_shield(Blast, Damage))                       // vt +0x204
        return;
    explosion_hit_hull(Blast, Damage);                             // vt +0x1fc
    explosion_hit_subobjs(Blast, Damage);                          // vt +0x200
    explosion_hit_power(Blast, Damage);                            // vt +0x208
}

// The distance bands the four explosion passes share (.text:06ce9580 ..
// 06ce95ae and the same code in each pass): R^2 = (float)(Radius * Radius),
// Inner = (float)(R^2 * 1/9) (DAT_06d655d0), Middle = (float)(R^2 * 4/9)
// (DAT_06d655d4). The distance D (the squared centre distance less the target's
// squared radius) is floored at 0 (TEST AH,0x41 / JZ keeps >), then
// D < Inner -> 3, D < Middle -> 2, D < R^2 -> 1 (TEST AH,5 / JP each), else 0
// (the target is out of reach). Retail keeps D on the x87 stack (53-bit
// precision control) and never stores it, so the port carries it as a double;
// each damage amount is likewise formed on the x87 stack and rounded to float
// once, by the FSTP / the float push.
static int EqObj_ExplosionBand(double Distance, float RadiusSq, float Middle, float Inner)
{
    if (!(Distance > 0.0))
        Distance = 0.0;
    if (Distance < Inner)
        return 3;
    if (Distance < Middle)
        return 2;
    if (Distance < RadiusSq)
        return 1;
    return 0;
}

// +0x1fc -- .text:06ce9550 (RET 8).
void EqObj::explosion_hit_hull(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();

    float const RadiusSq = Blast.Explosion->Radius * Blast.Explosion->Radius;
    float const Middle = RadiusSq * (4.0f / 9.0f);
    float const Inner = RadiusSq * (1.0f / 9.0f);

    // .text:06ce95b2 -- CEqObj::get_explosion_dmg_bounding_sphere.
    float Radius = 0.0f;
    Vector Center;
    Ce->get_explosion_dmg_bounding_sphere(Radius, Center);

    // .text:06ce95b8 .. 06ce95e5 -- ((dz^2 + dx^2) + dy^2) - r^2.
    double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
    double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
    double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
    double const Distance = ((Dz * Dz + Dx * Dx) + Dy * Dy) - static_cast<double>(Radius) * Radius;

    int const Band = EqObj_ExplosionBand(Distance, RadiusSq, Middle, Inner);
    if (Band == 0)
        return;

    // .text:06ce9643 .. 06ce9673 -- (float)(Band * 1/3 * HullDamage), times the
    // archetype's explosion_resistance (Root+0x28).
    float const Amount = static_cast<float>(static_cast<double>(Band) * (1.0f / 3.0f) *
                                            Blast.Explosion->HullDamage);
    damage_hull(Ce->get_archetype()->ExplosionResistance * Amount, Out);   // vt +0x1b0
}

// +0x200 -- .text:06ce9690 (RET 8).
void EqObj::explosion_hit_subobjs(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();

    float const RadiusSq = Blast.Explosion->Radius * Blast.Explosion->Radius;
    float const Middle = RadiusSq * (4.0f / 9.0f);
    float const Inner = RadiusSq * (1.0f / 9.0f);

    // .text:06ce96c7 .. 06ce980b -- the attached equipment (const traversal,
    // mask 0x1ee0; the manager is fixed before the loop).
    CEquipTraverser Walk(0x1ee0);
    CEquipManager const& Equipment = Ce->EquipManager;
    for (CEquip const* Equip = Equipment.Traverse(Walk); Equip != nullptr; Equip = Equipment.Traverse(Walk))
    {
        CAttachedEquip const* const Attached = static_cast<CAttachedEquip const*>(Equip);
        float Radius = 0.0f;
        Vector Center;
        Attached->GetRadius(Radius);                               // vt +0x7c
        Attached->GetCenterOfMass(Center);                         // vt +0x70

        // ((dz^2 + dy^2) + dx^2) - r^2.
        double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
        double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
        double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
        double const Distance = ((Dz * Dz + Dy * Dy) + Dx * Dx) - static_cast<double>(Radius) * Radius;

        int const Band = EqObj_ExplosionBand(Distance, RadiusSq, Middle, Inner);
        if (Band == 0)
            continue;
        float const Amount = static_cast<float>(static_cast<double>(Band) * (1.0f / 3.0f) *
                                            Blast.Explosion->HullDamage);
        damage_equip(const_cast<CEquip*>(Equip),
                     Attached->AttachedEquipArch()->ExplosionResistance * Amount, Out);   // vt +0x210
    }

    // .text:06ce9811 .. 06ce9921 -- every arch group, the same formula.
    CArchGrpTraverser Groups;
    CArchGroupManager const& GroupManager = Ce->ArchGroups;
    for (CArchGroup const* Group = GroupManager.Traverse(Groups); Group != nullptr;
         Group = GroupManager.Traverse(Groups))
    {
        float Radius = 0.0f;
        Vector Center;
        Group->GetRadius(Radius);
        Group->GetCenterOfMass(Center);

        double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
        double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
        double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
        double const Distance = ((Dz * Dz + Dy * Dy) + Dx * Dx) - static_cast<double>(Radius) * Radius;

        int const Band = EqObj_ExplosionBand(Distance, RadiusSq, Middle, Inner);
        if (Band == 0)
            continue;
        float const Amount = static_cast<float>(static_cast<double>(Band) * (1.0f / 3.0f) *
                                            Blast.Explosion->HullDamage);
        damage_group(const_cast<CArchGroup*>(Group), Group->ExplosionResistance() * Amount, Out);   // vt +0x218
    }
}

// +0x208 -- .text:06ce9940 (RET 8).
void EqObj::explosion_hit_power(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();

    // .text:06ce9960 .. 06ce9971 -- TEST AH,5 / JNP: out only on a negative
    // MaxPower (+0x19c); 0 and unordered go on.
    if (Ce->MaxPower < 0.0f)
        return;
    // .text:06ce997e .. 06ce998c -- no energy damage, nothing to do.
    if (Blast.Explosion->EnergyDamage == 0.0f)
        return;

    float const RadiusSq = Blast.Explosion->Radius * Blast.Explosion->Radius;
    float const Middle = RadiusSq * (4.0f / 9.0f);
    float const Inner = RadiusSq * (1.0f / 9.0f);

    // .text:06ce99c4 -- CObject vt +0x64, get_physical_radius_r(Center).
    Vector Center;
    float const Radius = Ce->get_physical_radius_r(Center);

    // ((dz^2 + dx^2) + dy^2) - r^2.
    double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
    double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
    double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
    double const Distance = ((Dz * Dz + Dx * Dx) + Dy * Dy) - static_cast<double>(Radius) * Radius;

    int const Band = EqObj_ExplosionBand(Distance, RadiusSq, Middle, Inner);
    if (Band == 0)
        return;

    // .text:06ce9a50 .. 06ce9a7c -- ((Band * 1/3) * EnergyDamage) * resistance.
    float const Resistance = Ce->get_archetype()->ExplosionResistance;
    damage_power(static_cast<float>(static_cast<double>(Band) * (1.0f / 3.0f) *
                                    Blast.Explosion->EnergyDamage * Resistance), Out);   // vt +0x21c
}

// +0x204 -- .text:06ce9a90 (RET 8). True when any functioning shield was met.
bool EqObj::explosion_hit_shield(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    bool bShielded = false;                                        // [ESP+0x13]
    CEqObj* const Ce = ceqobj();

    float const RadiusSq = Blast.Explosion->Radius * Blast.Explosion->Radius;
    float const Middle = RadiusSq * (4.0f / 9.0f);
    float const Inner = RadiusSq * (1.0f / 9.0f);

    CEquipTraverser Walk(0x100);
    CEquipManager const& Equipment = Ce->EquipManager;
    for (CEquip const* Equip = Equipment.Traverse(Walk); Equip != nullptr; Equip = Equipment.Traverse(Walk))
    {
        // .text:06ce9b20 .. 06ce9b29 -- vt +0x0c IsFunctioning.
        if (!Equip->IsFunctioning())
            continue;
        bShielded = true;

        CAttachedEquip const* const Attached = static_cast<CAttachedEquip const*>(Equip);
        float Radius = 0.0f;
        Vector Center;
        Attached->GetRadius(Radius);                               // vt +0x7c
        Attached->GetCenterOfMass(Center);                         // vt +0x70

        // ((dz^2 + dx^2) + dy^2) - r^2.
        double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
        double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
        double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
        double const Distance = ((Dz * Dz + Dx * Dx) + Dy * Dy) - static_cast<double>(Radius) * Radius;

        int const Band = EqObj_ExplosionBand(Distance, RadiusSq, Middle, Inner);
        if (Band == 0)
            continue;

        // .text:06ce9bd7 .. 06ce9c16 -- (float)((Hull * HULL_DAMAGE_FACTOR +
        // Energy) * (Band * 1/3)), times the shield's explosion_resistance.
        float const Amount = static_cast<float>(
            (static_cast<double>(Blast.Explosion->HullDamage) * ShieldEquipConsts::HULL_DAMAGE_FACTOR +
             Blast.Explosion->EnergyDamage) *
            (static_cast<double>(Band) * (1.0f / 3.0f)));
        damage_shield(static_cast<CEShield*>(const_cast<CEquip*>(Equip)),
                      Attached->AttachedEquipArch()->ExplosionResistance * Amount, Out);   // vt +0x214
    }
    return bShielded;
}

// +0x228 -- .text:06ce9c50 (RET 0xc).
void EqObj::destroy_cargo(CEquip* Equip, DamageEntry::SubObjFate Fate, DamageList& Out)
{
    // .text:06ce9c56 .. 06ce9c71 -- the stack in the pod (CECargoPod+0x4c).
    CECargo* const Cargo = static_cast<CECargoPod*>(Equip)->Cargo;
    if (Cargo != nullptr)
    {
        int const FateValue = static_cast<int>(Fate);
        if (FateValue != 4 && FateValue != 5)
        {
            if (Fate == DamageEntry::SubObjFate::Separated)
            {
                // .text:06ce9c8d .. 06ce9c96 -- a separated pod drops its stack
                // as loot (fate 4) through destroy_equip.
                destroy_equip(Cargo, static_cast<DamageEntry::SubObjFate>(4), Out);   // vt +0x224
            }
            else
            {
                // .text:06ce9ca1 .. 06ce9cac
                Out.add_damage_entry(Cargo->Id, 0.0f, DamageEntry::SubObjFate::Destroyed);
            }
        }
        else
        {
            // .text:06ce9cb7 .. 06ce9d2c -- at a solar, a valid inflicting player
            // (PlayerDB::is_valid, a real call here) that already has this visit
            // gets no loot.
            bool bRefused = false;
            unsigned int const Inflictor = Out.InflictorOwner;
            CSolar* const Solar = EqObj_Solar(this);
            if (Solar != nullptr && Players.is_valid(Inflictor))
            {
                if (!PlayerData_ShouldQueueVisit(Players[Inflictor], Solar->Id, Solar->GetVisitValue()))
                    bRefused = true;
            }

            if (!bRefused)
            {
                // .text:06ce9d39 .. 06ce9dc6 -- fate 4 asks the controller (event
                // 0x13) and takes the desc it hands back; fate 5 reads it.
                EquipDesc Desc;
                if (static_cast<int>(Fate) == 4)
                {
                    FEquipLootEvent Event;
                    Event.SystemId = 0;
                    Event.InflictorId = 0;
                    Event.ObjId = 0;
                    Event.Cause = static_cast<DamageCause>(0);
                    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;   // FUN_06d06cd0
                    Event.InflictorId = Out.InflictorId;
                    Event.ObjId = get_id();
                    Event.Cause = Out.Cause;
                    Cargo->GetEquipDesc(Event.Desc);                         // vt +0x28
                    Server_ControllerDispatch(0, pub::Controller::EMessage::EquipDropLoot, &Event);
                    Desc = Event.Desc;                                       // REP MOVSD 8
                }
                else
                {
                    Cargo->GetEquipDesc(Desc);
                }

                // .text:06ce9dc9 .. 06ce9de6 -- nothing left (count <= 0, or
                // health not > 0): the pod is only separated.
                if (Desc.Count <= 0 || !(Desc.Health > 0.0f))
                {
                    Fate = DamageEntry::SubObjFate::Separated;
                }
                else
                {
                    // .text:06ce9dec .. 06ce9e3a -- the record's first eight
                    // dwords zeroed, +0x68 = 1, +0x69 = 0, then build_equip_loot
                    // (vt +0x248) on the pod.
                    FLootRecord Record;
                    Record.Id = 0;
                    Record.Arch = nullptr;
                    Record.HitPts = 0.0f;
                    Record.ContentsArch = nullptr;
                    Record.ContentsHitPts = 0.0f;
                    Record.Count = 0;
                    Record.Field_0x18 = 0;
                    Record.Field_0x1c = 0;
                    Record.Field_0x68 = 1;
                    Record.bMission = 0;
                    if (build_equip_loot(static_cast<CAttachedEquip const*>(Equip), Record))
                    {
                        // .text:06ce9e48 .. 06ce9e85 -- the stack's health and
                        // count; cause 0x16 drops it in mode 1, anything else 2.
                        Record.ContentsHitPts = Desc.Health * Record.ContentsArch->HitPts;
                        Record.Count = Desc.Count;
                        int const Mode = (static_cast<int>(Out.Cause) == 0x16) ? 1 : 2;
                        OwnerSystem->create_loot(get_id(), Mode, &Record, 1);

                        // .text:06ce9e8a .. 06ce9f05 -- the visit recorded
                        // (FUN_06d4b270), owner re-read from Out+0x18.
                        CSolar* const SolarAgain = EqObj_Solar(this);
                        unsigned int const InflictorAgain = Out.InflictorOwner;
                        if (SolarAgain != nullptr && Players.is_valid(InflictorAgain))
                            PlayerData_QueueVisit(Players[InflictorAgain], SolarAgain->Id,
                                                  SolarAgain->GetVisitValue());
                    }
                }
            }

            // .text:06ce9f22 .. 06ce9f32 -- the stack is always gone.
            Out.add_damage_entry(Cargo->Id, 0.0f, DamageEntry::SubObjFate::Destroyed);
        }
    }

    // .text:06ce9f3a .. 06ce9f54 -- the pod's own entry with the (possibly
    // rewritten) fate.
    Out.add_damage_entry(Equip->Id, 0.0f, Fate);
}

// +0x224 -- .text:06ce9f70 (RET 0xc).
void EqObj::destroy_equip(CEquip* Equip, DamageEntry::SubObjFate Fate, DamageList& Out)
{
    if (Equip->IsDestroyed())                                      // vt +0x08
        return;

    // .text:06ce9fa5 .. 06ce9fca -- a cargo pod goes through destroy_cargo.
    if (Equip->Type == 0x800)
    {
        destroy_cargo(Equip, Fate, Out);                           // vt +0x228
        return;
    }

    int const FateValue = static_cast<int>(Fate);
    if (FateValue == 4 || FateValue == 5)
    {
        // .text:06ce9fe8 .. 06cea000 -- not lootable, or disabled: separated.
        if (!Equip->IsLootable() || Equip->IsDisabled())           // vt +0x20 / +0x10
        {
            Fate = DamageEntry::SubObjFate::Separated;
        }
        else
        {
            // .text:06cea006 .. 06cea084 -- the solar visit test (a real
            // PlayerDB::is_valid call).
            bool bRefused = false;
            unsigned int const Inflictor = Out.InflictorOwner;
            CSolar* const Solar = EqObj_Solar(this);
            if (Solar != nullptr && Players.is_valid(Inflictor))
            {
                if (!PlayerData_ShouldQueueVisit(Players[Inflictor], Solar->Id, Solar->GetVisitValue()))
                    bRefused = true;
            }

            if (bRefused)
            {
                Fate = DamageEntry::SubObjFate::Separated;                 // .text:06cea086
            }
            else
            {
                // .text:06cea098 .. 06cea139 -- the desc (event 0x13 for fate 4).
                EquipDesc Desc;
                if (static_cast<int>(Fate) == 4)
                {
                    FEquipLootEvent Event;
                    Event.SystemId = 0;
                    Event.InflictorId = 0;
                    Event.ObjId = 0;
                    Event.Cause = static_cast<DamageCause>(0);
                    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;   // FUN_06d06cd0
                    Event.ObjId = get_id();
                    Event.InflictorId = Out.InflictorId;
                    Event.Cause = Out.Cause;
                    Equip->GetEquipDesc(Event.Desc);
                    Server_ControllerDispatch(0, pub::Controller::EMessage::EquipDropLoot, &Event);
                    Desc = Event.Desc;
                }
                else
                {
                    Equip->GetEquipDesc(Desc);
                }

                // .text:06cea13c .. 06cea155
                if (Desc.Count <= 0 || !(Desc.Health > 0.0f))
                {
                    Fate = DamageEntry::SubObjFate::Separated;
                }
                else
                {
                    CAttachedEquip const* const Attached = CAttachedEquip::cast(Equip);
                    if (Attached != nullptr)
                    {
                        // .text:06cea16d .. 06cea1c7 -- one record for the
                        // attached equip itself.
                        FLootRecord Record;
                        Record.Id = 0;
                        Record.Arch = nullptr;
                        Record.HitPts = 0.0f;
                        Record.ContentsArch = nullptr;
                        Record.ContentsHitPts = 0.0f;
                        Record.Count = 0;
                        Record.Field_0x18 = 0;
                        Record.Field_0x1c = 0;
                        Record.Field_0x68 = 1;
                        Record.bMission = 0;
                        if (build_equip_loot(Attached, Record))            // vt +0x248
                        {
                            // .text:06cea1d5 .. 06cea22d -- both hit-point
                            // fields take Health * ContentsArch hit points.
                            float const HitPts = Desc.Health * Record.ContentsArch->HitPts;
                            Record.ContentsHitPts = HitPts;
                            Record.HitPts = HitPts;
                            int const Mode = (static_cast<int>(Out.Cause) == 0x16) ? 1 : 2;
                            OwnerSystem->create_loot(get_id(), Mode, &Record, 1);
                        }
                    }
                    else
                    {
                        // .text:06cea237 .. 06cea2c3 -- internal equipment
                        // becomes build_loot's records (mode 2).
                        std::vector<FLootRecord> Loot;
                        if (build_loot(Desc, Loot) != 0)                   // vt +0x244
                            OwnerSystem->create_loot(get_id(), 2, Loot.data(),
                                                     static_cast<int>(Loot.size()));
                    }

                    // .text:06cea2c7 .. 06cea345 -- on both paths, whether or
                    // not a record was built, the visit is recorded.
                    CSolar* const SolarAgain = EqObj_Solar(this);
                    unsigned int const InflictorAgain = Out.InflictorOwner;
                    if (SolarAgain != nullptr && Players.is_valid(InflictorAgain))
                        PlayerData_QueueVisit(Players[InflictorAgain], SolarAgain->Id,
                                              SolarAgain->GetVisitValue());
                }
            }
        }
    }

    // .text:06cea351 .. 06cea368
    Out.add_damage_entry(Equip->Id, 0.0f, Fate);
}

// +0x1b0 -- .text:06cea3a0 (RET 8).
bool EqObj::damage_hull(float Amount, DamageList& Damage)
{
    // .text:06cea3a0 .. 06cea3b2 -- only a positive amount is scaled
    // (TEST AH,0x41 / JNZ skips <= and unordered).
    if (Amount > 0.0f)
    {
        // .text:06cea3b8 .. 06cea43c -- a player's current ship takes the
        // player's difficulty scale (PlayerData+0x3c4).
        PlayerData* const Player = EqObj_PlayerOfShip(this);
        if (Player != nullptr)
            Amount = Player->difficulty_scale * Amount;

        // .text:06cea440 .. 06cea47b -- armour (type 0x1000000) divides by its
        // hit_pts_scale (Armor+0x70).
        CEquip* const Armor = ceqobj()->EquipManager.FindFirst(0x1000000u);
        if (Armor != nullptr)
            Amount = Amount / static_cast<CEArmor*>(Armor)->ArmorArch()->HitPtsScale;
    }

    // .text:06cea48b -- the base body (FUN_06ceec90), directly.
    return GameObject::damage_hull(Amount, Damage);
}

// +0x210 -- .text:06cea4a0 (RET 0xc).
bool EqObj::damage_equip(CEquip* Equip, float Damage, DamageList& Out)
{
    // .text:06cea4a0 .. 06cea4b7 -- TEST AH,0x44 / JNP: zero damage does nothing.
    if (Damage == 0.0f)
        return false;
    if (Equip->IsDestroyed())                                      // vt +0x08
        return false;

    // .text:06cea4d1 .. 06cea4fb -- what the list already holds for the equip,
    // else its hit points (TEST AH,5 / JP keeps >= and unordered).
    float Current = Out.get_hit_pts_left(Equip->Id);
    if (Current < 0.0f)
        Current = Equip->GetHitPoints();                           // vt +0x3c
    float NewHitPts = Current;

    if (Damage > 0.0f)
    {
        // .text:06cea51c .. 06cea5e4 -- an attached equip of an OWNED object
        // divides by PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE and, when this is the
        // owner's current ship, takes the difficulty scale. Unowned objects and
        // internal equipment take the damage as it is.
        double Applied = Damage;
        if (CAttachedEquip::cast(Equip) != nullptr && csimple()->OwnerPlayer != 0)
        {
            Damage = Damage / PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE;
            Applied = Damage;
            PlayerData* const Player = EqObj_PlayerOfShip(this);
            if (Player != nullptr)
                Applied = static_cast<double>(Player->difficulty_scale) * Damage;
        }
        // .text:06cea5e4 .. 06cea5fe -- floored at 0 (TEST AH,0x41 / JNZ).
        double const Left = Current - Applied;  // adj stays on the x87 stack
        NewHitPts = (Left > 0.0) ? static_cast<float>(Left) : 0.0f;
    }
    else if (Damage < 0.0f)
    {
        // .text:06cea600 .. 06cea611 -- TEST AH,5 / JP: a negative amount
        // empties it.
        NewHitPts = 0.0f;
    }

    // .text:06cea619 .. 06cea69b -- a blocked list (vt +0x1b8) leaves the equip
    // no lower than (1 - invincible timer) of its max: F = 1 - vt +0x1a0; a
    // current relative health already below F keeps the current value; a new
    // one below F is raised to Max * F. The current ratio tests Max for zero,
    // the new one does not.
    if (is_damage_blocked(Out))
    {
        float const Floor = 1.0f - get_invincible_timer();
        float const Max = Equip->GetMaxHitPoints();                // vt +0x38
        double const RelCurrent = (Max != 0.0f) ? static_cast<double>(Current) / Max : 0.0;
        if (RelCurrent < Floor)
        {
            NewHitPts = Current;
        }
        else if (static_cast<double>(NewHitPts) / Max < Floor)
        {
            NewHitPts = Max * Floor;
        }
    }

    // .text:06cea69d .. 06cea6aa -- unchanged: nothing to record.
    if (NewHitPts == Current)
        return false;

    // .text:06cea6ac .. 06cea6cd -- still alive: the new hit points.
    if (NewHitPts > 0.0f)
    {
        Out.add_damage_entry(Equip->Id, NewHitPts, DamageEntry::SubObjFate::None);
        return false;
    }

    // .text:06cea6de .. 06cea72c -- destroyed: separated (2), except a launcher
    // of a weapons-offline object (CEqObj+0x15c) outside single player, which
    // keeps fate 0.
    DamageEntry::SubObjFate DeathFate = DamageEntry::SubObjFate::Separated;
    if (!SinglePlayer() && ceqobj()->bWeaponsOffline && CELauncher::cast(Equip) != nullptr)
        DeathFate = DamageEntry::SubObjFate::None;
    destroy_equip(Equip, DeathFate, Out);                          // vt +0x224
    return true;
}

// +0x214 -- .text:06cea740 (RET 0xc).
bool EqObj::damage_shield(CEShield* Shield, float Damage, DamageList& Out)
{
    if (Damage == 0.0f)
        return false;
    if (Shield->IsDestroyed())
        return false;

    float Current = Out.get_hit_pts_left(Shield->Id);
    if (Current < 0.0f)
        Current = Shield->GetHitPoints();                          // vt +0x3c
    float NewHitPts = Current;

    if (Damage > 0.0f)
    {
        // .text:06cea7c8 .. 06cea84b -- only the difficulty scale here.
        double Applied = Damage;
        PlayerData* const Player = EqObj_PlayerOfShip(this);
        if (Player != nullptr)
            Applied = static_cast<double>(Player->difficulty_scale) * Damage;
        double const Left = Current - Applied;  // adj stays on the x87 stack
        NewHitPts = (Left > 0.0) ? static_cast<float>(Left) : 0.0f;
    }
    else if (Damage < 0.0f)
    {
        NewHitPts = 0.0f;
    }

    // .text:06cea880 .. 06cea93e -- a blocked list: with no invincible timer
    // left (TEST AH,0x41 / JNZ, <= and unordered) the shield keeps its current
    // value; otherwise a new relative value below F = 1 - timer is either held
    // at the current value (when that one is below F too) or raised to Max * F
    // (GetMaxHitPoints re-read). Both ratios test Max for zero here.
    if (is_damage_blocked(Out))
    {
        float const Timer = get_invincible_timer();
        if (!(Timer > 0.0f))
        {
            NewHitPts = Current;
        }
        else
        {
            float const Max = Shield->GetMaxHitPoints();
            double const RelNew = (Max != 0.0f) ? static_cast<double>(NewHitPts) / Max : 0.0;  // kept on the x87 stack
            float const RelCurrent = (Max != 0.0f) ? Current / Max : 0.0f;  // stored (FSTP [ESP+0x10])
            float const Floor = 1.0f - Timer;
            if (RelNew < Floor)
            {
                if (RelCurrent < Floor)
                    NewHitPts = Current;
                else
                    NewHitPts = Shield->GetMaxHitPoints() * Floor;
            }
        }
    }

    if (NewHitPts == Current)
        return false;

    // .text:06cea959 .. 06cea969
    Out.add_damage_entry(Shield->Id, NewHitPts, DamageEntry::SubObjFate::None);

    // .text:06cea96f .. 06cea9df -- true when the change crosses the shield's
    // OfflineThreshold (+0x74): from above to strictly below, or from at/below
    // (or unordered) to strictly above. The old side is GetRelativeHealth
    // (vt +0x44), the new side New / Max (0 for a zero Max).
    float const Max = Shield->GetMaxHitPoints();
    float const RelNew = (Max != 0.0f) ? NewHitPts / Max : 0.0f;
    float const Threshold = Shield->OfflineThreshold;
    // .text:06cea9a6 CALL [EDX + 0x44] / 06cea9a9 FCOMP float [Threshold] --
    // the old ratio is compared unrounded, never stored.
    double const RelOld = Shield->GetRelativeHealth();
    if (RelOld > Threshold)
        return RelNew < Threshold;
    return RelNew > Threshold;
}

// +0x218 -- .text:06ceaa80 (RET 0xc).
bool EqObj::damage_group(CArchGroup* Group, float Damage, DamageList& Out)
{
    if (Damage == 0.0f)
        return false;
    // .text:06ceaaa2 .. 06ceaab1 -- killed (2) or destroyed (3) groups are done.
    if (Group->State == 2 || Group->State == 3)
        return false;
    // .text:06ceaabd .. 06ceaad0 -- TEST AH,0x41 / JP: only a group with hit
    // points above 0 (or unordered) takes damage.
    if (!(Group->GetHitPoints() > 0.0f) && !(Group->GetHitPoints() != Group->GetHitPoints()))
        return false;

    unsigned short const GroupId = Group->Arch->Id;
    float Current = Out.get_hit_pts_left(GroupId);
    if (Current < 0.0f)
        Current = Group->GetHitPoints();
    float NewHitPts = Current;

    if (Damage > 0.0f)
    {
        // .text:06ceab2c .. 06ceab74 -- armour's hit_pts_scale divides first:
        // 06ceab67 FLD [Arch+0x70] / 06ceab6a FDIVR Damage, and the quotient
        // stays on the FPU (no store before 06ceab9c / 06ceac29).
        double Scaled = Damage;
        CEquip* const Armor = ceqobj()->EquipManager.FindFirst(0x1000000u);
        if (Armor != nullptr)
            Scaled = (double)Damage / ((CEArmor*)Armor)->ArmorArch()->HitPtsScale;

        // .text:06ceab74 .. 06ceac25 -- an OWNED object's group then divides by
        // PLAYER_COLLISION_GROUP_HIT_PTS_SCALE (06ceab9c FDIV on the unrounded
        // quotient, 06ceaba2 the only FSTP float) and, on the owner's current
        // ship, takes the difficulty scale. An unowned object subtracts the
        // unrounded quotient directly (06ceab90 JZ -> 06ceac29 FSUBR).
        double Applied = Scaled;
        if (csimple()->OwnerPlayer != 0)
        {
            float const OwnedScaled = (float)(Scaled / PLAYER_COLLISION_GROUP_HIT_PTS_SCALE);
            Applied = OwnedScaled;
            PlayerData* const Player = EqObj_PlayerOfShip(this);
            if (Player != nullptr)
                Applied = (double)Player->difficulty_scale * OwnedScaled;
        }
        double const Left = Current - Applied;  // adj stays on the x87 stack
        NewHitPts = (Left > 0.0) ? static_cast<float>(Left) : 0.0f;
    }
    else if (Damage < 0.0f)
    {
        NewHitPts = 0.0f;
    }

    // .text:06ceac59 .. 06ceacf2 -- the invincibility floor, as damage_equip's
    // but with Max tested for zero in both ratios.
    if (is_damage_blocked(Out))
    {
        float const Floor = 1.0f - get_invincible_timer();
        // .text:06ceac82 CArchGroup::GetMaxHitPoints / 06ceac88 FST float: the
        // first zero test (06ceac8c) sees the unrounded ST0, the divisions and
        // the second zero test (06ceacbe) the stored float.
        double const MaxExact = Group->GetMaxHitPoints();
        float const Max = (float)MaxExact;
        double const RelCurrent = (MaxExact != 0.0) ? (double)Current / Max : 0.0;
        if (RelCurrent < Floor)
        {
            NewHitPts = Current;
        }
        else
        {
            double const RelNew = (Max != 0.0f) ? static_cast<double>(NewHitPts) / Max : 0.0;
            if (RelNew < Floor)
                NewHitPts = Max * Floor;
        }
    }

    bool bDestroyed = false;                                       // BL
    if (NewHitPts != Current)
    {
        // .text:06cead05 .. 06cead1f -- a burning death fuse keeps low-cause
        // damage from destroying the group outright.
        bool const bFuseHolds = is_death_fuse_burning() && static_cast<int>(Out.Cause) < 0x10;

        // .text:06cead2b `f6c441` TEST AH,0x41 / 06cead2e `7418` JZ -- only
        // New > 0 reaches the entry arm; <= 0 AND unordered fall through to
        // the destroy unless the fuse holds.
        if (!(NewHitPts > 0.0f) && !bFuseHolds)
        {
            // .text:06cead34 .. 06cead44 -- destroy_group(fate 2, with contents).
            destroy_group(Group, DamageEntry::SubObjFate::Separated, Out, true);   // vt +0x220
            bDestroyed = true;
        }
        else
        {
            // .text:06cead48 .. 06cead74 -- the entry, and "destroyed" when it
            // is at or below 0 (TEST AH,0x41 / JP: > and unordered are not).
            Out.add_damage_entry(GroupId, NewHitPts, DamageEntry::SubObjFate::None);
            bDestroyed = (NewHitPts <= 0.0f);
        }
    }

    // .text:06cead78 .. 06cead96 -- a root-health-proxy group passes the ORIGINAL
    // damage on to the hull; its result counts as well.
    if (Group->IsRootHealthProxy() && damage_hull(Damage, Out))   // vt +0x1b0
        return true;
    return bDestroyed;
}

// +0x21c -- .text:06ceafc0 (RET 8).
bool EqObj::damage_power(float Damage, DamageList& Out)
{
    if (Damage == 0.0f)
        return false;

    // .text:06ceafdd .. 06ceb023 -- TEST AH,0x41 / JNP, twice in retail: no
    // power plant (MaxPower <= 0, CEqObj+0x19c) takes no energy damage.
    CEqObj* const Ce = ceqobj();
    if (Ce->MaxPower <= 0.0f)
        return false;

    float Current = Out.get_hit_pts_left(SUBOBJ_ID_POWER);
    if (Current < 0.0f)
        Current = Ce->Power;                                       // +0x198
    float NewHitPts = Current;

    if (Damage > 0.0f)
    {
        double Applied = Damage;
        PlayerData* const Player = EqObj_PlayerOfShip(this);
        if (Player != nullptr)
            Applied = static_cast<double>(Player->difficulty_scale) * Damage;
        double const Left = Current - Applied;  // adj stays on the x87 stack
        NewHitPts = (Left > 0.0) ? static_cast<float>(Left) : 0.0f;
    }
    else if (Damage < 0.0f)
    {
        NewHitPts = 0.0f;
    }

    // .text:06ceb12f .. 06ceb1a2 -- the invincibility floor over MaxPower,
    // through its reciprocal (1 / Max, no zero test).
    if (is_damage_blocked(Out))
    {
        float const Max = Ce->MaxPower;
        float const Floor = 1.0f - get_invincible_timer();
        double const Inverse = 1.0 / Max;  // never stored
        if (Current * Inverse < Floor)
            NewHitPts = Current;
        else if (NewHitPts * Inverse < Floor)
            NewHitPts = Floor * Max;
    }

    if (NewHitPts == Current)
        return false;

    Out.add_damage_entry(SUBOBJ_ID_POWER, NewHitPts, DamageEntry::SubObjFate::None);
    // .text:06ceb1d0 .. 06ceb1df -- true when the power is at or below 0
    // (TEST AH,0x41 / JP: > and unordered answer false).
    return NewHitPts <= 0.0f;
}

// +0x220 -- .text:06cea9f0 (RET 0x10).
void EqObj::destroy_group(CArchGroup* Group, DamageEntry::SubObjFate Fate, DamageList& Out,
                          bool bWithChildren)
{
    if (Group->State == 2 || Group->State == 3)
        return;

    // .text:06ceaa08 .. 06ceaa22 -- TEST AH,0x41 / JP: a group with hit points
    // left (or unordered) is destroyed regardless; an empty one only when the
    // fate changes.
    float const HitPts = Group->GetHitPoints();
    if (!(HitPts > 0.0f) && !(HitPts != HitPts) && Group->Fate == static_cast<int>(Fate))
        return;

    // .text:06ceaa24 .. 06ceaa3b -- a group that takes its parent with it only
    // marks the list destroyed.
    if (Group->DestroysParent())
    {
        Out.bDestroyed = true;
        return;
    }

    damage_linked_equipment(Group, Out);                           // vt +0x22c
    if (bWithChildren)
        destroy_group_contents(Group, Out);                        // vt +0x230

    Out.add_damage_entry(Group->Arch->Id, 0.0f, Fate);
}

// +0x22c -- .text:06ceadc0 (RET 8).
void EqObj::damage_linked_equipment(CArchGroup* Group, DamageList& Out)
{
    unsigned int const LinkedType = Group->LinkedEquipmentType();
    if (LinkedType == 0)
        return;

    if (LinkedType == 0x100)
    {
        // .text:06ceade2 .. 06ceae0d
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp", 0x91b,
              "Implement me!  Linked shield damage!");
        return;
    }

    // .text:06ceae10 .. 06ceae8f -- every equip of that type takes
    // LinkedEquipmentDamage * its max hit points (const traversal; the manager
    // is fixed before the loop).
    CEquipTraverser Walk(static_cast<int>(LinkedType));
    CEquipManager const& Equipment = ceqobj()->EquipManager;
    for (CEquip const* Equip = Equipment.Traverse(Walk); Equip != nullptr; Equip = Equipment.Traverse(Walk))
    {
        float const Max = Equip->GetMaxHitPoints();                // vt +0x38
        damage_equip(const_cast<CEquip*>(Equip), Group->LinkedEquipmentDamage() * Max, Out);   // vt +0x210
    }
}

// +0x230 -- .text:06ceaea0 (RET 8).
void EqObj::destroy_group_contents(CArchGroup* Group, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();

    // .text:06ceaec7 .. 06ceaf3b -- external equipment (mask 0x1fe3) mounted
    // under the group's instance, except types 1 and 2, is destroyed (fate 1).
    CEquipTraverser Walk(0x1fe3);
    CEquipManager const& Equipment = Ce->EquipManager;
    for (CEquip const* Equip = Equipment.Traverse(Walk); Equip != nullptr; Equip = Equipment.Traverse(Walk))
    {
        // vt +0x64: CExternalEquip::GetParentConnector(false).
        long const Parent = static_cast<CExternalEquip const*>(Equip)->GetParentConnector(false);
        if (!Group->IsInstInGroup(Parent))
            continue;
        if (Equip->Type == 1 || Equip->Type == 2)
            continue;
        destroy_equip(const_cast<CEquip*>(Equip), DamageEntry::SubObjFate::Destroyed, Out);   // vt +0x224
    }

    // .text:06ceaf41 .. 06ceafae -- every OTHER group whose root instance lies in
    // this one is destroyed too (fate 1, without its own contents).
    CArchGrpTraverser Groups;
    CArchGroupManager const& GroupManager = Ce->ArchGroups;
    for (CArchGroup const* Child = GroupManager.Traverse(Groups); Child != nullptr;
         Child = GroupManager.Traverse(Groups))
    {
        if (Group->Arch->Id == Child->Arch->Id)
            continue;
        if (!Group->IsInstInGroup(Child->GetRootIndex()))
            continue;
        destroy_group(const_cast<CArchGroup*>(Child), DamageEntry::SubObjFate::Destroyed, Out, false);   // vt +0x220
    }
}

// +0x244 -- .text:06cebe80 (RET 8). The loot records one EquipDesc becomes;
// returns how many were appended.
int EqObj::build_loot(EquipDesc const& Desc, std::vector<FLootRecord>& Out)
{
    int Built = 0;                                                 // [ESP+0x18]

    Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc.ArchId);
    if (Arch == nullptr)
        return 0;

    // .text:06cebeb0 .. 06cebf04 -- the record: first eight dwords zeroed,
    // +0x68 = 1, the contents are the equip itself at Health * its hit points,
    // the loot object is get_loot_appearance (Equipment slot 7) at ITS hit
    // points, bMission from the desc.
    FLootRecord Record;
    Record.Id = 0;
    Record.Arch = nullptr;
    Record.HitPts = 0.0f;
    Record.Count = 0;
    Record.Field_0x18 = 0;
    Record.Field_0x1c = 0;
    Record.Field_0x68 = 1;
    Record.bMission = 0;
    Record.ContentsArch = Arch;
    Record.ContentsHitPts = Desc.Health * Arch->HitPts;
    Archetype::Equipment* const Appearance = Arch->get_loot_appearance();
    Record.Arch = Appearance;
    Record.HitPts = Appearance->HitPts;
    Record.bMission = Desc.bMission ? 1 : 0;

    // .text:06cebf0b .. 06cebf3b -- orientation (IObjInspect slot 3, the
    // transform's rotation) and position (slot 0) of this object.
    Record.Orientation = get_transform().Rotation;
    Record.Position = get_position();

    // .text:06cebf3f .. 06cebfbc -- mounted equipment starts at its own centre:
    // a cargo stack at its pod's (CECargo+0x20), anything external at its own
    // (vt +0x70). Otherwise (not found, or neither) the desc's hardpoint on this
    // object (HardpointPosition on the engine index, slot 7).
    if (!Desc.is_internal())
    {
        bool bPlaced = false;
        CEquip* const Equip = ceqobj()->EquipManager.FindByID(Desc.Id);
        if (Equip != nullptr)
        {
            CExternalEquip const* Mounted = nullptr;
            if (Equip->Type == 0x10000)
                Mounted = static_cast<CExternalEquip const*>(static_cast<CECargo*>(Equip)->CargoPod);
            else
                Mounted = CExternalEquip::cast(Equip);
            if (Mounted != nullptr)
            {
                Mounted->GetCenterOfMass(Record.Position);
                bPlaced = true;
            }
        }
        if (!bPlaced)
            HardpointPosition(get_index(), Desc.Hardpoint, &Record.Position, nullptr);
    }

    // .text:06cebfbf .. 06cebfd7 -- the id maker, the count, and the stack size
    // (Equipment+0x64 units_per_container).
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    int Remaining = Desc.Count;
    int const PerContainer = Arch->UnitsPerContainer;
    if (Remaining <= 0)
        return Built;

    do
    {
        // .text:06cebff0 .. 06cec005 -- at most six records unless it is a
        // mission item (CMP 5 / JG returns the count so far).
        if (!Desc.bMission && Built > 5)
            return Built;

        // .text:06cec00b .. 06cec029 -- a fresh object id (the inlined
        // AllocateObjectID: counter & 0x3fffffff, then ++counter); the count is
        // min(remaining, per container), compared UNSIGNED (JA).
        Record.Id = Maker->AllocateObjectID();
        Record.Count = (static_cast<unsigned>(Remaining) > static_cast<unsigned>(PerContainer))
                           ? PerContainer
                           : Remaining;

        // .text:06cec02d .. 06cec04d -- this object's velocity (slot 1).
        Record.Velocity = get_velocity();

        // .text:06cec051 .. 06cec105 -- a random push: Scale = rand() / 32768
        // (DAT_06d656b8, kept as a double), a random unit vector times 2 * Scale
        // (each component rounded to float), times 15 (DAT_06d651a8), added.
        double const Scale = static_cast<double>(rand()) * 3.0517578125e-05;
        Vector const Direction = random_on_unit_sphere();
        Vector const Push = { static_cast<float>((Scale + Scale) * Direction.x),
                              static_cast<float>((Scale + Scale) * Direction.y),
                              static_cast<float>((Scale + Scale) * Direction.z) };
        Vector const Kick = { Push.x * 15.0f, Push.y * 15.0f, Push.z * 15.0f };
        Record.Velocity.x = Kick.x + Record.Velocity.x;
        Record.Velocity.y = Record.Velocity.y + Kick.y;
        Record.Velocity.z = Record.Velocity.z + Kick.z;

        // .text:06cec109 .. 06cec211 -- a random spin about the orientation's
        // first and third columns: A, B = rand() / 32768 * 0.8 - 0.4 (doubles
        // DAT_06d656b0 / DAT_06d656a8, each rounded to float);
        // AngularVelocity = col0 * A + col2 * B.
        float const SpinA = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05 *
                                               0.800000011920929 - 0.4000000059604645);
        float const Col0X = Record.Orientation.E[0][0] * SpinA;
        float const Col0Y = Record.Orientation.E[1][0] * SpinA;
        float const Col0Z = Record.Orientation.E[2][0] * SpinA;
        float const SpinB = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05 *
                                               0.800000011920929 - 0.4000000059604645);
        // .text:06cec1b3 .. 06cec211 -- the x and y products stay on the x87
        // stack before the add; only z's product is stored (FSTP [ESP+0xe0])
        // first.
        float const Col2Z = Record.Orientation.E[2][2] * SpinB;
        Record.AngularVelocity.x =
            static_cast<float>(static_cast<double>(Record.Orientation.E[0][2]) * SpinB + Col0X);
        Record.AngularVelocity.y =
            static_cast<float>(static_cast<double>(Record.Orientation.E[1][2]) * SpinB + Col0Y);
        Record.AngularVelocity.z = Col2Z + Col0Z;

        // .text:06cec223 -- FUN_06cece90, vector insert(end, 1, Record).
        Out.push_back(Record);

        // .text:06cec228 .. 06cec239
        ++Built;
        Remaining -= Record.Count;
    } while (Remaining > 0);

    return Built;
}

// +0x248 -- .text:06cec260 (RET 8). One attached equip -> one loot record.
bool EqObj::build_equip_loot(CAttachedEquip const* Equip, FLootRecord& Out)
{
    // .text:06cec267 .. 06cec294 -- a fresh object id, the equip's archetype
    // and hit points (vt +0x3c).
    Out.Id = CFLIDMaker::GetInstance()->AllocateObjectID();
    Out.Arch = Equip->Arch;
    float const HitPts = Equip->GetHitPoints();
    Out.HitPts = HitPts;

    if (Equip->Type == 0x800)
    {
        // .text:06cec2a0 .. 06cec2c5 -- a cargo pod carries its stack.
        CECargo* const Cargo = CECargoPod::cast(Equip)->Cargo;
        Out.ContentsArch = Cargo->Arch;
        Out.ContentsHitPts = Cargo->GetHitPoints();
        Out.Count = static_cast<int>(Cargo->Count);
    }
    else
    {
        // .text:06cec2c7 .. 06cec2d0
        Out.ContentsHitPts = HitPts;
        Out.ContentsArch = Out.Arch;
        Out.Count = 1;
    }

    // .text:06cec2d7 .. 06cec2df -- GetCenterOfMass (vt +0x70) into the
    // position; retail then overwrites it with the engine's instance position.
    Equip->GetCenterOfMass(Out.Position);

    // .text:06cec2e2 .. 06cec339 -- the root instance (vt +0x84), its position
    // (IEngine slot 36) and orientation (slot 38).
    long const Instance = Equip->GetRootIndex();
    float const* const Position =
        DALib::Engine->GetInstancePosition(reinterpret_cast<void*>(Instance));
    Out.Position.x = Position[0];
    Out.Position.y = Position[1];
    Out.Position.z = Position[2];
    float const* const Orientation =
        DALib::Engine->GetInstanceOrientation(reinterpret_cast<void*>(Instance));
    memcpy(&Out.Orientation, Orientation, sizeof(Out.Orientation));

    // .text:06cec330 .. 06cec346 -- ComputeExplosiveImpulses(instance, A, B, C);
    // only the second out vector is used.
    Vector Unused1;
    Vector Impulse;
    Vector Unused2;
    ComputeExplosiveImpulses(Instance, Unused1, Impulse, Unused2);

    // .text:06cec34c .. 06cec389 -- scaled by child_impulse / mass (Attached
    // +0x78 / Root +0x20).
    Archetype::AttachedEquipment const* const Arch = Equip->AttachedEquipArch();
    // The quotient stays on the x87 stack; each component is rounded once.
    double const ImpulseScale = static_cast<double>(Arch->ChildImpulse) / Equip->AttachedEquipArch()->Mass;
    Impulse.x = static_cast<float>(Impulse.x * ImpulseScale);
    Impulse.y = static_cast<float>(Impulse.y * ImpulseScale);
    Impulse.z = static_cast<float>(Impulse.z * ImpulseScale);

    // .text:06cec38b .. 06cec3ca -- times rand() / 32768 * 0.5 + 0.75 (doubles
    // DAT_06d656d8 / DAT_06d656d0).
    double const Jitter = static_cast<double>(rand()) * 3.0517578125e-05 * 0.5 + 0.75;
    Impulse.x = static_cast<float>(Jitter * Impulse.x);
    Impulse.y = static_cast<float>(Jitter * Impulse.y);
    Impulse.z = static_cast<float>(Jitter * Impulse.z);

    // .text:06cec3cc .. 06cec3eb -- the equip's own velocity (vt +0x6c) plus
    // the impulse.
    Equip->GetVelocity(Out.Velocity);
    Out.Velocity.x = Impulse.x + Out.Velocity.x;
    Out.Velocity.y = Impulse.y + Out.Velocity.y;
    Out.Velocity.z = Impulse.z + Out.Velocity.z;

    // .text:06cec3ee .. 06cec448 -- three rand() / 32768 values; the FIRST goes
    // to y, the second to x, the third to z.
    float const SpinY = static_cast<float>(rand()) * 3.0517578125e-05f;
    float const SpinX = static_cast<float>(rand()) * 3.0517578125e-05f;
    float const SpinZ = static_cast<float>(rand()) * 3.0517578125e-05f;
    Out.AngularVelocity.x = SpinX;
    Out.AngularVelocity.y = SpinY;
    Out.AngularVelocity.z = SpinZ;

    // .text:06cec44b .. 06cec4ae -- normalised and scaled to rand() / 32768 * 3
    // + 2 (doubles DAT_06d656c8 / DAT_06d656c0): factor = (float)(s / |v|).
    double const Spin = static_cast<double>(rand()) * 3.0517578125e-05 * 3.0 + 2.0;
    double const Length = sqrt(static_cast<double>(SpinX) * SpinX +
                               static_cast<double>(SpinY) * SpinY +
                               static_cast<double>(SpinZ) * SpinZ);
    float const Factor = static_cast<float>(Spin / Length);
    Out.AngularVelocity.x = Factor * Out.AngularVelocity.x;
    Out.AngularVelocity.y = Factor * Out.AngularVelocity.y;
    Out.AngularVelocity.z = Factor * Out.AngularVelocity.z;

    // .text:06cec4b1 .. 06cec4b7 -- vt +0x14 IsTemporary into +0x69.
    Out.bMission = Equip->IsTemporary() ? 1 : 0;
    return true;
}

// +0x24c -- .text:06cec910 (RET 4).
void EqObj::decay_cargo(float Elapsed)
{
    // .text:06cec934 .. 06cec967 -- a cause-0x14 damage list.
    DamageList Decay;
    Decay.Cause = static_cast<DamageCause>(0x14);

    // .text:06cec95e .. 06cec9a9 -- not a solar (0x303), and the equipment
    // manager has decaying cargo (CEquipManager+0x1c > 0, signed). The CEqObj
    // null is dereferenced as in retail.
    CEqObj* const Ce = ceqobj();
    bool bDecay = true;
    if (Ce != nullptr && (static_cast<unsigned>(Ce->ObjectClass) & 0x303u) == 0x303u)
        bDecay = false;
    if (!(Ce->EquipManager.DecayingCargoCount > 0))
        bDecay = false;

    // .text:06cec9ab .. 06cec9f2 -- a valid owning player with an open trade
    // (PlayerData+0x3cc) freezes the decay.
    unsigned int const Owner = csimple()->OwnerPlayer;
    if (Players.is_valid(Owner) && Players[Owner].trade_offer != nullptr)
        return;
    if (!bDecay)
        return;

    // .text:06ceca00 .. 06cecad1 -- every cargo stack (non-const traversal).
    CEquipTraverser Walk(0x10000);
    for (CEquip* Equip = Ce->EquipManager.Traverse(Walk); Equip != nullptr; Equip = Ce->EquipManager.Traverse(Walk))
    {
        CECargo* const Cargo = static_cast<CECargo*>(Equip);
        if (Cargo->Count == 0)                                     // JBE: unsigned
            continue;

        // TEST AH,0x41 / JNZ: only a positive decay amount.
        float const Amount = Cargo->GetDecayDamagePerSecond() * Elapsed;
        if (!(Amount > 0.0f))
            continue;

        float NewHitPts = 0.0f;
        if (Cargo->GetHitPoints() - Amount > 0.0f)
        {
            // .text:06ceca74 .. 06ceca8e -- hit points re-read; an unequal-to-0
            // result (TEST AH,0x44 / JP) is a plain entry.
            NewHitPts = Cargo->GetHitPoints() - Amount;
            if (NewHitPts != 0.0f)
            {
                Decay.add_damage_entry(Cargo->Id, NewHitPts, DamageEntry::SubObjFate::None);
                continue;
            }
        }

        // .text:06ceca90 .. 06cecabe -- a spent single item is destroyed; a
        // spent stack only records 0.
        if (Cargo->Count == 1)
            Decay.add_damage_entry(Cargo->Id, NewHitPts, DamageEntry::SubObjFate::Destroyed);
        else
            Decay.add_damage_entry(Cargo->Id, NewHitPts, DamageEntry::SubObjFate::None);
    }

    // .text:06cecad7 .. 06cecaea -- vt +0x188.
    if (Decay.Count != 0)
        apply_damage_list(Decay);
}

// ---------------------------------------------------------------------------
// ServerEquip family -- .text:06ced2d0 .. .text:06cee2e5
// ---------------------------------------------------------------------------

// .data:DAT_06d8d6f8 -- SrvGun::fire_forward's own lazily fetched math engine
// (.text:06cedf40 `MOV EAX,[DAT_06d8d6f8]` / JNZ / CALL FUN_06d5ff70 / store).
static I3DMathEngine* GSrvGunMathEngine = nullptr;

// .text:06ced2d0 (RET 8): +4 = the equip, +8 = the owner, ServerEquip's vtable
// (.rdata:06d65744), then CEquip::ConnectServerEquip(this) on the equip with no
// null test.
ServerEquip::ServerEquip(CEquip* InEquip, EqObj* InOwner)
    : Equip(InEquip)
    , Owner(InOwner)
{
    Equip->ConnectServerEquip(this);
}

// .text:06ced330 (and the scalar deleting destructor .text:06ced300 / the one
// every subclass table carries, .text:06ce8c20): the vtable reset, then
// CEquip::NotifyDisconnecting(this) while an equip is held.
ServerEquip::~ServerEquip()
{
    if (Equip != nullptr)
        Equip->NotifyDisconnecting(this);
}

// slot 0 -- .text:06ced360 (RET 8). Only event 0xa (the equip is going away):
// NotifyDisconnecting(this) on the held equip (no null test) and forget it.
void ServerEquip::notify(Event Code, void* Param)
{
    (void)Param;
    if (Code != EquipDestroying)
        return;
    Equip->NotifyDisconnecting(this);
    Equip = nullptr;
}

// slot 2 -- .text:06ced350 (RET 4): SETNZ on the held equip; Dt is not read.
bool ServerEquip::update(float Dt)
{
    (void)Dt;
    return Equip != nullptr;
}

// operator new(0x0c) + FUN_06ced2d0 + vtable .rdata:06d65564
// (.text:06ce8b12 .. 06ce8b3c).
SrvThruster::SrvThruster(CEquip* InEquip, EqObj* InOwner)
    : ServerEquip(InEquip, InOwner)
{
}

// .text:06ced380 (RET 8).
void SrvThruster::notify(Event Code, void* Param)
{
    (void)Param;
    if (Code == Event_0xF)
    {
        // .text:06ced38c .. 06ced3c4 -- an AI-owned object (CSimple+0xb4 == 0,
        // read through the unguarded (ObjectClass & 3) narrowing) switches its
        // thrusters off: a tail jump into the owner's IObjAffect slot +0x54 with
        // both arguments rewritten to 0. A player's object ignores the event.
        if (Owner->csimple()->OwnerPlayer == 0)
            Owner->slot_54(false, 0);
        return;
    }
    if (Code == EquipDestroying)
    {
        // .text:06ced3c7 .. 06ced3d6 -- the base body, inlined.
        Equip->NotifyDisconnecting(this);
        Equip = nullptr;
    }
}

// operator new(0x0c) + FUN_06ced2d0 + vtable .rdata:06d65544. The notify body
// (.text:06d06a40) lives with the StarSystem code.
SrvTractor::SrvTractor(CEquip* InEquip, EqObj* InOwner)
    : ServerEquip(InEquip, InOwner)
{
}

// operator new(0x14) + FUN_06ced2d0, +0x0c = 0 (.text:06ce8b90), +0x10 =
// SUBOBJ_ID_NONE (.text:06ce8b9c), vtable .rdata:06d65554.
SrvRepairDroid::SrvRepairDroid(CEquip* InEquip, EqObj* InOwner)
    : ServerEquip(InEquip, InOwner)
    , RepairTimer(0.0f)
    , TargetId(SUBOBJ_ID_NONE)
{
}

// .text:06ced3f0 (RET 4).
bool SrvRepairDroid::update(float Dt)
{
    // .text:06ced40d .. 06ced429 -- no equip: false.
    CEquip* const Droid = Equip;
    if (Droid == nullptr)
        return false;

    // .text:06ced42c .. 06ced435 -- an inactive droid (vt +0x04) does nothing.
    if (!Droid->IsActive())
        return true;

    // .text:06ced43b .. 06ced451 -- pick a target when there is none.
    if (TargetId == SUBOBJ_ID_NONE)
        TargetId = pick_target();
    if (TargetId == SUBOBJ_ID_NONE)
        return true;

    // .text:06ced467 .. 06ced47c -- FLD/FADD/FST into +0x0c; only past 2.0f
    // (DAT_06d6573c; TEST AH,0x41 / JNZ skips <= and unordered). The timer keeps
    // counting otherwise.
    RepairTimer = Dt + RepairTimer;
    if (!(RepairTimer > 2.0f))
        return true;

    // .text:06ced482 .. 06ced4ae -- the target's relative health through the
    // equip owner's CEqObj slot +0xf0 (get_sub_obj_relative_health); a failed
    // lookup, or a target already at 1.0f or above (TEST AH,5 / JP, DAT_06d65520;
    // unordered too), drops the target.
    float Relative = 0.0f;
    if (!Droid->Owner->get_sub_obj_relative_health(TargetId, Relative) || !(Relative < 1.0f))
    {
        TargetId = SUBOBJ_ID_NONE;                                  // .text:06ced66c
        RepairTimer = 0.0f;                                         // .text:06ced678
        return true;
    }

    // .text:06ced4b4 .. 06ced514 -- a cause-0x11 list from this droid's owner:
    // inflictor = its get_id() (vt +0x20), owner = its CSimple+0xb4.
    DamageList Repair;
    Repair.Cause = static_cast<DamageCause>(0x11);
    Repair.InflictorId = Owner->get_id();
    Repair.InflictorOwner = Owner->csimple()->OwnerPlayer;

    // .text:06ced518 .. 06ced531 -- the target's hit points (slot +0xe4,
    // get_sub_obj_hit_pts); failing that the target is dropped.
    float Current = 0.0f;
    if (Droid->Owner->get_sub_obj_hit_pts(TargetId, Current))
    {
        // .text:06ced537 .. 06ced591 -- the droid pays for the repair out of its
        // own hit points: Amount = (float)(repair_rate (RepairDroid+0x70) *
        // timer), the droid's entry = hit points - Amount floored at 0
        // (TEST AH,0x41 / JNZ; GetHitPoints re-read for the store).
        float const Amount = static_cast<CERepairDroid*>(Droid)->RepairDroidArch()->RepairRate * RepairTimer;
        float DroidHitPts = 0.0f;
        if (Droid->GetHitPoints() - Amount > 0.0f)
            DroidHitPts = Droid->GetHitPoints() - Amount;
        Repair.add_damage_entry(Droid->Id, DroidHitPts, DamageEntry::SubObjFate::None);

        // .text:06ced593 .. 06ced5d4 -- the target's max: current / relative
        // when the relative health is above 0 (TEST AH,0x41 / JNZ), else slot
        // +0xec (get_sub_obj_max_hit_pts); 0 until then.
        float MaxHitPts = 0.0f;
        if (Relative > 0.0f)
            MaxHitPts = Current / Relative;
        else
            Droid->Owner->get_sub_obj_max_hit_pts(TargetId, MaxHitPts);

        // .text:06ced5d4 .. 06ced608 -- the target gains Amount, capped at its
        // max (TEST AH,5 / JP takes the max on >= and unordered).
        double const Repaired = static_cast<double>(Amount) + Current;
        float NewHitPts = MaxHitPts;
        if (Repaired < MaxHitPts)
            NewHitPts = static_cast<float>(Repaired);
        Repair.add_damage_entry(TargetId, NewHitPts, DamageEntry::SubObjFate::None);

        // .text:06ced60a .. 06ced614 -- the owner's apply_damage_list (vt +0x188).
        Owner->apply_damage_list(Repair);
    }
    else
    {
        TargetId = SUBOBJ_ID_NONE;                                  // .text:06ced61c
    }

    // .text:06ced629 .. 06ced678 -- the list torn down, the timer reset.
    RepairTimer = 0.0f;
    return true;
}

// FUN_06ced6a0 (.text:06ced6a0, RET 0).
unsigned short SrvRepairDroid::pick_target() const
{
    float Lowest = 1.0f;                                            // .text:06ced6b9
    unsigned short Best = SUBOBJ_ID_NONE;                           // .text:06ced6c1

    // .text:06ced6ab .. 06ced72e -- the equip owner's (CEquip+0x04) attached,
    // non-shield equipment, const traversal with the default traverser: a
    // relative health (vt +0x44) strictly below the best so far (TEST AH,5 /
    // JP) wins.
    CEqObj* const Ce = Equip->Owner;
    CEquipTraverser Walk;
    CEquipManager const& Equipment = Ce->EquipManager;
    for (CEquip const* Item = Equipment.Traverse(Walk); Item != nullptr; Item = Equipment.Traverse(Walk))
    {
        if (CAttachedEquip::cast(Item) == nullptr)
            continue;
        if (Item->Type == 0x100)
            continue;
        // .text:06ced705 FCOM float [Lowest] on the unrounded ST0; only the
        // winner is rounded (06ced714 FSTP float).
        double const Relative = Item->GetRelativeHealth();
        if (Relative < Lowest)
        {
            Lowest = (float)Relative;
            Best = Item->Id;
        }
    }

    // .text:06ced730 .. 06ced784 -- then every arch group the same way.
    CArchGrpTraverser Groups;
    CArchGroupManager const& GroupManager = Ce->ArchGroups;
    for (CArchGroup const* Group = GroupManager.Traverse(Groups); Group != nullptr;
         Group = GroupManager.Traverse(Groups))
    {
        // .text:06ced758 FCOM / 06ced766 FSTP float, as above.
        double const Relative = Group->GetRelativeHealth();
        if (Relative < Lowest)
        {
            Lowest = (float)Relative;
            Best = Group->Arch->Id;
        }
    }
    return Best;
}

// The launcher wrappers' shared base. Retail has no constructor of its own:
// the factory stores +0x0c = 0 inline (.text:06ce8a2b and siblings) and never
// writes +0x10 (operator new garbage); the port zeroes both.
SrvLauncher::SrvLauncher(CEquip* InEquip, EqObj* InOwner)
    : ServerEquip(InEquip, InOwner)
    , FireCount(0)
    , LastDamageTime(0.0)
{
}

// slot 4 -- .text:06ced7a0 (RET 4), shared by the mine and countermeasure
// tables; SourceClient is not read. The barrels' average world position plus
// their average direction scaled to 1e10 (DAT_06d65750) over its length
// ((y^2 + z^2) + x^2 on the x87 stack), handed to slot 3.
bool SrvLauncher::fire_forward(int SourceClient)
{
    (void)SourceClient;
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);
    Vector const Position = Launcher->GetAvgBarrelPosWS();          // .text:06ced7ae
    Vector Direction = Launcher->GetAvgBarrelDirWS();               // .text:06ced7bc

    double const Scale = 10000000000.0 /
        sqrt((static_cast<double>(Direction.y) * Direction.y + static_cast<double>(Direction.z) * Direction.z) +
             static_cast<double>(Direction.x) * Direction.x);
    Direction.x = static_cast<float>(Direction.x * Scale);
    Direction.y = static_cast<float>(Direction.y * Scale);
    Direction.z = static_cast<float>(Direction.z * Scale);

    Vector const Target = { Position.x + Direction.x, Direction.y + Position.y, Direction.z + Position.z };
    return fire(Target);                                            // vt +0x0c
}

// FUN_06ced840 (.text:06ced840, RET 4; the Vector is not read).
bool SrvLauncher::use_ammo(Vector const& Target)
{
    (void)Target;

    // .text:06ced843 .. 06ced869 -- a live equip (vt +0x08) on a live owner
    // (vt +0x160).
    if (Equip == nullptr || Equip->IsDestroyed())
        return false;
    if (Owner->is_destroyed())
        return false;

    ++FireCount;                                                    // .text:06ced875
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);

    // .text:06ced879 .. 06ced899 -- outside single player the ammunition must be
    // there (vt +0xa4 AmmoNeedsMet) and is paid for (vt +0x94).
    if (!SinglePlayer())
    {
        if (!Launcher->AmmoNeedsMet())
            return false;
        Launcher->ConsumeFireResources();
    }

    // .text:06ced89f .. 06ced921 -- the owner's player (CSimple+0xb4), its id
    // (vt +0x20), PlayerDB::is_valid inlined, and PlayerData+0x3e4 against the
    // id: a player's current ship with an ammunition-using projectile
    // (Projectile+0x78) loses one round of it from the player's cargo
    // (FUN_06d4f260: arch = the projectile's nickname, 1, 1.0f, no notify).
    unsigned int const OwnerPlayer = Owner->csimple()->OwnerPlayer;
    unsigned int const OwnerId = Owner->get_id();
    if (Players.is_valid(OwnerPlayer))
    {
        PlayerData& Player = Players[OwnerPlayer];
        if (Player.ship_object_id == OwnerId)
        {
            Archetype::Projectile const* const Projectile = Launcher->ProjectileArch();
            if (Projectile->bRequiresAmmo)
                PlayerData_RemoveCargo(Player, Projectile->Nickname.Id, 1, 1.0f, false);
        }
    }
    return true;
}

// .rdata:06d65574 -- operator new(0x18) + FUN_06ced2d0, +0x0c = 0.
SrvMineDropper::SrvMineDropper(CEquip* InEquip, EqObj* InOwner)
    : SrvLauncher(InEquip, InOwner)
{
}

// slot 3 -- .text:06ced940 (RET 4).
bool SrvMineDropper::fire(Vector const& Target)
{
    if (Equip == nullptr || Equip->IsDestroyed())
        return false;
    if (Owner->is_destroyed())
        return false;
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);
    // .text:06ced970 -- vt +0x90 CELauncher::Fire must answer 9 (FireOk).
    if (Launcher->Fire(Target) != FireOk)
        return false;
    Launcher->ConsumeFireResources();                              // vt +0x94
    ++FireCount;
    launch(Target);                                                // FUN_06ced9d0
    return true;
}

// slot 5 -- .text:06ced9a0 (RET 4).
bool SrvMineDropper::fire_unchecked(Vector const& Target)
{
    if (!use_ammo(Target))
        return false;
    launch(Target);
    return true;
}

// Fills the per-shot part of a launch record from one ProjLaunchInfo
// (.text:06cedab8 .. 06cedaf4: three dwords, REP MOVSD x9, three dwords).
static void SrvLauncher_CopyLaunchInfo(FServerLaunchRecord& Record, ProjLaunchInfo const& Info)
{
    Record.Position = Info.Pos;
    Record.Orientation = Info.Orient;
    Record.Velocity = Info.Velocity;
}

// FUN_06ced9d0 (.text:06ced9d0, RET 4).
void SrvMineDropper::launch(Vector const& Target)
{
    // .text:06ced9f4 .. 06ceda19 -- CELauncher::ComputeLaunchInfo (vt +0x98).
    std::vector<ProjLaunchInfo> Shots;
    static_cast<CELauncher*>(Equip)->ComputeLaunchInfo(Shots, Target);
    if (Shots.empty())
        return;

    // .text:06ceda4c .. 06cedaad -- the shared part: owner player, owner id, the
    // mine archetype (CEMineDropper::MineArch), the equip id. Retail leaves
    // +0x4c/+0x50 (guided-only) as stack garbage; the port value-initialises.
    FServerLaunchRecord Record = {};
    Record.EquipId = SUBOBJ_ID_NONE;
    Record.OwnerPlayer = Owner->csimple()->OwnerPlayer;
    Record.OwnerId = Owner->get_id();
    Record.Arch = static_cast<CEMineDropper*>(Equip)->MineArch();
    Record.EquipId = Equip->Id;

    // .text:06cedab8 .. 06cedb0a -- StarSystem FUN_06d08ef0 per shot, the
    // system re-read from the owner (+0x28) each time.
    for (std::vector<ProjLaunchInfo>::const_iterator It = Shots.begin(); It != Shots.end(); ++It)
    {
        SrvLauncher_CopyLaunchInfo(Record, *It);
        Owner->OwnerSystem->create_mine(Record);
    }
}

// .rdata:06d65590 -- operator new(0x18) + FUN_06ced2d0, +0x0c = 0.
SrvCounterMeasureDropper::SrvCounterMeasureDropper(CEquip* InEquip, EqObj* InOwner)
    : SrvLauncher(InEquip, InOwner)
{
}

// slot 0 -- .text:06cedb30 (RET 8).
void SrvCounterMeasureDropper::notify(Event Code, void* Param)
{
    (void)Param;
    if (Code == Event_0x19)
    {
        // .text:06cedb3c .. 06cedb7a -- an AI owner (CSimple+0xb4 == 0) drops a
        // countermeasure through its own IObjAffect slot +0x34: this equip's id
        // (a dword on the stack), count 1, no target position, client -1.
        if (Owner->csimple()->OwnerPlayer != 0)
            return;
        unsigned short const EquipId = Equip->Id;
        Owner->slot_34(&EquipId, 1, nullptr, -1);
        return;
    }
    if (Code == EquipDestroying)
    {
        // .text:06cedb81 .. 06cedb90 -- the base body, inlined.
        Equip->NotifyDisconnecting(this);
        Equip = nullptr;
    }
}

// slot 3 -- .text:06cedba0 (RET 4).
bool SrvCounterMeasureDropper::fire(Vector const& Target)
{
    if (Equip == nullptr || Equip->IsDestroyed())
        return false;
    if (Owner->is_destroyed())
        return false;
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);
    if (Launcher->Fire(Target) != FireOk)
        return false;
    Launcher->ConsumeFireResources();
    ++FireCount;
    launch(Target);                                                // FUN_06cedc30
    return true;
}

// slot 5 -- .text:06cedc00 (RET 4).
bool SrvCounterMeasureDropper::fire_unchecked(Vector const& Target)
{
    if (!use_ammo(Target))
        return false;
    launch(Target);
    return true;
}

// FUN_06cedc30 (.text:06cedc30, RET 4). SrvMineDropper::launch with the
// countermeasure archetype (CECounterMeasureDropper::CounterMeasureArch) and
// StarSystem FUN_06d092c0.
void SrvCounterMeasureDropper::launch(Vector const& Target)
{
    std::vector<ProjLaunchInfo> Shots;
    static_cast<CELauncher*>(Equip)->ComputeLaunchInfo(Shots, Target);
    if (Shots.empty())
        return;

    FServerLaunchRecord Record = {};
    Record.EquipId = SUBOBJ_ID_NONE;
    Record.OwnerPlayer = Owner->csimple()->OwnerPlayer;
    Record.OwnerId = Owner->get_id();
    Record.Arch = static_cast<CECounterMeasureDropper*>(Equip)->CounterMeasureArch();
    Record.EquipId = Equip->Id;

    for (std::vector<ProjLaunchInfo>::const_iterator It = Shots.begin(); It != Shots.end(); ++It)
    {
        SrvLauncher_CopyLaunchInfo(Record, *It);
        Owner->OwnerSystem->create_counter_measure(Record);
    }
}

// .rdata:06d655ac -- operator new(0x18) + FUN_06ced2d0, +0x0c = 0.
SrvGun::SrvGun(CEquip* InEquip, EqObj* InOwner)
    : SrvLauncher(InEquip, InOwner)
{
}

// slot 3 -- .text:06cedd90 (RET 4).
bool SrvGun::fire(Vector const& Target)
{
    if (Equip == nullptr || Equip->IsDestroyed())
        return false;
    if (Owner->is_destroyed())
        return false;
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);
    if (Launcher->Fire(Target) != FireOk)
        return false;
    Launcher->ConsumeFireResources();
    ++FireCount;

    // .text:06ceddd8 .. 06cede0e -- a seeking munition (Munition+0x90) goes out
    // as guided missiles (slot 7); anything else as bullets (slot 6), and only
    // outside single player.
    if (static_cast<CEGun*>(Equip)->MunitionArch()->Seeker != 0)
    {
        launch_guided(Target);                                      // vt +0x1c
        return true;
    }
    if (!SinglePlayer())
        launch_bullets(Target);                                     // vt +0x18
    return true;
}

// slot 4 -- .text:06cede30 (RET 4).
bool SrvGun::fire_forward(int SourceClient)
{
    // .text:06cede4b .. 06ceded7 -- the base aim ((z^2 + y^2) + x^2 here).
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);
    Vector const Position = Launcher->GetAvgBarrelPosWS();
    Vector Direction = Launcher->GetAvgBarrelDirWS();

    double const Scale = 10000000000.0 /
        sqrt((static_cast<double>(Direction.z) * Direction.z + static_cast<double>(Direction.y) * Direction.y) +
             static_cast<double>(Direction.x) * Direction.x);
    Direction.x = static_cast<float>(Direction.x * Scale);
    Direction.y = static_cast<float>(Direction.y * Scale);
    Direction.z = static_cast<float>(Direction.z * Scale);
    Vector Target = { Position.x + Direction.x, Direction.y + Position.y, Direction.z + Position.z };

    // .text:06cedede .. 06cedef0 -- slot 3; a refused shot answers false.
    bool const bFired = fire(Target);
    if (!bFired)
        return false;

    // .text:06cedef6 .. 06cedefe -- no system, nothing to announce.
    StarSystem* const System = Owner->OwnerSystem;
    if (System == nullptr)
        return bFired;

    // .text:06cedf04 .. 06cedf7c -- outside single player, a locked target
    // (CEGun::GetTarget) turns the aim point into the target's frame: the math
    // engine's InverseTransform (slot +0x3c) over the target's Transform
    // (EngineObject+0x08 orientation, +0x2c position).
    if (!SinglePlayer())
    {
        CSimple* LockTarget = nullptr;
        unsigned short LockSubId = SUBOBJ_ID_NONE;
        static_cast<CEGun*>(Equip)->GetTarget(LockTarget, LockSubId);
        if (LockTarget != nullptr)
        {
            if (GSrvGunMathEngine == nullptr)
                GSrvGunMathEngine = Server_Acquire3DMathEngine();
            FVector Local;
            GSrvGunMathEngine->InverseTransform(
                Local, *reinterpret_cast<FTransform const*>(&LockTarget->Orientation),
                reinterpret_cast<FVector const&>(Target));
            Target = reinterpret_cast<Vector const&>(Local);
        }
    }

    // .text:06cedf7d .. 06cedfd6 -- a one-element id vector (the inlined
    // vector<unsigned short>::insert(begin, 1, id)), the owner's id, and
    // StarSystem FUN_06d0e2e0.
    std::vector<unsigned short> EquipIds;
    EquipIds.insert(EquipIds.begin(), 1, Equip->Id);
    unsigned int const OwnerId = Owner->get_id();
    System->notify_fire_weapon(OwnerId, EquipIds, Target, SourceClient);
    return bFired;
}

// slot 5 -- .text:06cee000 (RET 4). No bullets here: only a seeking munition
// is launched (slot 7) after use_ammo.
bool SrvGun::fire_unchecked(Vector const& Target)
{
    if (!use_ammo(Target))
        return false;
    if (static_cast<CEGun*>(Equip)->MunitionArch()->Seeker != 0)
        launch_guided(Target);                                      // vt +0x1c
    return true;
}

// slot 6 -- .text:06cee1f0 (RET 4).
void SrvGun::launch_bullets(Vector const& Target)
{
    std::vector<ProjLaunchInfo> Shots;
    static_cast<CELauncher*>(Equip)->ComputeLaunchInfo(Shots, Target);   // vt +0x98

    // .text:06cee23f .. 06cee270 -- the system (+0x28) read once, the owner id,
    // the munition archetype, the equip id.
    StarSystem* const System = Owner->OwnerSystem;
    FServerBulletRecord Record = {};
    Record.OwnerId = Owner->get_id();
    Record.Arch = static_cast<CEGun*>(Equip)->MunitionArch();
    Record.EquipId = Equip->Id;

    // .text:06cee276 .. 06cee2c4 -- StarSystem FUN_06d099e0 per shot.
    for (std::vector<ProjLaunchInfo>::const_iterator It = Shots.begin(); It != Shots.end(); ++It)
    {
        Record.Position = It->Pos;
        Record.Orientation = It->Orient;
        Record.Velocity = It->Velocity;
        System->create_bullet(Record);
    }
}

// slot 7 -- .text:06cee040 (RET 4).
void SrvGun::launch_guided(Vector const& Target)
{
    std::vector<ProjLaunchInfo> Shots;
    static_cast<CELauncher*>(Equip)->ComputeLaunchInfo(Shots, Target);   // vt +0x98
    if (Shots.empty())
        return;

    // .text:06cee0c4 .. 06cee12a -- the shared part (+0x4c = 0, +0x50 = NONE
    // until the first shot fills them).
    FServerLaunchRecord Record = {};
    Record.EquipId = SUBOBJ_ID_NONE;
    Record.TargetId = 0;
    Record.TargetSubId = SUBOBJ_ID_NONE;
    Record.OwnerPlayer = Owner->csimple()->OwnerPlayer;
    Record.OwnerId = Owner->get_id();
    Record.Arch = static_cast<CEGun*>(Equip)->MunitionArch();
    Record.EquipId = Equip->Id;

    // .text:06cee146 .. 06cee1c2 -- per shot: the lock target's id (CSimple
    // +0xb0) or 0, its sub-object, and StarSystem FUN_06d09640 (system re-read
    // each time).
    for (std::vector<ProjLaunchInfo>::const_iterator It = Shots.begin(); It != Shots.end(); ++It)
    {
        SrvLauncher_CopyLaunchInfo(Record, *It);
        Record.TargetId = (It->LockTarget != nullptr) ? It->LockTarget->Id : 0;
        Record.TargetSubId = It->LockSubId;
        Owner->OwnerSystem->create_guided(Record);
    }
}

// ---------------------------------------------------------------------------
// Ship -- .text:06d00e80 / create .text:06d01040
// ---------------------------------------------------------------------------

// .text:06d00e80. Retail zeroes its whole own block, builds the EquipDescList at
// +0xA0..+0xA8 (the 0x28-byte self-linked head), and constructs the EquipIdMaker
// at +0xC4 last. See server_gameobject.h for the instruction map.
Ship::Ship()
{
    DeathSequenceObjId = 0;      // .text:06d00eab
    DeathSequenceTimer = 0.0f;   // .text:06d00eb1
    Field_0x98 = 0;      // .text:06d00eb7
    PendingRequest = nullptr;    // .text:06d00ebd
    // .text:06d00ec3 .. .text:06d00ee2 (allocator byte +0xA0, operator new(0x28)
    // self-linked head +0xA4, size +0xA8 = 0): the Jettison member's
    // EquipDescList constructor, run before this body.
    JettisonTimer = 0.0f;        // .text:06d00ef3
    PendingBayCloses = 0;        // .text:06d00ef9
    // +0xB4 is deliberately not touched: retail's constructor never writes it
    // (there is no store between .text:06d00ef9 and .text:06d00eff). Zeroed
    // here anyway, because leaving a member indeterminate buys nothing.
    Field_0xB4 = nullptr;
    Field_0xB8 = 0;                    // .text:06d00eff
    AccumulatedDamage = 0.0f;          // .text:06d00f05
    AccumulatedDamageTime = 0.0f;      // .text:06d00f0b
    // EquipIds at +0xC4 constructs itself (.text:06d00f11).
}

// .text:06d00f70. CAUTION when reading that listing: it is reached through the
// CBase vtable at Ship+0x0C, so its ECX is `object + 0x0C` and every offset in
// it is 0x0C LOW. `MOV EBX,[ESI+0x98]` at .text:06d00fc0 is Ship+0xA4, the
// jettison list's sentinel, and `MOV ECX,[ESI+0x9c]` at .text:06d00fe9 is
// Ship+0xA8, its size -- NOT the scalars this port calls Field_0x98 and
// PendingRequest. The three stores at .text:06d00f92 / .text:06d00f98 /
// .text:06d00f9f are what prove the adjustment: they rewrite the same three
// vptrs the constructor stamps at .text:06d00f1b / .text:06d00f21 /
// .text:06d00f28, at +0x00, +0x04 and +0x0C of the object.
Ship::~Ship()
{
    // .text:06d00fa5 / .text:06d00fae -- PUSH EDI (the Ship base) / CALL
    // FUN_06d00180: drop this ship from the dying-ship list.
    Ship_ForgetDying(this);

    // .text:06d00fb3 .. .text:06d00fba -- CALL [vtable + 0x15c] with the Ship
    // vtable just re-stamped, i.e. this class's own destroy_notify.
    destroy_notify();

    // .text:06d00fc0 .. .text:06d01016 -- every node unlinked and freed, then
    // the head (the inlined EquipDescList destructor; Unwind@06d60ef8 runs the
    // imported one on the exceptional path). That is the Jettison member's
    // destructor, which runs after this body and before ~EqObj (.text:06d01024).
}

// ---------------------------------------------------------------------------
// The dying-ship list (server_gameobject.h). FUN_06d00090 / FUN_06d000d0 are
// this definition's construction and atexit destruction.
// ---------------------------------------------------------------------------
std::list<Ship*> GDyingShips;

// FUN_06d00130 (.text:06d00130 .. 06d00172, cdecl, one argument): operator
// new(0xc), link before the sentinel (a push_back), node value = the ship,
// ++DAT_06d8d8e8.
void Ship_QueueDying(Ship* DyingShip)
{
    GDyingShips.push_back(DyingShip);
}

// FUN_06d00180 (.text:06d00180 .. 06d001cb, cdecl, one argument): walk from
// the first node to the sentinel; every node whose value (+0x08) equals the
// ship is unlinked, freed and the size decremented -- the walk does not stop
// at the first match.
void Ship_ForgetDying(Ship* DyingShip)
{
    std::list<Ship*>::iterator It = GDyingShips.begin();
    while (It != GDyingShips.end()) {
        if (*It == DyingShip)
            It = GDyingShips.erase(It);
        else
            ++It;
    }
}

bool Ship::create(Archetype::Root* Arch)
{
    // .text:06d01042: PUSH 0x503.
    return CreateOfClass(CObject::ClassShip, Arch);
}

// Retail .text:06d02ae0 (five instructions):
//   MOV EAX,[ECX+0xb4] / MOV ECX,[ESP+4] / MOV [ECX],EAX / XOR EAX,EAX / RET 4
int Ship::get_data(void const*& OutData) const
{
    OutData = Field_0xB4;
    return 0;
}

bool Ship::find(unsigned int const& Id)
{
    // .text:06d010a5: PUSH 0x503.
    return FindOfClass(CObject::ClassShip, Id);
}

// .text:06d01c30. See the declaration for the instruction map.
unsigned short Ship::allocate_equip_id()
{
    // .text:06d01c30 .. 06d01c44: the (& 3) == 3 accessor may yield null
    // (.text:06d01c42 XOR EAX,EAX) and [EAX+0xb4] is read regardless -- retail
    // has no null test here.
    if (csimple()->OwnerPlayer != 0)
    {
        // .text:06d01c60 .. .text:06d01c7f -- strings read off the PE
        // (.rdata:06d67398 / .rdata:06d6736c), then *SUBOBJ_ID_NONE
        // (.text:06d01c81). (2026-09-24, claude:srv-pdb: the old comment said
        // "the port has no FDUMP", which was never true.)
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\ship.cpp", 0x1e5,
              "Ship::allocate_equip_id (): Player tried to allocate id!");
        return SUBOBJ_ID_NONE;
    }
    // .text:06d01c4e/.text:06d01c54 -- a TAIL JMP into
    // SubObjectID::EquipIdMaker::CreateEquipID on this + 0xc4.
    return EquipIds.CreateEquipID();
}

CShip* Ship::cship() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x503u) != 0x503u)
        return nullptr;
    return static_cast<CShip*>(Obj);
}

// FUN_06d010e0 (.text:06d010e0, RET 4), all ten instructions:
//   .text:06d010e1  MOV  ESI,ECX
//   .text:06d010e3  CALL FUN_06ce7d00              ; EqObj +0x1bc body, DIRECT
//   .text:06d010e8  MOV  EAX,[ESP + 0x8]           ; &State
//   .text:06d010ec  MOV  CX,word ptr [EAX]
//   .text:06d010ef  MOV  word ptr [ESI + 0xc4],CX  ; EquipIds
//   .text:06d010f6  MOV  EDX,dword ptr [EAX + 0x4]
//   .text:06d010f9  MOV  dword ptr [ESI + 0xb4],EDX ; Field_0xB4
void Ship::set_equip_state(FEquipState const& State)
{
    EqObj::check_fuses();
    EquipIds = State.EquipIds;
    Field_0xB4 = State.Data;
}

// FUN_06d01110 (.text:06d01110, RET 0):
//   .text:06d01110  MOV  AL,[ECX + 0xb8] / TEST / JNZ -> return
//   .text:06d0111d  MOV  EAX,1
//   .text:06d01126  MOV  byte [ECX + 0xb8],AL      ; latch
//   .text:06d0112c  MOV  [ESP],EAX                 ; Cmd.Type = 1
//   .text:06d01130  MOV  byte [ESP + 4],AL         ; Cmd.bProtect = 1
//   .text:06d01134  MOV  byte [ESP + 5],AL         ; Cmd.bFromPlayers = 1
//   .text:06d0113b  MOV  dword [ESP + 8],0         ; Cmd.MaxHpLoss = 0.0f
//   .text:06d01143  CALL [EAX + 0x194]             ; set_invincible(&Cmd)
void Ship::apply_launch_invincibility()
{
    if (Field_0xB8 != 0)
        return;
    Field_0xB8 = 1;
    FInvincibility Cmd = {};
    Cmd.Type = 1u;
    Cmd.bProtect = true;
    Cmd.bFromPlayers = true;
    Cmd.MaxHpLoss = 0.0f;
    set_invincible(Cmd);
}

// FUN_06d01150 (.text:06d01150, RET 4). EDI is the RAW CBase::Object
// (.text:06d0116f `MOV EDI,[ESI+0x10]`), used with no class or null test.
void Ship::tick_zone_damage(float Dt)
{
    CObject* const Raw = Object;
    CSimple* const Simple = static_cast<CSimple*>(Raw);

    // .text:06d01172  CALL [EAX + 0x19c] (is_invincible) / JNZ -> return
    if (is_invincible())
        return;

    // .text:06d01180  MOV ECX,[EDI + 0xa4]  -- the zone CSimple::update_zones
    // cached (Common ZONE, vftable .rdata:063a5094): slot +0x5c
    // (.rdata:063a50f0) -> .text:0633ad80 `FLD [ECX+0x60]; RET`, the zone's
    // damage rate.
    Universe::IZone* const Zone = Simple->ZoneAtA4;
    if (Zone != nullptr)
    {
        float const Rate = Zone->GetDamage();                          // .text:06d0118e
        // .text:06d01191 FCOM 0.0f (.rdata:06d65188) / TEST AH,0x41 / JNZ:
        // skip when Rate <= 0.
        if (Rate > 0.0f)
        {
            // .text:06d0119e FMUL [EDI+0xa8] / .text:06d011a4 FMUL dt /
            // .text:06d011a8 FADD [ESI+0xbc] / .text:06d011ae FSTP [ESI+0xbc]
            AccumulatedDamage = Rate * Simple->ScannerInterference * Dt + AccumulatedDamage;
        }
    }

    // .text:06d011b8 FLD dt / FADD [ESI+0xc0] / FST [ESI+0xc0]
    AccumulatedDamageTime = Dt + AccumulatedDamageTime;

    // .text:06d011ce FCOMP 0.0f / TEST AH,0x44 / JNP -> return when equal.
    if (AccumulatedDamage == 0.0f)
        return;
    // .text:06d011df FCOMP 1.0f (.rdata:06d67088 = 0x3f800000) /
    // TEST AH,0x41 / JNZ -> return when the time is <= 1.0.
    if (AccumulatedDamageTime <= 1.0f)
        return;

    // .text:06d011f4 CALL [EAX + 0xd4] on the raw object -- Common's CShip
    // vtable (.rdata:0639c02c + 0xd4 = .rdata:0639c100) holds
    // CEqObj::get_total_max_hit_pts there. .text:06d011fa FDIVR [ESI+0xbc]:
    // accumulated / total. .text:06d01200 FST to the dt slot.
    float const Ratio = AccumulatedDamage / static_cast<CEqObj*>(Raw)->get_total_max_hit_pts();
    // .text:06d01204 FCOMP 0.01f (.rdata:06d6708c = 0x3c23d70a) /
    // TEST AH,0x41 / JNZ -> return when Ratio <= 0.01.
    if (Ratio <= 0.01f)
        return;

    // .text:06d01219 .. 06d01250 -- a DamageList built inline on the stack:
    // operator new(0x14) self-linked head (+0x04), +0x08 = 0, byte +0x0c = 0,
    // +0x14 = +0x18 = 0 and +0x10 (the cause) = 8. No DamageList constructor
    // or set_cause call: the field is stored directly. 8 has no name in
    // DamageCause (common_misc.h names 0..5 only).
    DamageList Damage;
    Damage.Cause = static_cast<DamageCause>(8);

    apply_damage_ratio(Ratio, Damage);             // .text:06d01258 CALL FUN_06d012e0
    apply_damage_list(Damage);                     // .text:06d01266 CALL [EDX + 0x188]
    AccumulatedDamage = 0.0f;                      // .text:06d01270 MOV [ESI+0xbc],EBX
    // .text:06d01276 .. 06d012ae -- the list's nodes and head are freed
    // (FUN_06d5ffb0 == operator delete): ~DamageList.
}

// FUN_06d012e0 (.text:06d012e0, RET 8). EDI = this, EBP = the RAW
// CBase::Object (.text:06d012f3), kept in a local at .text:06d012f8.
bool Ship::apply_damage_ratio(float Ratio, DamageList& Damage)
{
    CObject* const Raw = Object;

    // .text:06d012e3 FLD Ratio / .text:06d012e8 FCOMP 1.0f (.rdata:06d65520) /
    // TEST AH,0x41 / JNZ LAB_06d01331 when Ratio <= 1.0.
    if (Ratio > 1.0f)
    {
        // .text:06d01308 CALL FUN_06ce6870 -- `[ECX+0x10]`, nulled unless
        // (ObjectClass & 0x103) == 0x103, i.e. ceqobj(); .text:06d0130f
        // CSimple::get_hit_pts on it, FSTP as the float argument;
        // .text:06d0131b CALL [EBX + 0x1b0] (damage_hull), result ignored.
        damage_hull(ceqobj()->get_hit_pts(), Damage);
        Damage.bDestroyed = true;                  // .text:06d01322 MOV byte [ESI+0xc],1
        return true;                               // .text:06d01328 MOV AL,1
    }

    // .text:06d01333 MOV byte [ESP+0x13],0 -- the result byte.
    bool bAnyDestroyed = false;

    // .text:06d01338 CSimple::get_max_hit_pts on the raw object (ECX = EBP),
    // .text:06d0133e FMUL Ratio, .text:06d01354 CALL [EAX + 0x1b0].
    if (damage_hull(static_cast<CSimple*>(Raw)->get_max_hit_pts() * Ratio, Damage))
        bAnyDestroyed = true;                      // .text:06d0135e

    // .text:06d01363 MOV AL,[ESI+0xc] / TEST / JNZ -- equipment pass only while
    // the list does not say destroyed.
    if (!Damage.bDestroyed)
    {
        // .text:06d0136a PUSH 0x1ee0 / CEquipTraverser::CEquipTraverser(int);
        // .text:06d01383 ADD EBP,0xe4 -- CEqObj::EquipManager.
        CEquipTraverser Walk(0x1ee0);
        CEquipManager& Equipment = static_cast<CEqObj*>(Raw)->EquipManager;
        for (CEquip* Equip = Equipment.Traverse(Walk); Equip != nullptr;
             Equip = Equipment.Traverse(Walk))
        {
            // .text:06d01398 CALL [EAX + 0x38] -- CEquip slot 14,
            // GetMaxHitPoints (common_cequip.h slot order); FMUL Ratio;
            // .text:06d013b2 CALL [EDX + 0x210] (damage_equip) / TEST AL.
            if (damage_equip(Equip, Equip->GetMaxHitPoints() * Ratio, Damage))
                bAnyDestroyed = true;              // .text:06d013bc
        }
    }

    // .text:06d013d4 re-reads byte +0xc of the list.
    if (!Damage.bDestroyed)
    {
        // .text:06d013e9 ADD EBP,0x144 -- CEqObj::ArchGroups. The traverser is
        // one byte set to 1 inline (.text:06d013f2 MOV byte [ESP+0x1c],1),
        // which is what CArchGrpTraverser's constructor does (AtStart).
        CArchGrpTraverser Walk;
        CArchGroupManager& Groups = static_cast<CEqObj*>(Raw)->ArchGroups;
        for (CArchGroup* Group = Groups.Traverse(Walk); Group != nullptr;
             Group = Groups.Traverse(Walk))
        {
            // .text:06d01402 CArchGroup::GetMaxHitPoints / 06d01408 FMUL Ratio
            // on the unrounded ST0 / 06d01412 FSTP float argument /
            // .text:06d0141f CALL [EDX + 0x218] (damage_group) / TEST AL.
            if (damage_group(Group, (float)(Group->GetMaxHitPoints() * Ratio), Damage))
                bAnyDestroyed = true;              // .text:06d01429
        }
    }

    return bAnyDestroyed;                          // .text:06d0143d MOV AL,[ESP+0x13]
}

// FUN_06d01550 (.text:06d01550, RET 4). ESI = this; EBX = 0 throughout.
// Stack base S = ESP after the four register pushes of the eject path; the
// FLootRecord is built at S+0x2c.
void Ship::tick_jettison(float Dt)
{
    // .text:06d0155a .. 06d01572  FLD +0xac / FCOMP 0.0f, then the queue size
    // (+0xa8) decides the arm.
    if (Jettison.Items.size() == 0u)
    {
        // .text:06d01574 TEST AH,0x44 / JNP -> return when the timer is 0.
        if (JettisonTimer == 0.0f)
            return;
        // .text:06d0157d .. 06d0159b  FLD / FSUB dt / FST, FCOMP 0.0f,
        // TEST AH,0x41 / JP -> return while it is still > 0 (or unordered).
        JettisonTimer = JettisonTimer - Dt;
        if (!(JettisonTimer <= 0.0f))
            return;
        // .text:06d015a1 .. 06d015cc -- the owed bay closes, one
        // CShip::close_bay on the raw [ESI+0x10] each (its float result is
        // dropped, .text:06d015b9 FSTP ST0).
        while (PendingBayCloses > 0)
        {
            static_cast<CShip*>(Object)->close_bay();
            --PendingBayCloses;
        }
        JettisonTimer = 0.0f;                               // .text:06d015ce
        return;
    }

    // .text:06d015df TEST AH,0x44 / JNP -- only a running timer counts down;
    // .text:06d015e4 .. 06d01604 clamps it to 0 once it is <= 0 (JP skips
    // the clamp while it is > 0 or unordered).
    if (JettisonTimer != 0.0f)
    {
        JettisonTimer = JettisonTimer - Dt;
        if (JettisonTimer <= 0.0f)
            JettisonTimer = 0.0f;
    }
    // .text:06d0160a .. 06d0161b  FCOMP 0.0f / TEST AH,0x44 / JP -> return
    // unless the timer is exactly 0.
    if (JettisonTimer != 0.0f)
        return;
    // .text:06d01621 .. 06d0162b  the raw CShip's BayState (+0x2ac) must be 2.
    if (static_cast<CShip*>(Object)->BayState != 2)
        return;

    // .text:06d01631 .. 06d0163b  EDI = &head->Next->Value: the front item.
    EquipDesc& Desc = Jettison.Items.front();

    // .text:06d0163e .. 06d0166e  +0x00 .. +0x1c zeroed, +0x68 = 1, +0x69 = 0.
    // The rest is filled below before anything reads it.
    FLootRecord Record;
    Record.Id = 0u;
    Record.Arch = nullptr;
    Record.HitPts = 0.0f;
    Record.ContentsArch = nullptr;
    Record.ContentsHitPts = 0.0f;
    Record.Count = 0;
    Record.Field_0x18 = 0u;
    Record.Field_0x1c = 0u;
    Record.Field_0x68 = 1;
    Record.bMission = 0;

    // .text:06d01675 .. 06d0168a -- CFLIDMaker::GetInstance, the id counter at
    // +4 masked with 0x3fffffff and post-incremented (inline
    // CFLIDMaker::AllocateObjectID).
    Record.Id = CFLIDMaker::GetInstance()->AllocateObjectID();

    // .text:06d0168e .. 06d01698  Archetype::GetEquipment(Desc +0x04).
    Archetype::Equipment* const Contents = Archetype::GetEquipment(Desc.ArchId);
    Record.ContentsArch = Contents;
    // .text:06d0169c FLD Desc +0x10 / FMUL Arch +0x1c / FSTP rec +0x10.
    Record.ContentsHitPts = Desc.Health * Contents->HitPts;
    // .text:06d016ab .. 06d016b4  CALL [EDX + 0x1c] -- get_loot_appearance.
    Archetype::Equipment* const LootArch = Contents->get_loot_appearance();
    Record.Arch = LootArch;
    // .text:06d016b8 .. 06d016cd  same nickname -> the contents' hit points,
    // else the loot archetype's own (+0x1c).
    if (LootArch->Nickname.Id == Contents->Nickname.Id)
        Record.HitPts = Record.ContentsHitPts;
    else
        Record.HitPts = LootArch->HitPts;

    // .text:06d016d1 MOV EBP,[EDI + 0x14] / .text:06d016e5 -> rec +0x14.
    int const Count = Desc.Count;
    Record.Count = Count;

    // .text:06d016d4 .. 06d016e9  FUN_06d01860(&rec +0x2c, &rec +0x38, &rec +0x20).
    compute_jettison_launch(&Record.Position, &Record.Orientation, &Record.Velocity);

    // .text:06d016ee .. 06d017db -- the spin: two rand() draws, each
    // rand() * 2^-15 (.rdata:06d656b8 = 0x38000000) * (double)0.8f
    // (.rdata:06d656b0) - (double)0.4f (.rdata:06d656a8), stored as a float;
    // the first scales the orientation's column 0, the second its column 2.
    Matrix const& M = Record.Orientation;
    float const A = static_cast<float>(
        static_cast<double>(static_cast<float>(rand()) * 3.0517578125e-05f) *
        static_cast<double>(0.8f) - static_cast<double>(0.4f));
    float const Ax = M.E[0][0] * A;                         // .text:06d0172c
    float const Ay = M.E[1][0] * A;                         // .text:06d01738
    float const Az = M.E[2][0] * A;                         // .text:06d01744
    float const B = static_cast<float>(
        static_cast<double>(static_cast<float>(rand()) * 3.0517578125e-05f) *
        static_cast<double>(0.8f) - static_cast<double>(0.4f));
    // .text:06d01785 .. 06d017db  column 2 * B, plus the column-0 terms.
    Record.AngularVelocity.x = M.E[0][2] * B + Ax;
    Record.AngularVelocity.y = M.E[1][2] * B + Ay;
    Record.AngularVelocity.z = M.E[2][2] * B + Az;

    // .text:06d017e2 .. 06d017f6  CALL [EAX + 0x20] (get_id), then
    // FUN_06d08a40 on OwnerSystem (+0x28) with (id, 1, &rec, 1).
    OwnerSystem->create_loot(get_id(), 1, &Record, 1);

    // .text:06d017fb .. 06d01802  EquipDesc::set_count(count - rec.Count).
    Desc.set_count(Count - Record.Count);

    // .text:06d01808 CMP [EDI + 0x14],EBX / JNZ -- an emptied stack is dropped:
    // one more bay close owed (.text:06d0180d), the front node unlinked and
    // freed (.text:06d01813 .. 06d0182b), size - 1 (.text:06d01839): pop_front.
    if (Desc.Count == 0)
    {
        ++PendingBayCloses;
        Jettison.Items.erase(Jettison.Items.begin());
    }

    // .text:06d01840 .. 06d01849  COMMON.DLL::CShip::JETTISON_CLEARANCE_TIME.
    JettisonTimer = CShip::JETTISON_CLEARANCE_TIME;
}

// FUN_06d01860 (.text:06d01860, RET 0xc). EBX = this; ESI = the raw
// CBase::Object's archetype (+0x88) read as Archetype::Ship, with no test.
// Stack base S = ESP after the three register pushes.
void Ship::compute_jettison_launch(Vector* OutPos, Matrix* OutOrient, Vector* OutVel)
{
    Archetype::Ship const* const Arch =
        static_cast<Archetype::Ship const*>(Object->Archetype);   // .text:06d0186c

    // .text:06d01872 .. 06d018af -- both bay hardpoints must exist on the
    // engine instance (IObjInspect +0x1c get_index): HpBaySurface (+0xe0)
    // into S+0xc, HpBayExternal (+0xe4) into S+0x10.
    long Surface = 0;
    long External = 0;
    Vector Dir;
    if (FindHardpoint(get_index(), Arch->HpBaySurface.Value, Surface) &&
        FindHardpoint(get_index(), Arch->HpBayExternal.Value, External))
    {
        // .text:06d018b1 .. 06d018ce  HardpointPosition(Surface, name, OutPos, OutOrient)
        HardpointPosition(Surface, Arch->HpBaySurface.Value, OutPos, OutOrient);
        // .text:06d018d0 .. 06d018e3  HardpointPosition(External, name, &S+0x20, 0)
        Vector ExternalPos;
        HardpointPosition(External, Arch->HpBayExternal.Value, &ExternalPos, nullptr);
        // .text:06d018e5 .. 06d01902  exterior minus surface.
        Dir.x = ExternalPos.x - OutPos->x;
        Dir.y = ExternalPos.y - OutPos->y;
        Dir.z = ExternalPos.z - OutPos->z;
    }
    else
    {
        // .text:06d01907 .. 06d0191e  the raw object's position (+0x2c).
        *OutPos = Object->WorldPosition;
        // .text:06d01921 .. 06d01932  its orientation (+0x08), REP MOVSD 9.
        *OutOrient = Object->Orientation;
        // .text:06d01934 .. 06d01953  minus column 1 of that matrix
        // (+0x04 / +0x10 / +0x1c, each FCHS).
        Dir.x = -OutOrient->E[0][1];
        Dir.y = -OutOrient->E[1][1];
        Dir.z = -OutOrient->E[2][1];
    }

    // .text:06d01955 .. 06d01970  the direction goes into OutVel.
    *OutVel = Dir;

    // .text:06d01973 .. 06d019aa  speed = (rand() * 2^-15 * 0.5 + 0.75)
    // (.rdata:06d656d8 / .rdata:06d656d0, doubles) * JETTISONED_CARGO_VELOCITY.
    double const Speed =
        (static_cast<double>(static_cast<float>(rand()) * 3.0517578125e-05f) * 0.5 + 0.75) *
        static_cast<double>(JETTISONED_CARGO_VELOCITY);
    // .text:06d019ac .. 06d019c4  |OutVel| = sqrt((x*x + y*y) + z*z); the scale
    // Speed / |OutVel| is stored as a float (.text:06d019c4 FSTP [ESP+0x34]).
    double const Length = sqrt(
        (static_cast<double>(OutVel->x) * OutVel->x + static_cast<double>(OutVel->y) * OutVel->y) +
        static_cast<double>(OutVel->z) * OutVel->z);
    float const Scale = static_cast<float>(Speed / Length);
    OutVel->x = Scale * OutVel->x;                          // .text:06d019d4
    OutVel->y = Scale * OutVel->y;                          // .text:06d019dd
    OutVel->z = Scale * OutVel->z;                          // .text:06d019e7

    // .text:06d019ea CALL [EAX + 0x4] -- IObjInspect::get_velocity on this,
    // added component by component (.text:06d019ef .. 06d01a05).
    Vector const Velocity = get_velocity();
    OutVel->x = Velocity.x + OutVel->x;
    OutVel->y = Velocity.y + OutVel->y;
    OutVel->z = Velocity.z + OutVel->z;
}

// LAB_06d01450 (.text:06d01450) — Ship vtable +0x16c. Term-by-term vs listing.
bool Ship::update(float Dt)
{
    // .text:06d01459  CALL FUN_06ce7e20 (EqObj::update)
    if (!EqObj::update(Dt))
        return false;

    // .text:06d01467  MOV EAX,[ESI+0x90] / TEST / JNZ death-sequence
    if (DeathSequenceObjId == 0u)
    {
        // .text:06d01471 .. .text:06d01486 -- ECX = [ESI+0x10] narrowed with
        // (ObjectClass & 3) == 3, then .text:06d01488 reads CSimple::OwnerPlayer
        // (+0xb4) through the result with NO null test. The CObject::update call
        // itself goes through the raw [ESI+0x10] (ECX from .text:06d01471 on the
        // unowned path, reloaded at .text:06d014a3 on the other).
        CObject* const Raw = Object;
        CSimple* Simple = nullptr;
        if (Raw != nullptr &&
            (static_cast<unsigned>(Raw->ObjectClass) & 0x3u) == 0x3u)
            Simple = static_cast<CSimple*>(Raw);

        if (Simple->OwnerPlayer == 0u)
        {
            // .text:06d01492 MOV EAX,[->CObject::NO_SCANNER] / .text:06d014b1
            // CALL [EDX + 0x50] (CObject::update).
            Raw->update(Dt, CObject::NO_SCANNER);
        }
        else if (SinglePlayer() == 0)
        {
            // .text:06d014a3 .. .text:06d014b1 -- CObject::UPDATE_DUMB.
            Object->update(Dt, CObject::UPDATE_DUMB);
        }
        // else: owned + single player -- .text:06d014a1 JNZ LAB_06d014b4 skips
        // CObject::update.

        tick_jettison(Dt);                                      // .text:06d014b7
        tick_zone_damage(Dt);                                   // .text:06d014bf
        return true;                                            // .text:06d014c5
    }

    // Death-sequence arm (.text:06d014cb ..). ECX = [ESI+0x10], the raw
    // CBase::Object (.text:06d014d3), read without a class test or null test.
    CObject* const DeathObj = Object;
    if (SinglePlayer() != 0)
    {
        // .text:06d014d8  MOV AL,[ECX+0x214] -- CShip::bJumpedOut, no class test.
        if (((CShip*)DeathObj)->bJumpedOut == 0)
            return true;
        // .text:06d014e2 / .text:06d014e3 -- PUSH ESI / CALL FUN_06d00130.
        Ship_QueueDying(this);
        DeathSequenceObjId = 0u;                               // .text:06d014ec
        return true;
    }

    // MP (.text:06d014fc): bail while CShip+0x2bd (bIsPlayer) is set.
    if (static_cast<CShip*>(DeathObj)->bIsPlayer != 0)
        return true;

    // .text:06d01506 .. 06d01521 -- FLD/FSUB/FST, FCOMP against 0.0f
    // (.rdata:06d65188), `TEST AH,0x41 / JP` -> leave unless <= 0.
    DeathSequenceTimer -= Dt;
    if (DeathSequenceTimer <= 0.0f)
    {
        // .text:06d01523 / .text:06d01525 -- PUSH 1 / CALL
        // CObject::enable_collisions_r with ECX still [ESI+0x10].
        DeathObj->enable_collisions_r(true);
        Ship_QueueDying(this);                                 // .text:06d0152c
        DeathSequenceTimer = 0.0f;                             // .text:06d01531
        DeathSequenceObjId = 0u;                               // .text:06d0153e
    }
    return true;
}

// ---------------------------------------------------------------------------
// Ship's GameObject / EqObj overrides and its IObjAffect table
// (2026-09-24, claude-code:w5-server-06d0). In every IObjAffect body retail
// runs with ECX = the IObjAffect sub-object (this + 4), so `[ECX+0xc]` is the
// raw CBase::Object (+0x10), `[ECX+0x24]` is OwnerSystem (+0x28) and
// `ECX - 4` is the wrapper; the port's `this` is the wrapper itself.
// ---------------------------------------------------------------------------

// .text:06d01a60 (RET 8), all eleven instructions: the two arguments pushed
// again, CALL FUN_06cee6e0 (GameObject's +0x170 body, direct), then
// `MOV dword ptr [ESI+0x90],0` (.text:06d01a74).
void Ship::enter_system(StarSystem* Sys, PhySys::CreateParms const* Parms)
{
    GameObject::enter_system(Sys, Parms);
    DeathSequenceObjId = 0u;
}

// .text:06d01a10 (RET 4):
//   .text:06d01a16  CMP [EDI+0x10],6 / JNZ -> base      ; DamageList cause
//   .text:06d01a1e .. 06d01a32  the (& 3) == 3 accessor, OwnerPlayer read
//                               through it unguarded; non-zero -> base
//   .text:06d01a3c .. 06d01a48  PUSH 0 / PUSH 1 / PUSH 0, CALL [EDX+0x50] on
//                               this+4: IObjAffect slot_50(false, true, 0)
//   .text:06d01a4e              CALL FUN_06ceb330 (EqObj +0x188 body, direct)
void Ship::apply_damage_list(DamageList& Damage)
{
    if (Damage.Cause == static_cast<DamageCause>(6) && csimple()->OwnerPlayer == 0u)
        slot_50(false, true, 0);
    EqObj::apply_damage_list(Damage);
}

// .text:06d01a90 (RET 0x14). EBP = this, EBX = the raw CBase::Object read as a
// CShip with no test (.text:06d01ad5).
//   .text:06d01aa3 .. 06d01ab4  EqObj::light_fuse(SourceId, FuseId, SubObjId,
//                               AtT, Lifetime) (FUN_06cec4d0, direct); its
//                               byte kept in the Lifetime slot (.text:06d01abb)
//                               and returned on every path
//   .text:06d01abf              not lit -> return it
//   .text:06d01ac5 .. 06d01acf  FuseId.Id != DEATH_COMM_FUSE.Id -> return it
//   .text:06d01ad8 .. 06d01b75  CShip +0x1d4 .. +0x1d8, 8-byte entries whose
//                               first dword is a Watchable (IObjRW + 8)
//   .text:06d01b7f .. 06d01c16  the same over CShip +0x1f8 .. +0x1fc
// Per entry: a null Watchable, or one whose IObjRW adjust is null, is skipped
// (.text:06d01af4 / 06d01afb); so is one whose is_player() (IObjInspect +0xbc)
// says true (.text:06d01b07). Otherwise its cobject() (+0x150) narrowed with
// (ObjectClass & 0x503) == 0x503 gives CEqObj::get_behavior_interface -- the
// narrowing is NOT tested before the call (.text:06d01b37) -- and a non-null
// behavior gets a {7, &<this ship's get_id()>} record through its vtable
// slot 0 (.text:06d01b43 .. 06d01b68): "my leader's death fuse is lit".
bool Ship::light_fuse(unsigned int SourceId, ID_String const& FuseId,
                      unsigned short SubObjId, float AtT, float Lifetime)
{
    bool const bLit = EqObj::light_fuse(SourceId, FuseId, SubObjId, AtT, Lifetime);
    if (!bLit)
        return bLit;
    if (FuseId.Id != DEATH_COMM_FUSE.Id)
        return bLit;

    CShip* const Raw = static_cast<CShip*>(Object);                  // .text:06d01ad5 EBX

    // The per-entry body of both loops (.text:06d01af0 .. 06d01b68 and
    // .text:06d01b91 .. 06d01c09 are the same code twice). *Entry is read again
    // for the cobject() call, and the ship's own id is fetched per follower.
    auto NotifyFollower = [this](FObjRWWatcher const& Entry)
    {
        // W = [slot]; W != 0 -> W - 8, tested for 0 (CShip's typed watcher, common_cobject.h).
        IObjRW* const Follower = Entry.get();
        if (Follower == nullptr)
            return;
        if (Follower->is_player())
            return;
        IObjRW* const FollowerAgain = Entry.get();
        CObject* const FollowerObj = const_cast<CObject*>(FollowerAgain->cobject());
        CEqObj* const FollowerEqObj =
            (FollowerObj != nullptr &&
             (static_cast<unsigned int>(FollowerObj->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CEqObj*>(FollowerObj) : nullptr;
        IBehaviorManager* const Behavior = FollowerEqObj->get_behavior_interface();
        if (Behavior == nullptr)
            return;
        // .text:06d01b43 .. 06d01b68 -- {7, &get_id()} to IBehaviorManager
        // slot 0 (Common LAB_062d3f90): "my leader's death fuse is lit".
        unsigned int LeaderId = get_id();
        FBehaviorManagerEvent FuseEvent;
        FuseEvent.Type = 7;
        FuseEvent.Data = &LeaderId;
        Behavior->Slot00(&FuseEvent);
    };

    // .text:06d01ad8 .. 06d01b75 -- CShip +0x1d4; the end (+0x1d8) is re-read
    // after every entry (.text:06d01b6a).
    for (std::vector<FObjRWWatcher>::const_iterator Entry = Raw->FormationFollowers.begin();
         Entry != Raw->FormationFollowers.end(); ++Entry)
        NotifyFollower(*Entry);
    // .text:06d01b7f .. 06d01c16 -- CShip +0x1f8, end +0x1fc re-read likewise.
    for (std::vector<FObjRWWatcher>::const_iterator Entry = Raw->FollowFollowers.begin();
         Entry != Raw->FollowFollowers.end(); ++Entry)
        NotifyFollower(*Entry);
    return bLit;
}

// .text:06d01c90 (RET 4): CALL FUN_06ce8510 (EqObj +0x1f4 body, direct), then
// for every node of the jettison queue (+0xa4, .text:06d01ca5 .. 06d01e64) the
// inlined vector insert(end(), 1, node->Value) -- _Ufill / _Ucopy / the
// reallocation through operator new(n * 0x20) -- i.e. push_back.
void Ship::collect_droppable_equip(std::vector<EquipDesc>& Out)
{
    EqObj::collect_droppable_equip(Out);
    for (std::list<EquipDesc>::const_iterator It = Jettison.Items.begin();
         It != Jettison.Items.end(); ++It)
        Out.push_back(*It);
}

// FUN_06d01e80 (.text:06d01e80, RET 0xc).
//   .text:06d01e86 .. 06d01e90  PendingRequest (+0x9c) == 0 -> the client arm
//   .text:06d01e92 .. 06d01ea3  PendingRequest->OnResult(RequestType,
//                               Response, Arg3) (vtable slot 0)
//   .text:06d01ea7              true -> return
//   .text:06d01ea9 / 06d01eaf   PendingRequest = 0, Field_0x98 = 0
//   client arm:
//   .text:06d01ebc .. 06d01ed4  OwnerPlayer through the (& 3) == 3 accessor,
//                               unguarded, into a stack slot
//   .text:06d01ef5              get_id()
//   .text:06d01f04              IClient +0x14c (SendServerRequestReturned) (&OwnerPlayer, id,
//                               RequestType, Response, Arg3)
//   .text:06d01f0a .. 06d01f1b  Field_0x98 = (RequestType == 0 && Response == 3)
void Ship::request_returned(int RequestType, int Response, int Arg3)
{
    if (PendingRequest != nullptr)
    {
        if (!PendingRequest->OnResult(RequestType, Response, Arg3))
        {
            PendingRequest = nullptr;
            Field_0x98 = 0;
        }
        return;
    }
    unsigned int OwnerPlayer = csimple()->OwnerPlayer;
    unsigned int const Id = get_id();
    g_pClientInterface->SendServerRequestReturned(OwnerPlayer, Id, RequestType, Response, Arg3);
    Field_0x98 = 0;
    if (RequestType == 0 && Response == 3)
        Field_0x98 = 1;
}

// .text:06d01f30: CShip::set_throttle (IAT 06d645c4) on [ECX+0xc]; return 0.
int Ship::set_throttle(float Throttle)
{
    static_cast<CShip*>(Object)->set_throttle(Throttle);
    return 0;
}

// .text:06d01f50: `ADD ECX,0x230` on [ECX+0xc], CSteering::SetAxisThrottle
// (IAT 06d645c8); return 0.
int Ship::set_axis_throttle(Vector const& AxisThrottle)
{
    static_cast<CShip*>(Object)->Steering.SetAxisThrottle(AxisThrottle);
    return 0;
}

// .text:06d01f70: CShip::set_nudge_vec (IAT 06d645cc); return 0.
int Ship::set_nudge_vec(Vector const& NudgeVec)
{
    static_cast<CShip*>(Object)->set_nudge_vec(NudgeVec);
    return 0;
}

// FUN_06d01f90 (.text:06d01f90, RET 4):
//   .text:06d01f97 .. 06d01fa3  CShip::get_strafe_dir() == Dir -> return 0
//   .text:06d01fa9 .. 06d01fb4  CEquipManager::FindFirst(0x20000) on
//                               CEqObj +0xe4 -- the engine, used untested
//   .text:06d01fc0 .. 06d01fc5  its IsActive() (vt +0x04); active -> skip
//   .text:06d01fc7 .. 06d01fde  IObjAffect slot 19 (+0x4c) with (&<its
//                               GetID() widened to a dword>, 1, 0)
//   .text:06d01fe5              CShip::set_strafe_dir(Dir)
// Always returns 0.
int Ship::set_strafe_dir(StrafeDir Dir)
{
    CShip* const Raw = static_cast<CShip*>(Object);
    if (Raw->get_strafe_dir() == Dir)
        return 0;
    CEquip* const Engine = Raw->EquipManager.FindFirst(0x20000u);
    if (!Engine->IsActive())
    {
        unsigned short const EngineId = Engine->GetID();
        slot_4c(&EngineId, true, 0);
    }
    static_cast<CShip*>(Object)->set_strafe_dir(Dir);
    return 0;
}

// FUN_06d029c0 (.text:06d029c0, RET 4).
//   .text:06d029cb .. 06d029e5  CShip +0x1b8 is a BaseWatcher: W = Leader ?
//                               Leader + 8 : 0; when W differs from the watched
//                               pointer, BaseWatcher::set_pointer(W)
//   .text:06d029eb .. 06d02a07  OwnerPlayer through the (& 3) == 3 accessor,
//                               unguarded; 0 -> return 0
//   .text:06d02a09 .. 06d02a14  get_formation_offset(offset) (IObjInspect +0xac)
//   .text:06d02a1a .. 06d02a27  Leader ? Leader->get_id() : 0
//   .text:06d02a38              IClient +0x1e4 (SendServerFormationUpdate) (owner, leader id,
//                               &offset) -- FORMATION_UPDATE
// CShip::FormationLeader is the BaseWatcher itself (common_cobject.h); BaseWatcher::set is
// the compare + set_pointer this body inlines.
int Ship::set_formation_leader(IObjRW* Leader)
{
    CShip* const Raw = static_cast<CShip*>(Object);
    Watchable const* const Watched = (Leader != nullptr) ? static_cast<Watchable const*>(Leader)
                                                         : nullptr;
    Raw->FormationLeader.set(Watched);

    unsigned int const Owner = csimple()->OwnerPlayer;
    if (Owner != 0u)
    {
        Vector Offset;
        get_formation_offset(Offset);
        unsigned int const LeaderId = (Leader != nullptr) ? Leader->get_id() : 0u;
        g_pClientInterface->SendServerFormationUpdate(Owner, LeaderId, Offset);
    }
    return 0;
}

// .text:06d02940 (RET 4).
//   .text:06d02940 .. 06d0295d  Offset copied into CShip +0x1c0 (FormationOffset)
//                               on the raw object, untested
//   .text:06d02960 .. 06d0297c  OwnerPlayer through the (& 3) == 3 accessor,
//                               unguarded; 0 -> return 0
//   .text:06d02984 .. 06d02991  leader = 0; get_formation_leader(leader)
//                               (IObjInspect +0xb0)
//   .text:06d02997 .. 06d029a6  leader ? leader->get_id() : 0
//   .text:06d029b3              IClient +0x1e4 (SendServerFormationUpdate) (owner, leader id, &Offset)
int Ship::set_formation_offset(Vector const& Offset)
{
    static_cast<CShip*>(Object)->FormationOffset = Offset;
    unsigned int const Owner = csimple()->OwnerPlayer;
    if (Owner != 0u)
    {
        IObjRW* Leader = nullptr;
        get_formation_leader(Leader);
        unsigned int const LeaderId = (Leader != nullptr) ? Leader->get_id() : 0u;
        g_pClientInterface->SendServerFormationUpdate(Owner, LeaderId, Offset);
    }
    return 0;
}

// .text:06d02a50 / 06d02a60 / 06d02a70 / 06d02ac0 / 06d02ad0 -- each is
// `MOV ECX,[ECX+0xc]` and a JMP into the CShip export, whose FORMATION_RTYPE
// (or count) is the slot's result.
int Ship::add_formation_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->add_formation_follower(Follower));
}

int Ship::remove_formation_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->remove_formation_follower(Follower));
}

int Ship::get_formation_follower_count()
{
    return static_cast<CShip*>(Object)->get_formation_follower_count();
}

int Ship::add_follow_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->add_follow_follower(Follower));
}

int Ship::remove_follow_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->remove_follow_follower(Follower));
}

// .text:06d02aa0: CShip::set_follow_leader (IAT 06d64608); return 0.
int Ship::set_follow_leader(IObjRW* Leader)
{
    static_cast<CShip*>(Object)->set_follow_leader(Leader);
    return 0;
}

// .text:06d02a80: CShip::set_follow_offset (IAT 06d64604); return 0.
int Ship::set_follow_offset(Vector const& Offset)
{
    static_cast<CShip*>(Object)->set_follow_offset(Offset);
    return 0;
}

// .text:06d020d0 (RET 0xc), the jettison request. EDI = this + 4.
//   .text:06d020ee .. 06d02109  OwnerSystem == 0 or is_destroyed() -> -5
//   .text:06d0210f .. 06d0212a  CEquipManager::FindByID(EquipId) on the raw
//                               object's +0xe4, CECargo::cast; null -> -2
//   .text:06d02137 .. 06d02148  Count = min(Count, cargo count) (unsigned JC)
//   .text:06d0214c .. 06d02179  controller event 0x23 {get_id(), word EquipId,
//                               Arch2Good(cargo arch id), Count}
//   .text:06d0217e              the cargo's pod (+0x20):
//   pod:
//   .text:06d0218c .. 06d021e4  a DamageList built inline: empty list, not
//                               destroyed, cause 0x16, inflictor get_id(),
//                               inflictor owner = OwnerPlayer through the
//                               (& 3) == 3 accessor, unguarded
//   .text:06d021fa              destroy_equip(pod, fate 4, list) (+0x224)
//   .text:06d02209              apply_damage_list(list) (+0x188)
//   .text:06d0220f .. 06d0225b  ~DamageList, return 0
//   no pod:
//   .text:06d02262 .. 06d0227b  EquipDesc ctor, cargo GetEquipDesc (vt +0x28);
//                               false -> -5
//   .text:06d02281 .. 06d0228a  EquipDesc::set_count(Count)
//   .text:06d02290 .. 06d022d6  operator new(0x28) node linked before the queue
//                               sentinel (+0xa4), its value copy-constructed
//                               (FUN_06ced210), queue size + 1 (+0xa8)
//   .text:06d022dc .. 06d022e5  CShip::open_bay(), the ST0 result dropped (FSTP ST0)
//   .text:06d022e7 .. 06d022f8  outside single player CECargo::RemoveFromStack(Count)
//   .text:06d022fe .. 06d0230c  OwnerSystem FUN_06d0f0c0(this, EquipId, Count)
//   return 0
int Ship::slot_38(unsigned short EquipId, int Count, int SourceClient)
{
    (void)SourceClient;   // retail never reads the third argument
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    CECargo* const Cargo =
        CECargo::cast(static_cast<CEqObj*>(Object)->EquipManager.FindByID(EquipId));
    if (Cargo == nullptr)
        return -2;

    unsigned int Amount = static_cast<unsigned int>(Count);
    if (!(Amount < Cargo->Count))
        Amount = Cargo->Count;

    struct FControllerMsg23
    {
        unsigned int ObjId;
        unsigned short EquipId;
        unsigned short Pad;       // not written by retail
        unsigned int GoodId;
        unsigned int Count;
    } Msg;
    Msg.ObjId = get_id();
    Msg.EquipId = EquipId;
    Msg.Pad = 0;
    Msg.GoodId = Arch2Good(Cargo->EquipArch()->get_id());
    Msg.Count = Amount;
    Server_ControllerDispatch(0, pub::Controller::EMessage::CargoJettisoned, &Msg);

    CEquip* const Pod = Cargo->CargoPod;
    if (Pod != nullptr)
    {
        DamageList Damage;
        Damage.bDestroyed = false;
        Damage.Cause = static_cast<DamageCause>(0x16);
        Damage.InflictorId = get_id();
        Damage.InflictorOwner = csimple()->OwnerPlayer;
        destroy_equip(Pod, static_cast<DamageEntry::SubObjFate>(4), Damage);
        apply_damage_list(Damage);
        return 0;
    }

    EquipDesc Desc;
    if (!Cargo->GetEquipDesc(Desc))
        return -5;
    Desc.set_count(static_cast<int>(Amount));

    // operator new(0x28) node linked before the head (+0xa4), the value copied
    // (FUN_06ced210), ++size (+0xa8): push_back.
    Jettison.Items.push_back(Desc);

    static_cast<CShip*>(Object)->open_bay();
    if (!SinglePlayer())
        Cargo->RemoveFromStack(Amount);
    OwnerSystem->notify_jettison(this, EquipId, static_cast<int>(Amount));
    return 0;
}

// .text:06d02340 (RET 0xc).
//   .text:06d02343 .. 06d0235c  OwnerSystem == 0 or is_destroyed() -> -5
//   .text:06d02362 .. 06d02369  SourceClient -1 -> 0
//   .text:06d02375 .. 06d02389  single player with a SourceClient -> straight
//                               to the notification
//   .text:06d0238c .. 06d023bc  CShip::get_target() != Target or
//                               get_sub_target() != SubTarget -> changed:
//                               CShip::set_target(Target), set_sub_target(SubTarget)
//   .text:06d023c2 .. 06d023cd  no SourceClient and nothing changed -> return 0
//   .text:06d023cf .. 06d023f6  OwnerSystem FUN_06d0e670(&get_id(),
//                               &SUBOBJ_ID_NONE, Target, &SubTarget, &SourceClient)
// Returns 0.
int Ship::set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;
    if (SourceClient == -1)
        SourceClient = 0;

    if (!(SinglePlayer() && SourceClient != 0))
    {
        bool bChanged = false;
        CShip* const Raw = static_cast<CShip*>(Object);
        if (Raw->get_target() != Target || Raw->get_sub_target() != SubTarget)
        {
            bChanged = true;
            Raw->set_target(Target);
            Raw->set_sub_target(SubTarget);
        }
        if (SourceClient == 0 && !bChanged)
            return 0;
    }

    unsigned int const Id = get_id();
    OwnerSystem->notify_set_target(Id, SUBOBJ_ID_NONE, Target, SubTarget, SourceClient);
    return 0;
}

// .text:06d02410 (RET 0xc).
//   .text:06d02415 .. 06d02430  OwnerSystem == 0 or is_destroyed() -> -5
//   .text:06d02436 .. 06d0243d  SourceClient -1 -> 0
//   .text:06d0244b .. 06d0245f  single player with a SourceClient -> notify
//   .text:06d02461 .. 06d0247d  CEEngine::cast(FindFirst(0x20000)), untested
//   .text:06d0247f .. 06d02487  engine +0x54 (bCruiseActive) == bEngage -> no change
//   changed:
//   .text:06d0248b              changed = 1
//   .text:06d02489 .. 06d024a2  bEngage and no SourceClient: IObjAffect
//                               slot 21 (+0x54) (0, 0) -- thrusters off first
//   .text:06d024a5 .. 06d024ad  CEEngine::EngageCruise(bEngage, bArg2)
//   .text:06d024b3 .. 06d024c2  no SourceClient and nothing changed -> return 0
//   .text:06d024c4 .. 06d024e3  OwnerSystem FUN_06d0e580(&get_id(), bEngage,
//                               bArg2, &SourceClient)
// Returns 0.
int Ship::slot_50(bool bEngage, bool bArg2, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;
    if (SourceClient == -1)
        SourceClient = 0;

    if (!(SinglePlayer() && SourceClient != 0))
    {
        bool bChanged = false;
        CEEngine* const Engine =
            CEEngine::cast(static_cast<CEqObj*>(Object)->EquipManager.FindFirst(0x20000u));
        if (Engine->bCruiseActive != static_cast<unsigned char>(bEngage))
        {
            bChanged = true;
            if (bEngage && SourceClient == 0)
                slot_54(false, 0);
            Engine->EngageCruise(bEngage, bArg2);
        }
        if (SourceClient == 0 && !bChanged)
            return 0;
    }

    unsigned int const Id = get_id();
    OwnerSystem->notify_cruise(Id, bEngage, bArg2, SourceClient);
    return 0;
}

// .text:06d02500 (RET 8).
//   .text:06d02507 .. 06d02522  OwnerSystem == 0 or is_destroyed() -> -5
//   .text:06d02528 .. 06d02533  result = 0; SourceClient -1 -> 0
//   .text:06d0253d .. 06d0254b  single player with a SourceClient -> notify
//   .text:06d02551 .. 06d0256e  FindFirst(0x20000) (the engine, untested): its
//                               +0x54 byte (bCruiseActive) and IsActive()
//   .text:06d02571 .. 06d0257f  only with no SourceClient and bActivate:
//   .text:06d02581 .. 06d02597    cruise on -> return -5
//   .text:06d0259a .. 06d025b5    engine inactive -> slot 19 (&<its id>, 1, 0)
//   .text:06d025b8 .. 06d02614  every 0x400 equip (CEquipTraverser): IsActive()
//                               != bActivate -> changed = 1, Activate(bActivate)
//                               (vt +0x30)
//   .text:06d0261c .. 06d0262b  no SourceClient and nothing changed -> result
//   .text:06d02633 .. 06d0264e  OwnerSystem FUN_06d0e600(&get_id(), bActivate,
//                               &SourceClient)
int Ship::slot_54(bool bActivate, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;
    int const Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    if (!(SinglePlayer() && SourceClient != 0))
    {
        bool bChanged = false;
        CEqObj* const Raw = static_cast<CEqObj*>(Object);
        CEquip* const Engine = Raw->EquipManager.FindFirst(0x20000u);
        unsigned char const bCruiseActive = static_cast<CEEngine*>(Engine)->bCruiseActive;
        bool const bEngineActive = Engine->IsActive();
        if (SourceClient == 0 && bActivate)
        {
            if (bCruiseActive != 0)
                return -5;
            if (!bEngineActive)
            {
                unsigned short const EngineId = Engine->GetID();
                slot_4c(&EngineId, true, 0);
            }
        }

        CEquipTraverser Walk(0x400);
        for (CEquip* Thruster = Raw->EquipManager.Traverse(Walk); Thruster != nullptr;
             Thruster = Raw->EquipManager.Traverse(Walk))
        {
            if (Thruster->IsActive() != bActivate)
            {
                bChanged = true;
                Thruster->Activate(bActivate);
            }
        }
        if (SourceClient == 0 && !bChanged)
            return Result;
    }

    unsigned int const Id = get_id();
    OwnerSystem->notify_thrusters(Id, bActivate, SourceClient);
    return Result;
}

// .text:06d02670 (RET 0x14).
//   .text:06d02676 .. 06d02680  Field_0x98 already set -> return 0
//   .text:06d02686 .. 06d02696  PendingRequest = Callback, Field_0x98 = 1 --
//                               both BEFORE the type is looked at
//   type 0 (.text:06d0269f .. 06d026e7): controller event 0x12 {get_id(),
//     *TargetId, system id, byte (bFlag == 1)}; retail zeroes +0..+0xc
//     (.text:06d026a5..b1) but never writes bytes +0xd..+0xf, which stay
//     stack garbage -- the port zeroes them
//   type 1 (.text:06d026fd .. 06d02727): 0x19 {get_id(), *TargetId, system id}
//   type 2 (.text:06d0273d .. 06d02771): 0x14 {get_id(), *TargetId, *SecondId,
//     system id}
//   anything else (.text:06d02782): Field_0x98 = 0 -- PendingRequest keeps
//     the callback
// Every path returns 0. The system id is FUN_06d06cd0, SystemInfo's slot 0.
int Ship::slot_5c(int RequestType, unsigned const* TargetId, unsigned const* SecondId,
                  int bFlag, RequestCB* Callback)
{
    if (Field_0x98 != 0)
        return 0;
    PendingRequest = Callback;
    Field_0x98 = 1;

    if (RequestType == 0)
    {
        unsigned int Msg[4] = { 0u, 0u, 0u, 0u };
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        reinterpret_cast<unsigned char*>(&Msg[3])[0] = (bFlag == 1) ? 1u : 0u;
        Server_ControllerDispatch(0, pub::Controller::EMessage::DockRequest, Msg);
        return 0;
    }
    if (RequestType == 1)
    {
        unsigned int Msg[3];
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::FormationRequest, Msg);
        return 0;
    }
    if (RequestType == 2)
    {
        unsigned int Msg[4];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = *SecondId;
        Msg[3] = OwnerSystem->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneRequest, Msg);
        return 0;
    }
    Field_0x98 = 0;
    return 0;
}

// .text:06d027a0 (RET 0xc; Ghidra splits it at .text:06d027ac, FUN_06d027ac is
// the same body).
//   .text:06d027af / 06d027b5  PendingRequest = 0, Field_0x98 = 0
//   type 0 (.text:06d027bd .. 06d027e3): controller event 0x10 {get_id(),
//     *TargetId, Arg3}
//   type 1 (.text:06d027ea .. 06d02814): 0x1a {get_id(), *TargetId, system id}
//   type 2 (.text:06d0281b .. 06d02843): 0x15 {get_id(), *TargetId, system id}
//   then every path (.text:06d0284d .. 06d02870): set_invincible({2, 0, 0, 1.0f})
// Returns 0.
int Ship::slot_60(int RequestType, unsigned const* TargetId, int Arg3)
{
    PendingRequest = nullptr;
    Field_0x98 = 0;

    if (RequestType == 0)
    {
        unsigned int Msg[3];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = static_cast<unsigned int>(Arg3);
        Server_ControllerDispatch(0, pub::Controller::EMessage::DockCancel, Msg);
    }
    else if (RequestType == 1)
    {
        unsigned int Msg[3];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::FormationLeave, Msg);
    }
    else if (RequestType == 2)
    {
        unsigned int Msg[3];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneRelease, Msg);
    }

    FInvincibility Cmd = {};
    Cmd.Type = 2u;
    Cmd.bProtect = false;
    Cmd.bFromPlayers = false;
    Cmd.MaxHpLoss = 1.0f;
    set_invincible(Cmd);
    return 0;
}

// .text:06d028c0 (RET 0x10).
//   .text:06d028d4 .. 06d028e1  CShip::go_tradelane(EntranceRing, ExitRing,
//                               <this wrapper as IObjRW>, bFastExit, Speed)
//   .text:06d028e7              non-zero -> -5
//   .text:06d028eb .. 06d0290a  ExitRing ? ExitRing->get_id() : 0, then
//                               EntranceRing->get_id() (untested)
//   .text:06d02912 .. 06d02919  OwnerSystem FUN_06d0df40(get_id(), entrance id,
//                               exit id, bFastExit, Speed)
// Returns 0.
int Ship::go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                       bool bFastExit, float Speed)
{
    if (static_cast<CShip*>(Object)->go_tradelane(EntranceRing, ExitRing, this, bFastExit, Speed) != 0)
        return -5;
    unsigned int const ExitId = (ExitRing != nullptr) ? ExitRing->get_id() : 0u;
    unsigned int const EntranceId = EntranceRing->get_id();
    unsigned int const ShipId = get_id();
    OwnerSystem->notify_go_tradelane(ShipId, EntranceId, ExitId, bFastExit, Speed);
    return 0;
}

// FUN_06d02880 (.text:06d02880, RET 0).
//   .text:06d02888  CShip::stop_tradelane()
//   .text:06d02896  CShip::get_tradelane_exit_ring()      -> third argument
//   .text:06d0289f  CShip::get_tradelane_entrance_ring()  -> second argument
//   .text:06d028aa  get_id()                               -> first argument
//   .text:06d028b1  OwnerSystem FUN_06d0e120
// Returns 0.
int Ship::stop_tradelane()
{
    CShip* const Raw = static_cast<CShip*>(Object);
    Raw->stop_tradelane();
    unsigned int const ExitId = Raw->get_tradelane_exit_ring();
    unsigned int const EntranceId = static_cast<CShip*>(Object)->get_tradelane_entrance_ring();
    unsigned int const ShipId = get_id();
    OwnerSystem->notify_stop_tradelane(ShipId, EntranceId, ExitId);
    return 0;
}

// .text:06d02000 (RET 4).
//   .text:06d02000 .. 06d0200d  Field_0xB8 clear -> return 0
//   .text:06d02010              Field_0xB8 = 0
//   .text:06d02019 .. 06d0203a  set_invincible({1, 0, 0, 1.0f}) -- lifts the
//                               launch grant apply_launch_invincibility made
//   .text:06d02040 .. 06d0205a  controller event 0x28 {Id, get_id()}
// Returns 0.
int Ship::slot_70(unsigned Id)
{
    if (Field_0xB8 != 0)
    {
        Field_0xB8 = 0;
        FInvincibility Cmd = {};
        Cmd.Type = 1u;
        Cmd.bProtect = false;
        Cmd.bFromPlayers = false;
        Cmd.MaxHpLoss = 1.0f;
        set_invincible(Cmd);
        unsigned int Msg[2];
        Msg[0] = Id;
        Msg[1] = get_id();
        Server_ControllerDispatch(0, pub::Controller::EMessage::LaunchComplete, Msg);
    }
    return 0;
}

// .text:06d02070 (RET 4).
//   .text:06d02077 .. 06d02092  controller event 0x26 {Arg, get_id()}
//   .text:06d02097 .. 06d020bd  set_invincible({2, 0, 0, 1.0f})
// Returns 0.
int Ship::slot_74(unsigned Arg)
{
    unsigned int Msg[2];
    Msg[0] = Arg;
    Msg[1] = get_id();
    Server_ControllerDispatch(0, pub::Controller::EMessage::JumpInComplete, Msg);
    FInvincibility Cmd = {};
    Cmd.Type = 2u;
    Cmd.bProtect = false;
    Cmd.bFromPlayers = false;
    Cmd.MaxHpLoss = 1.0f;
    set_invincible(Cmd);
    return 0;
}

// ---------------------------------------------------------------------------
// Solar -- create .text:06d02c50
// ---------------------------------------------------------------------------

Solar::Solar()
{
}

Solar::~Solar()
{
    // .text:06d02bfb: CALL [.rdata:06d675c8] -- Solar's +0x15c entry, which is
    // EqObj's destroy_notify (.text:06ce7c80) -- with the Solar vtables just
    // re-stamped; the virtual call made from this destructor resolves to the
    // same entry. ~EqObj (FUN_06ce6710) follows as the base destructor.
    destroy_notify();
}

// FUN_06d02c20 (.text:06d02c20, RET 4), all 15 instructions:
//   .text:06d02c27  MOV [ESI+0x10],ECX            ; CBase::Object = Obj
//   .text:06d02c2a  CALL COMMON.DLL::CObject::AddRef (ECX = Obj, untested)
//   .text:06d02c30 .. 06d02c35  CBase::advise(true) on this + 0xc
//   .text:06d02c3b .. 06d02c42  return Object != 0
bool Solar::attach_existing(CObject* Obj)
{
    Object = Obj;
    Obj->AddRef();
    advise(true);
    return Object != nullptr;
}

// +0x16c .text:06d02cc0 (RET 4):
//   .text:06d02cc9  CALL FUN_06ce7e20 (EqObj::update, direct); false -> return
//                   it (.text:06d02cd4)
//   .text:06d02cd7 .. 06d02cf1  the (ObjectClass & 0x303) == 0x303 narrowing,
//                   no null test on the result
//   .text:06d02cf1 .. 06d02cff  CSolar vt +0x50 (CObject::update)(Dt,
//                   COMMON.DLL::CObject::UPDATE_DEFAULT); its result is dropped
//   .text:06d02d03  MOV AL,1
bool Solar::update(float Dt)
{
    if (!EqObj::update(Dt))
        return false;
    csolar()->update(Dt, CObject::UPDATE_DEFAULT);
    return true;
}

// +0x158 .text:06d02d10 (RET 8): EqObj::destroy (FUN_06ce8080, direct) with
// both arguments; false -> false. Otherwise the owning system (+0x28) drops the
// solar, FUN_06d08940 (.text:06d02d2c), and the answer is true.
bool Solar::destroy(int Kind, unsigned int ObjId)
{
    if (!EqObj::destroy(Kind, ObjId))
        return false;
    OwnerSystem->remove_solar(this);
    return true;
}

// +0x17c .text:06d02d40 (RET 8). A solar that is not destructible
// (CSolar +0x1a8 clear, read through the unguarded 0x303 narrowing) ignores a
// hit on its root (SUBOBJ_ID_ROOT: IAT 06d6401c, .text:06d02d68 .. 06d02d77);
// everything else is EqObj's (FUN_06ce9250, a tail JMP at .text:06d02d7d).
void Solar::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    if (!csolar()->bDestructible && Hit.SubObjId == SUBOBJ_ID_ROOT)
        return;
    EqObj::damage_sub_obj(Hit, Damage);
}

// The 0x24-byte record Solar's two root-hit overrides hand to controller event
// 0xd (.text:06d02df1 .. 06d02e5e / .text:06d02f68 .. 06d02fd5): nine dwords
// zeroed, then the solar's id, its system's id, the DamageList's inflictor
// (+0x14) and cause (+0x10), and 1.0f in the last one. +0x0c/+0x10/+0x18/
// +0x1c stay zero on both paths.
struct FSolarHitEvent
{
    unsigned int ObjId;        // +0x00  get_id()
    unsigned int SystemId;     // +0x04  FUN_06d06cd0 on OwnerSystem
    unsigned int InflictorId;  // +0x08  DamageList +0x14
    unsigned int Zero0c;       // +0x0c
    unsigned int Zero10;       // +0x10
    unsigned int Cause;        // +0x14  DamageList +0x10
    unsigned int Zero18;       // +0x18
    unsigned int Zero1c;       // +0x1c
    float Scale;               // +0x20  1.0f
};
static_assert(sizeof(FSolarHitEvent) == 0x24, "Solar root-hit event is 0x24 bytes");

// +0x180 .text:06d02d90 (RET 8).
//   .text:06d02d98 .. 06d02dba  CSolar +0x1a8 (bDestructible) set -> EqObj
//                               body FUN_06ce9350 and return
//   .text:06d02dd6 .. 06d02de7  a hit on a sub-object (Hit +8 !=
//                               SUBOBJ_ID_ROOT, IAT 06d6401c) -> the same EqObj body
//   root hit on an indestructible solar:
//   .text:06d02ded .. 06d02e66  FSolarHitEvent (system id read first, then
//                               the inflictor, then get_id()) -> controller
//                               event 0xd
//   .text:06d02e6b .. 06d02e8a  CEqObj::get_behavior_interface on the
//                               unguarded 0x303 narrowing
//   .text:06d02e94 .. 06d02ee9  non-null -> a type-0 behavior event whose
//                               payload is the same nine dwords reordered
//                               {system, inflictor, id, 0, 0, cause, 0, 0, 1.0f},
//                               through the behavior manager's vtable slot 0
//   .text:06d02eeb .. 06d02efa  damage_power (+0x21c) with the munition's
//                               EnergyDamage (Archetype::Munition +0x84)
void Solar::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    if (csolar()->bDestructible)
    {
        EqObj::munition_hit(Hit, Damage);
        return;
    }
    if (Hit.SubObjId != SUBOBJ_ID_ROOT)
    {
        EqObj::munition_hit(Hit, Damage);
        return;
    }

    FSolarHitEvent Event = {};
    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Event.InflictorId = Damage.InflictorId;
    Event.ObjId = get_id();
    Event.Cause = static_cast<unsigned int>(Damage.Cause);
    Event.Scale = 1.0f;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Event);

    IBehaviorManager* const Behavior = static_cast<CEqObj*>(csolar())->get_behavior_interface();
    if (Behavior != nullptr)
    {
        // .text:06d02e94 .. 06d02ee9 --
        // the 0xd record's nine dwords reordered into FBehaviorDamageEvent (common_iface.h):
        // {system, inflictor, id, 0, 0, cause, 0, 0, 1.0f}; event {0, &payload} to
        // IBehaviorManager::Slot00 (Common LAB_062d3f90).
        FBehaviorDamageEvent Payload;
        Payload.SystemId = Event.SystemId;
        Payload.InflictorId = Event.InflictorId;
        Payload.ObjId = Event.ObjId;
        Payload.Cause = static_cast<int>(Event.Cause);
        std::memcpy(&Payload.Damage, &Event.Zero18, sizeof(unsigned int));
        std::memcpy(&Payload.RelativeDamage, &Event.Zero1c, sizeof(unsigned int));
        Payload.RelativeHealth = Event.Scale;
        Payload.EquipType = 0u;
        Payload.CollisionGroupType = 0u;
        FBehaviorManagerEvent DamageEvent;
        DamageEvent.Type = 0;
        DamageEvent.Data = &Payload;
        Behavior->Slot00(&DamageEvent);
    }

    damage_power(Hit.Munition->EnergyDamage, Damage);
}

// +0x1fc .text:06d02f20 (RET 8). A destructible solar (CSolar +0x1a8) takes
// EqObj's hull pass (FUN_06ce9550, .text:06d02f57). An indestructible one only
// reports the blast: the FSolarHitEvent above to controller event 0xd
// (.text:06d02f64 .. 06d02fdd, the system id read first) and the same type-0
// behavior event (.text:06d02fe2 .. 06d03063) -- no damage at all. The Blast
// itself is not read on that path.
void Solar::explosion_hit_hull(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    if (csolar()->bDestructible)
    {
        EqObj::explosion_hit_hull(Blast, Out);
        return;
    }

    FSolarHitEvent Event = {};
    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Event.InflictorId = Out.InflictorId;
    Event.ObjId = get_id();
    Event.Cause = static_cast<unsigned int>(Out.Cause);
    Event.Scale = 1.0f;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Event);

    IBehaviorManager* const Behavior = static_cast<CEqObj*>(csolar())->get_behavior_interface();
    if (Behavior != nullptr)
    {
        // .text:06d0300e .. 06d03063 --
        // the 0xd record's nine dwords reordered into FBehaviorDamageEvent (common_iface.h):
        // {system, inflictor, id, 0, 0, cause, 0, 0, 1.0f}; event {0, &payload} to
        // IBehaviorManager::Slot00 (Common LAB_062d3f90).
        FBehaviorDamageEvent Payload;
        Payload.SystemId = Event.SystemId;
        Payload.InflictorId = Event.InflictorId;
        Payload.ObjId = Event.ObjId;
        Payload.Cause = static_cast<int>(Event.Cause);
        std::memcpy(&Payload.Damage, &Event.Zero18, sizeof(unsigned int));
        std::memcpy(&Payload.RelativeDamage, &Event.Zero1c, sizeof(unsigned int));
        Payload.RelativeHealth = Event.Scale;
        Payload.EquipType = 0u;
        Payload.CollisionGroupType = 0u;
        FBehaviorManagerEvent DamageEvent;
        DamageEvent.Type = 0;
        DamageEvent.Data = &Payload;
        Behavior->Slot00(&DamageEvent);
    }
}

// .data:DAT_06d8d97c -- Solar.cpp's own lazily fetched math engine
// (.text:06d031b0 `MOV EAX,[DAT_06d8d97c]` / JNZ / CALL FUN_06d5ff70 / store).
static I3DMathEngine* GSolarMathEngine = nullptr;

// +0x250 .text:06d03070 (RET 0xc). The type bits (+0x154) pick the branch:
//   0x40 or 0x800 (.text:06d0307f / 06d03087) -- somewhere on a 500-unit ring
//     350 units behind the solar:
//     .text:06d03192 .. 06d031a4  the 0x30 bytes vt +0x10 returns copied (the
//                                 slot is ICF-folded with get_transform, so it
//                                 is the whole Transform: rotation + position)
//     .text:06d031a6 .. 06d031e5  a = rand() * (1/32768.0f) * 2pi (the double
//                                 .rdata:06d66b80); local {cos(a) * 500.0,
//                                 sin(a) * 500.0, -350.0f}
//     .text:06d031eb .. 06d03207  DAT_06d8d97c's TransformXform (+0x28)
//                                 (out, transform, local)
//     .text:06d0320a .. 06d0322f  OutPos = out, OutOrient = the rotation
//   0x400 (.text:06d03090) -- in place, turned round:
//     .text:06d03099 .. 06d030ac  OutPos = get_position() (vt +0x00)
//     .text:06d030b3 .. 06d030cb  OutOrient = get_transform()'s rotation (vt +0x0c)
//     .text:06d030b6 .. 06d03103  with c = cos(pi), s = sin(pi) (x87, the pi
//                                 double .rdata:06d67728), for each column i:
//                                 row0[i] = row0*c + s*row2, row2[i] = c*row2 - row0*s
//                                 (both from the old values) -- a half turn about
//                                 the local Y axis
//   anything else (.text:06d03111):
//     FDUMP(0x100001, "%s(%d) : *** ERROR: %s", Solar.cpp, 0xeb, ...);
//     OutPos = {35000, 0, 35000}, OutOrient = identity.
void Solar::ChooseSystemArrivalLocation(GameObject* Arriving, Vector& OutPos, Matrix& OutOrient)
{
    (void)Arriving;   // pushed by both callers, never read
    unsigned int const TypeBits = get_type_bits();

    if ((TypeBits & 0x40u) != 0 || (TypeBits & 0x800u) != 0)
    {
        Transform const Xform = reinterpret_cast<Transform const&>(get_orientation());
        int const Random = rand();
        double const Angle = static_cast<double>(static_cast<float>(Random) * 3.0517578125e-05f)
                           * 6.2831854820251465;
        Vector Local;
        Local.x = static_cast<float>(cos(Angle) * 500.0);
        Local.y = static_cast<float>(sin(Angle) * 500.0);
        Local.z = -350.0f;
        if (GSolarMathEngine == nullptr)
            GSolarMathEngine = Server_Acquire3DMathEngine();
        Vector Out;
        GSolarMathEngine->TransformXform(reinterpret_cast<FVector&>(Out),
                                         reinterpret_cast<FTransform const&>(Xform),
                                         reinterpret_cast<FVector const&>(Local));
        OutPos = Out;
        OutOrient = Xform.Rotation;
        return;
    }

    if ((TypeBits & 0x400u) != 0)
    {
        OutPos = get_position();
        OutOrient = get_transform().Rotation;
        double const Pi = 3.1415927410125732;
        double const C = cos(Pi);
        double const S = sin(Pi);
        for (int Column = 0; Column < 3; ++Column)
        {
            double const Row0 = OutOrient.E[0][Column];
            double const Row2 = OutOrient.E[2][Column];
            OutOrient.E[0][Column] = static_cast<float>(Row0 * C + S * Row2);
            OutOrient.E[2][Column] = static_cast<float>(C * Row2 - Row0 * S);
        }
        return;
    }

    FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
          "E:\\FL\\Scratch\\Source\\Server\\Space\\Solar.cpp", 0xeb,
          "Invalid type in ChooseSystemArrivalLocation()");
    OutPos.x = 35000.0f;
    OutPos.y = 0.0f;
    OutPos.z = 35000.0f;
    OutOrient.E[0][0] = 1.0f; OutOrient.E[0][1] = 0.0f; OutOrient.E[0][2] = 0.0f;
    OutOrient.E[1][0] = 0.0f; OutOrient.E[1][1] = 1.0f; OutOrient.E[1][2] = 0.0f;
    OutOrient.E[2][0] = 0.0f; OutOrient.E[2][1] = 0.0f; OutOrient.E[2][2] = 1.0f;
}

bool Solar::create(Archetype::Root* Arch)
{
    // .text:06d02c50. Same steps as Ship::create (CreateOfClass) except that
    // CObject::open (.text:06d02c90) runs BEFORE CBase::advise(true)
    // (.text:06d02c98). No null checks, as in retail.
    Object = CObject::Alloc(CObject::ClassSolar);                  // PUSH 0x303
    Arch->create_instance(static_cast<IEngineInstance*>(Object));  // .text:06d02c6c
    // .text:06d02c72 .. 06d02c87: IEngine::UpdateInstance(instance, 0, 0).
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Object->EngineInstanceId), 0, nullptr);
    Object->open(Arch);
    advise(true);
    return Object != nullptr;
}

bool Solar::find(unsigned int const& Id)
{
    // Solar vtable .rdata:06d6746c + 0x168 = .rdata:06d675d4 -> .text:06d02cb0
    // `XOR AL,AL / RET 4` (the ICF body Loot/Bullet/Mine's find and MedAsteroid's
    // create share): a Solar is never found by id. No CObject::Find, no advise --
    // single player wraps an existing CSolar through attach_existing (FUN_06d02c20)
    // instead. Ship's +0x168 (.text:06d010a0) is the only EqObj-family find that
    // calls CObject::Find.
    (void)Id;
    return false;
}

CSolar* Solar::csolar() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x303u) != 0x303u)
        return nullptr;
    return static_cast<CSolar*>(Obj);
}

// ---------------------------------------------------------------------------
// Loot -- create .text:06cf2440
// ---------------------------------------------------------------------------

Loot::Loot()
    : CheckTimer(0.0f)
    , UnseenLife(0.0f)
    , DecayAccum(0.0f)
{
    // Retail has no Loot constructor of its own: the spawner FUN_06d08a40 runs
    // GameObject's (.text:06d08bae FUN_06cee360), zeroes +0x60/+0x64/+0x68
    // (.text:06d08bb3-06d08bb9) and stores the three Loot vtables.
}

Loot::~Loot()
{
}

// Loot::create (.text:06cf2440) lives in server_loot.cpp.

bool Loot::find(unsigned int const& Id)
{
    // Loot vtable .rdata:06d67df4 + 0x168 = .rdata:06d67f5c -> .text:06d02cb0
    // `XOR AL,AL / RET 4` (the ICF body Solar/MedAsteroid's create share): a Loot is never
    // found by id, it is only ever created. No CObject::Find, no advise.
    (void)Id;
    return false;
}

// .text:06d08eb0 -- `MOV EAX,0x10000000 / RET`.
unsigned int Loot::get_type_bits() const
{
    return 0x10000000u;
}

CLoot* Loot::cloot() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x803u) != 0x803u)
        return nullptr;
    return static_cast<CLoot*>(Obj);
}
