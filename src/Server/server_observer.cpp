// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_observer.cpp -- the per-frame observer update and the damage fan-out.
//
// Two retail functions live here, both ported 2026-09-17 (wave 14):
//   FUN_06cf53e0  .text:06cf53e0  SystemPlayerView_Update   (1148 instructions)
//   FUN_06d0b800  .text:06d0b800  StarSystem_DamageObject   ( 295 instructions)
// plus the fourteen-instruction PlayerData helper FUN_06d4f430 that the second
// one needs.
//
// Together they close the loop wave 13 left open: FUN_06cf53e0 and FUN_06d0b800
// are the only two callers of the Observer entry points that wave
// ported, so until now that code had no caller at all in this port.
//
// Ground truth is EXE\Disasm\Server.dll.txt. The decompiled .c was used for
// shape only and is wrong in at least one place that matters -- see the
// UPDATEOBJECT phase below.
// ---------------------------------------------------------------------------

#include "../include/common_server.h"
#include "../include/common_server_playerdb.h"
#include "../include/common_misc.h"       // DamageList / DamageEntry, SUBOBJ_ID_NONE
#include "../include/common_physys.h"     // Quaternion
#include "../include/common_csys.h"       // Matrix
#include "../include/x86math.h"           // I3DMathEngine::MatrixToQuaternion
#include "../include/common_cobject.h"
#include "../include/common_cequip.h"
#include "../include/common_archetype.h"
#include "../include/server_createinfo.h"
#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h"
#include "../include/common_server_types.h"   // XSetTarget (IClient::SendCommonSetTarget)

#include <cstdlib>
#include <list>
#include <vector>

extern IClient* g_pClientInterface;

// ===========================================================================
// The bandwidth governor.
//
// Every phase of the observer update re-asks IClient vtable +0x50 how loaded
// the link to this player is and bails out the moment the answer reaches a
// ceiling. Six separate sites do it, all against the same constant:
//
//   .text:06cf555c  dc1d9069d606  FCOMP  double ptr [.rdata:DAT_06d66990]
//   .text:06cf5671  dfe0          FNSTSW AX
//   .text:06cf5673  f6c401        TEST   AH,0x1        ; C0 == "ST0 < mem"
//   .text:06cf5676  0f845f070000  JZ     LAB_06cf5ddb  ; not less -> stop
//
// and the other five are .text:06cf566b, .text:06cf5c67, .text:06cf5ebf,
// .text:06cf6005 and .text:06cf617f -- byte-for-byte the same instruction.
//
// The constant is NOT a float literal written by eye. .rdata:06d66990 reads
//   cd cc cc cc cc cc ec 3f
// which is 0x3FECCCCCCCCCCCCD, i.e. the IEEE-754 DOUBLE 0.9 exactly. FCOMP
// against a `double ptr` operand, so the comparison happens in double
// precision; IClient's getter returns in ST(0) and rpc_iclient.h already types
// it `virtual double GetLinkSaturation(unsigned int ClientId)`.
//
// The jump is `JZ` on C0, so the loop CONTINUES while the gauge is strictly
// below 0.9 and stops once it is >= 0.9. rpclocal's own concrete IClient
// answers a constant 0.0 (.text:06d8f0b0, `FLD [0.0]; RET 4`), so against the
// local transport the gate never trips -- which is exactly why this is a
// governor and not a feature.
// ===========================================================================
static double const kBandwidthCeiling = 0.9;

// .text:06cf554d loads .data:DAT_06d8fb14 and calls through it unconditionally;
// so does this.
static bool HasBandwidth(unsigned int PlayerId)
{
    return g_pClientInterface->GetLinkSaturation(PlayerId) < kBandwidthCeiling;
}

// ---------------------------------------------------------------------------
// The matrix -> quaternion conversion the update does three times.
//
// Retail lazily builds a singleton into .data:DAT_06d8d7f8 through
// FUN_06d5ff70 and calls its vtable +0x50 with (singleton, &outQuaternion,
// theMatrix):
//   .text:06cf5ce0  a1f8d7d806    MOV  EAX,[.data:DAT_06d8d7f8]
//   .text:06cf5ce7  75 0a         JNZ  LAB_06cf5cf3
//   .text:06cf5ce9  CALL FUN_06d5ff70
//   .text:06cf5cf3  8b08          MOV  ECX,[EAX]            ; its vtable
//   .text:06cf5cff  ff5150        CALL dword ptr [ECX + 0x50]
// Slot +0x50 of a lazily-acquired singleton taking (Quaternion&, Matrix const&)
// is I3DMathEngine::MatrixToQuaternion -- the same slot the common_ce*.cpp
// equip TUs, EngBase.cpp and Thorn.cpp use for this exact conversion.
//
// DAT_06d8d7f8 is Observer.cpp's own file-static, filled by Server.dll's
// FUN_06d5ff70 (Server_Acquire3DMathEngine, server_createinfo.h). Its XREF
// header lists exactly three users -- .text:06cf5ce0 and .text:06cf5eea here,
// and .text:06cf7204 in FUN_06cf7140 (SystemPlayerView_AddTrackedObject,
// server_classes.cpp) -- so this helper is shared with that function rather
// than static. No null test on the engine and no seed for Out: retail's out
// slot is uninitialised stack that the call always fills.
// ---------------------------------------------------------------------------
static I3DMathEngine* GObserverMathEngine = nullptr;   // .data:DAT_06d8d7f8

void Observer_MatrixToQuat(Quaternion& Out, Matrix const& In)
{
    if (GObserverMathEngine == nullptr)
        GObserverMathEngine = Server_Acquire3DMathEngine();
    GObserverMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Out),
                                            reinterpret_cast<FMatrix const&>(In));
}

// ---------------------------------------------------------------------------
// The two wire records this file can build in full.
//
// Both are laid out from the raw stores, not from the decompilation, and both
// deliberately stay LOCAL to Server.dll: the CREATE*/UPDATE* record family is
// shared with rpclocal.dll in retail and belongs in published ABI eventually,
// but that has to be a move of rpclocal's existing definitions rather than a
// second copy here. Until then the writes stay literal, which is what makes the
// eventual move mechanical.
// ---------------------------------------------------------------------------

// IClient +0x010 == SendCommonSetTarget, ch1 op 0x04 FLPACKET_COMMON_SETTARGET (packet 0x2c).
//
// The ship-wide send at .text:06cf58f6 .. .text:06cf5932 (ESP is the frame base
// S throughout, and the `LEA EAX,[ESP+0x4c]` at .text:06cf5918 is the record):
//   .text:06cf58fe  894c244c  MOV  [ESP + 0x4c],ECX  ; +0x00 = CShip::Id (+0xb0)
//   .text:06cf5907  89442454  MOV  [ESP + 0x54],EAX  ; +0x08 = target->get_id()
//   .text:06cf5910  66896c2458 MOV word [ESP+0x58],BP; +0x0c = ship sub-target
//   .text:06cf5924  66894c2454 MOV word [ESP+0x54],CX; +0x04 = SUBOBJ_ID_NONE
// The last one is the trap: a `PUSH EAX` at .text:06cf591c sits between it and
// the others, so `[ESP+0x54]` there is S+0x50 == record+0x04, NOT record+0x08.
// Read without tracking that push it looks like the target id gets clobbered.
//
// The per-gun send at .text:06cf59a0 .. .text:06cf59ee fills the same shape
// with (ShipRecord+0x04, the gun's own Id at CEquip+0x08, the gun's target id,
// the gun's sub-target). The record is common_server_types.h's XSetTarget
// {ObjectId, SubObjId, Pad, TargetObjectId, TargetSubObjId, Pad} -- the same 0x10
// bytes IServer::SetTarget takes the other way (the local FWireSetTarget it
// replaced on 2026-10-16 had this very layout).

// IClient +0x0dc == SendServerDestroyObject, ch2 op 0x06 FLPACKET_SERVER_DESTROYOBJECT (0x28).
//   .text:06cf5c98  895c2464  MOV [ESP + 0x64],EBX  ; zero both dwords first
//   .text:06cf5c9c  895c2468  MOV [ESP + 0x68],EBX
//   .text:06cf5cb1  89442468  MOV [ESP + 0x68],EAX  ; <- ESP is S-4 here
//   .text:06cf5cb5  895c246c  MOV [ESP + 0x6c],EBX  ; <- and here
// The `PUSH EDX` at .text:06cf5cad is the adjustment: the record is at S+0x64,
// so those last two writes land on +0x00 and +0x04, i.e. the object id goes
// FIRST and the kind second. Reading them at face value puts the id at +0x04.
// The record is rpc_iclient_args.h's FServerDestroyObjectInfo: +0x00 obj->get_id(),
// the byte at +0x04 (always 0 on this path) plus its three pad bytes -- the
// dword store of 0 above zeroes all four (the local FWireDestroyObject {ObjId,
// Kind} it replaced on 2026-10-16 had this layout).

// IClient +0x0d8 == SendCommonUpdateObject, ch1 op 0x01 FLPACKET_COMMON_UPDATEOBJECT (0x4c).
// Built at .text:06cf5f14 .. .text:06cf5fa1, record base S+0x6c:
//   +0x00 obj->get_id()                       .text:06cf5f1b
//   +0x04 .. +0x10  the quaternion, dword by dword from the S+0x4c scratch
//                   (.text:06cf5f2b/.text:06cf5f40/.text:06cf5f4f/.text:06cf5f57
//                    read it, .text:06cf5f53/.text:06cf5f44/.text:06cf5f60/
//                    .text:06cf5f64 store it)
//   +0x14 .. +0x1c  the position, three dwords off get_position()'s return
//   +0x20 (float)View->ElapsedTime            .text:06cf5f14 / .text:06cf5f39
//   +0x24 CShip::Throttle (+0x29c) when the object is a ship, else 0. Retail
//         moves it as a dword (.text:06cf5f8a `MOV EDX,[EAX + 0x29c]`) but
//         common_cobject.h's static_assert pins +0x29c as `float Throttle`, so
//         it is a float on the wire too -- a MOV is not evidence of an int.
//   +0x28 byte, seeded 0xff (.text:06cf5f68) and replaced by the object's
//         get_behavior_id() (vtable +0x148) only on the ship path
// The shared argument is FCommonUpdateObjectInfo from rpc_iclient_args.h.

// ---------------------------------------------------------------------------
// .text:06cf58de .. .text:06cf5a20 -- everything the CREATESHIP branch does
// AFTER the create packet: tell the client what this ship, and then each of its
// guns, is aiming at. Lifted into a helper because it is self-contained and is
// the one part of that branch the port can build in full.
// ---------------------------------------------------------------------------
static void SendShipTargets(unsigned int PlayerId, CShip* Ship)
{
    // (w4-server-guards) No null test on the ship: .text:06cf58de `MOV ECX,EDI`
    // / 06cf58e0 calls straight through it, and the only caller passes the
    // CREATESHIP branch's narrowed, already non-null CObject.

    // .text:06cf58e0 / .text:06cf58ea -- both through the Common.dll imports,
    // and the sub-target is read even when the target turns out to be null.
    IObjRW* const Target = Ship->get_target();
    unsigned short const SubTarget = Ship->get_sub_target();
    if (Target != nullptr) {
        XSetTarget Msg;
        Msg.ObjectId = Ship->Id;                 // .text:06cf58f6  [EDI + 0xb0]
        Msg.SubObjId = SUBOBJ_ID_NONE;           // .text:06cf5915 / .text:06cf5924
        Msg.Pad_0x06 = 0u;
        Msg.TargetObjectId = Target->get_id();   // .text:06cf5904  CALL [EDX + 0x20]
        Msg.TargetSubObjId = SubTarget;          // .text:06cf5910
        Msg.Pad_0x0e = 0u;
        g_pClientInterface->SendCommonSetTarget(PlayerId, Msg);
    }

    // .text:06cf5935  PUSH 0x80 / CALL CEquipTraverser::CEquipTraverser --
    // the one-argument constructor, mask 0x80.
    CEquipTraverser Walk(0x80);
    // .text:06cf594e  ADD EDI,0xe4 -- the ship's own CEquipManager.
    //
    // The loop condition is on the CAST, not on Traverse: .text:06cf596d
    // `JZ LAB_06cf5a11` leaves as soon as CEGun::cast answers null, it does not
    // skip and keep walking. With a 0x80 mask that never happens in practice,
    // but the shape is retail's.
    for (;;) {
        // .text:06cf5957 .. .text:06cf5964: Traverse's result goes straight into
        // COMMON.DLL::CEGun::cast, which answers null for a null equip, so the
        // end of the traversal and a non-gun both leave through the same JZ.
        CEGun* const Gun = CEGun::cast(Ship->EquipManager.Traverse(Walk));
        if (Gun == nullptr)
            break;

        // .text:06cf5973 .. .text:06cf5992 -- the out-parameters are seeded
        // (target null, sub-target SUBOBJ_ID_NONE) before the call, and
        // GetTarget's FIRST argument is the pushed-second `LEA EDX,[ESP+0x1c]`.
        CSimple* GunTarget = nullptr;
        unsigned short GunSub = SUBOBJ_ID_NONE;
        Gun->GetTarget(GunTarget, GunSub);
        if (GunTarget == nullptr)
            continue;

        XSetTarget Msg;
        // .text:06cf59a0  MOV ECX,[ESP + 0x1a4] -- the CREATESHIP record's
        // +0x04, which FUN_06d07690 fills from CShip+0xb0
        // (`*(undefined4 *)(param_1 + 4) = *(undefined4 *)(param_2 + 0xb0)`),
        // i.e. the same ship id. Read off the ship directly here because the
        // record is not built in this port yet.
        Msg.ObjectId = Ship->Id;
        Msg.SubObjId = Gun->Id;                  // .text:06cf59a7  [ESI + 0x8]
        Msg.Pad_0x06 = 0u;
        Msg.TargetObjectId = GunTarget->Id;      // .text:06cf59b2  [EAX + 0xb0]
        Msg.TargetSubObjId = GunSub;             // .text:06cf59b8
        Msg.Pad_0x0e = 0u;
        g_pClientInterface->SendCommonSetTarget(PlayerId, Msg);
    }
}

// ===========================================================================
// FUN_06cf53e0 -- Observer::update(double Dt).
// ===========================================================================
void SystemPlayerView_Update(Observer* View, double Dt)
{
    // No test on the view: retail is a thiscall on the list element and reads
    // it straight away (.text:06cf53fe `MOV EBP,ECX`).

    // .text:06cf5405  CALL [->COMMON.DLL::SinglePlayer]
    // .text:06cf540d  0f85e90e0000  JNZ LAB_06cf62fc
    // The whole body is multiplayer-only. Note that this returns BEFORE the
    // clock at +0x30 is touched, so ElapsedTime never advances in single
    // player -- retail's `FADD [EBP+0x30]` is inside the guard.
    if (SinglePlayer())
        return;

    unsigned int const PlayerId = View->PlayerId;

    // .text:06cf5475 .. .text:06cf5488 -- accumulate the frame time.
    View->ElapsedTime += Dt;

    // .text:06cf548b .. .text:06cf54d8. Retail refreshes the observer's own
    // position from the player's ship:
    //   .text:06cf5482  69c018040000  IMUL EAX,EAX,0x418
    //   .text:06cf5491  8d8408e8f...  LEA  EAX,[EAX + ECX*1 + 0xfffffbe8]
    //   .text:06cf54a2  05e4030000    ADD  EAX,0x3e4
    // -0x418 + 0x3e4 == -0x34, i.e. PlayerData::ship_object_id, which is the
    // same slot arithmetic common_server_playerdb.h documents. It then resolves
    // that id, calls the wrapper's get_position() (vtable slot 0,
    // .text:06cf54c6 `CALL dword ptr [EDX]`) and copies the three floats into
    // View+0x3c. No PlayerDB::is_valid: the slot is indexed straight off
    // [.data:Players] (.text:06cf548b / .text:06cf5491). The lookup is
    // FUN_06d006c0 (.text:06cf54b0) in retail and Server_LookupGameObject
    // here; both a failed lookup (.text:06cf54ba) and a null object
    // (.text:06cf54c2) skip the copy.
    {
        unsigned int const ShipId = Players[PlayerId].ship_object_id;
        GameObject* const Self = Server_LookupGameObject(ShipId);
        if (Self != nullptr)
            View->Position = Self->get_position();
    }

    // The three working lists. Retail builds them with three FUN_06cfc340(0,0)
    // calls at .text:06cf5423 / .text:06cf5445 / .text:06cf5468 -- that helper
    // is MSVC6's `std::list::_Buynode`, and _Buynode(0,0) followed by the
    // {allocator, _Head, _Size} stores is an empty list. Their frame slots are
    // S+0x3c (Pending), S+0x24 (Created) and S+0x30 (Uncreated).
    std::list<SystemTrackedObject*> Uncreated;   // S+0x30 -- not on the client yet
    std::list<SystemTrackedObject*> Created;     // S+0x24 -- already on the client
    std::list<SystemTrackedObject*> Pending;     // S+0x3c -- needs an UPDATEOBJECT

    // -----------------------------------------------------------------------
    // Phase 1 -- .text:06cf54db .. .text:06cf554b. Split the tracked list.
    //   .text:06cf54f7  3898f0020000  CMP byte ptr [EAX + 0x2f0],BL
    //   .text:06cf54fd  742f          JZ  LAB_06cf552e   ; not created -> Uncreated
    // and the created half also gets its simulation stepped
    // (.text:06cf5527 CRemotePhysicsSimulation::Doit, with the SAME Dt the
    // caller passed -- .text:06cf5522/.text:06cf5523 re-push the argument pair).
    // -----------------------------------------------------------------------
    {
        // (w4-server-guards) Neither a node nor its record is null-tested:
        // .text:06cf54e0 / 06cf5548 stop only at the sentinel, and 06cf54f0
        // `MOV EAX,[ESI + 0x8]` / 06cf54f7 `CMP byte ptr [EAX + 0x2f0],BL`
        // read the record straight away.
        SystemTrackedNode* const Sentinel = View->Tracked.Head;
        for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel;
             Node = Node->Next) {
            SystemTrackedObject* const Record = Node->Data;
            if (Record->bCreatedOnClient) {
                Created.push_back(Record);
                Record->Simulation.Doit(Dt);
            } else {
                Uncreated.push_back(Record);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Phase 2 -- .text:06cf555c .. .text:06cf5c40. For every record the client
    // does not have yet, decide whether the player can see it now and, if so,
    // send the matching CREATE* packet.
    // -----------------------------------------------------------------------
    if (!HasBandwidth(PlayerId))
        return;

    for (std::list<SystemTrackedObject*>::iterator It = Uncreated.begin();
         It != Uncreated.end(); ++It) {
        // (w4-server-guards) No null test on the record: .text:06cf5641
        // `MOV EAX,[EAX + 0x8]` / 06cf5648 `MOV ESI,[EAX]`.
        SystemTrackedObject* const Record = *It;
        GameObject* const Obj = Record->Object;

        // .text:06cf564d  CALL FUN_06cf7260 / TEST AL,AL / JZ -- skip silently.
        if (!SystemPlayerView_ShouldBeVisible(View, Obj))
            continue;
        // .text:06cf5668 -- and re-check the gauge before every single send.
        if (!HasBandwidth(PlayerId))
            return;

        // NOTE: a null object is NOT skipped. FUN_06cf7260 answers true for
        // null (.text:06cf726a), all five narrowings below then null out, and
        // retail falls straight through to LAB_06cf5bff and marks the record
        // created anyway. Returning early here would leave such a record stuck
        // in the uncreated half for ever.

        // .text:06cf567c .. .text:06cf5736 -- five narrowings of the SAME
        // CObject, tried in this order. The masks are CObject::Class values
        // straight out of common_cobject.h's enum, and they are tested in the
        // order Loot, Ship, CounterMeasure, Mine, Guided:
        //   .text:06cf5691  AND ECX,0x803 / CMP ECX,0x803   ClassLoot
        //   .text:06cf56bc  AND EDX,0x503 / CMP EDX,0x503   ClassShip
        //   .text:06cf56e1  AND ECX,0x17  / CMP CL,0x17     ClassCounterMeasure
        //   .text:06cf5706  AND EDX,0xf   / CMP DL,0xf      ClassMine
        //   .text:06cf572b  AND EAX,0xa7  / CMP AL,0xa7     ClassGuided
        CObject* const Raw = (Obj != nullptr) ? Obj->cobj() : nullptr;
        unsigned int const Cls =
            (Raw != nullptr) ? static_cast<unsigned int>(Raw->ObjectClass) : 0u;
        bool const bIsLoot = (Raw != nullptr) && (Cls & 0x803u) == 0x803u;
        bool const bIsShip = (Raw != nullptr) && (Cls & 0x503u) == 0x503u;
        bool const bIsCounter = (Raw != nullptr) && (Cls & 0x17u) == 0x17u;
        bool const bIsMine = (Raw != nullptr) && (Cls & 0x0fu) == 0x0fu;
        bool const bIsGuided = (Raw != nullptr) && (Cls & 0xa7u) == 0xa7u;

        if (bIsLoot) {
            // .text:06cf5742 .. .text:06cf581c -- CREATELOOT, IClient +0x0c0
            // == SendServerCreateLoot. Two objects: a 0x48-byte LootDesc that FUN_06d07b90
            // fills, and the 0x14-byte request block at S+0x6c that points at
            // it. The caller pre-zeroes the LootDesc's +0x00..+0x18
            // (.text:06cf574d .. .text:06cf577f) and seeds +0x44 = 1,
            // +0x45 = 0, +0x46 = 0 (.text:06cf5786 / .text:06cf578e /
            // .text:06cf5795); the builder then overwrites +0x45 and the caller
            // overwrites +0x46.
            CLoot* const Loot = static_cast<CLoot*>(Raw);
            FServerLootDesc Desc;
            Desc.LootId = 0u;
            Desc.ContentsNickname = 0u;
            Desc.ContentsHitPts = 0.0f;
            Desc.Units = 0u;
            Desc.ContainerNickname = 0u;
            Desc.HitPts = 0.0f;
            Desc.NameOverride = 0u;
            Desc.Field_0x44 = 1;
            Desc.bTemporary = 0;
            Desc.bMarkedBy = 0;
            Desc.Pad47 = 0;
            ServerLootDesc_Fill(&Desc, Loot);
            // .text:06cf57b1 / .text:06cf57ba -- the FIRST of two is_marked_by
            // answers, and it lands in the record, not in the request.
            Desc.bMarkedBy =
                static_cast<unsigned char>(Obj->is_marked_by(PlayerId) ? 1 : 0);

            FServerCreateLootRequest Req;
            Req.OwnerId = Loot->OwnerId;    // .text:06cf57c1  MOV EAX,[EBP+0xe4]
            Req.Kind = 5u;                  // .text:06cf57df
            Req.Items = &Desc;              // .text:06cf57e7
            Req.Count = 1;                  // .text:06cf57eb
            Req.Pad0a[0] = 0;
            Req.Pad0a[1] = 0;
            Req.Pad11[0] = 0;
            Req.Pad11[1] = 0;
            Req.Pad11[2] = 0;
            // .text:06cf57d7 / .text:06cf57db zero both bytes, then
            // .text:06cf57f3 asks a SECOND time and .text:06cf57fe stores the
            // answer over the first zero. Retail really does call
            // GameObject::is_marked_by twice with the same argument.
            Req.bMarkedBy =
                static_cast<unsigned char>(Obj->is_marked_by(PlayerId) ? 1 : 0);
            // .text:06cf5802 / .text:06cf5808
            Req.bTemporary = static_cast<unsigned char>(Loot->bTemporary ? 1 : 0);

            // .text:06cf5816  ff92c0000000  CALL dword ptr [EDX + 0xc0]
            g_pClientInterface->SendServerCreateLoot(PlayerId, Req);
        } else if (bIsShip) {
            // .text:06cf5821 .. .text:06cf5a25 -- CREATESHIP, IClient +0x0bc
            // == SendServerCreateShip, on the 0xe0-byte CShipCreateInfo built at S+0x1a0 by
            // FUN_06cf63f0 (base ctor) plus the derived ctor inlined at
            // .text:06cf5835 .. .text:06cf586e, filled by FUN_06d07690 and
            // destroyed by FUN_06cf64d0 at .text:06cf5a20.
            CShip* const Ship = static_cast<CShip*>(Raw);
            CShipCreateInfo Info;
            CShipCreateInfo_Fill(&Info, Ship);   // .text:06cf588a

            //   .text:06cf5896  8b0d6002d906  MOV  ECX,[.data:Players]
            //   .text:06cf589e  69d218040000  IMUL EDX,EDX,0x418
            //   .text:06cf58a4  8b8c0a00f...  MOV  ECX,[EDX + ECX*1 + 0xffffff00]
            //   .text:06cf58ae  898c24740...  MOV  [ESP + 0x274],ECX
            // record base is S+0x1a0, so 0x274 - 0x1a0 == +0xd4, and
            // -0x100 off `Players + id*0x418` is PlayerData::rep_vibe. The
            // opcode bytes `8b 8c 0a 00 ff ff ff` carry the -0x100 literally;
            // it is not read off the decompilation. rpclocal's independent
            // reading of the receiving side calls the same slot
            // "AttitudeTowards' second group", which is what a rep vibe is.
            // No PlayerDB::is_valid in front of it: the slot is indexed raw.
            Info.VibeOtherGroupId = Players[PlayerId].rep_vibe;
            //   .text:06cf58b8  CALL FUN_06cef450   ; is_marked_by(PlayerId)
            //   .text:06cf58c3  888424480...  MOV [ESP + 0x248],AL  ; +0xa8
            Info.bMarkedBy =
                static_cast<unsigned char>(Obj->is_marked_by(PlayerId) ? 1 : 0);

            // .text:06cf58d8  ff92bc000000  CALL dword ptr [EDX + 0xbc]
            g_pClientInterface->SendServerCreateShip(PlayerId, Info);

            // .text:06cf58de .. .text:06cf5a20 -- what FOLLOWS the create is
            // not part of the record: the ship's current target, and then one
            // SETTARGET per gun.
            SendShipTargets(PlayerId, Ship);
        } else if (bIsCounter) {
            // .text:06cf5a2a .. .text:06cf5ac1 -- CREATECOUNTER, IClient
            // +0x0cc == SendServerCreateCounter, record at S+0x110 filled by FUN_06d07c80.
            //
            // The caller pre-zeroes +0x00/+0x04/+0x08 (.text:06cf5a3b /
            // .text:06cf5a42 / .text:06cf5a49), writes SUBOBJ_ID_NONE into the
            // word at +0x0c (.text:06cf5a5c) and runs the three-argument Vector
            // constructor FUN_06ce7c10 (a plain `[EAX]=a; [EAX+4]=b;
            // [EAX+8]=c; RET 0xc`) three times with zeroes -- on +0x10
            // (.text:06cf5a55), +0x2c (.text:06cf5a6c) and +0x38
            // (.text:06cf5a7b). That third one is what says +0x38 is a Vector
            // and not padding; nothing ever writes it again.
            FServerCreateProjectileInfo Info;
            Info.ObjId = 0u;
            Info.ArchetypeNickname = 0u;
            Info.OwnerId = 0u;
            Info.SubObjId = SUBOBJ_ID_NONE;
            Info.Pad0e = 0u;
            Info.Position.x = 0.0f;
            Info.Position.y = 0.0f;
            Info.Position.z = 0.0f;
            Info.Velocity.x = 0.0f;
            Info.Velocity.y = 0.0f;
            Info.Velocity.z = 0.0f;
            Info.Field_0x38.x = 0.0f;
            Info.Field_0x38.y = 0.0f;
            Info.Field_0x38.z = 0.0f;
            Info.Field_0x44 = 0u;            // .text:06cf5a94
            ServerCreateProjectileInfo_Fill(&Info, static_cast<CProjectile*>(Raw));

            // .text:06cf5ab3 / .text:06cf5ab7 push `&View->PlayerId`, not
            // the value. Not a decompiler artefact -- the dedicated
            // notifier FUN_06cf6bc0 does the same at .text:06cf6c12
            // (`ADD EDI,0x38` / `PUSH EDI`), and so does the guided one.
            // SendServerCreateCounter and SendServerCreateGuided take the id by
            // reference where the other three create slots take a value.
            // .text:06cf5abb  ff92cc000000  CALL dword ptr [EDX + 0xcc]
            g_pClientInterface->SendServerCreateCounter(View->PlayerId, Info);
        } else if (bIsMine) {
            // .text:06cf5ac6 .. .text:06cf5b5f -- CREATEMINE, IClient +0x0c4
            // == SendServerCreateMine, record at S+0x158. Byte for byte the same preparation
            // as the countermeasure above (.text:06cf5ad7 / .text:06cf5ade /
            // .text:06cf5ae5 zero the head, .text:06cf5af8 writes
            // SUBOBJ_ID_NONE, the same three Vector constructions at +0x10 /
            // +0x2c / +0x38, .text:06cf5b30 zeroes +0x44) and the SAME builder
            // (FUN_06d07c80 again, .text:06cf5b37), but a by-VALUE player id
            // (.text:06cf5b53 `ADD EAX,0x38` / .text:06cf5b56 `MOV EAX,[EAX]`).
            FServerCreateProjectileInfo Info;
            Info.ObjId = 0u;
            Info.ArchetypeNickname = 0u;
            Info.OwnerId = 0u;
            Info.SubObjId = SUBOBJ_ID_NONE;
            Info.Pad0e = 0u;
            Info.Position.x = 0.0f;
            Info.Position.y = 0.0f;
            Info.Position.z = 0.0f;
            Info.Velocity.x = 0.0f;
            Info.Velocity.y = 0.0f;
            Info.Velocity.z = 0.0f;
            Info.Field_0x38.x = 0.0f;
            Info.Field_0x38.y = 0.0f;
            Info.Field_0x38.z = 0.0f;
            Info.Field_0x44 = 0u;
            ServerCreateProjectileInfo_Fill(&Info, static_cast<CProjectile*>(Raw));

            // .text:06cf5b59  ff92c4000000  CALL dword ptr [EDX + 0xc4]
            g_pClientInterface->SendServerCreateMine(PlayerId, Info);
        } else if (bIsGuided) {
            // .text:06cf5b64 .. .text:06cf5bfc -- CREATEGUIDED, IClient
            // +0x0c8 == SendServerCreateGuided, the 0x38-byte record at S+0x280 filled by
            // FUN_06d07d40. Unlike the other four, retail pre-initialises
            // nothing here: the builder writes every field except the two
            // 2-byte pads.
            CGuided* const Guided = static_cast<CGuided*>(Raw);
            FServerCreateGuidedInfo Info;
            Info.Pad26 = 0u;
            Info.Pad36 = 0u;
            ServerCreateGuidedInfo_Fill(&Info, Guided);   // .text:06cf5b75

            // .text:06cf5b95  ff92c8000000  CALL dword ptr [EDX + 0xc8],
            // again with `LEA ESI,[EAX+0x38]` -- the ADDRESS of PlayerId.
            g_pClientInterface->SendServerCreateGuided(View->PlayerId, Info);

            // .text:06cf5b9b .. .text:06cf5bfc -- and one SETTARGET for the
            // missile's own target, built at S+0x98 in the same XSetTarget
            // shape the ship path uses:
            //   .text:06cf5bbf  MOV [ESP + 0x98],EDX     ; +0x00 = CGuided+0xb0
            //   .text:06cf5bcf  MOV [ESP + 0xa0],EAX     ; +0x08 = target's id
            //   .text:06cf5bd6  MOV word [ESP + 0xa4],DI ; +0x0c = CGuided+0xf8
            //   .text:06cf5be7  MOV word [ESP + 0x9c],DX ; +0x04 = SUBOBJ_ID_NONE
            // ESP is the frame base S for all four, so no adjustment applies.
            // The sub-target word is loaded at .text:06cf5bae BEFORE the null
            // test at .text:06cf5bb5, but nothing is sent when the target is
            // null.
            IObjRW* const Target = static_cast<IObjRW*>(Guided->get_target());
            if (Target != nullptr) {
                XSetTarget Msg;
                Msg.ObjectId = Guided->Id;
                Msg.SubObjId = SUBOBJ_ID_NONE;
                Msg.Pad_0x06 = 0u;
                Msg.TargetObjectId = Target->get_id();
                Msg.TargetSubObjId = Guided->SubTarget;
                Msg.Pad_0x0e = 0u;
                // .text:06cf5bfc  ff5010  CALL dword ptr [EAX + 0x10], and the
                // first argument is `MOV EDX,[ESI]` -- the VALUE behind the
                // pointer this branch has been carrying.
                g_pClientInterface->SendCommonSetTarget(PlayerId, Msg);
            }
        }

        // .text:06cf5bff .. .text:06cf5c29 -- whatever branch ran (including
        // the "none of the five matched" fall-through, which reaches
        // LAB_06cf5bff directly from .text:06cf5b66), the record is marked
        // created, moved onto the created list and its simulation reset.
        Record->bCreatedOnClient = true;
        Created.push_back(Record);
        Record->Simulation.Reset();
    }

    // -----------------------------------------------------------------------
    // Phase 3 -- .text:06cf5c46 .. .text:06cf5d94. For everything the client
    // already has: drop it when it went out of view, otherwise ask the
    // simulation whether it needs a resync.
    // -----------------------------------------------------------------------
    for (std::list<SystemTrackedObject*>::iterator It = Created.begin();
         It != Created.end(); ++It) {
        if (!HasBandwidth(PlayerId))
            return;

        // (w4-server-guards) No null test on the record or on its object
        // anywhere in this phase: .text:06cf5c78 `MOV EAX,[ESI + 0x8]` /
        // 06cf5c7f `MOV ESI,[EAX]`, then 06cf5c92 `MOV EDX,[ESI]` feeds both
        // the get_id call on the DESTROY path (06cf5ca0) and get_position on
        // the other (06cf5cd1).
        SystemTrackedObject* const Record = *It;
        GameObject* const Obj = Record->Object;

        if (!SystemPlayerView_ShouldBeVisible(View, Obj)) {
            // .text:06cf5c98 .. .text:06cf5cc6 -- DESTROYOBJECT and clear the
            // flag, so the next frame's phase 2 can create it again.
            FServerDestroyObjectInfo Msg;
            Msg.ObjectId = Obj->get_id();
            Msg.Field_0x04 = 0u;                // the dword 0 at +0x04 (.text:06cf5cb5)
            Msg.Pad_0x05[0] = 0u;
            Msg.Pad_0x05[1] = 0u;
            Msg.Pad_0x05[2] = 0u;
            g_pClientInterface->SendServerDestroyObject(PlayerId, Msg);
            Record->bCreatedOnClient = false;
            continue;
        }

        // .text:06cf5cd1  ff12    CALL [EDX]      ; get_position, slot 0
        // .text:06cf5cdb  ff500c  CALL [EAX + 0xc]; get_orientation, slot 3
        // .text:06cf5cff  ff5150  CALL [ECX+0x50] ; matrix -> quaternion
        Vector const& Position = Obj->get_position();
        Quaternion Orient;
        Observer_MatrixToQuat(Orient, Obj->get_orientation());

        // .text:06cf5d02 .. .text:06cf5d1b -- only UNOWNED objects are
        // resynced. Retail nulls the CObject when (ObjectClass & 3) != 3 and
        // then reads +0xb4 THROUGH THE NULL (.text:06cf5d15
        // `CMP dword ptr [ESI + 0xb4],EBX` with ESI already zeroed at
        // .text:06cf5d13) -- the same inlined-accessor fault the create
        // notifiers have. (w4-server-guards) The port reads through the
        // narrowed pointer the same way; it no longer substitutes zero.
        unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
        if (OwnerPlayer != 0u)
            continue;

        // .text:06cf5d34  CALL CRemotePhysicsSimulation::CheckForSync
        // .text:06cf5d39  3c01  CMP AL,0x1 / JNZ -- retail tests for exactly 1.
        if (!Record->Simulation.CheckForSync(View->Position, Position, Orient))
            continue;

        // .text:06cf5d3d .. .text:06cf5dd9 -- insert into Pending keeping it in
        // DESCENDING GetSimulationTime() order:
        //   .text:06cf5d41  dd86c0020000  FLD   double ptr [ESI + 0x2c0]  ; ours
        //   .text:06cf5d53  dd82c8020000  FLD   double ptr [EDX + 0x2c8]  ; theirs
        //   .text:06cf5d59  d9c1          FLD   ST1
        //   .text:06cf5d5b  d8d9          FCOMP                            ; ours ? theirs
        //   .text:06cf5d61  f6c401        TEST  AH,0x1
        //   .text:06cf5d64  7439          JZ    LAB_06cf5d9f               ; ours >= theirs
        // ESI is record+8 (the simulation), so +0x2c0 is m_SimulationTime; the
        // other side reads the node's record at +0x2c8, which is the same
        // field one indirection up. Falling out of the loop without finding a
        // slot appends (.text:06cf5d6e).
        //
        // That ordering is the whole point of the phase: the record that has
        // gone longest without an update is served first out of the remaining
        // bandwidth.
        double const Ours = Record->Simulation.GetSimulationTime();
        std::list<SystemTrackedObject*>::iterator Where = Pending.begin();
        while (Where != Pending.end()) {
            // (w4-server-guards) .text:06cf5d50 `MOV EDX,[ESI + 0x8]` then FLD
            // [EDX + 0x2c8] -- the neighbour record is read unguarded.
            SystemTrackedObject* const Other = *Where;
            if (Other->Simulation.GetSimulationTime() <= Ours)
                break;
            ++Where;
        }
        Pending.insert(Where, Record);
    }

    // -----------------------------------------------------------------------
    // Phase 4 -- .text:06cf5ea1 .. .text:06cf5ff0. Drain Pending into
    // UPDATEOBJECT packets, stopping the instant the gauge fills.
    // -----------------------------------------------------------------------
    for (std::list<SystemTrackedObject*>::iterator It = Pending.begin();
         It != Pending.end(); ++It) {
        if (!HasBandwidth(PlayerId))
            return;

        // (w4-server-guards) Record and object read unguarded:
        // .text:06cf5ed0 `MOV ECX,[EDI + 0x8]` / 06cf5ed3 `MOV ESI,[ECX]` /
        // 06cf5ed5 `MOV EDX,[ESI]` / 06cf5ed9 `CALL [EDX]`.
        SystemTrackedObject* const Record = *It;
        GameObject* const Obj = Record->Object;

        Vector const& Position = Obj->get_position();
        Quaternion Orient;
        Observer_MatrixToQuat(Orient, Obj->get_orientation());

        FCommonUpdateObjectInfo Msg;
        Msg.ObjectId = Obj->get_id();
        Msg.Field_0x24 = 0.0f;         // .text:06cf5faa, the non-ship default
        Msg.Pad_0x29[0] = 0u;
        Msg.Pad_0x29[1] = 0u;
        Msg.Pad_0x29[2] = 0u;
        Msg.Orientation = Orient;
        Msg.Position = Position;
        // .text:06cf5f14 FLD double [EBP+0x30] / .text:06cf5f39 FSTP float --
        // the observer clock crosses the wire NARROWED to a float.
        Msg.Field_0x20 = static_cast<float>(View->ElapsedTime);
        // .text:06cf5f68  MOV byte ptr [ESP + 0x94],0xff -- seeded, and only
        // the ship path replaces it.
        Msg.Field_0x28 = 0xffu;

        CObject* const Raw = Obj->cobj();
        CShip* const Ship =
            (Raw != nullptr &&
             (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CShip*>(Raw) : nullptr;
        if (Ship != nullptr) {
            // .text:06cf5f8a  MOV EDX,[EAX + 0x29c] == CShip::Throttle.
            Msg.Field_0x24 = Ship->Throttle;
            // .text:06cf5f9b  CALL [EAX + 0x148] -- IObjInspect slot 82,
            // get_behavior_id, stored as a BYTE.
            Msg.Field_0x28 =
                static_cast<unsigned char>(Obj->get_behavior_id());
        }

        g_pClientInterface->SendCommonUpdateObject(PlayerId, Msg);

        // .text:06cf5fcc .. .text:06cf5fe7 -- hand the very same sample to the
        // record's own simulation through CRemotePhysicsSimulation's vtable
        // slot 1, as (position, quaternion, ElapsedTime):
        //   .text:06cf5fd5  50  PUSH EAX          ; [EBP+0x34]  high dword
        //   .text:06cf5fd9  50  PUSH EAX          ; [EBP+0x30]  low  dword
        //   .text:06cf5fde  50  PUSH EAX          ; &quaternion (S+0x4c)
        //   .text:06cf5fdf  8b44241c MOV EAX,[ESP + 0x1c]   ; <- S+0x10
        //   .text:06cf5fe6  50  PUSH EAX
        //   .text:06cf5fe7  ff5204  CALL dword ptr [EDX + 0x4]
        // DECOMPILER LIE: the .c passes `puVar7`, the get_orientation() result
        // held at S+0x1c. The three PUSHes before .text:06cf5fdf put ESP at
        // S-12, so `[ESP+0x1c]` is S+0x10 -- the get_POSITION result stored at
        // .text:06cf5edb. Arg 0 is the position.
        //
        // The quaternion is the SAME object the packet carries: retail builds it
        // once at .text:06cf5f0a, and the `LEA EDX,[ESP + 0x50]` at
        // .text:06cf5f04 runs with ESP already moved 4 by the `PUSH EDX` at
        // .text:06cf5f03, so the out quaternion lands at S+0x4c -- exactly the
        // address .text:06cf5fda hands to slot 1 (`LEA EAX,[ESP + 0x54]` with
        // ESP at S-8). One conversion, two consumers.
        //
        // The timestamp crosses here as a DOUBLE, unlike Msg.Timestamp above,
        // which .text:06cf5f39 narrows to a float for the wire.
        Record->Simulation.RemoteUpdate(Position, Orient, View->ElapsedTime);
    }

    // -----------------------------------------------------------------------
    // Phase 5 -- .text:06cf5ff6 .. .text:06cf6248. The deferred-damage retry.
    // This is the consumer wave 13 proved had to exist.
    // -----------------------------------------------------------------------
    if (!HasBandwidth(PlayerId))
        return;

    // .text:06cf6031 .. .text:06cf6153 -- walk the set, resolve every id, and
    // collect the objects that are visible again. Retail opens the walk on
    // `[EBP+0x18]` (the _Tree's _Head) and advances with FUN_06ce5700, MSVC6's
    // `_Tree::iterator::operator++`; `LEA EAX,[EDX + 0xc]` at .text:06cf6050 is
    // `&*it`, the node's _Value.
    std::vector<GameObject*> Retry;
    for (std::set<unsigned int>::const_iterator It = View->PendingDamageIds.begin();
         It != View->PendingDamageIds.end(); ++It) {
        // .text:06cf605e  CALL FUN_06d006c0 -- a failed lookup is turned into a
        // null object at .text:06cf606e rather than skipped, and the null is
        // what the next test rejects.
        GameObject* const Obj = Server_LookupGameObject(*It);
        if (Obj == nullptr)
            continue;
        if (!SystemPlayerView_ShouldBeVisible(View, Obj))
            continue;
        Retry.push_back(Obj);
    }

    // .text:06cf6170 .. .text:06cf6248 -- and drain what was collected.
    for (std::vector<GameObject*>::iterator It = Retry.begin();
         It != Retry.end(); ++It) {
        if (!HasBandwidth(PlayerId))
            return;

        GameObject* const Obj = *It;
        if (Obj == nullptr)
            continue;

        // .text:06cf61a4  CALL COMMON.DLL::std::list<DamageEntry>::list --
        // retail builds a DamageList in place on the stack: the std::list
        // member through that import, then +0x0c = 0 (.text:06cf61aa),
        // +0x10 = 0x19 (.text:06cf61d2), +0x14 = 0 and +0x18 = 0.
        DamageList Damage;
        Damage.set_destroyed(false);
        // SETTLED 2026-09-17 (wave 15): the 0x19 cannot be named, and the fault
        // was never on this side. The store is unambiguous --
        //   .text:06cf61d2  c78424840...  MOV dword ptr [ESP + 0x84],0x19
        // with the record base at S+0x6c, so +0x84 is exactly +0x10, the field
        // DamageList::get_cause reads. What was wrong was the enum it was being
        // measured against: the old six-case DmgCauseToString (now common_damagelist.cpp) was a
        // fabrication. Retail's body (.text:062bbce0) is `MOV EAX,
        // .rdata:0639ed24 / RET` and that .rdata holds the three bytes 3f 3f 3f
        // -- the string "???". There is no table in Common.dll at all, and
        // DamageList::set_cause is ICF-folded with three unrelated setters, so
        // no call site can be attributed to it either. See the long note above
        // the enum in common_misc.h for the whole trail.
        //
        // 0x19 is the ONLY non-zero cause either DLL ever writes: this site and
        // .text:06d07f8a, the other GameObject::get_damage_list caller. So it is
        // written as the literal, which is what it is.
        Damage.set_cause(static_cast<DamageCause>(0x19));
        Damage.set_inflictor_id(0u);
        Damage.set_inflictor_owner_player(0u);

        // .text:06cf61be  PUSH 0x40000000 / .text:06cf61dd CALL [EDX + 0x1a8].
        // 0x40000000 is a FLOAT 2.0f, not a bitmask -- see the byte trail on
        // GameObject::get_damage_list in server_gameobject.h.
        Obj->get_damage_list(Damage, 2.0f);

        // .text:06cf61e3  CMP dword ptr [ESP + 0x74],EBX -- S+0x74 is the
        // record's +0x08, which in retail's layout is the std::list's _Size.
        // An empty list is not worth a packet.
        if (!Damage.Entries.empty()) {
            g_pClientInterface->SendServerDamageObject(PlayerId, Obj->get_id(), Damage);
        }

        // .text:06cf6216 .. .text:06cf6225 -- retail re-asks get_id() rather
        // than reusing the one it just pushed, then erases it from the set.
        // The erase is UNCONDITIONAL: it happens whether or not the packet went
        // out, so an object with no damage to report leaves the queue too.
        View->PendingDamageIds.erase(Obj->get_id());
    }

    // -----------------------------------------------------------------------
    // What this port still does not do, gathered in one place so it cannot be
    // mistaken for a decision.
    //
    // CLOSED this wave: all five CREATE* branches now build and send their
    // records. The six builders live in src/Server/server_createinfo.cpp and
    // the two CObject vtable slots they needed are mapped -- +0x54 is
    // CObject::get_velocity and +0x88 is the get_name family, both read out of
    // Common.dll's own named vftables (??_7CEqObj@@6B@ at .rdata:0639be5c and
    // ??_7CLoot@@6B@ at .rdata:0639d81c), not guessed from a call site.
    //
    // ALSO CLOSED this wave: CRemotePhysicsSimulation vtable slot 1
    // (.rdata:0639b998 -> Common.dll .text:06291e10,
    // CRemotePhysicsSimulation::RemoteUpdate) exists now, and phase 4 calls it
    // with the same position/quaternion/clock it just put on the wire. Phase 3's
    // CheckForSync therefore sees a history that moves.
    //
    // STILL OPEN:
    //
    //   * FUN_06d07690 builds a complete DamageList of every equipment item's
    //     hit points and then destroys it without storing it (see the long
    //     comment on that function). Retail does the same, so the port is
    //     faithful -- but it means CEqObjCreateInfo's own list<DamageEntry> at
    //     +0x8c crosses the wire empty. SETTLED this wave: not just from this
    //     caller, from every caller. A byte-level scan of every `e8 rel32` in
    //     Server.dll.txt whose target is 0x06d07690 finds exactly three call
    //     sites -- .text:06cf588a here, .text:06d0843e in FUN_06d07e20 and
    //     .text:06d0d256 in FUN_06d0d100 -- and no .rdata/.data slot holds the
    //     address, so there is no indirect caller. In both of the other two the
    //     only std::list call on record+0x8c after the builder is the inlined
    //     ~list inside the record's own destructor, and the record's one other
    //     consumer, FUN_06cf6630, writes record+0xd4 (.text:06cf66a0) and
    //     nothing else before sending through IClient +0xbc. So the damage list
    //     is empty on the wire by construction. Still an open question -- where
    //     the receiver's per-equipment damage state on creation comes from --
    //     not an accepted simplification.
    // -----------------------------------------------------------------------
}

// ===========================================================================
// FUN_06d4f430 -- PlayerData::set_collision_group_health. Health is the
// group's relative health (sole caller .text:06d0ba27 passes
// CArchGroup::GetRelativeHealth rounded to float).
// ===========================================================================
void __cdecl PlayerData_SetCollisionGroupHealth(PlayerSlot& Slot,
                                                unsigned short GroupId,
                                                float Health)
{
    for (std::list<CollisionGroupDesc>::iterator It = Slot.collision_groups.begin();
         It != Slot.collision_groups.end(); ++It) {
        if (It->Id == GroupId) {
            It->Health = Health;
            return;
        }
    }
    // Retail falls off the end of the walk with no diagnostic at all.
}

// ===========================================================================
// FUN_06d0b800 -- StarSystem::damage_object(IObjRW*, DamageList&).
//
// Three callers, all thiscall on a StarSystem: .text:06ceb4bf, .text:06ceef18
// and .text:06cf28bb.
//
// The frame arithmetic worth writing down, because two reads look wrong without
// it: the prologue is `PUSH -1 / PUSH LAB / PUSH FS:[0] / SUB ESP,0x40`, so the
// locals start at entry-0x4c, and retail then REUSES the incoming param_1 slot
// as scratch -- .text:06d0b856 `LEA EDX,[ESP+0x60]` with four registers pushed
// resolves to entry+4, and .text:06d0b860 stores the owner player id there
// before handing its address to PlayerDB::is_valid. Likewise the allocator byte
// at .text:06d0ba8b is read out of the param_2 slot: MSVC6's `allocator<>`
// temporary, carrying no value, exactly the artefact server_sysdb.h already
// documents for Observer's constructor.
// ===========================================================================
void StarSystem_DamageObject(StarSystem* Sys, GameObject* Obj, DamageList* Damage)
{
    // (w4-server-guards) No null test on the system, the object or the damage
    // list: thiscall (06d0b821 spills ECX), 06d0b81c `MOV EAX,[EAX + 0x8]`
    // reads the list and 06d0b830 `MOV EDX,[EBX]` reads Obj's vtable.

    // .text:06d0b818  MOV EAX,[ESP+0x54] / MOV EAX,[EAX+0x8] / TEST / JZ --
    // +0x08 of retail's DamageList is its std::list's _Size. Nothing to do.
    if (Damage->Entries.empty())
        return;

    unsigned int const ObjId = Obj->get_id();

    // -----------------------------------------------------------------------
    // Part 1 -- .text:06d0b83b .. .text:06d0ba85. Mirror the damage into the
    // owning player's PlayerData, but ONLY when the damaged object is that
    // player's current ship.
    // -----------------------------------------------------------------------
    // (w4-server-guards) .text:06d0b84d `XOR EAX,EAX` / 06d0b84f
    // `MOV ESI,[EAX + 0xb4]` -- OwnerPlayer is read through the narrowed
    // pointer with no null test; the port no longer substitutes zero.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;

    CObject* const Raw = Obj->cobj();
    // .text:06d0b89c  AND ECX,0x103 / CMP ECX,0x103. 0x103 is the bit pattern
    // common to ClassSolar (0x303), ClassShip (0x503) and ClassLoot (0x803) and
    // absent from ClassAsteroid (0x1003) -- i.e. "is a CEqObj", an object that
    // carries equipment.
    CEqObj* const EqObj =
        (Raw != nullptr &&
         (static_cast<unsigned int>(Raw->ObjectClass) & 0x103u) == 0x103u)
            ? static_cast<CEqObj*>(Raw) : nullptr;

    bool const bMirror =
        Players.is_valid(OwnerPlayer) &&
        Players[OwnerPlayer].ship_object_id == ObjId &&
        EqObj != nullptr;

    if (bMirror) {
        PlayerSlot& Slot = Players[OwnerPlayer];
        for (std::list<DamageEntry>::const_iterator It = Damage->Entries.begin();
             It != Damage->Entries.end(); ++It) {
            DamageEntry const& Entry = *It;

            if (SubObjectID::IsEquipID(Entry.SubObjId)) {
                // .text:06d0b8de  MOV EAX,[ESI+0x10] / TEST / JNZ -- entry+0x08
                // is DamageEntry::Fate, and the two arms are "still there" and
                // "gone".
                if (Entry.Fate == DamageEntry::SubObjFate::None) {
                    // .text:06d0b8e9 .. .text:06d0b96a. Ask the live equipment
                    // manager for the equip, let it describe itself, and push
                    // that description into the player's own list.
                    EquipDesc Desc;
                    CEquip* const Equip =
                        EqObj->EquipManager.FindByID(Entry.SubObjId);
                    if (Equip == nullptr)
                        continue;
                    // .text:06d0b917  CALL [EDX + 0x28] -- CEquip::GetEquipDesc.
                    if (!Equip->GetEquipDesc(Desc))
                        continue;
                    // .text:06d0b93c loads Desc+0x0c (bEquipped) into ECX.
                    // 06d0b94a pushes zero as the sixth, unused argument; 06d0b94c
                    // pushes ECX as the fifth, equipped argument. The port omits only
                    // the unused sixth argument. Preserve the live item's mount state
                    // so damage cannot turn installed equipment into cargo in a save.
                    PlayerData_ModifyEquipment(
                        Slot, Desc.get_id(), Desc.get_hardpoint(),
                        Desc.get_count(), Desc.get_status(), Desc.is_equipped());
                } else {
                    // .text:06d0b974 .. .text:06d0b9ac -- the equip is gone:
                    //   .text:06d0b9a0  6a00  PUSH 0x0    ; bNotify := false
                    //   .text:06d0b9a2  6aff  PUSH -0x1   ; Count  := -1
                    //   .text:06d0b9ab  52    PUSH EDX    ; the sub-object id
                    PlayerData_RemoveEquipment(Slot, Entry.SubObjId, -1,
                                               /*bNotify*/false);
                }
            } else if (SubObjectID::IsArchGroupID(Entry.SubObjId)) {
                // .text:06d0b9ca .. .text:06d0ba27 -- a collision group. The
                // value that reaches PlayerData is the group's CURRENT relative
                // health off Common.dll, not the DamageEntry's hit points:
                //   .text:06d0ba14  ff15c842d606  CALL [06d642c8]
                //                   = COMMON.DLL::CArchGroup::GetRelativeHealth
                //   .text:06d0ba23  d91c24        FSTP float ptr [ESP]
                // The callee (06d4f430) stores it as CollisionGroupDesc::Health,
                // the same field get_undamaged_collision_group_list fills with 1.0.
                CArchGroup* const Group =
                    EqObj->ArchGroups.FindByID(Entry.SubObjId);
                if (Group == nullptr)
                    continue;
                PlayerData_SetCollisionGroupHealth(Slot, Entry.SubObjId,
                                                   (float)Group->GetRelativeHealth());
            } else if (Entry.SubObjId == SUBOBJ_ID_ROOT) {
                // .text:06d0ba2e  MOV AX,[ESI+0x8] / CMP AX,[SUBOBJ_ID_ROOT]
                // -- the hull itself, which lands in relative_health:
                //   .text:06d0ba6a  6a00  PUSH 0x0   ; bNotify := false
                //   .text:06d0ba6e  CALL COMMON.DLL::CSimple::get_relative_health
                //   .text:06d0ba77  d91c24 FSTP float ptr [ESP]
                PlayerData_SetRelativeHealth(Slot, EqObj->get_relative_health(),
                                             /*bNotify*/false);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Part 2 -- .text:06d0ba8b .. .text:06d0bb61. Build the reduced list and
    // tell every player in this system.
    // -----------------------------------------------------------------------

    // .text:06d0ba93 .. .text:06d0bab6 -- an EMPTY DamageList on the stack,
    // constructed by hand (`_Buynode` through the Common.dll import, then the
    // four POD fields zeroed one by one). Note that +0x10 is left at 0 here,
    // unlike the 0x19 the observer update writes.
    DamageList Reduced;

    // .text:06d0bac2  CALL [EAX + 0x150] -- IObjInspect::cobject -- then
    // .text:06d0bad0 `AND ECX,0x303 / CMP ECX,0x303`, i.e. ClassSolar. Only a
    // solar gets a reduced list built at all.
    bool const bIsSolar =
        Raw != nullptr &&
        (static_cast<unsigned int>(Raw->ObjectClass) & 0x303u) == 0x303u;
    if (bIsSolar) {
        // .text:06d0baf0  CMP dword ptr [ESI + 0x10],EDI -- entry+0x08 again,
        // DamageEntry::Fate. Only the entries that describe something actually
        // destroyed or separated are copied.
        for (std::list<DamageEntry>::const_iterator It = Damage->Entries.begin();
             It != Damage->Entries.end(); ++It) {
            if (It->Fate != DamageEntry::SubObjFate::None)
                Reduced.Entries.push_back(*It);
        }
        Reduced.Count = static_cast<int>(Reduced.Entries.size());
    }

    // .text:06d0bb36 .. .text:06d0bb61 -- the fan-out. `MOV EAX,[ESP+0xc]` is
    // `this` (spilled at .text:06d0b821 and read back after EBP was popped),
    // `MOV EDI,[EAX+0xc]` is StarSystem::PlayerListHead, and each node's
    // element is EMBEDDED at node+0x08 (.text:06d0bb55 `LEA ECX,[ESI + 0x8]`,
    // an address computation, not a load).
    //
    // The three stack arguments are pushed &Reduced, Damage, ObjId, i.e. in
    // source order (ObjId, Damage, &Reduced): .text:06d0bb4d/.text:06d0bb4e
    // push the last two and .text:06d0bb54 pushes the id after the get_id()
    // call at .text:06d0bb51.
    // (w4-server-guards) No null test on the head or on a node: 06d0bb3d
    // `MOV ESI,[EDI]` / 06d0bb3f `CMP ESI,EDI` and 06d0bb5f `CMP ESI,EDI` stop
    // only at the sentinel.
    SystemPlayerNode* const Head =
        reinterpret_cast<SystemPlayerNode*>(Sys->PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next) {
        SystemPlayerView_DamageObject(&Node->View, ObjId, Damage, &Reduced);
    }
}

// ===========================================================================
// FUN_06cf4f40 (.text:06cf4f40, thiscall on the list, `RET 0x8`) --
// StarSystem_TickObjectList(List, Dt, Deferred).
//   06cf4f50 .. 06cf4f5d  MetaList::traverse forward (null -> Head, else Next)
//   06cf4f63 .. 06cf4f75  Data->update(Dt) (vtable +0x16c); true keeps the node
//   06cf4f7e .. 06cf4fdd  MetaList::remove inlined: unlink, Node = old Prev,
//                         the node pushed on FreeList (bUseHeap) or deleted
//   06cf4fe1 .. 06cf5010  Deferred->append(Data) inlined (FUN_06d11850 is
//                         Deferred's alloc_node)
// No null test on Data: update is called through it unconditionally.
// ===========================================================================
void StarSystem_TickObjectList(ObjectList* List, float Dt, ObjectList* Deferred)
{
    FSystemListNode* Node = nullptr;
    while (List->traverse(Node, true))
    {
        if (Node->Data->update(Dt))
            continue;
        GameObject* const Data = Node->Data;
        List->remove(Node);
        Deferred->append(Data);
    }
}
