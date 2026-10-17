// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU19 (06eb3ec0-06eb6c00): the BaseSupport controller and the news table
// (missions\news.ini, [NewsItem]).

#include "BaseDB.h"             // FBarMissionOfferData
#include "common_archetype.h"   // CacheString
#include "common_goods.h"       // NewsBroadcastDescription

#include <list>
#include <map>
#include <vector>

class INI_Reader;
struct FContentPlayerState;     // ContentPlayerState.h

// One [NewsItem] record, 0x80 bytes (element of vector DAT_06fcef24..DAT_06fcef30).
// Default state is set by the loader FUN_06eb44c0 (06eb451a-06eb4597); copy is the
// member-wise FUN_06eb68a0, destruction FUN_06eb4680.
struct FNewsItem
{
    FNewsItem();
    FNewsItem(FNewsItem const& Other) = default;          // FUN_06eb68a0
    FNewsItem& operator=(FNewsItem const& Other) = default;
    ~FNewsItem();                                          // FUN_06eb4680

    std::vector<unsigned int> Bases;                       // +0x00 "base", unique ids
    int FromStage;                                         // +0x10 "rank"[0], default 0
    int ToStage;                                           // +0x14 "rank"[1], default 0x2a
    CacheString Audio;                                     // +0x18 "audio" (StringAlloc)
    CacheString Logo;                                      // +0x1c "logo" (StringAlloc)
    // +0x20. Common's field names do not follow the ini keys: "category" goes to
    // Desc.Headline (+0x2c), "headline" to Desc.Text (+0x44), "text" to Desc.Audio
    // (+0x5c); "breaking" -> bFlag1, "autoselect" -> bFlag2, "icon" -> Category,
    // "logo" -> LogoTexture (+0x74), "audio" -> AudioName (+0x78).
    NewsBroadcastDescription Desc;
};

// DAT_06fcef24: constructed by LAB_06eb4440, destroyed by the atexit FUN_06eb4470.
extern std::vector<FNewsItem> GNewsItems;

// FUN_06eb44c0: one record per [NewsItem] section, appended to GNewsItems.
void __cdecl LoadNewsItems(INI_Reader* Reader);

// BaseSupport message 0x2c (mission offer accepted), 0x10 bytes. Read by LAB_06eb4ba0
// 06eb5087; sent with Kind 0 by FBarOffer::Accept (06ea8ec2) and re-sent as is to the
// new mission controller (06eb51dc).
struct FMissionOfferAcceptMsg
{
    unsigned int OfferId;           // +0x00 vendor offer id / bar character id / controller
    unsigned int Kind;              // +0x04 2 vendor offer, 1 bar character
    unsigned char bAccepted;        // +0x08 raw message byte, forwarded unchanged
    unsigned int PlayerId;          // +0x0c
};

// A mission vendor offer of a docked player (list element, node +8). Built by TU35
// FUN_06f047e0 from pub::GF::MissionVendorOfferCreate and the generated mission (copied
// with FUN_06eb2060, the loader reference moves in); released by FUN_06f04a40 or by an
// accepted offer (BaseSupport msg 0x2c).
struct FBaseVendorOffer
{
    unsigned long VendorHandle;             // +0x00 MissionVendorOfferCreate
    unsigned int OfferId;                   // +0x04 the description's id after Create;
                                            // matched with BaseSupport msg 0x2c +0
    FBarMissionOfferData Data;              // +0x08 loader, mission, type
};

// The per-player record of a base visit (map value, node +0x10, 0x1c bytes): built by
// BaseSupport msg 1, dropped by msg 2.
struct FBaseVisit
{
    std::vector<unsigned long> NewsHandles;         // +0x00 pub::GF::NewsBroadcastCreate
    std::list<FBaseVendorOffer> VendorOffers;       // +0x10
};

// DAT_06fcef10: player id -> visit (map; find FUN_06f7b0f0, insert FUN_06eb5940, erase
// FUN_06eb5b50).
extern std::map<unsigned int, FBaseVisit> GBaseVisits;

// FUN_06eb55c0 (cdecl; caller FBarOffer::Accept FUN_06ea8d50 06ea8f55, bribe): rebuild the
// player's mission vendor offers -- release them (FUN_06f04a40), then FUN_06f047e0 for the
// player's base.
void __cdecl RefreshMissionVendorOffers(FContentPlayerState* State);
