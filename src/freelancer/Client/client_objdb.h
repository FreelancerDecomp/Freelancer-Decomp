// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_cobjdb.h"
#include "common_iface.h"
#include "common_cobject.h"

#include <cstddef>
#include <list>

class FClientGameobj;

namespace Universe
{
    struct ISystem;
}

// Client-side IObjDB — retail DAT_00678a2c via PTR_DAT_00613ed0 (StarSys.cpp).
// Concrete type FIObjDB in client_objdb.cpp: ten ManagerKind buckets (FUN_00540fc0),
// GetNext LAB_00541d60, FindById LAB_00542130, GetBucket → DAT_00678ae0 (built by FUN_00540eb0),
// GetZoneDb → DAT_00679954 (Universe::ISystem*).
//
// === Draw contract (neighbor renderer) ===
// Retail does NOT draw FSpaceSceneObject proxies. After FUN_00548a20 / Gameobj::create:
//   visual handle = IObjInspect::get_index()  (== CObject/EngineObject::EngineInstanceId @ +0x04)
//   world pose    = IObjInspect::get_position() / get_orientation()  (CObject WorldPosition @ +0x2c)
// Mesh is bound in create() via Root::create_instance → EngineInstanceId, then
// ICliObj+0x20 CliObjLoad(1) on the archetype companion. Enumerate live objects with
// GetClientObjDb() GetNext/Fill — same DB the scanner already uses.

IObjDB* GetClientObjDb();

// FUN_00540c30 (cdecl): link Go into its ManagerKind bucket; a kind-2 Gameobj also goes into
// the large-solar vector (radius > 200, get_type bit 7 clear) and the spatial partition;
// bRemotePhysics appends a CRemotePhysicsSimulation block for its CShip.
void ClientObjDb_RegisterGameobj(FClientGameobj* Go, bool bRemotePhysics = false);

// FUN_00541040 (cdecl): drop Go's remote-physics block, unlink it from its bucket, and for a
// solar erase it from the large-solar vector and the partition. Does not delete Go.
void ClientObjDb_Unregister(FClientGameobj* Go);

// FUN_00541380 (cdecl): the per-frame Gameobj update -- buckets 1, 3, 4, 5, 6, then (while the
// partition exists) every solar within 10000 of the listener camera plus every large solar,
// then buckets 10, the eleventh list, 7, 8, 9.
void ClientObjDb_UpdateAll(float Dt);

// FUN_00541510: unregister and delete every Gameobj whose slot 86 (+0x158) reports it done.
void ClientObjDb_Reap();

// FUN_005419a0 (cdecl): the Gameobj wrapping Obj, found in the bucket its ObjectClass selects.
FClientGameobj* ClientObjDb_FindByCObject(CObject const* Obj);

unsigned ClientObjDb_GetRegisteredCount();
unsigned ClientObjDb_GetSize();

bool ClientObjDb_TryGetWorldPosById(unsigned NicknameId, float OutPos[3]);
bool ClientObjDb_TryGetWorldPosByArchetypeId(unsigned ArchetypeId, float OutPos[3]);

// FUN_00540e90 -- FreeSpatial(DAT_00678ae0) and null it (StarSysLoadIni entry, 00553763).
void ClientObjDb_FreeSpatial();
// FUN_00541210 (cdecl) -- the star-system unload's ObjDB clear: every remote-physics block and
// every Gameobj not named in Keep (null = keep nothing) is removed and deleted, the large-solar
// vector emptied and the spatial partition freed.
void ClientObjDb_ClearExcept(std::list<FClientGameobj*> const* Keep);
// FUN_00540eb0 -- clear/create the "BSP" partition and bulk-add every solar whose get_type
// & 0xfdff (StarSysLoadIni exit, 00554280).
void ClientObjDb_RebuildSpatial();

unsigned ClientObjDb_GetSolarCount();
bool ClientObjDb_TryGetSolarWorldPosByIndex(unsigned Index, float OutPos[3]);

// FUN_00540fc0 (cdecl) -- first Gameobj of ManagerKind bucket *Kind (1..10), null for any
// other kind or an empty bucket. The rest of the chain follows FClientGameobj::ListNext.
FClientGameobj* ClientObjDb_GetKindHead(int const* Kind);
// The large-solar vector (DAT_00678ac0, solars of radius > 200) as the draw pass
// (0x0051c549..0x0051c5ac) and FUN_004d1027 walk it. *It is a slot index standing in for
// retail's element pointer. FUN_00540a00: start at the first slot; FUN_00540a20: advance one.
// Each returns the entry at the new slot, or null at the end.
FClientGameobj* ClientObjDb_LargeSolarFirst(size_t* It);
FClientGameobj* ClientObjDb_LargeSolarNext(size_t* It);
// FUN_00540a40 (cdecl): Go is in the large-solar vector (the walk stops at a null entry).
bool ClientObjDb_IsLargeSolar(FClientGameobj const* Go);
// FUN_00540bd0 (cdecl): CBeam::move(Dt) on the CBeam of every bucket-3 Gameobj.
void ClientObjDb_MoveBeams(float Dt);
// FUN_0052fc20 (cdecl, caller 0x0054b05d): PhySys::GenerateCollisions for every live bucket-3
// beam, then every PhySys collision of the active system resolved on both sides (FUN_0052fe60)
// and delivered to both Gameobjs' slot 101 (+0x194).
void ClientObjDb_ProcessCollisions();
// FUN_00541580 (cdecl, caller 0x0054b16d): multiplayer only -- every remote-physics block
// (DAT_00678a30) advances its time and CRemotePhysicsSimulation by Dt; a ship CheckForSync
// flags is sent to the server (IServer::SPObjUpdate) and fed back through RemoteUpdate.
void ClientObjDb_UpdateRemotePhysics(float Dt);
// FUN_005416c0 (cdecl) -- the Gameobj whose get_id is ObjectId: Kind 0 searches buckets
// 1, 4, 5, 6, 7, 2 in that order, any other Kind searches only that bucket. Null for an id
// with no bits under 0x3fffffff.
FClientGameobj* ClientObjDb_FindObject(unsigned ObjectId, int Kind);
// FUN_00541890 (cdecl) -- Kind 0: the ship (bucket 1) or else solar (bucket 2) whose
// CEqObj+0x104 equals VibeId; any other Kind: the get_id match in that bucket, as
// FUN_005416c0 does.
FClientGameobj* ClientObjDb_FindEqObjByVibeId(unsigned VibeId, int Kind);
