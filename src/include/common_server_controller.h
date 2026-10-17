// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>
#include "server_exports.h"

namespace pub {
namespace Controller {

// Values and names from Server.dll's priority table at .data:06d8d080 (five 0x14-byte
// rows {char[16] name, int value}), read by FUN_06d43840 for the [Initial * DLLs]
// `DLL` rows; Controller::Create keeps the list ordered by it, lowest value first.
enum PRIORITY { HIGHEST = 0, ABOVE_NORMAL = 1, NORMAL = 2, BELOW_NORMAL = 3, LOWEST = 4 };

// The message id of _SendMessage and IDLL::HandleMessage (Content.dll's controllers). Retail
// has no names for these; a name here comes from the sending function (the IServer method,
// the ContentCallback method), from the Cnd_* key that Content's mission triggers bind to the
// id (FUN_06ebb850, 06ebb890-06ebce72), or from the payload. Ids 0x00..0x41 are sent by
// Server.dll (through FUN_06d43650, i.e. _SendMessage(&0u, id, data), unless noted), 0x1000..
// 0x1011 by Content.dll. Neither module sends 0x0e, 0x0f or 0x34, and no port receiver has a
// case for them.
// Each line: sender .text address(es) in Server.dll / Content.dll, then the payload.
enum class EMessage : int
{
    PlayerWorthChanged    = 0x00, // PlayerData cash / cargo / equipment setters 06d4ef87 06d4f0dd 06d4f157 06d4f1e3 06d4f2da 06d4f4d0 06d4f548 06d55090 06d55466 06d56450: &client id
    BaseEnter             = 0x01, // IServerImpl::BaseEnter 06d1776c: {base, player}
    BaseExit              = 0x02, // IServerImpl::BaseExit 06d1791b: {base, player}
    CargoScanned          = 0x03, // IServerImpl::SPScanCargo 06cf12fe: {scanner, target} (Cnd_CargoScanned)
    PlayerSave            = 0x04, // player save writer 06d4dee1: {player, FILE*}
    PlayerLoad            = 0x05, // character load 06d4b132; Content Story 06ea6abb: {player, INI_Reader*}
    PlayerLaunch          = 0x06, // IServerImpl::PlayerLaunch 06cefbf9: {player, exited base} (Cnd_PlayerLaunch)
    PlayerLogin           = 0x07, // IServerImpl::Login (result 3) 06d450f9, character select 06d4a5af 06d4a7c0: &player
    PlayerLogout          = 0x08, // PlayerDB logout / character switch 06d4a535 06d4a72a 06d5447a: &player
    GroupMemberLeft       = 0x09, // CPlayerGroup::DelMember 06d5b971: &player
    GroupMemberJoined     = 0x0a, // CPlayerGroup::AddMember 06d5b85b: &player
    CommComplete          = 0x0b, // IServerImpl::CommComplete 06cf08c9; Content Gcs resend 06ee8d38: 5 dwords (Cnd_CommComplete)
    ControllerDestroyed   = 0x0c, // pub::Controller::Destroy 06d43ffc (direct _SendMessage): &flid
    ObjectDamaged         = 0x0d, // damage paths 06ceb607 06ceb951 06cebbac 06cebdf7 06d02e66 06d02fdd: FSubObjDamageEvent / FSolarHitEvent (Cnd_HealthDec, Cnd_ProjHit)
    DockCancel            = 0x10, // Ship request cancel, type 0, 06d027e3 -> 06d02845: {ship, target, arg}
    DockComplete          = 0x11, // StarSystem complete_dock 06d0ca85: {ship, dock, system}
    DockRequest           = 0x12, // Ship request, type 0, 06d026e7: {ship, target, system, byte}
    EquipDropLoot         = 0x13, // equipment about to drop as loot 06ce9da2 06cea109: FEquipLootEvent (receiver may rewrite Desc)
    TradelaneRequest      = 0x14, // Ship request, type 2, 06d02771: {ship, ring, second ring, system}
    TradelaneRelease      = 0x15, // Ship request cancel, type 2, 06d02845: {ship, target, system}
    TradelaneEnter        = 0x16, // StarSystem notify_go_tradelane 06d0dfce: {ship, entrance, exit, system} (Cnd_TLEntered)
    TradelaneExit         = 0x17, // StarSystem notify_stop_tradelane 06d0e1a6: {ship, entrance, exit, system} (Cnd_TLExited)
    LootAcquired          = 0x18, // StarSystem tractor_loot 06d0ee80: {loot, ship, good, sub-object, count} (Cnd_LootAcquired)
    FormationRequest      = 0x19, // Ship request, type 1, 06d02727: {ship, leader, system}
    FormationLeave        = 0x1a, // Ship request cancel, type 1, 06d02814 -> 06d02845: {ship, target, system}
    GFObjSelect           = 0x1b, // IServerImpl::GFObjSelect 06d45752: {object, player} (Cnd_CharSelect)
    GFGoodBuy             = 0x1c, // IServerImpl::GFGoodBuy 06d17be1: SGFGoodBuyInfo dwords + player
    GFGoodSell            = 0x1d, // IServerImpl::GFGoodSell 06d17c1a: SGFGoodSellInfo dwords + player
    GFGoodVaporized       = 0x1e, // IServerImpl::GFGoodVaporized 06d17c5a: SGFGoodVaporizedInfo dwords + player
    Hail                  = 0x1f, // IServerImpl::Hail 06cf083d: {object, arg1, arg2}
    Heartbeat             = 0x20, // controller heartbeat walk 06d4377d (vtbl[0] direct, not _SendMessage): float elapsed
    InterfaceItemUsed     = 0x21, // IServerImpl::InterfaceItemUsed 06d457ca: {player, item}
    RtcDone               = 0x22, // IServerImpl::RTCDone 06d457fa: {player, rtc} (Cnd_RTCDone)
    CargoJettisoned       = 0x23, // Ship jettison request 06d02179: {ship, word equip id, good, count}
    AbortMission          = 0x24, // IServerImpl::AbortMission 06d4582a: {player, mission}
    // TODO(name): sent by IServerImpl::SystemSwitchOutComplete 06cef791 as {ship, gate,
    // system} and for every dying ship 06d00221 as {ship, death-sequence object, system};
    // SpaceSupport destroys Msg[0] when it is one of its system's objects.
    Msg0x25               = 0x25,
    JumpInComplete        = 0x26, // Ship 06d02092 (vtable slot 0x74): {arg, ship} (Cnd_JumpInComplete)
    // TODO(name): StarSystem dock-list update 06d0c2bf (land = 1) / 06d0c337 (land = 0):
    // {ship, base, target, byte land}; SpaceSupport cancels the ship's docking when land != 0.
    Msg0x27               = 0x27,
    LaunchComplete        = 0x28, // Ship 06d0205a (vtable slot 0x70): {id, ship} (Cnd_LaunchComplete)
    LocationEnter         = 0x29, // IServerImpl::LocationEnter 06d179fc: {location, player} (Cnd_LocEnter)
    LocationExit          = 0x2a, // IServerImpl::LocationExit 06d178da 06d17a88: {location, player} (Cnd_LocExit)
    LootSpawned           = 0x2b, // StarSystem create_loot 06d08d92: {loot, source object, good, count}
    MissionResponse       = 0x2c, // IServerImpl::MissionResponse 06d4579a; Content 06ea8ec2 06eb51dc: FMissionOfferAcceptMsg {offer id, kind, byte accepted, player} (Cnd_MsnResponse)
    PhantomEnter          = 0x2d, // phantom pump 06cffe09: {object, phantom, system}
    PhantomExit           = 0x2e, // phantom pump 06cffe59: {object, phantom, system}
    PlayerManeuver        = 0x2f, // IServerImpl::SetManeuver (single player) 06cf15d1: {player, object, value} (Cnd_PlayerManeuver)
    PopUpDialog           = 0x30, // IServerImpl::PopUpDialog 06cf100a: {player, button} (Cnd_PopUpDialog)
    RequestBestPath       = 0x31, // IServerImpl::RequestBestPath 06cf0f88: 10 dwords
    FreeTerminalRequest   = 0x32, // launch position search 06d00ab7: {player, dock target}
    RequestPlayerStats    = 0x33, // IServerImpl::RequestPlayerStats 06cf0fd9: &player
    PlayerShipArchSet     = 0x35, // PlayerData ship arch setter 06d4ece6: &client id
    SpaceEnter            = 0x36, // IServerImpl::PlayerLaunch 06cf017e: {system, player, launch object, launch kind} (Cnd_SpaceEnter)
    SpaceExit             = 0x37, // StarSystem on_object_destroyed (the player's ship) 06d0b22b: {system, player} (Cnd_SpaceExit)
    DeathFuseLit          = 0x38, // death fuse 06ce812a 06cec66a: FDeathFuseEvent
    ObjectDestroyed       = 0x39, // 06ce833b 06cf2702 06d00523: FObjectDestroyedEvent (Cnd_Destroyed)
    SystemExit            = 0x3a, // 06cf16cf 06d0883e 06d088ee 06d176f4 06d49a5a 06d4a50f 06d4a6e4 06d54391 06d54450: {system, destination system, player} (Cnd_SystemExit)
    SystemEnter           = 0x3b, // 06cef9b2 06cf176f 06d1773a 06d455a6: {system, from system, player, arrival object, position} (Cnd_SystemEnter)
    SystemLoaded          = 0x3c, // StarSystem load 06d07101: &system
    SystemUnloaded        = 0x3d, // StarSystem shutdown / last player out 06d07157 06d08873 06d08923: &system
    TimerExpired          = 0x3e, // pub::SetTimer expiry 06d5e014 (to the timer's own controller): TimerExpired
    TradeResponse         = 0x3f, // IServerImpl::TradeResponse 06d45923: TradeResponseInfo members + player
    LoadGame              = 0x40, // server state load 06d448b7 06d44de0: INI_Reader*
    SaveGame              = 0x41, // server state save 06d44800: FILE*
    MissionStart          = 0x1000, // Content Story 06ea6382 06ea6acd 06ea6c46 (to the story mission): no payload
    CombatState           = 0x1001, // Content combat_state_notification 06ed5a53 06f340e3: {type, param1, param2, system}
    CombatDrift           = 0x1002, // Content combat_drift_event 06f3407e: {ship, system}
    NpcTurnedHostile      = 0x1003, // Content attitude sweep 06f70c13 06f70cf5: {object, other, system}
    FleeRequest           = 0x1004, // Content request_flee_destination 06f34ed0: {ship, reason, system}
    DamageReputation      = 0x1005, // Content combat reputation 06f70316 06f70409 06f7044b: {provoked, attacker, victim, system}
    // TODO(name): Content FUN_06f34100 06f341d4, when an NPC ship's comm-line counter
    // (+0x1b8, counted down per finished comm line) reaches 0: {ship, a dword of its first
    // directive picked by directive type, 1} to the ship's controller.
    Msg0x1006             = 0x1006,
    JumpgateAct           = 0x1007, // Content dock terminal apply 06f6bd59: &dock target (Cnd_JumpgateAct)
    LeadObject            = 0x1008, // Content lead_object_event 06f34631: {object, object, progress, value, position}
    FormationLeaderChanged = 0x1009, // Content 06f5e802: {new leader, old leader}
    NpcSystemExit         = 0x100a, // Content NPC ship system change 06f34860: {ship, old system} (Cnd_NPCSystemExit)
    PlayerMissionChanged  = 0x100b, // Content player state 06f486f7 06f489ae 06f48afd 06f48b66 06f48bd4: {mission controller, player}
    EncounterLaunched     = 0x100c, // Content 06f152ce: std::list<uint> of encounter ids (Cnd_EncLaunched)
    RumorHeard            = 0x100d, // Content BaseSupport 06eb4c9f: {player, TString<32> role} (Cnd_RumorHeard)
    ShipInSights          = 0x100e, // Content ship_in_sights_event 06f34036: {ship, object, system}
    NpcSystemEnter        = 0x100f, // never sent: a mission-trigger event made by Content 06ebab90 / 06eba8a4: {ship, system, object} (Cnd_NPCSystemEnter)
    NpcShipCreated        = 0x1010, // Content 06f60b25 06f73557: FContentNpcShip* by address
    TetherObject          = 0x1011, // Content tether_object_event 06f343e8 (also a mission-trigger tick, 06eb8fa7): {object, object, 0, 0} (Cnd_TetherBroke)
};

// Content.dll StoryLauncher (LAB_06ea6f50) passes a polymorphic parameter object
// whose payload is the player ID read from message 7. Server forwards it unchanged.
struct CreateParms { virtual ~CreateParms() = default; };
struct FStoryCreateParms final : CreateParms { unsigned int PlayerId; };
struct TimerExpired { unsigned int Param1; unsigned int Param2; };

SERVER_API unsigned int __cdecl Create(const char* DllPath, const char* InstanceName, const CreateParms* Parms, PRIORITY Priority);
SERVER_API void __cdecl Destroy(unsigned int Flid);
SERVER_API int __cdecl SetHeartbeatInterval(const unsigned int& Flid, float Interval);
SERVER_API int __cdecl _SendMessage(const unsigned int& Flid, EMessage MsgId, const void* MsgData);

} // namespace Controller

SERVER_API int __cdecl SetTimer(const unsigned int& Flid, const Controller::TimerExpired& Timer, float Timeout);

} // namespace pub

// Server.dll-internal, not exported: pub::Controller::_SendMessage's dispatch core
// (IServerImpl.cpp, retail 0x06d44080). Retail FUN_06d43650 is only
// _SendMessage(&0u, MsgId, MsgData), so Server.dll calls Server_ControllerDispatch(0, ...)
// for it. (Server_SendMessage, the port's Common.dll-exported "C wrapper for FUN_06d43650"
// over a handler table nobody filled, is gone.)
extern "C" int __cdecl Server_ControllerDispatch(unsigned int Flid, pub::Controller::EMessage MsgId,
                                                 const void* MsgData);
