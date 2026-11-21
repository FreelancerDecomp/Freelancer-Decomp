// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_starsystem.cpp -- StarSystem.
// Split out of server_classes.cpp unchanged; the declarations the split files share are in
// server_classes_internal.h.

#include "common_server.h"
#include "common_net.h"
#include "common_physys.h"
#include "common_ivp_real_object.h" // IVP_Real_Object +0x1c/+0xa4 (StarSystem_MpCollisionFilter)
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
// StarSystem::count_players — retail RVA 0x2BCB0, ordinal 476
// Iterates the player list at this+0x0C (doubly-linked list with sentinel).
// Counts entries where node[0x10] (dword at node+0x40) != ExcludeId.
// Retail: for (node = **(this+0xC); node != *(this+0xC); node = *node)
//           if (node[0x10] != param_1) count++;
// ===========================================================================
unsigned int StarSystem::count_players(unsigned int ExcludeId) const
{
    unsigned int Count = 0;
    // PlayerListHead at this+0x0C is a pointer to the list sentinel
    unsigned int const* Sentinel = reinterpret_cast<unsigned int const*>(
        *reinterpret_cast<void* const*>(
            reinterpret_cast<unsigned char const*>(this) + 0x0C));
    // No null test: .text:06d0bcb0 `MOV EDX,[ECX+0xc]` / .text:06d0bcb3
    // `MOV ECX,[EDX]` dereferences the sentinel at once.

    // First node: dereference the sentinel to get the head pointer
    // Retail: puVar1 = **(this + 0xc)
    unsigned int const* Node = reinterpret_cast<unsigned int const*>(*Sentinel);
    while (Node != Sentinel) {
        // node[0x10] = player ID at offset 0x40 bytes (0x10 dwords)
        if (Node[0x10] != ExcludeId) {
            Count++;
        }
        // Advance to next node: node = *node (first dword = next pointer)
        Node = reinterpret_cast<unsigned int const*>(*Node);
    }
    return Count;
}

// ===========================================================================
// StarSystem::remove_player — retail FUN_06d08740(this, SessionId), 0x06d08740.
// The first half of both system-exit entry points (FUN_06d087f0 and the one
// PlayerDB::cleanup/logout reach, FUN_06d08890).
// ===========================================================================
void StarSystem::remove_player(unsigned int SessionId)
{
    // (a) drop this session's node out of the player list at this+0x0C.
    //     Retail unlinks with `*(int*)node[1] = node[0]` (prev->next = next) and
    //     `*(int*)(node[0] + 4) = node[1]` (next->prev = prev), then calls the
    //     node's own disposer through node[2], frees the node, decrements the
    //     size at this+0x10, and BREAKS -- one match only.
    //
    //     Retail reads the head and dereferences it at once, with no null test,
    //     and the loop's only exit test is `node != head`:
    //       .text:06d08748  8b450c  MOV  EAX,[EBP + 0xc]    ; head
    //       .text:06d0874c  8b30    MOV  ESI,[EAX]          ; first = head->next
    //       .text:06d0874e  3bf0    CMP  ESI,EAX / JZ 06d08788
    //       .text:06d08753  395e40  CMP  [ESI + 0x40],EBX   ; View.PlayerId == sid?
    //       .text:06d08758  8b36    MOV  ESI,[ESI] / CMP ESI,EAX / JNZ 06d08753
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    // KNOWN CRASH (intentional, retail-faithful): if this faults on a garbage
    // Sentinel and the call came from PlayerDB_LeaveSystem, `this` is SysMap's
    // unconstructed head node -- a SysMap miss (bad universe.ini system id).
    // Retail .text:06d08748 / .text:06d0874c. See PlayerDB_LeaveSystem.
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->View.PlayerId == SessionId) {
            // .text:06d08760 .. .text:06d0876f
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            // Retail disposes the element through its own vtable slot 0.
            // Ghidra renders this `(**(code **)piVar1[2])(0)`, which reads
            // as "load node[2] and call through it"; the raw bytes say
            // otherwise -- it is a LEA, so the element is embedded IN the
            // node at +0x08, not pointed to from it:
            //   .text:06d0876c  8d4e08  LEA   ECX,[ESI + 0x8]   ; &node->Element
            //   .text:06d08772  8b11    MOV   EDX,[ECX]         ; its vtable
            //   .text:06d08774  6a00    PUSH  0x0               ; "do not free"
            //   .text:06d08776  ff12    CALL  dword ptr [EDX]   ; slot 0
            // (The +0x38 list below is the opposite shape: 06d0879f does
            // `MOV ECX,[ESI + 0x8]`, a real pointer load.)
            //
            // `PUSH 0` is the deleting flag: destroy in place, do NOT free,
            // because the node is freed separately on the next line. An
            // explicit destructor call through a pointer is exactly that
            // dispatch: vptr slot 0 (the scalar deleting destructor) with 0.
            Observer* const Element = &Node->View;                  // .text:06d0876c
            Element->~Observer();                                  // .text:06d08772 .. 06d08776
            ::operator delete(Node);                               // .text:06d08779
            --PlayerListSize;                                      // .text:06d0877e .. 06d08785
            break;
        }
    }

    // (b) the dispatch pass over the Ships list (head at this+0x38).
    //     +0x20 is IObjInspect::get_id, +0x158 GameObject::destroy and +0x160
    //     GameObject::is_destroyed (server_gameobject.h). Retail:
    //       .text:06d08788  XOR  ESI,ESI              ; node := null
    //       .text:06d08790  TEST ESI,ESI / JNZ        ; first pass: head,
    //       .text:06d08794  MOV  ESI,[EBP + 0x38]     ;   else node->next
    //       .text:06d08799  MOV  ESI,[ESI]            ;   (read AFTER destroy)
    //       .text:06d0879b  TEST ESI,ESI / JZ exit    ; NULL-terminated
    //       .text:06d0879f  MOV  ECX,[ESI + 0x8]      ; node->Data, a POINTER
    //       .text:06d087a2  MOV  EAX,[ECX + 0x10]     ; CBase::Object
    //       .text:06d087a5  TEST EAX,EAX / JZ 06d087b4
    //       .text:06d087a9  MOV  EDX,[EAX + 0x4c] / AND EDX,3 / CMP DL,3 / JZ 06d087b6
    //       .text:06d087b4  XOR  EAX,EAX              ; cobj() == null ...
    //       .text:06d087b6  CMP  [EAX + 0xb4],EBX     ; ... read through unguarded
    //       .text:06d087be  CALL [EAX + 0x160] / TEST AL,AL / JNZ next
    //       .text:06d087cf  CALL [EDI + 0x20]         ; get_id()
    //       .text:06d087d8  PUSH EAX / PUSH 0 / CALL [EDI + 0x158]
    //     The null path at .text:06d087b4 has no test before the +0xb4 read, so
    //     retail faults on a non-CSimple object: every object on this list is a
    //     CSimple there, and the port does the same unguarded read. node->Data
    //     is the wrapper itself (2026-09-24), so there is no registry miss to
    //     skip any more either.
    FSystemListNode* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->csimple()->OwnerPlayer != SessionId)
            continue;
        if (Obj->is_destroyed())
            continue;
        Obj->destroy(0, Obj->get_id());
    }
}

// ---------------------------------------------------------------------------
// StarSystem::player_switched_out -- retail FUN_06d087f0 (.text:06d087f0 ..
// 06d08888, RET 8). See server_sysdb.h. Stack base T = ESP after the three
// register pushes; the 0x3a payload is [T+0xc] {system id, DestSystemId,
// SessionId}.
// ---------------------------------------------------------------------------
void StarSystem::player_switched_out(unsigned int SessionId, unsigned int DestSystemId)
{
    remove_player(SessionId);                                       // .text:06d087fd FUN_06d08740

    // .text:06d08802 .. 06d08817 -- `Players + id*0x418 - 0x418`, the raw slot.
    PlayerData_FlushPendingVisits(Players[SessionId]);              // FUN_06d4b450

    if (DestSystemId != 0u)                                         // .text:06d0881c .. 06d08822
    {
        unsigned int Payload[3];
        Payload[0] = SystemInfo->NicknameId;                        // .text:06d08824 .. 06d0882f ISystem slot 0
        Payload[1] = DestSystemId;                                  // .text:06d0883a
        Payload[2] = SessionId;                                     // .text:06d08827
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);                // .text:06d0883e
    }

    // .text:06d08846 .. 06d0887d -- the single-player tail.
    if (SinglePlayer() && bLoaded != 0)
    {
        bLoaded = 0;                                                // .text:06d0885d
        unsigned int SystemNickname = SystemInfo->NicknameId;       // .text:06d0885a .. 06d08868
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemUnloaded, &SystemNickname);        // .text:06d08873
        unload();                                                   // .text:06d0887d FUN_06d072c0
    }
}

// FUN_06d0d8a0 (.text:06d0d8a0, RET 0xc). See server_sysdb.h for the map.
bool StarSystem::activate_object(GameObject* Obj, bool bOn, unsigned int Index)
{
    // .text:06d0d8ab .. 06d0d8c8 -- __RTDynamicCast(Obj, GameObject -> EqObj);
    // a failed cast returns BL, zeroed at .text:06d0d8bc.
    EqObj* const Eq = dynamic_cast<EqObj*>(Obj);
    if (Eq == nullptr)
        return false;

    // .text:06d0d8ce .. 06d0d8da -- FUN_06ce7850 = EqObj::activate(bOn, Index):
    // the (& 0x103) narrowing of the raw object (not null-tested) tail-jumping
    // into CEqObj::activate (vt +0xb4).
    ObjActivateResult const Result = Eq->activate(bOn, Index);

    // .text:06d0d8df .. 06d0d8ec -- 0 returns false; anything but 1 returns CL = 1.
    if (Result == ObjActivateFailed)
        return false;
    if (Result != ObjActivateChanged)
        return true;

    // .text:06d0d8f2 .. 06d0d91a -- cobject() (vt +0x150) under the (& 0x303)
    // test: a solar is announced to every player, anything else only where seen.
    CObject const* Solar = Obj->cobject();
    if (Solar != nullptr &&
        (static_cast<unsigned int>(Solar->ObjectClass) & 0x303u) != 0x303u)
        Solar = nullptr;

    // .text:06d0d91d .. 06d0d92f -- {get_id() (vt +0x20), BL, Index}; the pad
    // bytes are never written.
    FPayloadServerActivateObject Rec;
    Rec.ObjectId = Obj->get_id();
    Rec.bActivate = static_cast<unsigned char>(bOn);
    Rec.Field_0x08 = Index;

    // .text:06d0d928 .. 06d0d971 -- every player node; FUN_06cf73d0 on node+8
    // only when there is no solar, IClient +0xe0 (SendServerActivateObject) with node+0x40.
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        if (Solar == nullptr && !SystemPlayerView_IsObjectVisible(&Node->View, Obj))
            continue;
        ::g_pClientInterface->SendServerActivateObject(Node->View.PlayerId, Rec);
    }

    // .text:06d0d976 -- AL = 1.
    return true;
}

// FUN_06d0d9b0 (.text:06d0d9b0, RET 0x10). See server_sysdb.h for the map.
void StarSystem::spawn_mined_loot(Vector const& Pos, unsigned int ContainerArchId,
                                  unsigned int ContentsArchId, int Count)
{
    // .text:06d0d9bb .. 06d0d9db -- the record's +0x04, +0x08, +0x10 .. +0x1c are
    // zeroed first, byte +0x68 = 1, byte +0x69 = 0; +0x6a/+0x6b are never written.
    FLootRecord Rec;
    Rec.Arch = nullptr;
    Rec.HitPts = 0.0f;
    Rec.ContentsHitPts = 0.0f;
    Rec.Count = 0;
    Rec.Field_0x18 = 0u;
    Rec.Field_0x1c = 0u;
    Rec.Field_0x68 = 1;
    Rec.bMission = 0;

    // .text:06d0d9e2 .. 06d0da05 -- CFLIDMaker::AllocateObjectID inlined, taken
    // before any archetype is looked up (and so spent even on the early outs).
    Rec.Id = CFLIDMaker::GetInstance()->AllocateObjectID();

    // .text:06d0da04 .. 06d0da16 -- the contents archetype; none -> return.
    Archetype::Equipment* const Contents = Archetype::GetEquipment(ContentsArchId);
    if (Contents == nullptr)
        return;
    Rec.ContentsArch = Contents;                                    // .text:06d0da12  +0x0c
    Rec.ContentsHitPts = Contents->HitPts;                          // .text:06d0da25 / 06d0da2f  +0x10 (Root +0x1c)
    Rec.Count = Count;                                              // .text:06d0da28 / 06d0da33  +0x14

    // .text:06d0da1c .. 06d0da54 -- the container: GetEquipment(ContainerArchId)
    // when the id is non-zero; zero or unknown falls back to the contents'
    // get_loot_appearance() (vt +0x1c); null there -> return.
    Archetype::Equipment* Container = nullptr;
    if (ContainerArchId != 0u)
        Container = Archetype::GetEquipment(ContainerArchId);
    if (Container == nullptr)
    {
        Container = Contents->get_loot_appearance();
        if (Container == nullptr)
            return;
    }
    Rec.Arch = Container;                                           // .text:06d0da41 / 06d0da50  +0x04
    Rec.HitPts = Container->HitPts;                                 // .text:06d0da5a / 06d0da6c  +0x08

    // .text:06d0da5d .. 06d0da88 -- +0x2c the position, copied as three dwords.
    Rec.Position = Pos;

    // .text:06d0da8c .. 06d0dacc -- +0x38 the identity orientation.
    Rec.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                             0.0f, 1.0f, 0.0f,
                             0.0f, 0.0f, 1.0f);

    // .text:06d0da7e .. 06d0daf0 -- +0x20 random_inside_sphere(20.0f).
    Rec.Velocity = random_inside_sphere(20.0f);

    // .text:06d0dae6 .. 06d0db18 -- +0x5c random_inside_sphere(3.0f).
    Rec.AngularVelocity = random_inside_sphere(3.0f);

    // .text:06d0db05 .. 06d0db1f -- create_loot(0, 3, &Rec, 1) (FUN_06d08a40).
    create_loot(0u, 3, &Rec, 1);
}

// FUN_06d0e7a0 (.text:06d0e7a0, RET 8). See server_sysdb.h for the map.
void StarSystem::send_tractor_objects(unsigned int const& SkipPlayerId, XTractorObjects const& Info)
{
    // .text:06d0e7b8 .. 06d0e7c8 / 06d0e8a1 .. 06d0e8ae -- every player node,
    // the sentinel re-read from +0xc on each step.
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        // .text:06d0e7d7 .. 06d0e7e0 -- the player named by *SkipPlayerId is skipped.
        if (Node->View.PlayerId == SkipPlayerId)
            continue;

        // .text:06d0e7e6 .. 06d0e7f3 -- FUN_06cf7370 on the tractoring object's id.
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Info.ObjectId))
            continue;

        // .text:06d0e7f9 .. 06d0e820 -- the local XTractorObjects: an empty vector
        // owned id vector, ObjectId, TractorEquipId. The pad word +0x06 is never written.
        XTractorObjects Msg;
        Msg.ObjectId = Info.ObjectId;
        Msg.TractorEquipId = Info.TractorEquipId;

        // .text:06d0e80d .. 06d0e868 -- n = Begin ? (End - Begin) >> 2 : 0, a signed
        // loop; each id the player sees goes through FUN_06cecbd0 (_Insert_n at
        // end, 1) -- a push_back into the message's own vector.
        int const Count = static_cast<int>(Info.TargetIds.size());
        for (int Index = 0; Index < Count; ++Index)
        {
            if (SystemPlayerView_IsObjectIdVisible(&Node->View, Info.TargetIds[Index]))
                Msg.TargetIds.push_back(Info.TargetIds[Index]);
        }

        // .text:06d0e86c .. 06d0e87d -- IClient +0x14 (SendCommonTractorObjects) with
        // node+0x40, sent even when no target id survived.
        ::g_pClientInterface->SendCommonTractorObjects(Node->View.PlayerId, Msg);

        // .text:06d0e880 .. 06d0e89d -- FUN_06d5ffb0 (operator delete) on the
        // block, begin/end/capacity zeroed: here the std::vector going out of scope.
    }
}

// FUN_06d0ed40 (.text:06d0ed40, RET 0x10). See server_sysdb.h for the map.
void StarSystem::tractor_loot(::Loot* LootObj, EqObj* Ship, unsigned int Space, unsigned short TractorId)
{
    // .text:06d0ed4d .. 06d0ed7c -- (ObjectClass & 0x803) on the loot's raw
    // object; a null wrapper gives null too. Kept at [S+0x10].
    CLoot* const Cl = (LootObj != nullptr) ? LootObj->cloot() : nullptr;

    // .text:06d0ed7e .. 06d0ed9d -- ContentsArch (+0xec), Units (+0xf0), the
    // contents' archetype id (Root +0x08), Take = min(Space, Units) unsigned (JBE).
    Archetype::Equipment const* const Contents = Cl->ContentsArch;
    unsigned int const Units = Cl->Units;
    unsigned int const ContentsArchId = Contents->get_id();
    unsigned int Take = Space;
    if (Take > Units)
        Take = Units;

    // .text:06d0eda0 -- the Space slot is reused for the health, 1.0f.
    float Health = 1.0f;

    // .text:06d0ed9f .. 06d0ee1c -- the carried health.
    if (!arch_is_combinable(ContentsArchId))
    {
        // .text:06d0edb5 .. 06d0edc4 -- FLD ContentsHitPts / FDIV Root::HitPts / FSTP.
        Health = Cl->ContentsHitPts / Contents->HitPts;
    }
    else if (Contents->get_class_type() == Archetype::AClassCommodity)   // .text:06d0edc6 .. 06d0edd4 vt +0x10
    {
        // .text:06d0edd6 .. 06d0eded -- get_decay_per_second() compared with 0.0f
        // (.rdata:06d65188), TEST AH,0x41 / JNZ: only a strictly positive decay.
        Archetype::Commodity const* const CommodityArch =
            static_cast<Archetype::Commodity const*>(Contents);
        if (CommodityArch->get_decay_per_second() > 0.0f && Take == Units)   // .text:06d0edef JNZ
        {
            // .text:06d0edf5 .. 06d0ee1c -- (ContentsHitPts - Loot::DecayAccum
            // (+0x68)) held on the x87 stack, replaced by 0.0f unless strictly
            // positive (FCOM / TEST AH,0x41 / JZ), divided by Root::HitPts and
            // stored as a float.
            double Remaining = static_cast<double>(Cl->ContentsHitPts) -
                               static_cast<double>(LootObj->DecayAccum);
            if (!(Remaining > 0.0))
                Remaining = 0.0;
            Health = static_cast<float>(Remaining / static_cast<double>(Contents->HitPts));
        }
    }

    // .text:06d0ee20 .. 06d0ee43 -- add_cargo (FUN_06d0e8d0) on the ship with
    // the CLoot's bTemporary (+0xfd) as the mission flag.
    unsigned short const SubObjId = add_cargo(Ship, ContentsArchId, Take, Health, Cl->bTemporary);

    // .text:06d0ee48 .. 06d0ee80 -- controller event 0x18 through FUN_06d43650,
    // the record at S+0x30. The word at +0x0a is never written.
    struct FControllerMsg18 {
        unsigned int LootId;          // +0x00  .text:06d0ee4f / 06d0ee56
        unsigned int ShipId;          // +0x04  .text:06d0ee5a / 06d0ee5d
        unsigned short SubObjId;      // +0x08  .text:06d0ee77, add_cargo's result
        unsigned short Pad0a;         // +0x0a
        unsigned int GoodId;          // +0x0c  .text:06d0ee66 / 06d0ee73
        unsigned int Count;           // +0x10  .text:06d0ee7c, Take
    } Msg18;
    Msg18.LootId = LootObj->get_id();
    Msg18.ShipId = Ship->get_id();
    Msg18.GoodId = Arch2Good(ContentsArchId);
    Msg18.SubObjId = SubObjId;
    Msg18.Count = Take;
    Server_ControllerDispatch(0, pub::Controller::EMessage::LootAcquired, &Msg18);

    // .text:06d0ee85 .. 06d0eecd -- ITEMTRACTORED at S+0x24: the inlined
    // constructor's {0, 0, SUBOBJ_ID_NONE, 0} is overwritten field by field
    // with {loot id, ship id, TractorId, (ushort)Take}.
    FPayloadServerItemTractored Tractored;
    Tractored.Field_0x00 = 0u;
    Tractored.Field_0x04 = 0u;
    Tractored.Field_0x08 = SUBOBJ_ID_NONE;
    Tractored.Field_0x0a = 0;
    Tractored.Field_0x00 = LootObj->get_id();
    Tractored.Field_0x04 = Ship->get_id();
    Tractored.Field_0x08 = TractorId;
    Tractored.Field_0x0a = static_cast<unsigned short>(Take);

    // .text:06d0eec5 .. 06d0ef09 -- every player node that sees the ship
    // (FUN_06cf7370): IClient +0x100 (SendServerItemTractored) with node+0x40.
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Tractored.Field_0x04))
            continue;
        ::g_pClientInterface->SendServerItemTractored(Node->View.PlayerId, Tractored);
    }

    // .text:06d0ef0f .. 06d0efd6 -- part of the stack: the rest stays in space.
    if (Take != Units)
    {
        Cl->Units = Units - Take;
        return;
    }

    // .text:06d0ef1b .. 06d0ef41 -- the ship's CSimple (& 3, not null-tested)
    // OwnerPlayer (+0xb4): a player's ship in single player keeps its tractor
    // target.
    if (!(Ship->csimple()->OwnerPlayer != 0 && SinglePlayer()))
    {
        // .text:06d0ef43 .. 06d0ef90 -- the ship's CShip (& 0x503, not null-tested
        // before the EquipManager at +0xe4), FindByID(TractorId), CETractor::cast;
        // a tractor drops the loot's id.
        CObject* const Raw = Ship->Object;
        CShip* const ShipCobj =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CShip*>(Raw) : nullptr;
        CETractor* const Tractor = CETractor::cast(ShipCobj->EquipManager.FindByID(TractorId));
        if (Tractor != nullptr)
            Tractor->RemoveTarget(LootObj->get_id());
    }

    // .text:06d0ef96 .. 06d0efa5 -- destroy(2, ship id) through vt +0x158.
    unsigned int const ShipId = Ship->get_id();
    LootObj->destroy(2, ShipId);

    // .text:06d0efab .. 06d0efb2 -- Units = 0.
    Cl->Units = 0u;
}

// LAB_06d06a40 (.text:06d06a40, RET 8) -- SrvTractor's vtable slot 0
// (.rdata:06d65544).
void SrvTractor::notify(Event Code, void* Param)
{
    // .text:06d06a40 .. 06d06a67 -- only event 0x18 (TractorArm::Update's
    // consume timer ran out; Param is the TractorArm), and only with a target:
    // TargetWatch's watchable (+0x08) minus 0x9c, the CLoot (TractorArm::GetTarget).
    if (Code == Event_0x18)
    {
        CLoot* const Target = static_cast<TractorArm*>(Param)->GetTarget();
        if (Target != nullptr)
        {
            // .text:06d06a6d .. 06d06aa3 -- the owner's CShip: CBase (+0xc) then
            // its Object under (& 0x503); null otherwise, and not tested before
            // the call.
            CObject* const Raw = (Owner != nullptr) ? Owner->Object : nullptr;
            CShip* const ShipCobj =
                (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                    ? static_cast<CShip*>(Raw) : nullptr;

            // .text:06d06aa3 .. 06d06aae -- CShip::get_space_for_cargo_type(the
            // CLoot's ContentsArch, +0xec); no room -> nothing.
            int const Space = ShipCobj->get_space_for_cargo_type(Target->ContentsArch);
            if (Space != 0)
            {
                // .text:06d06ab0 .. 06d06acb -- the loot's server object by its
                // CSimple id (+0xb0) with mask 0x10000000 (FUN_06d0c8d0) in the
                // owner's system (+0x28).
                GameObject* const LootObj = Owner->OwnerSystem->find_object(Target->Id, 0x10000000u);
                // .text:06d06acd .. 06d06ad9 -- not while it is being destroyed (vt +0x160).
                if (LootObj != nullptr && !LootObj->is_destroyed())
                {
                    // .text:06d06adb .. 06d06aee -- FUN_06d0ed40(loot, owner,
                    // space, the equip's id (CEquip +0x08, zero-extended)). The
                    // mask above only matches Loot::get_type_bits.
                    Owner->OwnerSystem->tractor_loot(static_cast<::Loot*>(LootObj), Owner,
                                                     static_cast<unsigned int>(Space), Equip->Id);
                }
            }
        }
    }

    // .text:06d06af3 .. 06d06aff -- always the base body (FUN_06ced360).
    ServerEquip::notify(Code, Param);
}

// FUN_06d0efe0 (.text:06d0efe0, RET 0xc). See server_sysdb.h for the map.
void StarSystem::use_item(GameObject* Obj, unsigned short EquipId, int Count)
{
    // .text:06d0efeb .. 06d0f00d -- the (& 3) narrowing, not null-tested, for
    // OwnerPlayer (+0xb4); get_id() (vt +0x20) is taken before the test.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();

    // .text:06d0f010 .. 06d0f057 -- a player's object whose slot is valid
    // ((id - 1) below DAT_06d90264, client_id +0x348 set) and whose ship
    // (+0x3e4) is this object: FUN_06d4f100(slot, EquipId, Count, 0).
    if (OwnerPlayer != 0u && Players.is_valid_ship_owner(OwnerPlayer, ObjId))
        PlayerData_RemoveEquipment(Players[OwnerPlayer], EquipId, Count, false);

    // .text:06d0f05c .. 06d0f073 -- USE_ITEM {get_id() again, EquipId, (ushort)Count}.
    FPayloadServerUseItem Rec;
    Rec.Field_0x00 = Obj->get_id();
    Rec.Field_0x04 = EquipId;
    Rec.Field_0x06 = static_cast<unsigned short>(Count);

    // .text:06d0f06b .. 06d0f0ad -- every player node that sees the object
    // (FUN_06cf7370): IClient +0x104 (SendServerUseItem) with node+0x40.
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Rec.Field_0x00))
            continue;
        ::g_pClientInterface->SendServerUseItem(Node->View.PlayerId, Rec);
    }
}

// FUN_06d0bbc0 (.text:06d0bbc0, RET 8). See server_sysdb.h for the map.
void StarSystem::sp_obj_update(SSPObjUpdateInfo const& Info, unsigned int SessionId)
{
    // .text:06d0bbcc .. 06d0bc0d -- the ship block (+0x34, Head +0x38): `node =
    // node ? node->Next : Head`, a null node returns. Each node's wrapper (null
    // tested), its CBase::Object under (& 3), and that CSimple's Id (+0xb0) read
    // without a test on the narrowing.
    // PORT(perf-index): the walk is ObjectList::find_by_id's (FUN_06cf4f00) -- the first node whose
    // object's get_id, the same CSimple::Id (+0xb0) read, matches -- so the Ships block's id
    // index answers it. A node without a CSimple faults there as it does here: a null-page
    // read before any side effect.
    FSystemListNode* const Node = Ships.find_by_id(Info.ObjectId);
    if (Node == nullptr)
        return;
    CSimple* const Ship = Node->Data->csimple();

    // .text:06d0bc0f .. 06d0bc21 -- PhySys::RCSet(ship, &Position, &Orientation,
    // the double timestamp).
    PhySys::RCSet(Ship, Info.Position, Info.Orientation, Info.Timestamp);

    // .text:06d0bc27 .. 06d0bc70 -- the UPDATEOBJECT argument at S+0xc: id,
    // orientation, position, the timestamp stored as a float (FLD double /
    // FSTP float), +0x24 a raw dword copy of Info +0x28, byte +0x28 from Info
    // +0x2c. The pad bytes are never written.
    FCommonUpdateObjectInfo Rec;
    Rec.ObjectId = Info.ObjectId;
    Rec.Orientation = Info.Orientation;
    Rec.Position = Info.Position;
    Rec.Field_0x20 = static_cast<float>(Info.Timestamp);
    std::memcpy(&Rec.Field_0x24, &Info.Field_0x28, sizeof(Rec.Field_0x24));
    Rec.Field_0x28 = Info.Field_0x2c;

    // .text:06d0bc69 .. 06d0bc99 -- every player node but the sender's
    // (node+0x40 == SessionId skipped): FUN_06cf65b0 on the view at node+8.
    for (SystemPlayerNode* Player = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Player != static_cast<SystemPlayerNode*>(PlayerListHead); Player = Player->Next)
    {
        if (Player->View.PlayerId == SessionId)
            continue;
        SystemPlayerView_SendObjectUpdate(&Player->View, &Rec);
    }
}

// FUN_06d0bcd0 (.text:06d0bcd0, RET 0x14, 0x201c-byte frame through the stack
// probe FUN_06d60160). See server_sysdb.h for the map.
unsigned int StarSystem::scan_objects(unsigned int* OutIds, unsigned int MaxCount, Vector const* Pos,
                                      float Radius, unsigned int TypeMask)
{
    unsigned int Count = 0;

    // .text:06d0bcf7 .. 06d0bd21 -- ships first: FUN_06cf5020 on the +0x34 block.
    if ((TypeMask & 0x7f0000u) != 0u)
        Count = Ships.collect_in_sphere(OutIds, MaxCount, Pos, Radius, TypeMask);

    // .text:06d0bd23 .. 06d0bd29 -- nothing else asked for: done.
    if ((TypeMask & 0xfdffu) == 0u)
        return Count;

    // .text:06d0bd2f .. 06d0bd41 -- FCOMP 0.0f / TEST AH,0x41 / JNZ: a radius
    // that is not strictly positive walks the solar block (+0x5c) instead of
    // the spatial bucket, into what is left of the buffer.
    if (!(Radius > 0.0f))
    {
        // .text:06d0be11 .. 06d0be31
        Count += Solars.collect_in_sphere(OutIds + Count, MaxCount - Count, Pos, Radius, TypeMask);
        return Count;
    }

    // .text:06d0bd47 .. 06d0bd55 -- GetBucket (IObjDB vt +0xc) with 0xfdff; none -> done.
    void* const Bucket = GetBucket(0xfdffu);
    if (Bucket == nullptr)
        return Count;

    // .text:06d0bd5b .. 06d0bd98 -- the sphere {*Pos, Radius} at S+0x14 and the
    // result block at S+0x24 {capacity 0x800, count 0, 0x800 slots}, handed to
    // the partition's vtable +0x1c (Fill, thiscall). Its return value is not
    // used; the count is read back from the block.
    struct FBucketQuery
    {
        Vector Pos;
        float Radius;
    } Sphere;
    Sphere.Pos = *Pos;
    Sphere.Radius = Radius;
    struct FBucketResult
    {
        unsigned int Capacity;
        unsigned int Count;
        IObjRW* Data[0x800];
    } Found;
    Found.Capacity = 0x800u;
    Found.Count = 0u;
    // ISpatialPartition +0x1c (sphere query); the stack sphere and result have the layouts of
    // Geometry::Sphere and FSpatialQueryResult.
    static_cast<ISpatialPartition*>(Bucket)->QuerySphere(reinterpret_cast<Geometry::Sphere const*>(&Sphere),
                                                         reinterpret_cast<FSpatialQueryResult*>(&Found));

    // .text:06d0bd9b .. 06d0be00 -- each hit while the buffer has room (the
    // unsigned Count < MaxCount test runs before each element): the element (the
    // inlined bounds-checked accessor gives null past the count), its
    // IObjInspect::get_type (vt +0x88) -- a non-zero result skips it -- then
    // type & TypeMask, and get_id (vt +0x20) into the buffer.
    for (unsigned int Index = 0; Index < Found.Count; ++Index)
    {
        if (Count >= MaxCount)
            return Count;
        IObjRW* const Hit = (Index < Found.Count) ? Found.Data[Index] : nullptr;
        unsigned int HitType;
        if (Hit->get_type(HitType) != 0)
            continue;
        if ((HitType & TypeMask) == 0u)
            continue;
        OutIds[Count] = Hit->get_id();
        ++Count;
    }
    return Count;
}
// StarSystem::load — retail FUN_06d06ed0 (.text:06d06ed0).
// See the declaration in server_sysdb.h for the whole instruction map, for the
// arity correction (Ghidra says `void FUN_06d06ed0(void)`; .text:06d06ef0 is
// `MOV EBX,ECX`, so it is a thiscall with no stack arguments) and for what the
// populate helper does and does not reproduce.
// ===========================================================================
void StarSystem::load()
{
    // .text:06d06ef2  CMP byte ptr [EBX + 0xfc],0x1 / .text:06d06ef9 JZ -- note
    // that this guard tests for 1, not for "non-zero"; add_player's own guard
    // at .text:06d07e49 is the one that tests for zero.
    if (bLoaded == 1)
        return;

    // .text:06d06eff .. .text:06d06f06 -- `SystemInfo->vtable[0]()`. This port's
    // Universe::ISystem is a flat record rather than retail's interface (see
    // common_universe.h), so the same value is the NicknameId field. Retail
    // calls through SystemInfo without a null test; SysDB_Initialize sets it on
    // every entry it builds.
    unsigned int const SystemId = SystemInfo->NicknameId;

    // .text:06d06f0b  CALL COMMON.DLL::PhySys::Startup, cdecl, one argument
    // (.text:06d06f11 `ADD ESP,0x4`). The import is PhySys::Startup; earlier
    // notes in this tree called it "PhySys::Start", which is not the name in
    // the import table.
    PhySys::Startup(SystemId);

    // .text:06d06f14 .. .text:06d06f2e -- single player: wrap the solars the
    // client already built (FUN_06d07170), then mirror its asteroid fields
    // (FUN_06d03500).
    if (SinglePlayer()) {
        load_solars();
        SrvAsteroid::SrvAsteroid_MapSystem(SystemId);
    } else {
        // .text:06d06f33 .. .text:06d06f41 -- the system file's path:
        // SystemInfo vtable +0x14 is the file name (the flat record's File
        // field in this port), Universe::get_filename into a MAX_PATH buffer
        // (stack +0x18 .. +0x11c).
        char SystemIniPath[260];
        Universe::get_filename(SystemIniPath, SystemInfo->File);

        // .text:06d06f48 CmnAsteroid::Load(sysId), .text:06d06f54
        // FUN_06d032b0(sysId, path); both results dropped (.text:06d06f59
        // ADD ESP,0x14 with no test).
        CmnAsteroid::Load(SystemId);
        SrvAsteroid::SrvAsteroid_LoadSystem(SystemId, SystemIniPath);

        // .text:06d06f5c .. .text:06d06fd1 -- INI_Reader over the file; every
        // [Object] section goes to FUN_06d09d60, its result dropped. The
        // reader's destructor (.text:06d06fd3 .. 06d07041: FileMap::close and
        // the string destructors, inlined) closes it.
        INI_Reader Reader;
        if (Reader.open(SystemIniPath, false)) {
            while (Reader.read_header()) {
                if (Reader.is_header("Object"))
                    load_object(Reader);
            }
        }
    }

    // .text:06d07047 .. .text:06d070e7 -- the spatial partition over the
    // solars:
    //   06d07047  count = Solars.count_type(0xffffffff)      (FUN_06cf5130)
    //   06d0705c  this->Spatial = COMMON.DLL::NewSpatial("BSP") (.rdata:06d67d04)
    //   06d07065  when that is null nothing else happens (JZ 06d070e8)
    //   06d07074  operator new(count * 0x14)
    //   06d07085 .. 06d070ca  for EVERY node of the Solars list (the count
    //             above filtered, the walk does not) one 0x14-byte record:
    //             +0x00 node->Data                    (06d07098)
    //             +0x04 get_position(), three dwords  (06d0709f CALL [EAX] ..
    //                                                  06d070b1)
    //             +0x10 cobject()->Radius, a float    (06d070b8 CALL [EDX+0x150],
    //                                                  06d070be FLD [EAX+0x38])
    //   06d070d7  Spatial->vtable[+0x14](array, count)  -- the count is pushed
    //             first, so the array is the first argument
    //   06d070df  operator delete(array)
    int const SolarCount = static_cast<int>(Solars.count_type(0xffffffffu));
    Spatial = NewSpatial("BSP");
    if (Spatial != nullptr) {
        FSpatialEntry* const Entries = static_cast<FSpatialEntry*>(
            ::operator new(static_cast<size_t>(SolarCount) * sizeof(FSpatialEntry)));
        FSpatialEntry* Out = Entries;
        FSystemListNode* Node = nullptr;
        for (;;) {
            Node = (Node == nullptr) ? Solars.Head : Node->Next;
            if (Node == nullptr)
                break;
            GameObject* const Obj = Node->Data;
            Out->Object = static_cast<IObjRW*>(Obj);
            Out->Position = Obj->get_position();
            Out->Radius = Obj->cobject()->Radius;
            ++Out;
        }
        // ISpatialPartition +0x14: AddArray(entries, count).
        Spatial->AddArray(Entries, static_cast<unsigned>(SolarCount));
        ::operator delete(Entries);
    }

    // .text:06d070eb  MOV byte ptr [EBX + 0xfc],0x1
    bLoaded = 1;

    // .text:06d070e8 .. .text:06d07101 -- retail re-reads the system id off
    // SystemInfo (it is the same value) into a stack slot and broadcasts
    // FUN_06d43650(0x3c, &sysId): "this system is now loaded". FUN_06d43650 is
    // pub::Controller::_SendMessage(0, id, data), which this port reaches
    // through Server_ControllerDispatch.
    unsigned int EventData = SystemId;
    Server_ControllerDispatch(0u, pub::Controller::EMessage::SystemLoaded, &EventData);
}

// ===========================================================================
// StarSystem::load_solars -- retail FUN_06d07170 (.text:06d07170). See the
// declaration in server_sysdb.h for the instruction map.
// ===========================================================================
void StarSystem::load_solars()
{
    int Count = 0;                                                  // .text:06d07191
    CObject* Obj = CObject::FindFirst(CObject::ClassSolar);         // .text:06d07199 PUSH 0x303
    if (Obj != nullptr)
    {
        do
        {
            // .text:06d071b2 -- the walk's own reference; attach_existing
            // takes the wrapper's, and .text:06d07224 gives this one back.
            Obj->AddRef();
            ::Solar* const Wrapper = new ::Solar();                 // .text:06d071bd / FUN_06d02b70

            // .text:06d071e2 .. 06d07206 -- the object only when its class
            // passes the 0x303 test, else 0.
            CObject* const Attached =
                ((static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u) ? Obj : nullptr;
            Wrapper->attach_existing(Attached);                     // FUN_06d02c20

            Wrapper->enter_system(this, nullptr);                   // .text:06d07212 vt +0x170
            Wrapper->check_fuses();                                 // .text:06d0721c vt +0x1bc
            Obj->Release();                                         // .text:06d07224

            // .text:06d0722a .. 06d0726b -- MetaList append, inlined (the
            // FreeList reuse when bUseHeap, else operator new(0xc)).
            Solars.append(Wrapper);
            ++Count;

            Obj = CObject::FindNext();                              // .text:06d07272
        } while (Obj != nullptr);

        if (Count != 0)                                             // .text:06d07286 JNZ
            return;
    }

    // .text:06d0728b .. 06d072ac
    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x129,
          "failed to find any solar objects\n");
}

// ===========================================================================
// StarSystem::load_object -- retail FUN_06d09d60 (.text:06d09d60, RET 4). See
// the declaration in server_sysdb.h for the instruction map.
//
// The CSolar::CreateParms stack object: retail inlines its constructor and
// destructor. The CSimple half is the type's default member initialisers
// (FUN_06d08e20's values); the EqObj fields the inlined ctor writes are
// written below in the same order, and the ones it leaves alone are zeroed
// (retail leaves them as stack garbage for CSolar::ReadObj to overwrite).
// The anim-name list at +0xe4 (allocator byte) / +0xe8 (head) / +0xec (size)
// is a std::list<char const*> member of CSolar::CreateParms
// (CSolar::CreateParms::ContinualAnims, common_cobject.h): its ctor FUN_06d1ad30
// allocates a head linked to itself, CSolar::ReadObj appends one name per "anim"
// key, and the parms destructor (inlined at .text:06d09f4f / 06d09fd9: FUN_06d5c540
// begin + FUN_06cf7720 erase(first, last), then the head) frees it after
// CSolar::init. On an exception the same destructor runs from Unwind@06d613f4
// (-> ~CSolar::CreateParms FUN_06d0a120: that list first, then +0xac, then the
// vector); the member's own destructor is that unwind in the port.
// ===========================================================================
GameObject* StarSystem::load_object(INI_Reader& Reader)
{
    // .text:06d09d80 .. 06d09e47 -- the parms.
    CSolar::CreateParms Parms;
    Parms.Pad58 = 0;
    Parms.DockingCount = 0;
    Parms.VibeId = 0;
    Parms.SpaceCostume.PartIds[0] = 0;                             // FUN_06d0a060 +0x64
    Parms.SpaceCostume.PartIds[1] = 0;                             //              +0x68
    Parms.SpaceCostume.PartIds[2] = 0;                             //              +0x6c
    Parms.SpaceCostume.PartIds[3] = 0;                             //              +0x70
    for (int Index = 0; Index < 8; ++Index)
        Parms.SpaceCostume.AccessoryIds[Index] = 0;
    Parms.SpaceCostume.AccessoryCount = 0;                         //              +0x94
    Parms.VoiceId = 0;                                             // .text:06d09daa +0x98
    Parms.NicknameSrc.Id = 0;                                      // ID_String::clear +0xb8
    Parms.VisitValue = 0;                                          // .text:06d09df1 +0xbc
    Parms.ParentNicknameId = 0;                                    // ID_String::clear +0xc0
    Parms.bDestructible = 0;                                       // .text:06d09e01 +0xc4
    Parms.bDynamic = 0;                                            // .text:06d09e08 +0xc5
    Parms.DestSystem = 0;                                          // .text:06d09e0f +0xc8
    Parms.DestGateId = 0;                                          // ID_String::clear +0xcc
    Parms.PrevRing = 0;                                            // .text:06d09e24 +0xd0
    Parms.NextRing = 0;                                            // .text:06d09e2b +0xd4
    Parms.TradelaneSpaceNameId = 0;                                // .text:06d09e32 +0xd8
    Parms.AtmosphereRange = 0.0f;                                  // .text:06d09e39 +0xdc
    Parms.DockWithBaseId = 0;                                      // .text:06d09e40 +0xe0
    // +0xe4: Parms.ContinualAnims was built empty with Parms (FUN_06d1ad30).

    // .text:06d09e4c .. 06d09e69 -- the bool result is not read.
    Archetype::Solar* Arch = nullptr;
    CSolar::ReadObj(Reader, Arch, Parms);
    // .text:06d09e78 MOV [ESP+0x20],EDI -- Parms+0x04 (ObjDb) = this, after ReadObj and
    // before the archetype test; CSolar::init's connect() hands it to init_physics.
    Parms.ObjDb = this;

    ::Solar* Wrapper = nullptr;
    if (Arch != nullptr)                                           // .text:06d09e76 / 06d09e7c
    {
        Arch->load();                                              // .text:06d09e84 vt +0xc
        Wrapper = new ::Solar();                                   // .text:06d09e8c / FUN_06d02b70
        Wrapper->create(Arch);                                     // .text:06d09ec2 vt +0x164
        Wrapper->csolar()->init(Parms);                            // .text:06d09ec8 .. 06d09ef0

        // .text:06d09ef6 .. 06d09f30 -- MetaList append, inlined.
        Solars.append(Wrapper);

        Arch->Release();                                           // .text:06d09f33
        Wrapper->enter_system(this, nullptr);                      // .text:06d09f3f vt +0x170
        Wrapper->check_fuses();                                    // .text:06d09f49 vt +0x1bc
    }

    // .text:06d09f4f .. 06d09f9e / .text:06d09fd9 .. 06d0a02c -- the parms
    // destroyed: the anim list, then the CollisionGroupDesc list and the
    // EquipDescVector. That is Parms' destructor, run on return.
    return Wrapper;                                                // .text:06d09fd5 / 06d0a043
}

// ===========================================================================
// StarSystem::create_solar -- retail FUN_06d0a200 (.text:06d0a200, RET 4). See
// the declaration in server_sysdb.h for the instruction map. S = ESP after the
// four register pushes; `this` is kept at [S+0x10], the CSolar at [S+0x1c],
// the CSolar::CreateParms at [S+0x28].
// ===========================================================================
::Solar* StarSystem::create_solar(CSolarCreateInfo& Req)
{
    // .text:06d0a21e .. 06d0a24d -- operator new(0x90) + FUN_06d02b70.
    ::Solar* const Wrapper = new ::Solar();

    // .text:06d0a255 .. 06d0a289
    Archetype::Solar* const Arch = Archetype::GetSolar(Req.ArchetypeNickname);
    Arch->load();                                                  // vt +0xc
    Wrapper->create(Arch);                                         // vt +0x164
    Arch->Release();                                               // Archetype::Root::Release

    // .text:06d0a289 .. 06d0a2b0 -- the CBase object under the 0x303 guard.
    CObject* const Raw = Wrapper->Object;
    CSolar* const Cs =
        (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x303u) == 0x303u)
            ? static_cast<CSolar*>(Raw) : nullptr;

    // .text:06d0a2b0 .. 06d0a2b4 -- FUN_06d0a770: the CSimple half is the
    // type's default member initialisers (Id / ObjDb / OwnerPlayer 0, HitPts
    // -1.0f, Pos 0, Orient identity via FUN_06d0a850, Velocity / AngularVelocity
    // 0); then +0x58 / +0x5c 0, +0x60 -1, +0x64..+0x70 0, +0x94 / +0x98 0, the
    // EquipDescVector and the CollisionGroupDesc list empty. +0x74..+0x93
    // (the accessory ids) are not written by it.
    CSolar::CreateParms Parms;
    Parms.Pad58 = 0u;
    Parms.DockingCount = 0u;
    Parms.VibeId = 0xffffffffu;                                    // .text:06d0a7ce
    Parms.SpaceCostume.PartIds[0] = 0u;
    Parms.SpaceCostume.PartIds[1] = 0u;
    Parms.SpaceCostume.PartIds[2] = 0u;
    Parms.SpaceCostume.PartIds[3] = 0u;
    Parms.SpaceCostume.AccessoryCount = 0;
    Parms.VoiceId = 0u;

    // .text:06d0a2bf .. 06d0a34b -- the CSolar tail.
    Parms.NicknameSrc.clear();                                     // +0xb8
    Parms.VisitValue = 0u;                                         // +0xbc
    reinterpret_cast<ID_String&>(Parms.ParentNicknameId).clear();  // +0xc0
    Parms.bDestructible = 0u;                                      // +0xc4
    Parms.bDynamic = 0u;                                           // +0xc5
    Parms.DestSystem = 0u;                                         // +0xc8
    reinterpret_cast<ID_String&>(Parms.DestGateId).clear();        // +0xcc
    Parms.PrevRing = 0u;                                           // +0xd0
    Parms.NextRing = 0u;                                           // +0xd4
    Parms.TradelaneSpaceNameId = 0u;                               // +0xd8
    Parms.AtmosphereRange = 0.0f;                                  // +0xdc
    Parms.DockWithBaseId = 0u;                                     // +0xe0
    // +0xe4: Parms.ContinualAnims was built empty with Parms (FUN_06cfc340(0, 0) head).

    // .text:06d0a352 .. 06d0a3cc
    Parms.HitPts = static_cast<float>(Req.HitPts);                 // FILD +0x68
    Parms.Id = Req.ObjId;                                          // +0x04
    Parms.ObjDb = this;
    Parms.Pos = Req.Position;                                      // +0x4c
    Parms.OwnerPlayer = 0u;
    Server_QuatToMatrix(Parms.Orient, Req.Orientation);            // DAT_06d8da08 vt +0x54, REP MOVSD

    // .text:06d0a3b3 .. 06d0a453
    Parms.VisitValue = Req.Field_0xb4;
    Parms.DockingCount = Req.DockingBaseId;                        // +0x0c -> +0x5c
    Parms.DockWithBaseId = Req.DockingBaseId;                      //       -> +0xe0
    Parms.VibeId = static_cast<unsigned int>(Req.VibeGroupId);     // +0x10 -> +0x60
    Parms.SpaceCostume.PartIds[0] = Req.CostumeHead;               // +0x14 -> +0x64
    Parms.VoiceId = Req.VoiceId;                                   // +0x48 -> +0x98
    Parms.SpaceCostume.PartIds[1] = Req.CostumeBody;               // +0x18 -> +0x68
    Parms.SpaceCostume.PartIds[2] = Req.CostumeLeftHand;           // +0x1c -> +0x6c
    Parms.SpaceCostume.PartIds[3] = Req.CostumeRightHand;          // +0x20 -> +0x70
    Parms.SpaceCostume.AccessoryCount = Req.CostumeAccessoryCount; // +0x44 -> +0x94
    // JLE / JL (signed), the count re-read from Parms+0x94 every pass; no clamp.
    for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
        Parms.SpaceCostume.AccessoryIds[Index] = Req.CostumeAccessory[Index];

    // .text:06d0a455 .. 06d0a471 -- vector<EquipDesc>::operator= from the
    // loadout's items (Map +4; the port's Map keeps them in a std::vector).
    // Parms.Equipment is still empty here, so VC6's operator= takes its
    // reallocating branch for a non-empty source and does nothing for an
    // empty one. Loadout::Get's result is not tested.
    if (Req.Field_0x6c != 0u)
    {
        Loadout::Map const* const Map = Loadout::Get(Req.Field_0x6c);
        int const Count = static_cast<int>(Map->Items.size());
        if (Count > 0)
        {
            EquipDesc* const Block =
                static_cast<EquipDesc*>(::operator new(static_cast<size_t>(Count) * sizeof(EquipDesc)));
            EquipDesc* Dest = Block;
            for (int Index = 0; Index < Count; ++Index)
            {
                new (Dest) EquipDesc(Map->Items[static_cast<size_t>(Index)]);
                ++Dest;
            }
            Parms.Equipment.Begin = Block;
            Parms.Equipment.End = Dest;
            Parms.Equipment.Cap = Dest;
        }
    }
    Parms.Equipment.append(Req.Equipment);                         // .text:06d0a482

    // .text:06d0a488 .. 06d0a506 -- list operator=, inlined (the two lists are
    // never the same object).
    Parms.ArchGroupDescs = Req.CollisionGroups;

    // .text:06d0a50c .. 06d0a537
    Parms.NicknameSrc = Req.Field_0xb8;                            // +0xb8
    Parms.bDestructible = 1u;                                      // +0xc4
    Parms.bDynamic = Req.Field_0xac;                               // +0xc5
    Cs->init(Parms);

    // .text:06d0a53d .. 06d0a54f
    if ((Req.Field_0xbc & 0x3fffffffu) != 0u)
        Wrapper->mark_for_player(Req.Field_0xbc, 1);

    Wrapper->enter_system(this, nullptr);                          // .text:06d0a55f vt +0x170
    Wrapper->check_fuses();                                        // .text:06d0a569 vt +0x1bc

    // .text:06d0a56f .. 06d0a596 -- MetaList append on +0x5c, inlined.
    Solars.append(Wrapper);

    // .text:06d0a599 .. 06d0a614 -- the spatial partition's single add (vt +0x10)
    // with the same 0x14-byte record load() bulk-adds.
    void* const Bucket = GetBucket(0xfdffu);
    if (Bucket != nullptr)
    {
        CObject* const BucketRaw = Wrapper->Object;
        CSolar* const BucketSolar =
            (BucketRaw != nullptr && (static_cast<unsigned int>(BucketRaw->ObjectClass) & 0x303u) == 0x303u)
                ? static_cast<CSolar*>(BucketRaw) : nullptr;
        float const Radius = BucketSolar->Radius;                  // .text:06d0a5cc [EAX+0x38]
        Vector const& Position = Wrapper->get_position();          // .text:06d0a5d7 vt 0
        FSpatialEntry Entry;
        Entry.Position = Position;
        Entry.Radius = Radius;
        Entry.Object = static_cast<IObjRW*>(Wrapper);
        static_cast<ISpatialPartition*>(Bucket)->Add(&Entry); // +0x10
    }

    // .text:06d0a617 .. 06d0a61d -- on the CSolar kept at [S+0x1c].
    Cs->enable_collisions_r(true);

    // .text:06d0a623 .. 06d0a66f -- every player of this system.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        Req.bMarkedBy = Wrapper->is_marked_by(Player->View.PlayerId) ? 1u : 0u;  // +0xa8
        Req.VibeOtherGroupId = Players[Player->View.PlayerId].rep_vibe;           // +0xb0
        SystemPlayerView_CreateSolarForPlayer(&Player->View, &Req, Wrapper);
    }

    // .text:06d0a671 .. 06d0a74a -- the parms destroyed on return: the anim
    // list (every node erased, FUN_06d1b490 / FUN_06d0f620, then the head),
    // then the group list and the EquipDescVector (.text:06d0a6da .. 06d0a744).
    return Wrapper;                                                // .text:06d0a753
}

// ===========================================================================
// StarSystem::create_ship -- retail FUN_06d0a880 (.text:06d0a880, RET 4). See
// the declaration in server_sysdb.h for the instruction map. S = ESP after the
// four register pushes: the wrapper at [S+0x18], `this` at [S+0x20], the
// CShip at [S+0x14], the request copy at [S+0x2c], the CShip::CreateParms at
// [S+0x10c].
// ===========================================================================
::Ship* StarSystem::create_ship(FShipCreateRequest const& Req)
{
    // .text:06d0a8a1 .. 06d0a8d0 -- operator new(0xc8) + FUN_06d00e80.
    ::Ship* const Wrapper = new ::Ship();

    // .text:06d0a8d2 .. 06d0a98f -- FUN_06d0af00 (the CEqObjCreateInfo copy
    // constructor) and the derived tail +0xac .. +0xdc; the vftable is the
    // CShipCreateInfo one (.rdata:06d65f78).
    CShipCreateInfo Copy(static_cast<CShipCreateInfo const&>(Req));

    CShip* Cs = nullptr;                                            // [S+0x14]
    if (Copy.OwnerPlayer != 0u && SinglePlayer())                   // .text:06d0a997 / 06d0a9a6
    {
        // .text:06d0a9b0 .. 06d0a9ec -- vt +0x168 with &Req +0x04 (the request,
        // not the copy), then the 0x503 narrowing.
        Wrapper->find(Req.ObjId);
        CObject* const Raw = Wrapper->Object;
        Cs = (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                 ? static_cast<CShip*>(Raw) : nullptr;
    }
    else
    {
        // .text:06d0a9f1 .. 06d0aa15
        Archetype::Ship* const Arch = Archetype::GetShip(Copy.ArchetypeNickname);
        Arch->load();                                               // vt +0xc
        Wrapper->create(Arch);                                      // vt +0x164
        Arch->Release();                                            // Archetype::Root::Release

        // .text:06d0aa1b .. 06d0aa44
        CObject* const Raw = Wrapper->Object;
        Cs = (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                 ? static_cast<CShip*>(Raw) : nullptr;

        // .text:06d0aa44 .. 06d0aa65 -- FUN_06d0a770 (the CSimple half is the
        // type's default member initialisers; see create_solar), then the
        // CShip tail. The CEqObj tail +0xb8 .. +0xe4 is not written (retail
        // leaves it as stack garbage; CShip::init does not read it).
        CShip::CreateParms Parms;
        Parms.Pad58 = 0u;
        Parms.DockingCount = 0u;
        Parms.VibeId = 0xffffffffu;                                // .text:06d0a7ce
        Parms.SpaceCostume.PartIds[0] = 0u;
        Parms.SpaceCostume.PartIds[1] = 0u;
        Parms.SpaceCostume.PartIds[2] = 0u;
        Parms.SpaceCostume.PartIds[3] = 0u;
        Parms.SpaceCostume.AccessoryCount = 0;
        Parms.VoiceId = 0u;
        Parms.FieldE8 = 0u;                                       // +0xe8 [S+0x1f4]
        Parms.GroupNameId = 0u;                                    // +0xec [S+0x1f8]
        Parms.Rank = 0u;                                           // +0xf0 [S+0x1fc]
        Parms.bUsingTradelane = 0u;                                // +0xf4 [S+0x200]

        // .text:06d0aa6d .. 06d0abbf
        Parms.HitPts = static_cast<float>(Copy.HitPts);            // FILD +0x68
        Parms.OwnerPlayer = Copy.OwnerPlayer;                      // +0xac -> +0x08
        Parms.Id = Copy.ObjId;                                     // +0x04 -> +0x00
        Parms.Pos = Copy.Position;                                 // +0x4c -> +0x10
        Parms.ObjDb = this;                                        // EDI -> +0x04
        Server_QuatToMatrix(Parms.Orient, Copy.Orientation);       // +0x58, REP MOVSD -> +0x1c
        Parms.Velocity = Copy.Velocity;                            // +0xb8 -> +0x40
        Parms.AngularVelocity = Copy.AngularVelocity;              // +0xc4 -> +0x4c
        Parms.VibeId = static_cast<unsigned int>(Copy.VibeGroupId); // +0x10 -> +0x60
        Parms.bUsingTradelane = Copy.bUsingTradelane;              // byte +0xdc -> +0xf4
        Parms.SpaceCostume.PartIds[0] = Copy.CostumeHead;          // +0x14 -> +0x64
        Parms.VoiceId = Copy.VoiceId;                              // +0x48 -> +0x98
        Parms.SpaceCostume.PartIds[3] = Copy.CostumeRightHand;     // +0x20 -> +0x70
        Parms.Rank = Copy.ReputationRank;                          // +0xb4 -> +0xf0
        Parms.SpaceCostume.PartIds[2] = Copy.CostumeLeftHand;      // +0x1c -> +0x6c
        Parms.SpaceCostume.PartIds[1] = Copy.CostumeBody;          // +0x18 -> +0x68
        Parms.SpaceCostume.AccessoryCount = Copy.CostumeAccessoryCount; // +0x44 -> +0x94
        // .text:06d0abc6 JLE / .text:06d0abc8 loop (signed), the count re-read from
        // Parms+0x94 every pass; no clamp.
        for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
            Parms.SpaceCostume.AccessoryIds[Index] = Copy.CostumeAccessory[Index];

        // .text:06d0abdf .. 06d0abff -- vector<EquipDesc>::operator= from the
        // loadout's items (see create_solar: the destination is empty, so only
        // the reallocating branch or nothing). No test on Loadout::Get.
        if (Copy.Field_0x6c != 0u)
        {
            Loadout::Map const* const Map = Loadout::Get(Copy.Field_0x6c);
            int const Count = static_cast<int>(Map->Items.size());
            if (Count > 0)
            {
                EquipDesc* const Block =
                    static_cast<EquipDesc*>(::operator new(static_cast<size_t>(Count) * sizeof(EquipDesc)));
                EquipDesc* Dest = Block;
                for (int Index = 0; Index < Count; ++Index)
                {
                    new (Dest) EquipDesc(Map->Items[static_cast<size_t>(Index)]);
                    ++Dest;
                }
                Parms.Equipment.Begin = Block;
                Parms.Equipment.End = Dest;
                Parms.Equipment.Cap = Dest;
            }
        }
        Parms.Equipment.append(Copy.Equipment);                    // .text:06d0ac14

        // .text:06d0ac1a .. 06d0ac8e -- list operator=, inlined.
        Parms.ArchGroupDescs = Copy.CollisionGroups;

        // .text:06d0ac94 .. 06d0acbd
        Parms.GroupNameId = Copy.Field_0xb0;                       // +0xb0 -> +0xec
        Parms.DockingCount = Copy.DockingBaseId;                   // +0x0c -> +0x5c
        Cs->init(Parms);                                           // vt +0x10c

        // .text:06d0acc3 .. 06d0ace5 -- the owner's player group id into
        // CShip +0x1b4 (GroupId).
        if (Copy.OwnerPlayer != 0u)
        {
            CPlayerGroup* const Group =
                static_cast<CPlayerGroup*>(Players[Copy.OwnerPlayer].group_ptr);   // slot -0x14
            if (Group != nullptr)
            {
                // [group+4] read inline in retail; GetID is that same load.
                unsigned int const GroupId = Group->GetID();
                Cs->GroupId = GroupId;
            }
        }
        // .text:06d0aceb .. 06d0ad0f -- the parms' list and vector destroyed.
    }

    // .text:06d0ad19 .. 06d0ad41 -- an EquipIdMaker constructed, then its word
    // and the dword after it overwritten from Req +0xe0 / +0xe4.
    ::Ship::FEquipState State;
    State.EquipIds = Req.EquipIds;
    State.Data = Req.EquipStateData;

    // .text:06d0ad30 .. 06d0ad4c
    if ((Req.MarkPlayerId & 0x3fffffffu) != 0u)
        Wrapper->mark_for_player(Req.MarkPlayerId, 1);

    Wrapper->enter_system(this, nullptr);                          // .text:06d0ad58 vt +0x170
    Wrapper->set_equip_state(State);                               // .text:06d0ad65 FUN_06d010e0

    // .text:06d0ad6a .. 06d0ad8f -- MetaList append on +0x34, inlined.
    Ships.append(Wrapper);

    // .text:06d0ad92 .. 06d0adab
    if (!SinglePlayer() || Copy.OwnerPlayer == 0u)
        Cs->enable_collisions_r(true);

    // .text:06d0adb1 .. 06d0adf0 -- every player of this system but the owner.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == Copy.OwnerPlayer)
            continue;
        Copy.bMarkedBy = Wrapper->is_marked_by(Player->View.PlayerId) ? 1u : 0u;   // copy +0xa8
        SystemPlayerView_CreateShipForPlayer(&Player->View, &Copy, Wrapper);
    }

    // .text:06d0adf2 .. 06d0aed2 -- ~CShipCreateInfo on the copy.
    return Wrapper;                                                // .text:06d0aedf
}

// ===========================================================================
// StarSystem::add_player — retail FUN_06d07e20(this, SessionId), 0x06d07e20.
// The list-insert half; see the declaration in server_sysdb.h for the full
// instruction map and for what is still missing.
// ===========================================================================
void StarSystem::add_player(unsigned int SessionId)
{
    // .text:06d07e41  MOV AL,[EDI + 0xfc] / .text:06d07e49 CMP AL,BL /
    // .text:06d07e4f JNZ / .text:06d07e51 CALL FUN_06d06ed0 -- an unloaded
    // system is loaded first, and the load is what sets +0xfc to 1. EBX is zero
    // from .text:06d07e47 `XOR EBX,EBX`, so the test really is against zero
    // here, while load()'s own guard compares against 1 (.text:06d06ef2).
    if (bLoaded == 0)
        load();

    // .text:06d07e5d  TEST EBP,0x3fffffff / .text:06d07e63 JZ -- and note the
    // mask. Retail does not test the id against zero; it tests it against
    // 0x3fffffff, so an id whose only set bits are the top two is rejected too.
    if ((SessionId & 0x3fffffffu) == 0)
        return;

    // .text:06d07e76 / .text:06d07e79 -- the list is `this + 0x8` and the
    // insert position is its sentinel `this + 0xc`, i.e. a push_back.
    // No null test on the sentinel: retail loads [EDI+0xc] and links through it
    // (FUN_06d0f330, .text:06d0f330 -- `MOV ECX,[EAX+4]` on the position node
    // with no test). The StarSystem constructor always allocates it
    // (.text:06d06d17 FUN_06d0f3e0(0,0)).
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);

    // .text:06d07e8a  std::list<Observer>::insert(end(), Observer(SessionId)).
    // Retail builds the element on the stack first (.text:06d07e69 PUSH EBP /
    // .text:06d07e71 CALL FUN_06cf51e0), lets insert copy it into the freshly
    // allocated node, then destroys the temporary with a direct call
    // (.text:06d07ea1 CALL FUN_06cf52d0) -- the end of this block.
    {
        Observer Temp(SessionId);

        // FUN_06d0f330 (.text:06d0f330, list::insert, `RET 0xc`), inlined:
        //   06d0f336  EBP = position->Prev (read before the allocation)
        //   06d0f33f  operator new(0x50)
        //   06d0f34b  node->Next = position
        //   06d0f347 .. 06d0f34f  `TEST EBP,EBP / JNZ / MOV EBP,ESI` -- MSVC6
        //             _Buynode's null fallback: a null Prev becomes the node
        //   06d0f355  node->Prev = EBP; 06d0f358 position->Prev = node;
        //   06d0f35b .. 06d0f363  node->Prev->Next = node
        //   06d0f365  FUN_06d0f6d0 -- the element copy-constructed at node+8
        //   06d0f36a .. 06d0f371  ++size
        SystemPlayerNode* Prev = Sentinel->Prev;
        SystemPlayerNode* const Node =
            static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
        Node->Next = Sentinel;
        if (Prev == nullptr)
            Prev = Node;
        Node->Prev = Prev;
        Sentinel->Prev = Node;
        Node->Prev->Next = Node;
        ::new (static_cast<void*>(&Node->View)) Observer(Temp);
        ++PlayerListSize;
    }

    // .text:06d07ea6  MOV EAX,[EDI+0xc] / MOV EAX,[EAX+4] / ADD EAX,0x8 --
    // retail recovers the live element as sentinel->prev->View and keeps it in
    // local_1d0 ([ESP+0x1c]) for every pass below.
    Observer* const View = &Sentinel->Prev->View;

    // .text:06d07eb3  CALL COMMON.DLL::SinglePlayer / TEST AL,AL /
    // .text:06d07ebb JNZ LAB_06d082fa -- the multiplayer-only head is SKIPPED in
    // single player.
    bool const bSingle = SinglePlayer();

    if (!bSingle) {
        // .text:06d07ec1 .. .text:06d07ecf -- the system id comes off
        // SystemInfo's own vtable slot 0, and the two arguments are pushed
        // id-then-session, so the session is the FIRST parameter:
        //   .text:06d07ec8  50  PUSH EAX   ; the system id  -> 2nd argument
        //   .text:06d07ec9  55  PUSH EBP   ; the session id -> 1st argument
        Players.SendSystemID(SessionId,
                             (SystemInfo != nullptr) ? SystemInfo->NicknameId : 0u);

        // .text:06d07ed4 .. .text:06d07ef4 -- latch Players[SessionId] + 0x318
        // (`IMUL EAX,EAX,0x418` / `LEA EAX,[EAX + ECX + -0x418]` /
        // `MOV EAX,[EAX + 0x318]`) into [ESP+0x30]. PlayerDB::operator[] is the
        // same unchecked `m_PlayerArray[Id-1]`. Read back only by the
        // AttitudeTowards call below.
        int const PlayerRep = Players[SessionId].rep_vibe;

        // .text:06d07eef .. .text:06d07eff -- count the SOLAR block at +0x5c
        // (FUN_06cf5130 with mask -1), and skip the walk when it is empty.
        FSystemListNode* const SolarHead = Solars.Head;                // [EDI + 0x60]
        if (Solars.count_type(0xffffffffu) != 0) {
            // .text:06d07f05 .. .text:06d07f28 -- node = [this+0x60], then
            // node = node->Next, until null.
            for (FSystemListNode* SolarNode = SolarHead; SolarNode != nullptr;
                 SolarNode = SolarNode->Next) {
                // Retail dereferences node->Data unguarded (.text:06d07f2e).
                GameObject* const Obj = SolarNode->Data;

                // .text:06d07f2e .. .text:06d07f54 -- CALL [EAX + 0x150]
                // (IObjInspect::cobject), then `AND 0x303 / CMP 0x303`. Anything
                // that is not a solar keeps no CObject (`XOR EDI,EDI`).
                CObject* Raw = const_cast<CObject*>(Obj->cobject());
                CSolar* Solar = nullptr;
                if (Raw != nullptr &&
                    (static_cast<unsigned int>(Raw->ObjectClass) & 0x303u) == 0x303u)
                    Solar = static_cast<CSolar*>(Raw);

                if (Solar != nullptr && Solar->bDynamic) {
                    // .text:06d08226 CMP byte [EDI + 0x1a9],BL / JZ -- a DYNAMIC
                    // solar gets CREATESOLAR and skips the damage list.
                    // .text:06d08232 .. .text:06d08257 -- FUN_06cf63f0 base ctor,
                    // +0xac = 0, +0xad = 0, +0xb0 = -1, vtable 06cf0260.
                    CSolarCreateInfo Info;
                    // .text:06d08276  CALL FUN_06d07950(&Info, cobj)
                    CSolarCreateInfo_Fill(&Info, Solar);
                    // .text:06d0827b .. .text:06d08295 -- FUN_06cef450 on
                    // node->Data, byte into Info+0xa8.
                    Info.bMarkedBy = Obj->is_marked_by(SessionId) ? 1u : 0u;
                    // .text:06d0829f  CALL [EAX + 0xb8] -- IClient::SendServerCreateSolar,
                    // FLPACKET_SERVER_CREATESOLAR, (session, &Info).
                    ::g_pClientInterface->SendServerCreateSolar(SessionId, Info);
                    // .text:06d082a5 .. .text:06d082ef -- Info's destructor.
                } else {
                    // .text:06d07f56 .. .text:06d07fde -- everything else,
                    // including a static solar: a DamageList on the stack
                    // (+0x0c = 0, +0x10 = 0x19 at .text:06d07f8a, +0x14 = 0,
                    // +0x18 = 0), filled by node->Data's get_damage_list
                    // (CALL [EDX + 0x1a8], PUSH 0x3f800000 = 1.0f).
                    DamageList Damage;
                    Damage.set_destroyed(false);
                    // The same unnamed cause 0x19 the observer writes
                    // (.text:06cf61d2); see the note there.
                    Damage.set_cause(static_cast<DamageCause>(0x19));
                    Damage.set_inflictor_id(0u);
                    Damage.set_inflictor_owner_player(0u);
                    Obj->get_damage_list(Damage, 1.0f);
                    // .text:06d07fa6  CMP [ESP + 0x68],EBX -- the list's size.
                    if (!Damage.Entries.empty()) {
                        // .text:06d07fbe CALL [EDX + 0x20] (get_id) /
                        // .text:06d07fc9 CALL [ESI + 0xfc] -- IClient::SendServerDamageObject,
                        // DAMAGEOBJECT, (session, id, &list).
                        ::g_pClientInterface->SendServerDamageObject(SessionId, Obj->get_id(), Damage);
                    }
                }

                // .text:06d07fe4  CMP EDI,EBX / JZ -- the rest only for a solar,
                // dynamic or not.
                if (Solar == nullptr)
                    continue;

                // .text:06d07fec .. .text:06d08083 -- SETREPUTATION for the
                // solar's vibe. Two FmtStr default ctors (FUN_06d55bc0),
                // +0x10 = 0, byte +0x14 = 0, word +0x48 = 0, +0x00 = cobj+0x104.
                // +0x04/+0x08 come back from the two Reputation calls; +0x0c is
                // never written (stack residue in retail, zeroed here).
                FServerSetReputationInfo Rep;
                Rep.Field_0x0c = 0u;
                Rep.Relations = nullptr;
                Rep.RelationCount = 0;
                Rep.DisplayName[0] = L'\0';
                Rep.VibeHandle = static_cast<int>(Solar->VibeId);
                // .text:06d08052  AttitudeTowards(&Rep+0, &PlayerRep, &Rep+4)
                ::Reputation::Vibe::AttitudeTowards(Rep.VibeHandle, PlayerRep,
                                                    Rep.Attitude);
                // .text:06d08069  GetAffiliation(&Rep+0, &Rep+8, false)
                ::Reputation::Vibe::GetAffiliation(Rep.VibeHandle, Rep.Field_0x08,
                                                   false);
                // .text:06d08083  CALL [EDX + 0x108] -- IClient::SendServerSetReputation.
                ::g_pClientInterface->SendServerSetReputation(SessionId, Rep);

                // .text:06d08089 .. .text:06d0813f -- CEquipTraverser(0x80) over
                // cobj+0xe4. No CEGun::cast: the traverser's mask already
                // selects guns, and GetTarget is called on the element as is.
                CEquipTraverser Walk(0x80);
                for (CEquip* Equip = Solar->EquipManager.Traverse(Walk);
                     Equip != nullptr;
                     Equip = Solar->EquipManager.Traverse(Walk)) {
                    // .text:06d080b7  CALL [EAX + 0xc] -- IsFunctioning.
                    if (!Equip->IsFunctioning())
                        continue;
                    // .text:06d080c4 .. .text:06d080dd -- target = 0,
                    // sub = SUBOBJ_ID_NONE, CEGun::GetTarget(target, sub).
                    CSimple* Target = nullptr;
                    unsigned short TargetSub = SUBOBJ_ID_NONE;
                    static_cast<CEGun*>(Equip)->GetTarget(Target, TargetSub);
                    if (Target == nullptr)
                        continue;
                    // .text:06d080eb .. .text:06d08127 -- XSetTarget at S+0x50:
                    // +0x00 = cobj+0xb0, word +0x04 = equip+0x08,
                    // +0x08 = target+0xb0, word +0x0c = sub; the two pad words
                    // are never written. IClient::SendCommonSetTarget (CALL [EDX + 0x10]).
                    XSetTarget Msg;
                    Msg.ObjectId = Solar->Id;
                    Msg.SubObjId = Equip->Id;
                    Msg.Pad_0x06 = 0u;
                    Msg.TargetObjectId = Target->Id;
                    Msg.TargetSubObjId = TargetSub;
                    Msg.Pad_0x0e = 0u;
                    ::g_pClientInterface->SendCommonSetTarget(SessionId, Msg);
                }

                // .text:06d0814c .. .text:06d081d1 -- CALL [EAX + 0xb8]
                // (get_activate_state) into a fresh vector; on true, one
                // ACTIVATEOBJECT {cobj+0xb0, 1, index} per set entry through
                // IClient::SendServerActivateObject (CALL [EAX + 0xe0]).
                std::vector<bool> States;
                if (Solar->get_activate_state(States)) {
                    for (unsigned int Index = 0;
                         Index < static_cast<unsigned int>(States.size()); ++Index) {
                        if (!States[Index])
                            continue;
                        FPayloadServerActivateObject Activate;
                        Activate.ObjectId = Solar->Id;
                        Activate.bActivate = 1;
                        Activate.Pad_0x05[0] = 0;
                        Activate.Pad_0x05[1] = 0;
                        Activate.Pad_0x05[2] = 0;
                        Activate.Field_0x08 = Index;
                        ::g_pClientInterface->SendServerActivateObject(SessionId, Activate);
                    }
                }
                // .text:06d081da .. .text:06d0821f -- the vector and both FmtStr
                // are destroyed; JMP back to the next node.
            }
        }
    }

    // .text:06d082fa .. .text:06d08302 -- the SHIP block at +0x34/+0x38. The
    // branch is `TEST AL,AL / JZ LAB_06d08550`, so the JZ (the tracked-list-only
    // pass) is the NOT-single-player one.
    {
        FSystemListNode* Node2 = Ships.Head;
        while (Node2 != nullptr) {
            FSystemListNode* const Next = Node2->Next;
            GameObject* const Obj = Node2->Data;
            if (bSingle) {
                // .text:06d08308 .. .text:06d08474 -- SP eager CREATESHIP.
                CObject* const Raw = Obj->cobj();
                CShip* const Ship =
                    (Raw != nullptr &&
                     (static_cast<unsigned>(Raw->ObjectClass) & 0x503u) == 0x503u)
                        ? static_cast<CShip*>(Raw)
                        : nullptr;
                if (Ship != nullptr) {
                    CShipCreateInfo Info;
                    CShipCreateInfo_Fill(&Info, Ship);
                    // .text:06d0844a CALL FUN_06cef450 / store at Info+0xa8
                    Info.bMarkedBy =
                        Obj->is_marked_by(SessionId) ? 1u : 0u;
                    SystemPlayerView_CreateShipForPlayer(View, &Info, Obj);
                }
            } else if (!Obj->is_destroyed()) {
                // .text:06d08570  CALL [EDX + 0x160]   ; is_destroyed
                // .text:06d08578  JNZ                   ; skip when destroyed
                // .text:06d0857a  MOV EAX,[ESI + 0x8]  ; node->Data
                // .text:06d0857d  MOV ECX,[ESP + 0x1c] ; the live view
                // .text:06d08582  CALL FUN_06cf7140
                SystemPlayerView_AddTrackedObject(View, Obj);
            }
            Node2 = Next;
        }
    }

    // .text:06d084c4  PUSH -1 / .text:06d084c6 LEA ECX,[EDI + 0x48] /
    // .text:06d084c9 CALL FUN_06cf5130 -- the LOOT block at +0x48/+0x4c, counted
    // the same way, and BOTH branches return immediately when the count is zero
    // (.text:06d084e6 and .text:06d0866e).
    {
        FSystemListNode* const LootHead = Loot.Head;
        unsigned int const LootCount = Loot.count_type(0xffffffffu);
        if (LootCount == 0)
            return;

        if (bSingle) {
            // .text:06d084ec .. .text:06d0866a -- SP eager CREATELOOT.
            // Retail: operator_new(count*0x48), zero each record, seed
            // +0x44=1 / +0x45=0 / +0x46=0, fill+push every node, then
            // SystemPlayerView_CreateLootForPlayer. OwnerId stays 0 here
            // (observer's per-object path is the one that copies CLoot+0xe4).
            std::vector<GameObject*> LootObjs;
            LootObjs.reserve(LootCount);
            FServerLootDesc* const Descs = static_cast<FServerLootDesc*>(
                ::operator new(sizeof(FServerLootDesc) * LootCount));
            if (Descs == nullptr)
                return;
            unsigned int Filled = 0u;
            for (FSystemListNode* LootNode = LootHead; LootNode != nullptr;
                 LootNode = LootNode->Next) {
                if (Filled >= LootCount)
                    break;
                GameObject* const Obj = LootNode->Data;
                CObject* const Raw = Obj->cobj();
                CLoot* const Loot =
                    (Raw != nullptr &&
                     (static_cast<unsigned>(Raw->ObjectClass) & 0x803u) == 0x803u)
                        ? static_cast<CLoot*>(Raw)
                        : nullptr;
                FServerLootDesc* const Desc = &Descs[Filled];
                std::memset(Desc, 0, sizeof(*Desc));
                Desc->Field_0x44 = 1;   // .text:06d0853c (puVar10+0xf with base+8)
                ServerLootDesc_Fill(Desc, Loot);
                Desc->bMarkedBy = Obj->is_marked_by(SessionId) ? 1u : 0u;
                LootObjs.push_back(Obj);
                ++Filled;
            }

            FServerCreateLootRequest Req;
            Req.OwnerId = 0u;           // .text:06d084f8  uStack_1b0 = 0
            Req.Kind = 5u;              // .text:06d08507
            Req.bMarkedBy = 0;
            Req.bTemporary = 0;
            Req.Pad0a[0] = 0;
            Req.Pad0a[1] = 0;
            Req.Items = Descs;
            Req.Count = static_cast<unsigned char>(Filled);
            Req.Pad11[0] = 0;
            Req.Pad11[1] = 0;
            Req.Pad11[2] = 0;

            if (Filled > 0u) {
                SystemPlayerView_CreateLootForPlayer(
                    View, &Req, &LootObjs[0], &LootObjs[0] + LootObjs.size());
            }
            ::operator delete(Descs);
        } else {
            FSystemListNode* Node2 = LootHead;
            while (Node2 != nullptr) {
                FSystemListNode* const Next = Node2->Next;
                GameObject* const Obj = Node2->Data;
                if (!Obj->is_destroyed()) {
                    // .text:06d086a5  CALL [EDX + 0x160]
                    // .text:06d086b2  MOV ECX,[ESP + 0x1c]
                    // .text:06d086b7  CALL FUN_06cf7140
                    SystemPlayerView_AddTrackedObject(View, Obj);
                }
                Node2 = Next;
            }
        }
    }
}

// StarSystem::create_med_asteroid / create_med_mine -- retail FUN_06d09bb0 /
// FUN_06d09c80. See the declaration in server_sysdb.h for the map.
//   .text:06d09bc8/06d09c98  operator new(0x68 / 0x88); .text:06d09be2 /
//                            06d09cb5 JZ -> object 0 when the allocation fails
//   .text:06d09bf1 .. 06d09c4b  ListC0.append(object), inlined
//   .text:06d09c56/06d09d29  CALL [EAX + 0x168]  find(*ObjId)
//   .text:06d09c63/06d09d36  PUSH 0 / PUSH ESI / CALL [EDX + 0x170]
//                            enter_system(this, nullptr)
// No test of find()'s answer: the object is entered either way.
// ===========================================================================
SrvAsteroid::MedAsteroid* StarSystem::create_med_asteroid(unsigned int const& ObjId)
{
    SrvAsteroid::MedAsteroid* const Obj = new SrvAsteroid::MedAsteroid();
    ListC0.append(Obj);
    Obj->find(ObjId);
    Obj->enter_system(this, nullptr);
    return Obj;
}

SrvAsteroid::MedMine* StarSystem::create_med_mine(unsigned int const& ObjId)
{
    SrvAsteroid::MedMine* const Obj = new SrvAsteroid::MedMine();
    ListC0.append(Obj);
    Obj->find(ObjId);
    Obj->enter_system(this, nullptr);
    return Obj;
}

// ===========================================================================
// StarSystem::send_mine_explosion -- retail FUN_06d09270 (.text:06d09270).
//   .text:06d0927b .. 06d09297  the four dwords of *Rec copied to a local
//   .text:06d09290 / 06d0929b   sentinel = [this+0xc], node = sentinel->Next;
//                               nothing to do when they are equal
//   .text:06d092a1 .. 06d092a9  FUN_06cf6df0(&node->View, &local)
//   .text:06d092ae / 06d092b0   node = node->Next, compared against a fresh
//                               read of [this+0xc]
// ===========================================================================
void StarSystem::send_mine_explosion(FMineExplosion const& Rec)
{
    FMineExplosion Local = Rec;
    SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
    while (Node != static_cast<SystemPlayerNode*>(PlayerListHead)) {
        SystemPlayerView_SendMineExplosion(&Node->View, &Local);
        Node = Node->Next;
    }
}

// ===========================================================================
// StarSystem::explode -- retail FUN_06d0b260 (.text:06d0b260 .. 06d0b7dd).
// See the declaration in server_sysdb.h for the outline; the trail below is
// per statement. Stack base S = ESP after the four register pushes; the record
// is [S+0x184] (EBX), `this` is kept at [S+0x38].
// ===========================================================================
void StarSystem::explode(ExplosionDamageEvent const& Blast)
{
    // .text:06d0b283 .. 06d0b29c  EBP = [S+0x20] = find_object(Blast +4)
    GameObject* const Inflictor = find_object(Blast.InflictorId);

    // .text:06d0b295 .. 06d0b2b8  ISystem slot 0 (the system id), then
    // FindSphereCollisions(id, &Blast +0xc, Explosion->Radius (+4),
    // parts = S+0x84, 0x3c); the answer is EDI.
    CObject* Parts[0x3c];
    int const Count = PhySys::FindSphereCollisions(SystemInfo->NicknameId, Blast.Position,
                                                   Blast.Explosion->Radius, Parts, 0x3c);

    // .text:06d0b2c2 .. 06d0b2d5  the root vector at S+0x24 {alloc, first,
    // last, end}, all zero.
    // .text:06d0b2d9 / 06d0b2e2  CMP EDI,0 / JBE -- reserve only for a non-zero
    // count; .text:06d0b2e6 JGE clamps a negative count to 0 (06d0b2e8).
    // .text:06d0b2ea .. 06d0b349  reserve(count): new block, copy, free the
    // old one, end = block + count, last = block + size.
    std::vector<CObject*> Roots;
    if (Count != 0)
        Roots.reserve(Count < 0 ? 0u : static_cast<size_t>(Count));

    // .text:06d0b34f JLE / .text:06d0b360 .. 06d0b3b5  for i in [0, count)
    for (int Index = 0; Index < Count; ++Index) {
        // .text:06d0b368  GetCollisionGroup(parts[i])
        char const* const Group = PhySys::GetCollisionGroup(Parts[Index]);
        // .text:06d0b371 .. 06d0b388  both non-null and stricmp == 0 -> skip
        if (Blast.SkipCollisionGroup != nullptr && Group != nullptr &&
            _stricmp(Blast.SkipCollisionGroup, Group) == 0)
            continue;
        // .text:06d0b392 / 06d0b3ad  push_back(GetRoot(parts[i])) (FUN_06d0f460
        // at last, i.e. an insert at end)
        Roots.push_back(GetRoot(Parts[Index]));
    }

    Archetype::Explosion const* const Explosion = Blast.Explosion;

    // .text:06d0b3c4 .. 06d0b3e5  HullDamage (+0xc) > 0.0f goes straight in;
    // otherwise EnergyDamage (+0x10) > 0.0f, else skip to the impulse pass.
    // (FCOMP against DAT_06d65188 = 0.0f, `TEST AH,0x41` -- JZ = greater.)
    if (Explosion->HullDamage > 0.0f || Explosion->EnergyDamage > 0.0f) {
        // .text:06d0b3eb .. 06d0b3f4  a std::vector<unsigned int> at S+0x44
        // (FUN_06d0f1f0) of the ids already damaged.
        std::vector<unsigned int> Damaged;

        // .text:06d0b3f9 .. 06d0b40f / 06d0b5b4 .. 06d0b5c5 -- the walk; the
        // end is re-read every step.
        for (std::vector<CObject*>::iterator It = Roots.begin(); It != Roots.end(); ++It) {
            CObject* const Root = *It;
            // .text:06d0b420 .. 06d0b433  null or (class & 3) != 3 -> next
            if (Root == nullptr || (static_cast<unsigned int>(Root->ObjectClass) & 3u) != 3u)
                continue;
            // .text:06d0b44a / 06d0b452  CSimple id (+0xb0) into [S+0x20]
            unsigned int const Id = static_cast<CSimple*>(Root)->Id;
            // .text:06d0b450 / 06d0b456  the exploding object itself -> next
            if (Id == Blast.ExplodingObjId)
                continue;
            // .text:06d0b45c .. 06d0b473  linear search of Damaged; found -> next
            if (std::find(Damaged.begin(), Damaged.end(), Id) != Damaged.end())
                continue;
            // .text:06d0b479 .. 06d0b4b9  FUN_06d0f210 (find) misses, so
            // FUN_06d0f460 push_back and find again; the {iterator, bool} pair
            // (FUN_06d0f600) is not read afterwards.
            Damaged.push_back(Id);

            // .text:06d0b4d3 .. 06d0b4e5  find_object(id) on [S+0x38]; null -> next
            GameObject* const Obj = find_object(Id);
            if (Obj == nullptr)
                continue;
            // .text:06d0b4eb .. 06d0b4f7  vt +0x160 is_destroyed -> next
            if (Obj->is_destroyed())
                continue;
            // .text:06d0b4fd .. 06d0b525  !DAT_06d88a80 && inflictor &&
            // inflictor->is_player() (vt +0xbc) && Obj->is_player() -> next
            if (GbAllowPvp == 0 && Inflictor != nullptr && Inflictor->is_player() &&
                Obj->is_player())
                continue;

            // .text:06d0b52b .. 06d0b55b  the DamageList at S+0x54, built
            // inline: std::list<DamageEntry> ctor (Common import), bDestroyed
            // (+0xc) = 0, InflictorOwner (+0x18) = 0, Cause (+0x10) = Blast +8,
            // InflictorId (+0x14) = Blast +4.
            DamageList Damage;
            Damage.set_cause(Blast.Cause);
            Damage.set_inflictor_id(Blast.InflictorId);
            // .text:06d0b55f .. 06d0b57b  with an inflictor: its CSimple
            // OwnerPlayer (+0xb4) under the (class & 3) == 3 narrowing, read
            // through the nulled pointer exactly as retail does.
            if (Inflictor != nullptr)
                Damage.set_inflictor_owner_player(Inflictor->csimple()->OwnerPlayer);

            // .text:06d0b57f .. 06d0b589  vt +0x184 explosion_hit(Blast, list)
            Obj->explosion_hit(Blast, Damage);
            // .text:06d0b58f .. 06d0b598  vt +0x188 apply_damage_list(list)
            Obj->apply_damage_list(Damage);
            // .text:06d0b5aa  the list's destructor (Common import)
        }
        // .text:06d0b5cb .. 06d0b5d7  FUN_06d0b7e0 frees Damaged
    }

    // .text:06d0b5de .. 06d0b5ef  Impulse (+8) compared with 0.0f,
    // `TEST AH,0x44 / JNP` -- skipped only when it is exactly equal.
    if (!(Explosion->Impulse == 0.0f)) {
        // .text:06d0b5f5 .. 06d0b609  a second std::vector<unsigned int> at
        // S+0x54, all zero.
        std::vector<unsigned int> Pushed;

        // .text:06d0b60d .. 06d0b61d / 06d0b7a0 .. 06d0b7a9  the walk (EBP)
        for (std::vector<CObject*>::iterator It = Roots.begin(); It != Roots.end(); ++It) {
            CObject* const Root = *It;
            // .text:06d0b623 .. 06d0b637  null or (class & 3) != 3 -> next
            if (Root == nullptr || (static_cast<unsigned int>(Root->ObjectClass) & 3u) != 3u)
                continue;
            CSimple* const Simple = static_cast<CSimple*>(Root);
            // .text:06d0b63d .. 06d0b645  a player's object (+0xb4 != 0) -> next
            if (Simple->OwnerPlayer != 0)
                continue;
            // .text:06d0b64b .. 06d0b6a9  insert-unique of the id into Pushed.
            // Unlike the damage pass the answer is NOT tested: the found arm
            // (.text:06d0b696, pair {it, false}) falls into the same code as
            // the inserted arm (.text:06d0b677, pair {it, true}), so an object
            // whose several parts were in range is pushed once per part.
            if (std::find(Pushed.begin(), Pushed.end(), Simple->Id) == Pushed.end())
                Pushed.push_back(Simple->Id);

            // .text:06d0b6ae .. 06d0b6df  CObject vt +0x68 get_center_of_mass,
            // minus Blast.Position (+0xc), into a Vector at S+0x14
            // (FUN_06ce7c10 is Vector's three-float constructor).
            Vector const Center = Root->get_center_of_mass();
            Vector Dir;
            Dir.x = Center.x - Blast.Position.x;
            Dir.y = Center.y - Blast.Position.y;
            Dir.z = Center.z - Blast.Position.z;

            // .text:06d0b6e4 .. 06d0b6fe  y*y + z*z, then + x*x
            float const LengthSq = (Dir.y * Dir.y + Dir.z * Dir.z) + Dir.x * Dir.x;
            // .text:06d0b700 .. 06d0b70b  FCOM 0.0f / TEST AH,0x44 / JNP
            if (!(LengthSq == 0.0f)) {
                // .text:06d0b70d .. 06d0b733  FSQRT, FDIVR 1.0f (DAT_06d65520),
                // each component scaled
                float const Inverse = 1.0f / sqrtf(LengthSq);
                Dir.x = Dir.x * Inverse;
                Dir.y = Dir.y * Inverse;
                Dir.z = Dir.z * Inverse;
            } else {
                // .text:06d0b737 .. 06d0b757  COMMON.DLL::random_on_unit_sphere
                Dir = random_on_unit_sphere();
            }

            // .text:06d0b75b .. 06d0b784  scaled by Explosion->Impulse (+8)
            float const Impulse = Blast.Explosion->Impulse;
            Dir.x = Dir.x * Impulse;
            Dir.y = Dir.y * Impulse;
            Dir.z = Dir.z * Impulse;

            // .text:06d0b761 / 06d0b786  only with a physics body (+0x54)
            if (Root->BodyPtr != nullptr)
                PhySys::AddImpulseAtPoint(Root, Dir, Blast.Position);   // .text:06d0b78f
        }
        // .text:06d0b7af  Pushed freed
    }
    // .text:06d0b7b8 .. 06d0b7bd  Roots freed
}

// ===========================================================================
// FUN_06d0d500 (.text:06d0d500, thiscall, RET 4) -- StarSystem::munition_collision.
// Declared (with the record's layout) in server_sysdb.h. Traced per statement;
// S = ESP after the four register pushes (EBX, EBP, ESI, EDI).
// ===========================================================================
void StarSystem::munition_collision(SSPMunitionCollisionInfo const& Info)
{
    // .text:06d0d51b .. 06d0d538  EDI = find_object(+0x08) (the object hit),
    // EBP = find_object(+0x04) (the shooter).
    GameObject* const Target = find_object(Info.ObjectId);
    GameObject* const Shooter = find_object(Info.Field_0x04);

    // .text:06d0d53a .. 06d0d56a  DAT_06d88a80 clear and a shooter that is a
    // player (vt +0xbc): a missing target or a player target ends it here.
    if (GbAllowPvp == 0 && Shooter != nullptr && Shooter->is_player())
    {
        if (Target == nullptr)
            return;
        if (Target->is_player())
            return;
    }

    // .text:06d0d570 .. 06d0d584  no target, or a destroyed one (vt +0x160).
    if (Target == nullptr)
        return;
    if (Target->is_destroyed())
        return;

    // .text:06d0d58a .. 06d0d5cc  the FMunitionImpact at S+0x10: shooter id
    // (+0x04), Archetype::GetEquipment(+0x00), sub-object (+0x0c, after a first
    // store of SUBOBJ_ID_NONE), hit position (+0x10..+0x18).
    FMunitionImpact Impact;
    Impact.ShooterId = 0u;
    Impact.Munition = nullptr;
    Impact.SubObjId = SUBOBJ_ID_NONE;
    Archetype::Equipment* const MunitionArch = Archetype::GetEquipment(Info.Field_0x00);
    Impact.Munition = static_cast<Archetype::Munition const*>(MunitionArch);
    Impact.ShooterId = Info.Field_0x04;
    Impact.SubObjId = Info.Field_0x0c;
    std::memcpy(&Impact.HitPos, &Info.Field_0x10, sizeof(Vector));

    // .text:06d0d5d0 .. 06d0d600  the DamageList at S+0x28, built inline
    // (std::list<DamageEntry> head from the Common import, count 0, bDestroyed
    // 0, cause/inflictor/owner 0).
    DamageList Damage;

    // .text:06d0d604 .. 06d0d62e  the cause from the munition's ObjectType
    // (Root +0x10): 0x2000000 -> 2, 0x1000000 -> 3, anything else -> 1.
    unsigned int const ObjectType = MunitionArch->ObjectType;
    if (ObjectType == 0x2000000u)
        Damage.set_cause(static_cast<DamageCause>(2));
    else
        Damage.set_cause(static_cast<DamageCause>((ObjectType == 0x1000000u) ? 3 : 1));

    // .text:06d0d632 .. 06d0d637  inflictor = the shooter's id from the record.
    Damage.set_inflictor_id(Info.Field_0x04);

    // .text:06d0d63b .. 06d0d657  with a shooter: its CSimple OwnerPlayer
    // (+0xb4) under the (class & 3) == 3 narrowing, not null-tested.
    if (Shooter != nullptr)
        Damage.set_inflictor_owner_player(Shooter->csimple()->OwnerPlayer);

    // .text:06d0d65b .. 06d0d669  vt +0x180 munition_hit(Impact, list).
    Target->munition_hit(Impact, Damage);
    // .text:06d0d66f .. 06d0d678  vt +0x188 apply_damage_list(list).
    Target->apply_damage_list(Damage);
    // .text:06d0d67e .. 06d0d6c3  the list's nodes and head are freed.
}

// ===========================================================================
// FUN_06d08e80 (.text:06d08e80, thiscall, RET 0) -- FServerLootDesc's
// constructor, run on every element of create_loot's descriptor block:
//   .text:06d08e84 .. 06d08e95  +0x00 .. +0x18 = 0 (seven dwords)
//   .text:06d08e98  MOV byte [EAX + 0x44],1
//   .text:06d08e9c  MOV byte [EAX + 0x45],CL (0)
//   .text:06d08e9f  MOV byte [EAX + 0x46],CL (0)
// Position, orientation, velocity and +0x47 are left alone.
// ===========================================================================
static void ServerLootDesc_Init(FServerLootDesc* Desc)
{
    Desc->LootId = 0u;
    Desc->ContentsNickname = 0u;
    Desc->ContentsHitPts = 0.0f;
    Desc->Units = 0u;
    Desc->ContainerNickname = 0u;
    Desc->HitPts = 0.0f;
    Desc->NameOverride = 0u;
    Desc->Field_0x44 = 1;
    Desc->bTemporary = 0;
    Desc->bMarkedBy = 0;
}

// ===========================================================================
// StarSystem::create_loot -- retail FUN_06d08a40 (.text:06d08a40 .. 06d08e1c,
// thiscall, RET 0x10). Stack base S = ESP after the four register pushes;
// `this` is kept at [S+0x14], SourceObjId at [S+0x34], Mode at [S+0x38].
// ===========================================================================
void StarSystem::create_loot(unsigned int SourceObjId, int Mode, FLootRecord const* Records, int Count)
{
    // .text:06d08a6c / 06d08a7b  LEA EDX,[EBX+EBX*8] / SHL EDX,3 -- Count * 0x48
    // .text:06d08a9c             operator new, then FUN_06d08e80 on each
    //                            element (.text:06d08ac3 .. 06d08ad6): +0x00..+0x18
    //                            zeroed, byte +0x44 = 1, +0x45 = +0x46 = 0; the
    //                            position/orientation/velocity are left as they are.
    FServerLootDesc* const Items =
        static_cast<FServerLootDesc*>(operator new(static_cast<size_t>(Count) * sizeof(FServerLootDesc)));
    for (int Index = 0; Index < Count; ++Index)
        ServerLootDesc_Init(&Items[Index]);

    // .text:06d08a81 .. 06d08ae2 and .text:06d08aea -- the CREATELOOT request
    // every player view gets at the end: {SourceObjId, Mode, byte 0, byte 0,
    // Items, (byte)Count}. Server-side the layout is FServerCreateLootRequest.
    FServerCreateLootRequest Request = {};
    Request.OwnerId = SourceObjId;                 // .text:06d08a94  [S+0x34]
    Request.Kind = static_cast<unsigned int>(Mode); // .text:06d08a98 [S+0x38]
    Request.bMarkedBy = 0;                         // .text:06d08a81  [S+0x3c]
    Request.bTemporary = 0;                        // .text:06d08a86  [S+0x3d]
    Request.Items = Items;                         // .text:06d08ade  [S+0x40]
    Request.Count = static_cast<unsigned char>(Count); // .text:06d08aea MOV [S+0x44],BL

    // .text:06d08aee .. 06d08afa -- an empty std::vector<GameObject*> at S+0x18;
    // .text:06d08b11 .. 06d08b76 reserve(Count) (operator new(Count*4), the
    // uninitialised copy FUN_06d23e40, the old block freed).
    std::vector<GameObject*> Spawned;

    // .text:06d08afe CMP EBX,EDI / JBE -- an unsigned test: nothing to spawn
    // skips straight to the player walk (which still runs).
    if (static_cast<unsigned int>(Count) != 0u) {
        // .text:06d08b11 MOV EAX,EBX / JGE / XOR EAX,EAX -- the reserve size is
        // Count, or 0 for a negative one.
        Spawned.reserve(static_cast<size_t>(Count >= 0 ? Count : 0));

        FLootRecord const* Record = Records;       // EBP, .text:06d08b80
        FServerLootDesc* Item = Items;             // [S+0x30]
        for (unsigned int Left = static_cast<unsigned int>(Count); Left != 0u; --Left) {
            // .text:06d08b90 operator new(0x70), FUN_06cee360 (GameObject ctor),
            // +0x60/+0x64/+0x68 zeroed, the three Loot vtables (.text:06d08bbc ..
            // 06d08bc9) -- Loot::Loot. No null test on the result reaches the
            // create call below (.text:06d08bd4 XOR EBX,EBX then [EBX]).
            ::Loot* const Obj = new ::Loot();

            // .text:06d08bd6 .. 06d08be6  CALL [EAX + 0x164] (create) with
            // Record->Arch (+0x04); the result is not tested.
            Obj->create(const_cast<Archetype::Equipment*>(Record->Arch));

            // .text:06d08bec .. 06d08c0f  LEA EAX,[EBX+0xc] / [EAX+4]: the
            // wrapper's CBase::Object narrowed with (ObjectClass & 0x803) ==
            // 0x803, kept at [S+0x10] (EDI from .text:06d08c5b on).
            CLoot* const LootObject = Obj->cloot();

            // .text:06d08c13 FUN_06d08e20 -- CLoot::CreateParms defaults: ids 0,
            // HitPts -1.0f, identity orientation, +0x40 .. +0x58 zero. The
            // member initialisers give the CSimple part; Pad58 is stored here.
            CLoot::CreateParms Parms;
            Parms.Pad58 = 0u;
            Parms.Id = Record->Id;                             // .text:06d08c26  +0x00
            Parms.ObjDb = this;                                // .text:06d08c2a  +0x04 = [S+0x14]
            Parms.HitPts = Record->HitPts;                     // .text:06d08c2e  +0x0c
            Parms.Pos = Record->Position;                      // .text:06d08c32 .. 06d08c45  +0x10
            Parms.OwnerPlayer = 0u;                            // .text:06d08c49  +0x08 (EDI == 0)
            Parms.Orient = Record->Orientation;                // .text:06d08c4d .. 06d08c59  REP MOVSD 9
            Parms.Velocity = Record->Velocity;                 // .text:06d08c5f .. 06d08c78  +0x40
            Parms.AngularVelocity = Record->AngularVelocity;   // .text:06d08c7f .. 06d08cac  +0x4c
            Parms.OwnerId = SourceObjId;                       // .text:06d08c91 / 06d08c98  +0x5c
            Parms.Units = static_cast<unsigned int>(Record->Count); // .text:06d08c9f / 06d08cb6  +0x68
            Parms.NameId = static_cast<int>(Record->Field_0x1c);    // .text:06d08ca9 / 06d08cc0  +0x60
            Parms.ContentsArch = Record->ContentsArch;         // .text:06d08cb3 / 06d08cca  +0x64
            Parms.bTemporary = Record->bMission != 0;          // .text:06d08cbd / 06d08cd4  +0x71 <- rec +0x69
            Parms.ContentsHitPts = Record->ContentsHitPts;     // .text:06d08cc7 / 06d08cdb  +0x6c
            Parms.bCanAiTractor = Record->Field_0x68 != 0;     // .text:06d08cd1 / 06d08ce6  +0x70 <- rec +0x68

            // .text:06d08ced .. 06d08cf2  CALL [EDX + 0xa0] -- CLoot::init.
            LootObject->init(Parms);

            // .text:06d08cf8 .. 06d08d07  a player to mark the loot for
            // (FUN_06cef3b0 == GameObject::mark_for_player, mark type 1).
            if ((Record->Field_0x18 & 0x3fffffffu) != 0u)
                Obj->mark_for_player(Record->Field_0x18, 1);

            // .text:06d08d0c .. 06d08d17  CALL [EDX + 0x170] (enter_system)
            // with ESI = [S+0x14] and a null CreateParms.
            Obj->enter_system(this, nullptr);

            // .text:06d08d1d .. 06d08d49 -- MetaList::append inlined on the
            // +0x48 block (FUN_06d11850 node, {Next 0, Prev Tail, Data}).
            Loot.append(Obj);

            // .text:06d08d44 .. 06d08d5b  FUN_06cecbd0 -- vector insert(end, 1,
            // Obj), i.e. push_back.
            Spawned.push_back(Obj);

            // .text:06d08d60 .. 06d08d92 -- controller message 0x2b through
            // FUN_06d43650 (broadcast), the record at S+0x48:
            //   +0x00 the loot id (Parms +0x00, .text:06d08d60 / 06d08d6e)
            //   +0x04 SourceObjId ([S+0xdc], .text:06d08d64 / 06d08d72)
            //   +0x08 Arch2Good(ContentsArch->Nickname) (.text:06d08d6b .. 06d08d8a)
            //   +0x0c Record->Count (+0x14, .text:06d08d80 / 06d08d8e)
            // The field meanings are what the stores say; retail names none.
            struct FControllerMsg2B {
                unsigned int LootId;
                unsigned int SourceObjId;
                unsigned int GoodId;
                int Count;
            } Msg;
            Msg.LootId = Parms.Id;
            Msg.SourceObjId = SourceObjId;
            Msg.GoodId = Arch2Good(Record->ContentsArch->get_id());
            Msg.Count = Record->Count;
            Server_ControllerDispatch(0, pub::Controller::EMessage::LootSpawned, &Msg);

            // .text:06d08d97 .. 06d08d9d  FUN_06d07b90(Item, CLoot).
            ServerLootDesc_Fill(Item, LootObject);

            ++Item;                                // .text:06d08da6 ADD ESI,0x48
            ++Record;                              // .text:06d08dac ADD EBP,0x6c
        }
    }

    // .text:06d08dbe .. 06d08deb -- every player view of this system:
    // FUN_06cf6c50(&Request, &Spawned) on node+8.
    SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
    while (Node != static_cast<SystemPlayerNode*>(PlayerListHead)) {
        GameObject* const* const First = Spawned.empty() ? nullptr : &Spawned[0];
        GameObject* const* const Last = First + Spawned.size();
        SystemPlayerView_CreateLootForPlayer(&Node->View, &Request, First, Last);
        Node = Node->Next;
    }

    // .text:06d08ded .. 06d08dfc -- the descriptor block, then the vector's.
    operator delete(Items);
}

// ===========================================================================
// StarSystem::create_mine -- retail FUN_06d08ef0 (.text:06d08ef0 .. 06d09264,
// thiscall, RET 4). One mine per ProjLaunchInfo, from SrvMineDropper::launch
// (.text:06cedafc). Stack base T = ESP after the four register pushes: this at
// [T+0x14], the CMine::CreateParms at [T+0x70], the CREATEMINE record at
// [T+0x18].
// ===========================================================================
void StarSystem::create_mine(FServerLaunchRecord const& Rec)
{
    // .text:06d08f13 .. 06d08f2b -- the id maker first; none -> FDUMP line 0x3aa.
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x3aa,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    // .text:06d08f30 .. 06d08f57 -- operator new(0xa0), Mine::Mine (FUN_06cf3330).
    // The null test on the allocation reaches nothing below it.
    ::Mine* const Obj = new ::Mine();

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();                                                   // .text:06d08f60 .. 06d08f70 vt +0xc
    Obj->create(Arch);                                              // .text:06d08f73 .. 06d08f7c vt +0x164
    Arch->Release();                                                // .text:06d08f82 .. 06d08f85

    // .text:06d08f8b .. 06d08fac -- the object under (& 0xf) == 0xf, at [T+0x10].
    CMine* const MineObject = Obj->cmine();

    // .text:06d08fac .. 06d08fb5 -- FUN_06d08e20 (the CSimple half's defaults:
    // ids 0, HitPts -1.0f, identity orientation, +0x40 .. +0x58 zero), then
    // +0x5c = 0; both are the member initialisers here.
    CMine::CreateParms Parms;

    // .text:06d08fbc .. 06d08fcf -- CFLIDMaker::AllocateObjectID inlined: the
    // +0x04 counter post-incremented, masked with 0x3fffffff.
    Parms.Id = Maker->AllocateObjectID();
    if ((Parms.Id & 0x3fffffffu) == 0u)                             // .text:06d08fc7 TEST / JNZ
    {
        // .text:06d08fd5 .. 06d08ff9 -- FDUMP line 0x3c2 and out; the mine
        // wrapper built above is neither entered nor freed.
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x3c2,
              "Failed to allocate valid ID for new mine.\n");
        return;
    }

    // .text:06d08ffe .. 06d090b0 -- the rest of the parms.
    Parms.ObjDb = this;                                             // .text:06d0901a / 06d09020  +0x04
    Parms.OwnerPlayer = 0u;                                         // .text:06d09035  +0x08
    Parms.Pos = Rec.Position;                                       // .text:06d09001 .. 06d0903b  +0x10 <- rec +0x10
    Parms.Orient = Rec.Orientation;                                 // .text:06d09044 .. 06d09055  REP MOVSD 9 <- rec +0x1c
    Parms.Velocity = Rec.Velocity;                                  // .text:06d09059 .. 06d09079  +0x40 <- rec +0x40

    // .text:06d08ffe .. 06d0908a -- random_inside_sphere(|Velocity|) into
    // +0x4c. The length is summed (x*x + y*y) + z*z on the x87 stack (double
    // precision control) and stored as a float argument (FSTP dword).
    double LengthSq = static_cast<double>(Rec.Velocity.x) * static_cast<double>(Rec.Velocity.x) +
                      static_cast<double>(Rec.Velocity.y) * static_cast<double>(Rec.Velocity.y);
    LengthSq += static_cast<double>(Rec.Velocity.z) * static_cast<double>(Rec.Velocity.z);
    float const Speed = static_cast<float>(std::sqrt(LengthSq));
    Parms.AngularVelocity = random_inside_sphere(Speed);            // .text:06d09090 .. 06d090a9  +0x4c
    Parms.OwnerId = Rec.OwnerId;                                    // .text:06d0909c / 06d090b0  +0x5c <- rec +0x04

    MineObject->init(Parms);                                        // .text:06d090b7 .. 06d090c5 vt +0xa0
    Obj->enter_system(this, nullptr);                               // .text:06d090cb .. 06d090d7 vt +0x170
    Obj->arm();                                                     // .text:06d090df FUN_06cf3560

    // .text:06d090e4 .. 06d0910c -- MetaList append on the +0x98 block,
    // inlined (FUN_06d11850).
    List98.append(Obj);

    // .text:06d0910f .. 06d0922c -- the CREATEMINE record. Retail's inlined
    // constructor zeroes +0x00/+0x04/+0x08/+0x10..+0x18/+0x44, puts
    // SUBOBJ_ID_NONE in +0x0c and runs FUN_06ce7c10 (0,0,0) on +0x2c and +0x38;
    // every one of those is then overwritten from the parms and the record
    // except +0x44.
    FServerCreateProjectileInfo Info;
    Info.ObjId = Parms.Id;                                          // .text:06d0915b / 06d09162
    Info.ArchetypeNickname = Arch->get_id();                        // .text:06d0915f / 06d09175 [arch +0x08]
    Info.OwnerId = Rec.OwnerId;                                     // .text:06d0920b / 06d09221
    Info.SubObjId = Rec.EquipId;                                    // .text:06d0921d / 06d0922c
    Info.Pad0e = 0;
    Info.Position = Parms.Pos;                                      // .text:06d09166 .. 06d09199
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);            // .text:06d09183 .. 06d091f1
    Info.Velocity = Parms.Velocity;                                 // .text:06d091cc .. 06d0920e
    Info.Field_0x38 = Parms.AngularVelocity;                        // .text:06d091ea .. 06d09228
    Info.Field_0x44 = 0u;                                           // .text:06d0916d

    // .text:06d09225 .. 06d0924a -- every player view: FUN_06cf6d60.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
        SystemPlayerView_CreateMineForPlayer(&Player->View, &Info, Obj);
}

// ===========================================================================
// StarSystem::create_counter_measure -- retail FUN_06d092c0 (.text:06d092c0 ..
// 06d09634, thiscall, RET 4). Caller SrvCounterMeasureDropper::launch
// (.text:06cedd5c). Instruction for instruction create_mine's body with these
// differences only: the id-maker FDUMP line is 0x401 and the id FDUMP line 0x419
// (the id text is still "... new mine.", .rdata:06d67fb0 at .text:06d093a2);
// operator new(0x60) with FUN_06ce6170; the (& 0x17) narrowing; FUN_06ce6300
// after enter_system; the +0xac block; FUN_06cf6bc0 per player view.
// ===========================================================================
void StarSystem::create_counter_measure(FServerLaunchRecord const& Rec)
{
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x401,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    ::CounterMeasure* const Obj = new ::CounterMeasure();          // FUN_06ce6170

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();                                                   // vt +0xc
    Obj->create(Arch);                                              // vt +0x164 (.text:06ce6200)
    Arch->Release();

    CCounterMeasure* const CmObject = Obj->ccountermeasure();       // (& 0x17) == 0x17

    CCounterMeasure::CreateParms Parms;                             // FUN_06d08e20 + OwnerId 0

    Parms.Id = Maker->AllocateObjectID();
    if ((Parms.Id & 0x3fffffffu) == 0u)
    {
        // The wrapper built above is neither entered nor freed.
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x419,
              "Failed to allocate valid ID for new mine.\n");
        return;
    }

    Parms.ObjDb = this;
    Parms.OwnerPlayer = 0u;
    Parms.Pos = Rec.Position;
    Parms.Orient = Rec.Orientation;
    Parms.Velocity = Rec.Velocity;

    // random_inside_sphere(|Velocity|): (x*x + y*y) + z*z on the x87 stack, the
    // root stored as a float argument.
    double LengthSq = static_cast<double>(Rec.Velocity.x) * static_cast<double>(Rec.Velocity.x) +
                      static_cast<double>(Rec.Velocity.y) * static_cast<double>(Rec.Velocity.y);
    LengthSq += static_cast<double>(Rec.Velocity.z) * static_cast<double>(Rec.Velocity.z);
    float const Speed = static_cast<float>(std::sqrt(LengthSq));
    Parms.AngularVelocity = random_inside_sphere(Speed);
    Parms.OwnerId = Rec.OwnerId;

    CmObject->init(Parms);                                          // vt +0xa0
    Obj->enter_system(this, nullptr);                               // vt +0x170
    Obj->divert_missiles();                                         // .text:06d094ac FUN_06ce6300

    // MetaList append on the +0xac block, inlined (FUN_06d11850).
    ListAC.append(Obj);

    // The CREATECOUNTER record, built exactly as create_mine's.
    FServerCreateProjectileInfo Info;
    Info.ObjId = Parms.Id;
    Info.ArchetypeNickname = Arch->get_id();
    Info.OwnerId = Rec.OwnerId;
    Info.SubObjId = Rec.EquipId;
    Info.Pad0e = 0;
    Info.Position = Parms.Pos;
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);
    Info.Velocity = Parms.Velocity;
    Info.Field_0x38 = Parms.AngularVelocity;
    Info.Field_0x44 = 0u;

    // Every player view: FUN_06cf6bc0.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
        SystemPlayerView_CreateCounterForPlayer(&Player->View, &Info, Obj);
}

// ===========================================================================
// StarSystem::create_guided -- retail FUN_06d09640 (.text:06d09640 .. 06d099d5,
// thiscall, RET 4). One missile per launch record, from SrvGun::launch_guided
// (.text:06cee1ae). Stack base T = ESP after the three register pushes: the id
// at [T+0x28], the target at [T+0x24], the CGuided::CreateParms at [T+0x78],
// the one-element vector at [T+0x30], the CREATEGUIDED record at [T+0x40].
// Unlike create_mine, the id is allocated and the target looked up BEFORE the
// wrapper is built.
// ===========================================================================
void StarSystem::create_guided(FServerLaunchRecord const& Rec)
{
    // .text:06d09664 .. 06d09694 -- FDUMP line 0x446.
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x446,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    // .text:06d09699 .. 06d096d5 -- AllocateObjectID inlined; 0 -> FDUMP line 0x44d.
    unsigned int const Id = Maker->AllocateObjectID();
    if ((Id & 0x3fffffffu) == 0u)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x44d,
              "Failed to allocate valid, dynamic ID for guided object.\n");
        return;
    }

    // .text:06d096db .. 06d096f2 -- the lock target by id (FUN_06d0c840).
    GameObject* const Target = find_object(Rec.TargetId);

    // .text:06d096ed .. 06d09716 -- operator new(0x80), Guided::Guided
    // (FUN_06cf3ce0). The null test on the allocation reaches nothing below it.
    ::Guided* const Obj = new ::Guided();

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();                                                   // .text:06d09716 .. 06d09726 vt +0xc
    Obj->create(Arch);                                              // .text:06d09729 .. 06d09732 vt +0x164
    Arch->Release();                                                // .text:06d09738 .. 06d0973b

    // .text:06d09741 .. 06d09767 -- the object under (& 0xa7) == 0xa7, at [T+0x14].
    CGuided* const GuidedObject = Obj->cguided();

    // .text:06d09767 .. 06d097fa -- FUN_06d08e20 defaults, then:
    CGuided::CreateParms Parms;
    Parms.Id = Id;                                                  // .text:06d09782  +0x00
    Parms.ObjDb = this;                                             // .text:06d09786  +0x04
    Parms.OwnerPlayer = 0u;                                         // .text:06d0979b  +0x08
    Parms.Pos = Rec.Position;                                       // .text:06d09770 .. 06d09794  +0x10
    Parms.Orient = Rec.Orientation;                                 // .text:06d097a6 .. 06d097b5  +0x1c (REP MOVSD 9)
    Parms.Velocity = Rec.Velocity;                                  // .text:06d097b7 .. 06d097de  +0x40
    Parms.OwnerId = Rec.OwnerId;                                    // .text:06d097c9 / 06d097d7  +0x5c
    Parms.Target = static_cast<IObjRW*>(Target);                    // .text:06d097e5 / 06d097fa  +0x60
    Parms.SubTarget = Rec.TargetSubId;                              // .text:06d097d3 / 06d097ed  +0x64

    GuidedObject->init(Parms);                                      // .text:06d09801 CGuided::init (import)

    // .text:06d09807 .. 06d09834 -- MetaList append on the +0x70 block, inlined.
    List70.append(Obj);

    Obj->enter_system(this, nullptr);                               // .text:06d09832 .. 06d0983d vt +0x170
    Obj->arm();                                                     // .text:06d09843 .. 06d09848 vt +0x1bc

    // .text:06d0984e .. 06d0987f -- an empty std::vector<GameObject*> and
    // insert(end, 1, Obj) (FUN_06cecbd0).
    std::vector<GameObject*> Spawned;
    Spawned.push_back(Obj);

    // .text:06d09884 .. 06d0991f -- the CREATEGUIDED record.
    FServerCreateGuidedInfo Info;
    Info.ObjId = Parms.Id;                                          // .text:06d09891 / 06d098ab  +0x00
    Info.Position = Parms.Pos;                                      // .text:06d0988a .. 06d098be  +0x04
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);            // .text:06d09899 .. 06d09904  +0x10
    Info.TargetId = Rec.TargetId;                                   // .text:06d09908 / 06d0991b  +0x20
    Info.TargetSubObjId = Rec.TargetSubId;                          // .text:06d09900 / 06d09913  +0x24
    Info.Pad26 = 0;
    Info.ArchetypeNickname = Arch->get_id();                        // .text:06d09884 .. 06d098a0  +0x28
    Info.Field_0x2c = 0u;                                           // .text:06d098b6  +0x2c
    Info.OwnerId = Rec.OwnerId;                                     // .text:06d098f9 / 06d0990b  +0x30
    Info.SubObjId = Rec.EquipId;                                    // .text:06d0990f / 06d0991f  +0x34
    Info.Pad36 = 0;

    // .text:06d09918 .. 06d09947 -- every player view: FUN_06cf6cf0.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        GameObject* const* const First = &Spawned[0];
        SystemPlayerView_CreateGuidedForPlayer(&Player->View, &Info, First, First + Spawned.size());
    }

    // .text:06d09949 .. 06d09984 -- a live target that is a CEqObj ((& 0x103),
    // null tests included) with a behavior interface ...
    if (Target != nullptr && !Target->is_destroyed())
    {
        CObject* const Raw = Target->Object;
        if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x103u) == 0x103u)
        {
            IBehaviorManager* const Behavior = static_cast<CEqObj*>(Raw)->get_behavior_interface();
            if (Behavior != nullptr)
            {
                // .text:06d09986 .. 06d099ae -- block {+0 [EBX+4], +4 the new
                // missile's id ([ESP+0x2c])}, event {6, &block} to
                // IBehaviorManager slot 0 (Common LAB_062d3f90).
                unsigned int MissileBlock[2];
                MissileBlock[0] = Rec.OwnerId;
                MissileBlock[1] = Id;
                FBehaviorManagerEvent MissileEvent;
                MissileEvent.Type = 6;
                MissileEvent.Data = MissileBlock;
                Behavior->Slot00(&MissileEvent);
            }
        }
    }

    // .text:06d099b0 .. 06d099b5 -- the vector's block freed (its destructor).
}

// ===========================================================================
// StarSystem::create_bullet -- retail FUN_06d099e0 (.text:06d099e0 .. 06d09b82,
// thiscall, RET 4). One gun round from SrvGun::launch_bullets (.text:06cee2b6).
// No player view and no controller hears of it here: the client builds its own
// rounds from the FIREWEAPON notify (FUN_06d0e2e0).
// ===========================================================================
void StarSystem::create_bullet(FServerBulletRecord const& Rec)
{
    // .text:06d099fb .. 06d09a36 -- operator new(0x60), FUN_06cee360 (GameObject
    // ctor) and the three Bullet vtables: Bullet::Bullet. The null test on the
    // allocation reaches nothing below it (.text:06d09a38 keeps ESI = 0).
    ::Bullet* const Obj = new ::Bullet();

    Archetype::Munition* const Arch = const_cast<Archetype::Munition*>(Rec.Arch);
    Arch->load();                                                   // .text:06d09a46 .. 06d09a53 vt +0xc
    Obj->create(Arch);                                              // .text:06d09a56 .. 06d09a5e vt +0x164 (FUN_06cf4d40)
    Arch->Release();                                                // .text:06d09a64 .. 06d09a67

    // .text:06d09a6d .. 06d09a88 -- the wrapper's object under (& 0x67) == 0x67.
    CBeam* const Beam = Obj->cbeam();

    // .text:06d09a88 .. 06d09afc -- the CProjectile::CreateParms on the stack
    // (base [ESP+0x14]); every field is written here, the ctor is inlined away.
    CProjectile::CreateParms Parms;
    Parms.Id = 0u;                                                  // .text:06d09aad  +0x00
    Parms.ObjDb = this;                                             // .text:06d09adb  +0x04
    Parms.OwnerPlayer = 0u;                                         // .text:06d09aa5  +0x08
    Parms.HitPts = -1.0f;                                           // .text:06d09abb  +0x0c (0xbf800000)
    Parms.Pos = Rec.Position;                                       // .text:06d09a8f .. 06d09ab1  +0x10 <- rec +0x0c
    Parms.Orient = Rec.Orientation;                                 // .text:06d09ae8 REP MOVSD 9  +0x1c <- rec +0x18
    Parms.Velocity = Rec.Velocity;                                  // .text:06d09ab5 .. 06d09afc  +0x40 <- rec +0x3c
    Parms.AngularVelocity = Vector{0.0f, 0.0f, 0.0f};               // .text:06d09ac3 .. 06d09ad3  +0x4c
    Parms.Pad58 = 0u;                                               // .text:06d09aa9  +0x58
    Parms.OwnerId = Rec.OwnerId;                                    // .text:06d09a88 .. 06d09a8b  +0x5c <- rec +0x00
    // Rec.EquipId (+0x08) is not read.

    Beam->init(Parms);                                              // .text:06d09b00 .. 06d09b05 vt +0xa0
    Obj->enter_system(this, nullptr);                               // .text:06d09b0b .. 06d09b16 vt +0x170

    // .text:06d09b1c .. 06d09b70 -- MetaList append on the +0x84 block, inlined
    // (FreeList +0x94 when bUseHeap +0x90, else operator new(0xc)).
    Beams.append(Obj);
}

// ===========================================================================
// Four StarSystem members GameObject / EqObj reach through OwnerSystem.
// PORTED 2026-09-24 (claude-code:w5-server-06ce); see server_sysdb.h.
// ===========================================================================

// FUN_06d0cfa0 (.text:06d0cfa0, RET 4). Two MetaList walks: ESI = 0 then
// `ESI ? [ESI] : [EBX + 0x38]` (Ships' head) until null, CALL [obj vt + 0x178]
// with the id; then the same over [EBX + 0x60] (Solars' head).
void StarSystem::notify_object_left(unsigned int ObjId)
{
    MetaNode<GameObject>* Node = nullptr;
    while (Ships.traverse(Node, true))
        Node->Data->on_object_left_system(ObjId);           // .text:06d0cfc5
    Node = nullptr;
    while (Solars.traverse(Node, true))
        Node->Data->on_object_left_system(ObjId);           // .text:06d0cfe5
}

// FUN_06d0d730 (.text:06d0d730, RET 8).
void StarSystem::collision_damage(FCollisionDamage const& Rec, GameObject* Target)
{
    // .text:06d0d74d .. 06d0d753  the source, by id (FUN_06d0c840).
    GameObject* const Source = find_object(Rec.SourceId);

    // .text:06d0d75e .. 06d0d78d  with DAT_06d88a80 clear, a player source never
    // damages a player target (IObjInspect +0xbc is_player on both); a null
    // target also ends it here.
    if (GbAllowPvp == 0 && Source != nullptr && Source->is_player())
    {
        if (Target == nullptr)
            return;
        if (Target->is_player())
            return;
    }
    // .text:06d0d793 .. 06d0d7a7
    if (Target == nullptr)
        return;
    if (Target->is_destroyed())
        return;

    // .text:06d0d7ad .. 06d0d7ca  the hit: {source id, the target's sub-object,
    // the damage, byte +0x0c}. The byte is `MOV AL,[ESP + 0x44]` -- the low byte
    // of this function's own first argument slot, i.e. of the Rec POINTER; retail
    // never writes a value of its own there. Reproduced as read.
    FSubObjDamage Hit;
    Hit.InflictorId = Rec.SourceId;
    Hit.SubObjId = Rec.TargetSubObjId;
    Hit.Damage = Rec.Damage;
    Hit.Field_0x0c = static_cast<unsigned char>(reinterpret_cast<uintptr_t>(&Rec) & 0xffu);

    // .text:06d0d7ce .. 06d0d81f  a fresh DamageList: Cause 1, inflictor = the
    // source id, owner player = the source's CSimple::OwnerPlayer (guarded
    // accessor read without a null test) when there is a source, else 0.
    DamageList Damage;
    Damage.Cause = static_cast<DamageCause>(1);
    Damage.InflictorId = Rec.SourceId;
    Damage.InflictorOwner = 0;
    if (Source != nullptr)
        Damage.InflictorOwner = Source->csimple()->OwnerPlayer;

    Target->damage_sub_obj(Hit, Damage);                    // .text:06d0d831 vt +0x17c
    Target->apply_damage_list(Damage);                      // .text:06d0d840 vt +0x188
}

// FUN_06d0d6e0 (.text:06d0d6e0 .. .text:06d0d722, RET 4). PORTED 2026-09-24
// (claude-code:w5-server-06cf), all 28 instructions:
//   06d0d6e6 .. 06d0d6f3  obj = FUN_06d0c840([Info + 4]); null -> out
//   06d0d6f9 .. 06d0d701  obj->vt[+0x160] (is_destroyed) true -> out
//   06d0d707 .. 06d0d70f  obj->vt[+0x19c] (is_invincible) true -> out
//   06d0d711 .. 06d0d71a  obj->vt[+0x158] destroy(1, [Info + 0]); result unused
void StarSystem::badlands_collision(SSPBadLandsObjCollisionInfo const& Info)
{
    GameObject* const Obj = find_object(Info.ObjectId);
    if (Obj == nullptr)
        return;
    if (Obj->is_destroyed())
        return;
    if (Obj->is_invincible())
        return;
    Obj->destroy(1, Info.Field_0x00);
}

// FUN_06d0cd60 (.text:06d0cd60 .. .text:06d0ce40, RET 8). PORTED 2026-09-24
// (claude-code:w5-server-06cf), all 135 instructions:
//   06d0cd70 .. 06d0cd81  MetaList walk of the solar block: EDI = 0 then
//                         `EDI ? [EDI] : [this + 0x60]` until null
//   06d0cd87 .. 06d0cdad  obj = node->Data; the CObject at obj+0xc+4 narrowed
//                         with (ObjectClass & 0x303) == 0x303, else 0 -- there
//                         is no test on the result: [0 + 0x164] is read anyway
//   06d0cdaf .. 06d0cdc3  base = CEqObj+0x164; base != 0: BaseId != 0 and
//                         base != BaseId -> next node
//   06d0cdc5 .. 06d0cddb  Out.insert(end, obj) (FUN_06d05d90) -> next node
//   06d0cde2 .. 06d0cdf2  base == 0: TypeBits (+0xe0) with a bit outside 0xc40
//                         -> next node
//   06d0cdf8 .. 06d0cdfe  BaseId != 0 -> next node
//   06d0ce04 .. 06d0ce3b  the inline push_back (FUN_06cfc340 buynode,
//                         FUN_06d06720 construct, ++size) -> next node
// Both inserts are push_back; the list is the caller's.
void StarSystem::collect_dockable_solars(std::list<GameObject*>& Out, unsigned int BaseId)
{
    MetaNode<GameObject>* Node = nullptr;
    while (Solars.traverse(Node, true))
    {
        GameObject* const Obj = Node->Data;
        CObject* const Cobj = Obj->cobj();
        CEqObj* Eq = nullptr;
        if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & 0x303u) == 0x303u)
            Eq = static_cast<CEqObj*>(Cobj);
        // Retail reads +0x164 through the narrowed pointer without testing it.
        unsigned int const Base = Eq->DockingPointBaseId;
        if (Base != 0)
        {
            if (BaseId != 0 && Base != BaseId)
                continue;
            Out.push_back(Obj);
            continue;
        }
        unsigned int const Bits = Eq->TypeBits;
        if ((Bits & 0xc40u) != Bits)
            continue;
        if (BaseId != 0)
            continue;
        Out.push_back(Obj);
    }
}

// StarSystem::send_comm -- FUN_06d0db30 (.text:06d0db30 .. .text:06d0dd2f),
// thiscall, RET 0x58. Ported 2026-09-24 (claude-code:w5-server-06cf):
//   06d0db37 .. 06d0db52  (SenderId & 0x3fffffff) == 0 -> bNoSender = 1 (the sender
//                         position stays unset; it is never read on that path)
//   06d0db54 .. 06d0db7a  else FUN_06d0c840 = find_object(SenderId); null -> return;
//                         its IObjInspect vt[0] get_position() copied
//   06d0db7e .. 06d0db90  every node of the player list (head re-read, 06d0dd1f):
//     06d0db90 .. 06d0dbc1  PlayerDB::is_valid(node PlayerId [+0x40]) inlined, else next
//     06d0dbcd  ShipId = Players[PlayerId].ship_object_id [slot-0x34]
//     bNoSender (06d0dcc7): send when pub::SinglePlayer() (Server's forwarder onto
//               COMMON's SinglePlayer) or ReceiverId == ShipId or bBroadcast
//     ReceiverId == ShipId (06d0dc7e): send, then bBroadcast == 0 -> RETURN
//     otherwise bBroadcast == 0 -> next (06d0dbeb); find_object(ShipId) null -> next;
//               squared distance of its get_position() from the sender's, summed
//               (dz^2 + dx^2) + dy^2 on the x87 stack, FCOMP against .rdata:06d66af4
//               (1e8 = 10000^2), TEST AH,0x41 / JZ -> next when strictly greater
//               (an unordered compare sends); else send
//   every send is FUN_06cf6ac0(&node->View, SenderId, ReceiverId, VoiceId, Costume,
//   CommType, TextIds, TextCount, Mood, Duration) -- SystemPlayerView_SendComm.
// The x87 differences are taken in double here (float operands, so each difference
// is exact; the squares and sums round at 53 bits like MSVC's default x87 precision).
void StarSystem::send_comm(unsigned int SenderId, unsigned int ReceiverId, unsigned int VoiceId,
                           Costume const& Cost, unsigned int CommType, unsigned int* TextIds,
                           int TextCount, unsigned int Mood, float Duration, bool bBroadcast)
{
    bool bNoSender = false;
    Vector SenderPos = {};
    if ((SenderId & 0x3fffffffu) == 0) {
        bNoSender = true;                                                    // .text:06d0db4d
    } else {
        GameObject* const Sender = find_object(SenderId);                    // .text:06d0db57
        if (Sender == nullptr)
            return;
        SenderPos = Sender->get_position();                                  // .text:06d0db68
    }

    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        unsigned int const PlayerId = Node->View.PlayerId;                   // .text:06d0db90
        if (!Players.is_valid(PlayerId))
            continue;
        unsigned int const ShipId = Players[PlayerId].ship_object_id;       // .text:06d0dbcd

        if (bNoSender) {
            if (SinglePlayer() || ReceiverId == ShipId || bBroadcast)        // .text:06d0dcc7
                SystemPlayerView_SendComm(&Node->View, SenderId, ReceiverId, VoiceId, Cost,
                                          CommType, TextIds, TextCount, Mood, Duration);
            continue;
        }

        if (ReceiverId == ShipId) {                                          // .text:06d0dbdd
            SystemPlayerView_SendComm(&Node->View, SenderId, ReceiverId, VoiceId, Cost,
                                      CommType, TextIds, TextCount, Mood, Duration);
            if (!bBroadcast)                                                 // .text:06d0dcc3
                return;
            continue;
        }

        if (!bBroadcast)                                                     // .text:06d0dbeb
            continue;
        GameObject* const Ship = find_object(ShipId);                        // .text:06d0dbf4
        if (Ship == nullptr)
            continue;
        Vector const& Pos = Ship->get_position();                            // .text:06d0dc05
        double const Dx = static_cast<double>(Pos.x) - static_cast<double>(SenderPos.x);
        double const Dy = static_cast<double>(Pos.y) - static_cast<double>(SenderPos.y);
        double const Dz = static_cast<double>(Pos.z) - static_cast<double>(SenderPos.z);
        double const DistSq = (Dz * Dz + Dx * Dx) + Dy * Dy;
        if (DistSq > static_cast<double>(1.0e8f))                            // .text:06d0dc2b
            continue;
        SystemPlayerView_SendComm(&Node->View, SenderId, ReceiverId, VoiceId, Cost,
                                  CommType, TextIds, TextCount, Mood, Duration);
    }
}

// FUN_06d0e750 (.text:06d0e750, RET 4). Every node of the player list (+0x0c):
// FUN_06cf7370(&node->View, Info.ObjectId), and on true IClient +0x15c
// (DAT_06d8fb14) with &node+0x40 -- the view's PlayerId -- and the record.
void StarSystem::broadcast_fuse(FServerBurnFuseInfo const& Info)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Info.ObjectId))
            continue;
        g_pClientInterface->SendServerBurnFuse(Node->View.PlayerId, Info);   // .text:06d0e77f
    }
}

// ===========================================================================
// The IObjAffect notifiers (2026-09-24, claude-code:w5-server-06d0). Every one
// walks the player list (+0x0c, sentinel at [this+0xc]) the same way retail
// does -- `MOV ESI,[head]` / `CMP ESI,head` / JZ, then `MOV ESI,[ESI]` /
// `CMP ESI,[this+0xc]` at the bottom -- with no null test on the head.
// FUN_06cf7370 is SystemPlayerView_IsObjectIdVisible; DAT_06d8fb14 is
// g_pClientInterface, called unguarded. The four player-skip forms compare the
// view's player (node+0x40) with *SourceClient, the id of the client that
// asked for the change, so it is not told about its own action.
// ===========================================================================

// FUN_06d0e500 (.text:06d0e500, RET 0x10). Only caller EqObj slot_4c
// (.text:06ce71ef). Per node:
//   .text:06d0e520 .. 06d0e529  node+0x40 == *SourceClient -> next
//   .text:06d0e52b .. 06d0e538  FUN_06cf7370(&node->View, *ObjId) == 0 -> next
//   .text:06d0e53a .. 06d0e55a  record {*ObjId, word *EquipId, byte bActivate}
//   .text:06d0e561              IClient +0x04 (SendCommonActivateEquip) (node+0x40, &record)
// The record is XActivateEquip; its pad byte is not written (uninitialised in
// retail, zero here).
void StarSystem::notify_activate_equip(unsigned int const& ObjId, unsigned short const& EquipId,
                                       bool bActivate, int const& SourceClient)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        XActivateEquip Record = {};
        Record.ObjectId = ObjId;
        Record.EquipId = EquipId;
        Record.bActivate = bActivate;
        g_pClientInterface->SendCommonActivateEquip(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e580 (.text:06d0e580, RET 0x10). Only caller Ship slot_50
// (.text:06d024e3). Same walk; record {*ObjId, byte bEngage (+4, param 2),
// byte bArg2 (+5, param 3 kept in BL)} (.text:06d0e5ba .. 06d0e5d6), sent
// through IClient +0x08 (SendCommonActivateCruise) at .text:06d0e5dd. The record is
// XActivateCruise.
void StarSystem::notify_cruise(unsigned int const& ObjId, bool bEngage, bool bArg2,
                               int const& SourceClient)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        XActivateCruise Record = {};
        Record.ObjectId = ObjId;
        Record.bActivate = bEngage;
        Record.Field_0x05 = bArg2 ? 1u : 0u;
        g_pClientInterface->SendCommonActivateCruise(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e600 (.text:06d0e600, RET 0xc). Only caller Ship slot_54
// (.text:06d0264e). Same walk; record {*ObjId, byte bActivate}
// (.text:06d0e63a .. 06d0e64e) through IClient +0x0c (SendCommonActivateThrusters) at
// .text:06d0e655. The record is XActivateThrusters.
void StarSystem::notify_thrusters(unsigned int const& ObjId, bool bActivate, int const& SourceClient)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        XActivateThrusters Record = {};
        Record.ObjectId = ObjId;
        Record.bActivate = bActivate;
        g_pClientInterface->SendCommonActivateThrusters(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e670 (.text:06d0e670, RET 0x14). Callers: Ship set_target
// (.text:06d023f6), EqObj slot_44 (.text:06ce708c), Guided set_target
// (.text:06cf4d2d).
//   .text:06d0e67b .. 06d0e69e  FUN_06d006c0(&ObjId, &obj, &sys); a miss
//                               leaves obj = 0 (.text:06d0e696)
//   per node:
//   .text:06d0e6b4 .. 06d0e6bd  node+0x40 == *SourceClient -> next
//   .text:06d0e6bf              obj == 0 -> next (tested per node, not once)
//   .text:06d0e6c3 .. 06d0e6cc  obj->get_type(type) (IObjInspect +0x88), the
//                               type written over the Target argument's slot
//   .text:06d0e6d2              (type & 0xfdff) != 0 -> send without asking
//   .text:06d0e6dc .. 06d0e6e7  otherwise FUN_06cf73d0(&node->View, obj) must
//                               say visible
//   .text:06d0e6e9 .. 06d0e72d  record {*ObjId, word *SubObjId, Target ?
//                               Target->get_id() : 0, word *SubTarget}
//   .text:06d0e735              IClient +0x10 (SendCommonSetTarget) (node+0x40, &record)
// FUN_06d006c0 is Server_LookupGameObject in this port (see the tracker item
// for 0x06d006c0); it answers null on a miss, which is exactly obj = 0 here.
void StarSystem::notify_set_target(unsigned int const& ObjId, unsigned short const& SubObjId,
                                   IObjRW* Target, unsigned short const& SubTarget,
                                   int const& SourceClient)
{
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (Obj == nullptr)
            continue;
        unsigned int Type = 0u;
        Obj->get_type(Type);
        if ((Type & 0xfdffu) == 0u && !SystemPlayerView_IsObjectVisible(&Node->View, Obj))
            continue;
        XSetTarget Record = {};
        Record.ObjectId = ObjId;
        Record.SubObjId = SubObjId;
        Record.TargetObjectId = (Target != nullptr) ? Target->get_id() : 0u;
        Record.TargetSubObjId = SubTarget;
        g_pClientInterface->SendCommonSetTarget(Node->View.PlayerId, Record);
    }
}

// FUN_06d0f0c0 (.text:06d0f0c0, RET 0xc). Only caller Ship slot_38 (the
// jettison, .text:06d0230c), which pushes (Obj, EquipId, Count).
//   .text:06d0f0c7 .. 06d0f0e2  the owner: CSimple::OwnerPlayer through the
//                               inlined (& 3) == 3 accessor, read unguarded
//   .text:06d0f0eb              ObjId = Obj->get_id()
//   .text:06d0f0ee .. 06d0f12c  owner != 0, owner - 1 < Players.m_MaxPlayers
//                               (DAT_06d90264), slot +0x348 (client_id) != 0 and
//                               slot +0x3e4 (ship_object_id) == ObjId -- the
//                               body of PlayerDB::is_valid_ship_owner, inlined
//   .text:06d0f12e .. 06d0f138  FUN_06d4f100(slot, EquipId, Count, 0), i.e.
//                               PlayerData_RemoveEquipment without the notify
//   .text:06d0f13d .. 06d0f14d  record {ObjId, word EquipId, Count}
//   per node (no player skip here):
//   .text:06d0f157 .. 06d0f166  FUN_06cf7370(&node->View, ObjId)
//   .text:06d0f179              IClient +0x20 (SendCommonJettisonCargo) (node+0x40, &record)
// The record is XJettisonCargo.
void StarSystem::notify_jettison(GameObject* Obj, unsigned short EquipId, int Count)
{
    unsigned int const Owner = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    if (Players.is_valid_ship_owner(Owner, ObjId))
        PlayerData_RemoveEquipment(Players[Owner], EquipId, Count, false);

    XJettisonCargo Record = {};
    Record.ObjectId = ObjId;
    Record.ItemId = EquipId;
    Record.Count = static_cast<unsigned int>(Count);

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        g_pClientInterface->SendCommonJettisonCargo(Node->View.PlayerId, Record);
    }
}

// ===========================================================================
// StarSystem::notify_fire_weapon -- retail FUN_06d0e2e0 (.text:06d0e2e0 ..
// 06d0e4d5, thiscall, RET 0x10). Caller SrvGun::fire_forward (.text:06cedfd6).
// Stack base T = ESP after the three register pushes; the XFireWeaponInfo at
// [T+0x10], its hardpoint vector at [T+0x20].
// ===========================================================================
void StarSystem::notify_fire_weapon(unsigned int const& ObjId, std::vector<unsigned short> const& EquipIds,
                                    Vector const& Target, int SourceClient)
{
    // .text:06d0e2fb .. 06d0e326 -- the record's inlined constructor: id 0,
    // target (FLT_MAX, FLT_MAX, FLT_MAX), the vector built empty by its
    // allocator constructor (import .rdata:06d646fc) -- XFireWeaponInfo().
    // .text:06d0e32c .. 06d0e35c -- then id and target from the arguments.
    XFireWeaponInfo Info;
    Info.ObjectId = ObjId;
    Info.TargetPos = Target;

    // .text:06d0e34a .. 06d0e474 -- VC6 vector<unsigned short>::operator=
    // inlined, from an empty destination: a non-empty source exceeds the zero
    // capacity (.text:06d0e3d5 JA), so the destination gets a new block of
    // exactly size() elements (operator new(size * 2), .text:06d0e451) that the
    // source is copied into (import .rdata:06d646ec), with _Last = _End =
    // block + size (.text:06d0e470 / 06d0e474). An empty source takes the copy
    // path (.text:06d0e386 .. 06d0e3ba) and leaves all three pointers null.
    // std::vector's operator= into an empty vector does the same.
    Info.Hardpoints = EquipIds;

    // .text:06d0e478 .. 06d0e4b5 -- every player view but SourceClient's
    // (node +0x40) that can see the shooter (FUN_06cf7370): IClient slot 0
    // (FLPACKET_COMMON_FIREWEAPON) with the player id and the record.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Player->View, ObjId))
            continue;
        g_pClientInterface->SendCommonFireWeapon(Player->View.PlayerId, Info);
    }

    // .text:06d0e4b7 .. 06d0e4bc -- the vector's destructor: its block freed
    // (~XFireWeaponInfo at scope exit; LAB_06d616d8 is the same on unwind).
}

// FUN_06d0c970 (.text:06d0c970, RET 8). See server_sysdb.h for the map.
void StarSystem::complete_dock(GameObject* ShipObj, GameObject* Dock)
{
    ::Ship* const DockingShip = static_cast< ::Ship*>(ShipObj);
    if (DockingShip->DeathSequenceObjId != 0)                   // .text:06d0c978 .. 06d0c985
        return;

    // .text:06d0c993 .. 06d0ca43 -- the ship block (head +0x38), `node ?
    // node->Next : Head`. An owned ship (the inlined CSimple accessor, its
    // +0xb4 read unguarded) is skipped; for the rest the 0x503-narrowed object's
    // CEqObj::get_behavior_interface is asked, with no test on the narrowing.
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->csimple()->OwnerPlayer != 0u)
            continue;
        CObject* const Raw = (Obj != nullptr) ? Obj->Object : nullptr;
        CEqObj* const Other =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Other->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // .text:06d0c9fa .. 06d0ca41 -- block {+0 Dock->get_id() (EDI, vt +0x20),
        // +4 Dock's IObjInspect::get_type out value (vt +0x88), +8 ShipObj->
        // get_id() (EBP, vt +0x20)} (the three calls in retail order: ShipObj id
        // first, then Dock id, then Dock type); event {3, &block} to
        // IBehaviorManager slot 0 (Common LAB_062d3f90).
        unsigned int LeftBlock[3];
        LeftBlock[2] = ShipObj->get_id();
        LeftBlock[0] = Dock->get_id();
        unsigned int DockType;
        Dock->get_type(DockType);
        LeftBlock[1] = DockType;
        FBehaviorManagerEvent LeftEvent;
        LeftEvent.Type = 3;
        LeftEvent.Data = LeftBlock;
        Behavior->Slot00(&LeftEvent);
    }

    // .text:06d0ca48 .. 06d0ca85 -- controller event 0x11.
    unsigned int Msg[3];
    Msg[0] = ShipObj->get_id();
    Msg[1] = Dock->get_id();
    Msg[2] = SystemInfo->NicknameId;
    Server_ControllerDispatch(0, pub::Controller::EMessage::DockComplete, Msg);

    // .text:06d0ca8a .. 06d0cac8 -- every player view hears that the ship left
    // the system for the dock; both ids are fetched afresh per player.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        unsigned int const DockId = Dock->get_id();
        unsigned int const ShipId = ShipObj->get_id();
        SystemPlayerView_OnObjectLeftSystem(&Player->View, &ShipId, &DockId);
    }

    // .text:06d0cacc .. 06d0cad1
    DockingShip->DeathSequenceObjId = Dock->get_id();

    if (ShipObj->csimple()->OwnerPlayer != 0u)                  // .text:06d0cad7 .. 06d0caf5
    {
        // .text:06d0cba2 .. 06d0cbe3 -- a jump gate / jump hole protects the
        // player's ship while it goes through.
        if ((Dock->get_type_bits() & 0x40u) != 0u || (Dock->get_type_bits() & 0x800u) != 0u)
        {
            FInvincibility Cmd;
            Cmd.Type = 2;
            Cmd.bProtect = true;
            Cmd.bFromPlayers = true;
            Cmd.Pad06[0] = 0;
            Cmd.Pad06[1] = 0;
            Cmd.MaxHpLoss = 0.0f;
            ShipObj->set_invincible(Cmd);
        }
        return;
    }

    // The ship's CShip: the raw CBase object under the 0x503 test (null tests
    // included, as retail's inlined accessor has them).
    CObject* const Raw = ShipObj->Object;
    CShip* const Cs =
        (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
            ? static_cast<CShip*>(Raw) : nullptr;

    if (SinglePlayer())                                         // .text:06d0cafb
    {
        Cs->bJumpedOut = 0;                                     // .text:06d0cb27 MOV byte [EAX+0x214],0
        return;
    }

    Cs->enable_collisions_r(false);                             // .text:06d0cb57
    if ((Dock->get_type_bits() & 0x40u) != 0u || (Dock->get_type_bits() & 0x800u) != 0u)
        DockingShip->DeathSequenceTimer = 12.0f;                // .rdata:06d67cc0
    else
        DockingShip->DeathSequenceTimer = 0.0f;                 // .rdata:06d67cc4
}

// FUN_06d0cc00 (.text:06d0cc00, RET 0x10). See server_sysdb.h for the map.
void StarSystem::arrive_ship(GameObject* Obj, GameObject* JumpObj, Vector const& Pos, Matrix const& Orient)
{
    // .text:06d0cc0d .. 06d0cc1c
    unsigned int Bits = 0u;
    if (JumpObj != nullptr)
        Bits = JumpObj->get_type_bits();

    // .text:06d0cc1e .. 06d0cc47 -- the inlined (& 3) CSimple accessor, the
    // +0xb4 read unguarded.
    if (Obj->csimple()->OwnerPlayer == 0u)
    {
        // The 0x503-narrowed object (.text:06d0cc58 .. 06d0cc77 and
        // .text:06d0cc85 .. 06d0cca2), handed on without a null test.
        CObject* const Raw = (Obj != nullptr) ? Obj->Object : nullptr;
        CShip* const Cs =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CShip*>(Raw) : nullptr;

        if ((Bits & 0x40u) != 0u || (Bits & 0x800u) != 0u)          // .text:06d0cc49 / 06d0cc4e
        {
            // .text:06d0cca4 .. 06d0ccac -- PUSH 0x3f000000, EBP (Orient),
            // ESI (Pos), EDI: the jump object argument is Obj itself.
            Cs->jump_in(static_cast<IObjRW*>(Obj), Pos, Orient, 0.5f);
        }
        else if ((Bits & 0x400u) != 0u)                             // .text:06d0cc53
        {
            Cs->airlock_in(Pos, Orient);                            // .text:06d0cc79 .. 06d0cc7d
        }
    }

    // .text:06d0ccb2 .. 06d0cd17 -- the SYSTEM_SWITCH_IN record.
    FPayloadServerSystemSwitchIn Record;
    Record.Field_0x00 = Obj->get_id();                              // .text:06d0ccb6
    Record.Field_0x04 = Bits;                                       // .text:06d0ccd0
    Record.Position = Pos;                                          // .text:06d0ccb9 .. 06d0ccd8
    Server_MatrixToQuat(Record.Orientation, Orient);                // .text:06d0ccc9 .. 06d0ccf1

    // .text:06d0cd10 .. 06d0cd4b -- every player view that can see Obj.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectVisible(&Node->View, Obj))   // FUN_06cf73d0
            continue;
        g_pClientInterface->SendServerSystemSwitchIn(Node->View.PlayerId, Record);   // IClient +0xe8
    }
}

// FUN_06d0d000 (.text:06d0d000, RET 8). See server_sysdb.h for the map.
void StarSystem::remove_ship(GameObject* Obj, PhySys::CreateParms* OutParms)
{
    // .text:06d0d001 .. 06d0d01d -- the inlined (& 3) accessor, read unguarded.
    unsigned int const Owner = Obj->csimple()->OwnerPlayer;

    // .text:06d0d023 .. 06d0d045 -- every view but the owner's.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == Owner)
            continue;
        SystemPlayerView_OnObjectDestroyed(&Player->View, Obj, 0);  // FUN_06cf6820
    }

    // .text:06d0d047 .. 06d0d05c
    unsigned int const ObjId = Obj->get_id();
    Server_LookupCacheForget(ObjId);

    // .text:06d0d060 .. 06d0d077 -- `node = node ? node->Next : Head`.
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        if (Node->Data == Obj)
        {
            // .text:06d0d079 .. 06d0d0de -- MetaList::remove, inlined: the
            // head / tail / middle unlink, then FreeList (+0x44) when
            // bUseHeap (+0x40), else operator delete (FUN_06d5ffb0).
            Ships.remove(Node);
            break;
        }
    }

    // .text:06d0d0c4 .. 06d0d0cd / .text:06d0d0e1 .. 06d0d0ea -- both paths.
    Obj->leave_system(OutParms);
}

// FUN_06d0d100 (.text:06d0d100, RET 0x10). See server_sysdb.h for the map.
void StarSystem::add_ship(GameObject* Obj, PhySys::CreateParms* Parms, Vector const& Pos, Matrix const& Orient)
{
    // .text:06d0d11f .. 06d0d138 -- SystemInfo->vtable[0](), the system id.
    Parms->CollisionGroupId = SystemInfo->NicknameId;

    // .text:06d0d13b .. 06d0d158 -- the raw object under the (& 1) test; the
    // writes below do not test the result.
    CObject* const Raw =
        (Obj != nullptr && Obj->Object != nullptr &&
         (static_cast<unsigned int>(Obj->Object->ObjectClass) & 0x1u) == 0x1u)
            ? Obj->Object : nullptr;
    Raw->WorldPosition = Pos;                                       // .text:06d0d158 .. 06d0d176 (+0x2c)
    Raw->Orientation = Orient;                                      // .text:06d0d179 .. 06d0d186 (+0x08)

    // .text:06d0d188 .. 06d0d196 -- IEngine::UpdateInstance(instance, 0, 0).
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Raw->EngineInstanceId), 0, nullptr);

    Obj->enter_system(this, Parms);                                 // .text:06d0d1a7 vt +0x170

    // .text:06d0d1ad .. 06d0d1d2 -- MetaList append, inlined (FUN_06d11850).
    Ships.append(Obj);

    // .text:06d0d1d5 .. 06d0d1f8 -- the 0x503-narrowed CShip.
    CObject* const ShipRaw = (Obj != nullptr) ? Obj->Object : nullptr;
    CShip* const Cs =
        (ShipRaw != nullptr && (static_cast<unsigned int>(ShipRaw->ObjectClass) & 0x503u) == 0x503u)
            ? static_cast<CShip*>(ShipRaw) : nullptr;

    // .text:06d0d1fa .. 06d0d25b -- CShipCreateInfo (ctor FUN_06cf63f0 plus
    // its inlined tail), filled only when the object is a CShip.
    CShipCreateInfo Info;
    if (Cs != nullptr)
        CShipCreateInfo_Fill(&Info, Cs);                            // FUN_06d07690

    // .text:06d0d25e .. 06d0d28c -- every player view.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        Info.bMarkedBy = Obj->is_marked_by(Player->View.PlayerId) ? 1 : 0;   // FUN_06cef450
        SystemPlayerView_CreateShipForPlayer(&Player->View, &Info, Obj);    // FUN_06cf6630
    }
    // .text:06d0d290 .. 06d0d360 -- Info's destructor (inlined in retail).
}

// FUN_06d0cee0 (.text:06d0cee0, RET 0xc). See server_sysdb.h for the map.
void StarSystem::launch(GameObject* ShipObj, GameObject* Dock, int LaunchType)
{
    if (ShipObj->csimple()->OwnerPlayer != 0u)                      // .text:06d0cef9 .. 06d0cf01
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",                  // .text:06d0cf70 .. 06d0cf92
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x9f2,
              "StarSystem::launch() called on a player!");
        return;
    }

    // .text:06d0cf03 .. 06d0cf37 -- the dock's CEqObj, or null.
    CObject* const DockRaw = (Dock != nullptr) ? Dock->Object : nullptr;
    CEqObj* const DockEq =
        (DockRaw != nullptr && (static_cast<unsigned int>(DockRaw->ObjectClass) & 0x103u) == 0x103u)
            ? static_cast<CEqObj*>(DockRaw) : nullptr;

    // .text:06d0cf37 .. 06d0cf55 -- the ship's CShip, not tested afterwards.
    CObject* const ShipRaw = (ShipObj != nullptr) ? ShipObj->Object : nullptr;
    CShip* const Cs =
        (ShipRaw != nullptr && (static_cast<unsigned int>(ShipRaw->ObjectClass) & 0x503u) == 0x503u)
            ? static_cast<CShip*>(ShipRaw) : nullptr;

    Cs->launch(static_cast<IObjRW*>(ShipObj), DockEq, LaunchType); // .text:06d0cf5e (PUSH ESI = ShipObj)
    static_cast< ::Ship*>(ShipObj)->apply_launch_invincibility();  // .text:06d0cf66 FUN_06d01110
}

// FUN_06d0df40 (.text:06d0df40, RET 0x14). Callers: IServerImpl::GoTradelane
// (.text:06cf0adf) and Ship go_tradelane (.text:06d02919), both pushing
// (ShipId, EntranceId, ExitId, bFastExit, Speed).
//
// Pass 1, the ship block (+0x34, head +0x38, NULL-terminated; `node ? [node] :
// [this+0x38]` at .text:06d0df60 .. 06d0df6f):
//   * the ship itself (get_id() == ShipId, .text:06d0df7c):
//       .text:06d0df87 .. 06d0dfaf  SkipPlayer = its CSimple::OwnerPlayer
//                                   (the inlined accessor, read unguarded)
//       .text:06d0dfb3 .. 06d0dfce  controller event 0x16 {ShipId, EntranceId,
//                                   ExitId, SystemInfo vt0 = the system id}
//       .text:06d0dfd3 .. 06d0e00a  its CShip (the CBase pointer adjust, then
//                                   Object and (ObjectClass & 0x503) == 0x503):
//                                   CShip::use_tradelane(true)
//     and on to the next node (.text:06d0e010).
//   * every other ship nobody owns (.text:06d0e015 .. 06d0e032) hands its
//     IBehaviorManager (CEqObj::get_behavior_interface on the 0x503-narrowed
//     object, called with no test on the narrowing) a type-4 notification
//     {4, &payload} through its vtable slot 0 (.text:06d0e0af), payload
//       +0x00 byte 1, +0x01 byte 0, +0x02 byte bFastExit, +0x04 float Speed,
//       +0x08 0, +0x0c ShipId, +0x10 EntranceId, +0x14 ExitId
//     (.text:06d0e069 .. 06d0e0a7) -- "this ship entered a lane".
//
// Pass 2 (.text:06d0e0b6 .. 06d0e113): record {ShipId, EntranceId, ExitId,
// byte bFastExit} (XGoTradelane) through IClient +0x18 (SendCommonGoTradeLane) to every
// player except SkipPlayer that FUN_06cf7370 says can see ShipId.
//
// The type-4 notification goes to IBehaviorManager::Slot00 (Common
// LAB_062d3f90, non-virtual in the port) with FBehaviorTradelaneEvent.
void StarSystem::notify_go_tradelane(unsigned int ShipId, unsigned int EntranceId,
                                     unsigned int ExitId, bool bFastExit, float Speed)
{
    unsigned int SkipPlayer = 0u;                               // .text:06d0df55
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->get_id() == ShipId)
        {
            SkipPlayer = Obj->csimple()->OwnerPlayer;
            unsigned int Msg[4];
            Msg[0] = ShipId;
            Msg[1] = EntranceId;
            Msg[2] = ExitId;
            Msg[3] = SystemInfo->NicknameId;                    // .text:06d0dfc1
            Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneEnter, Msg);
            CObject* const Raw = Obj->Object;
            if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                static_cast<CShip*>(Raw)->use_tradelane(true);
            continue;
        }

        if (Obj->csimple()->OwnerPlayer != 0u)
            continue;
        CObject* const Raw = Obj->Object;
        CEqObj* const Ship =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // .text:06d0e069 .. 06d0e0af -- {4, &payload} to IBehaviorManager slot 0
        // (Common LAB_062d3f90); +3 is never written.
        FBehaviorTradelaneEvent Payload;
        Payload.EntranceId = EntranceId;                        // [ESP+0x50]
        Payload.bFastExit = static_cast<unsigned char>(bFastExit); // [ESP+0x42]
        Payload.Speed = Speed;                                  // [ESP+0x44]
        Payload.bExitRequested = 0u;                            // [ESP+0x41] BL
        Payload.bEnteringLane = 1u;                             // [ESP+0x40]
        Payload.Field_0x08 = 0u;                                // [ESP+0x48] EBX
        Payload.ExitId = ExitId;                                // [ESP+0x54] EBP
        Payload.ShipId = ShipId;                                // [ESP+0x4c] EDI
        FBehaviorManagerEvent LaneEvent;
        LaneEvent.Type = 4;
        LaneEvent.Data = &Payload;
        Behavior->Slot00(&LaneEvent);
    }

    XGoTradelane Record = {};
    Record.ObjectId = ShipId;
    Record.TradelaneRing1 = EntranceId;
    Record.TradelaneRing2 = ExitId;
    Record.Field_0x0c = bFastExit;
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (SkipPlayer == Player->View.PlayerId)
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Player->View, ShipId))
            continue;
        g_pClientInterface->SendCommonGoTradeLane(Player->View.PlayerId, Record);
    }
}

// FUN_06d0e120 (.text:06d0e120, RET 0xc). Callers: IServerImpl::StopTradelane
// (.text:06cf0b3d) and Ship stop_tradelane (.text:06d028b1), pushing
// (ShipId, EntranceId, ExitId).
//
// Pass 1, the ship block, NOT the same shape as FUN_06d0df40's:
//   * the ship itself (.text:06d0e15b): SkipPlayer = its OwnerPlayer,
//     controller event 0x17 {ShipId, EntranceId, ExitId, system id}
//     (.text:06d0e17c .. 06d0e1a6), CShip::use_tradelane(false)
//     (.text:06d0e1d4 PUSH EBX = 0) -- and then it FALLS THROUGH
//     (.text:06d0e1db) into the second test instead of moving on.
//   * every ship (the matched one included) nobody owns and that is not
//     destroyed (GameObject +0x160, .text:06d0e200) hands its IBehaviorManager
//     a type-4 notification {4, &payload} (.text:06d0e285), payload
//       +0x00 byte 0, +0x01 byte 0, +0x02 byte 0, +0x04 0.0f, +0x08 0,
//       +0x0c ShipId, +0x10 EntranceId, +0x14 0
//     (.text:06d0e244 .. 06d0e27d) -- the "left the lane" form.
//
// Pass 2 (.text:06d0e28c .. 06d0e2ca): IClient +0x1c (SendCommonStopTradeLane) (node+0x40,
// ShipId, EntranceId, ExitId) to every player except SkipPlayer that
// FUN_06cf7370 says can see ShipId.
//
// The type-4 notification goes to IBehaviorManager::Slot00 as in
// notify_go_tradelane.
void StarSystem::notify_stop_tradelane(unsigned int ShipId, unsigned int EntranceId,
                                       unsigned int ExitId)
{
    unsigned int SkipPlayer = 0u;                               // .text:06d0e133
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->get_id() == ShipId)
        {
            SkipPlayer = Obj->csimple()->OwnerPlayer;
            unsigned int Msg[4];
            Msg[0] = ShipId;
            Msg[1] = EntranceId;
            Msg[2] = ExitId;
            Msg[3] = SystemInfo->NicknameId;                    // .text:06d0e199
            Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneExit, Msg);
            CObject* const Raw = Obj->Object;
            if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                static_cast<CShip*>(Raw)->use_tradelane(false);
        }

        if (Obj->csimple()->OwnerPlayer != 0u)                  // .text:06d0e1f2
            continue;
        if (Obj->is_destroyed())                                // .text:06d0e200
            continue;
        CObject* const Raw = Obj->Object;
        CEqObj* const Ship =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // .text:06d0e244 .. 06d0e285 -- {4, &payload} to IBehaviorManager slot 0
        // (Common LAB_062d3f90), the "left the lane" form; +3 is never written.
        FBehaviorTradelaneEvent Payload;
        Payload.EntranceId = EntranceId;                        // [ESP+0x40]
        Payload.bFastExit = 0u;                                 // [ESP+0x32] BL
        Payload.Speed = 0.0f;                                   // [ESP+0x34] dword 0
        Payload.ExitId = 0u;                                    // [ESP+0x44] EBX
        Payload.bEnteringLane = 0u;                             // [ESP+0x30] BL
        Payload.bExitRequested = 0u;                            // [ESP+0x31] BL
        Payload.Field_0x08 = 0u;                                // [ESP+0x38] EBX
        Payload.ShipId = ShipId;                                // [ESP+0x3c] EDI
        FBehaviorManagerEvent LaneEvent;
        LaneEvent.Type = 4;
        LaneEvent.Data = &Payload;
        Behavior->Slot00(&LaneEvent);
    }

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (SkipPlayer == Player->View.PlayerId)
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Player->View, ShipId))
            continue;
        g_pClientInterface->SendCommonStopTradeLane(Player->View.PlayerId, ShipId, EntranceId, ExitId);
    }
}

// FUN_06d0c6d0 (.text:06d0c6d0, RET 4). `TEST EAX,0xfdff / JZ` on
// get_type_bits (.text:06d0c6dc .. 06d0c6e7), then MetaList::append on +0xd4
// inlined (.text:06d0c6e9 .. 06d0c719).
void StarSystem::add_to_list_d4(GameObject* Obj)
{
    if ((Obj->get_type_bits() & 0xfdffu) == 0u)
        return;
    ListD4.append(Obj);
}

// FUN_06d0c730 (.text:06d0c730, RET 4). The search is `node ? [node] : [+0xd8]`
// comparing node+8 with Obj (.text:06d0c74d); the first match is unlinked and
// freed by the inlined MetaList::remove (.text:06d0c752 .. 06d0c7c5). No match:
// nothing.
void StarSystem::remove_from_list_d4(GameObject* Obj)
{
    MetaNode<GameObject>* Node = nullptr;
    while (ListD4.traverse(Node, true))
    {
        if (Node->Data == Obj)
        {
            ListD4.remove(Node);
            return;
        }
    }
}

// ===========================================================================
// StarSystem::find_solar -- retail FUN_06d0c7e0 (.text:06d0c7e0).
// See the declaration in server_sysdb.h for the instruction-level map.
// ===========================================================================
GameObject* StarSystem::find_solar(unsigned int const* NicknameId) const
{
    GameObject* Found = nullptr;                                   // XOR EAX,EAX
    FSystemListNode* Node = nullptr;                               // XOR EDI,EDI
    for (;;) {
        // .text:06d0c7f0 .. .text:06d0c7f9
        Node = (Node == nullptr) ? Solars.Head : Node->Next;    // [this+0x60]
        // .text:06d0c7fb / .text:06d0c7ff
        if (Node == nullptr || Found != nullptr)
            break;
        GameObject* const Obj = Node->Data;
        // .text:06d0c80a .. .text:06d0c827 -- [Data+0x10] is the CBase sub-object's
        // Object (CBase at +0x0c, Object at CBase+0x04), which cobject() returns.
        CObject* const Raw = const_cast<CObject*>(Obj->cobject());
        CSolar* Solar = nullptr;
        if (Raw != nullptr &&
            (static_cast<unsigned int>(Raw->ObjectClass) & 0x303u) == 0x303u)
            Solar = static_cast<CSolar*>(Raw);
        // .text:06d0c82b -- retail reads +0x1a4 even off the zeroed pointer;
        // the solar block never holds anything else.
        if (Solar->Nickname.Id == *NicknameId)
            Found = Obj;                                           // MOV EAX,ESI
    }
    return Found;
}

// ===========================================================================
// StarSystem::on_object_destroyed -- retail FUN_06d0b160 (.text:06d0b160).
// See the declaration in server_sysdb.h for the instruction-level map.
// ===========================================================================
void StarSystem::on_object_destroyed(GameObject* Obj, int Kind)
{
    // (w4-server-guards) No null test on Obj: .text:06d0b16a
    // `MOV EAX,[ESI + 0x10]` is the first read.

    // .text:06d0b16a .. .text:06d0b189 -- the CBase::Object / (ObjectClass & 3)
    // narrowing, then CSimple::OwnerPlayer at +0xb4. Retail reads +0xb4 through
    // the nulled pointer (.text:06d0b182 `XOR EAX,EAX` / 06d0b189
    // `MOV EDI,[EAX+0xb4]`) exactly as StarSystem::remove_player does; the port
    // does not guard it either.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;

    // .text:06d0b18f  TEST EDI,EDI / JZ LAB_06d0b237 -- skip the whole player
    // half when the object belongs to nobody.
    if (SessionId != 0)
    {
        // .text:06d0b19f  CALL [EDX + 0x20] -- the object's own id, computed
        // BEFORE the slot lookup and kept in EBX across it.
        unsigned int const ObjId = Obj->get_id();

        // .text:06d0b1a9 .. .text:06d0b1ae -- PlayerDB::is_valid(&Players, &id).
        PlayerSlot* const Slot = PlayerDB_GetPlayer(SessionId);

        // .text:06d0b1c4  CMP dword ptr [ESI + EAX*0x1 + -0x34],EBX
        // -- only when the destroyed object IS the session's current ship.
        if (Slot != nullptr && Slot->ship_object_id == ObjId)
        {
            // .text:06d0b1d1 -- PlayerData::terminate_trade.
            PlayerData_TerminateTrade(*Slot);
            // .text:06d0b1dc  PUSH 0x1 -- the bool argument the decompilation
            // loses as `unaff_BL`; retail asks for the partner's inbound entry
            // to be dropped too.
            PlayerData_CancelTradeRequest(*Slot, /*bDropPartnerEntry*/true);
            // .text:06d0b1ea  CMP EBP,0x1 / JNZ -- only Kind == 1 zeroes the
            // health. PUSH 0 / PUSH 0, i.e. (0.0f, bNotify = false).
            if (Kind == 1)
                PlayerData_SetRelativeHealth(*Slot, 0.0f, /*bNotify*/false);
            // .text:06d0b20a  MOV dword ptr [ESI + EAX*0x1 + -0x34],0x0
            Slot->ship_object_id = 0;
            // .text:06d0b21c/.text:06d0b227 -- a two-dword stack block
            // { slot+0x3E0, sessionId } passed to FUN_06d43650 with id 0x37.
            // +0x3E0 is 0x418 - 0x38, the slot's system id.
            unsigned int Payload[2];
            Payload[0] = Slot->system_id;
            Payload[1] = SessionId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::SpaceExit, Payload);
        }
    }

    // .text:06d0b237 .. .text:06d0b254 -- and then, unconditionally, every
    // player currently in this system is told about it:
    //   MOV  EAX,[EBX + 0xc]      ; the player list sentinel
    //   MOV  ESI,[EAX]            ; first node
    //   LEA  ECX,[ESI + 0x8]      ; the EMBEDDED per-player element
    //   PUSH EBP (Kind) / PUSH Obj / CALL FUN_06cf6820
    //
    // The element, the tracker AND the insert are all real now:
    // StarSystem::add_player (retail FUN_06d07e20) pushes the node, this walk
    // visits it and StarSystem::remove_player takes it off again.
    {
        // (w4-server-guards) No null test on the sentinel or on a node:
        // 06d0b237 `MOV EAX,[EBX+0xc]` / 06d0b23a `MOV ESI,[EAX]` /
        // 06d0b23c `CMP ESI,EAX`, and 06d0b251 `CMP ESI,[EBX+0xc]` -- the walk
        // ends only at the sentinel.
        // The next node is read AFTER the call (06d0b24f `MOV ESI,[ESI]`) and
        // the sentinel is re-read from +0xc on every step (06d0b251).
        for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
             Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next) {
            // .text:06d0b24a  LEA ECX,[ESI+0x8] -- the EMBEDDED element.
            SystemPlayerView_OnObjectDestroyed(&Node->View, Obj, Kind);
        }
    }
}

// ===========================================================================
// StarSystem::add_cargo -- retail FUN_06d0e8d0 (.text:06d0e8d0, RET 0x14).
//
// Stack map, so the argument order below can be checked without re-deriving it:
// after `SUB ESP,0x58` plus PUSH EBX/EBP/ESI/EDI the incoming arguments sit at
// [ESP+0x6c] (Obj), [ESP+0x70] (ArchId), [ESP+0x74] (Count), [ESP+0x78]
// (Health) and [ESP+0x7c] (the bool) -- which is why .text:06d0e937 reads
// [ESP+0x70] to compare against the equip's archetype id and .text:06d0e8f9
// reads [ESP+0x7c] as the byte flag.
// ===========================================================================
// Retail FUN_06ce6870 (.text:06ce6870), the narrowing both cargo paths use:
//   MOV EAX,[ECX + 0x10]                 ; CBase::Object
//   TEST EAX,EAX / JZ null
//   MOV ECX,[EAX + 0x4c] / AND ECX,0x103 / CMP ECX,0x103 / JZ ok
//   null: XOR EAX,EAX
// It is a FREE function on the raw object pointer in retail, which is why
// .text:06d0e903 and .text:06d0ec06 can call it with a plain GameObject* in
// ECX. This port's EqObj::ceqobj() is the same test as a member, so it cannot
// be reached from a GameObject*; this is that member's free-function twin.
static CEqObj* GameObject_ceqobj(GameObject* Obj)
{
    CObject* const Raw = (Obj != nullptr) ? Obj->cobj() : nullptr;
    if (Raw == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Raw->ObjectClass) & 0x103u) != 0x103u)
        return nullptr;
    return static_cast<CEqObj*>(Raw);
}

unsigned short StarSystem::add_cargo(GameObject* Obj, unsigned int ArchId,
                                     unsigned int Count, float Health, bool bMission)
{
    // No null test on Obj: retail's first use is .text:06d0e903 / .text:06d0e953
    // `MOV EAX,[EDI + 0x10]` straight through the argument.
    CECargo* Stack = nullptr;
    bool bCombined = false;

    // .text:06d0e8e8  CALL [->COMMON.DLL::arch_is_combinable] / TEST AL,AL
    // .text:06d0e8f9  MOV AL,[ESP+0x7c] / TEST AL,AL / JNZ -- the flag SUPPRESSES
    // the combine, so an item marked with it always becomes its own entry.
    if (arch_is_combinable(ArchId) && !bMission)
    {
        // .text:06d0e903 FUN_06ce6870, then .text:06d0e923 `LEA EBP,[ESI+0xe4]`
        // with no test of the result in between.
        CEqObj* const EqObj = GameObject_ceqobj(Obj);
        {
            // .text:06d0e908  PUSH 0x10000 -- EquipTypeCargo, nothing else.
            CEquipTraverser Walk(EquipTypeCargo);
            CEquipManager& Mgr = EqObj->EquipManager;  // .text:06d0e923 LEA EBP,[ESI+0xe4]
            for (CEquip* Item = Mgr.Traverse(Walk); Item != nullptr;
                 Item = Mgr.Traverse(Walk))
            {
                // .text:06d0e934  MOV EDX,[ESI+0xc] / CMP [EDX+8],EAX
                // CEquip+0x0c is Arch and Archetype::Root+0x08 is Nickname,
                // whose Id is what get_id() returns.
                // .text:06d0e934 MOV EDX,[ESI+0xc] / .text:06d0e93b CMP [EDX+0x8],EAX
                // -- no test of Arch in between.
                if (Item->Arch->get_id() != ArchId)
                    continue;

                // .text:06d0e9df  MOV EAX,[ESI+0x28] -- CECargo::Count. Retail
                // uses the traverser's own 0x10000 mask as the downcast proof
                // and reads +0x28 with no Cast call at all; the port goes
                // through CECargo::cast, which cannot disagree for that mask.
                CECargo* const Cargo = CECargo::cast(Item);
                if (Cargo != nullptr && Cargo->Count != 0)
                {
                    // .text:06d0e9f2  CECargo::AddToStack(Count, Health)
                    Cargo->AddToStack(Count, Health);
                    Stack = Cargo;
                    bCombined = true;
                }
                else
                {
                    // .text:06d0ea81  CEquipManager::CleanUp, then fall out of
                    // the combine block into the "make a new one" path below.
                    Mgr.CleanUp();
                }
                break;
            }
        }
    }

    // The owning player's slot, resolved the same way on both paths
    // (.text:06d0e953 and .text:06d0e9fc): CBase::Object under the
    // (ObjectClass & 3) == 3 guard, CSimple::OwnerPlayer at +0xb4, the slot's
    // client id at +0x348 non-zero, and the slot's ship object id (+0x3E4)
    // equal to this object's own get_id().
    // .text:06d0e965 XOR EAX,EAX on the failed guard, then .text:06d0e969
    // `MOV ESI,[EAX + 0xb4]` through it -- retail reads OwnerPlayer unguarded.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    PlayerSlot* Slot = (SessionId != 0) ? PlayerDB_GetPlayer(SessionId) : nullptr;
    if (Slot != nullptr && (Slot->client_id == 0 || Slot->ship_object_id != ObjId))
        Slot = nullptr;

    if (bCombined)
    {
        // .text:06d0e9c1 -- FUN_06d4f230(slot, ArchId, Count, Health, 0, 0).
        // That forwarder is PlayerData::add_equipment with the cargo-bay
        // hardpoint baked in: .text:06d4f230 pushes, right to left,
        // (arg1, EquipDesc::CARGO_BAY_HP_NAME, arg2, arg3, 0, arg4, arg5) into
        // FUN_06d4efc0. Note the combine path forces BOTH trailing bytes to
        // zero -- bNoCombine and bNotify alike.
        if (Slot != nullptr)
        {
            PlayerData_AddEquipment(*Slot, ArchId, EquipDesc::CARGO_BAY_HP_NAME,
                                    (int)Count, Health, /*bEquipped*/false,
                                    /*bNoCombine*/false, /*bNotify*/false);
        }
    }
    else
    {
        // .text:06d0e9c1 on the other side of the branch:
        // FUN_06d4f230(slot, ArchId, Count, Health, bMission, 0) -- here the
        // public bool DOES travel on as bNoCombine.
        unsigned short EquipId = SUBOBJ_ID_NONE;
        if (Slot != nullptr)
        {
            EquipId = PlayerData_AddEquipment(*Slot, ArchId,
                                              EquipDesc::CARGO_BAY_HP_NAME,
                                              (int)Count, Health, /*bEquipped*/false,
                                              /*bNoCombine*/bMission, /*bNotify*/false);
        }
        else
        {
            // .text:06d0ea8e  MOV ECX,EDI / .text:06d0ea90 CALL FUN_06d01c30 --
            // Ship::allocate_equip_id straight on the GameObject*, reached from
            // all four slot-lookup misses (.text:06d0e97e/98f/9a9/9bb) with no
            // class test: retail assumes anything reaching here is a Ship.
            EquipId = static_cast<Ship*>(Obj)->allocate_equip_id();
        }

        // .text:06d0ea95 .. .text:06d0eae9 -- the EquipDesc retail hands to
        // CEqObj::add_item, built in retail's own order.
        EquipDesc Desc;
        Desc.set_id(EquipId);
        Desc.set_arch_id(ArchId);
        Desc.set_count((int)Count);
        Desc.set_status(Health);
        Desc.set_equipped(false);
        // .text:06d0eadd  MOV CL,byte ptr [ESP+0x7c]
        // .text:06d0eae1  MOV byte ptr [ESP+0x40],CL
        // [ESP+0x40] is EquipDesc+0x18 (the descriptor sits at [ESP+0x28] in the
        // same frame), and EquipDesc+0x18 is bMission. So the in-space path DOES
        // put pub::Player::AddCargo's fifth argument into EquipDesc::bMission --
        // by a direct byte store, which is why Server.dll imports no setter for
        // it and why an earlier note concluded the field was never written.
        Desc.bMission = bMission;
        // .text:06d0eae9  EquipDesc::make_internal
        Desc.make_internal();

        // .text:06d0eaff  CALL dword ptr [EDX + 0xb0] on the CEqObj.
        // ??_7CEqObj@@6B@ is Common.dll .rdata:0639be5c, so +0xb0 is
        // .rdata:0639bf0c == CEqObj::add_item.
        // .text:06d0eaf1 FUN_06ce6870, .text:06d0eaff CALL [EDX + 0xb0] -- no
        // test of the narrowed pointer, and none again at .text:06d0eb0c.
        CEqObj* const EqObj = GameObject_ceqobj(Obj);
        if (EqObj->add_item(Desc))
        {
            // .text:06d0eb19/.text:06d0eb20 -- FindByID then CECargo::cast.
            Stack = CECargo::cast(EqObj->EquipManager.FindByID(EquipId));
        }
    }

    // .text:06d0eb2d .. .text:06d0ebc2 -- the broadcast. A SECOND, freshly
    // constructed EquipDesc is filled by the cargo equip's own
    // GetEquipDesc (vtable +0x28 on ??_7CECargo@@6B@, Common.dll
    // .rdata:0639cd4c + 0x28 == .text:CECargo::GetEquipDesc), and when either
    // the cargo or that call is missing retail FDUMPs
    //   "StarSys::add_cargo().  Failed to get client cargo description."
    // and sends nothing.
    EquipDesc Wire;
    // .text:06d0eb3d / .text:06d0eb4e -- either miss jumps to the FDUMP at
    // .text:06d0eba0 (level 0x100001, StarSys.cpp line 0xd8e) and sends nothing.
    bool const bHaveDesc = (Stack != nullptr) && Stack->GetEquipDesc(Wire);
    if (bHaveDesc)
    {
        // .text:06d0eb50 .. .text:06d0eb97 -- for every player in this system
        // that can currently see the object, IClient slot 86 (+0x158) with
        // (node+0x40, Obj->get_id(), &Wire). The visibility test is
        // FUN_06cf73d0 on the node's EMBEDDED element at +0x08.
        // StarSystem::add_player fills the list now, so this walk has
        // something to visit.
        // .text:06d0eb54 MOV EAX,[EBX+0xc] / MOV ESI,[EAX] / CMP ESI,EAX, and
        // .text:06d0eb92 MOV ESI,[ESI] / CMP ESI,[EBX+0xc]: head->next until the
        // sentinel, no null tests, and DAT_06d8fb14 is loaded untested
        // (.text:06d0eb6d).
        SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
        {
            for (SystemPlayerNode* Node = Sentinel->Next;
                 Node != Sentinel; Node = Node->Next) {
                if (!SystemPlayerView_IsObjectVisible(&Node->View, Obj))
                    continue;
                // CITATION CORRECTED 2026-09-17 (wave 13): the CALL is at
                // .text:06d0eb8c, not .text:06d0eb8a -- that address is not an
                // instruction start at all. The whole sequence:
                //   .text:06d0eb7b  52            PUSH EDX            ; the EquipDesc
                //   .text:06d0eb7e  ff5020        CALL [EAX + 0x20]   ; get_id()
                //   .text:06d0eb81  50            PUSH EAX
                //   .text:06d0eb82  8d4e40        LEA  ECX,[ESI + 0x40]
                //   .text:06d0eb85  51            PUSH ECX
                //   .text:06d0eb8c  ff9558010000  CALL [EBP + 0x158]  ; slot 86
                // The first argument really is the ADDRESS of the id word, and
                // node+0x40 is element+0x38.
                g_pClientInterface->SendServerObjectCargoUpdate(Node->View.PlayerId,
                                                                Obj->get_id(), Wire);
            }
        }
    } else {
        // .text:06d0eba0 .. .text:06d0ebc2 -- FDUMP(0x100001, .rdata:06d655d8,
        // .rdata:06d67d10 = StarSys.cpp, 0xd8e, .rdata:06d682cc), through the
        // import with no null test.
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0xd8e,
              "StarSys::add_cargo().  Failed to get client cargo description.");
    }

    // .text:06d0ebc9  MOV AX,word ptr [EBP + 0x8] -- CEquip::Id, or
    // .text:06d0ebd7  *SUBOBJ_ID_NONE when nothing was created.
    return (Stack != nullptr) ? Stack->Id : SUBOBJ_ID_NONE;
}

// ===========================================================================
// StarSystem::remove_cargo -- retail FUN_06d0ebf0 (.text:06d0ebf0, RET 0x0C).
// ===========================================================================
void StarSystem::remove_cargo(GameObject* Obj, unsigned short EquipId, int Count)
{
    // .text:06d0ec03 .. .text:06d0ec1b -- FUN_06ce6870 (the & 0x103 narrowing),
    // then EquipManager (+0xe4).FindByID(id) with no test on the narrowing; only
    // FindByID's result is tested.
    CEqObj* const EqObj = GameObject_ceqobj(Obj);
    CEquip* const Item = EqObj->EquipManager.FindByID(EquipId);
    if (Item == nullptr)
        return;

    // .text:06d0ec22  CECargo::cast -- unlike add_cargo, the remove path really
    // does call it, because FindByID is not filtered by type.
    CECargo* const Cargo = CECargo::cast(Item);
    if (Cargo == nullptr)                                           // .text:06d0ec2f
        return;
    // .text:06d0ec35  MOV EAX,[ESI+0x28] / TEST / JZ -- an empty stack is left
    // alone entirely.
    if (Cargo->Count == 0)
        return;

    // .text:06d0ec40 .. .text:06d0ec53 -- one EquipDesc (EquipDesc::EquipDesc),
    // first filled from the cargo (CECargo vtable +0x28 == GetEquipDesc)...
    EquipDesc Wire;
    Cargo->GetEquipDesc(Wire);

    // .text:06d0ec5a  CECargo::RemoveFromStack(Count). The decompilation passes
    // param_2 here; the raw bytes load [ESP+0x40], the THIRD argument, at
    // .text:06d0ec56.
    Cargo->RemoveFromStack((unsigned)Count);

    // ...and then patched with what is left (.text:06d0ec63 set_count from the
    // live +0x28, .text:06d0ec75 set_status from CECargo vtable +0x44, which
    // ??_7CECargo@@6B@ resolves to CEquip::GetRelativeHealth).
    Wire.set_count((int)Cargo->Count);
    Wire.set_status((float)Cargo->GetRelativeHealth());           // .text:06d0ec7d FSTP float

    // .text:06d0ec86 .. .text:06d0ece6 -- the (& 3) narrowing, not null-tested,
    // for OwnerPlayer (+0xb4); get_id() (vt +0x20) before the test; then
    // PlayerDB::is_valid_ship_owner inlined (owner != 0, owner - 1 below
    // DAT_06d90264, client_id +0x348 set, ship +0x3e4 == id) and
    // FUN_06d4f100(slot, EquipId, Count, 0) == PlayerData_RemoveEquipment with
    // bNotify clear.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    if (SessionId != 0u && Players.is_valid_ship_owner(SessionId, ObjId))
        PlayerData_RemoveEquipment(Players[SessionId], EquipId, Count, /*bNotify*/false);

    // .text:06d0eceb .. .text:06d0ed2e -- every player node (sentinel re-read
    // from +0xc each step), gated on FUN_06cf73d0 (the object visibility test),
    // IClient slot 86 (+0x158).
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next) {
        // .text:06d0ecf9  LEA ECX,[ESI+0x8] / CALL FUN_06cf73d0.
        if (!SystemPlayerView_IsObjectVisible(&Node->View, Obj))
            continue;
        // .text:06d0ed12 .. .text:06d0ed23 -- three arguments, pushed across the
        // get_id call: (&element.PlayerId, Obj->get_id(), &Wire). The `PUSH ECX`
        // at .text:06d0ed12 is the THIRD argument going down early, not an
        // argument to get_id; `LEA EAX,[ESI+0x40]` hands over the ADDRESS.
        g_pClientInterface->SendServerObjectCargoUpdate(Node->View.PlayerId, Obj->get_id(), Wire);
    }
}

// ===========================================================================
// StarSystem::unload — retail FUN_06d072c0(this), 0x06d072c0. Run only on the
// single-player path, after event 0x3d, when the last player leaves a system
// (FUN_06d07130 .text:06d07161, FUN_06d087f0 .text:06d0887d, FUN_06d08890
// .text:06d0892d). See the declaration for the whole order; traced against the
// listing 2026-09-24 (claude-code:w3-starsystem-iobjdb).
// ===========================================================================

// Retail's per-object disposal, the same four instructions at every site
// (.text:06d072f7 .. 06d07306 and its siblings):
//   MOV ECX,[EAX + 0x8] / TEST / JZ  -- node->Data, skipped when null
//   MOV EDX,[ECX + 0xc] / ADD ECX,0xc / PUSH 1 / CALL [EDX]
// i.e. the scalar deleting destructor of the wrapper's CBase sub-object at
// +0x0c (IObjInspectImpl's CBase, server_gameobject.h), which destroys and
// frees the whole wrapper.
static void StarSystem_DeleteObject(GameObject* Obj)
{
    delete static_cast<CBase*>(Obj);
}

// The two passes retail runs over one block (for +0x34 at .text:06d072dc ..
// 06d07335; +0x48, +0x70, +0x84, +0x98, +0xac repeat the shape verbatim, and
// FUN_06d07570 / FUN_06d075f0 do the same for +0x5c / +0xc0):
//   pass 1: node = node ? node->Next : Head; delete node->Data when non-null;
//   pass 2: node = Head; while (node) { next = node->Next;
//           FUN_06d0f970(node) (MetaList::free_node); node = next; }
//           then Tail = 0, Head = 0 (.text:06d07332 / 06d07335).
// Pass 1 does not clear Data; pass 2 never looks at it.
static void StarSystem_DeleteBlock(ObjectList& List)
{
    FSystemListNode* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? List.Head : Node->Next;
        if (Node == nullptr)
            break;
        if (Node->Data != nullptr)
            StarSystem_DeleteObject(Node->Data);
    }

    Node = List.Head;
    while (Node != nullptr) {
        FSystemListNode* const Current = Node;
        Node = Node->Next;
        List.free_node(Current);
    }
    List.Tail = nullptr;
    List.Head = nullptr;
    List.invalidate_index(); // PORT(perf-index): the id index (ObjectList::find_by_id)
}

void StarSystem::unload()
{
    // .text:06d072c7 .. 06d072d7 -- FUN_06d0f390: std::list::erase(begin(),
    // end()) on the player list at +0x08. Per node (.text:06d0f3a2 ..
    // 06d0f3cd): unlink, the view's own destructor through its vtable slot 0
    // with flag 0, operator delete on the node, --size.
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    SystemPlayerNode* View = Sentinel->Next;
    while (View != Sentinel) {
        SystemPlayerNode* const Node = View;
        View = View->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        // .text:06d0f3b2 LEA ECX,[ESI+0x8] / MOV EDX,[ECX] / PUSH 0 /
        // .text:06d0f3bc CALL [EDX] -- vptr slot 0, flag 0.
        Observer* const Element = &Node->View;
        Element->~Observer();
        ::operator delete(Node);
        --PlayerListSize;
    }

    // .text:06d072dc .. .text:06d074ff -- six blocks in this order.
    StarSystem_DeleteBlock(Ships);     // +0x34
    StarSystem_DeleteBlock(Loot);      // +0x48
    StarSystem_DeleteBlock(List70);    // +0x70
    StarSystem_DeleteBlock(Beams);     // +0x84
    StarSystem_DeleteBlock(List98);    // +0x98
    StarSystem_DeleteBlock(ListAC);    // +0xac

    // .text:06d074f0 .. .text:06d0753b -- +0xe8: a forward traverse
    // (FUN_06d16120(block, &it, 1)) deleting every non-null Data, then
    // MetaList::free (FUN_06d17170).
    {
        FSystemListNode* Node = nullptr;
        while (Deferred.traverse(Node, true)) {
            if (Node->Data != nullptr)
                StarSystem_DeleteObject(Node->Data);
        }
        Deferred.free();
    }

    // .text:06d07542 -- FUN_06d07570: the solars, then the partition.
    StarSystem_DeleteBlock(Solars);                                // .text:06d07576 .. 06d075c9
    if (Spatial != nullptr) {                                      // .text:06d075d0 / 06d075d5
        FreeSpatial(Spatial);                                      // .text:06d075d8
        Spatial = nullptr;                                         // .text:06d075e1
    }

    // .text:06d07549 -- FUN_06d075f0. SystemInfo->vtable[0]() is the system
    // id (this port's flat ISystem keeps it in NicknameId, common_universe.h).
    unsigned int const SystemId = SystemInfo->NicknameId;
    // .text:06d075fd FUN_06d03410(sysId): drop one reference on this system's
    // SrvAsteroid::SrvAsteroidSystem (the map at DAT_06d8d980, erased at zero).
    // The return value is dropped (no test of EAX before .text:06d0760a).
    SrvAsteroid::SrvAsteroid_UnloadSystem(SystemId);               // .text:06d075fd
    CmnAsteroid::Unload(SystemId);                                 // .text:06d0760d
    StarSystem_DeleteBlock(ListC0);                                // .text:06d07616 .. 06d07680

    // .text:06d0754e / .text:06d07551 -- FUN_06d0f2f0 on +0x24: erase(begin,
    // end) of a vector of 0xC-byte PODs, i.e. _Last = _First.
    VectorEnd = VectorBegin;

    // .text:06d07556 .. .text:06d0755e -- the id again, PhySys::Shutdown(id).
    PhySys::Shutdown(SystemInfo->NicknameId);
}

// ===========================================================================
// StarSystem's constructors, destructor and SysDB init.
// ===========================================================================

// FUN_06d0f3e0(0, 0) (.text:06d0f3e0): operator new(0x50) with both links on
// the node itself -- the player list's sentinel. 0x50 is a full node
// (SystemPlayerNode): MSVC6's _Buynode allocates the value too.
static void* StarSystem_NewPlayerSentinel()
{
    SystemPlayerNode* const Node =
        static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
    Node->Next = Node;
    Node->Prev = Node;
    return Node;
}

// .text:06d06d26 .. 06d06d36 (constructor) and FUN_06d10aa0 (copy): operator
// new(0x20), self-linked -- the dock queue's sentinel.
static void* StarSystem_NewDockSentinel()
{
    SystemDockNode* const Node =
        static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
    Node->Next = Node;
    Node->Prev = Node;
    return Node;
}

// Retail's node is 0x50 bytes. The port's Observer embeds a real
// std::set whose size depends on the CRT build (server_sysdb.h), so the port
// allocates sizeof(SystemPlayerNode) rather than asserting 0x50.

// FUN_06d06ce0 (.text:06d06ce0).
//   06d06d08  vptr = IObjDB (the base-class constructor, inlined)
//   06d06d15  +0x08 allocator byte; 06d06d17 FUN_06d0f3e0(0,0) -> +0x0c; +0x10 = 0
//   06d06d2c  +0x14 allocator byte; 06d06d2f operator new(0x20) self-linked
//             -> +0x18; +0x1c = 0
//   06d06d46  +0x20 = 0; 06d06d49 +0x24 allocator byte; +0x28/+0x2c/+0x30 = 0
//   06d06d55 .. 06d06e16  the ten ObjectLists, each bUseHeap = 1 (ObjectList())
//   06d06e20  +0xfc = 0
//   06d06e26  vptr = StarSystem
// The allocator bytes are copies of an uninitialised stack byte (MSVC6's
// empty std::allocator) and carry no value; +0x04 is not written at all --
// FUN_06d06ea0 sets it on the map's copy before anything reads it. The port
// writes zero to all of them rather than copy indeterminate memory.
StarSystem::StarSystem()
    : SystemInfo(nullptr),
      ListAlloc(0),
      PlayerListHead(StarSystem_NewPlayerSentinel()),
      PlayerListSize(0),
      ObjectListAlloc(0),
      ObjectListHead(StarSystem_NewDockSentinel()),
      ObjectListSize(0),
      Spatial(nullptr),
      VectorAlloc(0),
      VectorBegin(nullptr),
      VectorEnd(nullptr),
      VectorCapacity(nullptr),
      bLoaded(0)
{
    Pad1[0] = Pad1[1] = Pad1[2] = 0;
    Pad2[0] = Pad2[1] = Pad2[2] = 0;
    Pad3[0] = Pad3[1] = Pad3[2] = 0;
    Pad4[0] = Pad4[1] = Pad4[2] = 0;
}

// FUN_06d10520 (.text:06d10520, `RET 0x4`); FUN_06d11330 inlines the same body
// for the map node.
//   06d10542  vptr = IObjDB
//   06d10548  +0x04 = Other+0x04
//   06d1054e  +0x08 allocator byte copied; 06d10560 new sentinel; +0x10 = 0;
//   06d10590  FUN_06d109c0: insert(end, Other.begin, Other.end) -- a
//             push_back of a COPY (FUN_06d0f6d0) of every player view
//   06d10595  the same for the dock queue: new 0x20 sentinel (FUN_06d10aa0),
//   06d105d3  FUN_06d10a30: push_back of a copy (FUN_06d0f830) of each entry
//   06d105d8  +0x20 = Other+0x20 (the partition POINTER, shared)
//   06d105de  +0x24 allocator byte; 06d105e6 .. 06d10634 vector copy: n = 0
//             when Other._First is null, else (_Last - _First) / 0xC, clamped
//             at 0; operator new(n * 0xC) -- even for n == 0 --, the elements
//             copied (FUN_06d10ad0), _Last = _End = the end of the copy
//   06d10637 .. 06d1072c  the ten ObjectLists EMPTY, bUseHeap = 0
//             (ObjectList(ObjectList const&) -- nothing of Other's is copied)
//   06d10736  +0xfc = Other+0xfc
//   06d1073d  vptr = StarSystem
StarSystem::StarSystem(StarSystem const& Other)
    : IObjDB(),
      SystemInfo(Other.SystemInfo),
      ListAlloc(Other.ListAlloc),
      PlayerListHead(StarSystem_NewPlayerSentinel()),
      PlayerListSize(0),
      ObjectListAlloc(Other.ObjectListAlloc),
      ObjectListHead(StarSystem_NewDockSentinel()),
      ObjectListSize(0),
      Spatial(Other.Spatial),
      VectorAlloc(Other.VectorAlloc),
      VectorBegin(nullptr),
      VectorEnd(nullptr),
      VectorCapacity(nullptr),
      Ships(Other.Ships),
      Loot(Other.Loot),
      Solars(Other.Solars),
      List70(Other.List70),
      Beams(Other.Beams),
      List98(Other.List98),
      ListAC(Other.ListAC),
      ListC0(Other.ListC0),
      ListD4(Other.ListD4),
      Deferred(Other.Deferred),
      bLoaded(Other.bLoaded)
{
    Pad1[0] = Pad1[1] = Pad1[2] = 0;
    Pad2[0] = Pad2[1] = Pad2[2] = 0;
    Pad3[0] = Pad3[1] = Pad3[2] = 0;
    Pad4[0] = Pad4[1] = Pad4[2] = 0;

    // FUN_06d109c0 (.text:06d109e0 .. 06d10a21): for each source node,
    // operator new(0x50), Next = end, Prev = end->Prev (or itself), end->Prev
    // = node, node->Prev->Next = node, the element copy-constructed at node+8,
    // ++size.
    {
        SystemPlayerNode* const End = static_cast<SystemPlayerNode*>(PlayerListHead);
        SystemPlayerNode* const SrcEnd = static_cast<SystemPlayerNode*>(Other.PlayerListHead);
        for (SystemPlayerNode* Src = SrcEnd->Next; Src != SrcEnd; Src = Src->Next) {
            SystemPlayerNode* const Node =
                static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
            Node->Next = End;
            Node->Prev = (End->Prev != nullptr) ? End->Prev : Node;
            End->Prev = Node;
            Node->Prev->Next = Node;
            ::new (static_cast<void*>(&Node->View)) Observer(Src->View);   // FUN_06d0f6d0
            ++PlayerListSize;
        }
    }

    // FUN_06d10a30 (.text:06d10a50 .. 06d10a91): the same over 0x20-byte nodes
    // with FUN_06d0f830, FSystemDockEntry's copy constructor.
    {
        SystemDockNode* const End = static_cast<SystemDockNode*>(ObjectListHead);
        SystemDockNode* const SrcEnd = static_cast<SystemDockNode*>(Other.ObjectListHead);
        for (SystemDockNode* Src = SrcEnd->Next; Src != SrcEnd; Src = Src->Next) {
            SystemDockNode* const Node =
                static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
            Node->Next = End;
            Node->Prev = (End->Prev != nullptr) ? End->Prev : Node;
            End->Prev = Node;
            Node->Prev->Next = Node;
            ::new (static_cast<void*>(&Node->Entry)) FSystemDockEntry(Src->Entry);
            ++ObjectListSize;
        }
    }

    // .text:06d105e6 .. 06d10634 -- the 0xC-byte-element vector.
    int Count = 0;
    if (Other.VectorBegin != nullptr) {
        Count = static_cast<int>(
            (static_cast<char const*>(Other.VectorEnd) -
             static_cast<char const*>(Other.VectorBegin)) / 0xC);
    }
    if (Count < 0)
        Count = 0;
    VectorBegin = ::operator new(static_cast<size_t>(Count) * 0xC);
    size_t const Bytes = static_cast<size_t>(
        static_cast<char const*>(Other.VectorEnd) - static_cast<char const*>(Other.VectorBegin));
    if (Bytes != 0)
        memcpy(VectorBegin, Other.VectorBegin, Bytes);
    VectorEnd = static_cast<char*>(VectorBegin) + Bytes;
    VectorCapacity = VectorEnd;
}

// FUN_06d10030 (.text:06d10030), from `LEA ESI,[ECX+0x4]` on -- the pair's
// destructor is this one inlined. In order:
//   06d10053 .. 06d1017b  +0xe8 down to +0x34: each ObjectList's destructor
//                         (vptr MetaList, MetaList::free, MetaList::free_heap)
//                         -- the members' own destructors, reverse order
//   06d10180 .. 06d1019b  operator delete(+0x28); +0x28/+0x2c/+0x30 = 0
//   06d1019e .. 06d101cf  the dock queue: FUN_06d0f410 erase(begin, end),
//                         operator delete on the sentinel, +0x18 = +0x1c = 0
//   06d101da              FUN_06d0f230: the player list -- every view's
//                         destructor (slot 0, flag 0) and node freed, then the
//                         sentinel, +0x0c = +0x10 = 0
// Nothing deletes the objects the ObjectLists point at, and +0x20 (the
// partition) is not freed here: unload() does both.
StarSystem::~StarSystem()
{
    ::operator delete(VectorBegin);
    VectorBegin = nullptr;
    VectorEnd = nullptr;
    VectorCapacity = nullptr;

    SystemDockNode* const DockHead = static_cast<SystemDockNode*>(ObjectListHead);
    erase_docks(DockHead->Next, DockHead);
    ::operator delete(ObjectListHead);
    ObjectListHead = nullptr;
    ObjectListSize = 0;

    // FUN_06d0f230 (.text:06d0f240 .. 06d0f284).
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    SystemPlayerNode* Node = Sentinel->Next;
    while (Node != Sentinel) {
        SystemPlayerNode* const Current = Node;
        Node = Node->Next;
        Current->Prev->Next = Current->Next;
        Current->Next->Prev = Current->Prev;
        // .text:06d0f250 LEA ECX,[ESI+0x8] / MOV EDX,[ECX] / PUSH 0 /
        // .text:06d0f25a CALL [EDX] -- vptr slot 0, flag 0.
        Observer* const Element = &Current->View;
        Element->~Observer();
        ::operator delete(Current);
        --PlayerListSize;
    }
    ::operator delete(PlayerListHead);
    PlayerListHead = nullptr;
    PlayerListSize = 0;
    // The ten ObjectLists are destroyed after this body, +0xe8 first. Retail
    // runs them BEFORE the vector/dock/player teardown (.text:06d10053 ..
    // 06d1017b come first). The order is not observable: a block's destructor
    // frees only its own nodes and never touches the objects, the vector or
    // either std::list.
}

// FUN_06d06ea0 (.text:06d06ea0, `RET 0x4`).
//   06d06ea8  +0x04 = COMMON.DLL::Universe::get_system(SystemId)
//   06d06eb4  if (!SinglePlayer())
//   06d06ebf    COMMON.DLL::PhySys::m_pCollisionFilter = LAB_06d06c30
// LAB_06d06c30 (.text:06d06c30 .. 06d06cc9, cdecl, plain RET) -- the
// multiplayer collision filter StarSystem::init installs into
// COMMON.DLL::PhySys::m_pCollisionFilter. Every branch, from the bytes:
//   06d06c32  SinglePlayer() -> TRUE
//   06d06c40  A == null -> TRUE;  06d06c48  B == null -> TRUE
//   06d06c50  ClientA = A->client_data (+0xa4); when it is null and A's +0x1c
//             is not, TRUE (06d06c5a .. 06d06c5f)
//   06d06c61  the same for B (06d06c6b .. 06d06c70)
//   06d06c7d  GetRoot(ClientA) when ClientA != null -- the result is
//             discarded: ESI still holds client_data afterwards
//   06d06c8d  GetRoot(ClientB) when ClientB != null -- likewise discarded;
//             EDI is re-read from B+0xa4 at 06d06c83
//   06d06c98  ClientA kept only when (ObjectClass(+0x4c) & 0x27) == 0x27
//   06d06ca8  the same for ClientB
//   06d06cb5  either survived -> TRUE (06d06cc2); neither -> FALSE (06d06cbe)
static IVP_BOOL __cdecl StarSystem_MpCollisionFilter(IVP_Real_Object* A, IVP_Real_Object* B)
{
    if (SinglePlayer())
        return IVP_TRUE;
    if (A == nullptr)
        return IVP_TRUE;
    if (B == nullptr)
        return IVP_TRUE;

    CObject* ClientA = static_cast<CObject*>(A->client_data);
    if (ClientA == nullptr && A->first_child != nullptr)
        return IVP_TRUE;
    if (B->client_data == nullptr && B->first_child != nullptr)
        return IVP_TRUE;

    if (ClientA != nullptr)
        GetRoot(ClientA);
    CObject* ClientB = static_cast<CObject*>(B->client_data);
    if (ClientB != nullptr)
        GetRoot(ClientB);

    if (ClientA != nullptr && (static_cast<unsigned int>(ClientA->ObjectClass) & 0x27u) != 0x27u)
        ClientA = nullptr;
    if (ClientB != nullptr && (static_cast<unsigned int>(ClientB->ObjectClass) & 0x27u) != 0x27u)
        ClientB = nullptr;

    if (ClientA != nullptr || ClientB != nullptr)
        return IVP_TRUE;
    return IVP_FALSE;
}

void StarSystem::init(unsigned int SystemId)
{
    SystemInfo = const_cast<Universe::ISystem*>(Universe::get_system(SystemId));
    if (!SinglePlayer()) {
        // .text:06d06ebf / 06d06ec5 -- MOV ECX,[->PhySys::m_pCollisionFilter];
        // MOV [ECX],LAB_06d06c30.
        PhySys::m_pCollisionFilter = &StarSystem_MpCollisionFilter;
    }
}

// ===========================================================================
// StarSystem as IObjDB -- the four slots of .rdata:06d67cd0.
// ===========================================================================

// The walk state Common hands to slot 0 ({0, 0, mask} at the start; see
// CAsteroidSystem::update and ScanList::add_in_range): the node last returned,
// the list it came from, and the type mask, which slot 0 re-reads before every
// block (`MOV ECX,[ESI + 0x8]`, .text:06d0dde8 and each later test).
struct FStarSystemObjDbCursor {
    FSystemListNode* Node;             // +0x00
    ObjectList* List;                  // +0x04
    unsigned int Mask;                 // +0x08
};

// FUN_06d0dd50 (.text:06d0dd50, cdecl, five arguments):
//   (Mask, List, &Cursor->Node, &Cursor->List, bFilter)
//   06d0dd50 .. 06d0dd66  a walk already running in ANOTHER list: return null
//                         at once, cursor untouched
//   06d0dd72 .. 06d0dd7f  node = node ? node->Next : List->Head; null -> done
//   06d0dd83  Data = node->Data
//   06d0dd81 / 06d0dd86   only when bFilter (BL, loaded once at 06d0dd6e):
//     06d0dd91  Data->get_type(bits) -- IObjInspect slot 34, +0x88; the out
//               parameter is the bFilter stack slot itself
//     06d0dd97  a non-zero RESULT skips the node
//     06d0dd9f  (bits & Mask) == 0 skips the node
//   06d0dda5  a null Data ends the walk of this list like running off its end
//   06d0dda9 .. 06d0ddbb  hit: cursor = {node, List}, return Data
//   06d0ddbc .. 06d0ddd2  end: cursor = {0, 0}, return null
static GameObject* StarSystem_ObjDbStep(unsigned int Mask, ObjectList* List,
                                        FSystemListNode** CursorNode,
                                        ObjectList** CursorList, bool bFilter)
{
    if (*CursorList != nullptr && *CursorList != List)
        return nullptr;

    FSystemListNode* Node = *CursorNode;
    GameObject* Data = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? List->Head : Node->Next;
        if (Node == nullptr) {
            Data = nullptr;
            break;
        }
        Data = Node->Data;
        if (bFilter) {
            unsigned int Bits = 0;
            if (Data->get_type(Bits) != 0)
                continue;
            if ((Bits & Mask) == 0)
                continue;
        }
        if (Data != nullptr) {
            *CursorNode = Node;
            *CursorList = List;
            return Data;
        }
        break;
    }
    *CursorNode = nullptr;
    *CursorList = nullptr;
    return Data;
}

// Slot 0 -- FUN_06d0dde0 (.text:06d0dde0, `RET 0x4`). Each block is tried only
// when its mask test passes, in this order, and the first object found wins:
//   0x7f0000   -> +0x34, filtered unless the mask covers all of 0x7f0000
//                 (`SETNZ AL` after `CMP EDX,0x7f0000`, .text:06d0ddf7)
//   0xfdff     -> +0x5c, filtered unless it covers all of 0xfdff (.text:06d0de28)
//   0x10000000 -> +0x48 unfiltered      0x1000000  -> +0x70 unfiltered
//   0x2000000  -> +0x84 unfiltered      0x4000000  -> +0x98 unfiltered
//   0x8000000  -> +0xac unfiltered      0x20000000 -> +0xc0 unfiltered
// Null when nothing matched (EAX is zero on every path into .text:06d0df2e
// that did not return an object).
IObjRW* __thiscall StarSystem::GetNext(void* IterState)
{
    FStarSystemObjDbCursor* const Cursor = static_cast<FStarSystemObjDbCursor*>(IterState);
    GameObject* Found = nullptr;

    unsigned int Mask = Cursor->Mask;
    if ((Mask & 0x7f0000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Ships, &Cursor->Node, &Cursor->List,
                                     (Mask & 0x7f0000u) != 0x7f0000u);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0xfdffu) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Solars, &Cursor->Node, &Cursor->List,
                                     (Mask & 0xfdffu) != 0xfdffu);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x10000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Loot, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x1000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &List70, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x2000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Beams, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x4000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &List98, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x8000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &ListAC, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x20000000u) != 0)
        Found = StarSystem_ObjDbStep(Mask, &ListC0, &Cursor->Node, &Cursor->List, false);
    return Found;
}

// Slot 1 -- LAB_06d0dd40: `MOV EAX,[ESP+4] / MOV EDX,[EAX] / MOV [ESP+4],EDX /
// JMP FUN_06d0c840` -- the id by value into find_object.
IObjRW* __thiscall StarSystem::FindById(unsigned const* ObjectId)
{
    return find_object(*ObjectId);
}

// Slot 2 -- .text:06ce10f0 `MOV EAX,[ECX+0x4]; RET` (the body ICF shares with
// CPlayerGroup::GetID): the Universe::ISystem at +0x04.
Universe::ISystem* __thiscall StarSystem::GetZoneDb()
{
    return SystemInfo;
}

// Slot 3 -- LAB_06d06e90: `MOV EAX,[ECX+0x20]; RET 0x4`. The mask is ignored.
void* __thiscall StarSystem::GetBucket(unsigned TypeMask)
{
    (void)TypeMask;
    return Spatial;
}

// FUN_06d0c840 (.text:06d0c840).
//   06d0c846  TEST EDI,0x3fffffff / JNZ -- otherwise return 0
//   06d0c859 .. 06d0c8bc  ObjectList::find_by_id (FUN_06cf4f00) on +0x34, +0x48,
//             +0x70, +0x98, +0xac, +0xc0, +0x5c, stopping at the first hit
//   06d0c8be  return node->Data (0 when no block had it)
GameObject* StarSystem::find_object(unsigned int Id)
{
    if ((Id & 0x3fffffffu) == 0)
        return nullptr;
    FSystemListNode* Node = Ships.find_by_id(Id);
    if (Node == nullptr)
        Node = Loot.find_by_id(Id);
    if (Node == nullptr)
        Node = List70.find_by_id(Id);
    if (Node == nullptr)
        Node = List98.find_by_id(Id);
    if (Node == nullptr)
        Node = ListAC.find_by_id(Id);
    if (Node == nullptr)
        Node = ListC0.find_by_id(Id);
    if (Node == nullptr)
        Node = Solars.find_by_id(Id);
    return (Node != nullptr) ? Node->Data : nullptr;
}

// FUN_06d0c8d0 (.text:06d0c8d0, `RET 0x8`).
//   06d0c8d4  (Id & 0x3fffffff) == 0 -> 0
//   06d0c8e3  TypeMask == -1 -> find_object(Id) (06d0c8e9)
//   then the FIRST matching test picks one block:
//     0x7f0000 -> +0x34   0x10000000 -> +0x48   0xfdff -> +0x5c
//     0x4000000 -> +0x98  0x1000000 -> +0x70    0x8000000 -> +0xac
//     0x60000000 -> +0xc0   nothing -> 0 (06d0c94b)
//   06d0c953 .. 06d0c95d  find_by_id on it; node->Data or 0
GameObject* StarSystem::find_object(unsigned int Id, unsigned int TypeMask)
{
    if ((Id & 0x3fffffffu) == 0)
        return nullptr;
    if (TypeMask == 0xffffffffu)
        return find_object(Id);

    ObjectList* List = nullptr;
    if ((TypeMask & 0x7f0000u) != 0)
        List = &Ships;
    else if ((TypeMask & 0x10000000u) != 0)
        List = &Loot;
    else if ((TypeMask & 0xfdffu) != 0)
        List = &Solars;
    else if ((TypeMask & 0x4000000u) != 0)
        List = &List98;
    else if ((TypeMask & 0x1000000u) != 0)
        List = &List70;
    else if ((TypeMask & 0x8000000u) != 0)
        List = &ListAC;
    else if ((TypeMask & 0x60000000u) != 0)
        List = &ListC0;
    else
        return nullptr;

    FSystemListNode* const Node = List->find_by_id(Id);
    return (Node != nullptr) ? Node->Data : nullptr;
}


// ===========================================================================
// The dock queue at StarSystem+0x14 (server_sysdb.h, SystemDockNode).
// ===========================================================================

// Retail's `[watcher]` with the `- 8` back to the wrapper and its null test
// (.text:06d0c1f0 `MOV EDI,[EBX+0x8]` / `TEST EDI,EDI` / `ADD EDI,-0x8`, and the
// same at .text:06d0c1fe for the second watcher). The Watchable is a
// non-virtual base of IObjRW, so the downcast is a static one.
GameObject* FSystemDockEntry::WatchedObject(BaseWatcher const& Watcher)
{
    Watchable const* const Target = Watcher.target();
    if (Target == nullptr)
        return nullptr;
    return static_cast<GameObject*>(static_cast<IObjRW*>(const_cast<Watchable*>(Target)));
}

// FUN_06d0d370 -- see the declaration for the instruction map.
void StarSystem::queue_dock(GameObject* Object, GameObject* Target,
                            unsigned int const* BaseId, bool const* bLand)
{
    // .text:06d0d390 .. .text:06d0d41b -- the stack element. The two watchers
    // start zeroed; `Object ? Object + 8 : 0` and `Target ? Target + 8 : 0`
    // are the Watchable sub-objects, set only when they differ from what the
    // watcher holds (BaseWatcher::set is exactly that compare-then-set_pointer).
    FSystemDockEntry Element;
    Element.Object.set(Object != nullptr ? static_cast<Watchable const*>(Object) : nullptr);
    Element.Target.set(Target != nullptr ? static_cast<Watchable const*>(Target) : nullptr);
    Element.BaseId = *BaseId;
    Element.bLand = *bLand;

    // .text:06d0d414 .. .text:06d0d441 -- `ESI = [this+0x18]` (the sentinel),
    // `EDI = ESI->Prev`, operator new(0x20), node->Next = sentinel,
    // node->Prev = EDI (the node itself when EDI is null), sentinel->Prev =
    // node, node->Prev->Next = node: a push_back.
    SystemDockNode* const Sentinel = static_cast<SystemDockNode*>(ObjectListHead);
    SystemDockNode* const Node =
        static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
    Node->Next = Sentinel;
    Node->Prev = Sentinel->Prev != nullptr ? Sentinel->Prev : Node;
    Sentinel->Prev = Node;
    Node->Prev->Next = Node;

    // .text:06d0d448 -- FUN_06d0f830 copy-constructs the element into node+8.
    new (&Node->Entry) FSystemDockEntry(Element);

    // .text:06d0d44d .. .text:06d0d454
    ++ObjectListSize;

    // .text:06d0d457 .. .text:06d0d483 -- the stack element's watchers are
    // released on the way out (Element's destructor).
}

// FUN_06d0f410 -- see the declaration.
SystemDockNode* StarSystem::erase_docks(SystemDockNode* First, SystemDockNode* Last)
{
    while (First != Last) {
        SystemDockNode* const Node = First;
        First = First->Next;                         // .text:06d0f429
        Node->Prev->Next = Node->Next;               // .text:06d0f42b
        Node->Next->Prev = Node->Prev;               // .text:06d0f435
        Node->Entry.~FSystemDockEntry();             // .text:06d0f438 FUN_06d0d4a0
        ::operator delete(Node);                     // .text:06d0f43e
        --ObjectListSize;                            // .text:06d0f443 .. 06d0f44c
    }
    return First;                                    // .text:06d0f452
}

// FUN_06d07130 -- see the declaration.
void StarSystem::shutdown()
{
    // .text:06d07134 MOV AL,[ESI+0xfc] / TEST AL,AL / JZ -- nothing when the
    // system is not loaded.
    if (bLoaded == 0)
        return;

    // .text:06d07141 -- the flag is cleared before the event goes out.
    bLoaded = 0;

    // .text:06d0713e .. .text:06d07157 -- `SystemInfo->vtable[0]()` into a
    // stack slot, then FUN_06d43650(0x3d, &slot). This port's
    // Universe::ISystem is a flat record (common_universe.h), so slot 0 is
    // the NicknameId field. Retail calls through SystemInfo unguarded.
    unsigned int SystemId = SystemInfo->NicknameId;
    Server_ControllerDispatch(0u, pub::Controller::EMessage::SystemUnloaded, &SystemId);

    // .text:06d07161 -- FUN_06d072c0.
    unload();
}
// FUN_06d08940 (.text:06d08940, RET 4). See server_sysdb.h for the map.
bool StarSystem::remove_solar(GameObject* Obj)
{
    // .text:06d0894a .. 06d08958
    unsigned int const ObjId = Obj->get_id();
    Server_LookupCacheForget(ObjId);

    // .text:06d08960 .. 06d08978 -- `node = node ? node->Next : Head`.
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Solars.Head : Node->Next;
        if (Node == nullptr)
            return false;                                  // .text:06d08a2a
        if (Node->Data == Obj)
            break;
    }

    // .text:06d0897a .. 06d089d1 -- MetaList::remove, inlined: the same
    // head / tail / middle unlink, then FreeList or the node's deleting
    // destructor (FUN_06d0f9a0).
    Solars.remove(Node);

    // .text:06d089d6 .. 06d089eb -- GetBucket (IObjDB vt +0xc) with 0xfdff;
    // the partition's vtable +0xc (remove) with the object. The BSP holds the
    // IObjRW pointers the load pass bulk-added (StarSystem::load).
    void* const Bucket = GetBucket(0xfdffu);
    if (Bucket != nullptr)
    {
        static_cast<ISpatialPartition*>(Bucket)->Remove(static_cast<IObjRW*>(Obj)); // +0x0c
    }

    // .text:06d089ee .. 06d08a20 -- onto the Deferred block (+0xe8).
    Deferred.append(Obj);
    return true;
}
