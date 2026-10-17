// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Server.dll's character-file entry points. RENAMED AND DE-EXPORTED 2026-09-17
// (wave 17): this used to be common_server_savegame.h and these functions used
// to be COMMON_API, i.e. Common.dll exports, even though every caller is in
// Server.dll. Nothing here crosses a DLL boundary, so nothing here is exported.
//
// REWRITTEN 2026-09-24 (claude:srv-save). The save/load family is now the
// retail functions themselves, one port per listing function:
//
//   FUN_06d47120  PlayerData_LoadSaveRecord       the character-file parser
//   FUN_06d48d50  PlayerData_LoadSavedCharacter   login scan: one file -> record
//   FUN_06d4b5a0  PlayerData_ReloadSavedCharacter the post-save reload
//   FUN_06d4c800  PlayerData_SaveGame             named save / "SaveXXXX" search
//   FUN_06d4c600  PlayerData_MissionSave          save + copy to AutoSave.fl
//   FUN_06d4c430  PlayerData_AutoSave             the autosave
//   FUN_06d4c5f0  PlayerData_SkipNextAutosave     the one-shot skip latch
//   FUN_06d4b790  PlayerData_CreateNewCharacter   new-character template expansion
//   FUN_06d4a5d0  PlayerData_SelectCharacter      head + MP front (server_classes.cpp)
//                 PlayerData_LoadSelectedCharacter its tail (.text:06d4aa85 ..)
//                 PlayerData_ApplySavedCharacter  its record -> slot copy
//   FUN_06d4b520  PlayerData_SendVisitedState     IClient +0x170
//   FUN_06d50740  PlayerData_SendMissionLog       IClient +0x184 (+ FUN_06d50850)
//                 (.text:06d4ac77 .. .text:06d4b089)
//
// The implementation is src/Server/server_savegame.cpp; the writer they all
// end in is PlayerData_SerializeToFile (retail FUN_06d4ccd0, server_pub.cpp).

#include "common_server_playerdb.h"

// Retail FUN_06d47120 (thiscall on the PlayerData, `RET 8`). Parses the
// character file at Path into Record. Returns true when a [Player] section was
// seen (the byte set at .text:06d472ea).
bool __cdecl PlayerData_LoadSaveRecord(PlayerData& Slot, char const* Path,
                                       PlayerData::FSavedCharacter& Record);

// Retail FUN_06d48d50 (thiscall, `RET 8`): parse <Dir>\<FileName> into
// Slot.saved_characters[FileName]; on failure warn and erase the entry.
bool __cdecl PlayerData_LoadSavedCharacter(PlayerData& Slot, char const* Dir,
                                           char const* FileName);

// Retail FUN_06d4b5a0 (thiscall, `RET 4`): re-read a file the slot has just
// written into its own record container.
void __cdecl PlayerData_ReloadSavedCharacter(PlayerData& Slot, char const* FileName);

// Retail FUN_06d4c800 (thiscall, `RET 0xc`). Name "" picks a free "Save%04x".
bool __cdecl PlayerData_SaveGame(PlayerData& Slot, char const* Name,
                                 wchar_t const* Description, unsigned int DescripStrId);

// Retail FUN_06d4c600 (thiscall, `RET 4`). Returns CopyFileA's result, or false
// when the save itself failed.
bool __cdecl PlayerData_MissionSave(PlayerData& Slot, unsigned int DescripStrId);

// Retail FUN_06d4c430 (thiscall, `RET`).
bool __cdecl PlayerData_AutoSave(PlayerData& Slot);

// Retail FUN_06d4c5f0: `MOV byte ptr [ECX + 0x340],0x1 / RET`.
void __cdecl PlayerData_SkipNextAutosave(PlayerData& Slot);

// The record -> live slot copy of retail FUN_06d4a5d0 (.text:06d4ac77 ..
// .text:06d4b089), including the rep-vibe rebuild. SessionId is the worker's
// own stack argument (.text:06d4acef `MOV ECX,[ESP + 0x192c]`).
void __cdecl PlayerData_ApplySavedCharacter(PlayerData& Slot,
                                            PlayerData::FSavedCharacter const& Record,
                                            unsigned int SessionId);

// Retail FUN_06d4b520 (thiscall, `RET 4`): the visit map packed as {count,
// {u32 id, u8 flags}...} into an operator-new block sent by IClient +0x170.
void __cdecl PlayerData_SendVisitedState(PlayerData& Slot, unsigned int SessionId);

// Retail FUN_06d50740 (thiscall, `RET 4`): the mission log flattened by
// AppendMissionLogData into an operator-new block sent by IClient +0x184.
void __cdecl PlayerData_SendMissionLog(PlayerData& Slot, unsigned int SessionId);

// The tail of retail FUN_06d4a5d0 (.text:06d4aa85 .. .text:06d4b1fa), which the
// SP and MP paths share: re-open the record's file, the SP [Time] / feelings
// load (FUN_06d44810), the record copy, the visited / weapon-group / mission-log
// / interface packets and controller event 5. Returns true (.text:06d4b1e2).
bool __cdecl PlayerData_LoadSelectedCharacter(PlayerData& Slot,
                                              PlayerData::FSavedCharacter const& Record,
                                              unsigned int SessionId);

// Retail FUN_06d4b790 (thiscall, `RET 4`), called only by
// PlayerDB::create_new_character (.text:06d541a0): expands the exe-side
// NewCharacter.fl / MPNewCharacter.fl template into
// <account dir>\<FUN_06d46370(Info.Name)>.fl, FLS1-encodes it, and on success
// reloads it into Slot.saved_characters through FUN_06d4b5a0.
bool __cdecl PlayerData_CreateNewCharacter(PlayerData& Slot, SCreateCharacterInfo const& Info);

// Retail FUN_06d4a4a0 (thiscall, `RET 4`) -- MP only: leave the world
// (system, base, trade, events 0x3a / 8 / 7, group logout, player-list
// removal) and clear +0x64. Defined in server_classes.cpp, next to logout.
void __cdecl PlayerData_DeselectCharacter(PlayerData& Slot, unsigned int SessionId);

// Retail FUN_06d4a5d0 (thiscall, `RET 8`), IServerImpl::CharacterSelect's worker:
// find the record (miss -> false); in MP leave the world if a character is
// selected, drop from the player lists when the id changes, assign +0x64,
// IClient +0x1c0 and the reputation / player-list exchange with every session;
// then the shared tail PlayerData_LoadSelectedCharacter. Defined in
// server_classes.cpp, next to PlayerData_DeselectCharacter.
bool __cdecl PlayerData_SelectCharacter(PlayerData& Slot, CHARACTER_ID const& Id,
                                        unsigned int SessionId);

// Retail FUN_06d499c0 (thiscall, `RET 4`), IServerImpl::CharacterInfoReq's
// worker (.text:06d452aa): in MP with a selected character, autosave and leave
// the world; then one CCharacterSet::CharacterInfo per saved character, sorted
// Restart.fl first, AutoSave.fl next, then newest first; the failed-save
// counter moves into Out.SaveFailureCount. Defined in server_classes.cpp.
class CCharacterSet;
void __cdecl PlayerData_BuildCharacterSet(PlayerData& Slot, CCharacterSet& Out);

// Retail FUN_06d49f60 (thiscall, `RET 8`), IServerImpl::DestroyCharacter's
// worker: deselect if selected, find the record, refuse Restart.fl, drop the
// character from Players' id map and its account, unlink the file, erase the
// record, FDUMP in MP. Defined in server_classes.cpp.
bool __cdecl PlayerData_DestroyCharacter(PlayerData& Slot, CHARACTER_ID const& Id,
                                         unsigned int SessionId);

// server_pub.cpp. Retail FUN_06d45b60 -- the vibe-id bitmap allocator
// FUN_06d4a5d0 calls before Reputation::Vibe::Alloc (.text:06d4ace3).
void Server_VibeID_Alloc(unsigned int* OutId);
