// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll bases and markets (retail BaseData.cpp): MarketGoodInfo, BaseData, BaseDataList and
// the BaseDataList_* entry points. Split out of common_goods.cpp.

#include "common_goods.h"
#include "common_goods_internal.h" // kPathChars
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"

#include <cstdio>
#include <list>
#include <map>
#include <new>
#include <string>

using CmnGoods::kPathChars;

namespace
{
    const char kBaseDataSource[] = "E:\\FL\\Scratch\\Source\\Common\\BaseData.cpp";
}

// ---------------------------------------------------------------------------
// Bases / markets
// ---------------------------------------------------------------------------

// Retail inlines this ctor into both of its callers and writes the seven
// dwords literally. In BaseDataList::load_market_data the inlined copy is at
// .text:06265c6d .. .text:06265c8d:
//   [ESP+0x4c] = EBX(0)          +0x00 GoodId       = 0
//   [ESP+0x50] = 0x3f800000      +0x04 Price        = 1.0f
//   [ESP+0x54] = EBX(0)          +0x08 MinInventory = 0
//   [ESP+0x58] = EBX(0)          +0x0c MaxInventory   = 0
//   [ESP+0x5c] = EBX(0)          +0x10 Type         = 0
//   [ESP+0x60] = 0               +0x14 MinLevel     = 0.0f
//   [ESP+0x64] = 0xbf800000      +0x18 MinRep       = -1.0f
// and BaseData::set_market_good has the same seven writes at
// .text:06264c25 .. .text:06264c5d. The 1.0f in the Price slot is the price
// multiplier's default leaking into the field the multiplier feeds; it is not
// a plausible price, but it is what retail stores.
MarketGoodInfo::MarketGoodInfo()
    : GoodId(0)
    , Price(1.0f)
    , MinInventory(0)
    , MaxInventory(0)
    , Type(TransactionBuyAndSell)
    , MinLevel(0.0f)
    , MinRep(-1.0f)
{
}


// 06264980: the four scalars zeroed, ship_repair_cost 0.33f (0x3ea8f5c3), empty market map
// and room list. The copy ctor (06261de0) and operator= (06261f20) are defaulted in the header.
BaseData::BaseData()
    : BaseId(0)
    , StartRoomId(0)
    , PriceVariance(0.0f)
    , ShipRepairCost(0.33f)
{
}

// 06264a40: delete every RoomData in the list, then the list and the market map go.
BaseData::~BaseData()
{
    for (std::list<RoomData*>::iterator It = Rooms.begin(); It != Rooms.end(); ++It)
        delete *It;
    Rooms.clear();
}

unsigned BaseData::get_base_id() const
{
    return BaseId;
}

float BaseData::get_price_variance() const
{
    return PriceVariance;
}

float BaseData::get_ship_repair_cost() const
{
    return ShipRepairCost;
}

unsigned BaseData::get_start_location() const
{
    return StartRoomId;
}

std::map<unsigned, MarketGoodInfo> const* BaseData::get_market() const
{
    // .text:06261dc0: LEA EAX,[this+0x10]; RET.
    return &Market;
}

std::list<RoomData*>* BaseData::get_room_data_list()
{
    // .text:06261dd0: LEA EAX,[this+0x24]; RET.
    return &Rooms;
}

std::list<RoomData*> const* BaseData::get_const_room_data_list() const
{
    // .text:06261dd0 (the same body as get_room_data_list).
    return &Rooms;
}

// ---------------------------------------------------------------------------
// 0x06264b60 ?set_market_good@BaseData@@QAEXIHHW4TransactionType@@MMM@Z
// (ordinal 3623). `RET 0x1c` at .text:06264d74 = seven dwords of arguments,
// which is the arity the mangling also gives.
//
// Was previously declared in common_goods.h with NO definition anywhere in the
// port, and its tracker note argued that an uncalled member of a dllexport'd
// class "emits/exports nothing". Rather than rely on that, it is implemented
// here -- it is real retail code and the implementation is short.
//
// Body:
//   .text:06264b69 .. .text:06264bc4 -- a pure early-out. If MinInventory,
//     MaxInventory and Type are all zero AND PriceMult == 1.0 (FCOMP against the
//     double at .rdata:06399410) AND MinLevel == 0.0 (float at
//     .rdata:06399408) AND MinRep == -1.0 (float at .rdata:06399404), the
//     function jumps straight to its epilogue at LAB_06264d6b and does
//     nothing. Note it does NOT erase an existing entry.
//   .text:06264bca .. .text:06264c71 -- build the 7-dword record: GoodId, then
//     (float)ftol(GoodList::find_by_id(GoodId)->Price * PriceMult) at +0x04,
//     MinInventory at +0x08, MaxInventory at +0x0c, Type at +0x10, MinLevel at
//     +0x14, MinRep at +0x18.
//   .text:06264c39 .. .text:06264d69 -- an inlined map insert against the tree
//     at this+0x10 (`ADD EBX,0x10` at .text:06264c22), followed by
//     `LEA EDI,[EAX+0x10] / MOV ECX,7 / REP MOVSD`: the mapped value is
//     overwritten whether or not the key was already there. That is
//     `Market[GoodId] = record`.
// ---------------------------------------------------------------------------
void BaseData::set_market_good(unsigned GoodId, int MinInventory, int MaxInventory,
                               TransactionType Type, float PriceMult, float MinLevel,
                               float MinRep)
{
    if (MinInventory == 0 && MaxInventory == 0 && Type == TransactionBuyAndSell &&
        PriceMult == 1.0f && MinLevel == 0.0f && MinRep == -1.0f)
    {
        return;
    }

    MarketGoodInfo Entry;
    Entry.GoodId = GoodId;
    // .text:06264bd3 CALL GoodList::find_by_id / .text:06264bd8 FLD [EAX + 0x58]
    // -- no null test -- then FMUL by the multiplier (the product stays on the x87
    // stack, exact in a double), ftol (.text:06264be9) and FILD back (.text:06264c00).
    Entry.Price = (float)(int)((double)GoodList::find_by_id(GoodId)->Price * PriceMult);
    Entry.MinInventory = MinInventory;
    Entry.MaxInventory = MaxInventory;
    Entry.Type = Type;
    Entry.MinLevel = MinLevel;
    Entry.MinRep = MinRep;

    Market[GoodId] = Entry;
}

// 0x06264d80 ?read_Base_block@BaseData@@AAEXPAVINI_Reader@@@Z -- [Base]/[BaseInfo] block.
// "nickname" is skipped; any other unknown key FDUMPs line 0x89 (06264e30); after the block a
// zero start room FDUMPs line 0x90 (06264e65).
void BaseData::read_Base_block(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
            continue;
        if (Reader->is_value("start_room"))
        {
            // 06264dce-06264ded: sprintf into a 0x100 frame buffer, then CreateID.
            char Combined[0x100];
            sprintf(Combined, "%X_%s", BaseId, Reader->get_value_string());
            StartRoomId = CreateID(Combined);
        }
        else if (Reader->is_value("price_variance"))
        {
            PriceVariance = Reader->get_value_float(0);
        }
        else if (Reader->is_value("ship_repair_cost"))
        {
            ShipRepairCost = Reader->get_value_float(0);
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid ini data in file %s\n",
                  kBaseDataSource, 0x89, Reader->get_file_name());
        }
    }
    if (StartRoomId == 0)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: No start room in base ini file %s\n",
              kBaseDataSource, 0x90, Reader->get_file_name());
    }
}

// 0x06264ea0 ?read_Room_block@BaseData@@AAEXPAVINI_Reader@@@Z -- [Room] block.
// The RoomData is new-ed and appended BEFORE the keys are read (06264ebd-06264f0a); when the
// block lacks a nickname or a file (the two string lengths, 06264ff8 / 06264ffe) retail only
// FDUMPs line 0xc1 and the never-initialised RoomData stays in the list. Its BaseId / RoomId
// / bLoaded are whatever the heap held (RoomData's ctor does not write them), as in retail.
void BaseData::read_Room_block(INI_Reader* Reader)
{
    // 06264ebd-06264ee6: VC6 operator new returns null on failure and the ctor is skipped;
    // the (possibly null) pointer is appended either way.
    RoomData* Room = new (std::nothrow) RoomData();
    Rooms.push_back(Room);

    std::string Nickname;
    std::string File;
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
            Nickname = Reader->get_value_string();
        else if (Reader->is_value("file"))
            File = Reader->get_value_string();
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid ini data in file %s\n",
                  kBaseDataSource, 0xb5, Reader->get_file_name());
        }
    }
    if (Nickname.length() != 0 && File.length() != 0)
    {
        // 0626501a-06265058: sprintf "%X_%s", CreateID, then
        // RoomData::init(BaseData+0, room id, nickname, file) -- init malloc_strcpy's both
        // strings (06277c29/06277c37); RoomData::destroy frees them with malloc_strfree.
        char Combined[0x100];
        sprintf(Combined, "%X_%s", BaseId, Nickname.c_str());
        Room->init(BaseId, CreateID(Combined), Nickname.c_str(), File.c_str());
    }
    else
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Invalid [Room] entry in ini file %s, nickname or file is missing\n",
              kBaseDataSource, 0xc1, Reader->get_file_name());
    }
}

// 0x06265100 ?read_from_ini@BaseData@@QAEXPBDI@Z. BaseId first, then the path through
// GetFileSysPath with *DALib::Data. [Base] and [BaseInfo] go to read_Base_block, [Room] to
// read_Room_block, anything else FDUMPs "Invalid ini block" line 0xe2; the reader is closed
// at the end (06265239). An unopenable file FDUMPs line 0xea with the resolved path.
void BaseData::read_from_ini(char const* RelPath, unsigned NewBaseId)
{
    BaseId = NewBaseId;

    char FullPath[kPathChars];
    GetFileSysPath(FullPath, RelPath, DALib::Data);

    INI_Reader Reader;
    if (Reader.open(FullPath, false))
    {
        while (Reader.read_header())
        {
            if (Reader.is_header("Base") || Reader.is_header("BaseInfo"))
            {
                read_Base_block(&Reader);
            }
            else if (Reader.is_header("Room"))
            {
                read_Room_block(&Reader);
            }
            else
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Invalid ini block in file %s\n",
                      kBaseDataSource, 0xe2, Reader.get_file_name());
            }
        }
        Reader.close();
    }
    else
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot open base .ini file %s\n",
              kBaseDataSource, 0xea, FullPath);
    }
}

static BaseDataList* GBaseDataList = nullptr;

// 06265450: empty base list and the two empty maps. Copy ctor / operator= are defaulted.
BaseDataList::BaseDataList()
{
}

// 06265560: delete every BaseData (null-tested, 06265598), then the list and both maps go.
BaseDataList::~BaseDataList()
{
    for (std::list<BaseData*>::iterator It = Bases.begin(); It != Bases.end(); ++It)
        delete *It;
}

// 06265f80: lookup in the +0x0c map; a miss FDUMPs line 0x1e8 and returns 0.
BaseData* BaseDataList::get_base_data(unsigned BaseId) const
{
    std::map<unsigned, BaseData*>::const_iterator Found = ById.find(BaseId);
    if (Found != ById.end())
        return Found->second;
    FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find base %u\n", kBaseDataSource, 0x1e8,
          BaseId);
    return nullptr;
}

std::list<BaseData*>* BaseDataList::get_base_data_list()
{
    // .text:06261260: MOV EAX,this; RET -- the list is the first member.
    return &Bases;
}

std::list<BaseData*> const* BaseDataList::get_const_base_data_list() const
{
    // .text:06261260 (the same body as get_base_data_list).
    return &Bases;
}

// 06266080: the same body as get_unloaded_room_data -- a lookup in the +0x20 room map, a miss
// FDUMPs line 0x1fc and returns 0. It does not load the room.
RoomData* BaseDataList::get_room_data(unsigned RoomId) const
{
    std::map<unsigned, RoomData*>::const_iterator Found = RoomById.find(RoomId);
    if (Found != RoomById.end())
        return Found->second;
    FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find room %u\n", kBaseDataSource, 0x1fc,
          RoomId);
    return nullptr;
}

// 06266000: +0x20 room map lookup, FDUMP line 0x1fc on a miss.
RoomData* BaseDataList::get_unloaded_room_data(unsigned RoomId) const
{
    std::map<unsigned, RoomData*>::const_iterator Found = RoomById.find(RoomId);
    if (Found != RoomById.end())
        return Found->second;
    FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find room %u\n", kBaseDataSource, 0x1fc,
          RoomId);
    return nullptr;
}

// 06265780. One BaseData per universe base, appended before it is read; IBase slot 3 is the
// base ini, slot 0 the base id. A base without an ini keeps BaseId 0 (read_from_ini is what
// stores it). Only after the whole walk (0626587e-062658e2) are the two maps filled: every
// base under BaseData+0, every room of it under RoomData+4 -- insert, so the first of two
// equal keys wins (FUN_063222f0).
void BaseDataList::load()
{
    Universe::IBase* Base = Universe::GetFirstBase();
    while (Base != nullptr)
    {
        BaseData* Data = new BaseData();
        Bases.push_back(Data);

        char const* File = Base->GetFile();
        if (File != nullptr && File[0] != '\0')
        {
            Data->read_from_ini(File, Base->GetId());
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: No ini file for base %u\n", kBaseDataSource,
                  0x11b, Base->GetId());
        }

        Base = Universe::GetNextBase();
    }

    for (std::list<BaseData*>::iterator BaseIt = Bases.begin(); BaseIt != Bases.end(); ++BaseIt)
    {
        BaseData* Data = *BaseIt;
        ById.insert(std::pair<unsigned const, BaseData*>(Data->BaseId, Data));
        for (std::list<RoomData*>::iterator RoomIt = Data->Rooms.begin();
             RoomIt != Data->Rooms.end(); ++RoomIt)
        {
            RoomById.insert(std::pair<unsigned const, RoomData*>((*RoomIt)->RoomId, *RoomIt));
        }
    }
}

// 06265900 ?load_market_data@BaseDataList@@QAEXPBD@Z. Every [BaseGood] block starts with no
// current base (062659b0). "base" resolves the nickname: an unknown name only warns (line
// 0x153) and leaves the current base as it was; a known id goes through the +0x0c map (the
// inlined get_base_data, miss = FDUMP line 0x1e8 and no current base).
void BaseDataList::load_market_data(char const* RelPath)
{
    char FullPath[kPathChars];
    GetFileSysPath(FullPath, RelPath, DALib::Data);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot open market .ini file %s\n",
              kBaseDataSource, 0x1d2, FullPath);
        return;
    }

    while (Reader.read_header())
    {
        if (!Reader.is_header("BaseGood"))
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: Unknown ini block [%s] in market .ini file %s - ignoring\n",
                  kBaseDataSource, 0x1ca, Reader.get_header_ptr(), FullPath);
            continue;
        }

        BaseData* Current = nullptr;
        while (Reader.read_value())
        {
            if (Reader.is_value("base"))
            {
                char const* Nickname = Reader.get_value_string();
                unsigned BaseId = Universe::get_base_id(Nickname);
                if (BaseId == 0)
                {
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: base name %s is unknown in market data file %s",
                          kBaseDataSource, 0x153, Nickname, FullPath);
                }
                else
                {
                    Current = get_base_data(BaseId);
                }
            }
            else if (Reader.is_value("MarketGood"))
            {
                unsigned GoodId = 0;
                GoodInfo const* Info = GoodList::find_by_nickname(Reader.get_value_string(0));
                if (Info != nullptr)
                    GoodId = Info->Id;

                // Retail keeps the seven INI values in registers/stack slots and
                // only assembles the record once it has decided to keep the line,
                // so the locals below mirror retail's scratch, not the struct.
                // Defaults installed at .text:06265ab0 .. .text:06265acd.
                float ValueLevel = 0.0f;        // index 1  -> record+0x14
                float ValueRep = -1.0f;         // index 2  -> record+0x18
                int ValueMinInventory = 0;      // index 3  -> record+0x08
                int ValueMaxInventory = 0;         // index 4  -> record+0x0c
                int ValueType = 0;              // index 5  -> record+0x10
                float ValuePriceMult = 1.0f;    // index 6  -> feeds record+0x04

                if (!Reader.is_value_empty(1))
                    ValueLevel = Reader.get_value_float(1);
                if (!Reader.is_value_empty(2))
                    ValueRep = Reader.get_value_float(2);
                if (!Reader.is_value_empty(3))
                    ValueMinInventory = Reader.get_value_int(3);
                if (!Reader.is_value_empty(4))
                    ValueMaxInventory = Reader.get_value_int(4);
                if (!Reader.is_value_empty(5))
                    ValueType = Reader.get_value_int(5);
                if (!Reader.is_value_empty(6))
                    ValuePriceMult = Reader.get_value_float(6);
                // .text:06265bbb / .text:06265bc0 `FSTP ST0` -- index 7 is read
                // off the line and dropped on the floor.
                if (!Reader.is_value_empty(7))
                    Reader.get_value_float(7);

                // .text:06265bc4 .. .text:06265c15 -- skip the line entirely when
                // the good is unknown, no [BaseGood] base is current, or every
                // value is still at its default.
                bool bKeep = false;
                if (GoodId != 0 && Current != nullptr)
                {
                    if (ValueMinInventory != 0 || ValueMaxInventory != 0 || ValueType != 0 ||
                        ValuePriceMult != 1.0f || ValueLevel != 0.0f || ValueRep != -1.0f)
                    {
                        bKeep = true;
                    }
                }

                if (bKeep)
                {
                    MarketGoodInfo Market;
                    Market.GoodId = GoodId;
                    // 06265c1c-06265c50: find_by_id(...)->price (+0x58) with no null test,
                    // FMUL by the multiplier, ftol, FILD, FSTP float. The product of two
                    // floats is exact in a double, so it truncates as the x87 one does.
                    double const Product =
                        (double)GoodList::find_by_id(GoodId)->Price * ValuePriceMult;
                    Market.Price = (float)(int)Product;
                    Market.MinInventory = ValueMinInventory;
                    Market.MaxInventory = ValueMaxInventory;
                    Market.Type = (TransactionType)ValueType;
                    Market.MinLevel = ValueLevel;
                    Market.MinRep = ValueRep;

                    // .text:06265cac .. .text:06265cbf -- retail inserts a
                    // default-constructed value under GoodId and then overwrites
                    // the mapped value through the returned iterator, which is an
                    // assign-over-existing, not insert-if-absent.
                    Current->Market[GoodId] = Market;
                }
            }
            else
            {
                // 06265cc3-06265cff: header, key and the whole value string.
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: Unknown ini data: [%s]  %s = %s in market .ini file - ignoring\n",
                      kBaseDataSource, 0x1c2, Reader.get_header_ptr(), Reader.get_name_ptr(),
                      Reader.get_value_string());
            }
        }
    }
    Reader.close();
}

// 06266110: a fresh BaseDataList (ctor inlined) into the global, then load(). The previous
// list is not destroyed -- it is simply overwritten, as in retail.
void __cdecl BaseDataList_load()
{
    GBaseDataList = new BaseDataList();
    GBaseDataList->load();
}

// 06266270: a plain forward through the global, no null test.
void __cdecl BaseDataList_load_market_data(char const* RelPath)
{
    GBaseDataList->load_market_data(RelPath);
}

void __cdecl BaseDataList_destroy()
{
    delete GBaseDataList;
    GBaseDataList = nullptr;
}

BaseDataList* __cdecl BaseDataList_get()
{
    return GBaseDataList;
}
