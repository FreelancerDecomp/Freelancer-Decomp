// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail CharacterSelect → spawn scratch globals (DAT_00672960 EquipDescList cluster,
// ship archetype DAT_0067337c, health DAT_00673380, system ids, etc.).
// Extractors are called from FUN_005ab250 after CharacterSelect completes; SpawnPlayerShip
// (FUN_0054b520) reads the same globals when building CShip::CreateParms.

#include "common_archetype.h"
#include "common_goods.h"
#include "server_characterset.h"

#include <list>

// --- Getters (retail FUN_004c3e10 / 4060 / 3f00 / 3de0 / 4650 / 4670 / 4f70 / 4830) ---
unsigned GetPlayerShipArchetypeIdRetail();          // FUN_004c3e10 → DAT_0067337c
EquipDescList& GetPlayerEquipDescListRetail();      // FUN_004c4060 → DAT_00672960
unsigned GetPlayerEquipRevisionRetail();            // FUN_004c4f90 → DAT_00673384
unsigned GetPlayerCollisionRevisionRetail();        // FUN_004c4fa0 → DAT_00673388
// FUN_004c43d0 (cdecl) -- set the hardpoint / count / health / equipped state of the player's
// item Id in DAT_00672960 (moving it to another hardpoint re-adds it); false when there is no
// such item. Bumps DAT_00673384.
bool ModifyPlayerEquipRetail(unsigned short Id, CacheString const* Hardpoint, int Count,
                             float Health, unsigned char bEquipped);
// FUN_004c45d0 (cdecl) -- the first entry of DAT_00672960 with this archetype id whose
// bMission (+0x18) is clear; null when there is none. Callers: 0x0048af5f, 0x004c0533,
// 0x004c0540, 0x004dc77b, 0x004dc7ea, HUD_Status 0x004de2fd / 0x004de34f, 0x0052d464.
EquipDesc* FindPlayerNonMissionEquipRetail(unsigned ArchId);
// FUN_004c4320 (cdecl; the first argument is never read) -- remove Count of the player's item
// Id from DAT_00672960 (EquipDescList::remove_equipment_item), bumping DAT_00673384; false when
// there is no such item, then the ObjectShip unmount notify (FUN_00446be0 / FUN_00447850).
// Callers: 0x0043760f, the launcher fire 0x0052d494, 0x0053beda..0x0053e070 (six).
bool RemovePlayerEquipRetail(unsigned char UnusedFlag, unsigned short Id, int Count);
// FUN_004c4170 (cdecl; the first argument is never read) -- DAT_00673384++, add Desc to
// DAT_00672960 (combined when arch_is_combinable), then the GF ObjectShip mount notify
// (FUN_00446be0 / FUN_004477d0). Callers: IClient slot 26 (0x004375ea), slot 86 (0x0053e081).
void AddPlayerEquipRetail(unsigned char UnusedFlag, EquipDesc const* Desc);
std::list<CollisionGroupDesc>& GetPlayerCollisionGroupListRetail(); // FUN_004c3f00
unsigned const* GetPlayerRepVibeScratchRetail();    // FUN_004c3de0 → &DAT_006728e4
int GetPlayerRepVibeScratchCountRetail();           // count at DAT_00672914
unsigned const* GetPlayerRepVibeScratchTailRetail(); // up to 8 rank/accessory dwords after header
unsigned GetPlayerSpawnScratchScalarRetail();       // FUN_004c4650 → DAT_00673360
int GetPlayerSpawnSystemIdRetail();                 // FUN_004c4670 → DAT_0061226c
float GetPlayerHealthScaleRetail();                 // FUN_004c4f70 → DAT_00673380
int GetPlayerRankLevelRetail();                     // FUN_004c55d0 → DAT_0067338c
void SetPlayerRankLevelRetail(int Level);           // FUN_004c55e0 → DAT_0067338c
unsigned GetPlayerCreditsRetail();                  // FUN_004c4ce0 → DAT_00673364
unsigned GetPlayerStarSystemIdRetail();             // FUN_004c4830 → DAT_00673354
unsigned GetPlayerBaseIdRetail();                   // DAT_00673358 (FUN_004c4910)
unsigned GetPlayerFactionScratchRetail();           // DAT_0067335c (FUN_004c4a10)

// --- Setters / sync (CharacterSelect field extractors) ---
void SetPlayerShipArchetypeIdRetail(unsigned ShipArchId); // FUN_004c3e20
void SyncPlayerEquipDescListRetail(EquipDescList const& Source); // FUN_004c4070
void SyncPlayerCollisionGroupListRetail(std::list<CollisionGroupDesc> const& Source); // FUN_004c3f30
void SetPlayerRepRankBufferRetail(unsigned const* SourceHeader4, int Count,
                                  unsigned const* TailDwords); // FUN_004c3d20
void SetPlayerRepVibeScratchRetail(unsigned const* SourceHeader4, int Count,
                                   unsigned const* TailDwords); // FUN_004c3d80
void SetPlayerSpawnScratchScalarRetail(unsigned Value); // FUN_004c4660
void SetPlayerSpawnSystemIdRetail(int SystemId);        // FUN_004c4680
void SetPlayerHealthScaleRetail(float Scale);           // FUN_004c4f80
void SetPlayerCreditsRetail(unsigned Credits, int bFromCharacterSelect); // FUN_004c4cf0
void SetPlayerNumKillsRetail(unsigned Kills);           // FUN_004c4d70 → DAT_00673368
void SetPlayerMissionSuccessesRetail(unsigned Successes); // FUN_004c4d80 → DAT_0067336c
void SetPlayerMissionFailuresRetail(unsigned Failures); // FUN_004c4d90 → DAT_00673370
void SetPlayerStarSystemIdRetail(unsigned SystemId);    // FUN_004c4840 (visit/notify stubbed)
void SetPlayerBaseIdRetail(unsigned BaseId);            // FUN_004c4910 (core assign only)
void SetPlayerFactionScratchRetail(unsigned Value);     // FUN_004c4a10

// The player ship's last position (DAT_006732e4, 3 floats) and orientation (DAT_006732c0,
// 9 floats), refreshed every space frame from the watched ship (0x0054b192 / 0x0054b1b0).
float const* GetPlayerLastPositionRetail();                   // FUN_004c4a20
void SetPlayerLastPositionRetail(float const* Position);      // FUN_004c4a30
float const* GetPlayerLastOrientationRetail();                // FUN_004c4a50
void SetPlayerLastOrientationRetail(float const* Orientation); // FUN_004c4a60

// Zero-init a CSimple::CreateParms-sized scratch (FUN_0052e0a0). Prefer CShip::CreateParms
// default members for new code; kept for listing parity with FUN_0054b520.
void InitCreateParmsScratchRetail(void* ParmsBytes);

// Apply CharacterSelect response fields (FUN_005ab250 extract tail) from one record
// of the character list (src/include/server_characterset.h). Returns false if
// CharInfo is null. Does NOT call IServer::CharacterSelect / wait.
bool ApplyCharacterSelectEquipFields(CCharacterSet::CharacterInfo const* CharInfo);

// FUN_005aa9c0 -- IClient slot 13 (ch2 0x03 FLPACKET_SERVER_CHARACTERINFO) on the
// connect context DAT_0067e7b8: mark the list as arrived (+0x1d4), append a deep
// copy of every record to the context's list (+0x1b4) and keep the set's
// SaveFailureCount (+0x1cc).
void CharacterSelect_ReceiveCharacterInfo(CCharacterSet const& Set);

// DAT_0067e7b8+0x1b4, the list FUN_005aa9c0 fills and FUN_005aad80 / FUN_005aadb0 /
// FUN_005aaca0 walk.
OwnerList<CCharacterSet::CharacterInfo>& CharacterSelect_GetConnectCharacters();

// FUN_005ab380 after the completion flag (CLoginMgr::OnCharacterSelectResponse sets it):
// spawn system id, Reputation::Vibe::SetClientID (multiplayer only), two scratch dwords.
void NotifyCharacterSelectComplete(unsigned const Response4[4]);

// DAT_006732f0 -- the name of the selected character (wcscpy'd by FUN_005ab250; read by
// EnterState case 19 through FUN_005aad30, and by 0x004c0842 / 0x004d328d / 0x0054b7a7).
wchar_t* GetPlayerCharacterNameRetail();

// The player's visit map DAT_00673320 (system / object id -> visit flags).
// FUN_004c4da0 (cdecl, four arguments: every one of the 33 call sites pushes four and cleans
// 0x10) -- the id's flags into *OutFlags; false (nothing written) when the id has no entry.
// Arg3 / Arg4 are not read by the body; the callers pass (0, 0), zones (1, 0) -- 0x0049398d,
// 0x00497f5c, 0x00498b82, 0x0049b71b.., 0x004a1c45 -- and 0x004a8bb3 / 0x0051cfea (0, 1).
// Their meaning is not recovered.
bool GetPlayerVisitFlagsRetail(unsigned Id, unsigned char* OutFlags, unsigned Arg3,
                               unsigned Arg4);
// FUN_004c4e00 (cdecl) -- unless the entry already carries every bit of Flags, store Flags as
// the entry's value (a store, not an OR) and, with bNotifyServer, send IServer::SetVisitedState
// (vt+0x11c) one {count 1, Id, Flags} record.
void SetPlayerVisitFlagsRetail(unsigned Id, unsigned char Flags, bool bNotifyServer);
// The map clear FUN_004c4a80 / FUN_004c4bc0 inline (not a retail function of its own).
void ClearPlayerVisitMapRetail();
// The two stores FUN_004c4a80 inlines (0x004c4a8a / 0x004c4a94): DAT_0067337c = 0 and
// DAT_00673380 = 0 -- plain stores, not FUN_004c3e20 / FUN_004c4f80. Not a retail function.
void ClearPlayerShipArchAndHealthRetail();
// FUN_004c4000 (cdecl) -- the health of the player's collision group Id in DAT_0067296c, then
// DAT_00673388++ (also when there is no such group). Caller: IClient slot 63 (0x0053bf3a).
void SetPlayerCollisionGroupHealthRetail(unsigned short Id, float Health);
