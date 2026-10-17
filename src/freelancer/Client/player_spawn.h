// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail player ship spawn (FUN_0054b520) and space-connect prelude (FUN_0054ab40).
// The spawn builds the player's Ship gameobj (FClientShipGameobj, FUN_00543380) exactly as
// retail does: slot 90 create, CShip::init from the character-select globals, the ObjDB
// registration FUN_00540c30(Go, 1) and the BaseWatcher DAT_00679744 on it.

#include "common_ini.h" // Vector

class FClientShipGameobj;

// DAT_00672978 -- one of the player's 99 waypoint records (stride 0x18), counted by
// DAT_00673374. Layout straight from FUN_004c4780's stores; the two middle dwords are its
// second and third arguments. +0x0c is the waypoint's star system (the HUD compares it with
// the player's system, the nav map passes the current ISystem's id); +0x10 is the object the
// waypoint sits on, 0 for none (FUN_005416c0 lookup and the target command in the HUD).
struct FPlayerWaypointRecord
{
    Vector Pos;          // +0x00
    unsigned SystemId;   // +0x0c
    unsigned ObjectId;   // +0x10
    int AutoId;          // +0x14  largest id + 1 at insert time
};
static_assert(sizeof(FPlayerWaypointRecord) == 0x18, "waypoint record 0x18");

int GetPlayerWaypointCountRetail();                    // FUN_004c4690
FPlayerWaypointRecord* GetPlayerWaypointRetail(int Index); // FUN_004c46a0 (null out of range)
void ResetPlayerWaypointsRetail();                     // FUN_004c46c0
void RemovePlayerWaypointRetail(int Index);            // FUN_004c46e0
int GetPlayerWaypointMaxIdRetail();                    // FUN_004c4750
bool AddPlayerWaypointRetail(Vector const& Pos, unsigned SystemId, unsigned ObjectId); // FUN_004c4780
// FUN_004c4ea0 -- DAT_00673390, the third argument IClient slot 93 gives its last waypoint
// (0x0053c395). Its only writer is FUN_004c4eb0 below.
unsigned GetPlayerWaypointTailTagRetail();
// FUN_004c4eb0 (cdecl) -- DAT_00673390 = Tag, and ask the server for a best path From -> To
// (IServer slot 72 RequestBestPath, 0x34 bytes: player vibe DAT_0061226c, 2, From, the player's
// system DAT_00673354, To, Field30). Caller: the nav map's best-path button (0x00493c0b).
void RequestBestPathRetail(Vector const& From, Vector const& To, unsigned Tag, unsigned Field30);

// The multiplayer group mission-offer answer. DAT_00673350 is the Gf ScriptSequence showing
// the offer (the sequence at 0x004559b0 stores itself there, 0x00455a07, when it asks a type-3
// offer in multiplayer; its FUN_004557d0 closes the NN_OfferDialog and clears it, 0x004557d3).
// DAT_00673348 is non-zero once the server answered (read at 0x004559ef / 0x00455a49 /
// 0x00456027); DAT_0067334c is the connection id of the player whose answer it is, compared
// with DAT_00673344 at 0x00456039.
struct FGfScriptSequence;
void ResetMissionOfferAnswerRetail();                                       // FUN_004c55b0
void SetMissionOfferAnswerRetail(unsigned Answered, unsigned AcceptorId);   // IClient slot 114
unsigned GetMissionOfferAnsweredRetail();                                   // DAT_00673348
unsigned GetMissionOfferAcceptorIdRetail();                                 // DAT_0067334c
FGfScriptSequence* GetMissionOfferSequenceRetail();                         // DAT_00673350
void SetMissionOfferSequenceRetail(FGfScriptSequence* Sequence);            // 0x00455a07

unsigned GetPlayerShipArchetypeId();   // retail DAT_0067337c
float GetPlayerShipHoldSizeRetail();    // retail FUN_004c4610 (0 if no ship archetype yet)
unsigned GetPlayerCharacterId();       // retail DAT_00673378
// IClient slot 61 (LAB_0053d920, REQUESTCREATESHIPRESP): DAT_00678b95 = 1, DAT_00678b94 = Result,
// DAT_00673378 = CharacterId. The waiter on +0x95 is FUN_0054ab40 (0x0054abcf, InitSpaceState).
void SetPlayerCreateShipResponse(unsigned char Result, unsigned CharacterId);
bool IsPlayerCreateShipAnswered();             // DAT_00678b95
unsigned char GetPlayerCreateShipResult();     // DAT_00678b94
// FUN_00548eb0 (cdecl) -- Id & 0x3fffffff non-zero and equal to DAT_00679774, the ship id the
// spawn (0x0054b835) stores. 13 call sites: HUD_Target_Base 0x004e6f15 (result unused there),
// 0x0052d44a, 0x0053566f, 0x0053bca0..0x0053e01c (nine), 0x00545b23, 0x00545bc5.
bool IsPlayerShipId(unsigned Id);
// DAT_00679774 itself -- read directly by the system-switch step FUN_0054a940 (0x0054aacc), which
// hands it to IServer::SystemSwitchOutComplete.
unsigned GetPlayerShipId();
unsigned GetPlayerConnectionId();        // retail DAT_00673344
// Writers of DAT_00673344 outside the spawn code: CLoginMgr::OnConnectResponse (.text:005a9c14),
// the disconnect paths of FUN_005a8080 / 005a8260 / 005a8770 / 005a8840 (store 0) and IClient
// slot 17 (.text:005a5a71, store 0).
void SetPlayerConnectionId(unsigned Id);
// The watched pointer of the BaseWatcher DAT_00679744: the player gameobj's Watchable
// (IObjRW+8), or null once that gameobj is gone. FUN_0054baf0 subtracts eight from it.
void* GetPlayerShipWatcher();
// The SpaceApp ctor's step on DAT_00679744 (0x005493f8..0x0054940b): unhook it when it holds
// something.
void ClearPlayerShipWatcher();
// NOT RETAIL (port accessor): the CShip of the watched player gameobj, or null.
void* GetPlayerShipObject();
// NOT RETAIL (port accessor): that CShip's world position, zero without one.
void GetPlayerShipWorldPosition3(float OutPos3[3]);
// DAT_00678b96: cleared by the spawn before IServer::PlayerLaunch (0x0054b867), set by the
// IClient launch handler (0x0053d87e / 0x0053d8da), waited on by FUN_0054ab40 (0x0054ae30).
void SetPlayerLaunchAcknowledged(bool bAcknowledged);
bool IsPlayerLaunchAcknowledged();
// The space init's side of DAT_00678b94 / 95 / 96 (FUN_0054ab40): both create-ship bytes cleared
// before IServer::RequestCreateShip (0x0054abbe / 0x0054abc4), and the addresses of the two bytes
// FUN_005a89b0 waits on (0x0054abb9 and 0x0054ae2b).
void ClearPlayerCreateShipResponse();
unsigned char* GetPlayerCreateShipAnsweredByte();
unsigned char* GetPlayerLaunchAcknowledgedByte();

FClientShipGameobj* SpawnPlayerShip();                   // FUN_0054b520
