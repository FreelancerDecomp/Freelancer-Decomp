// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_savegame.cpp -- Server.dll's character-file family: the parser that
// reads a saved character into the slot's record container, the login-scan and
// post-save wrappers around it, the three save wrappers that end in the
// serializer, and the record -> live-slot copy CharacterSelect runs.
//
// REWRITTEN 2026-09-24 (claude:srv-save). Every function here is a port of one
// retail function, named in its header comment, and read off
// EXE\Disasm\Server.dll.txt (string operands off the bytes of EXE\Server.dll,
// where the listing truncates them). What used to be here was a hand-rolled
// `key = value` scanner that wrote straight onto the LIVE slot; retail never
// does that. Retail parses into the 0x398-byte record that lives in the map at
// PlayerData+0x50 (PlayerData::FSavedCharacter), keyed by the file name, and
// CharacterSelect copies that record onto the slot later.
//
// Still open (not accepted, see each function's own notes):
//  * FUN_06d4a5d0's MP-only front section (.text:06d4a652 .. .text:06d4aa85:
//    events 0x3a / 8 / 7, the CHARACTER_ID compare, the IClient +0x1bc/+0x1c0
//    and reputation broadcast loops) is only approximated by
//    IServerImpl::CharacterSelect; its tail (.text:06d4aa85 .. .text:06d4b1fa)
//    is PlayerData_LoadSelectedCharacter below.

#include "../include/common_pub.h"
#include "../include/common_server.h"          // Players (PlayerDB::operator[])
#include "../include/common_server_playerdb.h"
#include "../include/server_savegame.h"
#include "../include/server_savefile.h"        // FLS1, account paths, last saved path
#include "../include/common_net.h"             // IsMPServer
#include "../include/common_universe.h"
#include "../include/common_archetype.h"
#include "../include/common_cobject.h"         // SubObjectID::EquipIdMaker
#include "../include/common_goods.h"           // Loadout::ReadEquipLine / ReadCargoLine
#include "../include/common_ini.h"             // INI_Reader, StringAlloc, RefreshLoadingProgress
#include "../include/common_misc.h"            // HexToWide, safe_strcpy
#include "../include/common_id.h"              // CreateID, MakeLocationID, ID_String
#include "../include/common_csys.h"            // EulerMatrix
#include "../include/common_reputation.h"      // Reputation::*, CostumeDescriptions
#include "../include/common_text.h"            // FmtStr
#include "../include/common_newchar.h"         // NewChar::TheDB
#include "../include/common_server_types.h"    // SCreateCharacterInfo
#include "../include/dacom.h"                  // FDUMP
#include "../include/rpc_iclient.h"            // IClient slots 91 / 92 / 97 / 98
#include "../include/msvcrt_compat.h"          // MsvcrtAtoi (retail MSVCRT.DLL atoi wraps)

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <ctime>
#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

// IServerImpl.cpp: the rpclocal client interface (DAT_06d8fb14) and the single-player
// [Time] / feelings load (FUN_06d44810). The controller broadcast (FUN_06d43650,
// Server_ControllerDispatch) is declared in common_server_controller.h.
extern IClient* g_pClientInterface;
void Server_LoadSinglePlayerState(INI_Reader& Reader);

namespace {

// .rdata:06d6b280 -- the __FILE__ every FDUMP in this family passes.
char const* const PlayerDbSource = "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp";

// ---------------------------------------------------------------------------
// FUN_06d03cf0 (.text:06d03cf0 .. .text:06d03d31), cdecl (Dst, Size, Src).
// A null Src returns 0 and leaves Dst alone; otherwise the length is clamped to
// Size - 1 (`CMP EAX,ECX / JC / LEA EAX,[ECX-1]`), copied, NUL-terminated at
// Dst[Len] and returned. Every caller stores the result as a TString length.
// ---------------------------------------------------------------------------
unsigned int CopyBoundedString(char* Dst, unsigned int Size, char const* Src) {
    if (Src == nullptr)
        return 0;
    unsigned int Length = static_cast<unsigned int>(strlen(Src));
    if (!(Length < Size))
        Length = Size - 1;
    memcpy(Dst, Src, Length);
    Dst[Length] = '\0';
    return Length;
}

// FSaveFileBuffer (a SaveGame with a closing destructor) and SaveGame's
// methods -- FUN_06d48960 is SaveGame::ReadTail, FUN_06d48b60 SaveGame::Encode
// -- are declared in server_savefile.h and defined in server_savefile.cpp;
// PlayerDB::ReadCharacterName uses them too.

} // namespace

namespace {

// The {Costume*, CostumeDescriptions const*} pair FUN_06d47120 keeps for each of
// its two costumes (.text:06d47375 .. .text:06d47391: [ESP+0x1c]/[ESP+0x20] for
// record+0x2d4, [ESP+0x24]/[ESP+0x28] for record+0x32c).
struct FCostumeTarget {
    Costume* Target;
    CostumeDescriptions const* Descriptions;
};

// ---------------------------------------------------------------------------
// FUN_06d48c80 (thiscall on FCostumeTarget, `RET 0xc`): (TypeId, Name, File).
// find_bodypart(TypeId, Name) (.text:06d48c91); a miss OR a part whose Id is 0
// both warn (.text:06d48c99 / .text:06d48c9f) and return 0.
// ---------------------------------------------------------------------------
unsigned int CostumeTarget_FindPartId(FCostumeTarget const& Holder, int TypeId,
                                      char const* Name, char const* File) {
    unsigned int Id = 0;
    bodypart const* Part = Holder.Descriptions->find_bodypart(TypeId, Name);
    if (Part != nullptr)
        Id = Part->Id;
    if (Id == 0) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: invalid costume part (%s) in (%s)\n",
              PlayerDbSource, 0x25a, Name, File);
    }
    return Id;
}

// ---------------------------------------------------------------------------
// FUN_06d48cd0 (thiscall on FCostumeTarget, `RET 8`): (Name, File). Same shape
// as FUN_06d48c80 over find_accessory(Name) (.text:06d48cdc), line 0x263.
// ---------------------------------------------------------------------------
unsigned int CostumeTarget_FindAccessoryId(FCostumeTarget const& Holder, char const* Name,
                                           char const* File) {
    unsigned int Id = 0;
    accessory const* Accessory = Holder.Descriptions->find_accessory(Name);
    if (Accessory != nullptr)
        Id = Accessory->Id;
    if (Id == 0) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: invalid accessory (%s) in character (%s)\n",
              PlayerDbSource, 0x263, Name, File);
    }
    return Id;
}

// ---------------------------------------------------------------------------
// FUN_06d48940 (thiscall on Costume, `RET 4`):
//   MOV EAX,[ECX+0x30] / MOV [ECX+EAX*4+0x10],EDX / INC [ECX+0x30]
// No bound and no zero test: an id of 0 (the miss above) is appended too.
// ---------------------------------------------------------------------------
void Costume_AddAccessory(Costume& Target, unsigned int AccessoryId) {
    Target.AccessoryIds[Target.AccessoryCount] = AccessoryId;
    ++Target.AccessoryCount;
}

// ---------------------------------------------------------------------------
// FUN_06d4ca60 (`RET 0xc`: Out, Reader, Index). One `log` line back into a
// FmtStr -- the inverse of the writer FUN_06d4cc20 ("%u, %u" then
// ", %u, %u, %u" per Val, recursing for tag 0x21):
//   06d4ca9e  MadLibId = get_value_int((*Index)++)
//   06d4caae  Count    = get_value_int((*Index)++)
//   06d4cb05  begin_mad_lib(MadLibId) / 06d4cb0f end_mad_lib()
//   loop Count times (06d4cb1b signed JLE):
//     Data  = get_value_int((*Index)++)       06d4cb2f
//     Tag   = get_value_int((*Index)++)       06d4cb3f
//     Flags = (byte)get_value_int((*Index)++) 06d4cb4f
//     if ((short)Tag == 0x21) Data = new FmtStr(<recursive read>)   06d4cb51
//     append(new Val(Tag, Data, Flags))       06d4cb8f .. 06d4cbca
//   *Out = copy of the local                  06d4cbe5
// ---------------------------------------------------------------------------
FmtStr ReadMissionLogEntry(INI_Reader& Reader, unsigned int& Index) {
    unsigned int const MadLibId = static_cast<unsigned int>(Reader.get_value_int(Index++));
    int const Count = Reader.get_value_int(Index++);

    FmtStr Local;
    Local.begin_mad_lib(MadLibId);
    Local.end_mad_lib();

    for (int I = 0; I < Count; ++I) {
        int const Data = Reader.get_value_int(Index++);
        int const Tag = Reader.get_value_int(Index++);
        unsigned char const Flags = static_cast<unsigned char>(Reader.get_value_int(Index++));
        void* DataPointer = reinterpret_cast<void*>(static_cast<intptr_t>(Data));
        if (static_cast<short>(Tag) == 0x21) {
            // operator new(0x18) then this same function into it (.text:06d4cb5f /
            // .text:06d4cb7b). The Val below owns it (FmtStr::Val::~Val deletes a
            // tag-0x21 subtree).
            DataPointer = new FmtStr(ReadMissionLogEntry(Reader, Index));
        }
        FmtStr::Val* Node = new FmtStr::Val(static_cast<unsigned short>(Tag), DataPointer, Flags);
        Local.append(Node);
    }
    return FmtStr(Local);
}

// ---------------------------------------------------------------------------
// The Reputation::RepGroupCB FUN_06d47120 builds on its frame for `group` /
// `initial_rep` (.text:06d48616 .. .text:06d48628): vptr LAB_06d46f10,
// +0x04 the record, +0x08 the player's own group id.
//
// Execute is LAB_06d46f10 (thiscall, `RET 4`):
//   06d46f2b  get_feelings_towards(*GroupId, this->GroupId, &f)
//   06d46f35  FCOMP double [06d64f00] = 1.0 ; TEST AH,0x1 ; JNZ keep
//   06d46f45  f = 0x3f733333 (0.95f)          -- i.e. if (!(f < 1.0)) f = 0.95f
//   06d46f4d  record->Relations.insert(end, 1, {*GroupId, f})
//   returns 1
// ---------------------------------------------------------------------------
struct FRepGroupRelationsCB : public ::Reputation::RepGroupCB {
    PlayerData::FSavedCharacter* Record;
    unsigned int GroupId;

    char Execute(unsigned const* OtherGroupId) override {
        float Feeling = 0.0f;
        ::Reputation::get_feelings_towards(*OtherGroupId, GroupId, Feeling);
        if (!(Feeling < 1.0))
            Feeling = 0.95f;
        ::Reputation::Relation Entry;
        Entry.GroupId = *OtherGroupId;
        Entry.Feeling = Feeling;
        Record->Relations.push_back(Entry);
        return 1;
    }
};

// TString<16> from an INI string the way every rep-group key builds it: a null
// string gives Length 0 / Data[0] 0 (.text:06d478bc), anything else goes
// through FUN_06d03cf0 with size 0x10 (.text:06d478d8).
TString<16> MakeGroupName(char const* Text) {
    TString<16> Name;
    Name.Length = 0;
    Name.Data[0] = '\0';
    if (Text != nullptr)
        Name.Length = CopyBoundedString(Name.Data, 0x10, Text);
    return Name;
}

// The costume block, one call per costume (.text:06d47a4b .. .text:06d47bf9 for
// the com_ costume on the suffix, .text:06d47c1a .. .text:06d47d98 for the bare
// costume on the whole name). Every compare is REPE CMPSB with the terminator
// counted (ECX = strlen + 1), i.e. an EXACT, case-SENSITIVE match.
// Returns false when nothing matched, so the key chain continues.
bool LoadCostumeKey(INI_Reader& Reader, FCostumeTarget const& Holder, char const* Suffix,
                    char const* File) {
    if (strcmp(Suffix, "body") == 0) {
        Holder.Target->PartIds[1] =
            CostumeTarget_FindPartId(Holder, 0, Reader.get_value_string(), File);
        return true;
    }
    if (strcmp(Suffix, "head") == 0) {
        Holder.Target->PartIds[0] =
            CostumeTarget_FindPartId(Holder, 1, Reader.get_value_string(), File);
        return true;
    }
    if (strcmp(Suffix, "lefthand") == 0) {
        Holder.Target->PartIds[2] =
            CostumeTarget_FindPartId(Holder, 2, Reader.get_value_string(), File);
        return true;
    }
    if (strcmp(Suffix, "righthand") == 0) {
        Holder.Target->PartIds[3] =
            CostumeTarget_FindPartId(Holder, 3, Reader.get_value_string(), File);
        return true;
    }
    if (strcmp(Suffix, "hair") == 0) {
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: hair is no longer a valid bodypart in character (%s)\n",
              PlayerDbSource, 0x286, File);
        return true;
    }
    if (strcmp(Suffix, "accessory") == 0) {
        unsigned int const Id =
            CostumeTarget_FindAccessoryId(Holder, Reader.get_value_string(), File);
        Costume_AddAccessory(*Holder.Target, Id);
        return true;
    }
    if (strcmp(Suffix, "costume") == 0) {
        Holder.Descriptions->get_costume(Reader.get_value_string(), *Holder.Target);
        return true;
    }
    return false;
}

// `equip` / `base_equip` / `cargo` / `base_cargo` (.text:06d47f45 ..
// .text:06d480d5): insert a default EquipDesc at the end of the list, read the
// line into it (result ignored), then stamp it from the RECORD's EquipIdMaker
// (.text:06d47fa9 CreateEquipID on record+0x360, .text:06d47fb2 set_id).
void LoadEquipKey(INI_Reader& Reader, PlayerData::FSavedCharacter& Record,
                  EquipDescList& List, bool bCargo) {
    List.Items.insert(List.Items.end(), EquipDesc());
    EquipDesc& Item = List.Items.back();
    if (bCargo)
        Loadout::ReadCargoLine(Reader, Item);
    else
        Loadout::ReadEquipLine(Reader, Item);
    Item.set_id(Record.EquipIdMaker.CreateEquipID());
}

// `collision_group` / `base_collision_group` (.text:06d480f7 .. .text:06d48238
// and .text:06d481be .. .text:06d48274).
void LoadCollisionGroupKey(INI_Reader& Reader, PlayerData::FSavedCharacter& Record,
                           std::list<CollisionGroupDesc>& Groups, int LineNotFound,
                           int LineBeforeShip) {
    CacheString Name;
    Name.Value = StringAlloc(Reader.get_value_string(0), false);
    if (Record.ShipArchetype == 0) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: collision_group %s specified BEFORE ship. Skipping.",
              PlayerDbSource, LineBeforeShip, Name.Value);
        return;
    }
    // No null test on GetShip: retail calls get_group_by_name on whatever it
    // returns (.text:06d4811a .. .text:06d4812c).
    Archetype::Ship const* Ship = Archetype::GetShip(Record.ShipArchetype);
    Archetype::CollisionGroup const* Group = Ship->get_group_by_name(Name);
    if (Group == nullptr) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: collision_group(%s) not found on ship(%s). Skipping.",
              PlayerDbSource, LineNotFound, Name.Value, Ship->Nickname.get_string());
        return;
    }
    // .text:06d48150 -- the first node whose Id matches gets Health = value 1;
    // no match does nothing.
    for (std::list<CollisionGroupDesc>::iterator It = Groups.begin(); It != Groups.end(); ++It) {
        if (It->Id == Group->Id) {
            It->Health = (float)Reader.get_value_float(1);         // .text:06d4822f FSTP float
            return;
        }
    }
}

// The account directory name: FUN_06d46370's "%02x-%08x" hash in MP, the
// narrowed wide account name in SP (wcstombs(buf, slot, 0x104)).
void BuildAccountDirName(char* Out, PlayerData const& Slot) {
    if (IsMPServer()) {
        Server_AccountDirName(Out, Slot.AccountName);
    } else {
        Out[0] = '\0';
        wcstombs(Out, Slot.AccountName, 0x104);
    }
}

// .text:06d4ce2b / .text:06d4b64a / .text:06d4c8f9 -- <accts>\<account dir>.
void BuildAccountDir(char* Out, PlayerData const& Slot) {
    char AccountDirName[0x104];
    BuildAccountDirName(AccountDirName, Slot);
    _makepath(Out, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);
}

} // namespace

// ===========================================================================
// PlayerData_LoadSaveRecord -- retail FUN_06d47120 (.text:06d47120 ..
// .text:06d48786), thiscall on the PlayerData, (Path, Record), `RET 8`.
// ===========================================================================
bool __cdecl PlayerData_LoadSaveRecord(PlayerData& Slot, char const* Path,
                                       PlayerData::FSavedCharacter& Record) {
    (void)Slot; // only forwarded as the log reader's unused ECX (.text:06d48510)

    bool bSawPlayer = false;
    FSaveFileBuffer Buffer;
    INI_Reader Reader;

    // .text:06d47182 -- fopen(path, "rt"), then the first four bytes.
    Buffer.File = fopen(Path, "rt");
    if (Buffer.File == nullptr)
        return false;
    char Magic[4] = { 0, 0, 0, 0 };
    fread(Magic, 1, 4, Buffer.File);
    if (memcmp(Magic, Server_FLS1Magic, 4) == 0) {
        // .text:06d471c5 -- "FLS1": close, reopen "rb" (.rdata:06d6aea8), read
        // from offset 4 and decode in place (FUN_06d48b60).
        fclose(Buffer.File);
        Buffer.File = fopen(Path, "rb");
        if (Buffer.File == nullptr)
            return false;
        Buffer.ReadTail(4);
        Buffer.Encode();
    } else if (memcmp(Magic, "FLSG", 4) == 0) {
        // .text:06d4720f -- .rdata:06d6b304 = "FLSG": straight to the exit,
        // whose inlined FUN_06d48c20 (.text:06d488c5 .. .text:06d488ec) closes
        // the file -- ~FSaveFileBuffer here.
        return false;
    } else {
        // .text:06d4721c -- plain text: the whole file from offset 0.
        Buffer.ReadTail(0);
    }

    // .text:06d4724c
    Reader.open_memory(Buffer.Contents.c_str(),
                       static_cast<unsigned int>(Buffer.Contents.size()));

    FCostumeTarget ComCostume = { &Record.ComCostume, nullptr };
    FCostumeTarget BaseCostume = { &Record.BaseCostume, nullptr };

    while (Reader.read_header()) {
        RefreshLoadingProgress();          // .text:06d47274
        int CommentDepth = 0;              // .text:06d47286
        if (!Reader.is_header("Player"))
            continue;

        // ---- per-[Player] reset, .text:06d47298 .. .text:06d47430 ----------
        Record.Position[0] = 0.0f;
        Record.Position[1] = 0.0f;
        Record.Position[2] = 0.0f;
        for (int Row = 0; Row < 3; ++Row)
            for (int Col = 0; Col < 3; ++Col)
                Record.Orientation[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
        bSawPlayer = true;                 // .text:06d472ea
        Record.SystemId = 0;
        Record.BaseId = 0;
        Record.LocationId = 0;             // +0x27c (last_base) is NOT reset
        Record.Description = L"";          // .rdata:06d6b2f8
        Record.DescripStrId = 0;
        Record.Money = 0x7b0;              // .text:06d47361
        Record.Interface = 3;              // .text:06d4736b
        ComCostume.Descriptions = GetCostumeDescriptions();
        BaseCostume.Descriptions = GetCostumeDescriptions();
        Record.WeaponGroups.clear();       // .text:06d473a3 erase(0, npos)
        Record.Visits.clear();             // .text:06d47430 erase(begin, end)

        while (Reader.read_value()) {
            // .text:06d47450 -- block comments.
            if (Reader.is_value("/*"))
                ++CommentDepth;
            else if (Reader.is_value("*/"))
                --CommentDepth;
            if (CommentDepth > 0)          // .text:06d47490 signed JG
                continue;

            if (Reader.is_value("name")) {
                wchar_t Wide[0x400];
                HexToWide(Reader.get_value_string(), Wide);
                Record.Name.assign(Wide, wcslen(Wide));
                continue;
            }
            if (Reader.is_value("description")) {
                wchar_t Wide[0x400];
                HexToWide(Reader.get_value_string(), Wide);
                Record.Description.assign(Wide, wcslen(Wide));
                continue;
            }
            if (Reader.is_value("descrip_strid")) {
                Record.DescripStrId = static_cast<unsigned int>(Reader.get_value_int(0));
                continue;
            }
            if (Reader.is_value("tstamp")) {
                if (Reader.is_value_empty(0) || Reader.is_value_empty(1)) {
                    Record.TimestampHigh = 0;
                    Record.TimestampLow = 0;
                } else {
                    Record.TimestampHigh = static_cast<unsigned int>(Reader.get_value_int(0));
                    Record.TimestampLow = static_cast<unsigned int>(Reader.get_value_int(1));
                }
                continue;
            }
            if (Reader.is_value("ship_archetype")) {
                // .text:06d47626 MSVCRT.DLL::atoi: wraps mod 2^32, so a hash id above
                // INT_MAX round-trips; the UCRT atoi would clamp it to 0x7fffffff.
                Record.ShipArchetype =
                    static_cast<unsigned int>(MsvcrtAtoi(Reader.get_value_string()));
                if (Record.ShipArchetype == 0) {
                    ID_String Nickname;
                    Nickname.Id = CreateID(Reader.get_value_string());
                    Record.ShipArchetype = Archetype::GetShipID(Nickname);
                }
                // .text:06d47668 -- no null test on GetShip.
                Archetype::Ship const* Ship = Archetype::GetShip(Record.ShipArchetype);
                Ship->get_undamaged_collision_group_list(Record.CollisionGroups);
                Ship->get_undamaged_collision_group_list(Record.BaseCollisionGroups);
                continue;
            }
            if (Reader.is_value("system")) {
                Record.SystemId = Universe::get_system_id(Reader.get_value_string());
                if (Record.SystemId == 0) {
                    FDUMP(0x100001,
                          "%s(%d) : *** ERROR: game (%s) has invalid system name (%s) in player file (%s)\n",
                          PlayerDbSource, 0x376, Reader.get_file_name(),
                          Reader.get_value_string(), Path);
                }
                continue;
            }
            if (Reader.is_value("base")) {
                Record.BaseId = Universe::get_base_id(Reader.get_value_string());
                if (Record.BaseId == 0) {
                    FDUMP(0x100001,
                          "%s(%d) : *** ERROR: game (%s) has invalid base name (%s) in player file (%s)\n",
                          PlayerDbSource, 0x37e, Reader.get_file_name(),
                          Reader.get_value_string(), Path);
                }
                continue;
            }
            if (Reader.is_value("last_base")) {
                Record.LastBaseId = Universe::get_base_id(Reader.get_value_string());
                if (Record.LastBaseId == 0) {
                    FDUMP(0x100001,
                          "%s(%d) : *** ERROR: game (%s) has invalid last_base name (%s) in player file (%s)\n",
                          PlayerDbSource, 0x386, Reader.get_file_name(),
                          Reader.get_value_string(), Path);
                }
                continue;
            }
            if (Reader.is_value("location")) {
                Record.LocationId = static_cast<unsigned int>(Reader.get_value_int(0));
                continue;
            }
            if (Reader.is_value("rank")) {
                Record.Rank = static_cast<unsigned int>(Reader.get_value_int(0));
                continue;
            }
            if (Reader.is_value("group") || Reader.is_value("initial_rep")) {
                // .text:06d485bb -- get_id, empty the relations (FUN_06d573e0),
                // then enumerate every group through the callback.
                TString<16> const GroupName = MakeGroupName(Reader.get_value_string());
                FRepGroupRelationsCB Callback;
                Callback.Record = &Record;
                Callback.GroupId = ::Reputation::get_id(GroupName);
                Record.Relations.clear();
                ::Reputation::enumerate(&Callback);
                continue;
            }
            if (Reader.is_value("rep_group")) {
                // .text:06d47890 -- an empty value resolves "" (.rdata:06d66ee4).
                TString<16> const GroupName = Reader.is_value_empty(0)
                    ? MakeGroupName("")
                    : MakeGroupName(Reader.get_value_string(0));
                Record.RepGroup = ::Reputation::get_id(GroupName);
                continue;
            }
            if (Reader.is_value("house")) {
                float const Feeling = (float)Reader.get_value_float(0);   // .text:06d4795d FSTP float
                TString<16> const GroupName = MakeGroupName(Reader.get_value_string(1));
                ::Reputation::Relation Entry;
                Entry.GroupId = ::Reputation::get_id(GroupName);
                Entry.Feeling = Feeling;
                Record.Relations.push_back(Entry);   // FUN_06d58b40 insert(end, 1, v)
                continue;
            }
            if (Reader.is_value("voice")) {
                char const* Voice = Reader.get_value_string();
                if (Voice == nullptr) {
                    Record.Voice.Length = 0;
                    Record.Voice.Data[0] = '\0';
                } else {
                    Record.Voice.Length = CopyBoundedString(Record.Voice.Data, 0x20, Voice);
                }
                continue;
            }

            // .text:06d47a1a -- the costume block, on the raw key name.
            {
                char const* const KeyName = Reader.get_name_ptr();
                char const* const File = Reader.get_file_name();
                bool bHandled = false;
                if (strstr(KeyName, "com_") == KeyName)
                    bHandled = LoadCostumeKey(Reader, ComCostume, KeyName + strlen("com_"), File);
                if (!bHandled) {
                    // .text:06d47bfe -- name and file are fetched again.
                    char const* const BareName = Reader.get_name_ptr();
                    bHandled = LoadCostumeKey(Reader, BaseCostume, BareName,
                                              Reader.get_file_name());
                }
                if (bHandled)
                    continue;
            }

            // .text:06d47dbe / 06d47de8 / 06d47e12 / 06d47e3c -- MSVCRT ftol on the
            // unrounded ST0 (64-bit truncation, low dword stored).
            if (Reader.is_value("money")) {
                Record.Money = (int)(long long)Reader.get_value_float(0);
            } else if (Reader.is_value("num_kills")) {
                Record.NumKills = (int)(long long)Reader.get_value_float(0);
            } else if (Reader.is_value("num_misn_successes")) {
                Record.NumMissionSuccesses = (int)(long long)Reader.get_value_float(0);
            } else if (Reader.is_value("num_misn_failures")) {
                Record.NumMissionFailures = (int)(long long)Reader.get_value_float(0);
            } else if (Reader.is_value("pos")) {
                Record.Position[0] = (float)Reader.get_value_float(0);   // .text:06d47e66..90 FSTP float
                Record.Position[1] = (float)Reader.get_value_float(1);
                Record.Position[2] = (float)Reader.get_value_float(2);
            } else if (Reader.is_value("rotate")) {
                Vector Angles;
                Angles.x = (float)Reader.get_value_float(0);             // .text:06d47eb5..e1 FSTP float
                Angles.y = (float)Reader.get_value_float(1);
                Angles.z = (float)Reader.get_value_float(2);
                Matrix const Rotation = EulerMatrix(Angles);
                memcpy(Record.Orientation, Rotation.E, sizeof(Record.Orientation)); // REP MOVSD x9
            } else if (Reader.is_value("starting_ring")) {
                Record.StartingRing = CreateID(Reader.get_value_string());
            } else if (Reader.is_value("equip")) {
                LoadEquipKey(Reader, Record, Record.Equipment, false);
            } else if (Reader.is_value("base_equip")) {
                LoadEquipKey(Reader, Record, Record.BaseEquipment, false);
            } else if (Reader.is_value("cargo")) {
                LoadEquipKey(Reader, Record, Record.Equipment, true);
            } else if (Reader.is_value("base_cargo")) {
                LoadEquipKey(Reader, Record, Record.BaseEquipment, true);
            } else if (Reader.is_value("collision_group")) {
                LoadCollisionGroupKey(Reader, Record, Record.CollisionGroups, 0x41f, 0x424);
            } else if (Reader.is_value("base_collision_group")) {
                LoadCollisionGroupKey(Reader, Record, Record.BaseCollisionGroups, 0x444, 0x449);
            } else if (Reader.is_value("wg")) {
                // .text:06d48295 -- the whole raw line, then "\n".
                Record.WeaponGroups.append(Reader.get_line_ptr());
                Record.WeaponGroups.append("\n");
            } else if (Reader.is_value("hull_status")) {
                Record.HullStatus = (float)Reader.get_value_float(0);    // .text:06d48446 FSTP float
            } else if (Reader.is_value("base_hull_status")) {
                Record.BaseHullStatus = (float)Reader.get_value_float(0); // .text:06d4846b FSTP float
            } else if (Reader.is_value("visit")) {
                // .text:06d48496 -- insert({id, 0}).first->second = (byte)v1.
                unsigned int const SystemId = static_cast<unsigned int>(Reader.get_value_int(0));
                unsigned char const Flags = static_cast<unsigned char>(Reader.get_value_int(1));
                Record.Visits.insert(std::make_pair(SystemId, static_cast<unsigned char>(0)))
                    .first->second = Flags;
            } else if (Reader.is_value("interface")) {
                Record.Interface = static_cast<unsigned int>(Reader.get_value_int(0));
            } else if (Reader.is_value("log")) {
                unsigned int LogIndex = 0;                                   // .text:06d48524
                Record.MissionLog.push_back(ReadMissionLogEntry(Reader, LogIndex));
            } else {
                // .text:06d485a8 -- `logid` is tested and the result dropped.
                Reader.is_value("logid");
            }
        }
    }

    // .text:06d48682 -- "Deck2" folds back to "Deck".
    if (MakeLocationID(Record.BaseId, "Deck2") == Record.LocationId)
        Record.LocationId = MakeLocationID(Record.BaseId, "Deck");

    // .text:06d488c5 .. .text:06d488ec -- the inlined FUN_06d48c20 closes the
    // file: ~FSaveFileBuffer.
    return bSawPlayer;
}

// ===========================================================================
// PlayerData_LoadSavedCharacter -- retail FUN_06d48d50 (.text:06d48d50 ..
// .text:06d48e6e), thiscall, (Dir, FileName), `RET 8`.
// ===========================================================================
bool __cdecl PlayerData_LoadSavedCharacter(PlayerData& Slot, char const* Dir,
                                           char const* FileName) {
    CHARACTER_ID Key;
    safe_strcpy(Key.Buffer, 0x200, FileName);                         // .text:06d48d79
    PlayerData::FSavedCharacter& Record = Slot.saved_characters[Key];  // FUN_06d57090

    char Path[0x104];
    _makepath(Path, nullptr, Dir, FileName, nullptr);                  // .text:06d48dac

    if (PlayerData_LoadSaveRecord(Slot, Path, Record)) {
        safe_strcpy(Record.FileName, 0x200, FileName);                 // .text:06d48dcf
        return true;
    }
    FDUMP(0x100002, "%s(%d) : *** WARNING: Failed to load saved game %s",
          PlayerDbSource, 0x4a8, Path);
    std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator It =
        Slot.saved_characters.find(Key);                               // FUN_06d5a180
    if (It != Slot.saved_characters.end())
        Slot.saved_characters.erase(It);                               // FUN_06d57760
    return false;
}

// ===========================================================================
// PlayerData_ReloadSavedCharacter -- retail FUN_06d4b5a0 (.text:06d4b5a0 ..
// .text:06d4b781), thiscall, (FileName), `RET 4`.
// ===========================================================================
void __cdecl PlayerData_ReloadSavedCharacter(PlayerData& Slot, char const* FileName) {
    // .text:06d4b5b9 -- the extension is masked to NULL in MP:
    // `NEG AL / SBB EAX,EAX / NOT EAX / AND EAX,"fl"`.
    char KeyName[0x104];
    _makepath(KeyName, nullptr, nullptr, FileName, IsMPServer() ? nullptr : "fl");

    char AccountDir[0x104];
    BuildAccountDir(AccountDir, Slot);                                  // .text:06d4b5de .. 06d4b699

    char Path[0x104];
    _makepath(Path, nullptr, AccountDir, KeyName, nullptr);            // .text:06d4b6b2

    CHARACTER_ID Key;
    safe_strcpy(Key.Buffer, 0x200, KeyName);                           // .text:06d4b6d1
    PlayerData::FSavedCharacter& Record = Slot.saved_characters[Key];
    if (PlayerData_LoadSaveRecord(Slot, Path, Record)) {
        safe_strcpy(Record.FileName, 0x200, KeyName);                  // .text:06d4b70d
        return;
    }
    std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator It =
        Slot.saved_characters.find(Key);
    if (It != Slot.saved_characters.end())
        Slot.saved_characters.erase(It);
}

// ===========================================================================
// PlayerData_SaveGame -- retail FUN_06d4c800 (.text:06d4c800 .. .text:06d4ca5c),
// thiscall, (Name, Description, DescripStrId), `RET 0xc`.
// ===========================================================================
bool __cdecl PlayerData_SaveGame(PlayerData& Slot, char const* Name,
                                 wchar_t const* Description, unsigned int DescripStrId) {
    std::string FileName;
    if (Name[0] != '\0') {
        // .text:06d4c850 -- REP MOVSD of 0x80 dwords, cut at the first '.'.
        char Copy[0x200];
        memcpy(Copy, Name, sizeof(Copy));
        char* Dot = strchr(Copy, '.');
        if (Dot != nullptr)
            *Dot = '\0';
        FileName.assign(Copy, strlen(Copy));
    } else {
        char AccountDir[0x104];
        BuildAccountDir(AccountDir, Slot);                              // .text:06d4c8cc .. 06d4c919

        // .text:06d4c91e -- ESI = rand(); up to 200 probes of <dir>\Save%04x.fl
        // (the extension is NOT masked in MP here). A name is free when
        // GetFileAttributesA fails with ERROR_FILE_NOT_FOUND.
        int Seed = rand();
        for (int Attempt = 1; ; ++Attempt) {
            char Candidate[0x20];
            sprintf(Candidate, "Save%04x", static_cast<int>(static_cast<short>(Seed))); // MOVSX
            char Probe[0x104];
            _makepath(Probe, nullptr, AccountDir, Candidate, "fl");
            if (GetFileAttributesA(Probe) == INVALID_FILE_ATTRIBUTES &&
                GetLastError() == ERROR_FILE_NOT_FOUND) {
                FileName.assign(Candidate, strlen(Candidate));
                break;
            }
            ++Seed;
            if (!(Attempt < 0xc8))
                break;                                                   // name stays ""
        }
    }

    if (PlayerData_SerializeToFile(Slot, FileName.c_str(), Description, DescripStrId)) {
        PlayerData_ReloadSavedCharacter(Slot, FileName.c_str());        // .text:06d4c9e6
        return true;
    }
    ++Slot.save_failure_count;                                          // .text:06d4ca15
    return false;
}

// ===========================================================================
// PlayerData_MissionSave -- retail FUN_06d4c600 (.text:06d4c600 .. .text:06d4c7f7),
// thiscall, (DescripStrId), `RET 4`.
// ===========================================================================
bool __cdecl PlayerData_MissionSave(PlayerData& Slot, unsigned int DescripStrId) {
    if (!PlayerData_SaveGame(Slot, "", nullptr, DescripStrId))
        return false;

    // .text:06d4c639 .. .text:06d4c768 -- the account name (the inline copy of
    // FUN_06d46370's hash in MP, wcstombs in SP), then .text:06d4c736
    // GetUserDataPath + "Accts[\MultiPlayer]" into the global DAT_06d90158, which
    // is exactly what Server_GetAccountsRoot builds.
    char AccountDirName[0x104];
    BuildAccountDirName(AccountDirName, Slot);
    char AccountDir[0x104];
    _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);

    // .text:06d4c7b9 -- <dir>\AutoSave.fl, extension unmasked.
    char Destination[0x104];
    _makepath(Destination, nullptr, AccountDir, "AutoSave", "fl");

    // .text:06d4c7e9 -- CopyFileA(last written path, dest, FALSE); its BOOL is
    // what AL carries out.
    return CopyFileA(GServerLastSavePath, Destination, FALSE) != 0;
}

// ===========================================================================
// PlayerData_AutoSave -- retail FUN_06d4c430 (.text:06d4c430 .. .text:06d4c5e0).
// ===========================================================================
bool __cdecl PlayerData_AutoSave(PlayerData& Slot) {
    if (IsMPServer()) {
        // .text:06d4c447 .. .text:06d4c465
        if (IsMPServer() && !Slot.selected_character.is_valid())
            return false;
        if (Slot.system_id == 0)                                        // +0x3e0
            return false;
    }

    if (IsMPServer()) {
        // .text:06d4c47f -- find(selected) with NO end() test: CharacterSelect
        // stores +0x64 only after its own find succeeded (.text:06d4a62f), so
        // retail never reaches here without a record.
        std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator It =
            Slot.saved_characters.find(Slot.selected_character);

        // .text:06d4c4f8 .. .text:06d4c52b -- "<date> <time>".
        char Time[0x10];
        char Date[0x10];
        wchar_t Description[0x200];
        _tzset();
        _strtime(Time);
        _strdate(Date);
        swprintf(Description, 0x200, L"%S %S", Date, Time);
        return PlayerData_SerializeToFile(Slot, It->second.FileName, Description, 0);
    }

    // .text:06d4c557 -- single player. Universe::IBase vtable +0x54 is the
    // port's IBase::bAutosaveForbidden field.
    bool bAllowed = true;
    if ((Slot.current_base & 0x3fffffff) != 0) {
        Universe::IBase const* Base = Universe::get_base(Slot.current_base);
        if (Base->bAutosaveForbidden)
            bAllowed = false;
    }
    if (Slot.bSkipNextAutosave == 0 && bAllowed) {
        char Name[0x200];
        safe_strcpy(Name, 0x200, "AutoSave");                           // .text:06d4c5a5
        return PlayerData_SaveGame(Slot, Name, nullptr, 0x65c);         // .text:06d4c5bf
    }
    Slot.bSkipNextAutosave = 0;                                         // .text:06d4c5ce
    return false;
}

// ===========================================================================
// PlayerData_SkipNextAutosave -- retail FUN_06d4c5f0.
// ===========================================================================
void __cdecl PlayerData_SkipNextAutosave(PlayerData& Slot) {
    Slot.bSkipNextAutosave = 1;
}

// ===========================================================================
// PlayerData_ApplySavedCharacter -- the record -> slot copy of retail
// FUN_06d4a5d0, .text:06d4ac77 .. .text:06d4b089. EBP is the record, EBX the
// slot.
// ===========================================================================
void __cdecl PlayerData_ApplySavedCharacter(PlayerData& Slot,
                                            PlayerData::FSavedCharacter const& Record,
                                            unsigned int SessionId) {
    // .text:06d4ac77 .. .text:06d4acc7
    Slot.current_base = Record.BaseId;                                  // +0x3ec
    Slot.last_base_id = Record.LastBaseId;                              // +0x3f0
    Slot.current_location = Record.LocationId;                          // +0x3f8
    if ((Record.BaseId & 0x3fffffff) != 0) {
        Slot.last_base_id = Record.BaseId;
        // IBase vtable +0x10 -- the port's IBase::SystemId field.
        Slot.system_id = Universe::get_base(Record.BaseId)->SystemId;
    } else {
        Slot.system_id = Record.SystemId;
    }

    // .text:06d4acc1 .. .text:06d4ada2 -- the rep vibe is rebuilt.
    if (Slot.rep_vibe != -1) {
        ::Reputation::Vibe::Free(Slot.rep_vibe);
        Slot.rep_vibe = -1;
    }
    Server_VibeID_Alloc(reinterpret_cast<unsigned int*>(&Slot.rep_vibe)); // FUN_06d45b60
    ::Reputation::Vibe::Alloc(Slot.rep_vibe);                           // .text:06d4ace9
    ::Reputation::Vibe::SetClientID(Slot.rep_vibe, SessionId);
    FmtStr const EmptyName;                                             // .text:06d4ad03
    ::Reputation::Vibe::Set(Slot.rep_vibe, Record.RepGroup, Record.Rank,
                            static_cast<unsigned char>(Record.Relations.size()),
                            Record.Relations.empty() ? nullptr : &Record.Relations[0],
                            EmptyName, EmptyName, Record.Name.c_str());

    // .text:06d4ada8 -- the com_ costume: four parts, the count, then only
    // `count` accessory ids.
    for (int Part = 0; Part < 4; ++Part)
        Slot.com_costume.PartIds[Part] = Record.ComCostume.PartIds[Part];
    Slot.com_costume.AccessoryCount = Record.ComCostume.AccessoryCount;
    for (int I = 0; I < static_cast<int>(Slot.com_costume.AccessoryCount); ++I)
        Slot.com_costume.AccessoryIds[I] = Record.ComCostume.AccessoryIds[I];

    // .text:06d4adf2 -- FUN_06d03cf0(slot+0x2c4, 0x20, record+0x30c).
    Slot.voice_length = CopyBoundedString(Slot.voice, 0x20, Record.Voice.Data);

    // .text:06d4ae25 -- the bare costume, same shape.
    for (int Part = 0; Part < 4; ++Part)
        Slot.costume.PartIds[Part] = Record.BaseCostume.PartIds[Part];
    Slot.costume.AccessoryCount = Record.BaseCostume.AccessoryCount;
    for (int I = 0; I < static_cast<int>(Slot.costume.AccessoryCount); ++I)
        Slot.costume.AccessoryIds[I] = Record.BaseCostume.AccessoryIds[I];

    // .text:06d4ae70 .. .text:06d4ae9d
    Slot.initial_pos[0] = Record.Position[0];
    Slot.initial_pos[1] = Record.Position[1];
    Slot.initial_pos[2] = Record.Position[2];
    memcpy(Slot.initial_ornt, Record.Orientation, sizeof(Slot.initial_ornt));

    // .text:06d4ae9f .. .text:06d4af28
    Slot.ship_archetype = Record.ShipArchetype;                         // +0x264
    Slot.relative_health = Record.HullStatus;                           // +0x268
    Slot.equipment.Items = Record.Equipment.Items;                      // list::operator=
    Slot.collision_groups = Record.CollisionGroups;                     // +0x26c

    // .text:06d4af2e .. .text:06d4afab
    Slot.cash = Record.Money;                                           // +0x31c
    Slot.equip_id_maker = Record.EquipIdMaker;                          // +0x3c8
    Slot.num_kills = Record.NumKills;                                   // +0x334
    Slot.num_mission_successes = Record.NumMissionSuccesses;            // +0x338
    Slot.num_mission_failures = Record.NumMissionFailures;              // +0x33c
    Slot.save_failure_count = 0;                                        // +0x344
    Slot.current_location = 0;                                          // +0x3f8, after the copy above
    Slot.Field_0x3FC = 0;
    Slot.ship_object_id = 0;                                            // +0x3e4
    Slot.created_ship_id = 0;                                           // +0x3e8
    Slot.exited_base_id = 0;                                            // +0x410
    Slot.bSkipNextAutosave = 0;                                        // +0x340
    Slot.starting_ring = Record.StartingRing;                           // +0x414
    Slot.rank_level = Record.Rank;                                      // +0x284

    // .text:06d4afc8 .. .text:06d4b096
    Slot.weapon_group_name = Record.WeaponGroups;                       // +0x380
    Slot.visit_map = Record.Visits;                                     // +0x3a0
    Slot.mission_log = Record.MissionLog;                               // +0x390
    Slot.interface_state = Record.Interface;                            // +0x39c
}

// ===========================================================================
// PlayerData_SendVisitedState -- retail FUN_06d4b520 (.text:06d4b520 ..
// .text:06d4b58f), thiscall on the slot, (SessionId), `RET 4`.
//   06d4b527  n = [this+0x3b0] (visit map size) ; 06d4b52d size = n*5 + 4
//   06d4b532  block = operator new(size) ; block[0] = n
//   06d4b550  per map node in order: dword key (+0xc), byte flags (+0x10), 5-byte stride
//   06d4b584  IClient +0x170 (slot 92)(SessionId, block, size) -- rpclocal's
//             sender (0x06d8b540, ch1 op 0x13) keeps the block and frees it
// ===========================================================================
void __cdecl PlayerData_SendVisitedState(PlayerData& Slot, unsigned int SessionId) {
    unsigned int const Count = static_cast<unsigned int>(Slot.visit_map.size());
    unsigned int const Size = Count * 5 + 4;
    unsigned char* const Block = static_cast<unsigned char*>(::operator new(Size));
    memcpy(Block, &Count, 4);
    unsigned char* Cursor = Block + 4;
    for (std::map<unsigned int, unsigned char>::const_iterator It = Slot.visit_map.begin();
         It != Slot.visit_map.end(); ++It) {
        memcpy(Cursor, &It->first, 4);
        Cursor[4] = It->second;
        Cursor += 5;
    }
    g_pClientInterface->SendCommonSetVisitedState(SessionId, Block, Size);
}

namespace {

// FUN_06d50850 (thiscall on FmtStr, plain RET): 6 + per Val 7, 0x17 for tag
// 0x4e, (this function on the nested FmtStr) + 3 for tag 0x21. FUN_06d50740
// inlines the same loop for each log entry (.text:06d50768 .. .text:06d507b7).
int MissionLogEntrySize(FmtStr const& Entry) {
    int Size = 6;
    for (FmtStr::Val const* Node = Entry.Head; Node != nullptr; Node = Node->Next) {
        int const Tag = Node->TypeTag;
        if (Tag == 0x21)
            Size += MissionLogEntrySize(*Node->Subtree) + 3;
        else
            Size += (Tag == 0x4e) ? 0x17 : 7;   // SETE / DEC / AND 0xfffffff0 / ADD 0x17
    }
    return Size;
}

} // namespace

// ===========================================================================
// PlayerData_SendMissionLog -- retail FUN_06d50740 (.text:06d50740 ..
// .text:06d50844), thiscall on the slot, (SessionId), `RET 4`.
//   size = 8 + sum(entry sizes)             .text:06d50762 .. .text:06d507b7
//   block = operator new(size); [0] = 1, [1] = list size (+0x398)
//   cursor = block + 8, offset = 8; AppendMissionLogData(&entry, cursor, offset, size)
//   per entry (.text:06d50813), then IClient +0x184 (slot 97)(SessionId, block,
//   offset) -- the running offset, not `size` (.text:06d50824). rpclocal keeps and
//   frees the block (flag 1, sender 0x06d8b7f0).
// ===========================================================================
void __cdecl PlayerData_SendMissionLog(PlayerData& Slot, unsigned int SessionId) {
    int Size = 8;
    for (std::list<FmtStr>::const_iterator It = Slot.mission_log.begin();
         It != Slot.mission_log.end(); ++It)
        Size += MissionLogEntrySize(*It);

    unsigned char* const Block = static_cast<unsigned char*>(::operator new(static_cast<size_t>(Size)));
    unsigned int const Header[2] = { 1u, static_cast<unsigned int>(Slot.mission_log.size()) };
    memcpy(Block, Header, sizeof(Header));
    unsigned char* Cursor = Block + 8;
    int Offset = 8;
    for (std::list<FmtStr>::const_iterator It = Slot.mission_log.begin();
         It != Slot.mission_log.end(); ++It)
        AppendMissionLogData(&*It, Cursor, Offset, Size);
    g_pClientInterface->SendCommonSetMissionLog(SessionId, Block, static_cast<unsigned int>(Offset));
}

// ===========================================================================
// PlayerData_LoadSelectedCharacter -- the second half of retail FUN_06d4a5d0,
// .text:06d4aa85 .. .text:06d4b1fa (both the SP and MP paths join here).
// EBX is the slot, EBP the found record (node + 0x20c), [ESP+0x1924] the session.
// ===========================================================================
bool __cdecl PlayerData_LoadSelectedCharacter(PlayerData& Slot,
                                              PlayerData::FSavedCharacter const& Record,
                                              unsigned int SessionId) {
    // .text:06d4aa85 .. .text:06d4ab60 -- <accts>\<account dir>\<record file name>:
    // FUN_06d46370 in MP / wcstombs in SP, the Accts root into DAT_06d90158,
    // then _makepath(S+0x9c, NULL, S+0x9c, record+0x000, NULL).
    char AccountDir[0x104];
    BuildAccountDir(AccountDir, Slot);
    char Path[0x104];
    _makepath(Path, nullptr, AccountDir, Record.FileName, nullptr);

    // .text:06d4ab63 .. .text:06d4ac57 -- the file again, the same FLS1 / FLSG /
    // plain three-way as FUN_06d47120. open_memory runs on every path, over an
    // empty buffer when fopen failed or the file is FLSG (.text:06d4ac3d).
    FSaveFileBuffer Buffer;                                             // S+0x4c, SaveGame ctor inlined
    INI_Reader Reader;                                                  // S+0x3a8
    Buffer.File = fopen(Path, "rt");
    if (Buffer.File != nullptr) {
        char Magic[4] = { 0, 0, 0, 0 };
        fread(Magic, 1, 4, Buffer.File);
        if (memcmp(Magic, Server_FLS1Magic, 4) == 0) {
            fclose(Buffer.File);
            Buffer.File = fopen(Path, "rb");
            if (Buffer.File != nullptr) {
                Buffer.ReadTail(4);                     // FUN_06d48960
                Buffer.Encode();                        // FUN_06d48b60
            }
        } else if (memcmp(Magic, "FLSG", 4) != 0) {
            Buffer.ReadTail(0);
        }
    }
    Reader.open_memory(Buffer.Contents.c_str(),
                       static_cast<unsigned int>(Buffer.Contents.size()));

    // .text:06d4ac5d .. .text:06d4ac6f -- single player only: [Time], feelings,
    // controller event 0x40 (FUN_06d44810, IServerImpl.cpp).
    if (SinglePlayer())
        Server_LoadSinglePlayerState(Reader);

    // .text:06d4ac77 .. .text:06d4b096 -- the record -> slot copy.
    PlayerData_ApplySavedCharacter(Slot, Record, SessionId);

    // .text:06d4b09f -- this->FUN_06d4b520(SessionId).
    PlayerData_SendVisitedState(Slot, SessionId);

    // .text:06d4b0a4 .. .text:06d4b0d5 -- IClient +0x16c (slot 91)(SessionId,
    // weapon groups c_str, strlen). rpclocal's sender leaves the not-owned flag
    // clear (payload+0x08 = 0), so the string's own buffer goes out as is.
    char const* const WeaponGroups = Slot.weapon_group_name.c_str();
    g_pClientInterface->SendCommonSetWeaponGroup(SessionId,
                                                 reinterpret_cast<const unsigned char*>(WeaponGroups),
                                                 static_cast<unsigned int>(strlen(WeaponGroups)));

    // .text:06d4b0e5 -- this->FUN_06d50740(SessionId).
    PlayerData_SendMissionLog(Slot, SessionId);

    // .text:06d4b0f3 .. .text:06d4b10f -- a 4-byte operator new block holding
    // +0x39c, IClient +0x188 (slot 98)(SessionId, block, 4); rpclocal owns it.
    unsigned int* const InterfaceBlock = static_cast<unsigned int*>(::operator new(4));
    *InterfaceBlock = Slot.interface_state;
    // A fresh operator-new block: SendCommonSetInterfaceState's packet takes it over.
    g_pClientInterface->SendCommonSetInterfaceState(SessionId,
                                                    reinterpret_cast<unsigned char*>(InterfaceBlock), 4);

    // .text:06d4b115 .. .text:06d4b132 -- FUN_06d43650(5, &{SessionId, &reader}):
    // the character-loaded broadcast hands the controllers the open reader.
    struct FCharacterLoadedEvent {
        unsigned int SessionId;                                         // S+0x64
        INI_Reader* Reader;                                             // S+0x68
    };
    FCharacterLoadedEvent Event;
    Event.SessionId = SessionId;
    Event.Reader = &Reader;
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLoad, &Event);

    // .text:06d4b13a .. .text:06d4b1dc -- ~FmtStr, ~INI_Reader, fclose, the buffer.
    if (Buffer.File != nullptr) {
        fclose(Buffer.File);
        Buffer.File = nullptr;
    }
    return true;                                                        // .text:06d4b1e2
}

// ===========================================================================
// PlayerData_CreateNewCharacter -- retail FUN_06d4b790 (.text:06d4b790 ..
// .text:06d4c227), thiscall on the PlayerData, (SCreateCharacterInfo const*),
// `RET 4`. Only caller: PlayerDB::create_new_character, .text:06d541a0.
//
// Frame (S = ESP after the four register pushes): +0x13 result byte, +0x14 pilot,
// +0x18 package, +0x1c home-base record, +0x6c faction, +0x24 the SaveGame_Writer
// (vptr 06d6b5c0 then 06d6b5a0, FILE* +0x28, buffer +0x2c, path +0x3c), +0x54 the
// template {FILE*, string}, +0x70 "<hash>.fl", +0x88 hash / module path / template
// path, +0x18c the output path, +0x594 one substituted value, +0x994 the joined
// value line, +0xd94 the INI_Reader.
// ===========================================================================
namespace {

// FUN_06d464f0 (cdecl, (Out, AccountName)), .text:06d464f0 .. .text:06d466ea:
// MP -- the FUN_06d46370 hash inlined (.text:06d46512 .. .text:06d465fb), SP --
// wcstombs(buf, name, 0x104) (.text:06d46681); both then GetUserDataPath +
// "Accts[\MultiPlayer]" into DAT_06d90158 (what Server_GetAccountsRoot builds)
// and _makepath(Out, NULL, DAT_06d90158, buf, NULL).
void BuildAccountDirForName(char* Out, wchar_t const* AccountName) {
    char AccountDirName[0x104];
    if (IsMPServer())
        Server_AccountDirName(AccountDirName, AccountName);
    else
        wcstombs(AccountDirName, AccountName, 0x104);
    _makepath(Out, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);
}

} // namespace

bool __cdecl PlayerData_CreateNewCharacter(PlayerData& Slot, SCreateCharacterInfo const& Info) {
    // .text:06d4b7d0 -- FUN_06d464f0(S+0x18c, this). The `this == NULL` arm
    // (.text:06d4b7f2, empty directory) is unreachable: the only caller passes
    // EBP = &m_PlayerArray[SessionId - 1] (.text:06d5416f).
    char AccountDir[0x104];
    BuildAccountDirForName(AccountDir, Slot.AccountName);

    // .text:06d4b80a -- FUN_06d46370 over the NEW character's name (always the
    // hash, in SP too), then "<hash>" + "." + "fl" (.rdata:06d6aecc / 06d6aea4).
    char HashName[0x104];
    Server_AccountDirName(HashName, Info.Name);
    std::string FileName(HashName);
    FileName.append(".");
    FileName.append("fl");

    // .text:06d4b899 -- _makepath(S+0x18c, NULL, S+0x18c, hash, "fl"); retail
    // uses the same buffer as directory and destination.
    char Path[0x104];
    _makepath(Path, nullptr, AccountDir, HashName, "fl");
    _strlwr(HashName);                                   // .text:06d4b8a3

    // .text:06d4b8a9 .. .text:06d4b901 -- NewChar::TheDB slots 0, 1, 3, 2.
    // Common's Load (Common.dll .text:063149d0) fills retail's four trees as
    // [Faction] -> +0x04 (slot 6 insert, slot 0 find), [Package] -> +0x18
    // (slot 7 / slot 1), [Pilot] -> +0x2c (slot 8 / slot 2), and every faction
    // `base` -> +0x40 as {CreateID(base), base} (slot 9 / slot 3). The port calls
    // those finds by name; common_newchar.h declares them in this slot order
    // (labels corrected 2026-09-24).
    NewChar::IDatabase const* Database = NewChar::TheDB;
    FNewCharFaction const* Faction = Database->FindFaction(Info.Field_0x30);   // S+0x6c
    FNewCharPackage const* Package = Database->FindPackage(Info.Field_0x38);   // S+0x18
    FNewCharExtra const* HomeBase = Database->FindExtra(Info.Field_0x34);      // S+0x1c
    FNewCharPilot const* Pilot = Database->FindPilot(Info.Field_0x3c);         // S+0x14

    // .text:06d4b905 .. .text:06d4b93a -- all four missing: false, no file.
    if (Faction == nullptr && Package == nullptr && HomeBase == nullptr && Pilot == nullptr)
        return false;

    bool bResult = false;                                // S+0x13, .text:06d4b8ba
    {
        SaveGame_Writer Writer;                             // .text:06d4b93f .. 06d4b975
        // .text:06d4b990 .. .text:06d4b9f7 -- path assigned, fopen(path, "w+t").
        if (Writer.Open(Path)) {
            // .text:06d4ba0b .. .text:06d4ba5d -- the template beside the exe.
            char ModulePath[0x104];
            GetModuleFileNameA(nullptr, ModulePath, 0x104);
            char Drive[_MAX_DRIVE];
            char Dir[0x104];
            _splitpath(ModulePath, Drive, Dir, nullptr, nullptr);
            char TemplatePath[0x104];
            _makepath(TemplatePath, Drive, Dir,
                      SinglePlayer() ? "NewCharacter.fl" : "MPNewCharacter.fl", nullptr);

            FSaveFileBuffer Template;                    // S+0x54, SaveGame ctor inlined
            INI_Reader Reader;                           // S+0xd94

            // .text:06d4ba97 -- fopen "rt", then the same FLS1 / FLSG / plain
            // three-way FUN_06d47120 uses.
            bool bOpened = false;
            Template.File = fopen(TemplatePath, "rt");
            if (Template.File != nullptr) {
                char Magic[4] = { 0, 0, 0, 0 };
                fread(Magic, 1, 4, Template.File);
                if (memcmp(Magic, Server_FLS1Magic, 4) == 0) {
                    fclose(Template.File);               // .text:06d4bae3
                    Template.File = fopen(TemplatePath, "rb");
                    if (Template.File != nullptr) {
                        Template.ReadTail(4);         // FUN_06d48960
                        Template.Encode();            // FUN_06d48b60
                        bOpened = true;
                    }
                } else if (memcmp(Magic, "FLSG", 4) != 0) {
                    // .text:06d4bb33 -- plain text from offset 0. "FLSG" instead
                    // jumps straight to .text:06d4bf63, file left open until the
                    // teardown at .text:06d4c086.
                    Template.ReadTail(0);
                    bOpened = true;
                }
            }

            if (bOpened) {
                // .text:06d4bb68
                Reader.open_memory(Template.Contents.c_str(),
                                   static_cast<unsigned int>(Template.Contents.size()));
                // .text:06d4bb6e .. .text:06d4bb8e -- any one missing: close the
                // reader, result stays false, but the (empty) output is still
                // finalized below.
                if (Faction != nullptr && Package != nullptr && HomeBase != nullptr &&
                    Pilot != nullptr) {
                    FILE* const Out = Writer.File;       // [writer+4], .text:06d4bbc2
                    // S+0x994. Retail never clears it between keys: a key with no
                    // values (.text:06d4bd06 JLE straight to the fprintf) prints
                    // whatever the previous key joined. Its first contents are
                    // stack garbage in retail; "" here is the only defined choice.
                    char Joined[0x400];
                    Joined[0] = '\0';
                    while (Reader.read_header()) {
                        fprintf(Out, "\n[%s]\n", Reader.get_header_ptr());      // .text:06d4bbcd
                        while (Reader.read_value()) {
                            char const* const Key = Reader.get_name_ptr();      // S+0x68
                            if (_stricmp(Key, "%%Package%%") == 0) {
                                // .text:06d4bc0f -- ship, then the loadout.
                                // package +0x10 / +0x20, each through the c_str idiom.
                                fprintf(Out, "%s = %s\n", "ship_archetype", Package->Ship.c_str());
                                // No null test on Loadout::Get (.text:06d4bc4e reads [EAX+8]).
                                Loadout::Map const* Map =
                                    Loadout::Get(Loadout::GetID(Package->Loadout.c_str()));
                                for (std::vector<EquipDesc>::const_iterator It = Map->Items.begin();
                                     It != Map->Items.end(); ++It) {
                                    // .text:06d4bc60 -- stride 0x20, byte +0xc picks
                                    // equip vs cargo; an unknown arch is skipped.
                                    Archetype::Equipment const* Equipment =
                                        Archetype::GetEquipment(It->ArchId);
                                    if (Equipment == nullptr)
                                        continue;
                                    char Line[0x100];
                                    if (It->bEquipped) {
                                        sprintf(Line, "%u, %s", Equipment->Nickname.Id, It->Hardpoint);
                                        fprintf(Out, "%s = %s\n", "equip", Line);
                                    } else {
                                        sprintf(Line, "%u, %d", Equipment->Nickname.Id, It->Count);
                                        fprintf(Out, "%s = %s\n", "cargo", Line);
                                    }
                                }
                                continue;
                            }

                            // .text:06d4bcee -- every value, substituted, joined
                            // with ", " (.rdata:06d6b564).
                            int const Count = static_cast<int>(Reader.get_num_parameters());
                            for (int Index = 0; Index < Count; ++Index) {
                                if (Index == 0)
                                    Joined[0] = '\0';
                                else
                                    strcat(Joined, ", ");
                                char const* Value = Reader.get_value_string(static_cast<unsigned int>(Index));
                                char Scratch[0x400];                              // S+0x594
                                if (Value[0] == '%') {
                                    if (_stricmp(Value, "%%Name%%") == 0) {
                                        WideToHex(Info.Name, Scratch);            // .text:06d4bd7d
                                        Value = Scratch;
                                    } else if (_stricmp(Value, "%%Faction%%") == 0) {
                                        Value = Faction->RepGroup.c_str();        // faction +0x08 (the string's pointer)
                                    } else if (_stricmp(Value, "%%Money%%") == 0) {
                                        sprintf(Scratch, "%u", static_cast<unsigned int>(Package->Money)); // package +0x2c
                                        Value = Scratch;
                                    } else if (_stricmp(Value, "%%Base_Costume%%") == 0) {
                                        Value = Pilot->Body.c_str();              // pilot +0x08
                                    } else if (_stricmp(Value, "%%Comm_Costume%%") == 0) {
                                        Value = Pilot->Comm.c_str();              // pilot +0x28
                                    } else if (_stricmp(Value, "%%Voice%%") == 0) {
                                        Value = Pilot->Voice.c_str();             // pilot +0x48
                                    } else if (_stricmp(Value, "%%Home_System%%") == 0) {
                                        // .text:06d4be3d -- record +0x00 is the key the
                                        // find matched; a missing base or system keeps
                                        // the literal placeholder (.text:06d4be4f /
                                        // .text:06d4be64 jump past the store).
                                        Universe::IBase const* Base = Universe::get_base(Info.Field_0x34);
                                        if (Base != nullptr) {
                                            // IBase vtable +0x10 / ISystem vtable +0x08 --
                                            // the port's plain SystemId / Nickname fields.
                                            Universe::ISystem const* System =
                                                Universe::get_system(Base->SystemId);
                                            if (System != nullptr)
                                                Value = System->Nickname;
                                        }
                                    } else if (_stricmp(Value, "%%Home_Base%%") == 0) {
                                        // .text:06d4be80-06d4be8b: [record+0x08] is the
                                        // string's pointer, read through the c_str idiom.
                                        Value = HomeBase->Value.c_str();
                                    }
                                }
                                strcat(Joined, Value);                            // .text:06d4be91
                            }
                            fprintf(Out, "%s = %s\n", Key, Joined);               // .text:06d4beee
                        }
                        fprintf(Out, "\n");                                       // .rdata:06d6af50
                    }
                    bResult = true;                                               // .text:06d4bf3f
                }
                Reader.close();                                                   // .text:06d4bf46 / 06d4bf5b
            }

            // .text:06d4bf69 .. .text:06d4bfea -- the writer's encode-and-rewrite,
            // on every path that opened the output.
            Writer.Close();

            // .text:06d4bfee .. .text:06d4c0b5 -- ~INI_Reader, close the template.
            if (Template.File != nullptr) {
                fclose(Template.File);
                Template.File = nullptr;
            }
        }

        if (bResult) {
            // .text:06d4c0d7 -- this->FUN_06d4b5a0("<hash>.fl").
            PlayerData_ReloadSavedCharacter(Slot, FileName.c_str());
            if (!SinglePlayer()) {
                FDUMP(0x100003, "LOG: Create character on account %S (\"%s\")\n",
                      Slot.AccountName, Path);                                    // .text:06d4c0ff
            }
        }
        // .text:06d4c104 -- the writer's destructor; its FILE* is already null.
    }
    return bResult;
}

// DeleteCharacterSave / CharacterSaveExists were removed 2026-09-24
// (claude:srv-save-2). Both built a path from a bare character NAME under the
// accounts root, which retail never does. IServerImpl::DestroyCharacter now
// runs retail's FUN_06d49f60 (PlayerData_DestroyCharacter, server_classes.cpp),
// and CharacterSaveExists had no caller.
