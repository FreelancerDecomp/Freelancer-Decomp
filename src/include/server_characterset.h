// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ===========================================================================
// server_characterset.h -- the character-select list: the record Server.dll
// builds for every saved character, the list IClient slot 13 (ch2 0x03
// FLPACKET_SERVER_CHARACTERINFO) carries, and the ONE declaration Server.dll,
// rpclocal.dll and freelancer.exe all read it through.
//
// NAMES. Retail's own, from RTTI. Server.dll's IServerImpl::CharacterInfoReq
// stamps .rdata:06d6ada8 on its 0x18-byte local (.text:06d45288) and, in the
// teardown, .rdata:06d6ada0 (.text:06d4531d). The locators behind them:
//   .rdata:06d6ada4 -> 06d70910, type .rdata:06d7091c
//       -> .data:06d8d0ec ".?AV?$OwnerList@UCharacterInfo@CCharacterSet@@@@"
//   .rdata:06d6ad9c -> 06d70954, type .rdata:06d70960
//       -> .data:06d8d128 ".?AV?$MetaList@UCharacterInfo@CCharacterSet@@@@"
// and 06d70910's base list names MetaList<> as OwnerList<>'s base
// (.rdata:06d70924). freelancer.exe carries the same two descriptors
// (.data:00615b24 / 00615b5c). So the record is `struct
// CCharacterSet::CharacterInfo` and the list is OwnerList<> of it -- the
// template in common_metalist.h, whose MetaList<GoodDesc> bodies are
// instruction-for-instruction what this instantiation compiled to:
//   * CharacterInfoReq's teardown (.text:06d452c0 .. .text:06d45341) is
//     OwnerList::free (record destructor FUN_06d45e50 + free, Data = 0, then
//     the nodes through FUN_06d0f970 = free_node) followed by MetaList's
//     destructor (the MetaList vptr, then the pool drain);
//   * the builder's FUN_06d5b0b0 / FUN_06d5a930 / FUN_06d11850 are prepend /
//     append / alloc_node, and its inline link at .text:06d49eda .. 06d49ef1
//     is insert_after;
//   * rpclocal's FUN_06d852e0 / FUN_06d85340 (vtables .rdata:06db3758 /
//     06db3754) are ~MetaList / ~OwnerList of the same instantiation;
//   * freelancer.exe's FUN_005ae700 is append.
//
// WHAT THE 0x18 OBJECT IS. The OwnerList sits at +0x00 with ITS OWN vptr as
// the complete-object vptr (the locator at 06d70910 has offset 0), so whatever
// encloses it adds no virtual function; the only other field is the dword at
// +0x14. That the enclosing type is CCharacterSet itself is the natural reading
// of the RTTI (CharacterInfo is nested in it) but is NOT proven by a descriptor
// of its own -- CCharacterSet has none, because it is not polymorphic.
//
// MODERN TYPES. Retail's two strings are MSVCP60 basic_string<unsigned short>
// (VC6 spelled wchar_t that way) and its two lists are VC6 std::list; the port
// uses std::wstring and std::list, so the record is not 0x2dc bytes here and no
// offset below is a byte offset of the port's struct. They are retail's record
// offsets, kept because every reader in every module is written against them.
// Only the ORDER and the VALUES are retail's.
// ===========================================================================

#include "common_server_types.h"   // CHARACTER_ID
#include "common_goods.h"          // EquipDesc, CollisionGroupDesc, Costume, OwnerList<T>

#include <list>
#include <string>

class CCharacterSet
{
public:
    struct CharacterInfo;

    // +0x00 .. +0x13. bUseHeap is false on every constructor in all three
    // modules (CharacterInfoReq's inline stores .text:06d45278 .. 06d45284;
    // rpclocal FUN_06d852c0 with `PUSH EBP` = 0 at .text:06d844ad).
    OwnerList<CharacterInfo> Characters;

    // +0x14. Server fills it from PlayerData::save_failure_count (+0x344) and
    // then zeroes the counter (.text:06d49f21 .. .text:06d49f39); rpclocal
    // carries it as the payload's first dword (06d89e9d / 06d84a93);
    // freelancer.exe copies it to its connect context +0x1cc (.text:005aac82).
    // CharacterInfoReq never writes it when its bool is true (the local is
    // uninitialised there); the port's constructor zeroes it.
    unsigned int SaveFailureCount;

    // Defined below, after OwnerListDeleteData's specialisation for the record:
    // the constructor is what instantiates OwnerList<CharacterInfo>'s vtable,
    // and with it free() and the element delete.
    CCharacterSet();
};

// The record. Server.dll FUN_06d499c0 builds it from a PlayerData::FSavedCharacter
// (the per-field source is named on each member); freelancer.exe FUN_005aa9c0
// copies it member by member (.text:005aab18 .. .text:005aac5b) and FUN_005ab250
// reads it at character select.
struct CCharacterSet::CharacterInfo
{
    CHARACTER_ID Id;                 // +0x000 the saved_characters key (.text:06d49bdc)
    // +0x200 / +0x201: stricmp(FileName, "Restart.fl") == 0 and
    // stricmp(FileName, "AutoSave.fl") == 0 (.text:06d49bee / .text:06d49c05,
    // `NEG / SBB / INC`). Also the first two keys of the list's sort order.
    unsigned char bIsRestart;        // +0x200
    unsigned char bIsAutoSave;       // +0x201
    std::wstring Name;               // +0x204 <- FSavedCharacter::Name
    std::wstring Description;        // +0x214 <- ::Description; L"" for the autosave
    unsigned int DescripStrId;       // +0x224 <- ::DescripStrId; 0x65c for the autosave
    // +0x228 / +0x22c: a FILETIME -- CompareFileTime reads it at .text:06d4999d.
    unsigned int TimestampLow;       // +0x228 <- ::TimestampLow  (dwLowDateTime)
    unsigned int TimestampHigh;      // +0x22c <- ::TimestampHigh (dwHighDateTime)
    unsigned int ShipArchetype;      // +0x230 <- ::ShipArchetype
    int Money;                       // +0x234 <- ::Money
    int NumKills;                    // +0x238 <- ::NumKills
    int NumMissionSuccesses;         // +0x23c <- ::NumMissionSuccesses
    int NumMissionFailures;          // +0x240 <- ::NumMissionFailures
    float HullStatus;                // +0x244 <- ::HullStatus
    // +0x248 is a COMMON.DLL EquipDescList: the inlined constructor calls
    // ??0EquipDescList@@QAE@XZ on it (Server .text:06d49b56 via .rdata:06d648f0,
    // rpclocal .text:06d845b0 via .rdata:06db30b8), and rpclocal's EH map destroys it
    // with the imported ~EquipDescList (Unwind@06db2349, ECX = record+0x248). In
    // freelancer.exe the same member's ctor/operator= are expanded to the underlying
    // std::list<EquipDesc> imports (.text:005aaa9a, .text:005aabe7).
    EquipDescList Equipment;                     // +0x248 <- ::Equipment
    std::list<CollisionGroupDesc> CollisionGroups; // +0x254 <- ::CollisionGroups
    unsigned int SystemId;           // +0x260 <- ::SystemId   (.text:06d49dab)
    unsigned int BaseId;             // +0x264 <- ::BaseId     (.text:06d49db7)
    unsigned int LocationId;         // +0x268 <- ::LocationId (.text:06d49dc3)
    unsigned int VoiceId;            // +0x26c <- CreateID(::Voice's text) (.text:06d49dd6)
    unsigned int Rank;               // +0x270 <- ::Rank       (.text:06d49de2)
    Costume ComCostume;              // +0x274 <- ::ComCostume  (.text:06d49e3a)
    Costume BaseCostume;             // +0x2a8 <- ::BaseCostume (.text:06d49dee)

    // The constructor both Server (.text:06d49ae1 .. .text:06d49bb7) and
    // freelancer.exe (.text:005aaa27 .. .text:005aab00) inline: the key's first
    // byte, both flags and every dword are zeroed, the two Costumes go through
    // their constructor (FUN_06d0a060 / FUN_004cdc10: the four parts and the
    // count, not the accessory slots). Each costume copy reads accessory ids
    // only up to AccessoryCount; the remaining slots stay unwritten.
    CharacterInfo()
        : bIsRestart(0), bIsAutoSave(0), DescripStrId(0), TimestampLow(0), TimestampHigh(0),
          ShipArchetype(0), Money(0), NumKills(0), NumMissionSuccesses(0),
          NumMissionFailures(0), HullStatus(0.0f), SystemId(0), BaseId(0), LocationId(0),
          VoiceId(0), Rank(0)
    {
        Id.Buffer[0] = '\0';
        // RS 06bd4eb0 / Server 06d0a060 / game 004cdc10: ComCostume, then BaseCostume.
        ComCostume.PartIds[0] = 0;
        ComCostume.PartIds[1] = 0;
        ComCostume.PartIds[2] = 0;
        ComCostume.PartIds[3] = 0;
        ComCostume.AccessoryCount = 0;
        BaseCostume.PartIds[0] = 0;
        BaseCostume.PartIds[1] = 0;
        BaseCostume.PartIds[2] = 0;
        BaseCostume.PartIds[3] = 0;
        BaseCostume.AccessoryCount = 0;
    }
};

// OwnerList<T>::free deletes its elements through OwnerListDeleteData. This
// record has real members, and retail's free runs its destructor before the
// free: Server FUN_06d45e50 + FUN_06d5ffb0 (.text:06d452eb / .text:06d452f1),
// rpclocal FUN_06d85340's record pass (the two lists, the two strings, then the
// block, .text:06d85396 .. .text:06d853d3).
template <>
inline void OwnerListDeleteData<CCharacterSet::CharacterInfo>(CCharacterSet::CharacterInfo* Data)
{
    delete Data;
}

// The retail Costume assignment every CharacterInfo copy uses (Server inline at
// .text:06d49df4 .. .text:06d49e7f, freelancer.exe FUN_0053c250): the four
// parts, the count, then only `count` accessory ids, with a SIGNED count test
// (`JLE` / `JL`).
inline void CharacterInfoCopyCostume(Costume& Dest, Costume const& Source)
{
    Dest.PartIds[0] = Source.PartIds[0];
    Dest.PartIds[1] = Source.PartIds[1];
    Dest.PartIds[2] = Source.PartIds[2];
    Dest.PartIds[3] = Source.PartIds[3];
    Dest.AccessoryCount = Source.AccessoryCount;
    for (int Index = 0; Index < static_cast<int>(Dest.AccessoryCount); ++Index)
        Dest.AccessoryIds[Index] = Source.AccessoryIds[Index];
}

inline CCharacterSet::CCharacterSet()
    : Characters(), SaveFailureCount(0)
{
}
