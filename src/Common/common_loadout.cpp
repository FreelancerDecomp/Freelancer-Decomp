// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll Loadout (retail Loadouts.cpp): the loadout table read from the loadout INIs.
// Split out of common_goods.cpp.

#include "common_goods.h"
#include "common_id.h"
#include "common_ini.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates
#include "dacom.h"

#include <map>
#include <vector>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    const char kLoadoutSource[] = "E:\\FL\\Scratch\\Source\\Common\\Loadouts.cpp";

    // Loadout::ReadEquipLine / ReadCargoLine (06311b8c-06311bbf / 06311c43-06311c76): MSVCRT
    // atoi of the token; when that is 0, CreateID of the same token (through the identity
    // Archetype::GetAsteroidID). No null or empty test.
    unsigned ParseArchToken(char const* Text)
    {
        unsigned const AsInt = MsvcrtAtoi(Text);
        if (AsInt != 0)
            return AsInt;
        return CreateID(Text);
    }

    // EquipDescVector::add_equipment_item (0630a0b0) over the std::vector<EquipDesc> that
    // Loadout::Map keeps where retail has an EquipDescVector (Loadout::Load 0631184b is its
    // caller). With bCombinable: find the FIRST element with the same arch; if mission byte,
    // owner or hardpoint (DACOM stricmp) differ, the scan restarts from the beginning
    // (JNZ 0630a0d0) -- it never moves past that element, so such a line hangs, as in retail.
    // On a full match the health-aware fold, x87 compares TEST AH,0x5 + JP = "not below 1.0"
    // (unordered counts as not below). Otherwise the item is appended and 0 returned.
    int AddEquipmentItem(std::vector<EquipDesc>& Items, EquipDesc const& Desc, bool bCombinable)
    {
        if (bCombinable)
        {
            for (;;)
            {
                EquipDesc* Match = nullptr;
                size_t Index = 0;
                for (Index = 0; Index < Items.size(); ++Index)
                {
                    if (Items[Index].ArchId == Desc.ArchId)
                    {
                        Match = &Items[Index];
                        break;
                    }
                }
                if (Match == nullptr)
                    break;

                if (Match->bMission != Desc.bMission || Match->Owner != Desc.Owner ||
                    CallDacomStricmp(Match->Hardpoint, Desc.Hardpoint) != 0)
                    continue;

                if (!(Match->Health < 1.0f))
                {
                    // 0630a19e: counts summed, the incoming health taken.
                    Match->Count += Desc.Count;
                    Match->Health = Desc.Health;
                }
                else if (!(Desc.Health < 1.0f))
                {
                    // 0630a18a.
                    Match->Count += Desc.Count;
                }
                else
                {
                    // 0630a147-0630a181: the sum stays on the x87 stack; a sum of two floats
                    // is exact in a double.
                    double const Sum = (double)Match->Health + Desc.Health;
                    int const Total = Desc.Count + Match->Count;
                    if (!(Sum < 1.0))
                    {
                        Match->Count = Total;
                        Match->Health = (float)(Sum - 1.0);
                    }
                    else
                    {
                        Match->Health = (float)Sum;
                        Match->Count = Total - 1;
                    }
                }
                return 1;
            }
        }

        Items.push_back(Desc);
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Loadout
// ---------------------------------------------------------------------------

namespace
{
    // DAT_063fd2a8: the loadout table, a map holding each Loadout::Map by value (the node's
    // value starts at node+0x10; Free's FUN_06311a90 destroys it in place).
    std::map<unsigned, Loadout::Map> GLoadouts;
}

// 06311b70. equipped, count 1, the arch (MSVCRT atoi of value 0, or CreateID of it when that
// gives 0), value 1 StringAlloc'd as the hardpoint or BAY when empty, value 2 the health.
bool __cdecl Loadout::ReadEquipLine(INI_Reader& Reader, EquipDesc& Desc)
{
    Desc.set_equipped(true);
    Desc.set_count(1);
    Desc.set_arch_id(ParseArchToken(Reader.get_value_string(0)));

    if (Reader.is_value_empty(1))
    {
        Desc.make_internal();
    }
    else
    {
        CacheString Hp;
        Hp.Value = StringAlloc(Reader.get_value_string(1), false);
        Desc.set_hardpoint(Hp);
    }

    if (!Reader.is_value_empty(2))
        Desc.set_status(Reader.get_value_float(2));

    return true;
}

// 06311c30. Not equipped; arch as ReadEquipLine; value 1 the count; value 2 the hardpoint --
// except that a value 2 whose atof is not 0.0 (.rdata:0639c430, TEST AH,0x44 + JP = not
// equal) ends the line there, before health and mission are read.
bool __cdecl Loadout::ReadCargoLine(INI_Reader& Reader, EquipDesc& Desc)
{
    Desc.set_equipped(false);
    Desc.set_arch_id(ParseArchToken(Reader.get_value_string(0)));

    if (!Reader.is_value_empty(1))
        Desc.set_count(Reader.get_value_int(1));

    if (Reader.is_value_empty(2))
    {
        Desc.make_internal();
    }
    else
    {
        if (atof(Reader.get_value_string(2)) != 0.0)
            return true;
        CacheString Hp;
        Hp.Value = StringAlloc(Reader.get_value_string(2), false);
        Desc.set_hardpoint(Hp);
    }

    if (!Reader.is_value_empty(3))
        Desc.set_status(Reader.get_value_float(3));

    if (!Reader.is_value_empty(4))
        Desc.bMission = Reader.get_value_bool(4);

    return true;
}

// 063115e0 ?Load@Loadout@@YAHPBD_N@Z. The path is opened as given (no GetFileSysPath); the
// bool argument is never read (no access to [ESP+0x1614] in the body). Returns the growth of
// the loadout map. Each [Loadout] block starts with no current loadout; "nickname" while one
// is current is ignored, otherwise it inserts {key id, Map{NicknameId 0 (a cleared ID_String),
// no items, EquipIdMaker()}} (06311729), then stores the id into the node's NicknameId
// (06311755) whether or not the key was new; an existing entry also has its items erased and
// its id maker reset (06311757 tests the insert's bool). "equip" and "cargo" use the current
// loadout without a null test (before any nickname that is a null dereference, as in retail).
// "archetype" is tested and ignored. The reader is not closed explicitly.
int __cdecl Loadout::Load(char const* Path, bool bReload)
{
    (void)bReload;
    size_t const Saved = GLoadouts.size();

    INI_Reader Reader;
    if (Reader.open(Path, false))
    {
        while (Reader.read_header())
        {
            if (!Reader.is_header("Loadout"))
                continue;

            Loadout::Map* Current = nullptr;
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    if (Current != nullptr)
                        continue;

                    unsigned const NicknameId = CreateID(Reader.get_value_string());
                    Loadout::Map Fresh;
                    Fresh.NicknameId = 0;
                    std::pair<std::map<unsigned, Loadout::Map>::iterator, bool> const Inserted =
                        GLoadouts.insert(std::pair<unsigned const, Loadout::Map>(NicknameId, Fresh));
                    Current = &Inserted.first->second;
                    Current->NicknameId = NicknameId;
                    if (!Inserted.second)
                    {
                        Current->Items.clear();
                        Current->IdMaker.Reset();
                    }
                }
                else if (Reader.is_value("equip"))
                {
                    // 063117a4-063117e2: a default EquipDesc appended, read in place, then
                    // given the next EquipIdMaker id.
                    Current->Items.push_back(EquipDesc());
                    ReadEquipLine(Reader, Current->Items.back());
                    Current->Items.back().set_id(Current->IdMaker.CreateEquipID());
                }
                else if (Reader.is_value("cargo"))
                {
                    // 06311801-06311863: EquipDescVector::add_equipment_item with
                    // combinable = arch_is_combinable(arch) && is_internal(); a fresh id only
                    // when it appended (returned 0).
                    EquipDesc Desc;
                    ReadCargoLine(Reader, Desc);
                    bool const bCombinable = arch_is_combinable(Desc.ArchId) && Desc.is_internal();
                    if (AddEquipmentItem(Current->Items, Desc, bCombinable) == 0)
                        Current->Items.back().set_id(Current->IdMaker.CreateEquipID());
                }
                else
                {
                    Reader.is_value("archetype");
                }
            }
        }
    }
    else
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: unable to open Loadout file %s", kLoadoutSource,
              0xb3, Path);
    }

    return (int)(GLoadouts.size() - Saved);
}

// .text:06311ab0: every node erased, its Map destroyed in place (FUN_06311a90 frees the item
// buffer), then the head re-linked to itself and the size zeroed.
void __cdecl Loadout::Free()
{
    GLoadouts.clear();
}

Loadout::Map const* __cdecl Loadout::Get(unsigned int Id)
{
    std::map<unsigned, Loadout::Map>::iterator Found = GLoadouts.find(Id);
    if (Found == GLoadouts.end())
        return nullptr;
    return &Found->second;
}

unsigned int __cdecl Loadout::GetID(char const* Name)
{
    return CreateID(Name);
}
