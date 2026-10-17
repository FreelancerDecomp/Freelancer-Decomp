// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU35 (06f03fa0-06f056d0): loot properties (missions\lootprops.ini,
// [mLootProps] and [PhantomLoot]). Both loaders are called from the TU32 game-table
// loader FUN_06efe300 (Encounters.cpp) with an open INI_Reader.

#include "common_id.h"

#include <vector>

class INI_Reader;

// [mLootProps] record, 0x1c bytes (vector DAT_06fd05ec..DAT_06fd05f8).
struct FLootProps
{
    ID_String Nickname;                                       // +0x00 CreateID(nickname)
    float DropChance;                                        // +0x04 drop_properties[0] * 0.01
    // +0x08..+0x18: drop_properties[1..5] as read (get_value_int).
    // TODO: meaning of each value is set by the readers (FUN_06f5d14f area, TU53).
    int DropProperties[5];
};
static_assert(sizeof(FLootProps) == 0x1c, "retail mLootProps record is 0x1c bytes");

// [PhantomLoot] record, 0x18 bytes (vector DAT_06fd05d4..DAT_06fd05e0).
struct FPhantomLoot
{
    ID_String Nickname;                                       // +0x00 CreateID(nickname)
    int ToughnessMin;                                        // +0x04 toughness_range[0]
    int ToughnessMax;                                         // +0x08 toughness_range[1], -1 -> 0x7fffffff
    float PercentChance;                                      // +0x0c percent_chance * 0.01
    int NumToDropMin;                                         // +0x10 num_to_drop[0]
    int NumToDropMax;                                         // +0x14 num_to_drop[1]
};
static_assert(sizeof(FPhantomLoot) == 0x18, "retail PhantomLoot record is 0x18 bytes");

// FUN_06f04cb0: append one record per [mLootProps] section.
void __cdecl LoadLootProps(INI_Reader* Reader);
// FUN_06f050e0: append one record per [PhantomLoot] section.
void __cdecl LoadPhantomLoot(INI_Reader* Reader);

// FUN_06f05000: first record whose nickname is "default_commodity", or null.
FLootProps* __cdecl FindDefaultCommodityLootProps();
// FUN_06f05030: first record whose nickname is "default_equipment", or null.
FLootProps* __cdecl FindDefaultEquipmentLootProps();
// FUN_06f05060: first record whose nickname is *Nickname, or null.
FLootProps* __cdecl FindLootProps(unsigned int const* Nickname);

// FUN_06f05440 (cdecl; caller SpaceSupport loot FUN_06f5cfa0 06f5d30f): every [PhantomLoot]
// record whose toughness_range holds *Toughness (signed, both ends inclusive), appended to
// Out in file order.
void __cdecl CollectPhantomLoot(int const* Toughness, std::vector<FPhantomLoot const*>* Out);

// ---------------------------------------------------------------------------------------
// TU35 -- mission offers (06f041c0-06f04be0).
// ---------------------------------------------------------------------------------------

struct FBaseNpcMission;         // BaseDB.h
struct FBarMissionOfferData;    // BaseDB.h
struct FContentPlayerState;     // ContentPlayerState.h

// FUN_06f041c0 (cdecl): a loader for Mission->Type into Out, then up to five attempts to
// build a random mission for Player at BaseId for Group, each at a new difficulty
// (FUN_06f4ad60); a difficulty of 0.0 gives false at once. True when the loader was made
// and the last attempt succeeded. Out keeps the loader either way (the caller releases it
// on false). Caller FBaseNpc::ChooseOffer (FUN_06ea8370 06ea8714).
bool __cdecl CreateBarMissionOffer(FBaseNpcMission const* Mission, FContentPlayerState* Player,
                                   unsigned int BaseId, unsigned int Group,
                                   FBarMissionOfferData* Out);

struct FBaseVisit;              // BaseSupport.h
struct FContentBase;            // BaseDB.h

// FUN_06f047e0 (cdecl; callers BaseSupport msg 1 06eb4e4c, FUN_06eb55c0 06eb561e): the
// mission vendor offers of Base for State into Visit->VendorOffers. The factions of the
// base that the player is not hostile to (FUN_06f042d0) get num_offers random missions
// (FUN_06f04450); each becomes a pub::GF::MissionVendorOfferCreate offer. The reason of
// an empty vendor goes to pub::GF::ReportWhyMissionVendorEmpty.
void __cdecl BuildMissionVendorOffers(FContentBase* Base, FBaseVisit* Visit,
                                      FContentPlayerState* State);
// FUN_06f04a40 (cdecl; callers BaseSupport msg 2 06eb4d9e, FUN_06eb55c0 06eb560c): every
// offer gets pub::GF::MissionVendorOfferDestroy and its loader slot 5 (the mission) and
// Release; then the list is emptied.
void __cdecl ClearMissionVendorOffers(FBaseVisit* Visit);
