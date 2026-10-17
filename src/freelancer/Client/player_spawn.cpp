// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "player_spawn.h"
#include "client_objdb.h"

#include "character_select_equip.h"
#include "client_cliobj.h"   // Archetype::LoadMountedEquipmentCompanions / HookUpInstanceTreeMaterials
#include "client_fuse.h"
#include "client_gameobj.h"  // FClientShipGameobj (FUN_00543380)
#include "hud_waypoint.h"    // HudWaypointOnPlayerWaypointsCleared (FUN_004f4cd0)
#include "loading_screen.h"  // LoadingScreenStart (FUN_00579290)
#include "engine_light.h"
#include "common_archetype.h"
#include "common_cobject.h"
#include "common_goods.h"    // BaseWatcher, EquipDescVector
#include "common_id.h"
#include "common_ini.h"
#include "common_reputation.h"
#include "common_server.h"   // IServer (DAT_0067ecd0)
#include "common_misc.h"     // SinglePlayer
#include "common_universe.h"
#include "gate_tunnel.h"     // GateTunnelIniLoad (FUN_004fe1c0)
#include "login_mgr.h"       // G_Server (DAT_0067ecd0)
#include "globals.h"         // g_display.gate_tunnels (DAT_0067a22c)
#include "menu_keymap_config.h"
#include "menu_sound_arch.h"  // PreloadClientSoundArchesByCategory (FUN_0042acb0)
#include "nn_modal.h"         // NnSystemInit (FUN_0045a100)
#include "server_connect.h"
#include "space_camera.h"
#include "space_scene_load.h"
#include "star_sys.h"

#include <cstddef>
#include <list>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace
{
    // Retail globals touched by FUN_0054b520 / FUN_0054ab40.
    // DAT_0067337c (ship archetype) lives in character_select_equip.cpp.
    static unsigned G_PlayerCharacterId = 0;       // DAT_00673378
    static unsigned G_PlayerConnectionId = 0;        // DAT_00673344
    static unsigned G_PlayerShipCharMirror = 0;    // DAT_00679774
    // DAT_00679744 -- the BaseWatcher on the player's Ship gameobj (its Watchable at IObjRW+8).
    // FUN_0054b520 points it at the gameobj it builds (0x0054b850); FUN_0054baf0 and its
    // inlined copies read the watched pointer and subtract eight.
    static BaseWatcher G_PlayerShipWatcher;
    // DAT_00678b96 -- cleared by FUN_0054b520 before IServer::PlayerLaunch (0x0054b867), set
    // by the IClient launch handler (0x0053d87e / 0x0053d8da), waited on by FUN_0054ab40
    // (FUN_005a89b0 at 0x0054ae30, space_state.cpp InitSpaceState). A byte, because the wait
    // takes it by address.
    static unsigned char G_bPlayerLaunchAcknowledged = 0;

    // DAT_00672978 (99 records of 0x18, FPlayerWaypointRecord in player_spawn.h) and
    // DAT_00673374 (the count). The six accessors FUN_004c4690..FUN_004c4780 are public below.
    static const int kMaxPlayerWaypoints = 0x63;
    static FPlayerWaypointRecord G_PlayerWaypoints[kMaxPlayerWaypoints] = {};
    static int G_PlayerWaypointCount = 0;
    // DAT_00673390 -- read by FUN_004c4ea0, written only by FUN_004c4eb0 (0x004c4eca, called
    // from the nav map's best-path button 0x00493c0b).
    static unsigned G_PlayerWaypointTailTag = 0;

    // DAT_00673348 / DAT_0067334c / DAT_00673350 -- the multiplayer group mission-offer answer
    // (player_spawn.h). FUN_004c55b0 zeroes all three; IClient slot 114 stores the first two;
    // the offer sequence at 0x004559b0 stores the third (0x00455a07) and FUN_004557d0 clears it.
    static unsigned G_MissionOfferAnswered = 0;              // DAT_00673348
    static unsigned G_MissionOfferAcceptorId = 0;            // DAT_0067334c
    static FGfScriptSequence* G_MissionOfferSequence = nullptr; // DAT_00673350
}

unsigned GetPlayerShipArchetypeId()
{
    return GetPlayerShipArchetypeIdRetail();
}

int GetPlayerWaypointCountRetail()
{
    // FUN_004c4690.
    return G_PlayerWaypointCount;
}

FPlayerWaypointRecord* GetPlayerWaypointRetail(int Index)
{
    // FUN_004c46a0 (cdecl): null for Index < 0 or >= the count (signed compares).
    if (Index < 0 || Index >= G_PlayerWaypointCount)
        return nullptr;
    return &G_PlayerWaypoints[Index];
}

void ResetPlayerWaypointsRetail()
{
    // FUN_004c46c0: only a positive count is cleared, and then retail tail-jumps
    // (0x004c46d3) to FUN_004f4cd0 = HudWaypointOnPlayerWaypointsCleared (hud_waypoint.h).
    if (G_PlayerWaypointCount > 0)
    {
        G_PlayerWaypointCount = 0;
        HudWaypointOnPlayerWaypointsCleared();
    }
}

void RemovePlayerWaypointRetail(int Index)
{
    // FUN_004c46e0 (cdecl): Index in [0, count) drops the count by one and, unless it was the
    // last record, moves every later record down one slot.
    if (Index < 0 || Index >= G_PlayerWaypointCount)
        return;
    G_PlayerWaypointCount = G_PlayerWaypointCount - 1;
    for (int Slot = Index; Slot < G_PlayerWaypointCount; ++Slot)
        G_PlayerWaypoints[Slot] = G_PlayerWaypoints[Slot + 1];
}

int GetPlayerWaypointMaxIdRetail()
{
    // FUN_004c4750: the largest AutoId (signed compares), 0 for an empty list.
    int MaxId = 0;
    for (int Slot = 0; Slot < G_PlayerWaypointCount; ++Slot)
    {
        if (G_PlayerWaypoints[Slot].AutoId > MaxId)
            MaxId = G_PlayerWaypoints[Slot].AutoId;
    }
    return MaxId;
}

unsigned GetPlayerWaypointTailTagRetail()
{
    // FUN_004c4ea0: MOV EAX,[DAT_00673390]; RET.
    return G_PlayerWaypointTailTag;
}

void RequestBestPathRetail(Vector const& From, Vector const& To, unsigned Tag, unsigned Field30)
{
    // FUN_004c4eb0 (cdecl): DAT_00673390 = Tag, then the 0x34-byte request on the stack --
    // +0x00 DAT_0061226c, +0x04 2, +0x08 byte 0, +0x0c From, +0x18 0, +0x1c DAT_00673354,
    // +0x20 To, +0x2c 0, +0x30 the fourth argument -- through IServer slot 72 (+0x120,
    // RequestBestPath) with the session DAT_00673344 and the size 0x34. The untouched bytes
    // (+0x09..+0x0b) are stack garbage in retail; the port zeroes them.
    G_PlayerWaypointTailTag = Tag;
    unsigned char Request[0x34];
    memset(Request, 0, sizeof(Request));
    unsigned const Field00 = static_cast<unsigned>(GetPlayerSpawnSystemIdRetail()); // DAT_0061226c
    unsigned const Field04 = 2;
    unsigned const Field1c = GetPlayerStarSystemIdRetail(); // DAT_00673354
    memcpy(Request + 0x00, &Field00, 4);
    memcpy(Request + 0x04, &Field04, 4);
    memcpy(Request + 0x0c, &From, 0xc);
    memcpy(Request + 0x1c, &Field1c, 4);
    memcpy(Request + 0x20, &To, 0xc);
    memcpy(Request + 0x30, &Field30, 4);
    G_Server->RequestBestPath(GetPlayerConnectionId(), Request, 0x34);
}

void ResetMissionOfferAnswerRetail()
{
    // FUN_004c55b0: XOR EAX,EAX; MOV [DAT_00673348],EAX; MOV [DAT_0067334c],EAX;
    // MOV [DAT_00673350],EAX; RET.
    G_MissionOfferAnswered = 0;
    G_MissionOfferAcceptorId = 0;
    G_MissionOfferSequence = nullptr;
}

void SetMissionOfferAnswerRetail(unsigned Answered, unsigned AcceptorId)
{
    // IClient slot 114, 0x0053e378 / 0x0053e386.
    G_MissionOfferAcceptorId = AcceptorId;
    G_MissionOfferAnswered = Answered;
}

unsigned GetMissionOfferAnsweredRetail()
{
    return G_MissionOfferAnswered;
}

unsigned GetMissionOfferAcceptorIdRetail()
{
    return G_MissionOfferAcceptorId;
}

FGfScriptSequence* GetMissionOfferSequenceRetail()
{
    return G_MissionOfferSequence;
}

void SetMissionOfferSequenceRetail(FGfScriptSequence* Sequence)
{
    G_MissionOfferSequence = Sequence;
}

bool AddPlayerWaypointRetail(Vector const& Pos, unsigned SystemId, unsigned ObjectId)
{
    // FUN_004c4780 (cdecl): refused at 0x63 records or once the largest AutoId reaches 0x63;
    // otherwise the record goes to the end with AutoId = largest + 1.
    if (G_PlayerWaypointCount >= kMaxPlayerWaypoints)
        return false;
    int const MaxId = GetPlayerWaypointMaxIdRetail();
    if (MaxId >= kMaxPlayerWaypoints)
        return false;
    FPlayerWaypointRecord& Record = G_PlayerWaypoints[G_PlayerWaypointCount];
    Record.Pos = Pos;
    Record.SystemId = SystemId;
    Record.ObjectId = ObjectId;
    Record.AutoId = MaxId + 1;
    G_PlayerWaypointCount = G_PlayerWaypointCount + 1;
    return true;
}

// FUN_004c4610 — retail: `if (DAT_0067337c != 0) fVar2 = *(float*)(Archetype::GetShip(DAT_0067337c)+0x100);`
// Ship+0x100 is HoldSize (see common_archetype.h Ship::HoldSize). Used alongside the
// not-yet-ported EquipDescList::get_cargo_space_occupied (FUN_004c4640) to compute remaining
// cargo space; this half of that pair has no EquipDescList dependency, so it is fully portable now.
float GetPlayerShipHoldSizeRetail()
{
    // FUN_004c4610: 0.0 (DAT_005c7474) without a ship archetype id (DAT_0067337c); otherwise
    // GetShip(id)+0x100 (HoldSize), no null test (0x004c4622..0x004c4628).
    if (GetPlayerShipArchetypeIdRetail() == 0)
        return 0.0f;
    Archetype::Ship* const ShipArch = Archetype::GetShip(GetPlayerShipArchetypeIdRetail());
    return ShipArch->HoldSize;
}

unsigned GetPlayerCharacterId()
{
    return G_PlayerCharacterId;
}

namespace
{
    // DAT_00678b94 / DAT_00678b95 -- the server's answer to IServer::RequestCreateShip and "an
    // answer arrived". Written by IClient slot 61 (.text:0053d928 / 0x0053d92f); FUN_0054ab40
    // clears both before the request (0x0054abbe / 0x0054abc4) and waits on +0x95 through
    // FUN_005a89b0 (0x0054abb9, 0x0054abcf).
    unsigned char G_PlayerCreateShipResult = 0;      // DAT_00678b94
    unsigned char G_bPlayerCreateShipAnswered = 0;   // DAT_00678b95
}

void SetPlayerCreateShipResponse(unsigned char Result, unsigned CharacterId)
{
    // LAB_0053d920: +0x95 = 1, +0x94 = Result, DAT_00673378 = CharacterId -- the only retail
    // writer of DAT_00673378 (XREF list at .data:00673378).
    G_bPlayerCreateShipAnswered = 1;
    G_PlayerCreateShipResult = Result;
    G_PlayerCharacterId = CharacterId;
}

bool IsPlayerCreateShipAnswered()
{
    return G_bPlayerCreateShipAnswered != 0;
}

unsigned char GetPlayerCreateShipResult()
{
    return G_PlayerCreateShipResult;
}

void ClearPlayerCreateShipResponse()
{
    // 0x0054abbe / 0x0054abc4: MOV [DAT_00678b95],BL / MOV [DAT_00678b94],BL (BL = 0).
    G_bPlayerCreateShipAnswered = 0;
    G_PlayerCreateShipResult = 0;
}

unsigned char* GetPlayerCreateShipAnsweredByte()
{
    // PUSH 0x00678b95 (0x0054abb9) -- the byte FUN_005a89b0 waits on.
    return &G_bPlayerCreateShipAnswered;
}

unsigned char* GetPlayerLaunchAcknowledgedByte()
{
    // PUSH 0x00678b96 (0x0054ae2b).
    return &G_bPlayerLaunchAcknowledged;
}

bool IsPlayerShipId(unsigned Id)
{
    // FUN_00548eb0: XOR EAX,EAX; TEST ECX,0x3fffffff / JZ; CMP DAT_00679774,ECX / SETZ AL.
    if ((Id & 0x3fffffffu) == 0)
    {
        return false;
    }
    return G_PlayerShipCharMirror == Id;
}

unsigned GetPlayerShipId()
{
    // DAT_00679774.
    return G_PlayerShipCharMirror;
}

unsigned GetPlayerConnectionId()
{
    return G_PlayerConnectionId;
}

void SetPlayerConnectionId(unsigned Id)
{
    G_PlayerConnectionId = Id;
}

void* GetPlayerShipWatcher()
{
    // The watched pointer of DAT_00679744 (the Watchable at the player gameobj's IObjRW+8), or
    // null -- what FUN_0054baf0 reads before subtracting eight.
    return const_cast<Watchable*>(G_PlayerShipWatcher.target());
}

void ClearPlayerShipWatcher()
{
    // 0x005493f8..0x0054940b (the SpaceApp ctor): CMP [DAT_00679744],0 / JZ, then
    // BaseWatcher::set_pointer(0) on it.
    if (G_PlayerShipWatcher.target() != nullptr)
        G_PlayerShipWatcher.set(nullptr);
}

void* GetPlayerShipObject()
{
    // NOT RETAIL (port accessor): the CShip behind the watched gameobj, narrowed the way
    // FUN_0054b520 narrows it (Object +0x10, class & 0x503 == 0x503, 0x0054b59f..0x0054b5c0).
    Watchable const* const Target = G_PlayerShipWatcher.target();
    if (Target == nullptr)
        return nullptr;
    FClientGameobj* const Go =
        static_cast<FClientGameobj*>(static_cast<IObjRW*>(const_cast<Watchable*>(Target)));
    CObject* const Obj = Go->Object;
    if (Obj == nullptr ||
        (static_cast<unsigned>(Obj->ObjectClass) & static_cast<unsigned>(CObject::ClassShip)) !=
            static_cast<unsigned>(CObject::ClassShip))
        return nullptr;
    return static_cast<CShip*>(Obj);
}

void GetPlayerShipWorldPosition3(float OutPos3[3])
{
    // NOT RETAIL (port accessor for the HUD scanner): the player CShip's world position.
    OutPos3[0] = 0.f;
    OutPos3[1] = 0.f;
    OutPos3[2] = 0.f;

    CShip* const Ship = static_cast<CShip*>(GetPlayerShipObject());
    if (Ship == nullptr)
        return;

    OutPos3[0] = Ship->WorldPosition.x;
    OutPos3[1] = Ship->WorldPosition.y;
    OutPos3[2] = Ship->WorldPosition.z;
}

void SetPlayerLaunchAcknowledged(bool bAcknowledged)
{
    G_bPlayerLaunchAcknowledged = bAcknowledged ? 1 : 0;
}

bool IsPlayerLaunchAcknowledged()
{
    return G_bPlayerLaunchAcknowledged != 0;
}

FClientShipGameobj* SpawnPlayerShip()
{
    // FUN_0054b520 (no arguments; returns the gameobj in EAX, which the one caller 0x0054acb3
    // drops).

    // 0x0054b53d..0x0054b564: operator new(0x190) + FUN_00543380.
    FClientShipGameobj* const Go = new FClientShipGameobj();

    // 0x0054b574..0x0054b599: the ship archetype of FUN_004c3e10 (DAT_0067337c), Root vt+0x0c
    // load(), the gameobj's slot 90 (+0x168) create(Arch), then Archetype::Root::Release.
    // Retail tests neither the archetype nor the gameobj.
    Archetype::Ship* const ShipArch = Archetype::GetShip(GetPlayerShipArchetypeIdRetail());
    ShipArch->load();
    Go->create(ShipArch);
    ShipArch->Release();

    // 0x0054b59f..0x0054b5c6: the Object at +0x10 narrowed to a CShip (class & 0x503).
    CObject* const Obj = Go->Object;
    CShip* const Ship =
        (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) &
                            static_cast<unsigned>(CObject::ClassShip)) ==
                               static_cast<unsigned>(CObject::ClassShip))
            ? static_cast<CShip*>(Obj)
            : nullptr;

    // 0x0054b5c6..0x0054b62c: the stack CShip::CreateParms -- FUN_0052e0a0 on the CSimple head,
    // +0x5c = 0, +0x60 = -1, FUN_004cdc10 on +0x64 (zeroes +0x64..+0x70 and the count +0x94),
    // +0x98 = 0, EquipDescVector at +0x9c, the collision group list at +0xac, +0xe8..+0xf0 = 0,
    // byte +0xf4 = 0. Value-initialisation zeroes every one of those.
    CShip::CreateParms Parms{};
    InitCreateParmsScratchRetail(&Parms);
    Parms.DockingCount = 0;
    Parms.VibeId = 0xffffffffu;

    // 0x0054b634..0x0054b658: +0x00 DAT_00673378, +0x04 PTR_DAT_00613ed0, +0x08 DAT_00673344.
    Parms.Id = G_PlayerCharacterId;
    Parms.ObjDb = GetClientObjDb();
    Parms.OwnerPlayer = G_PlayerConnectionId;

    // 0x0054b65c: +0x60 = FUN_004c4670 (the player's reputation vibe, DAT_0061226c).
    Parms.VibeId = static_cast<unsigned>(GetPlayerSpawnSystemIdRetail());

    // 0x0054b668..0x0054b6c6: FUN_004c3de0 (DAT_006728e4): its four header dwords into
    // +0x64..+0x70, its count (+0x30) into +0x94, and that many dwords from its +0x10 into
    // +0x74.
    unsigned const* const VibeHeader = GetPlayerRepVibeScratchRetail();
    Parms.SpaceCostume.PartIds[0] = VibeHeader[0];
    Parms.SpaceCostume.PartIds[1] = VibeHeader[1];
    Parms.SpaceCostume.PartIds[2] = VibeHeader[2];
    Parms.SpaceCostume.PartIds[3] = VibeHeader[3];
    Parms.SpaceCostume.AccessoryCount = GetPlayerRepVibeScratchCountRetail();
    unsigned const* const VibeTail = GetPlayerRepVibeScratchTailRetail();
    for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
        Parms.SpaceCostume.AccessoryIds[Index] = VibeTail[Index];

    // 0x0054b6c8: +0x98 = FUN_004c4650 (DAT_00673360).
    Parms.VoiceId = GetPlayerSpawnScratchScalarRetail();
    // 0x0054b6d4..0x0054b6dc: +0x0c = FUN_004c4f70 (DAT_00673380) * Arch->HitPts (+0x1c).
    Parms.HitPts = GetPlayerHealthScaleRetail() * ShipArch->HitPts;

    // 0x0054b6e0..0x0054b719: EquipDescVector(FUN_004c4060()) assigned into +0x9c; the
    // temporary's buffer is freed at 0x0054b719.
    Parms.Equipment = EquipDescVector(GetPlayerEquipDescListRetail());
    // 0x0054b721..0x0054b781: the list at +0xac assigned from FUN_004c3f00 (DAT_...). The
    // inlined VC6 operator= (copy over the shared prefix, erase the surplus / insert the rest).
    Parms.ArchGroupDescs = GetPlayerCollisionGroupListRetail();

    // 0x0054b783..0x0054b7ad: +0xf0 = Reputation::Vibe::GetRank(FUN_004c4670()), return value
    // ignored (GetRank writes the out value on every path).
    int const VibeId = GetPlayerSpawnSystemIdRetail();
    unsigned Rank = 0;
    Reputation::Vibe::GetRank(VibeId, Rank);
    Parms.Rank = Rank;
    // 0x0054b7a7..0x0054b7b4: wcscpy(Parms + 0xb8, DAT_006732f0), the character's name. Not
    // reproduced: nothing reads retail +0xb8..+0xe7 on this path -- CEqObj::init and
    // CShip::init (Common .text:062b2690..) never touch it (only CSolar::init reads +0xb8), and
    // the port's CreateParms has named CSolar fields there instead.

    // 0x0054b7c7..0x0054b7ca: the CShip's vt+0x10c init(Parms).
    Ship->init(Parms);

    // 0x0054b7d0..0x0054b7e1: CObject::get_archetype(), its companion (+0x04) vt+0x20 Load(1).
    Ship->get_archetype()->Companion->Load(1);

    // 0x0054b7e4..0x0054b7ee: FUN_004252d0(&Parms.Equipment, 1).
    Archetype::LoadMountedEquipmentCompanions(&Parms.Equipment, 1);
    // 0x0054b7f3..0x0054b7fe: FUN_00425480(Go->get_index()).
    Archetype::HookUpInstanceTreeMaterials(Go->get_index());

    // 0x0054b803..0x0054b80a: the gameobj's slot 92 (+0x170).
    Go->Slot170();

    // 0x0054b810..0x0054b813: FUN_00540c30(Go, 1) -- ObjDB bucket + remote-physics block.
    ClientObjDb_RegisterGameobj(Go, true);

    // 0x0054b81b..0x0054b829: multiplayer only, CObject::enable_collisions_r(true).
    if (!SinglePlayer())
        Ship->enable_collisions_r(true);

    // 0x0054b82f..0x0054b835: DAT_00679774 = DAT_00673378.
    G_PlayerShipCharMirror = G_PlayerCharacterId;

    // 0x0054b83b..0x0054b850: point DAT_00679744 at Go+8 unless it already watches it.
    G_PlayerShipWatcher.set(static_cast<Watchable const*>(static_cast<IObjRW*>(Go)));

    // 0x0054b856..0x0054b871: DAT_00678b96 = 0, then IServer (DAT_0067ecd0) slot 48 (+0xc0)
    // PlayerLaunch(DAT_00673378, DAT_00673344). No null test on the server.
    G_bPlayerLaunchAcknowledged = false;
    static_cast<IServer*>(GetLocalServerInterface())
        ->PlayerLaunch(G_PlayerCharacterId, G_PlayerConnectionId);

    // 0x0054b877..0x0054b8e1: the locals' destructors (list, vector) run on scope exit.
    return Go;
}
