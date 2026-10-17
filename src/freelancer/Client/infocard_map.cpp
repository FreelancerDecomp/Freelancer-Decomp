// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "infocard_map.h"

#include "common_ini.h"   // INI_Reader
#include "common_misc.h"  // GetFileSysPath

FInfocardMap* G_InfocardMap = nullptr; // DAT_0066d320

void FInfocardMap::Load()
{
    // FUN_004583a0: GetFileSysPath of "interface\InfocardMap.ini", open, then every header.
    char Path[0x100];
    GetFileSysPath(Path, "interface\\InfocardMap.ini", nullptr);
    INI_Reader Reader;
    if (!Reader.open(Path, false) || !Reader.read_header())
    {
        return;
    }
    do
    {
        // 0x00458427..0x0045844c: only [InfocardMapTable], and only when it has a value.
        if (!Reader.is_header("InfocardMapTable") || !Reader.read_value())
        {
            continue;
        }
        do
        {
            if (!Reader.is_value("map"))
            {
                continue;
            }
            // 0x0045846c..0x0045851e: key = ftol(value 0), ExtraIds = ftol(value 1); values
            // 2..4 are read only when not empty (INI_Reader::is_value_empty), else 1.
            unsigned const Key = static_cast<unsigned>(static_cast<int>(Reader.get_value_float(0)));
            FInfocardMapEntry Entry;
            Entry.ExtraIds = static_cast<int>(Reader.get_value_float(1));
            Entry.Field_0x04 = 1;
            Entry.Field_0x08 = 1;
            Entry.Field_0x0c = 1;
            if (!Reader.is_value_empty(2))
            {
                Entry.Field_0x04 = static_cast<int>(Reader.get_value_float(2));
            }
            if (!Reader.is_value_empty(3))
            {
                Entry.Field_0x08 = static_cast<int>(Reader.get_value_float(3));
            }
            if (!Reader.is_value_empty(4))
            {
                Entry.Field_0x0c = static_cast<int>(Reader.get_value_float(4));
            }
            // 0x00458520..0x00458615: the unique insert (FUN_00458d00) -- a key already in the
            // tree keeps its first line (0x00458602).
            Entries.insert(std::pair<unsigned const, FInfocardMapEntry>(Key, Entry));
        } while (Reader.read_value());
    } while (Reader.read_header());
    // 0x00458649..: INI_Reader's FileMap / buffer teardown -- its destructor here.
}

void InfocardMapCreate()
{
    // FUN_00458790: operator_new(0x14), the empty map, FUN_004583a0, stored in DAT_0066d320.
    if (G_InfocardMap != nullptr)
    {
        return;
    }
    FInfocardMap* const Map = new FInfocardMap();
    Map->Load();
    G_InfocardMap = Map;
}

void InfocardMapDestroy()
{
    // FUN_00458860: FUN_004582e0 (clear) + FUN_005b7e1d, then DAT_0066d320 = 0.
    FInfocardMap* const Map = G_InfocardMap;
    if (Map == nullptr)
    {
        return;
    }
    delete Map;
    G_InfocardMap = nullptr;
}

bool InfocardMapFind(unsigned Key, FInfocardMapEntry const** OutEntry)
{
    // FUN_00458890: no singleton -> false; lower_bound (FUN_0052c7a0), end or Key < node key is
    // a miss; a hit writes &node +0x10.
    FInfocardMap const* const Map = G_InfocardMap;
    if (Map == nullptr)
    {
        return false;
    }
    std::map<unsigned, FInfocardMapEntry>::const_iterator const It = Map->Entries.find(Key);
    if (It == Map->Entries.end())
    {
        return false;
    }
    *OutEntry = &It->second;
    return true;
}
