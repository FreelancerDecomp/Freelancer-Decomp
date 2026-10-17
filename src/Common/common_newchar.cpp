// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_newchar.h"

#include "common_ini.h"

extern "C"
{
    __declspec(dllimport) extern void(__cdecl* FDUMP)(int flags, const char* fmt, ...);
}

namespace
{
    const char kNewCharSource[] = "E:\\FL\\Scratch\\Source\\Common\\NewCharDB.cpp";

    NewChar::Database GNewCharDatabase;
}

NewChar::IDatabase* NewChar::TheDB = &GNewCharDatabase;

NewChar::Database::Database()
{
}

NewChar::Database::~Database()
{
    Shutdown();
}

bool NewChar::Database::Load(char const* Path)
{
    // Retail LAB_063149d0 (slot 14). Every section is parsed inline into a stack record that is
    // handed to its insert slot through the vtable (slots 6 / 7 / 8, and slot 9 for each
    // faction base).
    INI_Reader Reader;
    if (!Reader.open(Path, false))
    {
        // .text:06315544..06315562.
        FDUMP(0x100001, "%s(%d) : *** ERROR: failed to open configuration file (%s)\n",
              kNewCharSource, 0x1cd, Path);
        return false;
    }

    // The [Package] record's +0x04 / +0x08 / +0x2c ([ESP+0xa8] / [ESP+0xac] / [ESP+0xd0]) are
    // never initialised (.text:06314da6..06314df3 builds only the id and the two strings), and no
    // other section writes that stack range, so a [Package] missing strid_name / strid_desc /
    // money keeps the previous [Package]'s value. The first one's start value is whatever the
    // stack held; the port starts from 0.
    int PackageStridName = 0;
    int PackageStridDesc = 0;
    int PackageMoney = 0;

    while (Reader.read_header())
    {
        if (Reader.is_header("Faction"))
        {
            // .text:06314a59..06314d42: the 0x50-byte record (ID_String::clear on the id, an
            // empty rep_group string, three empty sets), one branch per key, then slot 6.
            FNewCharFaction Faction;
            Faction.NicknameId = 0;
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    // .text:06314b10..06314b1e.
                    Faction.NicknameId = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("rep_group"))
                {
                    // .text:06314b3f..06314b8b: the string assigned (strlen, grow, copy).
                    Faction.RepGroup.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("base"))
                {
                    // .text:06314ba8..06314c3c: a {CreateID(base), base} record through slot 9
                    // (`CALL [EAX + 0x24]`); Server.dll's FUN_06d4b790 finds it by the base id
                    // the client sends (slot 3) and writes its string as %%Home_Base%%.
                    FNewCharExtra Extra;
                    Extra.Id = CreateID(Reader.get_value_string());
                    Extra.Value.assign(Reader.get_value_string());
                    InsertExtra(Extra);
                    // .text:06314c3f..06314c5b: then that record's id into the base set (+0x14).
                    Faction.Bases.insert(Extra.Id);
                }
                else if (Reader.is_value("package"))
                {
                    // .text:06314c8e..06314cc8 -> FUN_063167e0 on +0x28.
                    Faction.Packages.insert(CreateID(Reader.get_value_string()));
                }
                else if (Reader.is_value("pilot"))
                {
                    // .text:06314cdf..06314d19 -> FUN_063167e0 on +0x3c.
                    Faction.Pilots.insert(CreateID(Reader.get_value_string()));
                }
            }
            // .text:06314d36..06314d42: slot 6; its answer is not read.
            InsertFaction(Faction);
        }
        else if (Reader.is_header("Package"))
        {
            // .text:06314da6..06314fb2: the 0x30-byte record (see above for +0x04 / +0x08 /
            // +0x2c), one branch per key, then slot 7.
            FNewCharPackage Package;
            Package.NicknameId = 0;
            Package.StridName = PackageStridName;
            Package.StridDesc = PackageStridDesc;
            Package.Money = PackageMoney;
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    // .text:06314e24..06314e32: only the id is kept.
                    Package.NicknameId = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("strid_name"))
                {
                    // .text:06314e53..06314e5e: ftol(get_value_float(0)).
                    Package.StridName = (int)Reader.get_value_float(0);
                }
                else if (Reader.is_value("strid_desc"))
                {
                    // .text:06314e7f..06314e8a.
                    Package.StridDesc = (int)Reader.get_value_float(0);
                }
                else if (Reader.is_value("ship"))
                {
                    // .text:06314eab..06314ef7: assign (strlen, grow, copy, eos) into +0x0c.
                    Package.Ship.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("loadout"))
                {
                    // .text:06314f17..06314f5f: into +0x1c.
                    Package.Loadout.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("money"))
                {
                    // .text:06314f75..06314f87.
                    Package.Money = (int)Reader.get_value_float(0);
                }
            }
            PackageStridName = Package.StridName;
            PackageStridDesc = Package.StridDesc;
            PackageMoney = Package.Money;
            // .text:06314fa6..06314fb2: slot 7; its answer is not read.
            InsertPackage(Package);
        }
        else if (Reader.is_header("Pilot"))
        {
            // .text:06314ff1..0631543c: the 0x64-byte record (the id cleared, every string and
            // both vectors empty), one branch per key, then slot 8.
            FNewCharPilot Pilot;
            Pilot.NicknameId = 0;
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    // .text:063150bc..063150ca: only the id is kept.
                    Pilot.NicknameId = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("body"))
                {
                    // .text:063150e8..0631512e: assign into +0x04.
                    Pilot.Body.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("body.anim"))
                {
                    // .text:06315148..06315202: the vector emptied (FUN_06316c60 erase(begin,
                    // end)), then every parameter up to the first empty one appended through a
                    // temporary string (FUN_06316ae0 insert at end).
                    Pilot.BodyAnims.clear();
                    unsigned ParamIndex = 0;
                    while (!Reader.is_value_empty(ParamIndex))
                    {
                        std::string const Anim(Reader.get_value_string(ParamIndex));
                        ParamIndex += 1;
                        Pilot.BodyAnims.push_back(Anim);
                    }
                }
                else if (Reader.is_value("comm"))
                {
                    // .text:06315223..06315269: assign into +0x24.
                    Pilot.Comm.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("comm.anim"))
                {
                    // .text:06315280..06315340: as body.anim, on +0x34.
                    Pilot.CommAnims.clear();
                    unsigned ParamIndex = 0;
                    while (!Reader.is_value_empty(ParamIndex))
                    {
                        std::string const Anim(Reader.get_value_string(ParamIndex));
                        ParamIndex += 1;
                        Pilot.CommAnims.push_back(Anim);
                    }
                }
                else if (Reader.is_value("voice"))
                {
                    // .text:06315361..063153ad: assign into +0x44.
                    Pilot.Voice.assign(Reader.get_value_string());
                }
                else if (Reader.is_value("thumb"))
                {
                    // .text:063153c3..06315418: assign into +0x54.
                    Pilot.Thumb.assign(Reader.get_value_string());
                }
            }
            // .text:06315433..0631543c: slot 8; its answer is not read.
            InsertPilot(Pilot);
        }
    }

    return true;
}

bool NewChar::Database::Slot15(void* Arg)
{
    // Retail slot 15 (vcall +0x3c) folded by /OPT:ICF onto 0x0629ba10, whose
    // entire body is `XOR AL,AL ; RET 0x4` -- it unconditionally returns false
    // and ignores its one argument. That is the whole observable behaviour;
    // the slot's meaning is still unknown and the symbol the linker left on
    // that address (CEShield::GetEquipDesc) belongs to an unrelated class.
    // TODO: identify slot 15 from a caller outside Common.dll.
    (void)Arg;
    return false;
}

void NewChar::Database::Shutdown()
{
    // Retail LAB_06313fd0 clears this+0x04 (.text:06313fdd, Factions),
    // +0x18 (.text:06313ff1, Packages), +0x2c (.text:0631400a, Pilots), then
    // the 4th collection (+0x40, .text:06314078) in that order.
    Factions.clear();
    Packages.clear();
    Pilots.clear();
    Extras.clear();
}

FNewCharPilot const* NewChar::Database::FindPilot(unsigned NicknameId) const
{
    std::map<unsigned, FNewCharPilot>::const_iterator It = Pilots.find(NicknameId);
    if (It == Pilots.end())
        return nullptr;
    return &It->second;
}

FNewCharPackage const* NewChar::Database::FindPackage(unsigned NicknameId) const
{
    std::map<unsigned, FNewCharPackage>::const_iterator It = Packages.find(NicknameId);
    if (It == Packages.end())
        return nullptr;
    return &It->second;
}

FNewCharFaction const* NewChar::Database::FindFaction(unsigned NicknameId) const
{
    std::map<unsigned, FNewCharFaction>::const_iterator It = Factions.find(NicknameId);
    if (It == Factions.end())
        return nullptr;
    return &It->second;
}

bool NewChar::Database::ForEachFaction(bool(__cdecl* Callback)(unsigned, FNewCharFaction const*, void*), void* Context) const
{
    // Retail LAB_06313f50 (slot 4, vcall +0x10): in-order walk of the tree at
    // this+0x04 (`MOV EAX,[ECX+0x8]`), which Load fills from [Faction]
    // (slot 6) -- the Factions, not the Pilots (corrected 2026-09-24). std::map
    // already iterates in ascending key order, matching the retail RB-tree walk
    // -- see common_newchar.h class comment. Retail calls the callback
    // (`CALL EBX`, .text:06313f77) with no null test; neither does the port.
    for (std::map<unsigned, FNewCharFaction>::const_iterator It = Factions.begin(); It != Factions.end(); ++It)
    {
        if (!Callback(It->first, &It->second, Context))
            return false;
    }
    return true;
}

bool NewChar::Database::RemovePilot(unsigned NicknameId)
{
    // Retail FUN_06314910 (slot 12, vcall +0x30, tree this+0x2c): erase-by-key,
    // false if absent.
    return Pilots.erase(NicknameId) != 0;
}

bool NewChar::Database::RemovePackage(unsigned NicknameId)
{
    // Retail FUN_063148b0 (slot 11, vcall +0x2c): erase-by-key, false if absent.
    return Packages.erase(NicknameId) != 0;
}

bool NewChar::Database::RemoveFaction(unsigned NicknameId)
{
    // Retail LAB_06314850 (slot 10, vcall +0x28, tree this+0x04): erase-by-key,
    // false if absent.
    return Factions.erase(NicknameId) != 0;
}

bool NewChar::Database::InsertExtra(FNewCharExtra const& Extra)
{
    // Retail LAB_063146c0 (slot 9, vcall +0x24, RET 4): the key (+0) found in the tree at
    // this+0x40 -> false; else FUN_06316330 (operator[]) makes the node, the id is copied to
    // the record's +0 (.text:0631471b-0631471d) and the string (+4) assigned -> true.
    if (Extras.find(Extra.Id) != Extras.end())
        return false;
    FNewCharExtra& Record = Extras[Extra.Id];
    Record.Id = Extra.Id;
    Record.Value = Extra.Value;
    return true;
}

bool NewChar::Database::InsertFaction(FNewCharFaction const& Faction)
{
    // Retail LAB_06314100 (slot 6, vcall +0x18, RET 4): the key (+0) found in the tree at
    // this+0x04 -> false; else a new node: the id, the string at +4 assigned, the three sets
    // (+0x14 / +0x28 / +0x3c) copied -> true.
    if (Factions.find(Faction.NicknameId) != Factions.end())
        return false;
    Factions[Faction.NicknameId] = Faction;
    return true;
}

bool NewChar::Database::InsertPackage(FNewCharPackage const& Package)
{
    // Retail LAB_06314280 (slot 7, vcall +0x1c, RET 4): FUN_062fcfb0 lower bound in the tree at
    // this+0x18; not the end and the key not below the node's (JC) -> present -> false
    // (0x063144a7). Else FUN_06315f90 makes the node: +0x00 / +0x04 / +0x08 copied, the strings
    // at +0x0c / +0x1c assigned, +0x2c copied -> true.
    if (Packages.find(Package.NicknameId) != Packages.end())
        return false;
    Packages[Package.NicknameId] = Package;
    return true;
}

bool NewChar::Database::InsertPilot(FNewCharPilot const& Pilot)
{
    // Retail LAB_063144c0 (slot 8, vcall +0x20, RET 4): the same lower-bound test on the tree at
    // this+0x2c -> false (0x063146b2). Else FUN_06316110 makes the node: +0x00 copied, the
    // strings at +0x04 / +0x24 / +0x44 / +0x54 assigned, the vectors at +0x14 (inline
    // operator=) and +0x34 (FUN_06315b90) copied -> true.
    if (Pilots.find(Pilot.NicknameId) != Pilots.end())
        return false;
    Pilots[Pilot.NicknameId] = Pilot;
    return true;
}

bool NewChar::Database::RemoveExtra(unsigned Id)
{
    // Retail FUN_06314970 (slot 13, vcall +0x34): erase-by-key, false if absent.
    return Extras.erase(Id) != 0;
}

FNewCharExtra const* NewChar::Database::FindExtra(unsigned Id) const
{
    // Retail LAB_06313f00 (slot 3, vcall +0x0c): find on the tree at this+0x40; miss -> 0,
    // hit -> node+0x10, the stored {Id, Value} record.
    std::map<unsigned, FNewCharExtra>::const_iterator It = Extras.find(Id);
    if (It == Extras.end())
        return nullptr;
    return &It->second;
}
