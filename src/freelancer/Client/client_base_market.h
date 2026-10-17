// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The market of the base the player is at, as the client keeps it (Gf\LocationController.cpp).
//
// Every loaded base's runtime (the 0x4c-byte object FUN_004397a0 allocates, see
// menu_universe_base.cpp) carries at +0x30 a std::map from good id to FClientMarketGood,
// constructed by FUN_0042abc0 and filled by FUN_00437de0 from the base's BaseData market with a
// random price scale. FUN_0043a510 (GetClientLocationBaseMarket) hands out its address; the
// methods below are thiscall on it.
//
// Map node: left +0x0, parent +0x4, right +0x8, key +0xc, value +0x10 (0x28 bytes), colour +0x38,
// _Isnil +0x39 (the walk at 0x00437ffe reads it).

#include "common_goods.h"

#include <list>
#include <map>

class BaseData;

// The mapped value (0x28 bytes, copied as ten dwords at 0x00437f1a / 0x00437f46).
struct FClientMarketGood
{
    MarketGoodInfo Info; // +0x00 BaseData's entry, or {Id, GoodInfo price} when the base has none
    int Stock;           // +0x1c 0x7f000000 when Info.MinInventory != 0 and Info.MaxInventory > 0
    float PriceScale;    // +0x20 1 - variance + rand() / 32768 * 2 * variance
    // +0x24 -- never written by FUN_00437de0 (the ten-dword copies take it from an unset stack
    // slot) and never read by the market methods. Its low byte is NN_Dealer::CanBuyGood's result,
    // stored by the dealer's list sorts (0x0048304e in FUN_00482f10, 0x00483310 in FUN_00483230)
    // and read by their comparisons; the list refresh FUN_00482a90 passes it on to the row
    // (NnDealer::SortMarketGoodsByName / SortEquipmentMarketGoods / RefreshMarketList).
    unsigned Field24;
};

static_assert(sizeof(FClientMarketGood) == 0x28, "the client market value is 0x28 bytes");

typedef std::map<unsigned, FClientMarketGood> FClientMarket;

// FUN_00437de0 (thiscall, RET 8) -- one entry per good of GoodList_get(), in list order: Base's
// market entry (Base == null: the current base's BaseData, FUN_0043b630) or a default one, its
// stock, and a price scale drawn with rand(). Existing entries are overwritten.
void ClientMarketFill(FClientMarket* Market, float PriceVariance, BaseData const* Base);

// FUN_00437f70 (thiscall, RET 8) -- append to Out every entry with Stock > 0 whose good's
// Category (GoodInfo +0x4c) is Category, in key order; the count appended.
int ClientMarketCollectCategory(FClientMarket const* Market,
                                std::list<FClientMarketGood const*>* Out, unsigned Category);

// FUN_00438060 (thiscall, RET 4) -- ftol(Price * PriceScale) clamped to 9999999, a negative
// result 1.0; 0.0 for a good the market does not hold.
float ClientMarketGetPrice(FClientMarket const* Market, unsigned GoodId);

// FUN_00438100 (thiscall, RET 4) -- the entry's TransactionType (+0x10), -1 without an entry.
int ClientMarketGetTransactionType(FClientMarket const* Market, unsigned GoodId);

// FUN_00438160 (thiscall, RET 4) -- the entry's MinLevel (+0x14), 0.0 without an entry.
float ClientMarketGetMinLevel(FClientMarket const* Market, unsigned GoodId);

// FUN_004381c0 (thiscall, RET 4) -- the entry's MinRep (+0x18), -1.0 without an entry.
float ClientMarketGetMinRep(FClientMarket const* Market, unsigned GoodId);

// FUN_00437d90 (thiscall, RET 4) -- the entry of GoodId (map node +0x10), null without one.
FClientMarketGood* ClientMarketFindEntry(FClientMarket* Market, unsigned GoodId);

// FUN_00438220 (thiscall, RET 0xc; the market itself is not read) -- ask the server to sell the
// player Count of GoodId at BaseId: IServer slot 45 GFGoodBuy({BaseId, 0, GoodId, Count}) with
// the player's connection id.
void ClientMarketRequestBuy(FClientMarket const* Market, unsigned BaseId, unsigned GoodId, int Count);

// FUN_00438270 (thiscall, RET 0xc; the market itself is not read) -- the reverse: IServer slot 46
// GFGoodSell({BaseId, GoodId, Count}) with the player's connection id.
void ClientMarketRequestSell(FClientMarket const* Market, unsigned BaseId, unsigned GoodId, int Count);

// FUN_0043a510 (thiscall on DAT_00668708, RET 4) -- the market of BaseId's runtime (+0x30). On a
// miss retail FDUMPs (LocationController.cpp:0x100) and reads through the null entry.
FClientMarket* GetClientLocationBaseMarket(unsigned BaseId);

// FUN_0043b630 (thiscall on DAT_00668708) -- the BaseData of the base the player is at
// (controller +0x34). Same miss behaviour.
BaseData* GetClientCurrentLocationBaseData();
