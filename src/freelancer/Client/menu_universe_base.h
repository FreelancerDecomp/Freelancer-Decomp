// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Gf\LocationController.cpp -- the singleton DAT_00668708: the base / room registry and
// the base and location changes (enter, leave, load, unload) every client state goes through.

class RoomData;

// FUN_004393b0 — LocationController base/room registry, built once after DataConfig. Retail
// reaches it through the menu pre-init chain: singleton FUN_00437420, vtable slot 8
// (LAB_00437270) → FUN_00558850 → FUN_004393b0(DAT_00668708).
void BuildClientLocationMaps();
void ClearClientLocationMaps();
// FUN_0043bab0's lookup through this+0x0c (record[0] = RoomData*). Null when the id is unknown.
RoomData* LookupClientLocationRoom(unsigned LocationId);
// FUN_0043a2b0 -- this+0x20, keyed by BaseData::BaseId. Returns BaseData*; an unknown id FDUMPs
// and reads through null, as retail.
void* LookupClientLocationBase(unsigned BaseId);
unsigned GetClientLocationBaseCount();
unsigned GetClientLocationRoomCount();

// LocationController field accessors (FUN_0043a460/a5a0/a630/a860/ -- a510 and b630 are
// GetClientLocationBaseMarket / GetClientCurrentLocationBaseData in client_base_market.h;
// a8d0/aaf0/ab60). Runtime/State pointers are opaque (FLocationBaseRuntime/State are file-local
// types); they are null until the base (FUN_004397a0) / room (FUN_00439d80) is loaded.
void* GetClientLocationBaseRuntime(unsigned BaseId);          // FUN_0043a8d0
// FUN_0043a8d0 again, typed: the runtime's +0x00 MissionVendorProperties cache
// (mirror_property_bucket.h), which is what NN_MissionVendor walks.
template <class T> struct TMirrorPropertyBucket;
class MissionVendorProperties;
TMirrorPropertyBucket<MissionVendorProperties>* GetClientLocationBaseMissionOffers(unsigned BaseId);
// FUN_0043a630 — the runtime's +0x18 NewsBroadcastProperties cache, which NN_News walks.
class NewsBroadcastProperties;
TMirrorPropertyBucket<NewsBroadcastProperties>* GetClientLocationBaseNews(unsigned BaseId);
void SetClientLocationBaseStartLocation(unsigned BaseId, unsigned LocationId); // FUN_0043a5a0
void UpsertClientLocationBaseMissionOffer(MissionVendorProperties const& Offer); // FUN_0043a960
void EraseClientLocationBaseMissionOffer(unsigned BaseId, unsigned long Key);   // FUN_0043a9f0
void UpsertClientLocationBaseNews(NewsBroadcastProperties const& News);         // FUN_0043a6c0
void EraseClientLocationBaseNews(unsigned BaseId, unsigned long Key);           // FUN_0043a750
void SetClientLocationBaseMissionListComplete(unsigned BaseId); // FUN_0043aaf0 (+0x14)
void SetClientLocationBaseNewsListComplete(unsigned BaseId);    // FUN_0043a860 (+0x2c)
void* GetClientLocationRoomState(unsigned LocationId);        // FUN_0043ab60
// The room state's MirrorDatabase buckets (the 0x48-byte object FUN_00439d80 allocates at room
// entry +0x04). Callers: IClient slots 30..37 (client_iclient.cpp).
class CharacterProperties;
class ScriptBehavior;
class AmbientScriptProperties;
void UpsertClientLocationRoomCharacter(CharacterProperties const& Character);          // FUN_0043abf0
void SetClientLocationRoomCharactersComplete(unsigned LocationId);                     // FUN_0043ace0
void UpsertClientLocationRoomScriptBehavior(ScriptBehavior const& Behavior);           // FUN_0043ade0
void EraseClientLocationRoomScriptBehavior(unsigned LocationId, unsigned long Key);    // FUN_0043ae70
void SetClientLocationRoomScriptBehaviorsComplete(unsigned LocationId);                // FUN_0043af80
void UpsertClientLocationRoomAmbientScript(AmbientScriptProperties const& Script);     // FUN_0043b080
void EraseClientLocationRoomAmbientScript(unsigned LocationId, unsigned long Key);     // FUN_0043b110
void SetClientLocationRoomAmbientScriptsComplete(unsigned LocationId);                 // FUN_0043b220
// FUN_0043ab60 (typed) / FUN_0043ad50 -- the room state's +0x00 / +0x18 buckets. Caller:
// RoomControl FUN_00449f40.
TMirrorPropertyBucket<CharacterProperties>* GetClientLocationRoomCharacters(unsigned LocationId);
TMirrorPropertyBucket<ScriptBehavior>* GetClientLocationRoomScriptBehaviors(unsigned LocationId);
// FUN_0043aff0 -- the room state's +0x30 bucket. Caller: RoomControl FUN_0044a540.
TMirrorPropertyBucket<AmbientScriptProperties>* GetClientLocationRoomAmbientScripts(
    unsigned LocationId);
// FUN_0043a340 (RET 4) -- the location's RoomData is loaded (RoomData +0x08). Caller: the Gf
// script sequence stop FUN_004554c0 (0x004554f7).
bool IsClientLocationRoomLoaded(unsigned LocationId);
void* GetClientCurrentLocationRuntime();                      // FUN_0043a460
void UnloadClientLocationRoomState(unsigned LocationId);      // FUN_0043a080

// LocationController (DAT_00668708) base and location changes. Every IServer notification
// (BaseEnter / BaseExit / LocationEnter / LocationExit / *InfoRequest) goes to DAT_0067ecd0
// with the session DAT_00673344 when both the server exists and this+0x5a is clear.
// IntroFocus writes +0x5a = 1 before entering an intro base (0x00573919), then clears it
// when leaving that base (0x0057409b / 0x0057412c / 0x00574ad8).
void SetClientLocationNoServerNotify(bool bDisabled);
//
// FUN_00439200 -- +0x34..+0x44, +0x48, +0x4c, +0x50 and +0x59 zeroed.
void ResetClientLocationController();
// FUN_00439540 (RET 4) -- count a queued change's timer up; once it reaches the delay, change
// to the queued location (FUN_0043b6b0), run its audio (FUN_0043bab0) when asked, clear the
// queue. Caller: the Gameflow tick FUN_00558ac0.
void TickClientLocationController(float DeltaSeconds);
// FUN_0043ba90 (RET 0xc) -- queue a location change after Delay seconds.
void QueueClientLocationChange(unsigned Location, float Delay, bool bWithAudio);
// FUN_0043b290 (RET 4) -- enter BaseId: BaseEnter, load it at a zero count, count + 1, then
// its start location (the server's, runtime+0x48, else BaseData's) through FUN_0043bab0 and
// FUN_0043b6b0. Caller: FUN_005588a0 (no location given).
void EnterClientLocationBase(unsigned BaseId);
// FUN_0043b570 (RET 4) -- the base half of FUN_0043b290 alone. Caller: FUN_005588a0 (a
// location given).
void SetClientLocationBase(unsigned BaseId);
// FUN_0043b6b0 (RET 4) -- move to Location: across bases (leave the room, unload/exit the old
// base, enter/load the new one, FUN_004c4910 / FUN_004c4a10 / FUN_0043d590) or within one
// (leave the room), then the new location's records (+0x3c / +0x40), LocationEnter and the room
// loaded at a zero count, count + 1. +0x59 is set for the length of the call.
void ChangeClientLocation(unsigned Location);
// FUN_0043bab0 (RET 4) -- the location's music and ambience (RoomData +0xcc..+0xd5) and the Gf
// audio fade. Callers outside the controller: FUN_005588a0 (0x00558997, a location given).
void ApplyClientLocationAudio(unsigned LocationId);
// FUN_005588a0 0x0055896f -- DAT_00668760 (the controller's this+0x58) = 1: the next
// FUN_0043bab0 keeps a non-looping music / ambience, then clears the byte.
void SetClientLocationKeepAudio();
// FUN_0043b3e0 -- leave the current location and base (LocationExit / BaseExit, rooms and base
// unloaded when their counts reach 0). Caller: FUN_00558a50 (and the menu 0x00574053 /
// 0x005740e4 / 0x00574a90).
void LeaveClientLocationBase();
// FUN_0043bbc0 -- draw the current location: +0x40's runtime through FUN_0044b570. Caller: the
// base frame FUN_00558c10.
void DrawClientLocation();
unsigned GetClientLocationCurrentBase();  // this+0x34
// FUN_0043bc90 (thiscall on DAT_00668708) -- this+0x54, the loaded base's solar object id
// (FUN_004397a0). Callers (unported): 0x00440819 / 0x0044103e / 0x004411f9.
unsigned GetClientLocationBaseSolarId();
// FUN_0043bbe0 (thiscall on DAT_00668708) -- the strid_name of the base the player is in: the
// this+0x34 base looked up (FUN_00439590 inlined, FDUMP on a miss), its BaseData's id through
// Universe::get_base, then IBase slot 2 (+0x08, [+0x0c] = strid_name). 0 when the entry has no
// BaseData or the universe has no such base. Callers: NN_ArrivalText 0x0045aeb4, the dealer
// 0x00481504.
int GetClientCurrentBaseStridName();
unsigned GetClientLocationCurrentRoom();  // this+0x38
// this+0x59 (DAT_00668761): zeroed by the ctor (0x00438eef) and FUN_00439200 (0x0043921a),
// raised for the length of FUN_0043b6b0 (0x0043b6bf / 0x0043b963) and set by the base enter
// FUN_005588a0 (0x005588bc) right after its FUN_00439200. Readers outside the controller
// address it absolutely: FUN_0045a740 (0x0045a75b), NN_MainButton 0x0045cb09, NN_Pause
// FUN_004a75c0 (0x004a75de).
bool IsClientLocationInTransition();
// FUN_005588a0 0x005588bc -- MOV byte [DAT_00668761],1.
void SetClientLocationInTransition();

// FUN_00438c90 — the current room's camera, or null when no room is entered. The object
// answers vtable slots 3 (position) and 4 (orientation), which is all FUN_00428040 asks of it.
void* GetClientRoomCamera();
